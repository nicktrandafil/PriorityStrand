/*
  MIT License

  Copyright (c) 2020 Nicolai Trandafil

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#pragma once

#include <boost/asio/detail/work_dispatcher.hpp>
#include <boost/asio/executor.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <thread>

namespace boost::asio {

/// Prioritized handler
/// Wrap the handler with this tag type in order for `PriorityStrand` to prioritize the
/// handler
template <class Function>
struct Prioritized {
    explicit Prioritized(Function&& f)
            : f(std::move(f)) {
    }

    void operator()() {
        f();
    }

    Function f;
};

template <class Function>
Prioritized(Function &&)->Prioritized<Function>;

namespace detail {

template <class Function>
struct IsPrioritizedImpl : std::false_type {};

template <class Function>
struct IsPrioritizedImpl<Prioritized<Function>> : std::true_type {};

template <class Function>
struct IsPrioritized;

template <class Function>
struct IsPrioritized<detail::work_dispatcher<Function>> : IsPrioritizedImpl<Function> {};

class SpinLock {
public:
    void lock() noexcept {
        Sleepyhead sleepy;
        while (locked.test_and_set(std::memory_order_acquire)) { sleepy.wait(); }
    }

    void unlock() noexcept {
        locked.clear(std::memory_order_release);
    }

private:
    class Sleepyhead {
    public:
        Sleepyhead() noexcept
                : spin_count(0) {
        }

        void wait() noexcept {
            if (spin_count < max_spin) {
                ++spin_count;
                pause();
            } else {
                sleep();
            }
        }

    private:
        static void sleep() noexcept {
            std::this_thread::sleep_for(std::chrono::nanoseconds(500000)); // 0.5ms
        }

#if defined(__i386__) || defined(__x86_64__) || defined(_M_X64)
        static inline void pause() noexcept {
            asm volatile("pause");
        }
#else
#error "Provide 'pause' for the platform"
#endif

    private:
        constexpr static uint32_t max_spin = 4000;
        uint32_t spin_count;
    };

private:
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
};

class PriorityStrandService
        : public detail::execution_context_service_base<PriorityStrandService> {
public:
    explicit PriorityStrandService(execution_context& ctx)
            : detail::execution_context_service_base<PriorityStrandService>(
                    ctx) /*noexcept*/ {
    }

    ~PriorityStrandService() noexcept override {
        assert(impl_list.empty()); // Invariant2 assert
        for (auto impl : impl_list) { impl->service = nullptr; }
    }

    class PriorityStrandImpl {
    public:
        // Full access
        friend PriorityStrandService;

        explicit PriorityStrandImpl(PriorityStrandService* service) noexcept
                : service(service)
                , shutdown(false)
                , locked(false)
                , total_in(0)
                , total_out(0) {
        }

        ~PriorityStrandImpl() noexcept {
            assert(service);
            std::scoped_lock lock(service->mutex);
            // Invariant1 impl.
            service->impl_list.erase(std::remove(service->impl_list.begin(),
                                                 service->impl_list.end(), this),
                                     service->impl_list.end());
        }

    private:
        // Owned by Boost.Asio. Invariant2: should never be `nullptr`.
        PriorityStrandService* service;
        detail::op_queue<detail::scheduler_operation> queue;
        detail::op_queue<detail::scheduler_operation> priority_queue;
        bool shutdown;
        bool locked;
        uint64_t total_in;
        uint64_t total_out;
        SpinLock mutex;
    };

    using ImplementationType = std::shared_ptr<PriorityStrandImpl>;

    template <typename Executor>
    class invoker {
    public:
        invoker(ImplementationType const& impl, Executor& ex) noexcept
                : impl(impl)
                , work(ex) {
        }

        invoker(invoker const& other) noexcept
                : impl(other.impl)
                , work(other.work) {
        }

        invoker(invoker&& other) noexcept
                : impl(std::move(other.impl))
                , work(std::move(other.work)) {
        }

        struct on_invoker_exit {
            invoker* this_;

            ~on_invoker_exit() /*noexcept*/ {
                this_->impl->mutex.lock();
                auto const more_handlers = this_->impl->locked =
                        !this_->impl->queue.empty()
                        || !this_->impl->priority_queue.empty();
                this_->impl->mutex.unlock();

                if (more_handlers) {
                    Executor ex(this_->work.get_executor());
                    detail::recycling_allocator<void> allocator;
                    ex.post(std::move(*this_), allocator);
                }
            }
        };

        void operator()() {
            detail::call_stack<PriorityStrandImpl>::context ctx(impl.get());

            on_invoker_exit on_exit = {this};
            (void)on_exit;

            constexpr size_t max_work_count = 30;

            boost::system::error_code ec;
            size_t work_count{0};
            bool empty{false};
            while (work_count < max_work_count && !empty) {
                if (auto lock = std::unique_lock(impl->mutex);
                    auto const o = impl->priority_queue.front()) {
                    impl->priority_queue.pop();
                    lock.unlock();
                    o->complete(impl.get(), ec, 0);
                    ++work_count;
                    continue;
                }

                if (auto lock = std::unique_lock(impl->mutex);
                    auto const o = impl->queue.front()) {
                    impl->queue.pop();
                    ++impl->total_out;
                    lock.unlock();
                    o->complete(impl.get(), ec, 0);
                    ++work_count;
                } else {
                    empty = true;
                }
            }
        }

    private:
        ImplementationType impl;
        executor_work_guard<Executor> work;
    };

    ImplementationType create_implementation() /*noexcept*/ {
        std::scoped_lock lock(mutex);
        auto impl = std::make_shared<PriorityStrandImpl>(this);
        impl_list.push_back(impl.get());
        return impl;
    }

    template <typename Executor, typename Function, typename Allocator>
    static void post(ImplementationType const& impl,
                     Executor& ex,
                     Function&& function,
                     Allocator const& a) /*noexcept*/ {
        using FunctionType = std::decay_t<Function>;

        // Allocate and construct an operation to wrap the function.
        using Op = detail::executor_op<FunctionType, Allocator>;
        typename Op::ptr p = {std::addressof(a), Op::ptr::allocate(a), 0};
        p.p = new (p.v) Op(std::forward<Function>(function), a);

        // Add the function to the strand and schedule the strand if required.
        bool first = enqueue(impl, p.p, IsPrioritized<Function>::value);
        p.v = p.p = 0;
        if (first) { ex.post(invoker<Executor>(impl, ex), a); }
    }

    static uint64_t total_normal_in(ImplementationType const& impl) noexcept {
        impl->mutex.lock();
        auto ret = impl->total_in;
        impl->mutex.unlock();
        return ret;
    }

    static uint64_t total_normal_out(ImplementationType const& impl) noexcept {
        impl->mutex.lock();
        auto ret = impl->total_out;
        impl->mutex.unlock();
        return ret;
    }

    static bool enqueue(ImplementationType const& impl,
                        detail::scheduler_operation* op,
                        bool prioritized) /*noexcept*/ {
        impl->mutex.lock();
        if (impl->shutdown) {
            impl->mutex.unlock();
            op->destroy();
            return false;
        } else if (impl->locked) {
            if (prioritized) {
                impl->priority_queue.push(op);
            } else {
                impl->queue.push(op);
                ++impl->total_in;
            }
            impl->mutex.unlock();
            return false;
        } else {
            impl->locked = true;
            if (prioritized) {
                impl->priority_queue.push(op);
            } else {
                impl->queue.push(op);
                ++impl->total_in;
            }
            impl->mutex.unlock();
            return true;
        }
    }

private:
    void shutdown() noexcept override {
        detail::op_queue<detail::scheduler_operation> tmp;
        std::scoped_lock lock(mutex);
        for (auto impl : impl_list) {
            std::scoped_lock lock(impl->mutex);
            impl->shutdown = true;
            tmp.push(impl->queue);
            tmp.push(impl->priority_queue);
        }
    }

private:
    // Owned by strands. Invariant1: there is no non-existing implementation in this list.
    std::mutex mutex;
    std::vector<PriorityStrandImpl*> impl_list;
};

} // namespace detail

/// Priority Strand
/// Supports two priorities - normal and high. Handlers wrapped with `Prioritized` have
/// high priority. A high priority task is executed as soon as the strand finishes its'
/// current task.
template <typename Executor>
class PriorityStrand {
public:
    PriorityStrand()
            : impl(use_service<detail::PriorityStrandService>(executor.context())
                           .create_implementation()) /*noexcept*/ {
    }

    explicit PriorityStrand(Executor const& e) /*noexcept*/
            : executor(e)
            , impl(use_service<detail::PriorityStrandService>(e.context())
                           .create_implementation()) {
    }

    execution_context& context() const noexcept {
        return executor.context();
    }

    void on_work_started() const noexcept {
        executor.on_work_started();
    }

    void on_work_finished() const noexcept {
        executor.on_work_finished();
    }

    template <typename Function, typename Allocator>
    void dispatch(Function&& f, Allocator const& a) const /*noexcept*/;

    template <typename Function, typename Allocator>
    void post(Function&& f, Allocator const& a) const /*noexcept*/ {
        detail::PriorityStrandService::post(impl, executor, std::forward<Function>(f), a);
    }

    template <typename Function, typename Allocator>
    void defer(Function&& f, Allocator const& a) const /*noexcept*/;

    uint64_t total_normal_in() const noexcept {
        return detail::PriorityStrandService::total_normal_in(impl);
    }

    uint64_t total_normal_out() const noexcept {
        return detail::PriorityStrandService::total_normal_out(impl);
    }

private:
    Executor executor;
    using ImplementationType = detail::PriorityStrandService::ImplementationType;
    ImplementationType impl;
};

} // namespace boost::asio
