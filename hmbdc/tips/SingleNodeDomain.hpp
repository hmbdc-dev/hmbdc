#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/Node.hpp"
#include "hmbdc/tips/Domain.hpp"

namespace hmbdc { namespace tips {


namespace detail {
template <typename CcNode, typename RecvMessageTuple>
struct ContextCallForwarder {
    CcNode* node = nullptr;
    template <app::MessageC Message>
    void send(Message&& message) {
        if constexpr (index_in_tuple<
            typename std::decay<Message>::type, RecvMessageTuple>::value
                != std::tuple_size<RecvMessageTuple>::value) {
            node->handleMessageCb(std::forward<Message>(message));
        }
    }
    void sendJustBytesInPlace(uint16_t tag, uint8_t* bytes, app::hasMemoryAttachment* att) {
        if constexpr (index_in_tuple<app::JustBytes, RecvMessageTuple>::value
                != std::tuple_size<RecvMessageTuple>::value) {
            node->handleJustBytesCb(tag, bytes, att);
        }
    }
    void invokedCb(size_t n) {
        if constexpr (std::is_base_of<time::TimerManager, CcNode>::value) {
            node->checkTimers(hmbdc::time::SysTime::now());
        }
        node->invokedCb(n);
    }
    
    void messageDispatchingStartedCb(size_t const*p) {node->messageDispatchingStartedCb(p);}
    void stoppedCb(std::exception const& e) {node->stoppedCb(e);}
    bool droppedCb() {return node->droppedCb();}
    void stop(){}
    void join(){}
};
}

/**
 * @brief Replace Node template with SingleNodeDomain if the application just has exact one Node
 * We call this kind of Node SingleNodeDomain. SingleNodeDomain adds Domain function into a single
 * Node. SingleNodeDomain is powered by a single pump thread. Lower latency can be achieved due to
 * one less thread hop compared to Regular Node
 * 
 * @tparam CcNode see Node documentation
 * @tparam RecvMessageTupleIn see Node documentation
 * @tparam IpcProp see Domain documentation
 * @tparam NetProp see Domain documentation
 * @tparam DefaultAttachmentAllocator see Domain documentation
 */
template <typename CcNode
    , app::MessageTupleC RecvMessageTupleIn
    , typename IpcProp
    , typename NetProp
    , typename AttachmentAllocator = DefaultAttachmentAllocator>
struct SingleNodeDomain 
: private Domain<RecvMessageTupleIn
    , IpcProp, NetProp, detail::ContextCallForwarder<CcNode, RecvMessageTupleIn>
    , AttachmentAllocator> {
public:
    /**
     * @brief Construct a new SingleNodeDomain object
     * 
     * @param cfg - used to construct Domain, see Domain documentation
     */
    SingleNodeDomain(app::Config cfg)
    : SingleNodeDomain::Domain(cfg.put("pumpRunMode", "delayed")) {
    }

    /**
     * @brief start this node - before this point, no thread is spawned and no message delievered
     *  or publish by this Node/Domain
     */
    void start(CcNode& node) {
        this->threadCtx_.node = &node;
        node.setDomain(*this);
        this->addPubSubFor(node);
        this->startDelayedPumping();
    }

    /**
     * @brief exposed from Domain - see Domain documentation
     * 
     */
    using SingleNodeDomain::Domain::runOnce;
    using SingleNodeDomain::Domain::ipcPartyDetectedCount;
    using SingleNodeDomain::Domain::netSendingPartyDetectedCount;
    using SingleNodeDomain::Domain::netRecvingPartyDetectedCount;
    using SingleNodeDomain::Domain::ipcSubscribingPartyCount;
    using SingleNodeDomain::Domain::netSubscribingPartyCount;
    using SingleNodeDomain::Domain::ownIpcTransport;
    using SingleNodeDomain::Domain::publish;
    using SingleNodeDomain::Domain::publishJustBytes;
    using SingleNodeDomain::Domain::allocateInShmFor0cpy;
    using SingleNodeDomain::Domain::stop;
    using SingleNodeDomain::Domain::join;
};

}}
