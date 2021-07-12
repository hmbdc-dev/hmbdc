#if 0
#include "hmbdc/Copyright.hpp"
#pragma once 
#include "hmbdc/app/netmap/SendTransportEngine.hpp"

namespace hmbdc { namespace app { namespace netmap {

/**
 * @brief fascade class for sending network messages
 */
struct Sender {
    using ptr = std::shared_ptr<Sender>;

private:
    friend struct NetContext;
    Sender(SendTransportEngine::ptr transport, comm::Topic const& t)
    : transport_(transport)
    , topic_(t) {
    }

public:
   /**
     * @brief send a message's first bytes
     * @details the bytes length could be larger than Message, but needs to 
     * be able to fit in the tansport buffer the sender is associated 
     * with - exception thrown otherwise
     * 
     * @param msg the message to send
     * @tparam Message Type
     * @param len just send  the first len bytes of this message
     * @tparam T integral type
     */
    template <typename Message, typename T
        , typename Enabled = typename std::enable_if<std::is_integral<T>::value, void>::type>                   
    void send(Message&& msg, T len) {
        using raw = typename std::decay<Message>::type;
        static_assert(std::is_trivially_destructible<raw>::value, "cannot send message with dtor");
        transport_->queueBytes(
            topic_, raw::typeTag, &msg, static_cast<size_t>(len));
    }

    /**
     * @brief send a batch of message asynchronizely 
     * @details although batching could implicitly happen
     *  further performance gain could be achieved by sending more msg in 
     *  a batch here
     * 
     * @param msgs messages
     * @tparam Messages message types
     */
    template <typename... Messages>
    void send(Messages&&... msgs) {
        transport_->queue(topic_, std::forward<Messages>(msgs)...);
    }
    
    /**
     * @brief try to send a batch of message asynchronizely, return false if queue is full
     * @details this call does not block and is transactional - all or none is queued
     * 
     * @param msgs messages
     * @tparam Messages message types
     * @return true if messages are queued successfully
     */
    template <typename... Messages>
    bool trySend(Messages&&... msgs) {
        return transport_->tryQueue(topic_, std::forward<Messages>(msgs)...);
    }

    /**
     * @brief send a message asynchronizely - avoiding Message copying
     * by directly constructing the message in the buffer
     * 
     * @param args Message's ctor args to construct the Message in the buffer
     * @tparam Message Type
     * @tparam typename ... Args args type
     */    
    template <typename Message, typename ... Args>
    void sendInPlace(Args&&... args) {
        transport_->template queueInPlace<Message>(topic_, std::forward<Args>(args)...);
    }

    /**
     * @brief send a message asynchronizely by providing message 
     * in tag and bytes 
     * @details for runtime typed usage when  the message type can only be decided
     * at runtime
     * 
     * @param tag message tag
     * @param bytes message byte starting address
     * @param len byte length of the above
     */    
    void sendBytes(uint16_t tag, void const* bytes, size_t len) {
        transport_->queueBytes(topic_, tag, bytes, len);
    } 
private:
    SendTransportEngine::ptr transport_;
    comm::Topic topic_;
};
}}}
#endif