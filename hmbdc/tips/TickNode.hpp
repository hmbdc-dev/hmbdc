#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/Node.hpp"
#include "hmbdc/MetaUtils.hpp"
#include <utility>

namespace hmbdc::tips {

/**
 * @brief Tick is a frequently used concept in robotics, in which a Node groups a set of input messages, and when 
 * all messages in the group are received (with some condition checked - for example all messages have the close enough timestamps), 
 * the Node starts to work to process the grouped inputs (start to 'tick'). This calss supports the Tick concept in addition to Node
 * 
 * 
 * @tparam CcNode The concrete Node type
 * @tparam TickMessageComboIn The std tuple list all the received Message types to be grouped together.
 * The matching TickCb for the above types needs to be provided - othewise cannot not compile, For example:
 * void tickCb(MessageA& a, MessageB const& b){...} // for std::tuple<MessageA, MessageB>
 * Each message in the tuple list must have have default contructor
 * @tparam NontickMessageTupleIn The std tuple list addiontional received Message types.
 * Those nessages are delivered one by one like regular Node, the matching nontickCb for the these type 
 * needs to be provided for each type, so this message is handled - othewise cannot not compile. For example:
 * void nontickCb(MessageC const& m){...}
 * @tparam SendMessageTupleIn The std tuple list all the publish Message types.
 * Cannot not compile if trying to publish a message not listed here.
 * @
 */
template <typename CcNode
    , app::MessageTupleC TickMessageComboIn
    , app::MessageTupleC NontickMessageTupleIn
    , app::MessageTupleC SendMessageTupleIn
    , typename ...Traits
>
class TickNode : public Node<CcNode
    , typename merge_tuple_unique<TickMessageComboIn, NontickMessageTupleIn>::type
    , SendMessageTupleIn
    , Traits...
> {
    TickMessageComboIn tickMsgCombo_;
    time::SysTime comboTimestamps_[std::tuple_size_v<TickMessageComboIn>];
    std::function<bool (TickMessageComboIn const&, time::SysTime*, size_t)> tickPred_;
public:
    using TickMessageCombo = TickMessageComboIn;

    /**
     * @brief Construct a new Tick Node object by specifying the tick condition to be
     * the group of messages need to be received within a specific duration (normally a short duration)
     * 
     * @param comboTimeout - the time duration, see above
     */
    explicit TickNode(time::Duration comboTimeout = std::numeric_limits<time::Duration>::max()) {
        tickPred_ = [comboTimeout](TickMessageComboIn const& tickMsgCombo, time::SysTime* comboTimestamps, size_t) {
            auto minmax = std::minmax_element(comboTimestamps, comboTimestamps + std::tuple_size_v<TickMessageComboIn>);
            return *minmax.second - *minmax.first <= comboTimeout;
        };
    }

    /**
     * @brief Construct a new Tick Node object by providing the tick condition algorithm
     * 
     * @param tickPred - specify the algorithm here. return true if a tick should be fired based on:
     *  TickMessageCombo - a std::tupe holding the latest messages in the message group
     *  time::SysTime* - an array of timestamps of when the above message arriving to the Node
     *  size_t - the index (in TickMessageCombo) of the latest arriving message in the group
     */
    explicit TickNode(std::function<bool (TickMessageCombo&, time::SysTime*, size_t)> tickPred)
    : tickPred_(tickPred) {}

    /**
     * @brief implementation detail - do not change
     */
    template <app::MessageC Message>
    void handleMessageCb(Message const& msg) {
        auto constexpr index = index_in_tuple<Message, TickMessageCombo>::value;
        if constexpr(index >= std::tuple_size_v<TickMessageComboIn>) {
            static_cast<CcNode*>(this)->nontickCb(msg);
        } else {
            comboTimestamps_[index] = time::SysTime::now();
            std::get<index>(tickMsgCombo_) = msg;
            if (tickPred_(tickMsgCombo_, comboTimestamps_, index)) {
                call_member_in_arg_pack(static_cast<CcNode*>(this), &CcNode::tickCb, std::move(tickMsgCombo_));
                 /// clear out for the new tick
                tickMsgCombo_ = TickMessageCombo{};
                std::fill(std::begin(comboTimestamps_), std::end(comboTimestamps_), time::SysTime{});
            }
        }
    }
};
} // hmbdc::tips
