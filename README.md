# Timestamped transactional ring buffer
Header-only library for **single** producer / **single** consumer, lock-free and **timestamped** transactional ring buffer data structure.

### Create buffer 

1. Select the time representation (_**float**_ for instance).
2. Create an instance on any thread that is not the producer nor the consumer threads.

```c++
qcstudio::storage::transactional_ring_buffer<float> rbuffer;
rbuffer.reserve(8192);
```

### Write transactions

On the **producer** side...

```c++
...
float now = get_now();
if (auto wr = rbuffer.try_write(now)) {
    wr.push_back(42);
    ...
    wr.invalidate(); // if necessary we can invalidate
}
...
```

Notice that the transactions follow [RAII](https://en.cppreference.com/w/cpp/language/raii) idiom meaning that they are **committed** upon their **destruction**.

### Read transactions

On the **consumer** side...

```c++
...
if (auto rd = rbuffer.try_read()) { 
    if (auto [data, ok] = rd.pop_front<int>(); ok) {
        ... 
        rd.invalidate(); // if necessary we can invalidate
    }
}
...
```

### Example

Please, find a full example [here](https://github.com/galtza/transactional-ring-buffer/blob/master/example/trb_test.cpp).

It consists of three threads (main, producer and consumer threads). The main thread generates random numbers (up to 420 MiB of numbers) and calculates its crc32 value, the producer thread stores random chunks of it into a transaction buffer and, Finally, the consumer reads it as it calculates the crc32 incrementally. If the two crc32 numbers coincide, all is ok.

We use **premake5** as a replacement of **cmake** for simplicity reasons. In all platforms it is as simple as invoking **premake5** with the action to be taken (*gmake* in linux/osx and *vs2017*/*vs2019*/etc. for instance for Windows):

```bash
example$ ../external/premake5/osx/premake5 gmake
example$ cd .build
.build$ make
.build$ ../.out/trb_test/x64/Release/trb_test 
```

### Benchmark

On a MacBook Air (Mid 2011) with a 1.8 GHz Inter Core i7 and 4GB 1333 MHz DDR3 the result of the execution of the previous example are:

```sh
$ ./trb_test
[347954498][Main] Generating random sample of 420 MiB...
[5699542353][Main] Calculating crc32...
[5866618530][Main] Crc32 = 0xa3de204
[5866664423][Main] Creating buffer...
[5866686171][Main] Buffer Capacity = 2 MiB
[5866803829][Producer] Starting
[5866826500][Consumer] Starting
[6011106349][Producer] Transfer speed = 2912.07 MiB/sec
[6011782830][Consumer] Read/process speed = 2897.85 MiB/sec
[6011875045][Main] PASSED (crc32 == 0xa3de204)
[6011897941][Main] == Stats == 
[6011907800][Main] Number of times the producer could not write = 705369
[6011935609][Main] Number of times the consumer could not read  = 614168
[6011982540][Main] Time elapsed  = 145167645 ns, 145168 us, 145.168 ms, 0.145168 sec
```
