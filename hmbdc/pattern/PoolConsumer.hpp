#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Config.hpp"
#include <boost/smart_ptr/detail/yield_k.hpp>
#include <limits>
#include <atomic>
#include <iostream>

#include <stdexcept>

namespace hmbdc { namespace pattern {

struct PoolConsumer {
    friend struct PoolImpl;

    template <typename Buffer>
    friend struct PoolTImpl;
    friend struct PoolMinusImpl;
    PoolConsumer(bool interestedInMessages, time::TimerManager* tmPtr, std::atomic<size_t>* pDispStartCount);
    PoolConsumer(PoolConsumer const&) = delete;
    PoolConsumer& operator = (PoolConsumer const&) = delete;
    virtual ~PoolConsumer();
    void stopped(std::exception const&) noexcept;
    bool dropped() noexcept;
    void messageDispatchingStarted() {
        if (hmbdc_unlikely(!messageDispatchingStarted_)) {
            messageDispatchingStartedCb(pDispStartCount_
            ? (++*pDispStartCount_, pDispStartCount_)
            :nullptr);
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
    virtual void messageDispatchingStartedCb(std::atomic<size_t> const*) {}
    virtual size_t handleRangeImpl(BufIt begin, 
        BufIt end, uint16_t threadId){ return 0; };
    virtual void invokedCb(size_t) {}
    virtual void stoppedCb(std::exception const&) {}
    virtual bool droppedCb() { return true; }
    
    hmbdc::time::TimerManager* tmPtr;
    uint64_t poolThreadAffinity;
    std::atomic<uint16_t> droppedCount;
    uint64_t skippedPoolThreadMask_;
    alignas(SMP_CACHE_BYTES) HMBDC_SEQ_TYPE nextSeq_;
    bool interestedInMessages;
    bool messageDispatchingStarted_;
    std::atomic<size_t>* pDispStartCount_ = nullptr;
};

}} // end namespace hmbdc::pattern


namespace hmbdc { namespace pattern {

inline
PoolConsumer::
PoolConsumer(bool interestedInMessages, time::TimerManager* tmPtr, std::atomic<size_t>* pDispStartCount)
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
    auto tmp = std::numeric_limits<typename BufIt::Sequence>::max();
    if (hmbdc_unlikely(nextSeq_ == std::numeric_limits<typename BufIt::Sequence>::max()
        && reinterpret_cast<std::atomic<HMBDC_SEQ_TYPE>*>(&nextSeq_)
                ->compare_exchange_strong(tmp
                    , begin.seq(), std::memory_order_acq_rel))) {
        // && __sync_bool_compare_and_swap(&nextSeq_
        // , std::numeric_limits<typename BufIt::Sequence>::max(), begin.seq()))) {
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
        if (hmbdc_likely(reinterpret_cast<std::atomic<HMBDC_SEQ_TYPE>*>(&nextSeq_)
                ->compare_exchange_strong(nextSeq
                    , IGNORE, std::memory_order_acq_rel))) {
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
            std::atomic_thread_fence(std::memory_order_acq_rel);
            if (hmbdc_likely(res)) nextSeq_ = end.seq();
        }
    } else if (hmbdc_unlikely(!interestedInMessages 
        || (end.seq() == nextSeq && begin.seq() == nextSeq))) {
        if (hmbdc_unlikely(reinterpret_cast<std::atomic<HMBDC_SEQ_TYPE>*>(&nextSeq_)
                ->compare_exchange_strong(nextSeq
                    , IGNORE, std::memory_order_acq_rel))) {
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
            std::atomic_thread_fence(std::memory_order_acq_rel);
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
