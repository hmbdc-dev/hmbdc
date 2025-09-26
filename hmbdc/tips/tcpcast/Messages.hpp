#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/comm/inet/Endpoint.hpp"
#include "hmbdc/app/Message.hpp"
#include "hmbdc/Endian.hpp"

#include <iostream>
#include <string>

#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>

namespace hmbdc { namespace tips { namespace tcpcast {

HMBDC_CLASS_HAS_DECLARE(hmbdc_net_queued_ts);
#pragma pack(push, 1)
struct TransportMessageHeader {
    uint8_t flag; //1 - hasAttachment
    XmitEndian<uint16_t> messagePayloadLen;

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
        return sizeof(TransportMessageHeader) + messagePayloadLen;
    }    

    size_t wireSizeContainsTRansportHeader() const {
        return sizeof(TransportMessageHeader);
    }
};

template <typename Message>
struct NetWrap {
    template <typename ...Args>
    NetWrap(Args&&... args)
    : wrappedMsg{std::forward<Args>(args)...} {
        header.messagePayloadLen = sizeof(app::MessageWrap<Message>);
        header.flag = wrappedMsg.scratchpad().desc.flag;

        if constexpr (has_hmbdc_net_queued_ts<Message>::value) {
            wrappedMsg.hmbdc_net_queued_ts = hmbdc::time::SysTime::now();
        }
    }

    TransportMessageHeader header;
    app::MessageWrap<Message> wrappedMsg;
};

template <>
struct NetWrap<app::JustBytes> {
    NetWrap(uint16_t tag, void const* bytes, size_t len, app::hasMemoryAttachment* att)
    : wrappedMsg{tag, bytes, len, att} {
        header.messagePayloadLen = len + sizeof(app::MessageHead);
        header.flag = wrappedMsg.scratchpad().desc.flag;
    }

    TransportMessageHeader header;
    app::MessageWrap<app::JustBytes> wrappedMsg;
};

#pragma pack(pop)

#pragma pack(push, 1)
struct TypeTagSource
: app::hasTag<250> {
    TypeTagSource()
    : port(0)
    , srcPid(0)
    , loopback(false)
    {}

    TypeTagSource(std::string const& ipIn
        , uint16_t portIn
        , bool loopbackIn)
    : port(portIn)
    , srcPid(getpid())
    , loopback(loopbackIn) {
        snprintf(ip, sizeof(ip), "%s", ipIn.c_str());
    }
    char ip[16];
    XmitEndian<uint16_t> typeTagCountContained{0};
    XmitEndian<uint16_t> typeTags[64];
    XmitEndian<uint16_t> port;
    XmitEndian<uint32_t> srcPid;
    bool loopback;

    bool addTypeTag(uint16_t tag) {
        auto c = uint16_t{typeTagCountContained};
        if (c < 64) {
            typeTags[c++] = tag;
            typeTagCountContained = c;
            return true;
        }
        return false;
    }

    friend
    std::ostream& operator << (std::ostream& os, TypeTagSource const& m) {
        os << "TypeTagSource:" << m.ip << ':' << m.port;
        for (auto i = 0u; i < m.typeTagCountContained; i++) {
            os << ' ' << m.typeTags[i];
        }

        return os;
    }
};
#pragma pack(pop)

#pragma pack(push, 1)
struct UdpcastListenedAt
: app::hasTag<251> {
    UdpcastListenedAt(){}

    UdpcastListenedAt(std::string const& ipIn, uint16_t portIn) {
        snprintf(ipPort, sizeof(ipPort), "%s:%d", ipIn.c_str(), portIn);
    }
    char ipPort[24] = {0};

    friend
    std::ostream& operator << (std::ostream& os, UdpcastListenedAt const& m) {
        os << "UdpcastListeningAt:" << m.ipPort;
        return os;
    }
};
#pragma pack(pop)

/**
 * @class SessionStarted
 * @brief this message in the receiver's buffer indicating a new source is connected
 * @details only appears on the receiving side, and the receiver buffer is big enough to hold this messages
 */
struct SessionStarted 
: app::hasTag<254> {
    char ip[16];
    friend 
    std::ostream& operator << (std::ostream& os, SessionStarted const & m) {
        return os << "Session to TypeTag source started " << m.ip;
    }
};

/**
 * @class SessionDropped
 * @brief this message in the receiver's buffer indicating a previously connected source is dropped
 * @details only appears on the receiving side, and the receiver buffer is big enough to hold this messages
 */
struct SessionDropped 
: app::hasTag<255> {
    char ip[16];
    friend 
    std::ostream& operator << (std::ostream& os, SessionDropped const & m) {
        return os << "Session to source dropped " << m.ip;
    }
};

}}}
