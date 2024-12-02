#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/DefaultUserConfig.hpp"
#include "hmbdc/tips/Messages.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/app/BlockingContext.hpp"
#include "hmbdc/app/BlockingContextRt.hpp"
#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/ClientWithStash.hpp"
#include "hmbdc/app/Context.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/pattern/GuardedSingleton.hpp"
#include "hmbdc/MetaUtils.hpp"

#include <deque>
#include <optional>
#include <type_traits>
#include <unistd.h>
#include <stdlib.h>

namespace hmbdc { namespace tips {

/**
 * @brief template that Domain uses for the network communication properties
 * 
 * @tparam Protocol the TIPS network protocol in use - tcpcast, rmcast etc
 * @tparam MaxMessageSize the compile time specified max size of the network transferred
 * message, this does not include the attachment size, it could be simply set to be 
 * something like hmbdc::max_size_in_tuple<AllSendMessagesTuple>
 * If set to be 0, the value becomes runtime configured.
 */
template <typename Protocol, size_t MaxMessageSize = 1000>
struct net_property {
    enum {
        max_message_size = MaxMessageSize
    };
    using protocol = Protocol;
};

/**
 * @brief Placeholder for the Protocol within net_property
 * that turns off network communication at compile time
 * 
 */
struct NoProtocol
: pattern::GuardedSingleton<NoProtocol> {
    using SendTransportEngine = void*;
    template <typename Buffer, typename AttachmentAllocator>
    using RecvTransportEngine = void*;
    static constexpr auto dftConfig() { return "{}"; }

    /**
     * @brief construct the host-wide unique TIPS domain name
     * from a configure file
     * 
     * @param cfg the configure file that contains network specifics
     * @return std::string 
     */
    std::string getTipsDomainName(app::Config const& cfg) {
        return cfg.getExt<std::string>("localDomainName");
    }

    private:
    friend pattern::SingletonGuardian<NoProtocol>;
    NoProtocol(){}
};

/**
 * @brief net_property that turns off network communication at compile time
 * 
 */
using NoNet = net_property<NoProtocol, 0>;

/**
 * @brief template that Domain uses for the IPC communication properties
 * 
 * @tparam IpcCapacity power of 2 value startign from 4, (8, 16 ... 256)
 *      It specify up to how many IPC parties (processes) are communicating
 * @tparam MaxMessageSize the compile time specified max size of the IPC transferred
 * message, this does not include the attachment size, it could be simply set to be 
 * something like hmbdc::max_size_in_tuple<AllSendMessagesTuple>
 * If set to be 0, the value becomes configured by ipcMaxMessageSizeRuntime.
 * @tparam UseDevMem set this to true if using the PCI dev shared memory (beta)
 * This is useful when doing pub/sub between processes distributed on both 
 * PCI dev and its host
 */
template <uint16_t IpcCapacity = 64, size_t MaxMessageSize = 1000, bool UseDevMem = false>
struct ipc_property {
    enum {
        capacity = IpcCapacity,
        max_message_size = MaxMessageSize,
        use_dev_mem = UseDevMem
    };
};

/**
 * @brief ipc_property that turns off IPC communication at compile time
 * 
 */
using NoIpc = ipc_property<0, 0>;

namespace domain_detail {
struct not_timermanager{};

template <typename CcNode>
struct NodeProxy
: std::conditional_t<CcNode::HasMessageStash
    , typename app::client_with_stash_using_tuple<NodeProxy<CcNode>, 0, typename CcNode::RecvMessageTuple>::type
    , typename app::client_using_tuple<NodeProxy<CcNode>, typename CcNode::RecvMessageTuple>::type
>
, std::conditional_t<std::is_base_of<time::TimerManagerTrait, CcNode>::value
    , time::TimerManagerTrait
    , not_timermanager
> {
    using SendMessageTuple = typename CcNode::SendMessageTuple;
    using RecvMessageTuple = typename CcNode::RecvMessageTuple;

    NodeProxy(CcNode& ccNode): ccNode_(ccNode){}

    /// forward on behalf of Node
    auto maxMessageSize() const {return ccNode_.maxMessageSize();}
    void addJustBytesSubsForCfg(std::function<void(uint16_t)> addTag) const {ccNode_.addJustBytesSubsForCfg(addTag);}
    void addJustBytesPubsForCfg(std::function<void(uint16_t)> addTag) const {ccNode_.addJustBytesPubsForCfg(addTag);}
    template <app::MessageC Message>
    void addTypeTagRangeSubsForCfg(Message* pm, std::function<void(uint16_t)> addOffsetInRange) const {
        ccNode_.addTypeTagRangeSubsForCfg(pm, addOffsetInRange);
    }
    template <app::MessageC Message>
    void addTypeTagRangePubsForCfg(Message* pm, std::function<void(uint16_t)> addOffsetInRange) const {
        ccNode_.addTypeTagRangePubsForCfg(pm, addOffsetInRange);
    }
    void updateSubscription() {ccNode_.updateSubscription();}
    template<app::MessageC Message>
    auto ifDeliverCfg(Message const& message) const {return ccNode_.ifDeliverCfg(message);}
    auto ifDeliverCfg(uint16_t tag, uint8_t const* bytes) const {return ccNode_.ifDeliverCfg(tag, bytes);}

    /// forward on behalf of Client
    auto hmbdcName() const {return ccNode_.hmbdcName();}
    auto schedSpec() const {return ccNode_.schedSpec();}
    template<typename Message> auto handleMessageCb(Message&& m) {ccNode_.handleMessageCb(std::forward<Message>(m));}
    void handleJustBytesCb(uint16_t tag, uint8_t const* bytes, app::hasMemoryAttachment* att) {
        ccNode_.handleJustBytesCb(tag, bytes, att);
    }
    void messageDispatchingStartedCb(std::atomic<size_t> const*p) override {ccNode_.messageDispatchingStartedCb(p);}
    void invokedCb(size_t n) override {ccNode_.invokedCb(n);}
    void stoppedCb(std::exception const& e) override {ccNode_.stoppedCb(e);}
    bool droppedCb() override {
        auto res = ccNode_.droppedCb();
        if (res) {
            delete this;
        }
        return res;
    }

    /// forward on behalf of TimerManager
    void checkTimers(time::SysTime t) { ccNode_.checkTimers(t); }
    time::Duration untilNextFire() const { return ccNode_.untilNextFire(); }

private:
    CcNode& ccNode_;
};

HMBDC_CLASS_HAS_DECLARE(droppedCb);
HMBDC_CLASS_HAS_DECLARE(stoppedCb);
HMBDC_CLASS_HAS_DECLARE(invokedCb);
HMBDC_CLASS_HAS_DECLARE(messageDispatchingStartedCb);

template <app::MessageC Message, bool canSerialize = has_toHmbdcIpcable<Message>::value>
struct matching_ipcable {
    using type = std::result_of_t<decltype(&Message::toHmbdcIpcable)(Message)>;
};

template <app::MessageC Message>
struct matching_ipcable<Message, false> {
    using type = Message;
};

template <app::MessageTupleC MessageTuple>
struct recv_ipc_converted;

template <>
struct recv_ipc_converted<std::tuple<>> {
    using type = std::tuple<>;
};

template <app::MessageTupleC MessageTuple>
struct recv_net_converted;

template <>
struct recv_net_converted<std::tuple<>> {
    using type = std::tuple<>;
};


template <app::MessageC Message>
struct is_threadable {
    enum {
        value = [](){
            if constexpr (is_derived_from_non_type_template_v<int, do_not_send_via, Message>) {
                auto mask = Message::do_not_send_via::mask;
                return ((int)mask & (int)INTER_THREAD) ? 0 : 1;
            }
            return 1;
        }(),
    };
};

template <app::MessageC Message>
struct is_ipcable {
    enum {
        value = [](){
            constexpr int res = std::is_trivially_destructible<
                typename matching_ipcable<Message>::type>::value;
            if constexpr (res && is_derived_from_non_type_template_v<int, do_not_send_via, Message>) {
                auto mask = Message::do_not_send_via::mask;
                return ((int)mask & (int)INTER_PROCESS) ? 0 : 1;
            }
            return res;
        }(),
    };
};

template <app::MessageC Message>
struct is_netable {
    enum {
        value = [](){
            constexpr int res = std::is_trivially_destructible<
                typename matching_ipcable<Message>::type>::value;
            if constexpr (res && is_derived_from_non_type_template_v<int, do_not_send_via, Message>) {
                auto mask = Message::do_not_send_via::mask;
                return ((int)mask & (int)OVER_NETWORK) ? 0 : 1;
            }
            return res;
        }(),
    };
};

template <app::MessageC Message, app::MessageC ...Messages>
struct recv_ipc_converted<std::tuple<Message, Messages...>> {
    using next = typename recv_ipc_converted<std::tuple<Messages...>>::type;
    using type = std::conditional_t<is_ipcable<Message>::value
        , typename add_if_not_in_tuple<typename matching_ipcable<Message>::type, next>::type
        , next
    >;
};

template <app::MessageC Message, app::MessageC ...Messages>
struct recv_net_converted<std::tuple<Message, Messages...>> {
    using next = typename recv_net_converted<std::tuple<Messages...>>::type;
    using type = std::conditional_t<is_netable<Message>::value
        , typename add_if_not_in_tuple<typename matching_ipcable<Message>::type, next>::type
        , next
    >;
};

template <app::MessageTupleC RecvMessageTuple>
struct need_recv_eng_at {
    bool operator()(uint16_t mod, uint16_t res) const { return false; }
};

template <app::MessageC M, app::MessageC ...Ms> 
struct need_recv_eng_at<std::tuple<M, Ms...>> {
    bool operator()(uint16_t mod, uint16_t res) const {
        if constexpr (!M::hasRange) {
            if (M::typeTag % mod == res) return true;
        } else {
            if (M::typeTagRange >= mod) return true;
            for (auto tag = M::typeTagStart; tag < M::typeTagStart + M::typeTagRange; tag++) {
                if (tag % mod == res) return true;
            }
        }
        return need_recv_eng_at<std::tuple<Ms...>>{}(mod, res);
    }
};

template <app::MessageC ...Ms> 
struct need_recv_eng_at<std::tuple<app::JustBytes, Ms...>> {
    bool operator()(uint16_t mod, uint16_t res) const { return true; }
};

template <app::MessageC Message>
struct ipc_from : Message {
    ipc_from(pid_t from, Message const& m)
    : Message(m)
    , hmbdcIpcFrom(from) {}
    pid_t hmbdcIpcFrom;
};

} //domain_detail

/**
 * @brief when Domain (it's pumps) receives a hasMemoryAttachment, there is a need
 * to allocate desired memory to hold the attachment bytes and release it after consumed.
 * This is the policy type dictating how that is done by default - using malloc/free
 */
struct DefaultAttachmentAllocator {
    /**
     * @brief fill in hasMemoryAttachment so it holds the desired
     * memory and the incoming attachment can be holden
     * 
     * @param typeTag the hasMemoryAttachment message type tag
     * @param att the hasMemoryAttachment struct to be filled in
     * @return the allocated memory
     */
    void* operator()(uint16_t typeTag, app::hasMemoryAttachment* att) {
        att->attachment = ::malloc(att->len);
        att->afterConsumedCleanupFunc = [](app::hasMemoryAttachment* hasAtt) {
            ::free(hasAtt->attachment);
            hasAtt->attachment = nullptr;
        };
        return att->attachment;
    }
};

/**
 * @brief Messages published on a TIPS pub/sub domain reach all the Nodes
 * within that domain based on their subscriptions.
 * This class represents a TIPS domain's handle / interface / fascade in its process.
 * Typically - by recommendation - there is just a single Domain object for a specific TIPS domain
 * within a process.
 * 
 * @details A Domain object also manages a group of Nodes on their communication
 * across the TIPS domain that this Domain object maps to.
 * The TIPS domain is determined by the network configurations
 * such as which NIC interface and multicast addresses configured.
 * see getTipsDomainName for network transport protocol
 * 
 * A Node can only be managed (be started) within a single Domain. 
 * A Domain instance can only be configured to map to a single TIPS domain
 * Multiple Domain objects could be mapped to a single TIPS domain - in one (not recommended) or
 * multiple processes on the same or different networked hosts.
 * 
 * @tparam RecvMessageTupleIn a std tuple that lists all the receiving message types
 * Any inbound messages (to the Domain) that are not within this list are dropped without
 * being deliverred to the Nodes in this Domain
 * @tparam IpcProp IPC preoperty - see ipc_property template
 * @tparam NetProp Network communication properties - see net_property template
 * @tparam NodeContext the type that manages the nodes and accepts the inter thread messages
 * the default app::BlockingContext is a relaible delivery NodeContext; Consider use 
 * app::BlockingContextRt for realtime application that only cares receiving the latest messages and lose
 * the older ones
 * @tparam AttachmentAllocator the memory allocation policy for attachment - see DefaultAttachmentAllocator
 */
template <app::MessageTupleC RecvMessageTupleIn
    , typename IpcProp = NoIpc
    , typename NetProp = NoNet
    , typename NodeContext = app::BlockingContext<RecvMessageTupleIn>
    , typename AttachmentAllocator = DefaultAttachmentAllocator >
struct Domain {
    using IpcProperty = IpcProp;
    using NetProperty = NetProp;
    using NetProtocol = typename NetProperty::protocol;
protected:
    using ThreadCtx = NodeContext;
    ThreadCtx threadCtx_;

private:
    using RecvMessageTuple = typename hmbdc::remove_duplicate<RecvMessageTupleIn>::type;

    using IpcableRecvMessages = typename domain_detail::recv_ipc_converted<RecvMessageTuple>::type;
    using NetableRecvMessages = typename domain_detail::recv_net_converted<RecvMessageTuple>::type;

    enum {
        run_pump_in_ipc_portal = IpcProperty::capacity != 0,
        run_pump_in_thread_ctx = run_pump_in_ipc_portal
            ? 0 : !std::is_same<NoProtocol, typename NetProperty::protocol>::value,
        has_a_pump = run_pump_in_ipc_portal || run_pump_in_thread_ctx,
        has_net_send_eng = !std::is_same<NoProtocol, typename NetProperty::protocol>::value,
        has_net_recv_eng = !std::is_same<NoProtocol, typename NetProperty::protocol>::value
            && std::tuple_size<NetableRecvMessages>::value,
    };
    
    template <app::MessageC Message> using ipc_from = domain_detail::ipc_from<Message>;

    using IpcSubscribeMessagesPossible = IpcableRecvMessages;
    using IpcTransport = std::conditional_t<
        IpcProperty::capacity != 0
        , app::Context<
            IpcProperty::max_message_size
            , app::context_property::broadcast<IpcProperty::capacity>
            , std::conditional_t<IpcProperty::use_dev_mem
                , app::context_property::dev_ipc
                , app::context_property::ipc_enabled>
        >
        , void*
    >;

    std::optional<IpcTransport> ipcTransport_;
    std::optional<
        std::conditional_t<IpcProperty::use_dev_mem
            , os::DevMemBasePtrAllocator
            , os::ShmBasePtrAllocator>
    > allocator_;
    TypeTagSet* pOutboundSubscriptions_ = nullptr;

    struct OneBuffer {
        ThreadCtx& threadCtx;
        size_t maxItemSize_;

        OneBuffer(ThreadCtx& threadCtx, size_t maxItemSizeIn) 
        : threadCtx(threadCtx)
        , maxItemSize_(maxItemSizeIn) {
        }
        size_t maxItemSize() const {
            return maxItemSize_ + sizeof(app::MessageHead);
        }

        template <app::MessageC Message>
        void handleMessageCb(Message& msg) {
            using hasMemoryAttachment = app::hasMemoryAttachment;
            if constexpr (has_fromHmbdcIpcable<Message>::value) {
                static_assert(std::is_same<Message
                    , decltype(msg.fromHmbdcIpcable().toHmbdcIpcable())>::value
                    , "mising or wrong toHmbdcIpcable() func - cannot serialize");
                threadCtx.send(msg.fromHmbdcIpcable());
            } else {
                threadCtx.send(msg);
            }
            if constexpr(std::is_base_of<hasMemoryAttachment, Message>::value) {
                /// do not let MD release the data
                msg.hasMemoryAttachment::attachment = nullptr;
                msg.hasMemoryAttachment::len = 0;
            } 
        }

        void handleJustBytesCb(uint16_t tag, void const* bytes, app::hasMemoryAttachment* att) {
            threadCtx.sendJustBytesInPlace(tag, bytes, maxItemSize_, att);
            if (att) {
                /// do not let MD release the data
                att->attachment = nullptr;
                att->len = 0;
            }
        }

        void put(void* item, size_t) {
            using NoAttRecv = typename filter_out_tuple_by_base<app::hasMemoryAttachment
                , NetableRecvMessages>::type;
            app::MessageDispacher<OneBuffer, NoAttRecv> disp;
            auto& h = *(app::MessageHead*)(item);
            h.scratchpad().desc.flag = 0;
            disp(*this, h);
        }

        void putAtt(app::MessageWrap<app::hasMemoryAttachment>* item, size_t) {
            using AttRecv = typename filter_in_tuple_by_base<app::hasMemoryAttachment
                , NetableRecvMessages>::type;
            app::MessageDispacher<OneBuffer, AttRecv> disp;
            item->scratchpad().desc.flag = app::hasMemoryAttachment::flag;
            if (disp(*this, *item)) {
            } else {
                /// MD did not release the data, do it here
                item->payload.release();
            }
        }
        
        template <typename T> void put(T& item) {put(&item, sizeof(T));}
        template <typename T> void putSome(T& item) {
            put(&item, std::min(sizeof(item), maxItemSize()));
        }
    };
    
    class PumpInThreadCtx 
    : public app::client_using_tuple<PumpInThreadCtx, std::tuple<>>::type {
        std::optional<typename NetProtocol::SendTransportEngine> sendEng_;
        using RecvTransportEngine 
            = typename NetProtocol::template RecvTransportEngine<OneBuffer, AttachmentAllocator>;
        std::optional<RecvTransportEngine> recvEng_;
        ThreadCtx& outCtx_;
        OneBuffer netBuffer_;
        std::string hmbdcName_;
        const uint16_t mod;
        const uint16_t res;
        public:
        typename ThreadCtx::ClientRegisterHandle handleInCtx;
        
        PumpInThreadCtx(ThreadCtx& outCtx, app::Config const& cfg
            , uint16_t mod, uint16_t res)
        : outCtx_(outCtx)
        , netBuffer_{outCtx
            , NetProperty::max_message_size != 0
                ? NetProperty::max_message_size 
                : cfg.getExt<uint32_t>("netMaxMessageSizeRuntime")
        }
        , hmbdcName_(cfg.getExt<std::string>("pumpHmbdcName"))
        , mod(mod)
        , res(res) {
            if constexpr (has_net_send_eng) {
                uint32_t limit = NetProperty::max_message_size;
                if (limit == 0) {
                    limit = cfg.getExt<uint32_t>("netMaxMessageSizeRuntime");
                }
                sendEng_.emplace(cfg, limit);
            }
            if constexpr (has_net_recv_eng) {
                if (domain_detail::need_recv_eng_at<NetableRecvMessages>{}(mod, res)) {
                    recvEng_.emplace(cfg, netBuffer_);
                }
            }
        }

        template <typename CcNode>
        void subscribeFor(CcNode const& node) {
            if constexpr (has_net_recv_eng) {
                using Messages = typename filter_in_tuple<domain_detail::is_netable
                    , typename CcNode::RecvMessageTuple>::type;
                if (recvEng_) recvEng_->template subscribeFor<Messages>(node, mod, res);
            }
        }

        size_t ipcSubscribingPartyCount(uint16_t tag) const {
            return 0;
        }

        size_t netSubscribingPartyCount(uint16_t tag) const {
            auto res = size_t{0};
            if constexpr (has_net_send_eng) {
                res += sendEng_->subscribingPartyDetectedCount(tag);
            }
            return res;
        }

        template <typename CcNode>
        void advertiseFor(CcNode const& node) {
            if constexpr (has_net_send_eng) {
                using Messages = typename filter_in_tuple<domain_detail::is_netable
                    , typename CcNode::SendMessageTuple>::type;
                sendEng_->template advertiseFor<Messages>(node, mod, res);
            }
        }

        size_t netSendingPartyDetectedCount() const {
            if constexpr (has_net_recv_eng) {
                return recvEng_ ? recvEng_->sessionsRemainingActive() : 0;
            }
            return 0;
        }

        size_t netRecvingPartyDetectedCount() const {
            if constexpr (has_net_send_eng) {
                return sendEng_->sessionsRemainingActive();
            }
            return 0;
        }
        
        template <app::MessageC Message>
        void send(Message const & message) {
            using M = typename std::decay<Message>::type;
            if constexpr (has_net_send_eng && domain_detail::is_netable<M>::value) {
                if constexpr(has_toHmbdcIpcable<M>::value) {
                    static_assert(std::is_same<M
                        , decltype(message.toHmbdcIpcable().fromHmbdcIpcable())>::value
                        , "mising or wrong fromHmbdcIpcable() func - cannot convertback");
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(decltype(message.toHmbdcIpcable())) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    sendEng_->queue(message.toHmbdcIpcable());
                } else {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(message) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    sendEng_->queue(message);
                }
            }
        }

        template <app::MessageC Message>
        bool trySend(Message const & message) {
            using M = typename std::decay<Message>::type;
            if constexpr (has_net_send_eng && domain_detail::is_netable<M>::value) {
                if constexpr(has_toHmbdcIpcable<M>::value) {
                    static_assert(std::is_same<M
                        , decltype(message.toHmbdcIpcable().fromHmbdcIpcable())>::value
                        , "mising or wrong fromHmbdcIpcable() func - cannot convertback");
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(decltype(message.toHmbdcIpcable())) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    return sendEng_->tryQueue(message.toHmbdcIpcable());
                } else {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(message) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    return sendEng_->tryQueue(message);
                }
            }
            return true;
        }

        void sendJustBytes(uint16_t tag, void const* bytes, size_t len
            , app::hasMemoryAttachment* att) {
            if constexpr (has_net_send_eng) {
                sendEng_->queueJustBytes(tag, bytes, len, att);
            }
        }

        char const* hmbdcName() const {
            return hmbdcName_.c_str();
        }

        void messageDispatchingStartedCb(std::atomic<size_t> const* p) override {
            if constexpr (domain_detail::has_messageDispatchingStartedCb<ThreadCtx>::value) {
                outCtx_.messageDispatchingStartedCb(p);
            }
        }

        void invokedCb(size_t previousBatch) override {
            bool layback = !previousBatch;
            if constexpr (has_net_send_eng) {
                layback = layback && !sendEng_->bufferedMessageCount();
            }
            if constexpr (domain_detail::has_invokedCb<ThreadCtx>::value) {
                outCtx_.invokedCb(previousBatch);
            }
            if constexpr (has_net_recv_eng) {
                if (hmbdc_likely(recvEng_)) {
                    recvEng_->rotate();
                }
            }
            if constexpr (has_net_send_eng) {
                sendEng_->rotate();
            }
            if constexpr (has_net_send_eng || has_net_recv_eng) {
                layback = layback 
                    && !(hmbdc::app::utils::EpollTask::initialized() && hmbdc::app::utils::EpollTask::instance().getPollPending());
            }
            if (layback) {
                std::this_thread::yield();
            }
                
        }

        void stoppedCb(std::exception const& e) override {
            if constexpr (domain_detail::has_stoppedCb<ThreadCtx>::value) {
                outCtx_.stoppedCb(e);
            }
        }

        bool droppedCb() override {
            if constexpr (domain_detail::has_droppedCb<ThreadCtx>::value) {
                return outCtx_.droppedCb();
            }
            return true;
        };

        void stop() {
            if constexpr (has_net_send_eng) {
                sendEng_->stop();
            }
        }
    };

    template <size_t MAX_MEMORY_ATTACHMENT>
    struct InBandMemoryAttachmentProcessor {
        InBandMemoryAttachmentProcessor()
        : att(new(underlyingMessage) app::hasMemoryAttachment){}
        uint8_t underlyingMessage[MAX_MEMORY_ATTACHMENT];
        AttachmentAllocator attAllocator;
        
        template <app::MessageC Message>
        bool cache(app::InBandHasMemoryAttachment<Message> const& ibma, uint16_t typeTagIn) {
            static_assert(sizeof(Message) <= MAX_MEMORY_ATTACHMENT, "");
            if (hmbdc_unlikely(att->attachment)) {
                HMBDC_THROW(std::logic_error, "previous InBandMemoryAttachment not concluded");
            }
            typeTag = typeTagIn;
            if constexpr(Message::justBytes) {
                memcpy(underlyingMessage, &ibma.underlyingMessage, sizeof(underlyingMessage));
            } else {
                memcpy(underlyingMessage, &ibma.underlyingMessage, sizeof(Message));
            }

            if (hmbdc_unlikely(!att->len)) {
                att->attachment = nullptr;
                att->afterConsumedCleanupFunc = nullptr;
                accSize = 0;
                return true;
            } else if (att->holdShmHandle<Message>()) {
                if constexpr (app::has_hmbdcIsAttInShm<Message>::value) {
                    auto shmAddr = hmbdcShmHandleToAddr(att->shmHandle);
                    att->attachment = shmAddr;
                    att->clientData[0] = (uint64_t)&hmbdcShmDeallocator;
                    att->afterConsumedCleanupFunc = [](app::hasMemoryAttachment* h) {
                        auto hmbdc0cpyShmRefCount = &Message::getHmbdc0cpyShmRefCount(*h);
                        if (h->attachment 
                            && 0 == --*reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)) {
                            // && 0 == __atomic_sub_fetch(hmbdc0cpyShmRefCount, 1, __ATOMIC_RELEASE)) {
                            auto& hmbdcShmDeallocator
                                = *(std::function<void (uint8_t*)>*)h->clientData[0];
                            hmbdcShmDeallocator((uint8_t*)h->attachment);
                        }
                    };
                    accSize = att->len;
                } /// no else
                return true;
            } else {
                attAllocator(typeTagIn, att);
                // att->attachment = malloc(att->len);
                // att->afterConsumedCleanupFunc = hasMemoryAttachment::free;
                accSize = 0;
                return false;
            }
        }

        bool accumulate(app::InBandMemorySeg const& ibms, size_t n) {
            if (accSize < att->len) {
                auto attBytes = ibms.seg;
                auto copySize = std::min(n, att->len - accSize);
                memcpy((char*)att->attachment + accSize, attBytes, copySize);
                accSize += copySize;
                return accSize == att->len;
            }
            return true;
        }
        app::hasMemoryAttachment * const att;
        uint16_t typeTag = 0;
        size_t accSize = 0;
        std::function<void* (boost::interprocess::managed_shared_memory::handle_t)> 
            hmbdcShmHandleToAddr;
        std::function<void (uint8_t*)> hmbdcShmDeallocator;
    };

    class PumpInIpcPortal 
    : public app::client_using_tuple<PumpInIpcPortal, IpcSubscribeMessagesPossible>::type {
        std::optional<typename NetProtocol::SendTransportEngine> sendEng_;
        using RecvTransportEngine 
            = typename NetProtocol::template RecvTransportEngine<OneBuffer, AttachmentAllocator>;
        std::optional<RecvTransportEngine> recvEng_;
        IpcTransport& ipcTransport_;
        ThreadCtx& outCtx_;
        OneBuffer netBuffer_;
        TypeTagSet* pOutboundSubscriptions_;
        TypeTagSet inboundSubscriptions_;
        std::string hmbdcName_;
        uint32_t pumpMaxBlockingTimeUs_;
        const uint16_t mod;
        const uint16_t res;

        public:
        InBandMemoryAttachmentProcessor<
            std::max((size_t)PumpInIpcPortal::MAX_MEMORY_ATTACHMENT, (size_t)(64 * 1024))> ibmaProc;
        pid_t const hmbdcAvoidIpcFrom = getpid();
        PumpInIpcPortal(IpcTransport& ipcTransport
            , TypeTagSet* pOutboundSubscriptions
            , ThreadCtx& outCtx, app::Config const& cfg
            , uint16_t mod, uint16_t res)
        : ipcTransport_(ipcTransport)
        , outCtx_(outCtx)
        , netBuffer_{outCtx
            , NetProperty::max_message_size != 0
                ? NetProperty::max_message_size 
                : cfg.getExt<uint32_t>("netMaxMessageSizeRuntime")
        }
        , pOutboundSubscriptions_(pOutboundSubscriptions)
        , hmbdcName_(cfg.getExt<std::string>("pumpHmbdcName"))
        , pumpMaxBlockingTimeUs_(cfg.getHex<double>("pumpMaxBlockingTimeSec") * 1000000)
        , mod(mod)
        , res(res) {
            pumpMaxBlockingTimeUs_ = std::min(1000000u, pumpMaxBlockingTimeUs_);
            static_assert(IpcTransport::MAX_MESSAGE_SIZE == 0 || IpcTransport::MAX_MESSAGE_SIZE 
                >= max_size_in_tuple<IpcSubscribeMessagesPossible>::value);
            
            if constexpr (has_net_send_eng) {
                uint32_t limit = NetProperty::max_message_size;
                if (limit == 0) {
                    limit = cfg.getExt<uint32_t>("netMaxMessageSizeRuntime");
                }
                sendEng_.emplace(cfg, limit);
            }
            if constexpr (has_net_recv_eng) {
                if (domain_detail::need_recv_eng_at<NetableRecvMessages>{}(mod, res)) {
                    recvEng_.emplace(cfg, netBuffer_);
                }
            }
        }

        template <typename CcNode>
        void subscribeFor(CcNode const& node) {
            using Messages = typename filter_in_tuple<domain_detail::is_ipcable
                , typename CcNode::RecvMessageTuple>::type;
            inboundSubscriptions_.markSubsFor<Messages>(node, mod, res
                , [this](uint16_t tag) {
                    pOutboundSubscriptions_->add(tag);
                });
            
            if constexpr (has_net_recv_eng) {
                using Messages = typename filter_in_tuple<domain_detail::is_netable
                    , typename CcNode::RecvMessageTuple>::type;
                if (recvEng_) recvEng_->template subscribeFor<Messages>(node, mod, res);
            }
        }

        size_t ipcSubscribingPartyCount(uint16_t tag) const {
            return pOutboundSubscriptions_->check(tag) - inboundSubscriptions_.check(tag);
        }

        size_t netSubscribingPartyCount(uint16_t tag) const {
            auto res = size_t{0};
            if constexpr (has_net_send_eng) {
                res += sendEng_->subscribingPartyDetectedCount(tag);
            }
            return res;
        }

        template <typename CcNode>
        void advertiseFor(CcNode const& node) {
            using Messages = typename filter_in_tuple<domain_detail::is_netable
                , typename CcNode::SendMessageTuple>::type;
            if constexpr (has_net_send_eng) {
                sendEng_->template advertiseFor<Messages>(node, mod, res);
            }
        }

        size_t netSendingPartyDetectedCount() const {
            if constexpr (has_net_recv_eng) {
                return recvEng_ ? recvEng_->sessionsRemainingActive() : 0;
            }
            return 0;
        }

        size_t netRecvingPartyDetectedCount() const {
            if constexpr (has_net_send_eng) {
                return sendEng_->sessionsRemainingActive();
            }
            return 0;
        }
        

        template <app::MessageC Message>
        void send(Message&& message) {
            using M = typename std::decay<Message>::type;
            using Mipc = typename domain_detail::matching_ipcable<M>::type;

            constexpr bool disableInterProcess = !domain_detail::is_ipcable<M>::value;
            constexpr bool disableNet = !(domain_detail::is_netable<M>::value && has_net_send_eng);
            std::optional<Mipc> serializedCached;
            app::hasMemoryAttachment::AfterConsumedCleanupFunc afterConsumedCleanupFuncKept = nullptr;
            (void)afterConsumedCleanupFuncKept;
            if constexpr (!disableInterProcess) {
                //only send when others have interests
                auto intDiff = pOutboundSubscriptions_->check(message.getTypeTag())
                    - inboundSubscriptions_.check(message.getTypeTag());
                if (intDiff) {
                    //ipc message should not come back based on hmbdcAvoidIpcFrom
                    using ToSendType = ipc_from<Mipc>;
                    if constexpr (has_toHmbdcIpcable<M>::value) {
                        static_assert(std::is_same<M
                            , decltype(message.toHmbdcIpcable().fromHmbdcIpcable())>::value
                            , "mising or wrong fromHmbdcIpcable() func - cannot convertback");
                        serializedCached.emplace(message.toHmbdcIpcable());
                        if (!disableNet) {
                            if constexpr (std::is_base_of<app::hasMemoryAttachment, Mipc>::value) {
                                //keep the att around
                                std::swap(afterConsumedCleanupFuncKept, serializedCached->afterConsumedCleanupFunc);
                            }
                        }
                        auto toSend = ToSendType{hmbdcAvoidIpcFrom, *serializedCached};

                        if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                            static_assert(!IpcProperty::use_dev_mem, "not implemented");
                            if (toSend.template holdShmHandle<ToSendType>()) {
                                toSend.hmbdcIsAttInShm = true;
                                auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                // __atomic_add_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                    -> fetch_add(intDiff, std::memory_order_release);
                            }
                        }
                        ipcTransport_.send(std::move(toSend));
                    } else if constexpr(std::is_base_of<app::hasMemoryAttachment, M>::value) {
                        auto toSend = ToSendType{hmbdcAvoidIpcFrom, message};
                        if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                            if (toSend.template holdShmHandle<ToSendType>()) {
                                toSend.hmbdcIsAttInShm = true;
                                auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                // __atomic_add_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                    -> fetch_add(intDiff, std::memory_order_release);
                            }
                        }
                        ipcTransport_.send(std::move(toSend));
                    } else {
                        ipcTransport_.template sendInPlace<ToSendType>(hmbdcAvoidIpcFrom, message);
                    }
                }
            }
            if constexpr (!disableNet) {
                if constexpr(has_toHmbdcIpcable<M>::value) {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(Mipc) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    if (serializedCached) {
                        if constexpr (std::is_base_of<app::hasMemoryAttachment, Mipc>::value) {
                            // restore
                            std::swap(afterConsumedCleanupFuncKept, serializedCached->afterConsumedCleanupFunc);
                        }
                        sendEng_->queue(*serializedCached);
                    } else {
                        sendEng_->queue(message.toHmbdcIpcable());
                    }
                } else {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(Mipc) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    sendEng_->queue(message);
                }
            }
        }

        template <app::MessageC Message>
        bool trySend(Message&& message) {
            using M = typename std::decay<Message>::type;
            using Mipc = typename domain_detail::matching_ipcable<M>::type;

            constexpr bool disableInterProcess = !domain_detail::is_ipcable<M>::value;
            constexpr bool disableNet = !(domain_detail::is_netable<M>::value && has_net_send_eng);
            auto res = true;

            std::optional<Mipc> serializedCached;
            app::hasMemoryAttachment::AfterConsumedCleanupFunc afterConsumedCleanupFuncKept = nullptr;
            (void)afterConsumedCleanupFuncKept;
            if constexpr (!disableInterProcess) {
                //only send when others have interests
                auto intDiff = pOutboundSubscriptions_->check(message.getTypeTag())
                    - inboundSubscriptions_.check(message.getTypeTag());
                if (intDiff) {
                    //ipc message should not come back based on hmbdcAvoidIpcFrom
                    using ToSendType = ipc_from<Mipc>;
                    if constexpr (has_toHmbdcIpcable<M>::value) {
                        static_assert(std::is_same<M
                            , decltype(message.toHmbdcIpcable().fromHmbdcIpcable())>::value
                            , "mising or wrong fromHmbdcIpcable() func - cannot convertback");
                        serializedCached.emplace(message.toHmbdcIpcable());
                        if (!disableNet) {
                            if constexpr (std::is_base_of<app::hasMemoryAttachment, Mipc>::value) {
                                //keep the att around
                                std::swap(afterConsumedCleanupFuncKept, serializedCached->afterConsumedCleanupFunc);
                            }
                        }
                        auto toSend = ToSendType{hmbdcAvoidIpcFrom, *serializedCached};

                        if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                            static_assert(!IpcProperty::use_dev_mem, "not implemented");
                            if (toSend.template holdShmHandle<ToSendType>()) {
                                toSend.hmbdcIsAttInShm = true;
                                auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                // __atomic_add_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                    -> fetch_add(intDiff, std::memory_order_release);
                            }
                        }
                        res = ipcTransport_.trySend(toSend);
                        if (!res) {
                            if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                                if (toSend.template holdShmHandle<ToSendType>()) {
                                    toSend.hmbdcIsAttInShm = true;
                                    auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                    // __atomic_sub_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                    reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                        -> fetch_sub(intDiff, std::memory_order_release);
                                }
                            }
                        }
                    } else if constexpr(std::is_base_of<app::hasMemoryAttachment, M>::value) {
                        auto toSend = ToSendType{hmbdcAvoidIpcFrom, message};
                        if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                            if (toSend.template holdShmHandle<ToSendType>()) {
                                toSend.hmbdcIsAttInShm = true;
                                auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                // __atomic_add_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                    -> fetch_add(intDiff, std::memory_order_release);
                            }
                        }
                        res = ipcTransport_.trySend(toSend);
                        if (!res) {
                            if constexpr (app::has_hmbdcIsAttInShm<ToSendType>::value) {
                                if (toSend.template holdShmHandle<ToSendType>()) {
                                    toSend.hmbdcIsAttInShm = true;
                                    auto hmbdc0cpyShmRefCount = &toSend.getHmbdc0cpyShmRefCount();
                                    // __atomic_sub_fetch(hmbdc0cpyShmRefCount, intDiff, __ATOMIC_RELEASE);
                                    reinterpret_cast<std::atomic<size_t>*>(hmbdc0cpyShmRefCount)
                                        -> fetch_sub(intDiff, std::memory_order_release);
                                }
                            }
                        }
                    } else {
                        res = ipcTransport_.template trySendInPlace<ToSendType>(hmbdcAvoidIpcFrom, message);
                    }
                }
            }
            if constexpr (!disableNet) {
                if constexpr(has_toHmbdcIpcable<M>::value) {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(Mipc) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    if (serializedCached) {    
                        if constexpr (std::is_base_of<app::hasMemoryAttachment, Mipc>::value) {
                            // restore
                            std::swap(afterConsumedCleanupFuncKept, serializedCached->afterConsumedCleanupFunc);
                        }
                        res = sendEng_->tryQueue(*serializedCached) && res;
                    } else {
                        res = sendEng_->tryQueue(message.toHmbdcIpcable()) && res;
                    }
                } else {
                    static_assert(NetProperty::max_message_size == 0 
                        || sizeof(Mipc) <= NetProperty::max_message_size
                        , "NetProperty::max_message_size is too small"); 
                    res = sendEng_->tryQueue(message) && res;
                }
            }
            return res;
        }

        void sendJustBytes(uint16_t tag, void const* bytes, size_t len
            , app::hasMemoryAttachment* att) {
            app::hasMemoryAttachment::AfterConsumedCleanupFunc afterConsumedCleanupFuncKept 
                = att ? att->afterConsumedCleanupFunc : nullptr;
            (void)afterConsumedCleanupFuncKept;
            //only send when others have interests
            if (pOutboundSubscriptions_->check(tag) > inboundSubscriptions_.check(tag)) {
                //ipc message should not come back based on hmbdcAvoidIpcFrom
                if (has_net_send_eng && att) {
                    //keep the att around
                    att->afterConsumedCleanupFunc = nullptr;
                }

                using ToAvoidClangCrash = domain_detail::ipc_from<app::JustBytes>;
                ipcTransport_.template sendJustBytesInPlace<ToAvoidClangCrash>(
                    tag, bytes, len, att, hmbdcAvoidIpcFrom);
            }

            if constexpr (has_net_send_eng) {
                if (att) {
                    att->afterConsumedCleanupFunc = afterConsumedCleanupFuncKept;
                }
                sendEng_->queueJustBytes(tag, bytes, len, att);
            }
        }

        template <typename Iterator>
        size_t handleRangeImpl(Iterator it, 
            Iterator end, uint16_t threadId) {
            size_t res = 0;
            for (;hmbdc_likely(!this->batchDone_ && it != end); ++it) {    
                auto& h = *static_cast<app::MessageHead*>(*it);
                auto tagInEffect = h.typeTag;
                if (tagInEffect == app::InBandHasMemoryAttachment<app::Flush>::typeTag) {
                    tagInEffect = h.scratchpad().ipc.hd.inbandUnderlyingTypeTag;
                }
                if (hmbdc_unlikely(tagInEffect > app::LastSystemMessage::typeTag
                    && !inboundSubscriptions_.check(tagInEffect))) {
                    continue;
                } else if (app::MessageDispacher<PumpInIpcPortal
                    , typename PumpInIpcPortal::Interests>()(
                    *this, *static_cast<app::MessageHead*>(*it))) {
                    res++;
                }
            }
            this->batchDone_ = false;
            return res;
        }

        char const* hmbdcName() const {
            return hmbdcName_.c_str();
        }

        void messageDispatchingStartedCb(std::atomic<size_t> const*p) override {
            if constexpr (domain_detail::has_messageDispatchingStartedCb<ThreadCtx>::value) {
                outCtx_.messageDispatchingStartedCb(p);
            }
        };

        void invokedCb(size_t previousBatch) override {
            if constexpr (domain_detail::has_invokedCb<ThreadCtx>::value) {
                outCtx_.invokedCb(previousBatch);
            }
            bool layback = !previousBatch;
            if constexpr (has_net_send_eng) {
                layback = layback && !sendEng_->bufferedMessageCount();
            }
            if constexpr (has_net_recv_eng) {
                if (hmbdc_likely(recvEng_)) {
                    recvEng_->rotate();
                }
            }
            if constexpr (has_net_send_eng) {
                sendEng_->rotate();
            }
            if constexpr (has_net_send_eng || has_net_recv_eng) {
                layback = layback 
                    && !(hmbdc::app::utils::EpollTask::initialized() && hmbdc::app::utils::EpollTask::instance().getPollPending());
            }
            if (layback) {
                if (pumpMaxBlockingTimeUs_) {
                    usleep(pumpMaxBlockingTimeUs_);
                } else {
                    std::this_thread::yield();
                }
            }
        }

        template <app::MessageC Message>
        void handleMessageCb(Message& m) {
            Message& msg = m;
            using hasMemoryAttachment = app::hasMemoryAttachment;
            if constexpr (has_fromHmbdcIpcable<Message>::value) {
                static_assert(std::is_same<Message
                    , decltype(msg.fromHmbdcIpcable().toHmbdcIpcable())>::value
                    , "mising or wrong toHmbdcIpcable() func - cannot serialize");
                outCtx_.send(msg.fromHmbdcIpcable());
            } else {
                outCtx_.send(msg);
            }
            if constexpr(std::is_base_of<hasMemoryAttachment, Message>::value) {
                msg.hasMemoryAttachment::attachment = nullptr;
                msg.hasMemoryAttachment::len = 0;
            }
        }

        void handleJustBytesCb(uint16_t tag, uint8_t const* bytes, app::hasMemoryAttachment* att) {
            outCtx_.sendJustBytesInPlace(tag, bytes, ipcTransport_.maxMessageSize(), att);
            if (att) {
                att->attachment = nullptr;
                att->len = 0;
            }
        }

        void stoppedCb(std::exception const& e) override {
            if constexpr (domain_detail::has_stoppedCb<ThreadCtx>::value) {
                outCtx_.stoppedCb(e);
            }
        }

        bool droppedCb() override {
            if constexpr (domain_detail::has_droppedCb<ThreadCtx>::value) {
                return outCtx_.droppedCb();
            }
            inboundSubscriptions_.exportTo([this](uint16_t tag, uint8_t) {
                pOutboundSubscriptions_->sub(tag);
            });
            return true;
        };

        void stop() {
            if constexpr (has_net_send_eng) {
                sendEng_->stop();
            }
        }
    }; //end of Pumps

    app::Config config_;
    using Pump = std::conditional_t<
        run_pump_in_ipc_portal
        , PumpInIpcPortal
        , std::conditional_t<
            run_pump_in_thread_ctx
            , PumpInThreadCtx
            , void*
        >
    >;
    std::deque<Pump> pumps_;

    bool ownIpcTransport_ = false;
    
public:
    /**
     * @brief Get a specific Pump Configs
     * 
     * @param index the pump index
     * @return app::Config 
     */
    app::Config getPumpConfig(auto index) const {
        app::Config res = config_;
        for (auto const& pc : config_.getChildExt("pumpConfigs")) {
            if (index--) continue;
            for (auto const& setting : pc.second) {
                res.push_back(setting);
            }
        }
        return res;
    }

    /**
     * @brief Construct a new Domain object
     * 
     * @tparam NodeContextCtorArgs Args for NodeContext's ctor
     * @param cfg configuration for Domain
     * @param nodeCtxArgs leave it empty unless user customizes NodeContext
     * then it is decided by the passed in NodeContext type
     */
    template <typename ...NodeContextCtorArgs>
    Domain(app::Config const& cfg, NodeContextCtorArgs&& ...nodeCtxArgs)
    : threadCtx_{std::forward<NodeContextCtorArgs>(nodeCtxArgs)...}
    , config_(cfg) {
        config_.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));
        auto pumpRunMode = config_.getExt<std::string>("pumpRunMode");
        if constexpr (run_pump_in_ipc_portal) {
            auto ownershipStr = config_.getExt<std::string>("ipcTransportOwnership");
            int ownership; 
            if (ownershipStr == "own") {
                ownership = 1;
            } else if (ownershipStr == "attach") {
                ownership = -1;
            } else if (ownershipStr == "optional") {
                ownership = 0;
            } else {
                HMBDC_THROW(std::out_of_range, "ipcTransportOwnership unsupported: " << ownershipStr);
            }
            ipcTransport_.emplace(ownership
                , NetProtocol::instance().getTipsDomainName(config_).c_str()
                , config_.getExt<uint32_t>("ipcMessageQueueSizePower2Num")
                , 0 //no pool
                , IpcTransport::MAX_MESSAGE_SIZE != 0
                    ? IpcTransport::MAX_MESSAGE_SIZE
                    : config_.getExt<size_t>("ipcMaxMessageSizeRuntime")                
                , 0xfffffffffffffffful
                , 0
                , config_.getExt<size_t>("ipcShmForAttPoolSize")
            );
            ownIpcTransport_ = ownership > 0;
            if constexpr (IpcProperty::use_dev_mem) {
                allocator_.emplace(NetProtocol::instance().getTipsDomainName(config_).c_str()
                , ipcTransport_->footprint()
                , sizeof(*pOutboundSubscriptions_)
                , ownership);
            } else {
                allocator_.emplace((NetProtocol::instance().getTipsDomainName(config_) + "-ipcsubs").c_str()
                    , 0
                    , sizeof(*pOutboundSubscriptions_)
                    , ownership);
            }
            pOutboundSubscriptions_ = allocator_->template allocate<TypeTagSet>(SMP_CACHE_BYTES);

            ipcTransport_->setSecondsBetweenPurge(
                config_.getExt<uint32_t>("ipcPurgeIntervalSeconds"));
            
            auto pumpCount = (uint16_t)config_.getChildExt("pumpConfigs").size();
            if (pumpCount > 64) {
                HMBDC_THROW(std::out_of_range, "pumpCount > 64 is not suppported");
            }

            for (auto i = 0u; i < pumpCount; ++i) {
                auto config = getPumpConfig(i);
                auto& pump = pumps_.emplace_back(*ipcTransport_, pOutboundSubscriptions_, threadCtx_, config, pumpCount, i);
                if (pumpRunMode == "manual") {
                    ipcTransport_->registerToRun(pump
                        , config_.getHex<uint64_t>("pumpCpuAffinityHex"));
                } else if (pumpRunMode == "delayed") {
                } else {
                    HMBDC_THROW(std::out_of_range, "pumpRunMode=" << pumpRunMode << " not supported");
                }
            }
        } else if constexpr (run_pump_in_thread_ctx) {
            auto pumpCount = (uint16_t)config_.getChildExt("pumpConfigs").size();
            for (auto i = 0u; i < pumpCount; ++i) {
                auto config = getPumpConfig(i);
                auto& pump = pumps_.emplace_back(threadCtx_, config, pumpCount, i);
                if (pumpRunMode == "manual") {
                    pump.handleInCtx = threadCtx_.registerToRun(pump, 0, 0);
                } else if (pumpRunMode == "delayed") {
                } else {
                    HMBDC_THROW(std::out_of_range, "pumpRunMode=" << pumpRunMode << " not supported");
                }
            }
        } // else no pump needed
    }

    /**
     * @brief Get the Config object that is in effect
     * 
     * @return app::Config const& reference to the config
     */
    app::Config const& getConfig() const {
        return config_;
    }
    
    /**
     * @brief get the default configuration
     * 
     * @param section nullptr, "tx", "rx"
     * network configuration have fallback, tx and rx sections
     * @return app::Config
     */
    static app::Config getDftConfig(char const* section = "") {
        auto res = app::Config{DefaultUserConfig};
        app::Config netCfg{NetProperty::protocol::dftConfig(), section};
        res.setAdditionalFallbackConfig(netCfg);
        return res;
    }

    /**
     * @brief manually drive the domain's pumps one time (vs startPumping
     *  that drives pump in dedicated threads)
     * 
     * @tparam Args the arguments used for pump
     * @param pumpIndex which pump - see pumpCount configure
     * @param args the arguments used for pump, if pump is default type:
     *  - if IPC is enabled:
     *      ignored
     *  - else
     *      optional, time::Duration maxBlockingTime
     * 
     * @return true if pump runs fine
     * @return false either Domain stopped or exception thrown
     */
    template <typename... Args>
    bool pumpOnce(size_t pumpIndex = 0, Args&& ...args) {
        auto& pump = pumps_[pumpIndex];
        if constexpr (run_pump_in_ipc_portal) {     
            return ipcTransport_->runOnce(pump);
        } else if constexpr (run_pump_in_thread_ctx) {
            return threadCtx_.runOnce(pump.handle, pump, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief configure the subscriptions and advertisement for a Node
     * @details typically this is automatically done when the Node is started
     * unless the Node wants to do this step at an earlier or later stage, this could be
     * manually called at that time. Threadsafe call.
     * 
     * @tparam CcNode the Node type
     * @param node the Node
     */
    template <typename CcNode>
    void addPubSubFor(CcNode& node) {
        node.updateSubscription();
        static_assert(hmbdc::is_subset<typename CcNode::RecvMessageTuple, RecvMessageTuple>::value
            , "the node expecting messages Domain not covering");
        if constexpr ((run_pump_in_ipc_portal || run_pump_in_thread_ctx)) {
            for (uint16_t i = 0u; i < pumps_.size(); ++i) {
                pumps_[i].template subscribeFor(node);
                pumps_[i].template advertiseFor(node);
            }
        }
    }

    /**
     * @brief depricated - use add() instead and following by startPumping()
     * 
     */
    template <typename Node>
    void start(Node& nodeIn
        , size_t capacity = 1024
        , time::Duration maxBlockingTime = time::Duration::seconds(1)
        , uint64_t cpuAffinity = 0
    ) {
        static_assert(!sizeof(Node)
            , R"(please replace start() with add() to start a Node in the Domain.
                When all Nodes are added into the Domain, call startPumping())");
    }

    /**
     * @brief add a Node within this Domain as a thread - handles its subscribing here too
     * @details It is a good practice to add all Nodes into the Domain then followed by startPumping()
     * in one thread (main thread) to ensure there is no race condition between messages
     * arriving and Node starting
     * @tparam Node a concrete Node type that send and/or recv Messages
     * @param nodeIn the instance of the node - the Domain does not manage the object lifespan
     * @param capacity the inbound message buffer depth - if the buffer is full the delivery mechanism 
     * is blocked until the buffer becomes available
     * @param maxBlockingTime The node wakes up periodically even there is no messages for it
     * so its thread can respond to Domain status change - like stopping
     * @param cpuAffinity The CPU mask that he Node thread assigned to
     * @return the Domain itself
     */
    template <typename Node>
    Domain& add(Node& nodeIn
        , size_t capacity = 1024
        , time::Duration maxBlockingTime = time::Duration::seconds(1)
        , uint64_t cpuAffinity = 0
    ) {
        auto& node = * new domain_detail::NodeProxy(nodeIn);
        if (std::tuple_size<typename Node::Interests>::value
            && capacity == 0) {
                HMBDC_THROW(std::out_of_range, "capacity cannot be 0 when receiving messages");
        }
        addPubSubFor(node);
        nodeIn.setDomain(*this);
        threadCtx_.start(node, capacity, node.maxMessageSize()
            , cpuAffinity, maxBlockingTime
             , [&node](auto && ...args) {
                return node.ifDeliverCfg(std::forward<decltype(args)>(args)...);
            }
        );
        return *this;
    }

    /**
     * @brief if pumpRunMode is set to be delayed
     * this function start all the pumps
     */
    void startPumping() {
        auto pumpRunMode = config_.getExt<std::string>("pumpRunMode");
        static_assert(run_pump_in_ipc_portal || run_pump_in_thread_ctx
            , "no pump or it does not need to be started for this Domain type, do not call this");
        if (pumpRunMode == "delayed") {
            if constexpr (run_pump_in_ipc_portal) {
                for (auto& pump : pumps_) {
                    ipcTransport_->start(pump
                        , config_.getHex<uint64_t>("pumpCpuAffinityHex"));
                }
            } else if constexpr (run_pump_in_thread_ctx) {
                for (auto& pump : pumps_) {
                    threadCtx_.start(pump, 0, 0
                        , config_.getHex<uint64_t>("pumpCpuAffinityHex")
                        , config_.getHex<time::Duration>("pumpMaxBlockingTimeSec"));
                }
            }
        } else {
            HMBDC_THROW(std::logic_error, "pump was not configured as delayed");
        }
    }

     /**
     * @brief start a group of Nodes as a thread pool within the Domain that collectively processing 
     * messages in a load sharing manner. Each Node is powered by a single OS thread.
     * 
     * @tparam LoadSharingNodePtrIt iterator to a Node pointer
     * 
     * @param begin an iterator, **begin should produce a Node& for the first Node
     * @param end an end iterator, [begin, end) is the range for Nodes
     * @param capacity the maximum messages this Node can buffer
     * @param cpuAffinity cpu affinity mask for this Node's thread
     * @param maxBlockingTime it is recommended to limit the duration a Node blocks due to 
     * no messages to handle, so it can respond to things like Domain is stopped, or generate
     * heartbeats if applicable.
     */
    template <typename LoadSharingNodePtrIt
        , typename CcClientPtr = typename std::iterator_traits<LoadSharingNodePtrIt>::value_type
    >
    Domain& addPool(LoadSharingNodePtrIt begin, LoadSharingNodePtrIt end
        , size_t capacity = 1024
        , time::Duration maxBlockingTime = time::Duration::seconds(1)
        , uint64_t cpuAffinity = 0) {
            
        if (begin == end) return *this;
        if (end - begin == 1) return add(**begin, capacity, maxBlockingTime, cpuAffinity);

        using Node = typename std::decay<decltype(*std::declval<CcClientPtr>())>::type;
        auto maxItemSize = (*begin)->maxMessageSize();
        
        if (std::tuple_size<typename Node::Interests>::value
            && capacity == 0) {
                HMBDC_THROW(std::out_of_range, "capacity cannot be 0 when receiving messages");
        }

        std::vector<domain_detail::NodeProxy<Node>*> proxies;

        for (auto it = begin; it != end; it++) {
            auto& nodeIn = **it;
            auto& node = *new domain_detail::NodeProxy<Node>(nodeIn);
            nodeIn.updateSubscription();
            nodeIn.setDomain(*this);
            if (it == begin) addPubSubFor(node);
            proxies.push_back(&node);
        }

        threadCtx_.start(proxies.begin(), proxies.end(), capacity, maxItemSize, cpuAffinity, maxBlockingTime
            , [&node = **proxies.begin()](auto && ...args) {
                return node.ifDeliverCfg(std::forward<decltype(args)>(args)...);
        });
        return *this;
    }


    /**
     * @brief how many IPC parties (processes) have been detected by this context
     * including the current process
     * @return
     */
    size_t ipcPartyDetectedCount() const {
        if constexpr (run_pump_in_ipc_portal) {
            return ipcTransport_->dispatchingStartedCount();
        }
        return 0;
    }

    /**
     * @brief how many network parties (processes) are ready to send messages to this Domain
     * 
     * @return size_t 
     */
    size_t netSendingPartyDetectedCount() const {
        if constexpr (has_a_pump) {
            size_t res = 0;
            for (auto& pump : pumps_) {
                res += pump.netSendingPartyDetectedCount();
            }
            return res;
        } else {
            return 0;
        }
    }

    /**
     * @brief how many network parties (processes) are ready to receive messages from this Domain
     * 
     * @return size_t 
     */
    size_t netRecvingPartyDetectedCount() const {
        if constexpr (has_a_pump) {
            size_t res = 0;
            for (auto& pump : pumps_) {
                res += pump.netRecvingPartyDetectedCount();
            }
            return res;
        }
        return 0;
    }

    /**
     * @brief how many nodes on local machine have the message tag
     * marked as subscribed excluding Nodes managed by this Domain instance
     * 
     * @param tag 
     * @return size_t 
     */
    size_t ipcSubscribingPartyCount(uint16_t tag) const {
        return pumps_[tag % pumps_.size()].ipcSubscribingPartyCount(tag);
    }

    /**
     * @brief how many processes connecting thru network have the message tag
     * marked as subscribed
     * 
     * @param tag 
     * @return size_t 
     */
    size_t netSubscribingPartyCount(uint16_t tag) const {
        return pumps_[tag % pumps_.size()].netSubscribingPartyCount(tag);
    }

    /**
     * @brief publish a message through this Domain, all the Nodes in the TIPS domain
     * could get it if it subscribed to the Message type
     * @details Messages that are not trivially destructable are not deliverred outside
     * of this process (No IPC and network paths). They are tried to be deliverred amoung 
     * the Nodes within local process only.
     * enum of DisableSendMask could also be used to disable local, IPC or network paths.
     * 
     * @tparam Message The type that need to fit into the max_message_size specified by the
     * ipc_property and net_property. Its copy ctor is used to push it to the outgoing buffer.
     * With that in mind, the user can do partial copy using the copy ctor to implement just publish
     * the starting N bytes
     * @param m message
     */
    template <app::MessageC Message>
    void publish(Message&& m) {
        using M = typename std::decay<Message>::type;
        static_assert((int)M::typeSortIndex > (int)app::LastSystemMessage::typeTag);
        constexpr bool disableInterThread = !domain_detail::is_threadable<M>::value;
        
        if constexpr (!disableInterThread) {
            threadCtx_.send(m);
        }
        if constexpr (run_pump_in_ipc_portal || run_pump_in_thread_ctx) {
            pumps_[m.getTypeTag() % pumps_.size()].send(std::forward<Message>(m));
        }
    }

    /**
     * @brief non-blokcing version of publish - best effort publish IPC or network paths.
     * 
     * @tparam Message The type that need to fit into the max_message_size specified by the
     * ipc_property and net_property. Its copy ctor is used to push it to the outgoing buffer.
     * With that in mind, the user can do partial copy using the copy ctor to implement just publish
     * the starting N bytes
     * @param m message
     * @return true if published successfully; false if failed or partially failed (reach some recipients)
     */
    template <app::MessageC Message>
    bool tryPublish(Message&& m) {
        using M = typename std::decay<Message>::type;
        static_assert((int)M::typeSortIndex > (int)app::LastSystemMessage::typeTag);
        constexpr bool disableInterThread = !domain_detail::is_threadable<M>::value;
        
        auto res = true;
        if constexpr (!disableInterThread) {
            res = threadCtx_.trySend(m);
        }
        if constexpr (run_pump_in_ipc_portal || run_pump_in_thread_ctx) {
            res = pumps_[m.getTypeTag() % pumps_.size()].trySend(std::forward<Message>(m)) && res;
        }
        return res;
    }

    /**
     * @brief publish a message using byte format on this Domain - not recommended
     * @details Messages MUST be trivially destructable - no type based delivery features
     * such as DisableSendMask are supported.
     * It is not recommended to publish with att while the tag is also subscribed locally
     * due to the complexity of att->release() will be called for each local recipient and
     * 1 additonal time in Domain. publish via hasSharedPtrAttachment is always prefered
     * @param tag - type tag
     * @param bytes - bytes of the message contents - must match the message already 
     * constructed binary wise (hasMemoryAttachment must be considered
     * - user needs to add those leading bytes to match binary wise)
     * @param len - len of the above buffer
     * @param att - if the message type is derived from hasMemoryAttachment, explictly
     * provides the ptr here, otherwise must be nullptr
     * it is user's responsibility to make sure att->afterConsumedCleanupFunc works
     * even multiple att->release() is called
     */
    void publishJustBytes(uint16_t tag, void const* bytes, size_t len
        , app::hasMemoryAttachment* att) {
        if (tag > app::LastSystemMessage::typeTag) {
            threadCtx_.sendJustBytesInPlace(tag, bytes, len, att);
            if constexpr (run_pump_in_ipc_portal || run_pump_in_thread_ctx) {
                pumps_[tag % pumps_.size()].sendJustBytes(tag, bytes, len, att);
            }
        }
    }

    /**
     * @brief publish a sequence of message of the same type 
     * through this Domain, all the Nodes in the TIPS domain
     * could get it if it subscribed to the Message type
     * @details It is recommended to use this method if publishing an array
     * of Messages (particularly to a Node pool - see startPool() above) for performance
     * Messages that are not trivially destructable are not deliverred outside
     * of this process (No IPC and network paths). They are tried to be deliverred among
     * the Nodes within local process though
     * 
     * @tparam ForwardIt a forward iterator type - dereferenced to be a Message
     * @param begin starting iterator
     * @param n range length
     */
    template <app::MessageForwardIterC ForwardIt>
    void publish(ForwardIt begin, size_t n) {
        using M = typename std::decay<decltype(*(ForwardIt()))>::type;
        constexpr bool disableInterThread = !domain_detail::is_threadable<M>::value;
        if constexpr (!disableInterThread) {
            threadCtx_.send(begin, n);
        }
        if constexpr (run_pump_in_ipc_portal || run_pump_in_thread_ctx) {
            while (n--) {
                auto& m = *begin;
                pumps_[m.getTypeTag() % pumps_.size()]->send(*begin++);
            }
        }
    }

    /**
     * @brief allocate in shm to be hold in a hasSharedPtrAttachment to be published later
     * The release of it is auto handled in TIPS after being published
     * Will block if the shm is out of mem
     * @param actualSize the byte size of the shm segment
     * @return std::shared_ptr<uint8_t[]> 
     */
    std::shared_ptr<uint8_t[]> allocateInShmFor0cpy(size_t actualSize) {
        if constexpr (run_pump_in_ipc_portal && !IpcProperty::use_dev_mem) {
            return ipcTransport_->allocateInShm(actualSize);
        } else {
            HMBDC_THROW(std::logic_error, "Domain not support shm IPC");
        }
    }

    /**
     * @brief allocate in shm for a hasSharedPtrAttachment to be published later
     * The release of it is auto handled in TIPS
     * a stronger typed than the previous version
     * Will block if the shm is out of mem
     * 
     * @tparam Message hasSharedPtrAttachment tparam
     * @tparam T hasSharedPtrAttachment tparam - it needs to be trivial destructable
     * @tparam Args args for T's ctor
     * @param att the message holds the shared memory
     * @param actualSize T's actual size in bytes - could be > sizeof(T) for open ended struct
     * @param args args for T's ctor
     */
    template <app::MessageC Message, typename T, typename ...Args>
    void allocateInShmFor0cpy(hasSharedPtrAttachment<Message, T> & att
        , size_t actualSize, Args&& ...args) {
        auto p = ipcTransport_->allocateInShm(actualSize);
        using ELE_T = typename std::shared_ptr<T>::element_type;
        new (p.get()) ELE_T{std::forward<Args>(args)...};
        att.reset(true, std::reinterpret_pointer_cast<T>(p), actualSize);
    }

    /**
     * @brief if the Domain object owns the IPC transport
     * 
     * @return true if yes
     * @return false if no
     */
    bool ownIpcTransport() const {
        return ownIpcTransport_;
    }

    /**
     * @brief stop the Domain and its Message delivery functions
     * @details a Async operation - expected to use the join to see the effects 
     */
    void stop() {
        if constexpr (run_pump_in_ipc_portal) {
            ipcTransport_->stop();
        }
        threadCtx_.stop();

        if constexpr (run_pump_in_ipc_portal || run_pump_in_thread_ctx) {
            for (auto& pump : pumps_) {
                pump.stop();
            }
        }
    }

    /**
     * @brief wait for all the Node threads to stop
     * 
     */
    void join() {
        if constexpr (run_pump_in_ipc_portal) {
            ipcTransport_->join();
        }
        threadCtx_.join();
    }
};

}

///
/// implementation details
///
namespace app {
template <app::MessageC Message>
struct MessageWrap<tips::domain_detail::ipc_from<Message>> : MessageHead {
    template <typename ...Args>
	MessageWrap(pid_t from, Args&& ... args)
	: MessageHead(0) //delayed
	, payload(std::forward<Args>(args)...) {
        if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
            this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        }
        if constexpr (Message::hasRange) {
            this->typeTag = payload.getTypeTag();
        } else {
            this->typeTag = Message::typeTag;
        }
        this->scratchpad().ipc.hd.from = from;
	}

    MessageWrap(tips::domain_detail::ipc_from<Message> const& m)
	: MessageHead(m.getTypeTag())
	, payload(m) {
        if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
            this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        }
        this->scratchpad().ipc.hd.from = m.hmbdcIpcFrom;
	}
    Message payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap const & w) {
    	return os << static_cast<MessageHead const&>(w) << ' ' << w.payload;
    }
};

template <app::MessageC Message>
struct MessageWrap<app::InBandHasMemoryAttachment<
    tips::domain_detail::ipc_from<Message>>> : MessageHead {
    MessageWrap(tips::domain_detail::ipc_from<Message> const& m)
	: MessageHead(app::InBandHasMemoryAttachment<
        tips::domain_detail::ipc_from<Message>>::typeTag)
	, payload(m) {
        // if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
        //     this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        // } no use conflicting ipc.from and this type is wellknown not an attachment
        // 
        this->scratchpad().ipc.hd.from = m.hmbdcIpcFrom;
        this->scratchpad().ipc.hd.inbandUnderlyingTypeTag = m.getTypeTag();
	}
    app::InBandHasMemoryAttachment<Message> payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap const & w) {
    	return os << static_cast<MessageHead const&>(w) << ' ' << w.payload;
    }
};

template<>
struct MessageWrap<tips::domain_detail::ipc_from<JustBytes>> :  MessageHead {
	MessageWrap(uint16_t tag, void const* bytes, size_t len, hasMemoryAttachment* att
        , pid_t hmbdcIpcFrom)
	: MessageHead(tag)
    , payload(bytes, len) {
        if (att) {
            this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        }
        this->scratchpad().ipc.hd.from = hmbdcIpcFrom;
	}
    JustBytes payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap const & w) {
    	return os << static_cast<MessageHead const&>(w) << " *";
    }
};

template<>
struct MessageWrap<app::InBandHasMemoryAttachment<
    tips::domain_detail::ipc_from<JustBytes>>> : MessageHead {
    MessageWrap(uint16_t tag, void const* bytes, size_t len, hasMemoryAttachment* att
        , pid_t hmbdcIpcFrom)
	: MessageHead(app::InBandHasMemoryAttachment<tips::domain_detail::ipc_from<JustBytes>>::typeTag)
	, payload(bytes, len) {
        // if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
        //     this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        // } no use conflicting ipc.from and this type is wellknown not an attachment
        // 
        this->scratchpad().ipc.hd.from = hmbdcIpcFrom;
        this->scratchpad().ipc.hd.inbandUnderlyingTypeTag = tag;
	}
    app::InBandHasMemoryAttachment<JustBytes> payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap const & w) {
    	return os << static_cast<MessageHead const&>(w) << ' ' << w.payload;
    }
};

} /// app
} /// hmbdc
