#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/os/Allocators.hpp"

#include "hmbdc/Config.hpp"

#include <utility>
#include <stdexcept>
#include <vector>
#include <limits>
#include <cstddef>

namespace hmbdc { namespace pattern {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

template <uint16_t MAX_PARALLE_CONSUMER>
struct LockFreeBufferT {
    using Sequence = HMBDC_SEQ_TYPE;
    using iterator = hmbdc::pattern::lf_misc::iterator<Sequence>;
    using value_type = void *;
    using DeadConsumer = hmbdc::pattern::lf_misc::DeadConsumer;
    enum {
        max_parallel_consumer = MAX_PARALLE_CONSUMER
    };

    template <typename Allocator = os::DefaultAllocator>
    LockFreeBufferT(size_t, uint32_t, Allocator& allocator = os::DefaultAllocator::instance());
    
    LockFreeBufferT(LockFreeBufferT const&) = delete;
    LockFreeBufferT& operator = (LockFreeBufferT const&) = delete;
    ~LockFreeBufferT();
    size_t capacity() const;
    size_t maxItemSize() const;

    void put(void const*, size_t sizeHint = 0);
    template <typename T> void put(T const& item) {put(&item, sizeof(T));}
    template <typename T> void putSome(T const& item) {put(&item, std::min(sizeof(item), maxItemSize()));}
    
    bool tryPut(void const*, size_t sizeHint = 0);
    template <typename T> bool tryPut(T const& item) {return tryPut(&item, sizeof(T));}

    void killPut(void const*, size_t sizeHint = 0);
    
    template <typename T> void killPut(T const& item) {killPut(&item, sizeof(T));}

    template <typename T, typename ...Args>
    void putInPlace(Args&&... args) {
        auto s = claim();
        new (*s) T(std::forward<Args>(args)...);
        commit(s);
    }

    template <typename T, typename ...Args>
    bool tryPutInPlace(Args&&... args) {
        auto s = tryClaim();
        if (!s) return false;
        new (*s) T(std::forward<Args>(args)...);
        commit(s);
        return true;
    }

    bool isFull() const;
    Sequence readSeq(uint16_t PARALLEL_CONSUMER_INDEX) const;
    iterator claim();
    iterator tryClaim();
    iterator claim(size_t);
    iterator tryClaim(size_t);
    iterator killClaim();
    iterator killClaim(size_t);
    void commit(iterator);
    void commit(iterator, size_t);
    void markDead(uint16_t);

    template <typename T>
    T take(uint16_t PARALLEL_CONSUMER_INDEX) {
        T res; 
        take(PARALLEL_CONSUMER_INDEX, &res, sizeof(res)); 
        return res;
    }

    void take(uint16_t PARALLEL_CONSUMER_INDEX, void *, size_t = 0);
    void takeReentrant(uint16_t PARALLEL_CONSUMER_INDEX, void *, size_t = 0);
    iterator peek(uint16_t PARALLEL_CONSUMER_INDEX) const;
    size_t peek(uint16_t PARALLEL_CONSUMER_INDEX, iterator&, iterator&
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) const;
    size_t peekSome(uint16_t PARALLEL_CONSUMER_INDEX, iterator&, iterator&
        , size_t maxPeekSize = std::numeric_limits<size_t>::max()) const;
    void waste(uint16_t PARALLEL_CONSUMER_INDEX, size_t);
    void wasteAfterPeek(uint16_t PARALLEL_CONSUMER_INDEX, size_t);
    Sequence catchUpWith(uint16_t PARALLEL_CONSUMER_INDEX, uint16_t);
    void catchUpTo(uint16_t PARALLEL_CONSUMER_INDEX, Sequence);

    size_t remainingSize(uint16_t PARALLEL_CONSUMER_INDEX) const;
    size_t remainingSize() const;
    size_t parallelConsumerAlive() const;
    void reset(uint16_t PARALLEL_CONSUMER_INDEX);

    static 
    size_t footprint(size_t, uint32_t);
    uint64_t purge();

    std::vector<uint16_t> 
    unusedConsumerIndexes() const;

    uint16_t
    takeUnusedConsumer();

private:
    std::ptrdiff_t impl_;
    bool allocateFromHeap_;
};

}} // end namespace hmbdc::pattern

#include "hmbdc/pattern/LockFreeBufferT.ipp"

#pragma GCC diagnostic pop
