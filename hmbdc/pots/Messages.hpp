#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/tcpcast/Protocol.hpp"
#include "hmbdc/tips/Messages.hpp"
#include "hmbdc/app/Message.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/pattern/GuardedSingleton.hpp"

#include <iostream>
#include <ctype.h>


namespace hmbdc { namespace pots {
/**
 * @brief Singleton class that holds POTS message delivery resources and configurations
 * Need to be contructed as the first step of POTS use
 * 
 */
class MessageConfigurator final : public hmbdc::pattern::GuardedSingleton<MessageConfigurator> {
    friend hmbdc::pattern::SingletonGuardian<MessageConfigurator>;

    /**
     * @brief Construct a the Message Configurator object with all topics passed in
     * NOTE: Since the order is used to map topic string into message tag,
     * all topics need to be exact the same order for different parts of the participants
     * 
     * @tparam StringIterator topic iterator
     * @param topicStringBegin iterator for topics begin
     * @param topicStringEnd  iterator for topics end
     */
    template <typename StringIterator>
    MessageConfigurator(StringIterator topicStringBegin, StringIterator topicStringEnd) {
        for (auto it = topicStringBegin; it != topicStringEnd; ++it) {
            tag2Topic_.emplace_back(*it);
        }

        auto tagIndex = kPotsMsgStartTag;
        for (auto const& topic : tag2Topic_) {
            topic2Tag_[topic] = tagIndex++;
        }
    }

    public:
    static constexpr uint16_t kPotsMsgStartTag = 10001;
    static constexpr uint16_t kPotsMsgRange = 10000;
    static constexpr size_t kMaxInPlaceMessageLen = 1024 - 128;

    /**
     * @brief return the topic->tag mapping
     * 
     * @param topic 
     * @return uint16_t mapped tag
     */
    uint16_t map2Tag(std::string_view topic) const {
        return topic2Tag_.at(topic);
    }

    /**
     * @brief return the tag->topic mapping
     * 
     * @param tag
     * @return std::string_view mapped topic
     */
    std::string_view map2Topic(uint16_t tag) const {
        uint16_t offset = tag - MessageConfigurator::kPotsMsgStartTag;
        return tag2Topic_[offset];
    }

    private:
    std::vector<std::string> tag2Topic_;
    std::unordered_map<std::string_view, uint16_t> topic2Tag_;
    pattern::SingletonGuardian<tips::tcpcast::Protocol> tcpcastGuard_; //RAII for tcpcast::Protocol resources
    pattern::SingletonGuardian<app::SyncLogger> logGuard_{std::cerr};
};

/**
 * @brief the TIPS message type used in POTS
 * 
 */
struct Message
: tips::hasSharedPtrAttachment<Message, uint8_t[]>
, app::hasTag<MessageConfigurator::kPotsMsgStartTag, MessageConfigurator::kPotsMsgRange> {
    size_t inPlacePayloadLen = 0;   // n place / shorter message size - if 0, payload is in attachment
    char inPlacePayload[MessageConfigurator::kMaxInPlaceMessageLen] = {0};

    /**
     * @brief Construct a new Message object
     * 
     * @param topic topic string
     * @param msg message byte ptr
     * @param msgLen message size
     */
    Message(std::string_view topic, void const* msg, size_t msgLen)
    : hasTag(MessageConfigurator::instance().map2Tag(topic) - MessageConfigurator::kPotsMsgStartTag) {
        // use attachment for large messages
        if (msgLen <= MessageConfigurator::kMaxInPlaceMessageLen) {
            inPlacePayloadLen = msgLen;
            memcpy(inPlacePayload, msg, msgLen);
        } else {
            (hasSharedPtrAttachment&)(*this) = hasSharedPtrAttachment{SP{new uint8_t[msgLen]}, msgLen};
            memcpy(hasSharedPtrAttachment::attachmentSp.get(), msg, msgLen);
        }
    }

    /**
     * @brief Construct a new Message object
     * 
     * @param topic topic string
     * @param message r-ref message bytes start ptr - means the bytes ownership is transferred to POTS
     * with this call for efficientcy
     * @param messageLen message bytes length
     */
    Message(std::string_view topic, void*&& msg, size_t msgLen)
    : hasSharedPtrAttachment{SP{(uint8_t*)msg}, msgLen}
    , hasTag(MessageConfigurator::instance().map2Tag(topic) - MessageConfigurator::kPotsMsgStartTag) 
        {}


    /**
     * @brief Construct a new Message object
     * 
     * @param topic topic string
     * @param message message bytes shared ptr
     * @param messageLen message bytes length
     */
    Message(std::string_view topic, std::shared_ptr<uint8_t[]> msg, size_t msgLen)
    : hasSharedPtrAttachment(msg, msgLen)
    , hasTag(MessageConfigurator::instance().map2Tag(topic) - MessageConfigurator::kPotsMsgStartTag) {}
};
}}
