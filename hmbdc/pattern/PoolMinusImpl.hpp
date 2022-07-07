#include "RingBuffer.hpp"
#include "hmbdc/numeric/BitMath.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/pattern/PoolMinus.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"
#include "hmbdc/os/Thread.hpp"

#include <boost/smart_ptr/detail/yield_k.hpp>
#include <utility>
#include <functional>
#include <vector>
#include <thread>
#include <string>

#pragma GCC diagnostic push
#ifdef __clang__        
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

namespace hmbdc { namespace pattern {

struct PoolMinusImpl {
    PoolMinusImpl(PoolMinusImpl const&) = delete;
    PoolMinusImpl& operator = (PoolMinusImpl const&) = delete;

    PoolMinusImpl(uint16_t maxConsumerSize
        , uint64_t activePoolThreadSequenceMask = 0) //if set other than 0, start cannot be called
    : stopped_(false)
    , activePoolThreadSequenceMask_(activePoolThreadSequenceMask)
    , consumerCount_(0u)
    , consumerQ_(hmbdc::numeric::log2Upper(maxConsumerSize * 2u)) {
    }

    uint32_t consumerSize() const {
        return consumerCount_;
    }

    void addConsumer(PoolConsumer& c, uint64_t poolThreadAffinityIn) {
        auto count = ++consumerCount_;
        if (count > consumerQ_.CAPACITY) {
            --consumerCount_;
            HMBDC_THROW(std::runtime_error
                , "too many consumers, poolsize = " << consumerCount_);
        }

        auto ptr = static_cast<PoolConsumer*>(&c);
        ptr->reset();
        c.poolThreadAffinity = poolThreadAffinityIn;
        consumerQ_.put(&c);
    }

    void start(uint16_t threadCount, uint64_t cpuAffinityMask) {
        if (threadCount > 64u) {
            HMBDC_THROW(std::runtime_error, 
                "threadCount too large " << threadCount << " > 64");
        }
        if (activePoolThreadSequenceMask_) {
            HMBDC_THROW(std::runtime_error, "cannot restart a pool.");
        }
        activePoolThreadSequenceMask_ = (1ul << threadCount) - 1u;
        for (auto i = 0u; i < threadCount; ++i) {
            threads_.push_back(
                std::thread([this, i, cpuAffinityMask]() {
                    threadEntry(i, cpuAffinityMask);
                })
            );
        }
    }

    void stop() {
        std::atomic_thread_fence(std::memory_order_acq_rel);
        stopped_ = true;
    }

    void join() {
        for (Threads::value_type& t : threads_) t.join();
        threads_.clear();
    }

    using ConsumerQ = RingBuffer<PoolConsumer*, 0>;

    template <typename HoggingCond>
    void runOnce(uint16_t threadSerialNumber
        , HoggingCond&& hoggingCond) {
        runOnceImpl(threadSerialNumber, std::forward<HoggingCond>(hoggingCond));
    }

    void cleanup(uint16_t threadSerialNumber) {
        if (stopped_) {
            while (consumerCount_) { 
                PoolConsumer* consumer;
                if (consumerQ_.tryTake(consumer)) {
                    if (hmbdc_unlikely(!(consumer->poolThreadAffinity & (1u << threadSerialNumber)))) {
                        consumerQ_.put(consumer);
                        continue;
                    }
                    --consumerCount_;
                    consumer->dropped();
                }
            }
        }
    }

private:
    void threadEntry(uint16_t threadSerialNumber, uint64_t cpuAffinityMask) HMBDC_RESTRICT {
        std::string name = "hmbdcp" + std::to_string(threadSerialNumber);
        if (cpuAffinityMask == 0) {
            auto cpuCount = std::thread::hardware_concurrency();
            cpuAffinityMask = (1ul << cpuCount) - 1u;
        }
        cpuAffinityMask = hmbdc::numeric::nthSetBitFromLsb(cpuAffinityMask
            , threadSerialNumber % hmbdc::numeric::setBitsCount(cpuAffinityMask));

        hmbdc::os::configureCurrentThread(name.c_str(), cpuAffinityMask);
        
        while(!stopped_){
            runOnceImpl(threadSerialNumber, [](){return true;});
        }
        cleanup(threadSerialNumber);
    }
    
    template <typename HoggingCond>
    void runOnceImpl(uint16_t threadSerialNumber, HoggingCond&& hoggingCond) HMBDC_RESTRICT {
        if (hmbdc_unlikely(stopped_)) return;
        PoolConsumer* consumer;
        if (hmbdc_unlikely(!consumerQ_.tryTake(consumer))) return;
        if (hmbdc_unlikely(!(consumer->poolThreadAffinity & (1u << threadSerialNumber)))) {
            consumerQ_.put(consumer);
            return;
        }
        try {
            if (hmbdc_unlikely(!consumer->messageDispatchingStarted_)) {
                consumer->messageDispatchingStarted();
            }
            do {
                consumer->invoked(threadSerialNumber);
            } while (hoggingCond() && consumerQ_.remainingSize() == 0 && !stopped_);
        } catch (std::exception const& e) {
            consumer->stopped(e);
            --consumerCount_;
            if (!consumer->dropped()) {//resume
                addConsumer(*consumer
                    , consumer->poolThreadAffinity & activePoolThreadSequenceMask_);
            }
            return;
        }
        consumerQ_.put(consumer);
    }


    using Threads = std::vector<std::thread>;
    Threads threads_;
    bool stopped_;
    uint64_t activePoolThreadSequenceMask_;
    std::atomic<uint32_t> consumerCount_;
    ConsumerQ consumerQ_;
};
}}

#pragma GCC diagnostic pop

#include "hmbdc/os/Allocators.hpp"

namespace hmbdc { namespace pattern {
    
inline
PoolMinus::
PoolMinus(uint32_t maxConsumerSize)
: impl_(os::DefaultAllocator::instance().allocate<PoolMinusImpl>(SMP_CACHE_BYTES, maxConsumerSize)) {
}

inline
PoolMinus::
~PoolMinus() {
    os::DefaultAllocator::instance().unallocate(static_cast<PoolMinusImpl*>(impl_));
}

inline void     PoolMinus::addConsumer(PoolConsumer& c, uint64_t poolThreadAffinityIn)            {       static_cast<PoolMinusImpl*>(impl_)->addConsumer(c, poolThreadAffinityIn);} 
inline uint32_t PoolMinus::consumerSize() const                                                   {return static_cast<PoolMinusImpl*>(impl_)->consumerSize();}  
inline void     PoolMinus::start(uint16_t threadCount, uint64_t cpuAffinityMask, bool)            {       static_cast<PoolMinusImpl*>(impl_)->start(threadCount, cpuAffinityMask);}
inline void     PoolMinus::runOnce(uint16_t threadSerialNumber)                    HMBDC_RESTRICT {       static_cast<PoolMinusImpl*>(impl_)->runOnce(threadSerialNumber, [](){return true;});}
inline void     PoolMinus::stop()                                                                 {       static_cast<PoolMinusImpl*>(impl_)->stop();}    
inline void     PoolMinus::join()                                                                 {       static_cast<PoolMinusImpl*>(impl_)->join();}      
}}
