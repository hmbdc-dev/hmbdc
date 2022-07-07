#include "hmbdc/pattern/RingBuffer.hpp"
#include "hmbdc/numeric/BitMath.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"
#include "hmbdc/pattern/PoolMinusImpl.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"
#include "hmbdc/os/Thread.hpp"
#include "hmbdc/os/Allocators.hpp"

#include <boost/smart_ptr/detail/yield_k.hpp>

#include <random>
#include <atomic>
#include <functional>
#include <vector>
#include <thread>
#include <string>

#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wshift-count-overflow"

namespace hmbdc { namespace pattern {

template <typename LockFreeBuffer>
struct PoolTImpl {
    using BufIt = typename LockFreeBuffer::iterator;

    PoolTImpl(PoolTImpl const&) = delete;
    PoolTImpl& operator = (PoolTImpl const&) = delete;

    PoolTImpl(LockFreeBuffer& lfb, uint16_t maxConsumerSize)
    : lfb_(lfb)
    , maxBatchMessageCount_(lfb.capacity())
    , poolMinus_(maxConsumerSize, -1) 
    , stopped_(false)
    , activePoolThreadSequenceMask_(0)
    , consumerCount_(0u)
    , hmbdcNumbers_{0xffff} {
        for (auto i = 0; i < LockFreeBuffer::max_parallel_consumer; ++i) {
            consumerQueues_.push_back(os::DefaultAllocator::instance().allocate<ConsumerQ>(SMP_CACHE_BYTES
                    , hmbdc::numeric::log2Upper(maxConsumerSize * 2u)
                )
            );
        }
    }

    ~PoolTImpl() {
        for (auto pc : consumerQueues_) {
            os::DefaultAllocator::instance().unallocate<ConsumerQ>(pc);
        }
    }

    uint32_t consumerSize() const {
        return consumerCount_ + poolMinus_.consumerSize();
    }

    void addConsumer(PoolConsumer& c, uint64_t poolThreadAffinityIn) {
        if (!c.interestedInMessages) {
            poolMinus_.addConsumer(c, poolThreadAffinityIn);
            return;
        }
        auto count = ++consumerCount_;
        if (count > consumerQueues_[0]->CAPACITY / 2u) {
            --consumerCount_;
            HMBDC_THROW(std::runtime_error
                , "too many consumers, poolsize = " << consumerCount_);
        }

        auto ptr = static_cast<PoolConsumer*>(&c);
        ptr->reset();
        c.poolThreadAffinity = poolThreadAffinityIn;
        for (uint16_t i = 0; i < consumerQueues_.size(); ++i) {
            if (c.poolThreadAffinity & (1ul << i)) {
              consumerQueues_[i]->put(&c);
            }
        }
    }

    void start(uint16_t threadCount, uint64_t cpuAffinityMask, bool thatIsAll) {
        if (threadCount > LockFreeBuffer::max_parallel_consumer){
            HMBDC_THROW(std::runtime_error, 
                "threadCount too large " << threadCount << " > " 
                    << LockFreeBuffer::max_parallel_consumer);
        }
        if (activePoolThreadSequenceMask_) {
            HMBDC_THROW(std::runtime_error, "cannot restart a pool.");
        }
        activePoolThreadSequenceMask_ = (1ul << threadCount) - 1u;
        for (auto i = 0u; i < LockFreeBuffer::max_parallel_consumer; ++i) {
            if (i < threadCount) {
                hmbdcNumbers_[i] = i;
                threads_.push_back(
                    std::thread([this, i, cpuAffinityMask]() {
                        threadEntry(i, cpuAffinityMask);
                    })
                );
            } else if (thatIsAll) {
                lfb_.markDead(i); //this is because we might not use to the full count of the pool threads
            }
        }
    }

    void startThruRecycling(uint16_t threadCount, uint64_t cpuAffinityMask) {
        auto slots = lfb_.unusedConsumerIndexes();
        startAt(threadCount, cpuAffinityMask, slots);
    }

    void startAt(uint16_t threadCount, uint64_t cpuAffinityMask, std::vector<uint16_t> const& slots) {
        if (activePoolThreadSequenceMask_) {
            HMBDC_THROW(std::runtime_error, "cannot restart a pool.");
        }
        if (threadCount > slots.size()) {
            HMBDC_THROW(std::runtime_error
                , "Buffer instance support Client count = " << slots.size());
        }
        activePoolThreadSequenceMask_ = (1ul << threadCount) - 1u;
        auto itSlot = slots.begin();
        for (auto i = 0u; i < threadCount; ++i) {
            hmbdcNumbers_[i] = *itSlot;
            lfb_.reset(*itSlot++);
        }
        for (auto i = 0u; i < threadCount; ++i) {
            threads_.push_back(
                std::thread([this, i, cpuAffinityMask]() {
                    threadEntry(i, cpuAffinityMask);
                })
            );
        }

    }

    void startAll(uint64_t cpuAffinityMask) {
        if (activePoolThreadSequenceMask_) {
            HMBDC_THROW(std::runtime_error, "cannot restart a pool.");
        }
        activePoolThreadSequenceMask_ = (1ul << LockFreeBuffer::max_parallel_consumer) - 1u;
        for (auto i = 0; i < LockFreeBuffer::max_parallel_consumer; ++i) {
            hmbdcNumbers_[i] = i;
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
        poolMinus_.stop();
    }

    void join() {
        for(Threads::value_type& t : threads_) t.join();
        threads_.clear();
    }

    using ConsumerQ = RingBuffer<PoolConsumer*, 0>;
    
    void runOnce(uint16_t threadSerialNumber) {
        auto hmbdcNumber = hmbdcNumbers_[threadSerialNumber];
        runOnceImpl(threadSerialNumber, hmbdcNumber
            , *consumerQueues_[threadSerialNumber]);
    }

    void threadEntry(uint16_t threadSerialNumber, uint64_t cpuAffinityMask) HMBDC_RESTRICT {
        auto hmbdcNumber = hmbdcNumbers_[threadSerialNumber];
        std::string name = "hmbdcp" + std::to_string(threadSerialNumber);
        if (cpuAffinityMask == 0) {
            auto cpuCount = std::thread::hardware_concurrency();
            cpuAffinityMask = (1ul << cpuCount) - 1u;
        }
        cpuAffinityMask = hmbdc::numeric::nthSetBitFromLsb(cpuAffinityMask
            , threadSerialNumber % hmbdc::numeric::setBitsCount(cpuAffinityMask));
        hmbdc::os::configureCurrentThread(name.c_str(), cpuAffinityMask);

        auto& consumers = *consumerQueues_[threadSerialNumber];
        
        //randomize the initial consumers  for fairness
        std::vector<ConsumerQ::value_type> space;
        ConsumerQ::iterator it, e;
        auto size = consumers.peek(it, e);
        auto b = it;
        for (;it != e; ++it) {
            space.push_back(*it);
        }
        std::shuffle(space.begin(), space.end(), std::mt19937(std::random_device()()));
        for (auto p : space) {
            consumers.put(p);
        }
        consumers.wasteAfterPeek(b, size);

        while(!stopped_){
            runOnceImpl(threadSerialNumber, hmbdcNumber, consumers);
        }
        if (stopped_) {
            ConsumerQ::iterator it, e;
            auto l = consumers.peek(it, e);
            for (; it != e; ++it) {
                PoolConsumer* consumer = *it;
                uint16_t droppedCount = ++consumer->droppedCount;
                if (hmbdc_unlikely(droppedCount == 
                    hmbdc::numeric::setBitsCount(
                        consumer->poolThreadAffinity & activePoolThreadSequenceMask_))) {
                    --consumerCount_;
                    //now consumer can be deleted
                    consumer->dropped();
                }
            }
            consumers.wasteAfterPeek(b, l);
        }
        lfb_.markDead(hmbdcNumber);

        poolMinus_.cleanup(hmbdcNumber);
    } 

    void runOnceImpl(uint16_t threadSerialNumber, uint16_t hmbdcNumber, ConsumerQ& consumers) HMBDC_RESTRICT {
        size_t count;
        BufIt begin, end;
        count = lfb_.peek(hmbdcNumber, begin, end, maxBatchMessageCount_);
        if (hmbdc_unlikely(stopped_)) return;

        ConsumerQ::iterator it, e;
        size_t size = consumers.peekAll(it, e);
        // for (uint32_t k = 0;
        //     true;
        //     boost::detail::yield(k++)) {
        //     size = consumers.peek(it, e);
        //     if (hmbdc_likely(!consumers.remainingSize())) {
        //         break;
        //     };
        //     consumers.wasteAfterPeek(it, 0, true);
        // }
        auto b = it;
        for (; it != e; ++it) {
            PoolConsumer* consumer = *it;
            if (consumer->handleRange(begin, end, threadSerialNumber)) {
                consumers.put(consumer);
            } else {
                uint16_t droppedCount = ++consumer->droppedCount;
                if (hmbdc_unlikely(droppedCount == 
                    hmbdc::numeric::setBitsCount(
                        consumer->poolThreadAffinity & activePoolThreadSequenceMask_))) {
                    --consumerCount_;
                    //now consumer can be deleted
                    if (!consumer->dropped()) { //resume
                        addConsumer(*consumer
                            , consumer->poolThreadAffinity & activePoolThreadSequenceMask_);
                    }
                }
            }
        }
        consumers.wasteAfterPeek(b, size);
        lfb_.wasteAfterPeek(hmbdcNumber, count);
        poolMinus_.runOnce(threadSerialNumber
            , [this, &consumers, hmbdcNumber]{
                return lfb_.remainingSize(hmbdcNumber) == 0 
                    && consumers.remainingSize() == 0;});
            // , [this, &consumers]{return false;});
    }

    LockFreeBuffer& HMBDC_RESTRICT lfb_;
    size_t maxBatchMessageCount_;

    using ConsumerQueues = std::vector<ConsumerQ*>;
    ConsumerQueues consumerQueues_;
    PoolMinusImpl poolMinus_;

    using Threads = std::vector<std::thread>;
    Threads threads_;
    bool stopped_;
    uint64_t activePoolThreadSequenceMask_;
    std::atomic<uint32_t> consumerCount_;
    uint16_t hmbdcNumbers_[LockFreeBuffer::max_parallel_consumer];
};

template <typename Buffer>
PoolT<Buffer>::
PoolT(Buffer& lfb, uint32_t maxConsumerSize)
: impl_(os::DefaultAllocator::instance().allocate<PoolTImpl<Buffer>>(SMP_CACHE_BYTES, lfb, maxConsumerSize)) {
}

template <typename Buffer>
PoolT<Buffer>::
~PoolT() {
    os::DefaultAllocator::instance().unallocate(static_cast<PoolTImpl<Buffer>*>(impl_));
}

template <typename Buffer> void     PoolT<Buffer>::addConsumer(PoolConsumer& c, uint64_t poolThreadAffinityIn)                                  {       static_cast<PoolTImpl<Buffer>*>(impl_)->addConsumer(c, poolThreadAffinityIn);} 
template <typename Buffer> uint32_t PoolT<Buffer>::consumerSize() const                                                                         {return static_cast<PoolTImpl<Buffer>*>(impl_)->consumerSize();}  
template <typename Buffer> void     PoolT<Buffer>::start(uint16_t threadCount, uint64_t cpuAffinityMask, bool thatIsAll)                        {       static_cast<PoolTImpl<Buffer>*>(impl_)->start(threadCount, cpuAffinityMask, thatIsAll);}
template <typename Buffer> void     PoolT<Buffer>::startAll(uint64_t cpuAffinityMask)                                                           {       static_cast<PoolTImpl<Buffer>*>(impl_)->startAll(cpuAffinityMask);}   
template <typename Buffer> void     PoolT<Buffer>::startThruRecycling(uint16_t threadCount, uint64_t cpuAffinityMask)                           {       static_cast<PoolTImpl<Buffer>*>(impl_)->startThruRecycling(threadCount, cpuAffinityMask);}
template <typename Buffer> void     PoolT<Buffer>::startAt(uint16_t threadCount, uint64_t cpuAffinityMask, std::vector<uint16_t> const& slots)    {       static_cast<PoolTImpl<Buffer>*>(impl_)->startAt(threadCount, cpuAffinityMask, slots);}
template <typename Buffer> void     PoolT<Buffer>::runOnce(uint16_t threadSerialNumber)                    HMBDC_RESTRICT                       {       static_cast<PoolTImpl<Buffer>*>(impl_)->runOnce(threadSerialNumber);}
template <typename Buffer> void     PoolT<Buffer>::stop()                                                                                       {       static_cast<PoolTImpl<Buffer>*>(impl_)->stop();}    
template <typename Buffer> void     PoolT<Buffer>::join()                                                                                       {       static_cast<PoolTImpl<Buffer>*>(impl_)->join();}      

}}


