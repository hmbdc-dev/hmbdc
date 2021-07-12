#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/ClientWithStash.hpp"
#include "hmbdc/MetaUtils.hpp"

#include <functional>

#include <unistd.h>

namespace hmbdc { namespace tips {
template <typename... Nodes>
struct aggregate_recv_msgs {
    using type = std::tuple<>;
};

template <typename Node, typename... Nodes>
struct aggregate_recv_msgs<Node, Nodes ...> {
    using type = typename hmbdc::merge_tuple_unique<
        typename Node::RecvMessageTuple, typename aggregate_recv_msgs<Nodes ...>::type
    >::type;
};

template <typename... Nodes>
struct aggregate_send_msgs {
    using type = std::tuple<>;
};

template <typename Node, typename... Nodes>
struct aggregate_send_msgs<Node, Nodes ...> {
    using type = typename hmbdc::merge_tuple_unique<
        typename Node::SendMessageTuple, typename aggregate_send_msgs<Nodes ...>::type
    >::type;
};

/**
 * @brief a Node is a thread of execution that can suscribe and receive Messages
 * @details All messages are received through callback function. All callback functions
 * are called in this Node thread sequentially, so there is no data protection needs
 * within a Node from this perspective.
 * @tparam CcNode The concrete Node type
 * @tparam RecvMessageTuple The std tuple list all the received Message types.
 * The matching handleMessageCb for the above type needs to be provided for each type
 * so this message is handled - othewise cannot not compile. For example:
 * void handleMessageCb(MessageA const& m){...}
 * @tparam HasMessageStash - if the Node needs Message reorderring support
 * See ClientWithStash.hpp
 */
template <typename CcNode, app::MessageTupleC RecvMessageTuple, bool HasMessageStash = false>
struct Node;

template <typename CcNode, app::MessageC ...RecvMessages, bool HasMessageStash>
struct Node<CcNode, std::tuple<RecvMessages...>, HasMessageStash>
: std::conditional<HasMessageStash
    , app::ClientWithStash<CcNode, 0, RecvMessages...> 
    , app::Client<CcNode, RecvMessages...>
>::type {
    enum {
        manual_subscribe = false, /// if defined as true in CcNode, auto subscribe is 
                                  /// not happening - User manually call
                                  /// Domain::subscribeFor() at the right time
    };
    /**
     * @brief Concrete Node needs to specify a list of Messages that would be sent out
     * if this Node is active to help IPC and network delivering filtering.
     * If not fully specified the message could be invisible outside of this process.
     */
    using SendMessageTuple = std::tuple<>;
    using RecvMessageTuple = typename app::Client<CcNode, RecvMessages...>::Interests;

    /**
     * @brief the thread name used to identify the thread the Node is running on
     *  - only for display purpose
     * 
     * @return char const* a string - less than 8 char typically
     */
    char const* hmbdcName() const {return "node";}

    /**
     * @brief unless JustBytes is used in Interests - this is automatic
     * 
     * @return size_t what is the biggest message this Node is to receive
     */
    size_t maxMessageSize() const {
        using I = typename CcNode::Interests;
        static_assert(index_in_tuple<app::JustBytes, I>::value == std::tuple_size<I>::value
            , "need to override this function since JustBytes is used");
        return max_size_in_tuple<typename CcNode::Interests>::value;
    }

    /**
     * @brief What tags are going to published when JustBytes is in the RecvMessageTuple
     * 
     * @param addTag functor used to addd a tag value (NOT tag offset)
     */
    void addJustBytesSubsFor(std::function<void(uint16_t)> addTag) const {
        using I = typename CcNode::Interests;
        static_assert(index_in_tuple<app::JustBytes, I>::value == std::tuple_size<I>::value
            , "need to override this function since JustBytes is used");
        // addTag(aTag);
    }

    /**
     * @brief What tags are going to published when JustBytes is in the SendMessageTuple
     * 
     * @param addTag functor used to addd a tag value (NOT tag offset)
     */
    void addJustBytesPubsFor(std::function<void(uint16_t)> addTag) const {
        using I = typename CcNode::Interests;
        static_assert(index_in_tuple<app::JustBytes, I>::value == std::tuple_size<I>::value
            , "need to override this function since JustBytes is used");
        // addTag(aTag);
    }

    /**
     * @brief The concrete node overrides this function for each inTagRange type of Message
     * to indicate which type tags in the range this Node subscribes for - otherwise each one
     * in range is subscribed
     * 
     * @tparam Message the inTagRange Message
     * @param addOffsetInRange a functor that adds a type tag into the subscription
     * Note - the functor does not take a type tag - instead it takes the tag offset
     * within the range
     */
    template <app::MessageC Message>
    void addTypeTagRangeSubsFor(Message*, std::function<void(uint16_t)> addOffsetInRange) const {
        for (auto i = 0; i < Message::typeTagRange; ++i) {
            addOffsetInRange(i);
        }
    }

    /**
     * @brief The concrete node overrides this function for each inTagRange type of Message
     * to indicate which type tags in the range this Node subscribes for - otherwise each one
     * in range is expected to be published
     * 
     * @tparam Message the inTagRange Message
     * @param addOffsetInRange a functor that adds a type tag into the subscription
     * Note - the functor does not take a type tag - instead it takes the tag offset
     * within the range
     */
    template <app::MessageC Message>
    void addTypeTagRangePubsFor(Message*, std::function<void(uint16_t)> addOffsetInRange) const {
        for (auto i = 0; i < Message::typeTagRange; ++i) {
            addOffsetInRange(i);
        }
    }

    /**
     * @brief Domain calls this first before handles the subscritions for this Node
     * 
     */
    void updateSubscription() {
        auto& node = *static_cast<CcNode*>(this);
        subscription.markSubsFor<typename CcNode::Interests>(node, 1, 0);
    }

    /**
     * @brief an overridable method in the concrete Node that 
     * determine if a message should be deliverred to this Node instance
     * This is called at the sending part before the Node is unblocked
     * due to this delivery
     * 
     * @tparam Message decision is for this Message type
     * @param message The decision on this Message instance
     * @return true if proceed to deliever it
     * @return false if not
     */
    template<app::MessageC Message>
    bool ifDeliver(Message const& message) const {
        if constexpr(!Message::hasRange) {
            return true;
        } else {
            return subscription.check(message.getTypeTag());
        }
    }

    /**
     * @brief an overridable method in the concrete Node that 
     * determine if a message should be deliverred to this Node instance's
     * JustBytes Interests
     * This is called at the sending part before the Node is unblocked
     * due to this delivery
     * 
     * @tparam Message decision is for this Message type
     * @param tag message tag 
     * @param bytes message address
     * @return true if proceed to deliever it
     * @return false if not
     */
    bool ifDeliver(uint16_t tag, uint8_t const* bytes) const {
        return subscription.check(tag);
    }

    /**
     * @brief this callback is called all the time (frequently) - the exact timing is
     * after a batch of messages are dispatched. After this call returns, the previously 
     * dispatched message's addresses are no longer valid, which means if you cache the 
     * event addresses in the previous handleMessageCb()s, you cannot use those after 
     * the return of the next invokeCb function.
     * @details you can collectively process the messages received/cached so far here, or 
     * do something needs to be done all the time like powering another message loop
     * 
     * @param dispatched the number of messages dispatched since last invokedCb called
     */
    void invokedCb(size_t dispatched) override {
        // called as frequently as workload allowed
    }

protected:
    TypeTagSet subscription;
};

template <typename SendMessageTupleIn, typename RecvMessageTupleIn>
struct RegistrationNode
: Node<RegistrationNode<SendMessageTupleIn, RecvMessageTupleIn>, RecvMessageTupleIn> {
    using SendMessageTuple = SendMessageTupleIn;
};
}}
