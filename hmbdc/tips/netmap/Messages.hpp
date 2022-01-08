#include "hmbdc/Copyright.hpp"
#pragma once 
#include "hmbdc/tips/Messages.hpp"

namespace hmbdc { namespace tips { namespace netmap {

struct TransportMessageHeader {
    uint16_t& messagePayloadLen() {
        return *reinterpret_cast<uint16_t*>(payload());
    }
    uint16_t const& messagePayloadLen()  const {
        return *reinterpret_cast<uint16_t const*>(payload());
    }

    //copy message into the right place in memory
    template <typename M>
    static TransportMessageHeader*
    copyTo(void* addrIn, M&& m) {
        using Message = typename std::decay<M>::type;
        auto addr = (char*)addrIn;
        auto h = reinterpret_cast<TransportMessageHeader*>(addr);
        new (addr + sizeof(TransportMessageHeader)) 
            app::MessageWrap<Message>(std::forward<M>(m));
        h->messagePayloadLen() = sizeof(app::MessageWrap<Message>);
        return h;
    }

    static TransportMessageHeader*
    copyTo(void* addrIn, uint16_t tag, void const* bytes, size_t len) {
        auto addr = (char*)addrIn;
        auto h = reinterpret_cast<TransportMessageHeader*>(addr);
        new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<app::JustBytes>(tag, bytes, len, nullptr);
        h->messagePayloadLen() = sizeof(app::MessageWrap<app::JustBytes>) 
            - sizeof(app::MessageWrap<app::JustBytes>::payload) + len;
        return h;
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

    template <typename Message>
    static 
    size_t wireSize() {
        return sizeof(TransportMessageHeader) + sizeof(app::MessageWrap<Message>);
    }
} __attribute__((packed));
}}}
