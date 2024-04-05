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
        if constexpr (is_in_tuple_v<typename std::decay<Message>::type, RecvMessageTuple>) {
            node->handleMessageCb(std::forward<Message>(message));
        }
    }

    template <app::MessageC Message>
    bool trySend(Message&& message) {
        if constexpr (is_in_tuple_v<typename std::decay<Message>::type, RecvMessageTuple>) {
            node->handleMessageCb(std::forward<Message>(message));
        }
        return true;
    }

    void sendJustBytesInPlace(uint16_t tag, void const* bytes, size_t, app::hasMemoryAttachment* att) {
        if constexpr (is_in_tuple_v<app::JustBytes, RecvMessageTuple>) {
            node->handleJustBytesCb(tag, (uint8_t const*)bytes, att);
        }
    }
    void invokedCb(size_t n) {
        if constexpr (std::is_base_of<time::TimerManager, CcNode>::value) {
            node->checkTimers(hmbdc::time::SysTime::now());
        }
        node->invokedCb(n);
    }
    
    void messageDispatchingStartedCb(std::atomic<size_t> const*p) {node->messageDispatchingStartedCb(p);}
    void stoppedCb(std::exception const& e) {node->stoppedCb(e);}
    bool droppedCb() {return node->droppedCb();}

    // the follwoing are only used when NoIpc
    // forward all to
    using CtxUsedWhenNoIpc = app::BlockingContext<>;
    CtxUsedWhenNoIpc ctxUsedWhenNoIpc_;
    using ClientRegisterHandle = typename CtxUsedWhenNoIpc::ClientRegisterHandle;
    template <typename ...Args>
    ClientRegisterHandle registerToRun(Args&& ... args) {return ctxUsedWhenNoIpc_.registerToRun(std::forward<Args>(args)...);}
    template <typename ...Args>
    void start(Args&& ... args) {ctxUsedWhenNoIpc_.start(std::forward<Args>(args)...);}
    void stop(){ctxUsedWhenNoIpc_.stop();}
    void join(){ctxUsedWhenNoIpc_.join();}
};
}

/**
 * @brief Replace Domain template with SingleNodeDomain if the Domain object holds exact one Node
 * We call this kind of Domain SingleNodeDomain. SingleNodeDomain is powered by a single pump thread.
 * Lower latency can be achieved due to one less thread hop compared to Regular Node
 * 
 * @tparam CcNode the Node type the Domain is to hold
 * @tparam RecvMessageTupleIn see Domain documentation
 * @tparam IpcProp see Domain documentation
 * @tparam NetProp see Domain documentation
 * @tparam DefaultAttachmentAllocator see Domain documentation
 */
template <typename CcNode
    , typename IpcProp
    , typename NetProp
    , typename AttachmentAllocator = DefaultAttachmentAllocator>
struct SingleNodeDomain 
: private Domain<typename CcNode::RecvMessageTuple
    , IpcProp, NetProp, detail::ContextCallForwarder<CcNode, typename CcNode::RecvMessageTuple>
    , AttachmentAllocator> {
public:
    /**
     * @brief Construct a new SingleNodeDomain object
     * 
     * @param cfg - used to construct Domain, see Domain documentation
     */
    SingleNodeDomain(app::Config cfg)
    : SingleNodeDomain::Domain(cfg) {
    }

/**
     * @brief add THE Node within this Domain as a thread - handles its subscribing here too
     * @details should only call this once since it is a SingleNodeDomain
     * @tparam Node a concrete Node type that send and/or recv Messages
     * @param node the instance of the node - the Domain does not manage the object lifespan
     * @return the SingleNodeDomain itself
     */
    SingleNodeDomain& add(CcNode& node) {
        if (this->threadCtx_.node) {
            HMBDC_THROW(std::logic_error, "previously added a Node")
        }
        this->threadCtx_.node = &node;
        node.setDomain(*this);
        this->addPubSubFor(node);
        return *this;
    }

    /**
     * @brief exposed from Domain - see Domain documentation
     * 
     */
    using SingleNodeDomain::Domain::getConfig;
    using SingleNodeDomain::Domain::getDftConfig;
    using SingleNodeDomain::Domain::startPumping;
    using SingleNodeDomain::Domain::pumpOnce;
    using SingleNodeDomain::Domain::addPubSubFor;
    using SingleNodeDomain::Domain::ipcPartyDetectedCount;
    using SingleNodeDomain::Domain::netSendingPartyDetectedCount;
    using SingleNodeDomain::Domain::netRecvingPartyDetectedCount;
    using SingleNodeDomain::Domain::ipcSubscribingPartyCount;
    using SingleNodeDomain::Domain::netSubscribingPartyCount;
    using SingleNodeDomain::Domain::ownIpcTransport;
    using SingleNodeDomain::Domain::publish;
    using SingleNodeDomain::Domain::tryPublish;
    using SingleNodeDomain::Domain::publishJustBytes;
    using SingleNodeDomain::Domain::allocateInShmFor0cpy;
    using SingleNodeDomain::Domain::stop;
    using SingleNodeDomain::Domain::join;
};

}}
