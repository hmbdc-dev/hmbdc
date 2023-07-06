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
#include <mutex>
#include <atomic>

#ifndef HMBDC_YIELD
#define HMBDC_YIELD(x) boost::detail::yield(x)
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

namespace hmbdc { namespace pattern {

namespace memringbuffer_detail {
struct my_spin_lock : std::atomic_flag {
    my_spin_lock()
    :  std::atomic_flag{0} {
        //clang has ATOMIC_FLAG_INIT as {0} not 0, so ...
        constexpr int f[] = ATOMIC_FLAG_INIT;
        static_assert(0 == f[0], "");
    }
    void lock() {
        while (test_and_set(std::memory_order_acquire))  {
            std::this_thread::yield();
        }
    }

    void unlock() {
        clear(std::memory_order_release);
    }
};

template<uint16_t parallel_consumer_count>
class MemRingBuffer {
public:
    using Sequence = HMBDC_SEQ_TYPE;
    enum {PARALLEL_CONSUMER_COUNT = parallel_consumer_count,};

    const size_t CAPACITY;
    const size_t VALUE_TYPE_SIZE;
private:

    const Sequence READ_SEQ_MAX;
    const Sequence MASK;

    lf_misc::chunk_base_ptr<Sequence> const buffer_;
    alignas(SMP_CACHE_BYTES) Sequence toBeClaimedSeq_;
    alignas(SMP_CACHE_BYTES) Sequence readSeq_[PARALLEL_CONSUMER_COUNT];
    alignas(SMP_CACHE_BYTES) Sequence readSeqLastPurge_[PARALLEL_CONSUMER_COUNT];

    my_spin_lock locks_[PARALLEL_CONSUMER_COUNT];

    Sequence readSeqLow() const HMBDC_RESTRICT {
        Sequence res = readSeq_[0];
        for (uint16_t i = 1;
                i < PARALLEL_CONSUMER_COUNT; ++i)
            if (res > readSeq_[i]) res = readSeq_[i];
        return res;
    }

    uint16_t findSlowestReader() const HMBDC_RESTRICT {
        Sequence smallest = readSeq_[0];
        uint16_t smallestLoc = 0;
        for (uint16_t i = 1; i < PARALLEL_CONSUMER_COUNT; ++i)
            if (smallest > readSeq_[i]) {
                smallest = readSeq_[i];
                smallestLoc = i;
            }
        return smallestLoc;
    }

public:
    using value_type = void *;
    using DeadConsumer = hmbdc::pattern::lf_misc::DeadConsumer;

    using iterator = hmbdc::pattern::lf_misc::iterator<Sequence>;
    
    static size_t footprint(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num) {
        return sizeof(MemRingBuffer) + decltype(buffer_)::footprint(
            valueTypeSizePower2Num, ringSizePower2Num) + SMP_CACHE_BYTES;
    }

    template <typename Allocator = os::DefaultAllocator>
    MemRingBuffer(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num
        , Allocator& allocator = os::DefaultAllocator::instance())
        : CAPACITY(1u << ringSizePower2Num)
        , VALUE_TYPE_SIZE((1u << valueTypeSizePower2Num) - sizeof(Sequence))
        , READ_SEQ_MAX(std::numeric_limits<Sequence>::max() - CAPACITY - 1000u)
        //1000 is a safe number yet small for how many threads implement a single consumer
        , MASK(CAPACITY - 1)
        , buffer_(valueTypeSizePower2Num, ringSizePower2Num
            , (allocator))
        , toBeClaimedSeq_(0u) {
        std::fill_n(readSeq_, (int)PARALLEL_CONSUMER_COUNT, 0);
        std::fill_n(readSeqLastPurge_, (int)PARALLEL_CONSUMER_COUNT, READ_SEQ_MAX);
        for (auto i = CAPACITY; i != 0 ; --i) {
            *buffer_.getSeq(i - 1) = std::numeric_limits<Sequence>::max();
        }
    }

    void put(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        for (uint32_t k = 0;
            seq >= CAPACITY + readSeqLow();
            ++k) {
            HMBDC_YIELD(k);
        }
        size_t index = seq & MASK;
        memcpy(buffer_ + index
            , item, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        std::atomic_thread_fence(std::memory_order_acquire);
        *buffer_.getSeq(index) = seq;
    }

    bool tryPut(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        for(auto seq = toBeClaimedSeq_; 
            seq < CAPACITY + readSeqLow();
            seq = toBeClaimedSeq_) {
            // if (hmbdc_likely(__atomic_compare_exchange_n (
            //     &toBeClaimedSeq_, &seq, seq + 1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
            if (reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
                ->compare_exchange_weak(seq, seq + 1
                    , std::memory_order_relaxed, std::memory_order_relaxed)) {
                size_t index = seq & MASK;
                memcpy(buffer_ + index
                    , item, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
                std::atomic_thread_fence(std::memory_order_acquire);
                *buffer_.getSeq(index) = seq;
                return true;
            }
        }
        return false;
    }

    void killPut(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);

        while (seq >= CAPACITY + readSeqLow()) {
            uint16_t slowLoc = findSlowestReader();
            markDead(slowLoc);
        }

        size_t index = seq & MASK;
        memcpy(buffer_ + index, item, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        std::atomic_thread_fence(std::memory_order_acquire);
        *buffer_.getSeq(index) = seq;
    }

    bool isFull() const {
        return toBeClaimedSeq_ >= CAPACITY + readSeqLow();
    }


    Sequence readSeq(uint16_t PARALLEL_CONSUMER_INDEX) const HMBDC_RESTRICT {
        return readSeq_[PARALLEL_CONSUMER_INDEX];
    }

    iterator claim() HMBDC_RESTRICT {
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        for (uint32_t k = 0;
            seq >= CAPACITY + readSeqLow();
            ++k) {
            HMBDC_YIELD(k);
        }
        return iterator(buffer_, seq);
    }

    iterator tryClaim() HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        for(auto seq = toBeClaimedSeq_; 
            seq < CAPACITY + readSeqLow();
            seq = toBeClaimedSeq_) {
            // if (hmbdc_likely(__atomic_compare_exchange_n (
            //     &toBeClaimedSeq_, &seq, seq + 1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
             if (reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
                ->compare_exchange_weak(seq, seq + 1
                    , std::memory_order_relaxed, std::memory_order_relaxed)) {
                return iterator(buffer_, seq);
            }
        }
        return iterator();
    }

    /**
     * @brief claim slots in the ring buffer for write, return empty iterator
     * when not possible - this call does not block
     * 
     * @details the user needs to call commit after being done with the slots 
     * @param n size of the claimed slots
     * @return iterator pointing to the start of the slot, or empty when not possible
     */
    iterator tryClaim(size_t n) HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        for(auto seq = toBeClaimedSeq_; 
            seq + n - 1 < CAPACITY + readSeqLow();
            seq = toBeClaimedSeq_) {
            // if (hmbdc_likely(__atomic_compare_exchange_n (
            //     &toBeClaimedSeq_, &seq, seq + n, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
             if (reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
                ->compare_exchange_weak(seq, seq + n
                    , std::memory_order_relaxed, std::memory_order_relaxed)) {
                return iterator(buffer_, seq);
            }
        }
        return iterator();
    }

    iterator claim(size_t n) HMBDC_RESTRICT {
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, n, __ATOMIC_RELAXED);
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(n, std::memory_order_relaxed);
        for (uint32_t k = 0;
            seq + n > CAPACITY + readSeqLow();
            ++k) {
            HMBDC_YIELD(k);
        }
        return iterator(buffer_, seq);
    }

    iterator killClaim() HMBDC_RESTRICT {
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(1, std::memory_order_relaxed);
        while (seq >= CAPACITY + readSeqLow()) {
            uint16_t slowLoc = findSlowestReader();
            markDead(slowLoc);
        }
        return iterator(buffer_, seq);
    }

    iterator killClaim(size_t n) HMBDC_RESTRICT {
        // Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, n, __ATOMIC_RELAXED);
        Sequence seq = reinterpret_cast<std::atomic<Sequence>*>(&toBeClaimedSeq_)
            ->fetch_add(n, std::memory_order_relaxed);
        while (seq + n > CAPACITY + readSeqLow()) {
            uint16_t slowLoc = findSlowestReader();
            markDead(slowLoc);
        }
        return iterator(buffer_, seq);
    }

    void commit(iterator it) HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acq_rel);
        *buffer_.getSeq(*it - buffer_) = it.seq_;
    }

    void commit(iterator from, size_t n) HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acq_rel);
        for (size_t i = 0; i < n; ++i) {
            *buffer_.getSeq(*from - buffer_) = from.seq_;
            ++from;
        }
    }

    void markDead(uint16_t parallel_consumer_index) HMBDC_RESTRICT {
        if (parallel_consumer_index < PARALLEL_CONSUMER_COUNT) {
            readSeq_[parallel_consumer_index] = READ_SEQ_MAX;
            std::atomic_thread_fence(std::memory_order_acq_rel);
        }
    }

    auto
    unusedConsumerIndexes() const {
        std::vector<uint16_t> res;
        for (uint16_t i = 0; i < PARALLEL_CONSUMER_COUNT; ++i) {
            if (readSeq_[i] == READ_SEQ_MAX) {
                res.push_back(i);
            }
        }
        return res;
    }

    void take(uint16_t PARALLEL_CONSUMER_INDEX, void * HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        auto seq = readSeq_[PARALLEL_CONSUMER_INDEX];
        if (hmbdc_unlikely(seq >= READ_SEQ_MAX)) {
            HMBDC_THROW(DeadConsumer, PARALLEL_CONSUMER_INDEX);
        }
        size_t index =  seq & MASK;
        for (uint32_t k = 0;
            seq != *buffer_.getSeq(index);
            ++k) {
            HMBDC_YIELD(k);
        }

        memcpy(item, buffer_ + index, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        // __atomic_fetch_add(readSeq_ + PARALLEL_CONSUMER_INDEX, 1, __ATOMIC_RELEASE);
        reinterpret_cast<std::atomic<Sequence>*>(
            readSeq_ + PARALLEL_CONSUMER_INDEX)->fetch_add(1, std::memory_order_release);
    }

    void takeReentrant(uint16_t PARALLEL_CONSUMER_INDEX, void * HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        std::lock_guard<my_spin_lock> guard(locks_[PARALLEL_CONSUMER_INDEX]);
        take(PARALLEL_CONSUMER_INDEX, item, sizeHint);
    }

    iterator peek(uint16_t PARALLEL_CONSUMER_INDEX) const HMBDC_RESTRICT {
        auto readSeq = readSeq_[PARALLEL_CONSUMER_INDEX];
        if (hmbdc_unlikely(readSeq >= READ_SEQ_MAX)) {
            HMBDC_THROW(DeadConsumer, PARALLEL_CONSUMER_INDEX);
        }
        if (readSeq == *buffer_.getSeq(readSeq & MASK)) {
            return iterator(buffer_, readSeq);
        }
        return iterator();
    }

    size_t peek(uint16_t PARALLEL_CONSUMER_INDEX, iterator& begin, iterator& end
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) const {
        Sequence readSeq = readSeq_[PARALLEL_CONSUMER_INDEX];
        if (hmbdc_unlikely(readSeq >= READ_SEQ_MAX)) {
            HMBDC_THROW(DeadConsumer, PARALLEL_CONSUMER_INDEX);
        }
        begin = iterator(buffer_, readSeq);
        while (readSeq == *buffer_.getSeq(readSeq & MASK)
            && maxPeekSize--) {
            ++readSeq;
        }
        end = iterator(buffer_, readSeq);
        return readSeq - readSeq_[PARALLEL_CONSUMER_INDEX];
    }

    size_t peekSome(uint16_t PARALLEL_CONSUMER_INDEX, iterator& begin, iterator& end
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) const {
        size_t res;
        for (uint32_t k = 0;
            !(res = peek(PARALLEL_CONSUMER_INDEX, begin, end, maxPeekSize))
            && k < 64;
            ++k) {
            HMBDC_YIELD(k);
        }
        return res;
    }

    void waste(uint16_t PARALLEL_CONSUMER_INDEX, size_t size) HMBDC_RESTRICT {
            Sequence seq = readSeq_[PARALLEL_CONSUMER_INDEX];
            if (hmbdc_unlikely(seq >= READ_SEQ_MAX)) {
                HMBDC_THROW(DeadConsumer, PARALLEL_CONSUMER_INDEX);
            }
            for (uint32_t k = 0;
                seq + size > toBeClaimedSeq_;
                ++k) {
                HMBDC_YIELD(k);
            }
            // __atomic_fetch_add(readSeq_ + PARALLEL_CONSUMER_INDEX, size, __ATOMIC_RELEASE);
            reinterpret_cast<std::atomic<Sequence>*>(readSeq_ + PARALLEL_CONSUMER_INDEX)
                ->fetch_add(size, std::memory_order_release);
    }

    /**
     * @brief for batching process, mark the next items consumed
     * @details faster than waste, only use it if the size is 
     * what peek just returned or less. Otherwise, undefined behavior
     * 
     * @param PARALLEL_CONSUMER_INDEX identifying which parallel consumer
     * @param size consume count
     */
    void wasteAfterPeek(uint16_t PARALLEL_CONSUMER_INDEX, size_t size) HMBDC_RESTRICT
    {
        if (!size) return;
        // __atomic_fetch_add(readSeq_ + PARALLEL_CONSUMER_INDEX, size, __ATOMIC_RELEASE);
        reinterpret_cast<std::atomic<Sequence>*>(readSeq_ + PARALLEL_CONSUMER_INDEX)
                ->fetch_add(size, std::memory_order_release);

    }

    Sequence catchUpWith(uint16_t PARALLEL_CONSUMER_INDEX, uint16_t WITH_PARALLEL_CONSUMER_INDEX) {
        readSeq_[PARALLEL_CONSUMER_INDEX] = readSeq_[WITH_PARALLEL_CONSUMER_INDEX];
        std::atomic_thread_fence(std::memory_order_acquire);
        return readSeq_[PARALLEL_CONSUMER_INDEX];
    }

    void catchUpTo(uint16_t PARALLEL_CONSUMER_INDEX, Sequence newSeq) {
        if (readSeq_[PARALLEL_CONSUMER_INDEX] <= newSeq) {
            readSeq_[PARALLEL_CONSUMER_INDEX] = newSeq;
        }
        std::atomic_thread_fence(std::memory_order_release);
    }

    size_t remainingSize(uint16_t index) const HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        Sequence r = readSeq_[index];
        Sequence w = toBeClaimedSeq_;
        return w > r ? w - r : 0;
    }
    size_t remainingSize() const HMBDC_RESTRICT {
        std::atomic_thread_fence(std::memory_order_acquire);
        Sequence r = readSeqLow();
        Sequence w = toBeClaimedSeq_;
        return w > r ? w - r : 0;
    }

    void reset(uint16_t PARALLEL_CONSUMER_INDEX) {
        size_t index;
        do {
            readSeq_[PARALLEL_CONSUMER_INDEX] = toBeClaimedSeq_;
            index = readSeq_[PARALLEL_CONSUMER_INDEX] & MASK;
            std::atomic_thread_fence(std::memory_order_acq_rel);
        } while (*buffer_.getSeq(index) == readSeq_[PARALLEL_CONSUMER_INDEX]);
    }

    size_t parallelConsumerAlive() const {
        return count_if(readSeq_, readSeq_ + PARALLEL_CONSUMER_COUNT
            , [m = READ_SEQ_MAX](Sequence s) {
                return s < m;
            }
        );
    }

    /**
     * @brief call this peroidically to mark the stuck consumers dead
     * @details caller needs to make sure there r new messages between calls
     * @return mask indicating which consumer marked dead
     */
    uint64_t purge() {
        std::atomic_thread_fence(std::memory_order_acq_rel);
        uint64_t res = 0;
        for (uint16_t i = 0; i < PARALLEL_CONSUMER_COUNT; ++i) {
            auto seq = readSeq_[i];
            if (seq < READ_SEQ_MAX) {
                size_t index =  seq & MASK;
                if (hmbdc_unlikely(readSeqLastPurge_[i] == seq)) {
                    if (i == findSlowestReader()) {
                        res |= (1ul << i);
                    }
                } else if (hmbdc_unlikely(seq == *buffer_.getSeq(index))) {
                    readSeqLastPurge_[i] = seq;
                }
            }
        }
        for (uint16_t i = 0; res && i < PARALLEL_CONSUMER_COUNT; ++i) {
            if (res & (1ul << i)) markDead(i);
        }
        return res;
    }
};

} //memringbuffer_detail 

}} // end namespace hmbdc::pattern

#include "hmbdc/pattern/MemRingBuffer2.hpp"

namespace hmbdc { namespace pattern {
template<uint16_t PARALLEL_CONSUMER_COUNT>
using MemRingBuffer = memringbuffer_detail::MemRingBuffer<PARALLEL_CONSUMER_COUNT>;
}}

#pragma GCC diagnostic pop
