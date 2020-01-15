# timestamped, transactional, ring buffer
Header-only library for **Single** producer / **single** consumer, lock-free and **timestamped** transactional ring buffer data structure.

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
