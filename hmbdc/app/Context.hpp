#include "hmbdc/Copyright.hpp"
#pragma once


#include "hmbdc/app/StuckClientPurger.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/numeric/BitMath.hpp"
#include "hmbdc/time/Time.hpp"

#include <boost/interprocess/allocators/allocator.hpp>
#include <memory>
#include <vector>
#include <list>
#include <mutex>
#include <atomic>
#include <optional>

namespace hmbdc { namespace app {


/** 
* @example hello-world.cpp
* @example hmbdc.cpp
* @example hmbdc-log.cpp
* @example ipc-market-data-propagate.cpp
*/

/**
 * @namespace hmbdc::app::context_property
 * contains the trait types that defines how a Context behave and capabilities
 */
namespace context_property {
    /**
     * @class broadcast
     * @brief Context template parameter inidcating each message is 
     * sent to all clients within the Context. 
     * This is the default property of a Context. 
     * @details each message is still subjected to Client's message type 
     * filtering. In the case of ipc Context
     * it is also sent to all clients in the attached ipc Contexts.
     * When this Context is specialized using this type, the context normally 
     * works with heterogeneous Clients and all Clients can talk to each 
     * other thru the Context. Load balance among Clients can be achieved by
     * participating Clients coordinatedly select message to process
     * In addtion to the direct mode Clients, a Client running pool is supported 
     * with the Context - see pool related functions in Context.
     * 
     * There is no hard coded limit on how many Clients can be added into a pool
     * Also, there is no limit on when you can add a Client into a pool.
     * @tparam max_parallel_consumer max thread counts that processes messages
     * that incudes pool threads plus the count of direct mode Clients that 
     * registers messages within the Context
     * supported values: 4(default) 
     * 2,8,16,32,64,128,256 requires hmbdc licensed
     */
    template <uint16_t max_parallel_consumer = DEFAULT_HMBDC_CAPACITY>
    struct broadcast{
        static_assert(max_parallel_consumer >= 4u 
            && hmbdc::numeric::set_bits_count<max_parallel_consumer>::value == 1u, "");
    };

    /**
     * @class partition
     * @brief Context template parameter inidcating each message is 
     * sent to one and only one of the clients within the Context 
     * and its attached ipc Contexts if appllies.
     * @details each message is still subjected to Client's message type 
     * filtering 
     * When this Context is specialized using this type, the context normally 
     * works with homogeneous Clients to achieve load balance thru threads. No
     * coordination is needed between Clients.
     * Only the direct mode Clients are supported, thread pool is NOT supported
     * by this kind of Context - the pool related functions in Context are also disabled
     * 
     */
    struct partition{};

    /**
     * @class msgless_pool
     * @brief Context template parameter indicating the Context must contain a pool to run Clients
     * and the Clients in the pool shall not receive messages - Unlike the default pool.
     * @details msgless_pool performs better when its Clients don't need to receive messages from the Context.
     * This is useful when the Clients are network transport engines. By default, partition Context
     * don't come with a pool due to semantic reason, but this Context property enables a pool that 
     * does not deliver messages.
     */
    struct msgless_pool{};

    /**
     * @class ipc_enabled
     * @brief Context template parameter indicating the Context is ipc enabled and
     * it can create or be attached to an ipc transport thru a transport name.
     * @details In addition to the normal Context functions, the Context acts either as 
     * the creator (owner) of the named ipc transport or an attcher to the transport.
     * Since the creator performs a critical function to purge crushed or 
     * stuck Clients to avoid buffer full for other well-behaving Clients, it is 
     * expected to be running (started) as long as ipc functions.
     * ipc transport uses persistent shared memory and if the dtor of Context is not called 
     * due to crashing, there will be stale shared memory in /dev/shm.
     * It is very important that the Context is constructed exactly 
     * the same size (see constructor) and type as the ipc 
     * transport creator specified. 
     * All Contexts attaching to a single ipc transport collectively are subjected to the 
     * max_parallel_consumer limits just like a sinlge local (non-ipc) Context does.
     * Example in ipc-market-data-propagate.cpp
     */
    struct ipc_enabled{};

    /**
     * @class dev_ipc
     * @brief when processes are distributed on a PCIe board and host PC, use this property
     * instead of ipc_enabled -specify the dev name in localDomainName config param
     * @details beta
     * 
     */
    struct dev_ipc{};
}
}}

#include "hmbdc/app/ContextDetail.hpp"
namespace hmbdc { namespace app {

namespace context_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_ctx_queued_ts);
HMBDC_CLASS_HAS_DECLARE(hmbdcIpcFrom);
HMBDC_CLASS_HAS_DECLARE(ibmaProc);

/**
 * @class ThreadCommBase<>
 * @brief covers the inter-thread and ipc communication fascade
 * @details this type's interface is exposed thru Context and the type itself is
 * not directly used by users
 * @tparam MaxMessageSize What is  the max message size, need at compile time
 * if the value can only be determined at runtime, set this to 0. Things can still work
 * but will lost some compile time checking advantages, see maxMessageSizeRuntime below
 * @tparam ContextProperties see types in context_property namespace
 */
template <size_t MaxMessageSize, typename... ContextProperties>
struct ThreadCommBase
 : private context_detail::context_property_aggregator<ContextProperties...> {
    using cpa = context_property_aggregator<ContextProperties...>;
    using Buffer = typename cpa::Buffer;
    using Allocator = typename cpa::Allocator;

	enum {
        MAX_MESSAGE_SIZE = MaxMessageSize,
		BUFFER_VALUE_SIZE = MaxMessageSize + sizeof(MessageHead), //8bytes for wrap
	};

    /**
     * @brief max message in bytes (excluding attachment) can handle
     * 
     * @return size_t 
     */
    size_t maxMessageSize() const {
        if (MaxMessageSize == 0) return maxMessageSizeRuntime_;
        return MaxMessageSize;
    }

    /**
     * @brief memory footprint
     * @return size_t - always in multiple of 4K
     */
    size_t footprint() const {
        return footprint_;
    }

    /**
     * @brief try send a batch of messages to the Context or attached ipc Contexts
     * @details only the Clients that handles the Message will get it of course
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param msgs messages
     * @tparam Messages message types
     */
    template <MessageC M0, MessageC M1, typename ... Messages, typename Enabled 
        = typename std::enable_if<!std::is_integral<M1>::value, void>::type>
    void
    send(M0&& m0, M1&& m1, Messages&&... msgs) {
        auto n = sizeof...(msgs) + 2;
        auto it = buffer_.claim(n);
        sendRecursive(it, std::forward<M0>(m0), std::forward<M1>(m1), std::forward<Messages>(msgs)...);
        buffer_.commit(it, n);
    }

    /**
     * @brief try to send a batch of message to the Context or attached ipc Contexts
     * @details this call does not block and it is transactional - send all or none
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @tparam M0 Message types
     * @tparam M1 Message types
     * @tparam Messages more Message types 
     * @tparam std::enable_if<!std::is_integral<M1>::value, void>::type ignore
     * @param m0 first message
     * @param m1 second message
     * @param msgs more messages
     * @return true when sent out properly - no need to retry
     * @return false - all not sent out properly - could send out some of them properly though
     */
    template <MessageC M0, MessageC M1, typename ... Messages, typename Enabled 
        = typename std::enable_if<!std::is_integral<M1>::value, void>::type>
    bool
    trySend(M0&& m0, M1&& m1, Messages&&... msgs) {
        auto n = sizeof...(msgs) + 2;
        auto it = buffer_.tryClaim(n);
        if (it) {
            sendRecursive(it, std::forward<M0>(m0), std::forward<M1>(m1), std::forward<Messages>(msgs)...);
            buffer_.commit(it, n);
            return true;
        }

        return false;
    }

    /**
     * @brief send a range of messages to the Context or attached ipc Contexts
     * @details only the Clients that handles the Message will get it of course
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param begin a forward iterator point at the start of the range 
     * @param n length of the range
     */
    template <MessageForwardIterC ForwardIt>
    void
    send(ForwardIt begin, size_t n) {
        if (hmbdc_likely(n)) {
            auto bit = buffer_.claim(n);
            auto it = bit;
            for (auto i = 0ul; i < n; i++) {
                using Message = typename std::iterator_traits<ForwardIt>::value_type;
                static_assert(std::is_trivially_destructible<Message>::value
                    , "cannot send message with dtor");
                static_assert(!std::is_base_of<hasMemoryAttachment, Message>::value
                    , "hasMemoryAttachment Messages cannot be sent in group");
                auto wrap = new (*it++) MessageWrap<Message>(*begin++);
                if constexpr (has_hmbdc_ctx_queued_ts<Message>::value) {
                    wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                } else (void)wrap;
            }
            buffer_.commit(bit, n);
        }
    }

    /**
     * @brief try send a range of messages to the Context or attached ipc Contexts
     * @details this call does not block and it is transactional - send all or none
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param begin a forward iterator point at the start of the range 
     * @param n length of the range
     */
    template <MessageForwardIterC ForwardIt>
    bool
    trySend(ForwardIt begin, size_t n) {
        if (hmbdc_likely(n)) {
            auto bit = buffer_.tryClaim(n);
            if (hmbdc_unlikely(!bit)) return false;
            auto it = bit;
            for (auto i = 0ul; i < n; i++) {
                using Message = typename std::iterator_traits<ForwardIt>::value_type;
                static_assert(std::is_trivially_destructible<Message>::value
                    , "cannot send message with dtor");
                static_assert(!std::is_base_of<hasMemoryAttachment, Message>::value
                    , "hasMemoryAttachment Messages cannot be sent in group");
                auto wrap = new (*it++) MessageWrap<Message>(*begin++);
                if constexpr (has_hmbdc_ctx_queued_ts<Message>::value) {
                    wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                } else (void)wrap;
            }
            buffer_.commit(bit, n);
        }
        return true;
    }

    /**
     * @brief send a message including hasMortAttachment message to the Context or attached ipc Contexts
     * @details only the Clients that handles the Message will get it of course
     * This function is threadsafe, which means you can call it anywhere in the code
     * If sending hasMortAttachment message, the size of the attachment is runtime checked and 
     * restricted by Context capacity
     * 
     * @param m message
     * @tparam Message type
     */
    template <MessageC Message>
    void
    send(Message&& m) {
        using M = typename std::decay<Message>::type;
        static_assert(std::is_trivially_destructible<M>::value, "cannot send message with dtor");
        static_assert(MAX_MESSAGE_SIZE == 0 || sizeof(MessageWrap<M>) <= BUFFER_VALUE_SIZE
            , "message too big");
        if constexpr(!std::is_base_of<hasMemoryAttachment, M>::value
            || !cpa::ipc) {
            if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 && sizeof(MessageWrap<M>) > buffer_.maxItemSize())) {
                HMBDC_THROW(std::out_of_range, "message too big, typeTag=" << m.getTypeTag());
            }

            if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                auto it = buffer_.claim();
                auto wrap = new (*it++) MessageWrap<M>(std::forward<Message>(m));
                wrap->template get<M>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                buffer_.commit(it);
            } else {
        	    buffer_.put(MessageWrap<M>(std::forward<Message>(m)));
            }
        } else {
            if constexpr (cpa::ipc && !cpa::dev_mem && M::att_via_shm_pool) {
                if (m.template holdShmHandle<M>()) {
                    auto it = buffer_.claim(1);
                    auto wrap = (new (*it) MessageWrap<InBandHasMemoryAttachment<M>>(m)); (void)wrap;
                    wrap->payload.shmConvert(*shmAttAllocator_);
                    if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                        wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                    }
                    buffer_.commit(it, 1);
                    m.hasMemoryAttachment::release();
                    return;
                } // else handle as regular att
            } // else handle as regular att
            if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 
                && sizeof(MessageWrap<InBandHasMemoryAttachment<M>>) > buffer_.maxItemSize())) {
                HMBDC_THROW(std::out_of_range, "message too big, typeTag=" << m.getTypeTag());
            }
            auto att = reinterpret_cast<hasMemoryAttachment&>(m);
            size_t segSize = buffer_.maxItemSize() - sizeof(MessageHead);
            auto n = (att.len + segSize - 1) / segSize + 1;
            if (hmbdc_unlikely(n > buffer_.capacity())) {
                HMBDC_THROW(std::out_of_range
                    , "hasMemoryAttachment message too big, typeTag=" << m.getTypeTag());
            }
            
            auto bit = buffer_.claim(n);
            auto it = bit;
            // InBandHasMemoryAttachment<M> ibm{m};
            auto wrap = (new (*it++) MessageWrap<InBandHasMemoryAttachment<M>>(m)); (void)wrap;
            // wrap->scratchpad().ipc.hd.inbandUnderlyingTypeTag = m.getTypeTag();
            
            if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
            }
            auto segStart = (char*)att.attachment;
            auto remaining = (size_t)att.len;
            while(--n) {
                auto wrap = (new (*it++) MessageWrap<InBandMemorySeg>());
                auto& ms = wrap->get<InBandMemorySeg>();
                auto bytes = std::min(segSize, (size_t)remaining);
                wrap->scratchpad().ipc.inbandPayloadLen = bytes;
                memcpy(ms.seg, segStart, bytes);
                segStart += bytes;
                remaining -= bytes;
            }
            buffer_.commit(bit, it - bit);
            att.release();
        }
    }

    /**
     * @brief preallocate consecutive buffers so they can be send out later
     * 
     * @param n how many buffers to allocate - each buffer has the same sizeof max
     * @return Buffer::iterator 
     */
    template <MessageC Message>
    auto allocateForSend(size_t n) {
        struct IteratorAdaptor {
            typename Buffer::iterator it;
            Message& get() {
                auto wrap = (MessageWrap<Message>*)(*it);
                return wrap->payload;
            }
            auto& operator ++() {
                ++it;
                return *this;
            }
            auto operator ++(int) {
                auto tmp = IteratorAdaptor{it++};
                return tmp;
            }
        };

        auto it = buffer_.claim(n);
        auto res = IteratorAdaptor{it};
        while (n--) {
            new (*it++) MessageWrap<Message>;
        }
        return res;
    }

    /**
     * @brief commit all the filled up buffers allocated by allocateForSend
     * and send them out
     * 
     * @tparam IteratorAdaptor the return value type of allocateForSend
     * @param itA the returned calue of allocateForSend
     */
    template <typename IteratorAdaptor>
    void commitForSend(IteratorAdaptor itA) {
        buffer_.commit(itA.it);
    }
    
    /**
     * @brief try to send a message including hasMortAttachment message to the Context or attached ipc Contexts 
     * if it wouldn't block
     * @details this call does not block - return false when buffer is full
     * This function is threadsafe, which means you can call it anywhere in the code
     * If sending hasMortAttachment message, the size of the attachment is runtime checked and 
     * restricted by Context capacity
     * 
     * @param m message
     * @tparam Message type
     * @return true if send successfully
     */
    template <MessageC Message>
    bool trySend(Message&& m) {
        using M = typename std::decay<Message>::type;
        static_assert(std::is_trivially_destructible<M>::value, "cannot send message with dtor");
        static_assert(MAX_MESSAGE_SIZE == 0 || sizeof(MessageWrap<M>) <= BUFFER_VALUE_SIZE
            , "message too big");
        
        if constexpr(!std::is_base_of<hasMemoryAttachment, M>::value 
            || !cpa::ipc) {
            if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 && sizeof(MessageWrap<M>) > buffer_.maxItemSize())) {
                HMBDC_THROW(std::out_of_range, "message too big, typeTag=" << m.getTypeTag());
            }

            if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                auto it = buffer_.tryClaim();
                if (!it) return false;
                auto wrap = new (*it++) MessageWrap<M>(std::forward<Message>(m));
                wrap->template get<M>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                buffer_.commit(it);
                return true;
            } else {
        	    return buffer_.tryPut(MessageWrap<M>(std::forward<Message>(m)));
            }
        } else {
            if constexpr (cpa::ipc && !cpa::dev_mem && M::att_via_shm_pool) {
                if (m.template holdShmHandle<M>()) {
                    auto it = buffer_.tryClaim(1);
                    if (!it) {
                        m.hasMemoryAttachment::release();
                        return false;
                    }
                    auto wrap = (new (*it) MessageWrap<InBandHasMemoryAttachment<M>>(m)); (void)wrap;
                    wrap->payload.shmConvert(*shmAttAllocator_);
                    if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                        wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                    }
                    buffer_.commit(it, 1);
                    m.hasMemoryAttachment::release();
                    return true;
                } // else handle as regular att
            } // else handle as regular att
            if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 
                && sizeof(MessageWrap<InBandHasMemoryAttachment<M>>) > buffer_.maxItemSize())) {
                HMBDC_THROW(std::out_of_range, "message too big, typeTag=" << m.getTypeTag());
            }
            auto att = reinterpret_cast<hasMemoryAttachment&>(m);
            size_t segSize = buffer_.maxItemSize() - sizeof(MessageHead);
            auto n = (att.len + segSize - 1) / segSize + 1;
            if (hmbdc_unlikely(n > buffer_.capacity())) {
                HMBDC_THROW(std::out_of_range
                    , "hasMemoryAttachment message too big, typeTag=" << m.getTypeTag());
            }
            
            auto bit = buffer_.tryClaim(n);
            if (!bit) {
                att.release();
                return false;
            }
            auto it = bit;
            // InBandHasMemoryAttachment<M> ibm{m};
            auto wrap = (new (*it++) MessageWrap<InBandHasMemoryAttachment<M>>(m)); (void)wrap;
            // wrap->scratchpad().ipc.hd.inbandUnderlyingTypeTag = m.getTypeTag();
            
            if constexpr (has_hmbdc_ctx_queued_ts<M>::value) {
                wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
            }
            auto segStart = (char*)att.attachment;
            auto remaining = (size_t)att.len;
            while(--n) {
                auto wrap = (new (*it++) MessageWrap<InBandMemorySeg>());
                auto& ms = wrap->get<InBandMemorySeg>();
                auto bytes = std::min(segSize, (size_t)remaining);
                wrap->scratchpad().ipc.inbandPayloadLen = bytes;
                memcpy(ms.seg, segStart, bytes);
                segStart += bytes;
                remaining -= bytes;
            }
            buffer_.commit(bit, it - bit);
            att.release();
            return true;
        }
    }

    /**
     * @brief send a message to all Clients in the Context or attached ipc Contexts
     * @details construct the Message in buffer directly
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param args ctor args
     * @tparam Message type
     * @tparam typename ... Args args
     */
    template <MessageC Message, typename ... Args>
    void sendInPlace(Args&&... args) {
        static_assert(!std::is_base_of<JustBytes, Message>::value
            , "use sendJustBytesInPlace");
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
    	static_assert(MAX_MESSAGE_SIZE == 0 || sizeof(MessageWrap<Message>) <= BUFFER_VALUE_SIZE
            , "message too big");
        static_assert(!std::is_base_of<hasMemoryAttachment, Message>::value
            , "hasMemoryAttachment Messages cannot be sent in place");
        if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 && sizeof(MessageWrap<Message>) > buffer_.maxItemSize())) {
            HMBDC_THROW(std::out_of_range
                , "message too big buffer_.maxItemSize()=" << buffer_.maxItemSize());
        }
    	buffer_.template putInPlace<MessageWrap<Message>>(std::forward<Args>(args)...);
    }

    template <typename JustBytesType, typename ... Args>
    void sendJustBytesInPlace(uint16_t tag, void const* bytes, size_t len
        , app::hasMemoryAttachment* att, Args&& ...args) {
        if (hmbdc_unlikely(len > maxMessageSize())) {
            HMBDC_THROW(std::out_of_range, "message too big, typeTag=" << tag
                << " len=" << len);
        }
        if (!att || !cpa::ipc) {
            auto it = buffer_.claim();
            new (*it) MessageWrap<JustBytesType>(tag, bytes, len, att
                , std::forward<Args>(args)...);
            buffer_.commit(it);
        } else {
            size_t segSize = buffer_.maxItemSize() - sizeof(MessageHead);
            auto n = (att->len + segSize - 1) / segSize + 1;
            if (hmbdc_unlikely(n > buffer_.capacity())) {
                HMBDC_THROW(std::out_of_range
                    , "hasMemoryAttachment message too big, typeTag=" << tag);
            }
            
            auto bit = buffer_.claim(n);
            auto it = bit;
            auto wrap = new (*it++) MessageWrap<InBandHasMemoryAttachment<JustBytesType>>(
                tag, bytes, len, att, std::forward<Args>(args)...); (void)wrap;
            // wrap->scratchpad().ipc.hd.inbandUnderlyingTypeTag = tag;
            
            auto segStart = (char*)att->attachment;
            auto remaining = (size_t)att->len;
            while(--n) {
                auto wrap = (new (*it++) MessageWrap<InBandMemorySeg>());
                auto& ms = wrap->get<InBandMemorySeg>();
                auto bytes = std::min(segSize, (size_t)remaining);
                wrap->scratchpad().ipc.inbandPayloadLen = bytes;
                memcpy(ms.seg, segStart, bytes);
                segStart += bytes;
                remaining -= bytes;
            }
            buffer_.commit(bit, it - bit);
            att->release();
        }
    }


    /**
     * @brief try send a message to all Clients in the Context or attached ipc Contexts if it wouldn't block
     * @details this call does not block - return false when buffer is full
     * constructed the Message in buffer directly if returns true
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param args ctor args
     * @tparam Message type
     * @tparam typename ... Args args
     * @return true if send successfully
     */
    template <MessageC Message, typename ... Args>
    bool trySendInPlace(Args&&... args) {
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        static_assert(MAX_MESSAGE_SIZE == 0 || sizeof(MessageWrap<Message>) <= BUFFER_VALUE_SIZE
            , "message too big");
        static_assert(!std::is_base_of<hasMemoryAttachment, Message>::value, "hasMemoryAttachment Messages cannot be sent in place");
        if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 && sizeof(MessageWrap<Message>) > buffer_.maxItemSize())) {
            HMBDC_THROW(std::out_of_range, "message too big");
        }
        return buffer_.template tryPutInPlace<MessageWrap<Message>>(std::forward<Args>(args)...);
    }

    /**
     * @brief accessor - mostly used internally
     * @return underlying buffer used in the Context
     */
    Buffer& buffer() {
        return buffer_;
    }

    /**
     * @brief return how many IPC parties (processes) is managed by this
     * Context - including itself
     * 
     * @return size_t (>= 1)
     */
    size_t dispatchingStartedCount() const {
        std::atomic_thread_fence(std::memory_order_acquire);
        return *pDispStartCount_;
    }

    std::shared_ptr<uint8_t[]> allocateInShm(size_t actualSize) {
        static_assert(!cpa::dev_mem, "no shm");
        auto ptr = shmAttAllocator_->allocate(actualSize);
        if (ptr) {
            // HMBDC_LOG_N(shmAttAllocator_->getHandle(ptr));
            return std::shared_ptr<uint8_t[]>(
                ptr
                , [this](uint8_t* t) {
                    auto& hmbdc0cpyShmRefCount = shmAttAllocator_->getHmbdc0cpyShmRefCount(t);
                    if (1 == hmbdc0cpyShmRefCount.fetch_sub(1, std::memory_order_release)) {
                        shmAttAllocator_->deallocate(t);
                    }
            });
        }
        return std::shared_ptr<uint8_t[]>{};
    }

protected:
    template <typename IntLvOrRv>
    ThreadCommBase(uint32_t messageQueueSizePower2Num
        , size_t maxMessageSizeRuntime
        , char const* shmName
        , size_t offset
        , IntLvOrRv&& ownership
        , size_t ipcShmForAttPoolSize)
    : footprint_((Buffer::footprint(maxMessageSizeRuntime + sizeof(MessageHead)
            , messageQueueSizePower2Num) + sizeof(*pDispStartCount_) + 4096 -1)
            / 4096 * 4096)
    , allocator_(shmName, offset, footprint_, ownership)
    , pDispStartCount_(allocator_.template allocate<size_t>(SMP_CACHE_BYTES, 0)) 
    , bufferptr_(allocator_.template allocate<Buffer>(SMP_CACHE_BYTES
        , maxMessageSizeRuntime + sizeof(MessageHead), messageQueueSizePower2Num
        , allocator_)
    )
    , buffer_(*bufferptr_) {
        if (messageQueueSizePower2Num < 2) {
            HMBDC_THROW(std::out_of_range
                , "messageQueueSizePower2Num need >= 2");
        }
        if (MaxMessageSize && maxMessageSizeRuntime != MAX_MESSAGE_SIZE) {
            HMBDC_THROW(std::out_of_range
                , "can only set maxMessageSizeRuntime when template value MaxMessageSize is 0");
        }
        maxMessageSizeRuntime_ = maxMessageSizeRuntime;
        // primeBuffer<(cpa::create_ipc || (!cpa::create_ipc && !cpa::attach_ipc)) && cpa::has_pool>();
        if (((cpa::ipc && ownership > 0) || !cpa::ipc) && cpa::has_pool) {
            markDeadFrom(buffer_, 0);
        }

        if (cpa::ipc && ipcShmForAttPoolSize && !cpa::dev_mem) {
            size_t retry = 3;
            while (true) {
                try {
                    auto name = std::string(shmName) + "-att-pool";
                    if (ownership > 0) {
                        shm_unlink(name.c_str());
                        shmAttAllocator_.emplace(
                            ownership > 0, boost::interprocess::create_only
                            , name.c_str(), ipcShmForAttPoolSize);
                    } else {
                        shmAttAllocator_.emplace(ownership > 0
                            , boost::interprocess::open_only, name.c_str());
                    }
                    return;
                } catch (boost::interprocess::interprocess_exception const&) {
                    if (--retry == 0) throw;
                    sleep(1);
                }
            }
        }
    }

    ~ThreadCommBase() {
        allocator_.unallocate(bufferptr_);
    }

    static
    void markDeadFrom(pattern::MonoLockFreeBuffer& buffer, uint16_t) {
        // does not apply
    }
    
    template <typename BroadCastBuf>
    static
    void markDeadFrom(BroadCastBuf& buffer, uint16_t poolThreadCount) {
        for (uint16_t i = poolThreadCount; 
             i < BroadCastBuf::max_parallel_consumer; 
            ++i) {
            buffer.markDead(i);
        }
    }


    static
    void markDead(pattern::MonoLockFreeBuffer& buffer, std::list<uint16_t>slots) {
        // does not apply
    }
    
    template <typename BroadCastBuf>
    static
    void markDead(BroadCastBuf& buffer, std::list<uint16_t>slots) {
        for (auto s : slots) {
            buffer.markDead(s);
        }
    }
    size_t footprint_;
    Allocator allocator_;
    size_t* pDispStartCount_;
    Buffer* HMBDC_RESTRICT bufferptr_;
    Buffer& HMBDC_RESTRICT buffer_;
    
    struct ShmAttAllocator {
        enum {
            hmbdc0cpyShmRefCountSize = sizeof(size_t),
        };

        template <typename Arg, typename ...Args>
        ShmAttAllocator(bool own, Arg&& arg, char const* name, Args&& ... args)
        : managedShm_(std::forward<Arg>(arg), name, std::forward<Args>(args)...) {
            if (own) {
                nameUnlink_ = name;
            }
        }

        ~ShmAttAllocator() {
            if (nameUnlink_.size()) {
                shm_unlink(nameUnlink_.c_str());
            }
        }

        boost::interprocess::managed_shared_memory::handle_t
        getHandle(void* localAddr) const {
            return managedShm_.get_handle_from_address((uint8_t*)localAddr - hmbdc0cpyShmRefCountSize);
        }

        uint8_t*
        getAddr(boost::interprocess::managed_shared_memory::handle_t h) const {
            return (uint8_t*)managedShm_.get_address_from_handle(h) + hmbdc0cpyShmRefCountSize;
        }
       
        static std::atomic<size_t>& getHmbdc0cpyShmRefCount(void* attached) {
            auto addr = (size_t*)attached;
            addr -= 1;
            return *(std::atomic<size_t>*)addr;
        }
        
        uint8_t* allocate(size_t len) {
            auto res = (uint8_t*)nullptr;
            len += hmbdc0cpyShmRefCountSize;
            while (!(res = (uint8_t*)managedShm_.allocate(len, std::nothrow))) {
                std::this_thread::yield();
            };
            *(size_t*)res = 1;  /// prime the ref count
            // HMBDC_LOG_N(getHandle(res));
            return res + hmbdc0cpyShmRefCountSize;
        }

        auto deallocate(uint8_t* p) {
            // HMBDC_LOG_N(getHandle(p));
            return managedShm_.deallocate(p - hmbdc0cpyShmRefCountSize);
        }

        private:
        boost::interprocess::managed_shared_memory managedShm_;
        std::string nameUnlink_;
    };
    std::optional<ShmAttAllocator> shmAttAllocator_;

private:

    template <typename M, typename... Messages>
    void sendRecursive(typename Buffer::iterator it
        , M&& msg, Messages&&... msgs) {
        using Message = typename std::decay<M>::type;
        static_assert(std::is_trivially_destructible<Message>::value
            , "cannot send message with dtor");
        static_assert(MAX_MESSAGE_SIZE == 0 || sizeof(MessageWrap<Message>) <= BUFFER_VALUE_SIZE
            , "message too big");
        static_assert(!std::is_base_of<hasMemoryAttachment, Message>::value
            , "hasMemoryAttachment Messages cannot be sent in group");
        if (hmbdc_unlikely(MAX_MESSAGE_SIZE == 0 
            && sizeof(MessageWrap<Message>) > buffer_.maxItemSize())) {
            HMBDC_THROW(std::out_of_range, "message too big");
        }
        auto wrap = new (*it) MessageWrap<Message>(msg);
        if constexpr (has_hmbdc_ctx_queued_ts<Message>::value) {
            wrap->template get<Message>().hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
        } else (void)wrap;
        sendRecursive(++it, std::forward<Messages>(msgs)...);
    }
    void sendRecursive(typename Buffer::iterator) {}

    size_t maxMessageSizeRuntime_;
};

} //context_detail

/**
 * @example hmbdc.cpp
 * @example server-cluster.cpp
 * a partition Context rightlyfully doesn't contain a thread pool and all its Clients 
 * are in direct mode. Pool related interfaces are turned off in compile time
 */

/**
 * @class Context<>
 * @brief A Context is like a media object that facilitates the communications 
 * for the Clients that it is holding.
 * a Client can only be added to (or started within) once to a single Context, 
 * undefined behavior otherwise.
 * the communication model is determined by the context_property
 * by default it is in the nature of broadcast fashion within local process indicating
 * by broadcast<>
 * 
 * @details a broadcast Context contains a thread Pool powered by a number of OS threads. 
 * a Client running in such a Context can either run in the pool mode or a direct mode 
 * (which means the Client has its own dedicated OS thread)
 * direct mode provides faster responses, and pool mode provides more flexibility.
 * It is recommended that the total number of threads (pool threads + direct threads) 
 * not exceeding the number of available CPUs.
 * @tparam MaxMessageSize What is  the max message size if known 
 * at compile time(compile time sized);
 * if the value can only be determined at runtime (run time sized), set this to 0. 
 * Things can still work but will lost some compile time checking advantages, 
 * see maxMessageSizeRuntime below
 * @tparam ContextProperties see context_property namespace
 */
template <size_t MaxMessageSize = 0, typename... ContextProperties>
struct Context
: context_detail::ThreadCommBase<MaxMessageSize, ContextProperties...> {
    using Base = context_detail::ThreadCommBase<MaxMessageSize, ContextProperties...>;
    using Buffer = typename Base::Buffer;
    using cpa = typename Base::cpa;
    using Pool = typename std::conditional<cpa::pool_msgless
        , pattern::PoolMinus
        , pattern::PoolT<Buffer>>::type;
    /**
     * @brief ctor for construct local non-ipc Context
     * @details won't compile if calling it for ipc Context
     * @param messageQueueSizePower2Num value of 10 gives message queue if size of 1024 
     * (messages, not bytes)
     * @param maxPoolClientCount up to how many Clients the pool is suppose to support, 
     * only used when
     * pool supported in the Context with broadcast property
     * @param maxMessageSizeRuntime if MaxMessageSize == 0, this value is used
     * the context can manage
     */
	Context(uint32_t messageQueueSizePower2Num = MaxMessageSize?20:2
        , size_t maxPoolClientCount = MaxMessageSize?128:0
        , size_t maxMessageSizeRuntime = MaxMessageSize)
    : Base(messageQueueSizePower2Num < 2?2:messageQueueSizePower2Num
        , MaxMessageSize?MaxMessageSize:maxMessageSizeRuntime
        , nullptr, 0, false, 0)
    , usedHmbdcCapacity_(0)
    , stopped_(false)
    , pool_(createPool<cpa>(maxPoolClientCount))
    , poolThreadCount_(0) {
        static_assert(!cpa::ipc, "no name specified for ipc Context");
    }
    
    /**
     * @brief ctor for construct local ipc Context
     * @details won't compile if calling it for local non-ipc Context
     * 
     * @param ownership input/output flag - 
     * < 0 attach only and not own it; 
     * 0 attach or create (input); 
     * > 0 recreate and own
     * this IPC Context - mean ctor called on it and when exiting, remove the IPC shm file.
     * @param ipcTransportName the id to identify an ipc transport that supports
     * a group of attached together Contexts and their Clients
     * @param messageQueueSizePower2Num value of 10 gives message queue if size of 
     * 1024 (messages, not bytes)
     * @param maxPoolClientCount up to how many Clients the pool is suppose to support, 
     * only used when pool supported in the Context with broadcast property
     * @param maxMessageSizeRuntime if MaxMessageSize == 0, this value is used
     * @param purgerCpuAffinityMask which CPUs to run the low profile (sleep mostly)
     * thread in charge of purging crashed Clients. Used only for ipc_creator Contexts. 
     * @param ipcTransportDeviceOffset the offset in the ipcTransport dev for the use region
     * the context can manage
     */
    template <typename IntRvOrLv
        , std::enable_if_t<std::is_same<int, typename std::decay<IntRvOrLv>::type>::value>* 
            = nullptr
    >
    Context(IntRvOrLv&& ownership
        , char const* ipcTransportName
        , uint32_t messageQueueSizePower2Num = MaxMessageSize?20:0
        , size_t maxPoolClientCount = MaxMessageSize?128:0
        , size_t maxMessageSizeRuntime = MaxMessageSize
        , uint64_t purgerCpuAffinityMask = 0xfffffffffffffffful
        , size_t ipcTransportDeviceOffset = 0
        , size_t ipcShmForAttPoolSize = 0)
    : Base(messageQueueSizePower2Num
        , MaxMessageSize?MaxMessageSize:maxMessageSizeRuntime
        , ipcTransportName
        , ipcTransportDeviceOffset, ownership, ipcShmForAttPoolSize)
    , usedHmbdcCapacity_(0)
    , stopped_(false)
    , pool_(createPool<cpa>(maxPoolClientCount))
    , poolThreadCount_(0)
    , secondsBetweenPurge_(60)
    , ifIpcCreator_(ownership > 0)
    , purgerCpuAffinityMask_(purgerCpuAffinityMask) {
        static_assert(cpa::ipc, "ctor can only be used with ipc turned on Context");
    }
    
    /**
     * @brief dtor
     * @details if this Context owns ipc transport, notify all attached processes
     * that read from it that this tranport is dead
     */
    ~Context() {
        if (ifIpcCreator_) {
            Base::markDeadFrom(this->buffer_, 0);
        }
        stop();
        join();
    }

    /**
     * @brief add a client to Context's pool - the Client is run in pool mode
     * @details if pool is already started, the client is to get current Messages immediatly 
     *  - might miss older messages.
     * if the pool not started yet, the Client does not get messages or other callbacks until
     * the Pool starts.
     * This function is threadsafe, which means you can call it anywhere in the code
     * @tparam Client client type
     * @param client to be added into the Pool
     * @param poolThreadAffinityIn pool is powered by a number of threads 
     * (thread in the pool is identified (by a number) in the mask starting from bit 0)
     * it is possible to have a Client to use just some of the threads in the Pool 
     * - default to use all.
     * 
     */
    template <typename Client>
    void addToPool(Client &client
        , uint64_t poolThreadAffinityIn = 0xfffffffffffffffful) {
        static_assert(cpa::has_pool, "pool is not support in the Context type");
        if (std::is_base_of<single_thread_powered_client, Client>::value 
            && hmbdc::numeric::setBitsCount(poolThreadAffinityIn) != 1
            && poolThreadCount_ != 1) {
            HMBDC_THROW(std::out_of_range
                , "cannot add a single thread powered client to the non-single" 
                "thread powered pool without specifying a single thread poolThreadAffinity"
            );
        }
        primeForShmAtt(client);
        auto stub = new context_detail::PoolConsumerProxy<Client>(client, this->pDispStartCount_);
        pool_->addConsumer(*stub, poolThreadAffinityIn);

    }

    /**
     * @brief add a bunch of clients to Context's pool - the Clients are run in pool mode
     * @details if pool is already started, the client is to get current Messages immediatly 
     *  - might miss older messages.
     * if the pool not started yet, the Client does not get messages or other callbacks until
     * the Pool starts.
     * This function is threadsafe, which means you can call it anywhere in the code
     * @tparam Client client type
     * @param client to be added into the Pool
     * @param poolThreadAffinityIn pool is powered by a number of threads 
     * (thread in the pool is identified (by a number) in the mask starting from bit 0)
     * it is possible to have a Client to use just some of the threads in the Pool 
     * - default to use all.
     * @param args more client and poolThreadAffinityIn pairs can follow
     */
    template <typename Client, typename ... Args>
    void addToPool(Client &client
        , uint64_t poolThreadAffinityIn, Args&& ...args) {
        addToPool(client, poolThreadAffinityIn);
        addToPool(std::forward<Args>(args)...);
    }

    /**
     * @brief add a bunch of clients to Context's pool - the Clients are run in pool mode
     * @details the implementatiotn tells all
     * if the pool not started yet, the Client does not get messages or other callbacks until
     * the Pool starts.
     * This function is threadsafe, which means you can call it anywhere in the code
     * @tparam Client client type
     * @tparam Client2 client2 type
     * @param client to be added into the Pool using default poolThreadAffinity
     * @param client2 to be added into the Pool
     * @param args more client (and/or poolThreadAffinityIn pairs can follow
     */
    template <typename Client, typename Client2, typename ... Args, typename Enabled
        = typename std::enable_if<!std::is_integral<Client2>::value, void>::type>
    void
    addToPool(Client &client, Client2 &client2, Args&& ...args) {
        addToPool(client);
        addToPool(client2, std::forward<Args>(args)...);
    }

    /**
     * @brief return the numebr of clients added into pool
     * @details the number could change since the clients could be added in another thread
     * @return client count
     */
    size_t clientCountInPool() const  {
        static_assert(cpa::has_pool, "pool is not support in the Context type");
        return pool_->consumerSize();
    }
    
    /**
     * @brief how many parallel consummers are started
     * @details the dynamic value could change after the call returns
     * see max_parallel_consumer Context property
     * @return how many parallel consummers are started
     */
    size_t parallelConsumerAlive() const {
        return this->buffer_.parallelConsumerAlive();
    }
    /**
     * @brief start the context by specifying what are in it (Pool and/or direct Clients)
     * and their paired up cpu affinities.
     * @details All direct mode or clients in a pool started by a single start 
     * statement are dispatched with starting from the same event 
     * (subjected to event filtering of each client).
     * many compile time and runtime check is done, for example:
     * won't compile if start a pool in a Context does not support one;
     * exception throw if the Context capacity is reached or try to start a second pool, etc.
     * 
     * Usage example: 
     * 
     * @code
     * // the following starts the pool powered by 3 threads that are affinitied to 
     * // the lower 8 CPUs; client0 affinitied to 4th CPU and  client1 affinitied to 5th CPU
     * ctx.start(3, 0xfful, client0, 0x8ul, client1, 0x10ul);
     * 
     * // the following starts the pool powered by 3 threads that are affinitied to 
     * // all exisiting CPUs; client0 affinitied to a rotating CPU and 
     * // client1 affinitied to 5th CPU
     * ctx.start(3, 0, client0, 0, client1, 0x10ul);
     * 
     * // the following starts 2 direct mode Clients (client2 and client3)
     * ctx.start(client2, 0x3ul, client3, 0xful);
     * @endcode
     * 
     * @tparam typename ...Args types
     * 
     * @param args paired up args in the form of (pool-thread-count|client, cpuAffinity)*.
     * see examples above.
     * If a cpuAffinity is 0, each thread's affinity rotates to one of the CPUs in the system.
     */
    template <typename ...Args>
    void
    start(Args&& ... args) {
        startWithContextProperty<cpa>(true, std::forward<Args>(args) ...);
    }

    /**
     * @brief similarly to the start call for the usage and parameters except the Client thread
     * is not started by  this call and the user is expected to use runOnce() to
     * manually power the Client externally
     * 
     * @tparam Args see start above
     * @param args see start above
     */
    template <typename ...Args>
    void
    registerToRun(Args&& ... args) {
        startWithContextProperty<cpa>(false, std::forward<Args>(args) ...);
    }

    /**
     * @brief stop the message dispatching - asynchronously
     * @details asynchronously means not garanteed message dispatching 
     * stops immidiately after this non-blocking call
     */
    void
    stop() {
        stopWithContextProperty<cpa>();
    }

    /**
     * @brief wait until all threads (Pool threads too if apply) of the Context exit
     * @details blocking call
     */
    void
    join() {
        joinWithContextProperty<cpa>();
    }
   
    /**
     * @brief ipc_creator Context runs a StcuClientPurger to purge crashed (or slow, stuck ...) 
     * Clients from the ipc transport to make the ipc trasnport healthy (avoiding buffer full).
     * It periodically looks for things to purge. This is to set the period (default is 60 seconds).
     * @details If some Client are known to 
     * take long to process messages, increase it. If you need to remove slow Clients quickly
     * reduce it. 
     * Only effective for ipc_creator Context. 
     * 
     * @param s seconds - if set zero, purger is disabled
     */
    void 
    setSecondsBetweenPurge(uint32_t s) {
        secondsBetweenPurge_ = s;
    }
    
    /**
     * @brief normally not used until you want to run your own message loop
     * @details call this function frequently to pump hmbdc message loop in its pool
     * 
     * @param threadSerialNumber starting from 0, indicate which thread in the pool
     * is powering the loop
     */
    void
    runOnce(uint16_t threadSerialNumberInPool) {
        static_assert(cpa::has_pool, "pool is not support in the Context type");
        pool_->runOnce(threadSerialNumberInPool);
    }

    /**
     * @brief normally not used until you want to run your own message loop
     * @details call this function frequently to pump hmbdc message loop for a direct mode Client
     * 
     * @param threadSerialNumber indicate which thread is powering the loop
     * @param c the Client
     * @return true when the Client did not terminate itself by throwing an exeption
     * @return false otherwise
     */
    template <typename Client>
    bool runOnce(Client&& c) {
        // c.messageDispatchingStarted(
        //     hmbdcNumbers_[threadSerialNumber]); //lower level ensures executing only once
        uint16_t tn = cpa::broadcast_msg?c.hmbdcNumber:0;
        primeForShmAtt(c);

        return context_detail::runOnceImpl(tn
            , stopped_, this->buffer_
            , c);
    }

private:
    template <typename cpa>
    typename std::enable_if<cpa::has_pool && !cpa::pool_msgless, typename Pool::ptr>::type
    createPool(size_t maxPoolClientCount) {
        return Pool::create(this->buffer(), maxPoolClientCount);
    }

    template <typename cpa>
    typename std::enable_if<cpa::pool_msgless, typename Pool::ptr>::type
    createPool(size_t maxPoolClientCount) {
        return Pool::create(maxPoolClientCount);
    }

    template <typename cpa>
    typename std::enable_if<!cpa::has_pool && !cpa::pool_msgless, typename Pool::ptr>::type
    createPool(size_t) {
        return typename Pool::ptr();
    }

    template <typename cpa>
    typename std::enable_if<cpa::has_pool, void>::type
    stopWithContextProperty() {
        if (pool_) pool_->stop();
        std::atomic_thread_fence(std::memory_order_acquire);
        stopped_ = true;
    }

    template <typename cpa>
    typename std::enable_if<!cpa::has_pool, void>::type
    stopWithContextProperty() {
        std::atomic_thread_fence(std::memory_order_acquire);
        stopped_ = true;
    }

    template <typename cpa>
    typename std::enable_if<cpa::has_pool, void>::type
    joinWithContextProperty() {
        if (pool_) pool_->join();
        for (auto& t : threads_) {
            t.join();
        }
        threads_.clear();
    }

    template <typename cpa>
    typename std::enable_if<!cpa::has_pool, void>::type
    joinWithContextProperty() {
        for (auto& t : threads_) {
            t.join();
        }
        threads_.clear();
    }

    template <typename cpa>
    void
    reserveSlots(std::list<uint16_t>&) {
    }

    template <typename cpa, typename ...Args>
    typename std::enable_if<cpa::broadcast_msg && !cpa::pool_msgless, void>::type
    reserveSlots(std::list<uint16_t>& slots, uint16_t poolThreadCount, uint64_t, Args&& ... args) {
        auto available = this->buffer_.unusedConsumerIndexes();
        if (available.size() < poolThreadCount) {
            HMBDC_THROW(std::out_of_range
                , "Context remaining capacilty = " << available.size() 
                << ", consider increasing max_parallel_consumer");
        }
        for (uint16_t i = 0; i < poolThreadCount; ++i) {
            slots.push_back(available[i]);
            this->buffer_.reset(available[i]);
        }
        reserveSlots<cpa>(slots, std::forward<Args>(args) ...);
    }

    template <typename cpa, typename ...Args>
    typename std::enable_if<!cpa::broadcast_msg || cpa::pool_msgless, void>::type
    reserveSlots(std::list<uint16_t>& slots, uint16_t poolThreadCount, uint64_t, Args&& ... args) {
        reserveSlots<cpa>(slots, std::forward<Args>(args) ...);
    }

    template <typename cpa, typename CcClient, typename ...Args>
    typename std::enable_if<cpa::broadcast_msg && !std::is_integral<CcClient>::value, void>::type
    reserveSlots(std::list<uint16_t>& slots, CcClient& c, uint64_t, Args&& ... args) {
        const bool clientParticipateInMessaging = 
            std::decay<CcClient>::type::INTERESTS_SIZE != 0;
        if (clientParticipateInMessaging) {
            auto available = this->buffer_.unusedConsumerIndexes();
            if (!available.size()) {
                HMBDC_THROW(std::out_of_range
                    , "Context reached capacity, consider increasing max_parallel_consumer");
            }
            this->buffer_.reset(available[0]);
            slots.push_back(available[0]);
        }
        reserveSlots<cpa>(slots, std::forward<Args>(args) ...);
    }

    template <typename cpa, typename CcClient, typename ...Args>
    typename std::enable_if<!cpa::broadcast_msg && !std::is_integral<CcClient>::value, void>::type
    reserveSlots(std::list<uint16_t>& slots, CcClient& c, uint64_t, Args&& ... args) {
    }

    template <typename cpa, typename ...Args>
    typename std::enable_if<cpa::ipc, void>::type
    startWithContextProperty(bool kickoffThread, Args&& ... args) {
        auto& lock = this->allocator_.fileLock();
        std::lock_guard<decltype(lock)> g(lock);
        std::list<uint16_t> slots;
        try {
            reserveSlots<cpa>(slots, args ...);
            auto sc = slots;
            startWithContextPropertyImpl<cpa>(kickoffThread, sc, std::forward<Args>(args) ...);
        } catch (std::out_of_range const&) {
            Base::markDead(this->buffer_, slots);
            throw;
        }
    }

    template <typename cpa, typename ...Args>
    typename std::enable_if<!cpa::ipc, void>::type
    startWithContextProperty(bool kickoffThread, Args&& ... args) {
        std::list<uint16_t> slots;
        try {
            reserveSlots<cpa>(slots, args ...);
            auto sc = slots;
            startWithContextPropertyImpl<cpa>(kickoffThread, sc, std::forward<Args>(args) ...);
        } catch (std::out_of_range const&) {
            Base::markDead(this->buffer_, slots);
            throw;
        }
    }

    template <typename cpa>
    typename std::enable_if<cpa::broadcast_msg && cpa::ipc, void>::type
    startWithContextPropertyImpl(bool kickoffThread, std::list<uint16_t>& slots) {
        if (ifIpcCreator_ && !purger_ && secondsBetweenPurge_) {
            purger_.reset(
                new StuckClientPurger<Buffer>(secondsBetweenPurge_, this->buffer_));
            startWithContextPropertyImpl<cpa>(kickoffThread, slots, *purger_, purgerCpuAffinityMask_);
        }
    }

    template <typename cpa>
    typename std::enable_if<!cpa::broadcast_msg || !cpa::ipc, void>::type
    startWithContextPropertyImpl(bool kickoffThread, std::list<uint16_t>& slots) {
    }

    template <typename cpa, typename ...Args>
    typename std::enable_if<cpa::has_pool, void>::type
    startWithContextPropertyImpl(bool kickoffThread, std::list<uint16_t>& slots
        , uint16_t poolThreadCount, uint64_t poolThreadsCpuAffinityMask
        , Args&& ... args) {
        if (poolThreadCount_) {
            HMBDC_THROW(std::out_of_range, "Context pool already started");
        }
        std::vector<uint16_t> sc(slots.begin(), slots.end());
        if (!poolThreadsCpuAffinityMask) {
            auto cpuCount = std::thread::hardware_concurrency();
            poolThreadsCpuAffinityMask = 
                ((1ul << poolThreadCount) - 1u) << (hmbdcNumbers_.size() % cpuCount);
        }

        pool_->startAt(poolThreadCount, poolThreadsCpuAffinityMask, sc);
        while(poolThreadCount--) {
            if (!cpa::pool_msgless) {
                hmbdcNumbers_.push_back(*slots.begin());
                slots.pop_front();
            }
        }
        poolThreadCount_ = poolThreadCount;
        startWithContextPropertyImpl<cpa>(kickoffThread, slots, std::forward<Args>(args) ...);
    }

    template <typename cpa, typename Client, typename ...Args>
    typename std::enable_if<!std::is_integral<Client>::value, void>::type
    startWithContextPropertyImpl(bool kickoffThread, std::list<uint16_t>& slots
        , Client& c, uint64_t cpuAffinity
        , Args&& ... args) {
        auto clientParticipateInMessaging = 
            std::decay<Client>::type::INTERESTS_SIZE;
        uint16_t hmbdcNumber = 0xffffu;
        if (clientParticipateInMessaging && cpa::broadcast_msg) {
            hmbdcNumber = *slots.begin();
            c.hmbdcNumber = hmbdcNumber;
            slots.pop_front();
        }
        if (kickoffThread) {
            auto thrd = kickOffClientThread(
                c, cpuAffinity, hmbdcNumber, hmbdcNumbers_.size());
            threads_.push_back(move(thrd));
        }
        hmbdcNumbers_.push_back(hmbdcNumber);
        startWithContextPropertyImpl<cpa>(kickoffThread, slots, std::forward<Args>(args) ...);
    }

    template <typename Client>
    auto kickOffClientThread(
        Client& c, uint64_t mask, uint16_t hmbdcNumber, uint16_t threadSerialNumber) {
        primeForShmAtt(c);
        std::thread thrd([
            this
            , &c
            , mask
            , h=hmbdcNumber
            , threadSerialNumber
            ]() {
                auto hmbdcNumber = h;
                std::string name;
                char const* schedule;
                int priority;
                auto clientParticipateInMessaging = 
                    std::decay<Client>::type::INTERESTS_SIZE;

                
                if (c.hmbdcName()) {
                    name = c.hmbdcName();
                } else {
                    if (clientParticipateInMessaging) {
                        name = "hmbdc" + std::to_string(hmbdcNumber);
                    } else {
                        name = "hmbdc-x";
                    }
                }
                try {
                    auto cpuAffinityMask = mask; 
                    std::tie(schedule, priority) = c.schedSpec();

                    if (!schedule) schedule = "SCHED_OTHER";

                    if (!mask) {
                        auto cpuCount = std::thread::hardware_concurrency();
                        cpuAffinityMask = 1ul << (threadSerialNumber % cpuCount);
                    }

                    hmbdc::os::configureCurrentThread(name.c_str(), cpuAffinityMask
                        , schedule, priority);
                    
                    hmbdcNumber = clientParticipateInMessaging?hmbdcNumber:0xffffu;
                    reinterpret_cast<std::atomic<size_t>*>(this->pDispStartCount_)
                        ->fetch_add(1, std::memory_order_release);
                    c.messageDispatchingStartedCb(this->pDispStartCount_);
                } catch (std::exception const& e) {
                    c.stopped(e);
                    return;
                } catch (int code) {
                    c.stopped(ExitCode(code));
                    return;
                } catch (...) {
                    c.stopped(UnknownException());
                    return;
                }

                bool stillIn = true;
                while(!stopped_ &&
                    (stillIn = context_detail::runOnceImpl(hmbdcNumber, this->stopped_, this->buffer_, c))) {
                }
                if (this->stopped_) {
                    if (clientParticipateInMessaging) { /// drain all to release sending party
                        typename Buffer::iterator begin, end;
                        size_t count;
                        do {
                            usleep(10000);
                            if constexpr (cpa::broadcast_msg) {
                                count = this->buffer_.peek(hmbdcNumber, begin, end);
                                this->buffer_.wasteAfterPeek(hmbdcNumber, count);
                            } else {
                                count = this->buffer_.peek(begin, end);
                                this->buffer_.wasteAfterPeek(begin, count);
                            }
                        } while (count);
                    }
                    
                    if (stillIn) {
                        c.dropped();
                    }
                }
                if (clientParticipateInMessaging) context_detail::unblock(this->buffer_, hmbdcNumber);
            }
        );

        return thrd;
    }

    template <typename Client>
    void primeForShmAtt(Client& c) {
        if constexpr (cpa::ipc && context_detail::has_ibmaProc<Client>::value) {
            if constexpr(!std::is_same<std::nullptr_t, decltype(c.ibmaProc)>::value) {
                if (!c.ibmaProc.hmbdcShmHandleToAddr) {
                    c.ibmaProc.hmbdcShmHandleToAddr = [&alloc = this->shmAttAllocator_]
                        (boost::interprocess::managed_shared_memory::handle_t h) {
                        return alloc->getAddr(h);
                    };
                    c.ibmaProc.hmbdcShmDeallocator = [&alloc = this->shmAttAllocator_]
                        (uint8_t* addr) {
                        return alloc->deallocate(addr);
                    };
                }
            }
        }
    }

    Context(Context const&) = delete;
    Context& operator = (Context const&) = delete;
    uint16_t usedHmbdcCapacity_;
    std::vector<uint16_t> hmbdcNumbers_;

    std::atomic<bool> stopped_;
    typename Pool::ptr pool_;
    using Threads = std::vector<std::thread>;
    Threads threads_;
    size_t poolThreadCount_;
    uint32_t secondsBetweenPurge_;
    bool const ifIpcCreator_ = false;
    uint64_t purgerCpuAffinityMask_;
    typename std::conditional<cpa::broadcast_msg && cpa::ipc
        , std::unique_ptr<StuckClientPurger<Buffer>>, uint32_t
    >::type purger_;
};

}}

