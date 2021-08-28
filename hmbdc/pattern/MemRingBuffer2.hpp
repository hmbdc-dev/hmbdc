#include <atomic>

namespace hmbdc { namespace pattern {

namespace memringbuffer_detail {
template<typename SeqT>
class MemRingBuffer<0u, SeqT> {
public:
    using Sequence = SeqT;
    enum {PARALLEL_CONSUMER_COUNT = 0,}; //not used

    const SeqT CAPACITY;
    const SeqT VALUE_TYPE_SIZE;
private:
    const Sequence MASK;

    hmbdc::pattern::lf_misc::chunk_base_ptr<Sequence> const buffer_;
    Sequence toBeClaimedSeq_
    __attribute__((__aligned__(SMP_CACHE_BYTES)));
    std::atomic<Sequence> readSeq_
    __attribute__((__aligned__(SMP_CACHE_BYTES)));

    bool readyToWriteOver(Sequence seq) const {
        return *buffer_.getSeq(seq & MASK) == std::numeric_limits<Sequence>::max();
    }

    bool readyToWriteOver(Sequence seq, size_t n) const {
        for (auto i = 0ul; i < n; ++i) {
            if (*buffer_.getSeq(seq & MASK) != std::numeric_limits<Sequence>::max()) {
                return false;
            }
        }
        return true;
    }

    void markAsToWriteOver(Sequence seq) {
        *buffer_.getSeq(seq & MASK) = std::numeric_limits<Sequence>::max();
    }


public:
    using value_type = void *;
    using DeadConsumer = hmbdc::pattern::lf_misc::DeadConsumer; //not used, just for compiling
    using iterator = hmbdc::pattern::lf_misc::iterator<Sequence>;
    
    template <typename Allocator = os::DefaultAllocator>
    MemRingBuffer(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num
        , Allocator& allocator = os::DefaultAllocator::instance())
        : CAPACITY(1u << ringSizePower2Num)
        , VALUE_TYPE_SIZE((1u << valueTypeSizePower2Num) - sizeof(Sequence))
        , MASK(CAPACITY - 1)
        , buffer_(valueTypeSizePower2Num, ringSizePower2Num, (allocator))
        , toBeClaimedSeq_(0u)
        , readSeq_(0u) {
        for (auto i = CAPACITY; i != 0 ; --i) {
            markAsToWriteOver(i - 1);
        }
    }

    static 
    size_t footprint(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num) {
        return sizeof(MemRingBuffer) + decltype(buffer_)::footprint(
            valueTypeSizePower2Num, ringSizePower2Num) + SMP_CACHE_BYTES;
    }

    void put(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        // Sequence seq = __sync_fetch_and_add(&toBeClaimedSeq_, 1);
        Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        for (uint32_t k = 0;
            seq >= CAPACITY + readSeq_ || !readyToWriteOver(seq);
            ++k) {
            boost::detail::yield(k);
        }
        auto index = seq & MASK;
        memcpy(buffer_ + index
            , item, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        *buffer_.getSeq(index) = seq;
    }

    bool tryPut(void const* HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        for(auto seq = toBeClaimedSeq_; 
            seq < CAPACITY + readSeq_ && readyToWriteOver(seq);
            seq = toBeClaimedSeq_) {
            if (hmbdc_likely(__sync_bool_compare_and_swap(
                &toBeClaimedSeq_, seq, seq + 1))) {
                auto index = seq & MASK;
                memcpy(buffer_ + index
                    , item, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
                // __sync_synchronize();
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                *buffer_.getSeq(index) = seq;
                return true;
            }
        }
        return false;
    }

    bool isFull() const {
        return toBeClaimedSeq_ >= CAPACITY + readSeq_;
    }

    SeqT readSeq() const {
        return readSeq_;
    }

    iterator claim() HMBDC_RESTRICT {
        // Sequence seq = __sync_fetch_and_add(&toBeClaimedSeq_, 1);
        Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, 1, __ATOMIC_RELAXED);
        for (uint32_t k = 0;
            seq >= CAPACITY + readSeq_ 
                || !readyToWriteOver(seq);
            ++k) {
            boost::detail::yield(k);
        }
        return iterator(buffer_, seq);
    }

    iterator tryClaim() HMBDC_RESTRICT {
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        for(auto seq = toBeClaimedSeq_; 
            seq < CAPACITY + readSeq_ 
                && readyToWriteOver(seq);
            seq = toBeClaimedSeq_) {
            // if (hmbdc_likely(__sync_bool_compare_and_swap(&toBeClaimedSeq_, seq, seq + 1)))
            if (hmbdc_likely(__atomic_compare_exchange_n (
                &toBeClaimedSeq_, &seq, seq + 1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
                return iterator(buffer_, seq);
            }
        }
        return iterator();
    }

    iterator tryClaim(size_t n) HMBDC_RESTRICT {
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        for(auto seq = toBeClaimedSeq_; 
            seq + n - 1 < CAPACITY + readSeq_ 
                && readyToWriteOver(seq, n);
            seq = toBeClaimedSeq_) {
            // if (hmbdc_likely(__sync_bool_compare_and_swap(&toBeClaimedSeq_, seq, seq + n))) {
            if (hmbdc_likely(__atomic_compare_exchange_n (
                &toBeClaimedSeq_, &seq, seq + n, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
                return iterator(buffer_, seq);
            }
        }
        return iterator();
    }

    iterator claim(size_t n) HMBDC_RESTRICT {
        // Sequence seq = __sync_fetch_and_add(&toBeClaimedSeq_, n);
        Sequence seq = __atomic_fetch_add(&toBeClaimedSeq_, n, __ATOMIC_RELAXED);
        for (uint32_t k = 0;
            seq + n > CAPACITY + readSeq_;
            ++k) {
            boost::detail::yield(k);
        }
        for (uint32_t i = 0, k = 0; i < n;) {
            if (!readyToWriteOver(seq + i)) {
                boost::detail::yield(k++);
            } else {
                k = 0;
                i++;
            }
        }
        return iterator(buffer_, seq);
    }

    void commit(iterator it) HMBDC_RESTRICT {
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        *buffer_.getSeq(*it - buffer_) = it.seq_;
    }

    void commit(iterator from, size_t n) HMBDC_RESTRICT {
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        for (size_t i = 0; i < n; ++i) {
            *buffer_.getSeq(*from - buffer_) = from.seq_;
            ++from;
        }
    }

    void take(void * HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        Sequence seq;
        do {
            seq = readSeq_;
            auto index =  seq & MASK;
            for (uint32_t k = 0;
                seq != *buffer_.getSeq(index);
                ++k) {
                boost::detail::yield(k);
            }
            memcpy(item, buffer_ + index, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        // } while (hmbdc_unlikely(!__atomic_compare_exchange_n(
        //     &readSeq_, &seq, seq + 1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)));
        }  while (hmbdc_unlikely(
            !readSeq_.compare_exchange_weak(seq, seq + 1
                , std::memory_order_relaxed, std::memory_order_relaxed)));
        markAsToWriteOver(seq);
    }

    bool tryTake(void * HMBDC_RESTRICT item, size_t sizeHint = 0) HMBDC_RESTRICT {
        Sequence seq;
        do {
            seq = readSeq_;
            auto index =  seq & MASK;
            if (seq != *buffer_.getSeq(index)) return false;
            memcpy(item, buffer_ + index, sizeHint ? sizeHint : VALUE_TYPE_SIZE);
        // } while (hmbdc_unlikely(!__atomic_compare_exchange_n(
        //     &readSeq_, &seq, seq + 1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)));
        }  while (hmbdc_unlikely(
            !readSeq_.compare_exchange_weak(seq, seq + 1
                , std::memory_order_relaxed, std::memory_order_relaxed)));

        markAsToWriteOver(seq);
        return true;
    }

    iterator peek() HMBDC_RESTRICT {
        Sequence seq, readSeq;
        iterator res;
        do {
            res.clear();
            readSeq = readSeq_;
            seq = readSeq;
            if (seq == *buffer_.getSeq(seq & MASK)) {
                res = iterator(buffer_, seq++);
            } 
        // } while (hmbdc_unlikely(!__atomic_compare_exchange_n(
        //     &readSeq_, &readSeq, seq, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)));
        }  while (hmbdc_unlikely(
            !readSeq_.compare_exchange_weak(readSeq, seq
                , std::memory_order_relaxed, std::memory_order_relaxed)));
        return res;
    }

    size_t peek(iterator& begin, iterator& end
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) HMBDC_RESTRICT {
        Sequence seq, readSeq;
        do {
            readSeq = readSeq_;
            seq = readSeq;
            begin = iterator(buffer_, seq);
            auto count = maxPeekSize;
            while (count--
                && seq == *buffer_.getSeq(seq & MASK)) {
                ++seq;
            }
            end = iterator(buffer_, seq);
        // } while (hmbdc_unlikely(!__atomic_compare_exchange_n(
        //     &readSeq_, &readSeq, seq, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)));
        }  while (hmbdc_unlikely(
            !readSeq_.compare_exchange_weak(readSeq, seq
                , std::memory_order_relaxed, std::memory_order_relaxed)));
        
        return end - begin;
    }

    size_t peekSome(iterator& begin, iterator& end
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) {
        size_t res;
        for (uint32_t k = 0;
            !(res = peek(begin, end, maxPeekSize))
            && k < 64;
            ++k) {
            HMBDC_YIELD(k);
        }
        return res;
    }

    size_t peekAll(iterator& begin, iterator& end) HMBDC_RESTRICT {
        Sequence seq, readSeq;
        do {
            readSeq = readSeq_;
            seq = readSeq;
            begin = iterator(buffer_, seq);

            for (uint32_t k = 0;
                seq < toBeClaimedSeq_;
                boost::detail::yield(k++)) {
                while (seq == *buffer_.getSeq(seq & MASK)) {
                    ++seq;
                }
            }
            end = iterator(buffer_, seq);
        // } while (hmbdc_unlikely(!__atomic_compare_exchange_n(
        //     &readSeq_, &readSeq, seq, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)));
        }  while (hmbdc_unlikely(
            !readSeq_.compare_exchange_weak(readSeq, seq
                , std::memory_order_relaxed, std::memory_order_relaxed)));
        return end - begin;
    }

    void wasteAfterPeek(iterator begin, size_t size, bool incomplete = false) HMBDC_RESTRICT {
        if (!size && !incomplete) return;
        auto seq = begin.seq();
        for (; seq < begin.seq() + size; seq++) {
            markAsToWriteOver(seq);
        }
        //this is very dangerous if size is not matching;
        //if any reads other than the matching peek happened
        //bad things happen
        if (incomplete) readSeq_ = seq; 
        // __sync_synchronize();
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    size_t remainingSize() const HMBDC_RESTRICT {
        __sync_synchronize();
        Sequence r = readSeq_;
        Sequence w = toBeClaimedSeq_;
        return w > r ? w - r : 0;
    }

    void reset() {
        __sync_synchronize();
        for (auto i = 0ul; i < CAPACITY; ++i) {
            *buffer_.getSeq(i & MASK) = std::numeric_limits<Sequence>::max();
        }
        // __atomic_store(&readSeq_, &toBeClaimedSeq_, __ATOMIC_RELEASE);
        readSeq_ = toBeClaimedSeq_;
    }
};
} //memringbuffer_detail


}} // end namespace hmbdc::pattern

