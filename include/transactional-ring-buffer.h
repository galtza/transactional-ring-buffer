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

/*
     _   _
    | | | |___  __ _  __ _  ___  _
    | | | / __|/ _` |/ _` |/ _ \(_)
    | |_| \__ \ (_| | (_| |  __/ _
     \___/|___/\__,_|\__, |\___|(_)
                     |___/

    CREATION of a buffer shared by two threads only: the PRODUCER and the CONSUMER...

        qcstudio::storage::transactional_ring_buffer<time_type> buffer;
        buffer.reserve(8192);

    On the PRODUCER side...

        time_type now = _arbitrary_get_time_function_();
        if (auto wr = buffer.try_write(now)) {
            wr.push_back(42);
            ...
            wr.invalidate(); // you can invalidate before the destruction
        }

    On the CONSUMER side...

        if (auto rd = buffer.try_read()) { 
            if (auto [data, ok] = rd.pop_front<int>(); ok) {
                ... 
                rw.invalidate(); // you can invalidate before the destruction
            }
        }

    FINALLY, notice that...

        - transactions are committed upon destruction
        - transactions can be be invalidated before destruction
        - read transactions do not need to be read completely 
        - requires c++11
*/

#include <atomic>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <functional>

#pragma push_macro("forceinline")
#undef forceinline
#if defined(_WIN32)
#   define forceinline __forceinline
#   pragma warning(disable : 4714)
#elif defined (__clang__) || defined(__GNUC__)
#   define forceinline __attribute__((always_inline)) inline
#else
#   define forceinline inline
#endif

namespace qcstudio {
namespace containers {

    template<typename TIMESTAMP_TYPE> class transaction_base;
    template<typename TIMESTAMP_TYPE> class read_transaction;
    template<typename TIMESTAMP_TYPE> class write_transaction;

    template<typename TIMESTAMP_TYPE>
    class transactional_ring_buffer {

    public:

        /*
            Construction / Destruction

            - The default construction allocates NO MEMORY and sets the buffer as non-valid
            - The destructor FREES owned memory (not borrowed memory)

            - 'reserve' could fail if it cannot allocate memory
            - 'reserve' and 'borrow' are mutually exclusive and must be called before any transaction
            - 'reserve', regardless of _wanted_capacity, shall use a capacity greater or equal that is power of 2 
            - 'reserve' called many times frees the previous buffer and allocate a new one

            - 'borrow' shall fail if the size is not power of 2 or below 'min_capacity'
            - 'borrow' called many times substitutes previous buffer
            - The memory ownership of 'borrow' parameter is external
        */
        transactional_ring_buffer() = default;
        ~transactional_ring_buffer();

        auto reserve(uint32_t _wanted_capacity) -> bool;
        auto borrow(uint8_t* _memory, uint32_t _capacity) -> bool;

        /*
            Getters

            - 'min_capacity' shall return a value power of 2
            - 'has_data' must be called from the consumer only
            - 'size' is a debug function (use always 'try_read' / 'try_write').
        */
        static constexpr auto min_capacity() -> uint32_t;
        auto has_data() const -> bool;
        auto size() const -> uint32_t;
        explicit operator bool() const;
        auto capacity() const -> uint32_t;

        /*
            Transactions

            - The destructor of the transaction shall commit it unless 'invalidate' has been called
            - There can only be 1 transaction per type per buffer at a time. Subsequent attempts to
              create the same type of transactions shall fail until commit or invalidate.
            - Transactions might fail if there is no room to write or there is no data to read
            - Write transactions shall be created by producer and read transactions
              shall be created by the consumer
        */
        auto try_write(TIMESTAMP_TYPE _timestamp) -> write_transaction<TIMESTAMP_TYPE>;
        auto try_read()                           -> read_transaction<TIMESTAMP_TYPE>;

    private:
        bool valid_ = false;
        bool own_memory_ = true;
        bool reading_ = false, writing_ = false;

        uint32_t capacity_ = 0, capacity_mask_;
        uint32_t start_, end_;
        std::atomic_uint32_t size_ = ATOMIC_VAR_INIT(0);
        uint8_t* memory_ = nullptr;

        // Disallow copy, assign and move

        transactional_ring_buffer(const transactional_ring_buffer&) = delete;
        transactional_ring_buffer(const transactional_ring_buffer&&) = delete;
        auto operator =(const transactional_ring_buffer&) -> transactional_ring_buffer& = delete;
        auto operator =(transactional_ring_buffer&&) -> transactional_ring_buffer& = delete;

        // Become a friend of transactions

        friend class transaction_base<TIMESTAMP_TYPE>;
        friend class read_transaction<TIMESTAMP_TYPE>;
        friend class write_transaction<TIMESTAMP_TYPE>;

        // Initialization

        void set_buffer(uint8_t* _memory, uint32_t _capacity);

        // Low-level read / write memory blocks and arithmetic values (no availability checks)

        void llwrite(uint32_t _idx, const uint8_t* _src, uint32_t _size);
        void llread (uint32_t _idx, uint8_t* _dest, uint32_t _size);

        template<typename T, typename U = void> using iff_arith_t     = typename std::enable_if< std::is_arithmetic<T>::value, U>::type;
        template<typename T, typename U = void> using iff_not_arith_t = typename std::enable_if<!std::is_arithmetic<T>::value, U>::type;

        template<typename T> auto llwrite(uint32_t _idx, const T& _src)  -> iff_arith_t<T>;
        template<typename T> auto llwrite(uint32_t _idx, const T& _src)  -> iff_not_arith_t<T>;
        template<typename T> auto llread (uint32_t _idx,       T& _dest) -> iff_arith_t<T>;
        template<typename T> auto llread (uint32_t _idx,       T& _dest) -> iff_not_arith_t<T>;

        // helpers

        auto index_of(uint32_t _index) const -> uint32_t;
        auto round_up(uint32_t _index) const -> uint32_t;
    };

    // == Constants and global structs ========

    static constexpr uint32_t INVALID_INDEX = 0xFFffFFff;

    template<typename TIMESTAMP_TYPE>
    struct transaction_header {
        uint32_t size;
        TIMESTAMP_TYPE timestamp;
    };

    // == Base of all transactions ========

    template<typename TIMESTAMP_TYPE>
    class transaction_base {

    public:

        /*
            Getters

            - 'operator bool' shall be false if the transaction is not valid
            - 'size' varies on write transactions as we pour data into the transaction
            - 'timestamp' is immutable during the life of the transaction
            - if the transaction is not valid getters shall return undefined data
              (except for the 'operator bool')
        */
        explicit operator bool() const;
        auto size() const -> uint32_t;
        auto timestamp() const -> TIMESTAMP_TYPE;
        static constexpr auto header_size() -> uint32_t;

    protected:

        transaction_base(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer);

        transaction_header<TIMESTAMP_TYPE> header_;
        transactional_ring_buffer<TIMESTAMP_TYPE>& buffer_;
        uint32_t index_; // index on the ring buffer
        uint32_t available_;
    };

    // == Write transaction ========

    template<typename TIMESTAMP_TYPE>
    class write_transaction : public transaction_base<TIMESTAMP_TYPE> {

    public:

        /*
            prevent the transaction from committing 
        */
        void invalidate();

        /*
            Construction

            - write transactions can be moved but not copied
            - destructor shall commit changes
        */
        write_transaction(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer, TIMESTAMP_TYPE _timestamp);
        write_transaction(const write_transaction& _other) = delete;
        write_transaction(write_transaction&& _other);
        ~write_transaction();

        /*
            Data operations

            - the failure of the operations does not invalidate the whole transaction.
            - on simple 'push_back' the operation occurs completely or not (no partial additions)
            - on variadic 'push_backs' it is added as many as it can and returns the number of successfully added items
            - commit is not mandatory as destructor shall call it automatically
        */

        // raw memory
        auto push_back(const uint8_t* _data, const uint32_t _size) -> bool;

        // single
        template<typename T>
        auto push_back(const T& _data) -> bool;

        // variadic
        template<typename T, typename ...REST>
        auto push_back(const T& _data, REST... _rest) -> typename std::enable_if<!std::is_pointer<T>::value, int>::type;

        void commit();

    private:

        auto can_write(const uint32_t _size) -> bool;
    };

    // == Read transaction ========

    template<typename TIMESTAMP_TYPE>
    class read_transaction : public transaction_base<TIMESTAMP_TYPE> {

    public:

        /*
            prevent the transaction from committing 
        */
        void invalidate();

        /*
            Construction

            - read transactions can be moved but not copied
            - destructor shall commit changes
        */
        read_transaction(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer);
        read_transaction(const read_transaction& _other) = delete;
        read_transaction(read_transaction&& _other);
        ~read_transaction();

        /*
            Data operations

            - the failure of the operations does not invalidate the whole transaction.
            - no partial reads occur. All or nothing.
            - "sized" read operations can happen in up to 2 rounds (2 calls to lambda)
              The lambda receives a buffer/size with the partial read
            - 'commit' is a manual version of the destructor
        */
        template<typename T> auto pop_front() -> std::pair<T, bool>;
        template<typename T> auto pop_front(T& _dest) -> bool;
        auto pop_front(uint32_t _size, std::function<void(const uint8_t*, uint32_t)> _callback) -> bool;

        void commit();

    private:

        auto can_read(uint32_t _bytes) -> bool;
    };

    // == implementation of transactions ========

    template<typename TIMESTAMP_TYPE>
    forceinline transaction_base<TIMESTAMP_TYPE>::transaction_base(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer) : buffer_(_buffer), index_(INVALID_INDEX) {
    }

    template<typename TIMESTAMP_TYPE>
    forceinline transaction_base<TIMESTAMP_TYPE>::operator bool() const {
        return index_ != INVALID_INDEX;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transaction_base<TIMESTAMP_TYPE>::size() const -> uint32_t {
        assert((bool)*this); // TODO: Add debug checks
        return header_.size - header_size();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transaction_base<TIMESTAMP_TYPE>::timestamp() const -> TIMESTAMP_TYPE {
        assert((bool)*this); // TODO: Add debug checks
        return header_.timestamp;
    }

    template<typename TIMESTAMP_TYPE>
    constexpr auto transaction_base<TIMESTAMP_TYPE>::header_size() -> uint32_t {
        // note: do not change this to the size of the struct as it might have padding
        return sizeof (transaction_header<TIMESTAMP_TYPE>::size) + sizeof (transaction_header<TIMESTAMP_TYPE>::timestamp);
    }

    template<typename TIMESTAMP_TYPE>
    forceinline void write_transaction<TIMESTAMP_TYPE>::invalidate() {
        this->index_ = INVALID_INDEX;
        this->buffer_.writing_ = false;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline write_transaction<TIMESTAMP_TYPE>::write_transaction(write_transaction&& _other) : transaction_base<TIMESTAMP_TYPE>(_other.buffer_) {
        this->header_.size = _other.header_.size;
        this->header_.timestamp = _other.header_.timestamp;
        this->index_ = _other.index_;
        this->available_ = _other.available_;

        _other.invalidate();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline write_transaction<TIMESTAMP_TYPE>::write_transaction(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer, TIMESTAMP_TYPE _timestamp) : transaction_base<TIMESTAMP_TYPE>(_buffer) {
        if (_buffer && !this->buffer_.writing_) {
            this->header_.size = this->header_size();
            auto actual_available_size = this->buffer_.capacity_ - this->buffer_.size_.load(std::memory_order_acquire);
            if (actual_available_size >= this->header_.size) {
                this->available_ = actual_available_size - this->header_.size;
                this->header_.timestamp = _timestamp;
                this->buffer_.llwrite(this->buffer_.index_of(this->buffer_.end_ + sizeof(this->header_.size)), _timestamp); // transaction size gap will be filled on the destructor
                this->index_ = this->buffer_.index_of(this->buffer_.end_ + this->header_size());

                this->buffer_.writing_ = true;
            }
        }
    }

    template<typename TIMESTAMP_TYPE>
    forceinline write_transaction<TIMESTAMP_TYPE>::~write_transaction() {
        commit();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline void write_transaction<TIMESTAMP_TYPE>::commit() {
        if (*this) {
            this->buffer_.llwrite(this->buffer_.end_, reinterpret_cast<const uint8_t*>(&this->header_.size), sizeof(this->header_.size));
            this->buffer_.end_ = this->buffer_.index_of(this->buffer_.end_ + this->header_.size);
            this->buffer_.size_.fetch_add(this->header_.size, std::memory_order_release);
            this->invalidate();
        }
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto write_transaction<TIMESTAMP_TYPE>::can_write(const uint32_t _size) -> bool {
        if (!*this) {
            return false;
        }

        // 'available_' is cached from when the transaction was generated. Try to sync it again before failing
        if (this->available_ < _size) {
            this->available_ = this->buffer_.capacity_ - this->buffer_.size_.load(std::memory_order_acquire) - this->header_.size;
            if (this->available_ < _size) {
                return false;
            }
        }

        return true;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto write_transaction<TIMESTAMP_TYPE>::push_back(const uint8_t* _data, const uint32_t _size) -> bool {
        if (!can_write(_size)) {
            return false;
        }

        this->buffer_.llwrite(this->index_, reinterpret_cast<const uint8_t*>(_data), _size);
        this->index_ = this->buffer_.index_of(this->index_ + _size);
        this->available_ -= _size;
        this->header_.size += _size;

        return true;
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto write_transaction<TIMESTAMP_TYPE>::push_back(const T& _data) -> bool {
        if (!can_write(sizeof(T))) {
            return false;
        }

        this->buffer_.llwrite(this->index_, _data);
        this->index_ = this->buffer_.index_of(this->index_ + sizeof(T));
        this->available_ -= sizeof(T);
        this->header_.size += sizeof(T);

        return true;
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T, typename ...REST>
    forceinline auto write_transaction<TIMESTAMP_TYPE>::push_back(const T& _item, REST... _rest) -> typename std::enable_if<!std::is_pointer<T>::value, int>::type {
        if (!push_back(_item)) {
            return 0;
        }
        return 1 + push_back(_rest...);
    }

    template<typename TIMESTAMP_TYPE>
    forceinline read_transaction<TIMESTAMP_TYPE>::read_transaction(read_transaction&& _other) : transaction_base<TIMESTAMP_TYPE>(_other.buffer_) {
        this->header_.size = _other.header_.size;
        this->header_.timestamp = _other.header_.timestamp;
        this->index_ = _other.index_;
        this->available_ = _other.available_;

        _other.invalidate();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline read_transaction<TIMESTAMP_TYPE>::read_transaction(transactional_ring_buffer<TIMESTAMP_TYPE>& _buffer) : transaction_base<TIMESTAMP_TYPE>(_buffer) {
        if (_buffer) {
            if (!this->buffer_.reading_ && this->buffer_.size_.load(std::memory_order_acquire) > 0) { // note: as transactions are atomic we just need to check that the buffer size is greater than zero
                this->buffer_.llread(this->buffer_.start_,                                                      this->header_.size);
                this->buffer_.llread(this->buffer_.index_of(this->buffer_.start_ + sizeof(this->header_.size)), this->header_.timestamp);

                this->index_ = this->buffer_.index_of(this->buffer_.start_ + this->header_size());
                this->available_ = this->header_.size - this->header_size();
                this->buffer_.reading_ = true;
            }
        }
    }

    template<typename TIMESTAMP_TYPE>
    forceinline void read_transaction<TIMESTAMP_TYPE>::invalidate() {
        this->index_ = INVALID_INDEX;
        this->buffer_.reading_ = false;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline read_transaction<TIMESTAMP_TYPE>::~read_transaction() {
        commit();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline void read_transaction<TIMESTAMP_TYPE>::commit() {
        if (*this) {
            this->buffer_.start_ = this->buffer_.index_of(this->buffer_.start_ + this->header_.size);
            this->buffer_.size_.fetch_sub(this->header_.size, std::memory_order_release);
            this->buffer_.reading_ = false;
            this->index_ = INVALID_INDEX;
        }
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto read_transaction<TIMESTAMP_TYPE>::can_read(uint32_t _bytes) -> bool {
        return (bool)*this && this->available_ >= _bytes;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto read_transaction<TIMESTAMP_TYPE>::pop_front(uint32_t _size, std::function<void(const uint8_t*const, uint32_t)> _callback) -> bool {
        if (!can_read(_size)) {
            return false;
        }

        auto size = std::min(this->available_, _size);
        if (_callback) {
            if ((this->index_ + size) <= this->buffer_.capacity_) {
                _callback(reinterpret_cast<const uint8_t*>(&this->buffer_.memory_[this->index_]), size);
            } else {
                auto first_chunk_size = this->buffer_.capacity_ - this->index_;
                _callback(reinterpret_cast<const uint8_t*>(&this->buffer_.memory_[this->index_]), first_chunk_size);
                _callback(reinterpret_cast<const uint8_t*>(&this->buffer_.memory_[0]), size - first_chunk_size);
            }
        }
        this->index_ = this->buffer_.index_of(this->index_ + size);
        this->available_ -= size;
        return true;
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto read_transaction<TIMESTAMP_TYPE>::pop_front() -> std::pair<T, bool> {
        T value;
        if (!can_read(sizeof(T))) {
            return std::make_pair(value, false);
        }
        this->buffer_.llread(this->index_, value);
        this->index_ = this->buffer_.index_of(this->index_ + sizeof(T));
        this->available_ -= sizeof(T);
        return std::make_pair(value, true);
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto read_transaction<TIMESTAMP_TYPE>::pop_front(T& _dest) -> bool {
        if (!can_read(sizeof(T))) {
            return false;
        }
        this->buffer_.llread(this->index_, _dest);
        this->index_ = this->buffer_.index_of(this->index_ + sizeof(T));
        this->available_ -= sizeof(T);
        return true;
    }

    // == Buffer implementation  ========

    // Construction / destruction / set_buffer

    template<typename TIMESTAMP_TYPE>
    forceinline transactional_ring_buffer<TIMESTAMP_TYPE>::~transactional_ring_buffer() {
        if (own_memory_ && memory_) {
            delete[] memory_;
        }
    };

    template<typename TIMESTAMP_TYPE>
    forceinline void transactional_ring_buffer<TIMESTAMP_TYPE>::set_buffer(uint8_t* _memory, uint32_t _capacity) {
        memory_ = _memory;
        capacity_ = _capacity;
        capacity_mask_ = capacity_ - 1;
        start_ = end_ = 0;
        valid_ = memory_ != nullptr;
    }

    // Memory allocation / borrowing

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::reserve(uint32_t _wanted_capacity) -> bool {
        if (!own_memory_) {
            return false; // 'borrow' called before
        }

        /*
            note: On same or less capacity we do not need to deallocate; Just adjustments.
        */

        auto new_capacity = round_up(_wanted_capacity < min_capacity()? min_capacity() : _wanted_capacity);
        if (valid_ && new_capacity <= capacity_) {
            set_buffer(memory_, new_capacity); // same or less buffer size (if less, we will only use a portion; deletion will be alright, though)
        } else {
            if (memory_) {
                delete[] memory_;
                memory_ = nullptr;
            }

            set_buffer(new uint8_t[new_capacity], new_capacity);
        }

        return valid_;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::borrow(uint8_t* _memory, uint32_t _capacity) -> bool {
        if (!_memory || (own_memory_ && memory_)) {
            return false; // nullptr buffer or 'reserve' called before
        }

        valid_ = _capacity >= min_capacity() && !(_capacity & (_capacity - 1)); // check that _capacity is indeed power of 2 and >= than 'min_capacity'
        if (valid_) {
            set_buffer(_memory, _capacity);
            own_memory_ = false;
            return true;
        }
        return false;
    };

    // Creation of transactions

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::try_read() -> read_transaction<TIMESTAMP_TYPE> {
        return read_transaction<TIMESTAMP_TYPE>(*this);
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::try_write(TIMESTAMP_TYPE _timestamp) -> write_transaction<TIMESTAMP_TYPE> {
        return write_transaction<TIMESTAMP_TYPE>(*this, _timestamp);
    }

    // Low level writes and reads (for integrals try to assign instead of memcpy when possible)

    template<typename TIMESTAMP_TYPE>
    forceinline void transactional_ring_buffer<TIMESTAMP_TYPE>::llwrite(uint32_t _idx, const uint8_t* _src, uint32_t _size) {
        if (_idx + _size <= capacity_) {
            memcpy(&memory_[_idx], _src, _size);
        } else {
            auto first_chunk_size = capacity_ - _idx;
            memcpy(&memory_[_idx], _src, first_chunk_size);
            memcpy(&memory_[0], _src + first_chunk_size, _size - first_chunk_size);
        }
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::llwrite(uint32_t _idx, const T& _value) -> iff_arith_t<T> {
        if (_idx + sizeof(T) <= capacity_) {
            *((T*)(memory_ + _idx)) = _value; // prefer assignment
        } else {
            auto first_chunk_size = capacity_ - _idx;
            memcpy(&memory_[_idx], (uint8_t*)&_value, first_chunk_size);
            memcpy(&memory_[0], ((uint8_t*)&_value) + first_chunk_size, sizeof(T) - first_chunk_size);
        }
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::llwrite(uint32_t _idx, const T& _value) -> iff_not_arith_t<T> {
        static_assert(std::is_pod<T>::value, "Non arithmetic values must be POD types");
        llwrite(_idx, (const uint8_t*)&_value, sizeof(T));
    }

    template<typename TIMESTAMP_TYPE>
    forceinline void transactional_ring_buffer<TIMESTAMP_TYPE>::llread(uint32_t _idx, uint8_t* _dest, uint32_t _size) {
        if ((_idx + _size) <= capacity_) {
            memcpy(_dest, reinterpret_cast<void*>(memory_ + _idx), _size);
        } else {
            const auto first_chunk_size = capacity_ - _idx;
            memcpy(_dest, reinterpret_cast<void*>(&memory_[_idx]), first_chunk_size);
            memcpy(_dest + first_chunk_size, reinterpret_cast<void*>(&memory_[0]), _size - first_chunk_size);
        }
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::llread(uint32_t _idx, T& _dest) -> iff_arith_t<T> {
        if ((_idx + sizeof(T)) <= capacity_) {
            _dest = *((T*)(memory_ + _idx)); // prefer assignment
        } else {
            const auto first_chunk_size = capacity_ - _idx;
            memcpy((uint8_t*)&_dest, reinterpret_cast<void*>(&memory_[_idx]), first_chunk_size);
            memcpy((uint8_t*)&_dest + first_chunk_size, reinterpret_cast<void*>(&memory_[0]), sizeof(T) - first_chunk_size);
        }
    }

    template<typename TIMESTAMP_TYPE>
    template<typename T>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::llread(uint32_t _idx, T& _dest) -> iff_not_arith_t<T> {
        static_assert(std::is_pod<T>::value, "Non arithmetic values must be POD types");
        llread(_idx, (uint8_t*)&_dest, (uint32_t)sizeof(T));
    }

    // helpers

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::index_of(uint32_t _index) const -> uint32_t {
        return _index & capacity_mask_;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::round_up(uint32_t _value) const -> uint32_t {
        // round-up the size to the next power of 2. ref: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        auto ret = std::max(_value, min_capacity()) - 1;
        for (auto i : { 1, 2, 4, 8, 16 }) {
            ret |= ret >> i;
        }
        return ++ret;
    }

    // class traits

    template<typename TIMESTAMP_TYPE>
    forceinline constexpr auto transactional_ring_buffer<TIMESTAMP_TYPE>::min_capacity() -> uint32_t {
        return (uint32_t)(sizeof (transaction_header<TIMESTAMP_TYPE>::size) + sizeof (transaction_header<TIMESTAMP_TYPE>::timestamp));
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::size() const -> uint32_t {
        return size_.load();
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::has_data() const -> bool {
        return size_.load(std::memory_order_acquire) > 0;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline auto transactional_ring_buffer<TIMESTAMP_TYPE>::capacity() const -> uint32_t {
        return capacity_;
    }

    template<typename TIMESTAMP_TYPE>
    forceinline transactional_ring_buffer<TIMESTAMP_TYPE>::operator bool() const {
        return valid_;
    }

} // namespace qcstudio
} // namespace containers

#pragma pop_macro("forceinline")
