#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/Message.hpp"

#include "hmbdc/pattern/PoolT.hpp"
#include "hmbdc/pattern/PoolMinus.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"

#include "hmbdc/os/Thread.hpp"
#include "hmbdc/os/Allocators.hpp"
#include "hmbdc/time/Timers.hpp"

#include <vector>
#include <type_traits>
#include <thread>
#include <utility>

namespace hmbdc { namespace app { namespace context_detail {

template <typename CcClient>
struct PoolConsumerProxy
: pattern::PoolConsumer {
    template <typename U = CcClient>
    PoolConsumerProxy(U& client
        , size_t* pDispStartCount
        , typename std::enable_if<std::is_base_of<time::TimerManager, U>::value>::type* = nullptr)
    : pattern::PoolConsumer(CcClient::INTERESTS_SIZE != 0, &client, pDispStartCount)
    , client_(client){}

    template <typename U = CcClient>
    PoolConsumerProxy(U& client
        , size_t* pDispStartCount
        , typename std::enable_if<!std::is_base_of<time::TimerManager, U>::value>::type* = nullptr)
    : pattern::PoolConsumer(CcClient::INTERESTS_SIZE != 0, nullptr, pDispStartCount)
    , client_(client){}

    virtual size_t handleRangeImpl(BufIt it, 
        BufIt end, uint16_t threadSerialNumber) override {
        return client_.CcClient::handleRangeImpl(it, end, threadSerialNumber);
    }
    virtual void messageDispatchingStartedCb(size_t const* dispStartCount) override {
        client_.CcClient::messageDispatchingStartedCb(dispStartCount);
    }
    virtual void invokedCb(size_t n) override {
        client_.CcClient::invokedCb(n);
    }
    virtual void stoppedCb(std::exception const&e) override {
        client_.CcClient::stoppedCb(e);
    }
    virtual bool droppedCb() override {
        if (client_.CcClient::droppedCb()) {
            delete this;
            return true;
        } else {
            return false;
        }
    }
private:
    CcClient& HMBDC_RESTRICT client_;
};

template <typename... ContextProperties>
struct context_property_aggregator{
    using Buffer = hmbdc::pattern::LockFreeBufferT<DEFAULT_HMBDC_CAPACITY>;
    using Allocator = os::DefaultAllocator;
    enum {
        broadcast_msg = 1,
        has_pool = 1,
        pool_msgless = 0,
        ipc = 0,
        dispatch_reverse = 0,
    };
};

template <uint16_t c, typename... ContextProperties>
struct context_property_aggregator<context_property::broadcast<c>
    , ContextProperties...>
: context_property_aggregator<ContextProperties...> {
    using Buffer = hmbdc::pattern::LockFreeBufferT<c>;
    static_assert(context_property_aggregator<ContextProperties...>::broadcast_msg, "contradicting properties");
    enum {
        broadcast_msg = 1,
        has_pool = 1,
    };
};

template <typename... ContextProperties>
struct context_property_aggregator<context_property::partition
    , ContextProperties...> 
: context_property_aggregator<ContextProperties...> {
    using Buffer = hmbdc::pattern::MonoLockFreeBuffer;
    enum {
        broadcast_msg = 0,
        has_pool = context_property_aggregator<ContextProperties...>::pool_msgless,
    };
};

template <typename... ContextProperties>
struct context_property_aggregator<context_property::msgless_pool
    , ContextProperties...> 
: context_property_aggregator<ContextProperties...> {
    enum {
        has_pool = 1,
        pool_msgless = 1
    };
};

template <typename... ContextProperties>
struct context_property_aggregator<context_property::ipc_enabled
    , ContextProperties...> 
: context_property_aggregator<ContextProperties...> {
    using Allocator = os::ShmBasePtrAllocator;
    enum {
        ipc = 1,
    };
};

template <typename... ContextProperties>
struct context_property_aggregator<context_property::pci_ipc
    , ContextProperties...> 
: context_property_aggregator<ContextProperties...> {
    using Allocator = os::DevMemBasePtrAllocator;
    enum {
        ipc = 1,
    };
};

template <bool is_timer_manager>
struct tm_runner {
    template<typename C>
    void operator()(C&) {}
};

template <>
struct tm_runner<true> {
    void operator()(hmbdc::time::TimerManager& tm) {
        tm.checkTimers(hmbdc::time::SysTime::now());
    }
};

template <typename LFB, typename CcClient>
bool runOnceImpl(uint16_t hmbdcNumber, bool& HMBDC_RESTRICT stopped
    , LFB& HMBDC_RESTRICT lfb, CcClient& HMBDC_RESTRICT c) {
    typename LFB::iterator begin, end;
    try {
        tm_runner<std::is_base_of<hmbdc::time::TimerManager, CcClient>::value> tr;
        tr(c);

        using Cc = typename std::decay<CcClient>::type;

        const bool clientParticipateInMessaging = Cc::INTERESTS_SIZE != 0;
        if (clientParticipateInMessaging) {
            uint64_t count = lfb.peek(hmbdcNumber, begin, end, c.maxBatchMessageCount());
            c.Cc::invokedCb(c.Cc::handleRangeImpl(begin, end, hmbdcNumber));
            lfb.wasteAfterPeek(hmbdcNumber, count);
        } else {
            c.Cc::invokedCb(0);
        }
    } catch (std::exception const& e) {
        if (!stopped) {
            c.stopped(e);
            return !c.dropped();
        }
    } catch (int code) {
        if (!stopped) {
            c.stopped(hmbdc::ExitCode(code));
            return !c.dropped();
        }
    } catch (...) {
        if (!stopped) {
            c.stopped(hmbdc::UnknownException());
            return !c.dropped();
        }
    }
    return !stopped;
} 

template <typename CcClient>
bool runOnceImpl(uint16_t threadSerialNumber, bool& HMBDC_RESTRICT stopped
    , hmbdc::pattern::MonoLockFreeBuffer& HMBDC_RESTRICT lfb, CcClient& HMBDC_RESTRICT c) {
    hmbdc::pattern::MonoLockFreeBuffer::iterator begin, end;
    try {
        tm_runner<std::is_base_of<hmbdc::time::TimerManager, CcClient>::value> tr;
        tr(c);

        using Cc = typename std::decay<CcClient>::type;
        const bool clientParticipateInMessaging = Cc::INTERESTS_SIZE != 0;
        if (clientParticipateInMessaging) {
            uint64_t count = lfb.peek(begin, end, c.maxBatchMessageCount());
            c.Cc::invokedCb(c.Cc::handleRangeImpl(begin, end, threadSerialNumber));
            lfb.wasteAfterPeek(begin, count);
        } else {
            c.Cc::invokedCb(0);
        }
    } catch (std::exception const& e) {
        if (!stopped) {
            c.stopped(e);
            return !c.dropped();
        }
    } catch (int code) {
        if (!stopped) {
            c.stopped(hmbdc::ExitCode(code));
            return !c.dropped();
        }
    } catch (...) {
        if (!stopped) {
            c.stopped(hmbdc::UnknownException());
            return !c.dropped();
        }
    }
    return !stopped;
}

inline
void unblock(hmbdc::pattern::MonoLockFreeBuffer& buffer, uint16_t){
    buffer.reset();
}

template <typename Buffer>
void unblock(Buffer& lfb, uint16_t threadSerialNumber) {
    lfb.markDead(threadSerialNumber);
}

}}}
