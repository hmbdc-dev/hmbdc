#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/pots/Messages.hpp"
#include "hmbdc/tips/Node.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/time/Time.hpp"

namespace hmbdc { namespace pots {
/**
 * @brief a pots Node is built on top of tips::Node, is also a thread of execution that can suscribe 
 * and receive Messages
 * @details It is a collection of callback functions "xxxCb".
 * All messages are received through "Cb" callback functions. All "Cb"callback functions
 * are called in this Node's thread sequentially, so there is no data protection needs
 * within a Node from this perspective.
 * 
 * @tparam CcNode The concrete node type
 */
template <typename CcNode>
class Node 
: public tips::Node<CcNode, std::tuple<Message>, std::tuple<Message>>
, public time::TimerManager {
    using Base = tips::Node<CcNode, std::tuple<Message>, std::tuple<Message>>;
    std::unordered_set<uint16_t> subs_;
    std::unordered_set<uint16_t> pubs_;

    public:
    /**
     * @brief Construct a new Node object by declaring the pub and sub interests
     * 
     * @param subs a vector of subscribed topis strings
     * @param pubs a vector of publish topics strings
     */
    Node(std::vector<std::string_view> const& subs, std::vector<std::string_view> const& pubs)
    : Node{subs.begin(), subs.end(), pubs.begin(), pubs.end()}
    {}

    /**
     * @brief Construct a new Node object by declaring the pub and sub interests
     * 
     * @tparam TopicIterator interator type to topics
     * @param subBegin begin iterator of subscribed topics
     * @param subEnd end iterator of subscribed topics
     * @param pubBegin begin iterator of published topics
     * @param pubEnd end iterator of subscribed topics
     */
    template <typename TopicIterator>
    Node(TopicIterator subBegin, TopicIterator subEnd
        , TopicIterator pubBegin, TopicIterator pubEnd)
    : Base{} {
        auto & cfg = MessageConfigurator::instance();
        for (auto it = subBegin; it != subEnd; ++it) {
            subs_.insert(cfg.map2Tag(*it));
        }

        for (auto it = pubBegin; it != pubEnd; ++it) {
            pubs_.insert(cfg.map2Tag(*it));
        }
    }

    /**
     * @brief unimplemented callback for when a msg is received
     * 
     * @param topic topic of the message
     * @param msg msg bytes ptr
     * @param msgLen msg length
     */
    virtual void potsCb(std::string_view topic, void const* msg, size_t msgLen) = 0;
    /**
     * @brief publish some bytes (a message) on a topic
     * 
     * @param topic topic string
     * @param message message bytes start ptr
     * @param messageLen message bytes length
     */
    void publish(std::string_view topic, void const* message, size_t messageLen) {
        auto toPub = Message{topic, message, messageLen};
        Base::publish(toPub);
    }

    /**
     * @brief publish some bytes (a message) on a topic
     * 
     * @param topic topic string
     * @param message r-ref message bytes start ptr - means the bytes ownership is transferred to POTS
     * with this call for efficientcy
     * @param messageLen message bytes length
     */
    void publish(std::string_view topic, void*&& message, size_t messageLen) {
        auto toPub = Message{topic, std::move(message), messageLen};
        Base::publish(toPub);
    }

    /**
     * @brief publish some bytes (a message) on a topic
     * 
     * @param topic topic string
     * @param message message bytes shared ptr
     * @param messageLen message bytes length
     */
    void publish(std::string_view topic, std::shared_ptr<uint8_t[]> message, size_t messageLen) {
        auto toPub = Message{topic, message, messageLen};
        Base::publish(toPub);
    }

    /**
     * @brief overridable, the thread name used to identify the thread the Node is running on
     *  - only for display purpose
     * 
     * @return char const* a string - less than 8 char typically
     */
    char const* hmbdcName() const {return "potsnode";}

    /**
     * @brief one time callback when the node starts runs in its own thread
     * @param ignore
     */
    virtual void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
        auto& ccNode = static_cast<CcNode&>(*this);
        HMBDC_LOG_N(ccNode.hmbdcName(), " starting");
    }

    /**
     * @brief callback when Node's running has thrown an exception
     * @param exception
     */
    virtual void stoppedCb(std::exception const& e) override {
        auto& ccNode = static_cast<CcNode&>(*this);
        HMBDC_LOG_N(ccNode.hmbdcName(), " stopped, reason: ", e.what());
    }

    /**
     * @brief callback when the node is about to be dropped from its domain - no more messaging function
     * 
     * @return true - if let it happen
     * @return false - if we do not want it dropped
     */
    virtual bool droppedCb() override {return true;}

    /**
     * @brief this callback is called all the time (frequently) - the exact timing is
     * after a batch of messages are dispatched or max blocking timeouts (1 sec by default).
     * After this call returns, the previously dispatched message's addresses are 
     * no longer valid, which means if you cache the 
     * event addresses in the previous potsCb()s, you cannot use those after 
     * the return of the next invokeCb function.
     * @details you can collectively process the messages received/cached so far here, or 
     * do something needs to be done all the time like powering another message loop
     * 
     * @param dispatched the number of messages dispatched since last invokedCb called
     */
    virtual void invokedCb(size_t dispatched) override {}

    /**
     * @brief functions below are impl details - do not change
     */

    void handleMessageCb(Message const& msg) {
        auto& ccNode = static_cast<CcNode&>(*this);
        if (msg.inPlacePayloadLen) {
            ccNode.potsCb(MessageConfigurator::instance().map2Topic(msg.getTypeTag())
                , msg.inPlacePayload, msg.inPlacePayloadLen);
        } else {
            ccNode.potsCb(MessageConfigurator::instance().map2Topic(msg.getTypeTag())
                , msg.hasSharedPtrAttachment::attachmentSp.get()
                , msg.hasSharedPtrAttachment::len);
        }
    }
    void addTypeTagRangeSubsForCfg(Message*, std::function<void(uint16_t)> addOffsetInRange) const {
        for (auto tag : subs_) {
            addOffsetInRange(tag - MessageConfigurator::kPotsMsgStartTag);
        }
    }
    void addTypeTagRangePubsForCfg(Message*, std::function<void(uint16_t)> addOffsetInRange) const {
        for (auto tag : pubs_) {
            addOffsetInRange(tag - MessageConfigurator::kPotsMsgStartTag);
        }
    }
};
}}
