/*
    MIT License

    Copyright (c) 2016-2020 Raul Ramos

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
#include <iomanip>
#include <limits>
#include <chrono>
#include <type_traits>
#if defined WIN32
#include <intrin.h>
#endif
#include "transactional-ring-buffer.h"

using namespace std;

#ifdef WIN32
#define DBREAK __debugbreak()
#else
#define DBREAK __builtin_debugtrap()
#endif

#define verify(_cond) _verify(_cond, __FILE__, __LINE__)
#define _verify(_cond, _file, _line) do {\
    if (!_cond) {\
        std::cout << _file << "(" << _line << "): Verification failed (" << #_cond << ")" << std::endl;\
        DBREAK;\
    }\
} while(false)

auto main () -> int {

    /*
        Helper local lambdas
    */

    const auto is_power_of_2 = [] (const int _number) { 
        return (_number & (_number - 1)) == 0; 
    };

    const auto next_power_of_2 = [&] (int _number) { 
        auto ret = !is_power_of_2(_number) ? _number - 1 : _number;
        ret |= ret >> 1;
        ret |= ret >> 2;
        ret |= ret >> 4;
        ret |= ret >> 8;
        ret |= ret >> 16;
        return ++ret;
    };

    /*
        Tests' begin / end tags
    */
    #if defined(__APPLE__)
    const auto OK_STRING = " \u2705";
    const auto FAILED_STRING = " \u274C";
    #else
    const auto OK_STRING = " Ok";
    const auto FAILED_STRING = " Failed";
    #endif

    int num_test = 0;
    const auto BEGIN_TEST = [&](const char* _title) { cout << "[" << setfill('0') << setw(2) << num_test++ << "] " << _title; };
    const auto END_TEST   = [&]                     { cout << OK_STRING << endl; };
    const auto CHECK      = [&](bool _ok)           { if (!_ok) { cout << FAILED_STRING << endl; } return _ok; };

    /*
        'reserve' function alone
    */
    {
        qcstudio::containers::transactional_ring_buffer<float> buff;

        // Reserve below minimum
        BEGIN_TEST("'reserve' 0...");
        verify(CHECK(buff.reserve(0) == true));
        verify(CHECK(buff.capacity() == buff.min_capacity()));
        END_TEST();

        BEGIN_TEST("'reserve' Below minimum...");
        verify(CHECK(buff.reserve(buff.min_capacity() - 1) == true));
        verify(CHECK(buff.capacity() == buff.min_capacity()));
        END_TEST();

        // Reserve more than minimum
        BEGIN_TEST("'reserve' more than the minimum...");
        verify(CHECK(buff.reserve(next_power_of_2(next_power_of_2(buff.min_capacity())) + 1) == true));
        verify(CHECK(is_power_of_2(buff.capacity())));
        END_TEST();

        // Reserve less than before and check that capacity is a power of 2
        BEGIN_TEST("'reserve' slightly less than before...");
        verify(CHECK(buff.reserve(buff.min_capacity() + 1) == true));
        verify(CHECK(is_power_of_2(buff.capacity())));
        END_TEST();
    }

    /*
        'borrow' alone
    */
    {
        qcstudio::containers::transactional_ring_buffer<float> buff;

        // call 'borrow' with a small buffer
        {

            BEGIN_TEST("'borrow' 1 byte buffer...");
            auto buffer = unique_ptr<uint8_t>(new uint8_t[1]);
            verify(CHECK(buff.borrow(buffer.get(), 1) == false));
            END_TEST();
        }

        // call 'borrow' with a buffer with more size than minimim but not power of 2
        {
            //qcstudio::containers::transactional_ring_buffer<float> buff;

            BEGIN_TEST("'borrow' buffer with size not power of 2 but higher than the minimum...");
            auto buffer = unique_ptr<uint8_t>(new uint8_t[buff.min_capacity() + 1]);
            verify(CHECK(buff.borrow(buffer.get(), buff.min_capacity() + 1) == false));
            END_TEST();
        }

        // call 'borrow' with a proper sized buffer
        {
            //qcstudio::containers::transactional_ring_buffer<float> buff;

            BEGIN_TEST("'borrow' with proper buffer size...");
            auto buffer = unique_ptr<uint8_t>(new uint8_t[2 * buff.min_capacity()]);
            verify(CHECK(buff.borrow(buffer.get(), 2 * buff.min_capacity()) == true));
            END_TEST();
        }

    }

    /*
        'borrow' and 'reserve' mixed
    */
    {
        // 'reserve' first
        {
            BEGIN_TEST("'reserve' before 'borrow'...");
            qcstudio::containers::transactional_ring_buffer<float> buff;
            verify(CHECK(buff.reserve(10 * buff.min_capacity()) == true));
            auto buffer = unique_ptr<uint8_t>(new uint8_t[1024]);
            verify(CHECK(buff.borrow(buffer.get(), 1024) == false));
            END_TEST();
        }

        // 'borrow' first
        {
            BEGIN_TEST("'reserve' after 'borrow'...");
            qcstudio::containers::transactional_ring_buffer<float> buff;
            auto buffer = unique_ptr<uint8_t>(new uint8_t[1024]);
            verify(CHECK(buff.borrow(buffer.get(), 1024) == true));
            verify(CHECK(buff.reserve(10 * buff.min_capacity()) == false));
            END_TEST();
        }
    }

    /*
        transactions on uninitialized buffers
    */
    {
        qcstudio::containers::transactional_ring_buffer<float> buff;

        BEGIN_TEST("try write/read from uninitialized buffer...");
        verify(CHECK(!buff.try_write(0.f)));
        verify(CHECK(!buff.try_read()));
        END_TEST();
    }

    /*
        write transactions creation alone
    */
    {
        qcstudio::containers::transactional_ring_buffer<float> buff;

        verify(CHECK(buff.reserve(32) == true));
        verify(CHECK(buff.size() == 0));

        BEGIN_TEST("Create several empty write transaction...");
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
        }
        verify(CHECK(buff.size() == qcstudio::containers::transaction_base<float>::header_size()));
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
        }

        // empty transactions occupy data (size of the header)
        verify(CHECK(buff.size() == 2 * qcstudio::containers::transaction_base<float>::header_size()));
        END_TEST();

        // create multiple write transactions in a row
        BEGIN_TEST("Create mote write transaction until there is no more room...");
        {
            auto wr1 = buff.try_write(0.f);
            verify(CHECK((bool)wr1));

            auto wr2 = buff.try_write(0.f);
            verify(CHECK(!(bool)wr2));
        }

        verify(CHECK(buff.size() == 3 * qcstudio::containers::transaction_base<float>::header_size())); // only three transactions were created successfully
        END_TEST();
    }

    /*
        Pouring data
    */

    {
        qcstudio::containers::transactional_ring_buffer<uint64_t> buff;
        verify(CHECK(buff.reserve(16) == true));

        // Create two empty transactions
        BEGIN_TEST("Create two empty transactions in a row...");
        {
            verify(CHECK(buff.capacity() == 16));
            verify(CHECK(buff.size() == 0));
            auto wr1 = buff.try_write(0);
            wr1.commit();
            verify(CHECK(buff.size() == 12));
            verify(CHECK(buff.capacity() == 16));
            auto wr2 = buff.try_write(0);
            verify(CHECK(!wr2));
            wr2.commit();
            verify(CHECK(buff.size() == 12));
        }
        END_TEST();
    }

    {
        qcstudio::containers::transactional_ring_buffer<float> buff;
        verify(CHECK(buff.reserve(32) == true));

        // 'push_back'
        BEGIN_TEST("Multiple 'push_back's that fit into the buffer...");
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
            verify(CHECK(wr.size() == 0));
            verify(CHECK(wr.push_back(42)));
            verify(CHECK(wr.push_back(42)));
            verify(CHECK(wr.size() == 2 * sizeof(42)));
            verify(CHECK((bool)wr));
        }
        verify(CHECK(buff.size() == (qcstudio::containers::transaction_base<float>::header_size() + 2 * sizeof (42))));
        END_TEST();

        // Too many 'push_back'
        BEGIN_TEST("Too many 'push_back's, hence, fail...");
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
            verify(CHECK(wr.size() == 0));
            verify(CHECK(wr.push_back(42)));
            verify(CHECK(wr.push_back(42)));
            verify(CHECK((bool)wr));
            verify(CHECK(!wr.push_back(42)));
            verify(CHECK((bool)wr)); // after failing a push_back the transaction is still valid. only that operation failed
            verify(CHECK(wr.size() == 2 * sizeof(42)));
        }
        verify(CHECK(buff.size() == (2 * qcstudio::containers::transaction_base<float>::header_size() + 4 * sizeof (42))));
        END_TEST();
    }

    {
        qcstudio::containers::transactional_ring_buffer<float> buff;
        BEGIN_TEST("'push_back' + 'invalidate'...");
        verify(CHECK(buff.reserve(32) == true));
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
            verify(CHECK(wr.size() == 0));
            wr.push_back(42);
            verify(CHECK(wr.size() == sizeof(42)));
            wr.invalidate();
            verify(CHECK(!wr));
        }
        verify(CHECK(buff.size() == 0)); // after invalidation the size of the buffer needs to be still zero
        END_TEST();

        // 'push_back' after 'invalidate'
        BEGIN_TEST("'push_back' after 'invalidate' => invalid buffer / invalid operation...");
        {
            auto wr = buff.try_write(0.f);
            verify(CHECK((bool)wr));
            verify(CHECK(wr.size() == 0));
            wr.push_back(42);
            verify(CHECK(wr.size() == sizeof(42)));
            wr.invalidate();
            verify(CHECK(!wr.push_back(42)));
            verify(CHECK(!wr.push_back(42)));
            verify(CHECK(!wr));
        }

        verify(CHECK(buff.size() == 0)); // after invalidation the size of the buffer needs to be still zero
        END_TEST();
    }

    /*
        Reading data
    */
    {
        qcstudio::containers::transactional_ring_buffer<float> buff;
        BEGIN_TEST("'pop_front' from empty buffer => invalid transacion...");
        verify(CHECK(buff.reserve(32) == true));
        auto rd = buff.try_read();
        verify(CHECK(!rd));
        END_TEST();
    }

    return 0;
}
