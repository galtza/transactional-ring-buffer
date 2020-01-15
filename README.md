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

Please, find a full example [here](https://github.com/galtza/transactional-ring-buffer/blob/master/example/crc32.cpp).

To build and run it do the following (OSX / linux):

```bash
example$ mkdir .build
example$ cd .build
.build$ cmake ..
.build$ make
.build$ ./crc32
```

### Benchmark

On a MacBook Air (Mid 2011) with a 1.8 GHz Inter Core i7 and 4GB 1333 MHz DDR3 the result of the execution of the previous example are:

```sh
$ ./crc32
[304173274][Main] Generating random sample of 420 MiB...
[5188132117][Main] Calculating crc32...
[5343847553][Main] Crc32 = 0xa3de204
[5343889510][Main] Creating buffer...
[5343914834][Main] Buffer Capacity = 2 MiB
[5344007821][Producer] Starting
[5344047299][Consumer] Starting
[5489134410][Producer] Transfer speed = 2895.78 MiB/sec
[5489783869][Consumer] Read/process speed = 2882.22 MiB/sec
[5489861695][Main] PASSED (crc32 == 0xa3de204)
[5489882500][Main] == Stats == 
[5489890198][Main] Number of times the producer could not write = 818202
[5489925755][Main] Number of times the consumer could not read  = 407736
[5489940523][Main] Time elapsed  = 145905986 ns, 145906 us, 145.906 ms, 0.145906 sec
```
