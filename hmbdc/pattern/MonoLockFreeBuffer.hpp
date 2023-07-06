#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/os/Allocators.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/pattern/MemRingBuffer.hpp"

#include <utility>
#include <functional>
#include <limits>
#include <stdexcept>
#include <cstddef>

namespace hmbdc { namespace pattern {
    struct MonoLockFreeBuffer {
    using Sequence = HMBDC_SEQ_TYPE;
    using iterator = hmbdc::pattern::lf_misc::iterator<Sequence>;
    using value_type = void *;
    using DeadConsumer = hmbdc::pattern::lf_misc::DeadConsumer;
    enum {
        max_parallel_consumer = 0xffff
    };

    template <typename Allocator = os::DefaultAllocator>
    MonoLockFreeBuffer(size_t, uint32_t, Allocator& allocator = os::DefaultAllocator::instance());

    MonoLockFreeBuffer(MonoLockFreeBuffer const&) = delete;
    MonoLockFreeBuffer* operator = (MonoLockFreeBuffer const&) = delete;
    ~MonoLockFreeBuffer();
    
    size_t maxItemSize() const;
    size_t capacity() const;
    void put(void const*, size_t sizeHint = 0);  
    template <typename T> void put(T const& item) {put(&item, sizeof(item));}
    template <typename T> void putSome(T const& item) {put(&item, std::min(sizeof(item), maxItemSize()));}
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
    
    bool tryPut(void const*, size_t sizeHint = 0);
    bool isFull() const;
    Sequence readSeq() const;
    iterator claim();
    iterator tryClaim();
    iterator claim(size_t);
    iterator tryClaim(size_t);
    iterator killClaim();
    iterator killClaim(size_t);
    void commit(iterator);
    void commit(iterator, size_t);
    void take(void *, size_t = 0);
    bool tryTake(void *, size_t = 0);
    iterator peek();
    size_t peek(iterator&, iterator&
        , size_t maxPeekSize = std::numeric_limits<size_t>::max());
    size_t peekSome(iterator&, iterator&
        , size_t maxPeekSize = std::numeric_limits<size_t>::max());
    /**
     * @brief if size not matching - please refer to the impl for details
     */
    void wasteAfterPeek(iterator, size_t, bool = false);
    size_t remainingSize() const;
    size_t parallelConsumerAlive() const {return 1;}
    void reset();

    static 
    size_t footprint(size_t, uint32_t);
private:
    std::ptrdiff_t impl_;
    std::function<void()> freer_;
};
}}


namespace hmbdc { namespace pattern {
using MMRB = MemRingBuffer<0u>;

namespace {
inline
uint32_t itemSizePower2Num(size_t valueTypeSize) {
    uint32_t res = 1ul;
    valueTypeSize += sizeof(MMRB::Sequence); 
    while ((1ul << res) < valueTypeSize) res++;
    return res;
}
}

#ifdef impl_vptr
#undef impl_vptr
#endif
#define impl_vptr ((void*)(((char*)this) + impl_))

inline
size_t 
MonoLockFreeBuffer::
maxItemSize() const {
	return static_cast<MMRB*>(impl_vptr)->VALUE_TYPE_SIZE;
}

inline
size_t
MonoLockFreeBuffer::
capacity() const {
	return static_cast<MMRB*>(impl_vptr)->CAPACITY;
}

template <typename Allocator>
MonoLockFreeBuffer::
MonoLockFreeBuffer(size_t valueTypeSize, uint32_t ringSizePower2Num, Allocator& allocator)
// : impl_(new MMRB(itemSizePower2Num(valueTypeSize), ringSizePower2Num))
: impl_(((char*)allocator.template allocate<MMRB>(SMP_CACHE_BYTES
        , itemSizePower2Num(valueTypeSize), ringSizePower2Num
        , (allocator))) - (char*)this)
, freer_([this, &allocator](){allocator.unallocate(static_cast<MMRB*>(impl_vptr));})
{}

inline
size_t                      
MonoLockFreeBuffer::
footprint(size_t valueTypeSize, uint32_t ringSizePower2Num){
	return  sizeof(MonoLockFreeBuffer) + SMP_CACHE_BYTES + MMRB::footprint(
        itemSizePower2Num(valueTypeSize), ringSizePower2Num) + SMP_CACHE_BYTES;
}

inline
MonoLockFreeBuffer::
~MonoLockFreeBuffer() {
    freer_();
	// if (allocateFromHeap_) {
 //    	delete static_cast<MMRB*>(impl_vptr);
	// } else {
	// 	static_cast<MMRB*>(impl_vptr)->~MMRB();
	// }
}
                                    
inline void                        MonoLockFreeBuffer::put(void const* item, size_t sizeHint)                	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->put(item, sizeHint);}
inline bool                        MonoLockFreeBuffer::tryPut(void const* item, size_t sizeHint)             	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->tryPut(item, sizeHint);}
inline bool                        MonoLockFreeBuffer::isFull() const                                        	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->isFull();}
inline MonoLockFreeBuffer::Sequence    MonoLockFreeBuffer::readSeq() const                                   	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->readSeq();}
inline MonoLockFreeBuffer::iterator    MonoLockFreeBuffer::claim()                                           	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->claim();}
inline MonoLockFreeBuffer::iterator    MonoLockFreeBuffer::tryClaim()                                        	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->tryClaim();}
inline MonoLockFreeBuffer::iterator    MonoLockFreeBuffer::claim(size_t n)                                   	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->claim(n);}
inline MonoLockFreeBuffer::iterator    MonoLockFreeBuffer::tryClaim(size_t n)                                	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->tryClaim(n);}
inline void                        MonoLockFreeBuffer::commit(MonoLockFreeBuffer::iterator it)               	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->commit(it);}
inline void                        MonoLockFreeBuffer::commit(iterator it, size_t n)                         	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->commit(it, n);}
inline void                        MonoLockFreeBuffer::take(void *i, size_t n)                               	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->take(i, n);}
inline bool                        MonoLockFreeBuffer::tryTake(void *i, size_t n)                            	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->tryTake(i, n);}
inline MonoLockFreeBuffer::iterator    MonoLockFreeBuffer::peek()                                            	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->peek();}
inline size_t                      MonoLockFreeBuffer::peek(iterator& b, iterator& e, size_t s)              	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->peek(b, e, s);}
inline size_t                      MonoLockFreeBuffer::peekSome(iterator& b, iterator& e, size_t s)            HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->peekSome(b, e, s);}
inline void                        MonoLockFreeBuffer::wasteAfterPeek(iterator b, size_t n, bool incomplete )	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->wasteAfterPeek(b, n, incomplete);}
inline size_t                      MonoLockFreeBuffer::remainingSize() const                                 	HMBDC_RESTRICT {return  static_cast<MMRB*>(impl_vptr)->remainingSize();}
inline void                        MonoLockFreeBuffer::reset()                                               	HMBDC_RESTRICT {        static_cast<MMRB*>(impl_vptr)->reset();}

template MonoLockFreeBuffer::MonoLockFreeBuffer(size_t, uint32_t, os::DefaultAllocator&);
template MonoLockFreeBuffer::MonoLockFreeBuffer(size_t, uint32_t, os::ShmBasePtrAllocator&);
}} // end namespace hmbdc::pattern

#undef impl_vptr
