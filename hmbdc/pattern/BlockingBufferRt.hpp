#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/Compile.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/Config.hpp"

#include <functional>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable
#include <malloc.h>
#include <assert.h>


namespace hmbdc { namespace pattern {

namespace blocking_buffer_rt_detail {
struct iterator;
}

class BlockingBufferRt {
public:
    using iterator = blocking_buffer_rt_detail::iterator;
    using value_type = void *;
    BlockingBufferRt(size_t maxItemSize, size_t capacity) 
    : maxItemSize_(maxItemSize)
    , capacity_(capacity)
    , size_(0)
    , seq_(0) {
        store_ = (char*)memalign(SMP_CACHE_BYTES, capacity*maxItemSize);
    }

    BlockingBufferRt(BlockingBufferRt const&) = delete;
    BlockingBufferRt& operator = (BlockingBufferRt const&) = delete;
    ~BlockingBufferRt() {
        ::free(store_);
    }
    
    value_type getItem(size_t seq) {
        return store_ + seq % capacity_ * maxItemSize_;
    }
    
    size_t maxItemSize() const {return maxItemSize_;}
    size_t capacity() const {return capacity_;}

    using PushedOutBytesHandler = std::function<void(void*)>;
    const PushedOutBytesHandler noopPOH{[](void*){}};

    void put(PushedOutBytesHandler handlePushedOut, void const* item, size_t sizeHint = 0) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (capacity_ <= size_) {
            handlePushedOut(getItem(seq_ - size_--));
        }
        memcpy(getItem(seq_++), item, sizeHint?sizeHint:maxItemSize_);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_all();
        }
    }

    template <typename T, typename... Ts> 
    void 
    put(PushedOutBytesHandler handlePushedOut, T&& item, Ts&&... items) {
        std::unique_lock<std::mutex> lck(mutex_);
        while (capacity_ <= size_ + sizeof...(Ts)) {
            handlePushedOut(getItem(seq_ - size_--));
        }
        fill(std::forward<T>(item), std::forward<Ts>(items)...);
        size_ += sizeof...(items) + 1;
        if (size_ == sizeof...(items) + 1) {
            lck.unlock();
            hasItem_.notify_all();
        }
    }

    template <typename It> 
    void
    putBatch(PushedOutBytesHandler handlePushedOut, It begin, size_t n) {
        std::unique_lock<std::mutex> lck(mutex_);
        while (capacity_ <= size_ + n) {
            handlePushedOut(getItem(seq_ - size_--));
        }
        fillBatch(begin, n);
        size_ += n;
        if (size_ == n) {
            lck.unlock();
            hasItem_.notify_all();
        }
    }

    template <typename Item, typename It> 
    void
    putBatchInPlace(PushedOutBytesHandler handlePushedOut, It begin, size_t n) {
        std::unique_lock<std::mutex> lck(mutex_);
        while (capacity_ <= size_ + n) {
            handlePushedOut(getItem(seq_ - size_--));
        }
        fillBatchInPlace<Item>(begin, n);
        size_ += n;
        if (size_ == n) {
            lck.unlock();
            hasItem_.notify_all();
        }
    }

    template <typename T, typename ...Args>
    void putInPlace(PushedOutBytesHandler handlePushedOut, Args&&... args) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (capacity_ <= size_) {
            handlePushedOut(getItem(seq_ - size_--));
        }
        new (getItem(seq_++)) T(std::forward<Args>(args)...);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_all();
        }
    }

    template <typename T, typename ...Args>
    bool tryPutInPlace(Args&&... args) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (capacity_ <= size_) return false;
        new (getItem(seq_++)) T(std::forward<Args>(args)...);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_all();
        }
        return true;
    }

    bool tryPut(void const* item, size_t sizeHint = 0
        , time::Duration timeout = time::Duration::seconds(0)) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (capacity_ <= size_) return false;
        memcpy(getItem(seq_++), item, sizeHint?sizeHint:maxItemSize_);
        if (++size_ == 1) {
            lck.unlock();
            hasItem_.notify_all();
        }
        return true;
    }

    template <typename T> 
    bool tryPut(T const& item
        , time::Duration timeout = time::Duration::seconds(0)) {
        return tryPut(&item, sizeof(item), timeout);
    }

    bool isFull() const {return size_ == capacity_;}


    using CopyOutBytesHandler = std::function<void* (void*, void*, size_t)>;
    void take(CopyOutBytesHandler coh, void *dest, size_t sizeHint = 0) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait(lck, [this](){return size_;});
        coh(dest, getItem(seq_ - size_), sizeHint?sizeHint:maxItemSize_);
        size_--;
    }

    template <typename T>
    T take(CopyOutBytesHandler coh) {
        T res;
        take(coh, &res, sizeof(res));
        return res;
    }

    bool tryTake(CopyOutBytesHandler coh, void *dest, size_t sizeHint = 0, time::Duration timeout = time::Duration::seconds(0)) {
        std::unique_lock<std::mutex> lck(mutex_);
        if (!hasItem_.wait_for(lck, std::chrono::nanoseconds(timeout.nanoseconds()), [this](){return size_;})) return false;
        coh(dest, getItem(seq_ - size_), sizeHint?sizeHint:maxItemSize_);
        size_--;
        return true;
    }

    template <typename T>
    bool tryTake(CopyOutBytesHandler coh, T& dest, time::Duration timeout = time::Duration::seconds(0)) {
        return tryTake(coh, &dest, sizeof(T), timeout);
    }

    template <typename itOut>
    size_t take(CopyOutBytesHandler coh, itOut b, itOut e) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait(lck, [this](){return size_;});
        auto s = size_;
        while (s && b != e) {
            coh(&*b++, getItem(seq_ - s--), std::min(maxItemSize_, sizeof(*b)));
        }
        auto ret = size_ - s;
        size_ = s;
        return ret;
    }

    size_t remainingSize() const {
        return size_;
    }
    void reset() {
        size_ = 0;
        seq_ = 0;
    }

    void waitItem(time::Duration timeout) {
        std::unique_lock<std::mutex> lck(mutex_);
        hasItem_.wait_for(lck, std::chrono::nanoseconds(timeout.nanoseconds())
            , [this](){return size_;});
    }
private:
    void fill(){}
    template <typename T, typename... Ts> 
    void 
    fill(T&& item, Ts&&... items) {
        new (getItem(seq_++)) T(std::forward<T>(item));
        fill(std::forward<Ts>(items)...);
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
};

namespace blocking_buffer_rt_detail {
struct iterator {
    using Sequence = uint64_t;
    iterator(BlockingBufferRt* HMBDC_RESTRICT buf, Sequence seq)
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
    BlockingBufferRt* buf_;
    Sequence seq_;
};
} //blocking_buffer_rt_detail

}}
