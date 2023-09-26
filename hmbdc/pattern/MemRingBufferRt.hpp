#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"

#include <boost/smart_ptr/detail/yield_k.hpp>

#include <iterator>
#include <thread>
#include <vector>
#include <limits>
#include <algorithm>
#include <atomic>

#ifndef HMBDC_YIELD
#define HMBDC_YIELD(x) boost::detail::yield(x)
#endif

namespace hmbdc { namespace pattern {
/**
 * @brief Multiple producer multiple consumer thread safe ring buffer target realtime usage
 * The writes could wrap back in the ring buffer so it never blocks the producer or consumer.
 * The consumer is garanteed the always getting the lastest bufferred items and in FIFO order
 * @tparam IdentyStruct - a unique struct type for each buffer instance within the process.
 * This is to avoid clients keeping ReadRecord. 
 * In C++20:
 * MemRingBufferRt<> a;
 * MemRingBufferRt<> b;     // good, a and b are different types
 * 
 * MemRingBufferRt<> a, b;  // a and b are the same type, need to use the ReadRecord
 * 
 * in C++17
 * struct id1{}; 
 * MemRingBufferRt<id1> a, b; // a and b are the same type, need to use the ReadRecord
 * struct id2{};
 * MemRingBufferRt<id1> a;
 * MemRingBufferRt<id2> b;  // good, a and b are different types
 */
template<typename IdentyStruct
#if __cplusplus >= 202002L
= decltype([]{})
#endif
>
class MemRingBufferRt {
public:
    const size_t CAPACITY;
    const size_t VALUE_TYPE_SIZE;
    /**
     * @brief Each reader can keep an instance of this default contructable struct 
     * to keep read history if the record auto kept in local thread storage is not desired. 
     * For example,
     * when using a single thread to emulate reading the buffer from multiple threads;
     * or having trouble to specify unique IdentityStruct (not using C++20)
     */
    struct ReadRecord {
        const HMBDC_SEQ_TYPE readSeq = 0;
        operator HMBDC_SEQ_TYPE() const { return readSeq; }
        operator HMBDC_SEQ_TYPE&() { return const_cast<HMBDC_SEQ_TYPE&>(readSeq); }
    };
private:
    using Sequence = HMBDC_SEQ_TYPE;
    static constexpr auto UPDATING = std::numeric_limits<Sequence>::max();
    const Sequence MASK;

    lf_misc::chunk_base_ptr<Sequence> const buffer_;
    alignas(SMP_CACHE_BYTES) Sequence toBeClaimedSeq_;
    alignas(SMP_CACHE_BYTES) thread_local static Sequence readSeq_;

    Sequence getReadSeq(ReadRecord* readRecord) const {
        if (readRecord) return *readRecord;
        return readSeq_;
    }

    Sequence& getReadSeq(ReadRecord* readRecord) {
        if (readRecord) return (Sequence&)*readRecord;
        return readSeq_;
    }

public:
    /**
     * @brief thrown when the content is too big to write into this buffer
     * 
     */
    struct ItemTooBig : std::out_of_range { using std::out_of_range::out_of_range; };

    using iterator = hmbdc::pattern::lf_misc::iterator<Sequence>;
    
    static size_t footprint(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num) {
        return sizeof(MemRingBufferRt) + decltype(buffer_)::footprint(
            valueTypeSizePower2Num, ringSizePower2Num) + SMP_CACHE_BYTES;
    }

    template <typename Allocator = os::DefaultAllocator>
    MemRingBufferRt(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num
        , Allocator& allocator = os::DefaultAllocator::instance())
        : CAPACITY(1u << ringSizePower2Num)
        , VALUE_TYPE_SIZE((1u << valueTypeSizePower2Num) - sizeof(Sequence))
        , MASK(CAPACITY - 1)
        , buffer_(valueTypeSizePower2Num, ringSizePower2Num
            , (allocator))
        , toBeClaimedSeq_(0u) {
        for (auto i = 0u; i < CAPACITY; ++i) {
            *buffer_.getSeq(i) = std::numeric_limits<Sequence>::max();
        }
        readSeq_ = 0;
    }

    size_t remainingSize(ReadRecord* readRecord = nullptr) const HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        Sequence r = getReadSeq(readRecord);
        Sequence w = toBeClaimedSeq_;
        return w > r ? w - r : 0;
    }

    void put(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        size_t index = seq & MASK;
        *buffer_.getSeq(index) = UPDATING;
        std::atomic_thread_fence(std::memory_order_release);
        sizeHint = sizeHint ? sizeHint : VALUE_TYPE_SIZE;
        if (hmbdc_likely(sizeHint <= VALUE_TYPE_SIZE)) {    
            memcpy(buffer_ + index, item, sizeHint);
        } else {
            HMBDC_THROW(ItemTooBig, "sizeof(item)=" << sizeof(item) 
                << " VALUE_TYPE_SIZE=" << VALUE_TYPE_SIZE);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        *buffer_.getSeq(index) = seq;
    }

    template <typename T>
    void put(T const& item) HMBDC_RESTRICT {
        static_assert(std::is_trivially_destructible_v<T>, "dtor! no support for this type");
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        size_t index = seq & MASK;
        *buffer_.getSeq(index) = UPDATING;
        std::atomic_thread_fence(std::memory_order_release);

        if (hmbdc_likely(sizeof(item) <= VALUE_TYPE_SIZE)) {
            new (buffer_ + index) T{item};
        } else {
            HMBDC_THROW(ItemTooBig, "sizeof(item)=" << sizeof(item) 
                << " VALUE_TYPE_SIZE=" << VALUE_TYPE_SIZE);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        *buffer_.getSeq(index) = seq;
    }

    template <typename T, typename ...Args>
    void putInPlace(Args&&... args) HMBDC_RESTRICT {
        static_assert(std::is_trivially_destructible_v<T>, "dtor! no support for this type");
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        size_t index = seq & MASK;
        *buffer_.getSeq(index) = UPDATING;
        std::atomic_thread_fence(std::memory_order_release);

        if (hmbdc_likely(sizeof(T) <= VALUE_TYPE_SIZE)) {
            new (buffer_ + index) T{std::forward<Args>(args)...};
        } else {
            HMBDC_THROW(ItemTooBig, "sizeof(item)=" << sizeof(T) 
                << " VALUE_TYPE_SIZE=" << VALUE_TYPE_SIZE);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        *buffer_.getSeq(index) = seq;
    }

    void take(void * HMBDC_RESTRICT item, size_t sizeHint = 0, ReadRecord* readRecord = nullptr) HMBDC_RESTRICT {
        auto readSeq = getReadSeq(readRecord);
            Sequence itemSeq;
        do {
            while (readSeq != (itemSeq = *buffer_.getSeq(readSeq & MASK))) {
                if (itemSeq != UPDATING && readSeq < itemSeq) {    // missed
                    if (toBeClaimedSeq_ - readSeq >= 2 * CAPACITY) {
                        // accelerate for late starters
                        readSeq += CAPACITY;
                    } else {
                        ++readSeq;
                    }
                } else {                    // starving
                    for (uint32_t k = 0;
                        *buffer_.getSeq(readSeq & MASK) == UPDATING || readSeq >= toBeClaimedSeq_;
                        ++k) {
                        HMBDC_YIELD(k);
                        std::atomic_thread_fence(std::memory_order_acquire);
                    }
                }
            };
            // copy out 
            memcpy(item, buffer_ + (readSeq++ & MASK), sizeHint ? sizeHint : VALUE_TYPE_SIZE);
            std::atomic_thread_fence(std::memory_order_acquire);
        } while (readSeq - 1 != *buffer_.getSeq((readSeq - 1) & MASK)); // check copy
        getReadSeq(readRecord) = readSeq;
    }

    /**
     * @brief non-blocking read
     * 
     * @param item - ptr to outbuffer
     * @param sizeHint - sizeof the outbuffer, 0 means VALUE_TYPE_SIZE
     * @return int 0 - nothing to read, -1 - read fine but missed older msgs, 1 - read fine no missing
     */
    int tryTake(void * HMBDC_RESTRICT item, size_t sizeHint = 0, ReadRecord* readRecord = nullptr) HMBDC_RESTRICT {
        auto readSeq = getReadSeq(readRecord);
        Sequence itemSeq;
        do {
            while (readSeq != (itemSeq = *buffer_.getSeq(readSeq & MASK))) {
                if (itemSeq != UPDATING && readSeq < itemSeq) {    // missed
                    if (toBeClaimedSeq_ - readSeq >= 2 * CAPACITY) {
                        // accelerate for late starters
                        readSeq += CAPACITY;
                    } else {
                        ++readSeq;
                    }
                } else {                    // starving
                    return 0;
                }
            };
            // copy out
            memcpy(item, buffer_ + (readSeq++ & MASK), sizeHint ? sizeHint : VALUE_TYPE_SIZE);
            std::atomic_thread_fence(std::memory_order_acquire);
        } while (readSeq - 1 != *buffer_.getSeq((readSeq - 1) & MASK)); // check copy
        auto res = getReadSeq(readRecord) == (readSeq - 1) ? 1 : -1;
        getReadSeq(readRecord) = readSeq;
        return res;
    }

    /**
     * @brief blocking read
     * 
     * @tparam T - type of item
     * @param item - read struct type
     * @return read out struct
     */
    template <typename T>
    T take(ReadRecord* readRecord = nullptr) HMBDC_RESTRICT {
        T res;
        auto readSeq = getReadSeq(readRecord);
        Sequence itemSeq;
        do {
            while (readSeq != (itemSeq = *buffer_.getSeq(readSeq & MASK))) {
                if (itemSeq != UPDATING && readSeq < itemSeq) {    // missed
                    if (toBeClaimedSeq_ - readSeq >= 2 * CAPACITY) {
                        // accelerate for late starters
                        readSeq += CAPACITY;
                    } else {
                        ++readSeq;
                    }
                } else {                    // starving
                    for (uint32_t k = 0;
                        *buffer_.getSeq(readSeq & MASK) == UPDATING || readSeq >= toBeClaimedSeq_;
                        ++k) {
                        HMBDC_YIELD(k);
                        std::atomic_thread_fence(std::memory_order_acquire);
                    }
                }
            };
            // copy out
            res = *reinterpret_cast<T const*>(buffer_ + (readSeq++ & MASK));
            std::atomic_thread_fence(std::memory_order_acquire);
        } while (readSeq - 1 != *buffer_.getSeq((readSeq - 1) & MASK)); // check copy
        getReadSeq(readRecord) = readSeq;
        return res;
    }

    /**
     * @brief non-blocking read
     * 
     * @tparam T - type of item
     * @param item - read struct type
     * @param result - tryTake result: 0 - nothing to read, -1 - read fine but missed older msgs, 1 - read fine no missing
     * @return read out struct if reads fine
     */
    template <typename T>
    T tryTake(int& result, ReadRecord* readRecord = nullptr) HMBDC_RESTRICT {
        T res;
        auto readSeq = getReadSeq(readRecord);
        Sequence itemSeq;
        do {
            while (readSeq != (itemSeq = *buffer_.getSeq(readSeq & MASK))) {
                if (itemSeq != UPDATING && readSeq < itemSeq) {    // missed
                    if (toBeClaimedSeq_ - readSeq >= 2 * CAPACITY) {
                        // accelerate for late starters
                        readSeq += CAPACITY;
                    } else {
                        ++readSeq;
                    }
                } else {                    // starving
                    return 0;
                }
            };
            // copy out
            res = *reinterpret_cast<T const*>(buffer_ + (readSeq++ & MASK));
            std::atomic_thread_fence(std::memory_order_acquire);
        } while (readSeq - 1 != *buffer_.getSeq((readSeq - 1) & MASK)); // check copy
        result = getReadSeq(readRecord) == (readSeq - 1) ? 1 : -1;
        getReadSeq(readRecord) = readSeq;
        return res;
    }
};

template<typename IdentyStruct> 
alignas(SMP_CACHE_BYTES) thread_local typename MemRingBufferRt<IdentyStruct>::Sequence MemRingBufferRt<IdentyStruct>::readSeq_ = 0;
}} // end namespace hmbdc::pattern
