#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/tips/Messages.hpp"

namespace hmbdc { namespace tips { namespace udpcast {

#pragma pack(push, 1)
struct TransportMessageHeader {
    uint16_t& messagePayloadLen() {
        return *reinterpret_cast<uint16_t*>(payload());
    }

    uint16_t const& messagePayloadLen()  const {
        return *reinterpret_cast<uint16_t const*>(payload());
    }

    void const* payload() const {
        return reinterpret_cast<const char*>(this) 
            + sizeof(TransportMessageHeader); 
    }

    void * payload() {
        return reinterpret_cast<char*>(this) 
            + sizeof(TransportMessageHeader); 
    }

    uint16_t typeTag() const {
        auto h = static_cast<app::MessageHead const*>(payload());
        return h->typeTag;
    }

    template <typename Message>
    Message& wrapped() {
        auto wrap = static_cast<app::MessageWrap<Message>*>(payload());
        return wrap->payload;
    }

    template <typename Message>
    Message const& wrapped() const {
        auto wrap = static_cast<app::MessageWrap<Message> const *>(payload());
        return wrap->payload;
    }

    size_t wireSize() const {
        return sizeof(TransportMessageHeader) + messagePayloadLen();
    }    
};
#pragma pack(pop)

}}}
