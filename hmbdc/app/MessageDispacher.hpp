#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/app/Message.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/MetaUtils.hpp"

namespace hmbdc { namespace app {

namespace messagehandler_detail {
HMBDC_CLASS_HAS_DECLARE(ibmaProc);
HMBDC_CLASS_HAS_DECLARE(hmbdc_dispatched_ts);
HMBDC_CLASS_HAS_DECLARE(hmbdcName);
HMBDC_CLASS_HAS_DECLARE(hmbdcAvoidIpcFrom);

template <typename Message>
int compare(uint16_t tag) {
    if constexpr (Message::hasRange) {
        if (tag < Message::typeTagStart) return -1;
        if (tag >= Message::typeTagStart + Message::typeTagRange) return 1;
        return 0;
    } else {
        if (tag < Message::typeTag) return -1;
        if (tag > Message::typeTag) return 1;
        return 0;
    }
}

template <typename CcClient, typename Message>
struct dispatcher {
    bool operator()(CcClient& c, MessageHead& m) {
        if (typeTagMatch<Message>((uint16_t)m.typeTag)) {
            if constexpr (has_hmbdcName<CcClient>::value) {
                HMBDC_LOG_r(c.hmbdcName(), " (", this, ") <=== ", m.get<Message>());
            }
            if constexpr (has_hmbdc_dispatched_ts<Message>::value) {
                m.get<Message>().hmbdc_dispatched_ts = hmbdc::time::SysTime::now();
            }
            auto& msg = m.get<Message>();
            if constexpr (has_hmbdcAvoidIpcFrom<CcClient>::value) {
                    if (hmbdc_likely(c.hmbdcAvoidIpcFrom != m.scratchpad().ipc.hd.from)) {
                        if constexpr (Message::justBytes) {
                            c.handleJustBytesCb(m.typeTag, msg.bytes
                                , m.scratchpad().desc.flag? (hasMemoryAttachment*)msg.bytes : nullptr);
                        } else {
                            c.handleMessageCb(msg);
                        }
                    }
            } else {
                if constexpr (Message::justBytes) {
                    c.handleJustBytesCb(m.typeTag, msg.bytes
                        , m.scratchpad().desc.flag? (hasMemoryAttachment*)msg.bytes : nullptr);
                } else {
                    c.handleMessageCb(msg);
                }
            }
            using Wrap = MessageWrap<Message>;
            auto& wrap = static_cast<Wrap&>(m);
            wrap.~Wrap();
            return true; 
        } else if constexpr((std::is_base_of<hasMemoryAttachment, Message>::value
            || Message::justBytes) && has_ibmaProc<CcClient>::value) {
            if constexpr(std::is_same<std::nullptr_t, decltype(c.ibmaProc)>::value) {
                return false;
            } else {
                auto& ibmaproc = c.ibmaProc;
                if (ibmaproc.att->attachment) {
                    if (hmbdc_unlikely(m.typeTag != InBandMemorySeg::typeTag)) return false;
                    if (ibmaproc.accumulate(m.get<InBandMemorySeg>(), m.scratchpad().ipc.inbandPayloadLen)) {
                        if (typeTagMatch<Message>(ibmaproc.typeTag)) {
                            auto* msg = reinterpret_cast<Message*>(ibmaproc.underlyingMessage); (void)msg;
                            if constexpr (has_hmbdcName<CcClient>::value) {
                                HMBDC_LOG_r(c.hmbdcName(), " (", this, ") <=== ", *msg);
                            }
                            if constexpr (has_hmbdc_dispatched_ts<Message>::value) {
                                msg->hmbdc_dispatched_ts = hmbdc::time::SysTime::now();
                            }

                            if constexpr (Message::justBytes) {
                                c.handleJustBytesCb(ibmaproc.typeTag, ibmaproc.underlyingMessage, ibmaproc.att);
                                ibmaproc.att->release();
                            } else {
                                c.handleMessageCb(*msg);
                                msg->release();
                            }
                            // ibmaproc.att->attachment = nullptr;
                            return true;
                        } else {
                            return false;
                        }
                    } else {
                        return true;
                    }
                } else if (m.typeTag == InBandHasMemoryAttachment<Flush>::typeTag) {
                    auto underlyTag = m.scratchpad().ipc.hd.inbandUnderlyingTypeTag;
                    if (typeTagMatch<Message>((uint16_t)underlyTag)) {
                        auto& ibma = m.get<InBandHasMemoryAttachment<Message>>();
                        if constexpr (has_hmbdcAvoidIpcFrom<CcClient>::value) {
                            if (hmbdc_likely(c.hmbdcAvoidIpcFrom == m.scratchpad().ipc.hd.from)) {
                                return true; // skip the rest
                            }
                        }
                        
                        if (ibmaproc.cache(ibma, underlyTag)) {
                            auto* msg = reinterpret_cast<Message*>(ibmaproc.underlyingMessage);(void)msg;
                            if constexpr (has_hmbdcName<CcClient>::value) {
                                HMBDC_LOG_r(c.hmbdcName(), " (", this, ") <=== ", *msg);
                            }
                            if constexpr (has_hmbdc_dispatched_ts<Message>::value) {
                                msg->hmbdc_dispatched_ts = hmbdc::time::SysTime::now();
                            }

                            if constexpr (Message::justBytes) {
                                c.handleJustBytesCb(ibmaproc.typeTag, ibmaproc.underlyingMessage, ibmaproc.att);
                                ibmaproc.att->release();
                            } else {
                                c.handleMessageCb(*msg);
                                msg->release();
                            }
                        }
                        return true;
                    }
                    return false;
                } //return false;
            }
        }
        return false;
    }
};

template<class T, class U>
struct ascending_by_tag 
: std::integral_constant<bool, (uint8_t)T::typeSortIndex < (uint8_t)U::typeSortIndex>
{};

template <typename CcClient, typename>
struct SeqMessageDispacher {
    bool operator()(CcClient&, MessageHead&) const{return false;}
};

template <typename CcClient, typename M, typename ... Messages>
struct SeqMessageDispacher<CcClient, std::tuple<M, Messages...>> {
    bool operator()(CcClient& c, MessageHead& e) const {
        if (!dispatcher<CcClient, M>()(c, e))
            return SeqMessageDispacher<CcClient, std::tuple<Messages ...>>()(c, e);
        else
            return true;
    }
};

template <typename CcClient, typename>
struct SortedMessageDispacher {
    bool operator()(CcClient&, MessageHead&) const;
};

template <typename CcClient, typename ... Messages>
struct SortedMessageDispacher<CcClient, std::tuple<Messages...>> {
    bool operator()(CcClient& c, MessageHead& e) const {
        if constexpr (sizeof...(Messages) > 16) {
            using SortedTupleMessages
                = typename sort_tuple<messagehandler_detail::ascending_by_tag
                    , std::tuple<Messages...>>::type;

            return bsearch_tuple<SortedTupleMessages>()(
                [&c, &e](auto* t) {
                    using M = typename std::remove_reference<decltype(*t)>::type;
                    if (messagehandler_detail::dispatcher<CcClient, M>()(c, e)) {
                        return 0;
                    }
                    return messagehandler_detail::compare<M>(e.typeTag);
                }
            );
        } else {
            return messagehandler_detail::SeqMessageDispacher<
                CcClient, std::tuple<Messages ...>>()(c, e);
        }
    }
};

}

template <typename CcClient, typename MessageTuple>
struct MessageDispacher {
    /// place the JustBytes and its derived the last
    using SortedTupleMessages
        = typename sort_tuple<messagehandler_detail::ascending_by_tag, MessageTuple>::type;
    bool operator()(CcClient& c, MessageHead& e) const {
        return messagehandler_detail::SortedMessageDispacher<CcClient, SortedTupleMessages>()(c, e);
    }
};

}}
