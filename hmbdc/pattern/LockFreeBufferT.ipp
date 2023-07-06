#include "hmbdc/pattern/MemRingBuffer.hpp"

#include <fstream>
#include <utility>
#include <string>
#include <stdlib.h>
#include <stdint.h>


#ifdef _QNX_SOURCE
#include <process.h>
#define __GNUC_PREREQ(x, y) 0
char const* const __progname_full = _cmdname(nullptr);
#else
extern const char * const __progname_full;
#endif

namespace {
template <typename MRB>
uint32_t itemSizePower2NumT(size_t valueTypeSize) {
    uint32_t res = 1ul;
    valueTypeSize += sizeof(typename MRB::Sequence); 
    while ((1ul << res) < valueTypeSize) res++;
    return res;
}
}

#ifdef impl_vptr
#undef impl_vptr
#endif
#define impl_vptr ((void*)(((char*)this) + impl_))

namespace hmbdc { namespace pattern {

template <uint16_t mpc>
using MRB = MemRingBuffer<mpc>;

template <uint16_t mpc>
size_t 
LockFreeBufferT<mpc>::
capacity() const {
    return static_cast<MRB<mpc>*>(impl_vptr)->CAPACITY;
}
template <uint16_t mpc>
size_t 
LockFreeBufferT<mpc>::
maxItemSize() const {
    return static_cast<MRB<mpc>*>(impl_vptr)->VALUE_TYPE_SIZE;
}

template <uint16_t mpc>
template <typename Allocator>
LockFreeBufferT<mpc>::
LockFreeBufferT(size_t valueTypeSize, uint32_t ringSizePower2Num
    , Allocator& allocator)
: impl_(((char*)allocator.template allocate<MRB<mpc>>(SMP_CACHE_BYTES
        , itemSizePower2NumT<MRB<mpc>>(valueTypeSize), ringSizePower2Num
        , (allocator))) - (char*)this)
, allocateFromHeap_(Allocator::fromHeap) {
}

template <uint16_t mpc>
LockFreeBufferT<mpc>::
~LockFreeBufferT() {
    if (allocateFromHeap_) {
        delete static_cast<MRB<mpc>*>(impl_vptr);
    } else {
        using mrb = MRB<mpc>;
        static_cast<mrb*>(impl_vptr)->~mrb();
    }
}

template <uint16_t mpc>
size_t
LockFreeBufferT<mpc>::
footprint(size_t valueTypeSize, uint32_t ringSizePower2Num) {
    return sizeof(LockFreeBufferT) + SMP_CACHE_BYTES + MRB<mpc>::footprint(
        itemSizePower2NumT<MRB<mpc>>(valueTypeSize), ringSizePower2Num) + SMP_CACHE_BYTES;
}



using iterator = hmbdc::pattern::lf_misc::iterator<HMBDC_SEQ_TYPE>;
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::put(void const* item, size_t sizeHint)                                           HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->put(item, sizeHint);}
template <uint16_t mpc> bool                              LockFreeBufferT<mpc>::tryPut(void const* item, size_t sizeHint)                                        HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->tryPut(item, sizeHint);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::killPut(void const* i, size_t sizeHint)                                          HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->killPut(i, sizeHint);}
template <uint16_t mpc> bool                              LockFreeBufferT<mpc>::isFull() const                                                                   HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->isFull();}
template <uint16_t mpc> HMBDC_SEQ_TYPE                    LockFreeBufferT<mpc>::readSeq(uint16_t PARALLEL_CONSUMER_INDEX) const                                  HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->readSeq(PARALLEL_CONSUMER_INDEX);}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::claim()                                                                          HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->claim();}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::tryClaim()                                                                       HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->tryClaim();}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::claim(size_t n)                                                                  HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->claim(n);}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::tryClaim(size_t n)                                                               HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->tryClaim(n);}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::killClaim()                                                                      HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->killClaim();}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::killClaim(size_t n)                                                              HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->killClaim(n);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::commit(LockFreeBufferT<mpc>::iterator it)                                        HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->commit(it);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::commit(iterator it, size_t n)                                                    HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->commit(it, n);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::markDead(uint16_t PARALLEL_CONSUMER_INDEX)                                       HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->markDead(PARALLEL_CONSUMER_INDEX);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::take(uint16_t PARALLEL_CONSUMER_INDEX, void *i, size_t n)                        HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->take(PARALLEL_CONSUMER_INDEX, i, n);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::takeReentrant(uint16_t PARALLEL_CONSUMER_INDEX, void *i, size_t n)               HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->takeReentrant(PARALLEL_CONSUMER_INDEX, i, n);}
template <uint16_t mpc> iterator                          LockFreeBufferT<mpc>::peek(uint16_t PARALLEL_CONSUMER_INDEX) const                                     HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->peek(PARALLEL_CONSUMER_INDEX);}
template <uint16_t mpc> size_t                            LockFreeBufferT<mpc>::peek(uint16_t PARALLEL_CONSUMER_INDEX, iterator& b, iterator& e, size_t s) const HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->peek(PARALLEL_CONSUMER_INDEX, b, e, s);}
template <uint16_t mpc> size_t                            LockFreeBufferT<mpc>::peekSome(uint16_t PARALLEL_CONSUMER_INDEX, iterator& b, iterator& e, size_t s) const HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->peekSome(PARALLEL_CONSUMER_INDEX, b, e, s);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::waste(uint16_t PARALLEL_CONSUMER_INDEX, size_t n)                                HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->waste(PARALLEL_CONSUMER_INDEX, n);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::wasteAfterPeek(uint16_t PARALLEL_CONSUMER_INDEX, size_t n)                       HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->wasteAfterPeek(PARALLEL_CONSUMER_INDEX, n);}
template <uint16_t mpc> HMBDC_SEQ_TYPE                    LockFreeBufferT<mpc>::catchUpWith(uint16_t PARALLEL_CONSUMER_INDEX, uint16_t WITH)                     HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->catchUpWith(PARALLEL_CONSUMER_INDEX, WITH);}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::catchUpTo(uint16_t PARALLEL_CONSUMER_INDEX, Sequence seq)                        HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->catchUpTo(PARALLEL_CONSUMER_INDEX, seq);}
template <uint16_t mpc> size_t                            LockFreeBufferT<mpc>::remainingSize(uint16_t PARALLEL_CONSUMER_INDEX) const                            HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->remainingSize(PARALLEL_CONSUMER_INDEX);}
template <uint16_t mpc> size_t                            LockFreeBufferT<mpc>::remainingSize() const                                                            HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->remainingSize();}
template <uint16_t mpc> size_t                            LockFreeBufferT<mpc>::parallelConsumerAlive() const                                                    HMBDC_RESTRICT{return  static_cast<MRB<mpc>*>(impl_vptr)->parallelConsumerAlive();}
template <uint16_t mpc> void                              LockFreeBufferT<mpc>::reset(uint16_t PARALLEL_CONSUMER_INDEX)                                          HMBDC_RESTRICT{        static_cast<MRB<mpc>*>(impl_vptr)->reset(PARALLEL_CONSUMER_INDEX);}
template <uint16_t mpc> uint64_t                          LockFreeBufferT<mpc>::purge()                                                                                      {return  static_cast<MRB<mpc>*>(impl_vptr)->purge();}
template <uint16_t mpc> std::vector<uint16_t>             LockFreeBufferT<mpc>::unusedConsumerIndexes() const                                                                {return  static_cast<MRB<mpc>*>(impl_vptr)->unusedConsumerIndexes();}

}} // end namespace 

#undef impl_vptr