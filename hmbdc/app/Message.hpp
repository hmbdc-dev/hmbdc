#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/time/Time.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/Endian.hpp"
#include "hmbdc/MetaUtils.hpp"
#include "hmbdc/Exception.hpp"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <utility>
#include <ostream>
#include <type_traits>
#include <bitset>
#include <cstddef>
#include <memory.h>
#include <stdint.h>

namespace hmbdc { namespace app {

template <typename M>
concept MessageC = std::decay<M>::type::typeSortIndex >= 0x0 
    && std::decay<M>::type::typeSortIndex <= 0xffff;

namespace message_detail {
template <typename ...M>
constexpr bool is_message_tuple = false;

template <MessageC ...M>
constexpr bool is_message_tuple<std::tuple<M...>> = true;
}

template <typename MT>
concept MessageTupleC = message_detail::is_message_tuple<MT>;

template <typename MIter>
concept MessageForwardIterC = MessageC<decltype(*(MIter{}))> 
    && requires(MIter it) {++it;};

/** 
* see @example hmbdc.cpp
* @example perf-tcpcast.cpp
*/

/**
 * @class hasTag<>
 * @brief util template to specify how a message type is mapped to tag (or tags).
 * Concrete message type needs to derive from this template.
 * By default, each message type has a unique 16 bit tag, so the message type 
 * and its tag is 1-1 mapped at compile time ("static-tagged");
 * If the map has to be runtime determined (such as configuration determined), the message type
 * can specify a rangeSize > 1, in that case, each message type is mapped to a tag range
 * [tag, tag + rangeSize) at compile time and the message's tag is determined at contruction time. 
 * They are also called "runtime-tagged" messages.
 * @tparam tag 16 bit unsigned tag
 * @tparam rangeSize how many tags starting from tag are mapped to this mesage type
 */
template <uint16_t tag, uint16_t rangeSize = 1> struct hasTag;
template <uint16_t tag>
struct hasTag<tag, 1> {
    using hmbdc_tag_type = hasTag;
    enum {
        typeTagInSpec = tag,
        typeTag = tag,
        typeSortIndex = tag,
        hasRange = 0,
        justBytes = 0,
    };

    friend 
    std::ostream& 
    operator << (std::ostream & os, hasTag const& t) {
        return os << "hasTag=" << tag;
    }
    uint16_t constexpr getTypeTag() const {
        return typeTag;
    }
};
template <uint16_t tagStart, uint16_t rangeSize>
struct hasTag {
    using hmbdc_tag_type = hasTag;
    enum {
        typeTagInSpec = tagStart,
        typeSortIndex = tagStart,
        hasRange = 1,
        justBytes = 0,
    };
    static constexpr uint16_t typeTagStart = tagStart;
    static constexpr uint16_t typeTagRange = rangeSize;

    hasTag(uint16_t offset)
    : typeTagOffsetInRange(offset) {
        if (offset >= typeTagRange) {
            HMBDC_THROW(std::out_of_range, offset << " too big for typeTagRange=" << typeTagRange);
        }
    }

    uint16_t getTypeTag() const { return typeTagStart + typeTagOffsetInRange; }
    void resetTypeTagOffsetInRange(uint16_t newOffset) {const_cast<XmitEndian<uint16_t>&>(typeTagOffsetInRange) = newOffset;}
    XmitEndian<uint16_t> const typeTagOffsetInRange{0};
    friend 
    std::ostream& 
    operator << (std::ostream & os, hasTag const& t) {
        return os << "hasTag tag=" << t.typeTagStart + t.typeTagOffsetInRange;
    }
};

HMBDC_CLASS_HAS_DECLARE(hmbdcIsAttInShm);
/**
 * @class hasMemoryAttachment
 * @brief if a specific hmbdc network transport (for example tcpcast, rmcast, and rnetmap) supports message 
 * with memory attachment, the message needs to be derived from this base - as the FIRST base, 
 * so it can be handled properly by the hmbdc network transport when sending and receiving it.
 * @details user on the sending side cannot directly free the attachment, instead the user can provide 
 * a callback func and hmbdc will call it - the default callback function is to call free on the attachment
 */
struct alignas(8) hasMemoryAttachment {
    /**
     * @brief default ctor 
     * @details assuming attachment will be allocated and release thru heap
     */
    hasMemoryAttachment()
    : afterConsumedCleanupFunc(hasMemoryAttachment::free)
    , attachment(NULL)
    , len(0) {
    }
    enum {
        flag = 1,
        att_via_shm_pool = 0,
    };

    /**
     * @brief file mapped memory
     * @param fileName file name to map
     */
    hasMemoryAttachment(char const* fileName) 
    : afterConsumedCleanupFunc(nullptr)
    , attachment(nullptr)
    , len(0) {
        map(fileName);
    }

    void release() {
        if (afterConsumedCleanupFunc) {
            afterConsumedCleanupFunc(this);
            afterConsumedCleanupFunc = nullptr;
            attachment = nullptr;
        }
    }

    using AfterConsumedCleanupFunc = void (*)(hasMemoryAttachment*);
    mutable
    AfterConsumedCleanupFunc afterConsumedCleanupFunc;  /// pointing to a func that handles cleaning up the attachment
                                                        /// it is automatically set, but user can change it as desire
                                                        /// when the sending side is done with this message, 
                                                        /// user code should never explicitly call this function
    union {
        void* attachment;                               /// pointer to the attachment bytes - see clientData data member 
                                                        /// usage if those bytes are already managed by shared_ptr mechanism
        boost::interprocess::managed_shared_memory
            ::handle_t shmHandle;                         
    };
    template <MessageC Message>
    bool holdShmHandle() const {
        if constexpr (has_hmbdcIsAttInShm<Message>::value) {
            auto& m = static_cast<Message const&>(*this);
            return m.hmbdcIsAttInShm;
        }
        return false;
    }

    mutable
    XmitEndian<size_t> len;                             /// byte size of the above
    
    mutable
    uint64_t clientData[2] = {0};                       /// client can put in any data - it will be transferred around
                                                        /// typically to be used to help in afterConsumedCleanupFunc.
                                                        /// 
                                                        /// for example, when the attached memory is managed by shared_ptr<uint_8[]>:
                                                        ///
                                                        /// using SP = std::shared_ptr<uint8_t[]>;
                                                        /// auto sp = SP(new uint8_t[a.len]);
                                                        /// MemoryAttachment3 a3;
                                                        /// a3.len = a.len;
                                                        /// a3.attachment = sp.get();
                                                        /// new (a3.clientData) SP{sp};
                                                        /// a3.afterConsumedCleanupFunc = [](hasMemoryAttachment* h) {
                                                        ///     ((SP*)h->clientData)->~SP();
                                                        /// };

    static void unmap(hasMemoryAttachment*);
    static void free(hasMemoryAttachment*);
    size_t map(char const* fileName);
};

/** 
 * @class MessageHead
 * @details It is recommneded that a Message type do not have significant dtor defined
 * because the message could travel through process bundaries or through network 
 * - won't compile if trying to send them.
 * The only exception is the BlockingContext (licensed package), which does allow  
 * message's dtors and call them properly within a process boundary.
 * @snippet hmbdc.cpp define a message
 */
#pragma pack(push, 1)
struct MessageHead {
	MessageHead(uint16_t typeTag)
	: reserved2(0)
    , reserved(0)
	, typeTag(typeTag)
	{}
	template <typename Message> Message& get();
	template <typename Message> Message const& get() const;
	template <typename Message> void setPayload(Message const&);
	template <typename Message, typename ...Args> void setPayloadInPlace(Args&& ... args);
    uint32_t reserved2;
    uint16_t reserved;

    XmitEndian<uint16_t> typeTag;
   
    using Scratchpad = 
        union {
            // XmitEndian<uint16_t> payloadLen;
            struct {
                char forbidden[sizeof(pid_t)];
                uint8_t flag;   /// not usable when inbandUnderlyingTypeTag is used
            } desc;
            XmitEndianByteField<uint64_t, 6> seq;
            union {
                struct {
                    pid_t from;
                    uint16_t inbandUnderlyingTypeTag : 16;
                } hd;
                uint64_t inbandPayloadLen : 48;
            } ipc;
            uint64_t reserved : 48;
            uint8_t space[6];
        };
    static_assert(sizeof(Scratchpad) == sizeof(reserved2) + sizeof(reserved), "");
    static_assert(sizeof(Scratchpad::seq) == sizeof(reserved2) + sizeof(reserved), "");
    Scratchpad& scratchpad() {return *reinterpret_cast<Scratchpad*>(&reserved2);}
    Scratchpad const& scratchpad() const{return *reinterpret_cast<Scratchpad const*>(&reserved2);}
    friend 
    std::ostream& operator << (std::ostream& os, MessageHead const & h) {
    	return os << h.scratchpad().reserved << ' ' << h.typeTag;
    }

    template <MessageC Message>
    static MessageHead const& retrieveFrom(Message const& m) {
        auto addr = (char const*)&m - sizeof(MessageHead);
        return *(MessageHead const*)addr;
    }
};
#pragma pack(pop)
static_assert(sizeof(MessageHead) == 8, "pakcing issue?");

template <typename Message>
struct MessageWrap : MessageHead {
    template <typename ...Args>
	explicit MessageWrap(Args&& ... args)
	: MessageHead(0) //delayed
	, payload(std::forward<Args>(args)...) {
        // static_assert(std::is_base_of<hasTag<Message::typeTag>, Message>::value
        //     , "not a properly defined Message");
        if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
            this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        }
        if constexpr (Message::hasRange) {
            this->typeTag = payload.getTypeTag();
        } else {
            this->typeTag = Message::typeTag;
        }
	}

    MessageWrap(MessageWrap const&) = default;
    MessageWrap(MessageWrap &) = default;
    MessageWrap(MessageWrap &&) = default;
    MessageWrap& operator = (MessageWrap const&) = default;
    MessageWrap& operator = (MessageWrap &&) = default;

    Message payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap<Message> const & w) {
    	return os << static_cast<MessageHead const&>(w) << ' ' << w.payload;
    }
};

template <typename Message>
Message& 
MessageHead::get() {
	return static_cast<MessageWrap<Message>*>(this)->payload;
}

template <typename Message>
Message const& 
MessageHead::get() const{
	return static_cast<MessageWrap<Message> const*>(this)->payload;
}

template <typename Message> 
void 
MessageHead::
setPayload(Message const& m) {
	new (&get<Message>()) Message(m);
	typeTag = Message::typeTag;
}

template <typename Message, typename ...Args> 
void
MessageHead::
setPayloadInPlace(Args&& ... args) {
	new (&get<Message>()) Message(std::forward<Args>(args)...);
	typeTag = Message::typeTag;
}

struct Flush
: hasTag<0> {
};


#pragma pack(push, 1)
template <size_t MaxStreamableSize>
struct LoggingT
: hasTag<3> {
    char payload[MaxStreamableSize];
};
#pragma pack(pop)

/**
 * @class JustBytes
 * @brief A special type of message only used on the receiving side
 * @details When a Client declare this type among its interested message types, all
 * messages types (WITHOUT the destructor) will be delievered in 
 * byte form to this client. It is normmaly listed as the last Message type to
 * in the interested message type list to catch all
 * See in example hmbdc.cpp @snippet hmbdc.cpp use JustBytes instead of Message types
 */
struct JustBytes
: hasTag<4> {
    enum {
        justBytes = 1,
        typeSortIndex = 65535,
    };
    uint8_t bytes[1];

	JustBytes(void const* bytesIn, size_t len) {
		memcpy(bytes, bytesIn, len);
    }

    JustBytes(JustBytes const&) = delete;
};

struct Trace {
    Trace()
    : hmbdc_ctor_ts(time::SysTime::now()){}
    time::SysTime hmbdc_ctor_ts;
    time::SysTime hmbdc_net_queued_ts;
    time::SysTime hmbdc_dispatched_ts;

    uint8_t tsIndex;
    friend 
    std::ostream& operator << (std::ostream& os, Trace const & t) {
        return os << t.hmbdc_ctor_ts << ' ' << t.hmbdc_net_queued_ts << ' ' << t.hmbdc_dispatched_ts;
    }
};

template<>
struct MessageWrap<JustBytes> :  MessageHead {
	MessageWrap(uint16_t tag, void const* bytes, size_t len, hasMemoryAttachment const* att)
	: MessageHead(tag)
    , payload(bytes, len) {
        if (att) {
            this->scratchpad().desc.flag = hasMemoryAttachment::flag;
        }
	}
    JustBytes payload;

    friend 
    std::ostream& operator << (std::ostream& os, MessageWrap const & w) {
    	return os << static_cast<MessageHead const&>(w) << " *";
    }
};

static_assert(sizeof(MessageWrap<JustBytes>) == sizeof(MessageHead) + 1, "wrong packing");
static_assert(sizeof(MessageWrap<Flush>) == sizeof(MessageHead) + sizeof(Flush), "wrong packing");

template <MessageC Message>
struct is_hasMemoryAttachment_first_base_of {
    static bool constexpr value = [](){
        if constexpr (std::is_base_of<hasMemoryAttachment, Message>::value) {
#ifdef __clang__
            return true;
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
            return offsetof(Message, hasMemoryAttachment::afterConsumedCleanupFunc) == 0;
#pragma GCC diagnostic pop
#endif    
        } else {
            return false;
        }
    }();
};

struct alignas(8) StartMemorySegTrain
: hasTag<5> {
    XmitEndian<uint16_t> inbandUnderlyingTypeTag;
    hasMemoryAttachment att;
    XmitEndian<size_t> segCount;

    friend 
    std::ostream& operator << (std::ostream& os, StartMemorySegTrain const & m) {
        return os << "StartAttachmentTrain " << m.inbandUnderlyingTypeTag 
        << ' ' << ' ' << m.segCount;
    }
};

#pragma pack(push, 1)
struct MemorySeg
: hasTag<6> { //none of the following reaches the receiving side - only used on sedning side
    void const *  seg;
    uint16_t len;
    uint16_t inbandUnderlyingTypeTag{0};
    friend 
    std::ostream& operator << (std::ostream& os, MemorySeg const & m) {
        return os << "MemorySeg " << m.seg << ' ' << m.len << ' ' << m.inbandUnderlyingTypeTag;
    }
};
#pragma pack(pop)

template <MessageC Message>
struct alignas(8) InBandHasMemoryAttachment
: hasTag<7> {

    template <typename ShmAllocator>
    void shmConvert(ShmAllocator&& allocator) {
        hasMemoryAttachment& att = underlyingMessage;
        assert(att.holdShmHandle<Message>());
        if constexpr (Message::att_via_shm_pool) {
            if (underlyingMessage.hmbdcIsAttInShm) {
                /// already in shm
                att.shmHandle = allocator.getHandle(att.attachment);
                return;
            } else {
                assert(0);
            }
        } 
        auto shmAddr = allocator.allocate(att.len);
        memcpy(shmAddr, att.attachment, att.len);
        att.shmHandle = allocator.getHandle(shmAddr);
    }

    Message underlyingMessage;
private:
    template <typename M>
    friend struct MessageWrap;

    template <typename ...Args>
    explicit InBandHasMemoryAttachment(Args&&... args)
    : underlyingMessage(std::forward<Args>(args)...) {}
    using M = typename std::decay<Message>::type;
    static_assert(std::is_trivially_destructible<M>::value, "cannot send message with dtor");
};


#pragma pack(push, 1)
struct InBandMemorySeg
: hasTag<8> {
    char seg[1]; //open end
    friend 
    std::ostream& operator << (std::ostream& os, InBandMemorySeg const & m) {
        return os << "MemorySegInBand";
    }
};
#pragma pack(pop)

template<MessageC Message>
struct MessageWrap<InBandHasMemoryAttachment<Message>> :  MessageHead {
    template <typename ...Args>
    explicit MessageWrap(Args&& ...args)
    : MessageHead(InBandHasMemoryAttachment<JustBytes>::typeTag)
    , payload(std::forward<Args>(args)...) {
        this->scratchpad().ipc.hd.inbandUnderlyingTypeTag 
            = payload.underlyingMessage.getTypeTag();
        }
    InBandHasMemoryAttachment<Message> payload;

    friend 
    std::ostream& operator << (std::ostream& os
        , MessageWrap<InBandHasMemoryAttachment<Message>> const & w) {
        return os << static_cast<MessageHead const&>(w) << " *";
    }
};

template<>
struct MessageWrap<InBandHasMemoryAttachment<JustBytes>> :  MessageHead {
	MessageWrap(uint16_t tag, void const* bytes, size_t len, hasMemoryAttachment* att)
	: MessageHead(InBandHasMemoryAttachment<JustBytes>::typeTag)
    , payload(bytes, len) {
        this->scratchpad().ipc.hd.inbandUnderlyingTypeTag = tag;
	}
    InBandHasMemoryAttachment<JustBytes> payload;

    friend 
    std::ostream& operator << (std::ostream& os
        , MessageWrap<InBandHasMemoryAttachment<JustBytes>> const & w) {
    	return os << static_cast<MessageHead const&>(w) << " *";
    }
};

/**
 * @class LastSystemMessage
 * @brief hmbdc system messages use tag values less than this one
 */
struct LastSystemMessage 
: hasTag<999> {
};

template <MessageC Message>
constexpr bool typeTagMatch(uint16_t tag) {
    if constexpr (Message::justBytes) {
        return tag > LastSystemMessage::typeTag;
    }
    if constexpr (Message::hasRange) {
        return tag >= Message::typeTagStart 
            && tag < Message::typeTagStart + Message::typeTagRange;
    } else {
        return tag == Message::typeTag;
    }
}

inline
size_t 
hasMemoryAttachment::
map(char const* fileName) {
    int fd;
    struct stat sb;
 
    fd = open(fileName, O_RDONLY);
    fstat(fd, &sb);
    len = (size_t)sb.st_size;
    attachment = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (attachment == MAP_FAILED) {
        HMBDC_THROW(std::runtime_error, "map failed for " << fileName);
    } 
    close(fd);
    afterConsumedCleanupFunc = hasMemoryAttachment::unmap;
    return len;
}

inline
void 
hasMemoryAttachment::
free(hasMemoryAttachment* a) {
    ::free(a->attachment);
    a->attachment = nullptr;
}

inline
void 
hasMemoryAttachment::
unmap(hasMemoryAttachment* a) {
    munmap(a->attachment, a->len);
    a->attachment = nullptr;
}
}} //hmbdc::app

