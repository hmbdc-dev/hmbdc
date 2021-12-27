#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/Compile.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/Config.hpp"
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable
#include <malloc.h>


namespace hmbdc { namespace pattern {

namespace blocking_buffer_detail {
struct iterator;
}

struct BlockingBuffer {
    using iterator = blocking_buffer_detail::iterator;
    using value_type = void *;
    BlockingBuffer(size_t maxItemSize, size_t capacity) 
    : maxItemSize_(maxItemSize)
    , capacity_(capacity)
    , size_(0)
    , seq_(0) {
        store_ = (char*)memalign(SMP_CACHE_BYTES, capacity*maxItemSize);
    }

    BlockingBuffer(BlockingBuffer const&) = delete;
    BlockingBuffer& operator = (BlockingBuffer const&) = delete;
    ~BlockingBuffer() {
        ::free(store_);
    }
    
    value_type getItem(size_t seq) {
        return store_ + seq % capacity_ * maxItemSize_;
    }
    
    size_t maxItemSize() const {return maxItemSize_;}
    size_t capacity() const {return capacity_;}
    void put(void const* item, size_t sizeHint = 0) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasSlot_.wait(lck, [this]{return capacity_ > size_;});
        memcpy(getItem(seq_++), item, sizeHint?sizeHint:maxItemSize_);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_one();
        }
    }

    template <typename T, typename... Ts> 
    void 
    put(T&& item, Ts&&... items) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasSlot_.wait(lck, [this]{return capacity_ > size_ + sizeof...(Ts);});
        fill(std::forward<T>(item), std::forward<Ts>(items)...);
        size_ += sizeof...(items) + 1;
        if (size_ == sizeof...(items) + 1) {
            lck.unlock();
            hasItem_.notify_one();
        }
    }

    template <typename It> 
    bool 
    tryPutBatch(It begin, size_t n) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (hasSlot_.wait_for(
            lck, std::chrono::seconds(0), [this, n]{return capacity_ > size_ + n - 1;})) {
            fillBatch(begin, n);
            size_ += n;
            if (size_ == n) {
                lck.unlock();
                hasItem_.notify_all();
            }
            return true;
        }
        return false;
    }

    template <typename Item, typename It> 
    bool 
    tryPutBatchInPlace(It begin, size_t n) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (hasSlot_.wait_for(
            lck, std::chrono::seconds(0), [this, n]{return capacity_ > size_ + n - 1;})) {
            fillBatchInPlace<Item>(begin, n);
            size_ += n;
            if (size_ == n) {
                lck.unlock();
                hasItem_.notify_all();
            }
            return true;
        }
        return false;
    }


    template <typename T> void putSome(T const& item) {put(&item);}
    template <typename T, typename ...Args>
    void putInPlace(Args&&... args) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasSlot_.wait(lck, [this](){return capacity_ > size_;});
        new (getItem(seq_++)) T(std::forward<Args>(args)...);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_one();
        }
    }
    template <typename T, typename ...Args>
    bool tryPutInPlace(Args&&... args) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (!hasSlot_.wait_for(lck, std::chrono::seconds(0), [this](){return capacity_ > size_;}))
            return false;
        new (getItem(seq_++)) T(std::forward<Args>(args)...);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_one();
        }
        return true;
    }

    bool tryPut(void const* item, size_t sizeHint = 0
        , time::Duration timeout = time::Duration::seconds(0)) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (!hasSlot_.wait_for(lck, std::chrono::nanoseconds(timeout.nanoseconds())
            , [this](){return capacity_ > size_;}))
            return false;
        memcpy(getItem(seq_++), item, sizeHint?sizeHint:maxItemSize_);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_one();
        }
        return true;
    }

    template <typename T> 
    bool tryPut(T const& item
        , time::Duration timeout = time::Duration::seconds(0)) {
        return tryPut(&item, sizeof(item), timeout);
    }

    bool isFull() const {return size_ == capacity_;}
    void take(void *dest, size_t sizeHint = 0) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait(lck, [this](){return size_;});
        memcpy(dest, getItem(seq_ - size_), sizeHint?sizeHint:maxItemSize_);
        if (size_-- == capacity_) {
            lck.unlock();
            hasSlot_.notify_all();
        }
    }

    template <typename T>
    T& take(T& dest) {
        take(&dest, sizeof(T));
        return dest;
    }

    bool tryTake(void *dest, size_t sizeHint = 0, time::Duration timeout = time::Duration::seconds(0)) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (!hasItem_.wait_for(lck, std::chrono::nanoseconds(timeout.nanoseconds()), [this](){return size_;})) return false;
        memcpy(dest, getItem(seq_ - size_), sizeHint?sizeHint:maxItemSize_);
        if (size_-- == capacity_) {
            lck.unlock();
            hasSlot_.notify_all();
        }
        return true;
    }

    template <typename T>
    bool tryTake(T& dest) {
        return tryTake(&dest, sizeof(T));
    }

    template <typename itOut>
    size_t take(itOut b, itOut e) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait(lck, [this](){return size_;});
        auto s = size_;
        while (s && b != e) {
            memcpy(&*b++, getItem(seq_ - s--), std::min(maxItemSize_, sizeof(*b)));
        }
        auto ret = size_ - s;
        size_ = s;
        if (size_ + ret == capacity_) {
            lck.unlock();
            hasSlot_.notify_all();
        }
        return ret;
    }

    iterator peek();
    size_t peek(iterator& b, iterator& e);
    void wasteAfterPeek(size_t len) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (size_ == capacity_) hasSlot_.notify_all();
        size_ -= len;
    }

    void waitItem(time::Duration timeout) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait_for(lck, std::chrono::nanoseconds(timeout.nanoseconds())
            , [this](){return size_;});
    }

    size_t remainingSize() const {
        return size_;
    }
    void reset() {
        size_ = 0;
        seq_ = 0;
        hasSlot_.notify_all();
    }

private:
    void fill(){}
    template <typename T, typename... Ts> 
    void 
    fill(T&& item, Ts&&... items) {
        new (getItem(seq_++)) T(std::forward<T>(item));
        fill(std::forward<Ts>(items)...);
    }

    template <typename It> 
    void 
    fillBatch(It begin, size_t n) {
        for (auto it = begin; n; ++it, --n) {
            using T = decltype(*begin);
            new (getItem(seq_++)) T(*it);
        }
    }

    template <typename Item, typename It> 
    void 
    fillBatchInPlace(It begin, size_t n) {
        for (auto it = begin; n; ++it, --n) {
            new (getItem(seq_++)) Item(*it);
        }
    }

    size_t maxItemSize_;
    size_t capacity_;
    char* store_;
    size_t size_;
    size_t seq_;
    std::mutex mutex_;
    std::condition_variable hasItem_;
    std::condition_variable hasSlot_;
};

namespace blocking_buffer_detail {
struct iterator {
    using Sequence = uint64_t;
    iterator(BlockingBuffer* HMBDC_RESTRICT buf, Sequence seq)
    : buf_(buf), seq_(seq){}
    iterator() : buf_(nullptr), seq_(0){}
    void clear() {buf_ = nullptr;}

    iterator& operator ++() HMBDC_RESTRICT {
        ++seq_;
        return *this;
    }

    iterator operator ++(int) HMBDC_RESTRICT {
        iterator tmp = *this;
        ++*this;
        return tmp;
    }

    iterator operator + (size_t dis) const HMBDC_RESTRICT {
        iterator tmp = *this;
        tmp.seq_ += dis;
        return tmp;
    }

    iterator& operator += (size_t dis) HMBDC_RESTRICT {
        seq_ += dis;
        return *this;
    }

    size_t operator - (iterator const& other) const HMBDC_RESTRICT {
        return seq_ - other.seq_;
    }

    explicit operator bool() const HMBDC_RESTRICT {
        return buf_;
    }

    bool operator < (iterator const& other) const HMBDC_RESTRICT {
        return seq_ < other.seq_;
    }

    bool operator == (iterator const& other) const HMBDC_RESTRICT {return seq_ == other.seq_;}
    bool operator != (iterator const& other) const HMBDC_RESTRICT {return seq_ != other.seq_;}
    void* operator*() const HMBDC_RESTRICT {return buf_->getItem(seq_);}
    template <typename T> T& get() const HMBDC_RESTRICT {return *static_cast<T*>(**this);}
    template <typename T>
    T* operator->() HMBDC_RESTRICT {return static_cast<T*>(buf_->getItem(seq_));}
//private:
    BlockingBuffer* buf_;
    Sequence seq_;
};
} //blocking_buffer_detail

inline
size_t 
BlockingBuffer::
peek(iterator& b, iterator& e) {
    std::unique_lock<std::mutex> lck(mutex_);
    e = iterator(this, seq_);
    b = iterator(this, seq_ - size_);
    return size_;
}

inline
BlockingBuffer::iterator 
BlockingBuffer::
peek() {
    std::unique_lock<std::mutex> lck(mutex_);
    if (size_) {
        return iterator(this, seq_ - size_);
    }
    return iterator();
}
}}
