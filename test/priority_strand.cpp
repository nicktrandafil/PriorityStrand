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

#include "priority_strand/priority_strand.hpp"

#include <catch2/catch.hpp>

#include <boost/asio.hpp>

#include <vector>

using namespace boost::asio;
using namespace std::chrono;

namespace {

template <class F>
struct ScopeExit {
    explicit ScopeExit(F&& f)
            : f{std::move(f)} {
    }

    void scope_exit() {
        if (!done) {
            f();
            done = true;
        }
    }

    ~ScopeExit() {
        scope_exit();
    }

    bool done{false};
    F f;
};

} // namespace

TEST_CASE("basic") {
    for (size_t i = 0; i < 100; ++i) {
        boost::asio::io_context io;
        PriorityStrand<io_context::executor_type> strand(io.get_executor());

        std::vector<size_t> order;

        for (size_t i = 0; i < 100; ++i) {
            post(strand, [&, i] { order.push_back(i); });
        }

        for (size_t i = 100; i < 200; ++i) {
            defer(strand.high_priority(), [&, i] { order.push_back(i); });
        }

        std::thread workder1([&io] { io.run(); });
        std::thread workder2([&io] { io.run(); });
        workder1.join();
        workder2.join();

        std::vector<size_t> expected(200);
        std::generate(expected.begin(), expected.begin() + 100,
                      [i = size_t(100)]() mutable { return i++; });
        std::generate(expected.begin() + 100, expected.begin() + 200,
                      [i = 0]() mutable { return i++; });

        REQUIRE(expected == order);
    }
}

namespace {

steady_clock::time_point next_interval(steady_clock::time_point now,
                                       milliseconds interval) noexcept {
    auto const since_epoch = duration_cast<milliseconds>(now.time_since_epoch());
    return steady_clock::time_point(milliseconds(since_epoch / interval * interval)
                                    + interval);
}

} // namespace

TEST_CASE("active priority push") {
    constexpr auto interval = milliseconds(10);
    constexpr auto count_max = 10;
    std::atomic<bool> done{false};
    std::atomic<size_t> not_priority_executed{0};
    std::atomic<size_t> not_priority_scheduled{0};

    io_context io;
    steady_timer timer(io.get_executor(), seconds(100));
    timer.async_wait([](auto) {});
    PriorityStrand<io_context::executor_type> strand(io.get_executor());

    std::thread t3([strand, interval, &not_priority_executed, &not_priority_scheduled] {
        for (size_t i = 0; i < count_max; ++i) {
            post(strand, [interval, &not_priority_executed] {
                std::this_thread::sleep_until(
                        next_interval(steady_clock::now(), interval));
                ++not_priority_executed;
            });
            ++not_priority_scheduled;
        }
    });

    std::thread t1([&io] { io.run(); });
    std::thread t2([&io] { io.run(); });

    ScopeExit join([&] {
        timer.cancel();
        t1.join();
        t2.join();
        t3.join();
    });

    post(strand.high_priority(), [&done] { done = true; });

    auto tmp1 = not_priority_executed.load();
    while (true) {
        std::this_thread::sleep_until(next_interval(steady_clock::now(), interval));
        auto tmp2 = not_priority_executed.load();
        // between two ordinar tasks priority should have been executed
        if (tmp2 - tmp1 > 1) { break; }
    }

    INFO("Not prioritized works done " << not_priority_executed << " of " << count_max);
    INFO("Not prioritized works scheduled " << not_priority_scheduled << " of "
                                            << count_max);
    REQUIRE(done);

    join.scope_exit();
    REQUIRE(strand.normal_in() == count_max);
    REQUIRE(strand.normal_out() == count_max);

    timer.cancel();
}

TEST_CASE("timer") {
    io_context io;
    PriorityStrand<io_context::executor_type> strand(io.get_executor());

    bool done{false};

    steady_timer timer(io.get_executor(), seconds(0));
    timer.async_wait(boost::asio::bind_executor(
            strand.high_priority(), [&done](boost::system::error_code) { done = true; }));

    io.run();

    REQUIRE(strand.normal_in() == 0);
    REQUIRE(strand.priority_in() == 1);
    REQUIRE(strand.priority_out() == 1);
}