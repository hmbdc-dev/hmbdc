#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Config.hpp"
#include <boost/smart_ptr/detail/yield_k.hpp>
#include <limits>
#include <iostream>

#include <stdexcept>

namespace hmbdc { namespace pattern {

struct PoolConsumer {
    friend struct PoolImpl;

    template <typename Buffer>
    friend struct PoolTImpl;
    friend struct PoolMinusImpl;
    PoolConsumer(bool interestedInMessages, time::TimerManager* tmPtr, size_t* pDispStartCount);
    virtual ~PoolConsumer();
    void stopped(std::exception const&) noexcept;
    bool dropped() noexcept;
    void messageDispatchingStarted() {
        if (hmbdc_unlikely(!messageDispatchingStarted_)) {
            messageDispatchingStartedCb(pDispStartCount_
            ?(__atomic_add_fetch(pDispStartCount_, 1, __ATOMIC_RELEASE), pDispStartCount_)
            :0);
            messageDispatchingStarted_ = true;
        }
    }
    void invoked(size_t);

protected:
    using BufIt = lf_misc::iterator<HMBDC_SEQ_TYPE>;

private:
    void reset();
    bool handleRange(BufIt begin, 
        BufIt end, uint16_t threadId) noexcept;
    bool handleInvokeOnly(uint16_t threadId) noexcept;
    virtual void messageDispatchingStartedCb(size_t const*) {}
    virtual size_t handleRangeImpl(BufIt begin, 
        BufIt end, uint16_t threadId){ return 0; };
    virtual void invokedCb(size_t) {}
    virtual void stoppedCb(std::exception const&) {}
    virtual bool droppedCb() { return true; }
    
    hmbdc::time::TimerManager* tmPtr;
    uint64_t poolThreadAffinity;
    uint16_t droppedCount;
    uint64_t skippedPoolThreadMask_;
    HMBDC_SEQ_TYPE nextSeq_ __attribute__((__aligned__(SMP_CACHE_BYTES)));
    bool interestedInMessages;
    bool messageDispatchingStarted_;
    size_t* pDispStartCount_ = nullptr;
};

}} // end namespace hmbdc::pattern


namespace hmbdc { namespace pattern {

inline
PoolConsumer::
PoolConsumer(bool interestedInMessages, time::TimerManager* tmPtr, size_t* pDispStartCount)
: tmPtr(tmPtr)
, poolThreadAffinity(0xfffffffffffffffful)
, droppedCount(0u)
, nextSeq_(std::numeric_limits<typename BufIt::Sequence>::max()) 
, interestedInMessages(interestedInMessages)
, messageDispatchingStarted_(false)
, pDispStartCount_(pDispStartCount) {
}

inline
void
PoolConsumer::
reset() {
    poolThreadAffinity = 0xfffffffffffffffful;
    droppedCount = 0u;
    nextSeq_ = std::numeric_limits<uint64_t>::max();
}

inline
PoolConsumer::
~PoolConsumer(){
}

inline
void 
PoolConsumer::
invoked(size_t n) {
    if (tmPtr) {
        tmPtr->checkTimers(time::SysTime::now());
    }
    invokedCb(n);
}

inline
bool 
PoolConsumer::
handleRange(BufIt begin, 
    BufIt end, uint16_t threadId) HMBDC_RESTRICT noexcept {
    const auto STOP = std::numeric_limits<typename BufIt::Sequence>::max() - 1;
    const auto IGNORE = std::numeric_limits<typename BufIt::Sequence>::max() - 2;
    bool res = true;
    if (hmbdc_unlikely(nextSeq_ == std::numeric_limits<typename BufIt::Sequence>::max()
        && __sync_bool_compare_and_swap(&nextSeq_
        , std::numeric_limits<typename BufIt::Sequence>::max(), begin.seq()))) {
        try {
            messageDispatchingStarted();
        }  
        catch (std::exception const& e) {
                nextSeq_ = STOP;
                stopped(e);
                res = false;
        } catch (int c) {
                nextSeq_ = STOP;
                stopped(hmbdc::ExitCode(c));
                res = false;
        } catch (...) {
                nextSeq_ = STOP;
                stopped(hmbdc::UnknownException());
                res = false;
        }
    }
    auto nextSeq = nextSeq_;
    if (hmbdc_unlikely(nextSeq == STOP)) return false;
    else if (hmbdc_unlikely(nextSeq == IGNORE)) return true;
    if ((hmbdc_unlikely(interestedInMessages && begin.seq() > nextSeq))) {
        return true;
    } else if (hmbdc_likely(end.seq() > nextSeq)) {
        if (hmbdc_likely(__sync_bool_compare_and_swap(&nextSeq_, nextSeq, IGNORE))) {
            try {
                size_t res = 0;
                if (hmbdc_likely(interestedInMessages)) {
                    PoolConsumer::BufIt myStart = begin +  (size_t)(nextSeq - begin.seq());
                    res = handleRangeImpl(myStart, end, threadId);
                }
                invoked(res);
            } catch (std::exception const& e) {
                nextSeq_ = STOP;
                stopped(e);
                res = false;
            } catch (int c) {
                nextSeq_ = STOP;
                stopped(hmbdc::ExitCode(c));
                res = false;
            } catch (...) {
                nextSeq_ = STOP;
                stopped(hmbdc::UnknownException());
                res = false;
            }
            __sync_synchronize();
            if (hmbdc_likely(res)) nextSeq_ = end.seq();
        }
    } else if (hmbdc_unlikely(!interestedInMessages 
        || (end.seq() == nextSeq && begin.seq() == nextSeq))) {
        if (hmbdc_unlikely(__sync_bool_compare_and_swap(&nextSeq_, nextSeq, IGNORE))) {
            try {
                invoked(0);
            } catch (std::exception const& e) {
                nextSeq_ = STOP;
                stopped(e);
                res = false;
            } catch (int c) {
                nextSeq_ = STOP;
                stopped(hmbdc::ExitCode(c));
                res = false;
            } catch (...) {
                nextSeq_ = STOP;
                stopped(hmbdc::UnknownException());
                res = false;
            }
            __sync_synchronize();
            if (hmbdc_likely(res)) nextSeq_ = end.seq();
        }
    }

    return res;
}

inline
void
PoolConsumer::
stopped(std::exception const&e) noexcept {
    try {
        stoppedCb(e);
    } catch (...) {}
}

inline
bool
PoolConsumer::
dropped() noexcept {
    bool res = true;
    try {
        res = droppedCb();
    } catch (...) {}
    return res;
}

}} // end namespace hmbdc::pattern
