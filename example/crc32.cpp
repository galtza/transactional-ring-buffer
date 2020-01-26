/*
    MIT License

    Copyright (c) 2016-2020 Raúl Ramos

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
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>
#include <cstdint>
#include <map>
#include <random>
#include <mutex>
#include <future>
#include <sstream>
#include <cstdint>
#include <nmmintrin.h>

#include "transactional-ring-buffer.h"

using namespace std;
using namespace chrono;
using namespace chrono_literals;

#define INTRIN_CRC32 1

/*
    == Helper functions ========
*/

constexpr auto operator""_KiB(unsigned long long int v) -> uint64_t;
constexpr auto operator""_MiB(unsigned long long int v) -> uint64_t;
constexpr auto operator""_GiB(unsigned long long int v) -> uint64_t;

auto crc32(const uint8_t* _buff, uint64_t _len, uint32_t _crc = 0xffFFffFF) -> uint32_t;
auto time_now () -> uint64_t;

/*
    == Global data ========
*/

qcstudio::containers::transactional_ring_buffer<uint64_t> g_rbuffer;
unique_ptr<uint8_t[]> g_data;
uint64_t g_data_size;
mutex g_print_mutex;
auto g_start_time = high_resolution_clock::now();
map<thread::id, string> g_tid2str;
auto g_producer_hash = 0xFFffFFff;
auto g_consumer_hash = 0xFFffFFff;
auto g_failed_writes = 0;
auto g_failed_reads = 0;

/*
    == Producer function (it has its own thread) ========
*/

void producer();
void consumer();

/*
    == Helper functions implementation ========
*/

auto crc32(const uint8_t* _buff, uint64_t _len, uint32_t _crc) -> uint32_t {
#if defined(INTRIN_CRC32) && INTRIN_CRC32
    const auto end = _buff + _len;

    #if defined (_M_X64)
    const auto m = (uint64_t*)_buff + (_len / sizeof(uint64_t));
    auto i = (uint64_t*)_buff;
    for (; i < m; ++i) {
        _crc = (uint32_t)_mm_crc32_u64(_crc, *i);
    }
    #else
    const auto m = (uint32_t*)_buff + (_len / sizeof(uint32_t));
    auto i = (uint32_t*)_buff;
    for (; i < m; ++i) {
        _crc = (uint32_t)_mm_crc32_u32(_crc, *i);
    }
    #endif
    for (auto j = (uint8_t*)i; j < end; ++j) {
        _crc = _mm_crc32_u8(_crc, *j);
    }
#else
    // CRC32 without look-up table
    for (auto i = 0u; i < _len; ++i) {
        uint32_t val = 0xFF & (_crc ^ *(_buff + i));
        for (auto j = 0u; j < 8; j++) {
            val = (val & 1)? (0xEDB88320 ^ (val >> 1)) : (val >> 1);
        }
        _crc = (_crc >> 8) ^ val;
    }
#endif
    return _crc;
}

// Get current time
auto time_now () -> uint64_t {
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

// Non-interleaving debug print
class internal_coutln {

 public:

    internal_coutln() {
        auto elapsed = high_resolution_clock::now() - g_start_time;
        cout << "[" << dec << duration_cast<nanoseconds>(elapsed).count() << "]";
        if (auto it = g_tid2str.find(this_thread::get_id()); it != g_tid2str.end()) {
            cout << "[" << it->second.c_str() << "]";
        }
        cout << " ";
    }

    template <typename T> 
    auto operator<<(T const& _val) -> internal_coutln& {
       stream_ << _val;
       return *this;
    }

    ~internal_coutln() {
       cout << stream_.str() << endl;
    }

 private:
    ostringstream stream_;
};

#define coutln internal_coutln()

constexpr auto operator""_KiB(unsigned long long int v) -> uint64_t { return 1024u * v; }
constexpr auto operator""_MiB(unsigned long long int v) -> uint64_t { return 1024u * 1024u * v; }
constexpr auto operator""_GiB(unsigned long long int v) -> uint64_t { return 1024u * 1024u * 1024u * v; }

/*
    == Producer ========

    - select random-size chunks of data from g_data
    - write down transactions
    - transaction format:
      - uint32_t:  size of data following
      - uint8_t[]: array of bytes
    - last transaction with size 0xFFffFFff indicates end of transmission
*/

void producer () {
    {
        lock_guard<mutex> lock(g_print_mutex);
        g_tid2str[this_thread::get_id()] = string("Producer");
    }
    coutln << "Starting";

    mt19937 gen(random_device{}());
    uniform_int_distribution<> dis(1, g_rbuffer.capacity() - 1);
    auto t0 = high_resolution_clock::now();
    auto pc = 0u;
    while(true) {
        if (pc < g_data_size) {
            auto ok = false;
            auto chunk_size = min((uint32_t)dis(gen), (uint32_t)(g_data_size - pc));
            if (auto wt = g_rbuffer.try_write(time_now())) {
                if (wt.push_back(chunk_size) &&
                    wt.push_back(g_data.get() + pc, chunk_size)) {
                    pc += chunk_size;
                    ok = true;
                } else {
                    wt.invalidate();
                }
            }

            if (!ok) {
                g_failed_writes++;
            }

        } else if (auto wt = g_rbuffer.try_write(time_now())) {
            if (wt.push_back(0xFFffFFff)) {
                break; // Final transaction
            }
            wt.invalidate();
        }
    }

    auto ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
    coutln << "Transfer speed = " << dec << (((double)g_data_size / 1_MiB) / (ns / 1000000000.0)) << " MiB/sec";
}

/*
    == Consumer ========

    - read transactions until the last one
*/

void consumer() {
    {
        lock_guard<mutex> lock(g_print_mutex);
        g_tid2str[this_thread::get_id()] = string("Consumer");
    }
    coutln << "Starting";

    const auto process_chunk = [&](auto _buff, auto _len) { 
        g_consumer_hash = crc32(_buff, _len, g_consumer_hash); 
    };

    auto t0 = high_resolution_clock::now();
    while (true) {
        if (auto rt = g_rbuffer.try_read()) {
            if (auto [tsize, ok] = rt.pop_front<uint32_t>(); ok) {
                if (tsize == 0xFFffFFff) {
                    break; // Done!
                } else if (!rt.pop_front(tsize, process_chunk)) {
                    return; // Error!
                }
            } else {
                return; // Error!
            }
        } else {
            g_failed_reads++;
        }
    }

    g_consumer_hash = g_consumer_hash ^ 0xFFffFFff;

    auto ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
    coutln << "Read/process speed = " << dec << (((double)g_data_size / 1_MiB) / (ns / 1000000000.0)) << " MiB/sec";
}

/*
    == Main function ========
*/

auto main() -> int {

    // Register main thread
    g_tid2str[this_thread::get_id()] = "Main";

    // Allocate a big data chunk and store random numbers in it

    g_data_size = 420_MiB;
    g_data = make_unique<uint8_t[]>((size_t)g_data_size);
    if (!g_data) {
        coutln << "ERR: No memory!";
        return 1;
    }

    coutln << "Generating random sample of " << (float)g_data_size / 1_MiB << " MiB...";
    auto step = g_data_size / thread::hardware_concurrency();
    auto start = g_data.get();
    auto end = start + g_data_size;
    const auto generator = [](uint8_t* _start, uint8_t* _end, int _seed) {
        mt19937 gen(_seed);
        uniform_int_distribution<> dis(0, 255);
        for (auto pc = _start; pc < _end; ++pc) {
            *pc = (uint8_t)dis(gen);
        }
    };
    vector<future<void>> results;
    results.reserve(thread::hardware_concurrency());
    const auto next_fib = [] (int _n) { 
        return (int)round(_n * (1 + sqrt(5)) / 2.0); 
    };
    auto nfib = 13;
    while (start < end) {
        nfib = next_fib(nfib);
        results.push_back(async(launch::async, generator, start, min(start + step, end - 1), nfib));
        start += step;
    }
    for (auto& res : results) {
        res.get();
    }

    // Calculate the crc32

    coutln << "Calculating crc32...";
    g_producer_hash = crc32(g_data.get(), g_data_size, g_producer_hash);
    g_producer_hash = g_producer_hash ^ 0xFFffFFff;
    coutln << "Crc32 = 0x" << hex << g_producer_hash;

    // Reserve space for the ring buffer

    coutln << "Creating buffer...";
    if (!g_rbuffer.reserve((uint32_t)2_MiB)) {
        coutln << "ERR: No memory!";
        return 1;
    }
    coutln << "Buffer Capacity = " << (float)g_rbuffer.capacity() / 1_MiB << " MiB";

    // Run threads

    auto t0 = high_resolution_clock::now();
    auto producer_thread = thread(producer);
    auto consumer_thread = thread(consumer);
    producer_thread.join();
    consumer_thread.join();
    auto ns = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
    coutln << (g_consumer_hash == g_producer_hash ? "PASSED" : "ERROR") << " (crc32 == 0x" << hex << g_consumer_hash << ")";
    coutln << "== Stats == ";
    coutln << "Number of times the producer could not write = " << dec << g_failed_writes;
    coutln << "Number of times the consumer could not read  = " << dec << g_failed_reads;
    coutln << "Time elapsed  = " << dec << ns << " ns, " << ns / 1000.f << " us, " << ns / 1000000.f << " ms, " << ns / 1000000000.f << " sec";

    return 0;
}
