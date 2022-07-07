
#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/tips/Messages.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/Compile.hpp"

#include <ostream>
#include <string>
#include <limits>
#include <unistd.h>
#include <stdio.h>
#include <memory.h>

namespace hmbdc { namespace tips { namespace rnetmap {

#pragma pack(push, 1)
struct TransportMessageHeader {
    uint8_t flag = 0; //1 - hasAttachment
    XmitEndian<uint16_t> messagePayloadLen;
    static size_t maxPayloadSize() {
        return 1023;
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
        return sizeof(TransportMessageHeader) + messagePayloadLen;
    }

    void const* wireBytes() const {
        return reinterpret_cast<const char*>(this);
    }

    void * wireBytes() {
        return reinterpret_cast<char*>(this);
    }

    void const* wireBytesMemorySeg() const {
        return wrapped<app::MemorySeg>().seg;
     }
 
    size_t wireSizeMemorySeg() const {
        return wrapped<app::MemorySeg>().len;
    }

    HMBDC_SEQ_TYPE getSeq() const {
        auto res = (HMBDC_SEQ_TYPE)static_cast<app::MessageHead const*>(payload())->scratchpad().seq;
        if (hmbdc_unlikely(res == 0xfffffffffffful)) return std::numeric_limits<HMBDC_SEQ_TYPE>::max();
        return res;
    }

    void setSeq(HMBDC_SEQ_TYPE seq) {
        static_cast<app::MessageHead*>(payload())->scratchpad().seq = seq;
    }
};
#pragma pack(pop)

struct TypeTagBackupSource 
: app::hasTag<553> {
    TypeTagBackupSource(std::string const& ipIn
        , uint16_t portIn
        , time::Duration recvReportDelayIn
        , bool loopbackIn)
    : port(portIn)
    , recvReportDelay(recvReportDelayIn)
    , loopback(loopbackIn) {
        srcPid = getpid();
        snprintf(ip, sizeof(ip), "%s", ipIn.c_str());
    }
    char ip[16];
    XmitEndian<uint16_t> typeTagCountContained{0};
    XmitEndian<uint16_t> typeTags[64];
    bool addTypeTag(uint16_t tag) {
        auto c = uint16_t{typeTagCountContained};
        if (c < 64) {
            typeTags[c++] = tag;
            typeTagCountContained = c;
            return true;
        }
        return false;
    }

    XmitEndian<uint16_t> port;
    time::Duration recvReportDelay; //reporting back the seq expecting every so often
    bool loopback;
    uint32_t sendFrom; //to be filled by recv side
    XmitEndian<uint32_t> srcPid;

    friend 
    std::ostream& operator << (std::ostream& os, TypeTagBackupSource const & m) {
        os << "TypeTagBackupSource:" << m.ip << ':' << m.port;
        for (auto i = 0u; i < m.typeTagCountContained; i++) {
            os << ' ' << m.typeTags[i];
        }
        return os;
    }
};

/**
 * @class SessionStarted
 * @brief this message appears in the receiver's buffer indicating a new source is connected
 * @details only appears on the receiving side, and the receiver buffer is big enough to hold this messages
 */
struct SessionStarted 
: app::hasTag<554> {
    char ip[16];
    friend 
    std::ostream& operator << (std::ostream& os, SessionStarted const & m) {
        return os << "Session to type tag source started " << m.ip;
    }
};

/**
 * @class SessionDropped
 * @brief this message appears in the receiver's buffer indicating a previously connected source is dropped
 * @details only appears on the receiving side, and the receiver buffer is big enough to hold this messages
 */
struct SessionDropped 
: app::hasTag<555> {
    char ip[16];
    friend 
    std::ostream& operator << (std::ostream& os, SessionDropped const & m) {
        return os << "Session to tag tag source dropped " << m.ip;
    }
};

struct SeqAlert
: app::hasTag<556> {
    XmitEndian<HMBDC_SEQ_TYPE> expectSeq;
    friend 
    std::ostream& operator << (std::ostream& os, SeqAlert const & m) {
        return os << "SeqAlert " << m.expectSeq;
    }
};

static_assert(sizeof(SeqAlert) == sizeof(HMBDC_SEQ_TYPE)
    , "do you have a pack pragma unclosed that influencs the above struct packing unexpectedly?");
}}}
