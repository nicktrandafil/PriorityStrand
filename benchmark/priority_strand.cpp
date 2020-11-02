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

#include <benchmark/benchmark.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <thread>

using namespace boost::asio;

void bm_strand(benchmark::State& state) {
    io_context io;
    strand strand(io.get_executor());
    steady_timer timer(io.get_executor(), std::chrono::seconds(100));
    timer.async_wait([](auto&&...) {});
    std::thread t1([&io] { io.run(); });
    std::thread t2([&io] { io.run(); });

    for (auto _ : state) {
        std::atomic<size_t> async_count{};
        std::atomic<size_t> strand_count{};

        for (size_t i = 0; i < 10; ++i) {
            post(io, [&async_count] { ++async_count; });
            post(strand, [&strand_count] { ++strand_count; });
            post(strand, [&strand_count] { ++strand_count; });
        }

        while (async_count.load() != 10 || strand_count != 20)
            ;
    }

    timer.cancel();
    t1.join();
    t2.join();
}

BENCHMARK(bm_strand);

void bm_priority_strand(benchmark::State& state) {
    io_context io;
    PriorityStrand strand(io.get_executor());
    steady_timer timer(io.get_executor(), std::chrono::seconds(100));
    timer.async_wait([](auto&&...) {});
    std::thread t1([&io] { io.run(); });
    std::thread t2([&io] { io.run(); });

    for (auto _ : state) {
        std::atomic<size_t> async_count{};
        std::atomic<size_t> strand_count{};
        std::atomic<size_t> priority_strand_count{};

        for (size_t i = 0; i < 10; ++i) {
            post(io, [&async_count] { ++async_count; });
            post(strand, [&strand_count] { ++strand_count; });
            post(strand,
                 Prioritized([&priority_strand_count] { ++priority_strand_count; }));
        }

        while (async_count != 10 || strand_count != 10 || priority_strand_count != 10)
            ;
    }

    timer.cancel();
    t1.join();
    t2.join();
}

BENCHMARK(bm_priority_strand);

BENCHMARK_MAIN();
