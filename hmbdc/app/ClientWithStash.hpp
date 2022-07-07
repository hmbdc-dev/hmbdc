#include "hmbdc/Copyright.hpp"
#pragma once
 
#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/Message.hpp"

#include <deque>

namespace hmbdc { namespace app {
/**
 * @class ClientWithStash<>
 * @brief It behaves like Client, with the extra capability of storing messages received and processing
 *  them later - it is used when the user wants to process messages in a specific order other than 
 *  FIFO. If a message is deemed to come too early, just stash it and move on (to 
 *  the next message). The stashed messages will be delivered later at the user's choice - see the added 
 *  stash and openStash functions. The stash mechanism can enforce any particular order of message
 *  processing regarless of the order of messages being received.
 * 
 * @tparam CcClient the concrete Client type
 * @tparam MaxStashedMessageSizeIn byte size of the max message that will need to be stashed,
 * use 0 to auto-set to hold the max interested message
 * @tparam typename ... Messages message types that the Client interested
 */
template <typename CcClient, size_t MaxStashedMessageSizeIn, MessageC ... Messages>
class ClientWithStash
: public Client<CcClient, Messages...> {
public:    
    using Base = Client<CcClient, Messages...>;
    using Interests = typename Base::Interests;

    enum {
        MaxStashedMessageSize = MaxStashedMessageSizeIn
            ?MaxStashedMessageSizeIn
            :max_size_in_tuple<Interests>::value,
    };

protected:
/**
 * @brief stash a message that will be delivered later to the same callback
 * @details when the Client decide a message is not ready to be handled now but is needed later,
 * it calls this message to stash it, see also openStash()
 * 
 * @param message the message to be stashed
 */
    template <MessageC Message>
    void stash(Message const& message) {
        static_assert(is_in_tuple_v<Message, std::tuple<Messages...>>
            , "cannot stash uninterested message");
        stash_.push_back(typename decltype(stash_)::value_type{});
        new (stash_.back().data()) MessageWrap<Message>(message);
    }


/**
 * @brief release the stashed messages - after this call is returned, the callbacks of
 * the stashed message will be invoked with the stashed messages until the 'currently'
 * stashed messages runs out then the normal new message dispatching resumes
 * @details it is fine to stash more / new messages even in the above callbacks invoked
 * due to stashed messages, those newly stahed message will be released after the next
 * opeStash call though
 */
    void openStash() {
        stashOpenCount_ = stash_.size();
    }

private:
    void consumeStash() {
        CcClient& c = static_cast<CcClient&>(*this);
        while (hmbdc_unlikely(stashOpenCount_)) {
            auto m = (MessageHead*)stash_.front().data();
            MessageDispacher<CcClient, Interests>()(c, *m);
            stash_.pop_front();
            stashOpenCount_--;
        }
    }
    size_t stashOpenCount_ = 0;
    std::deque<std::array<char, MaxStashedMessageSize + sizeof(MessageHead)>> stash_;

public:
/**
 * @brief do not touch - internal use
 */
    template <typename Iterator>
    size_t handleRangeImpl(Iterator it, Iterator end, uint16_t threadId) {
        auto res = Base::handleRangeImpl(it, end, threadId);
        consumeStash();
        return res;
    }
};

/**
 * if you do not have Message paramter pack, just have tuple
 */
template <typename CcClient, size_t MaxStashedMessageSizeIn, typename MessageTuple>
struct client_with_stash_using_tuple;
template <typename CcClient, size_t MaxStashedMessageSizeIn, MessageC ... Messages>
struct client_with_stash_using_tuple<CcClient, MaxStashedMessageSizeIn, std::tuple<Messages...>> {
    using type = ClientWithStash<CcClient, MaxStashedMessageSizeIn, Messages...>;
};
}} //hmbdc::app
