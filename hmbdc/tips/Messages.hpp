#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Message.hpp"

#include <memory>
#include <assert.h>

namespace hmbdc { namespace tips {
/**
 * @brief Each TIPS Message type could optionally specify if the message delivery
 * should be dissabled for some types of delivery using these Masks
 * See below:
 * struct noLocal {
 *      static uint32_t tipsDisableSendMask() {
 *          return INTER_THREAD;
 *      }
 *  };
 * struct MyMsgNotDeliverredWithinLocalProcess : app::hasTag<1001>, noLocal{};
 */
enum DisableSendMask {
    DEFAULT_DISABLED= 0,
    INTER_THREAD    = 1 << 0,
    INTER_PROCESS   = 1 << 1,
    OVER_NETWORK    = 1 << 2,
};

HMBDC_CLASS_HAS_DECLARE(tipsDisableSendMask);   /// internal use
HMBDC_CLASS_HAS_DECLARE(hmbdc0cpyShmRefCount);   /// internal use

/**
 * @brief Message that need to be IPCed and/or go through Network
 * can derived from this type if it holds 1 (and only 1)
 * shared ptr to an already serilized POD type
 *
 * use this instead of app::hasMemoryAttachment directly to avoid the 
 * complication of releasing the attachment
 * 
 * @tparam Mesasge the concrete Message type thatderived from this tmeplate
 * @tparam T the underlying POD data type
 */
template <typename Message, typename T>
struct hasSharedPtrAttachment {
    using SP = std::shared_ptr<T>;
    static_assert(std::is_trivially_copyable<T>::value);
    SP attachmentSp;            /// the attachment
    size_t len = 0;                 /// the length of the POD data

    /**
     * @brief empty ctor
     * 
     */
    hasSharedPtrAttachment()
    {}


    /**
     * @brief Construct a new has Shared Ptr Attachment object
     * 
     * @param attachmentIn  input shared_ptr
     * @param len           byte size of the attachment
     */
    hasSharedPtrAttachment(SP attachmentIn, size_t len = sizeof(T))
    : attachmentSp(attachmentIn)
    , len(len) {
        static_assert(sizeof(app::hasMemoryAttachment::clientData) == sizeof(std::shared_ptr<T>));
    }

    /**
     * @brief internal use
     * 
     */
    struct hmbdcSerialized
    : app::hasMemoryAttachment
    , Message::tagBase::type {
    private:
        using tagBase = typename Message::tagBase;
        static constexpr size_t meatOffsetInMessage = sizeof(SP) + sizeof(size_t)
            + tagBase::size;
        char meat[sizeof(Message) - meatOffsetInMessage];

    public:
        enum {
            is_att_0cpyshm = has_hmbdc0cpyShmRefCount<T>::value,
        };
        uint64_t hmbdcShmRefCount = 0;
        hmbdcSerialized(Message const& message)
        : tagBase::type(message) {
            afterConsumedCleanupFunc = [](app::hasMemoryAttachment* h) {
                auto psp = (SP*)h->app::hasMemoryAttachment::clientData;
                psp->SP::~SP();
                h->app::hasMemoryAttachment::afterConsumedCleanupFunc = nullptr;
            };
            attachment = message.hasSharedPtrAttachment::attachmentSp.get();
            len = message.hasSharedPtrAttachment::len;
            new (this->clientData) SP(message.hasSharedPtrAttachment::attachmentSp);
            memcpy(meat, (char*)&message + meatOffsetInMessage, sizeof(meat));
        }

        Message toHmbdcUnserialized() {
            auto& att = (app::hasMemoryAttachment&)(*this);
            // assert(att.attachment);
            char resSpace[sizeof(Message)] = {0};
            auto& res = *(Message*)resSpace;
            if constexpr (Message::hasRange) {
                res.resetTypeTagOffsetInRange(this->typeTagOffsetInRange);
            }
            memcpy((char*)&res + meatOffsetInMessage, meat, sizeof(meat));

            if (att.attachment) {
                res.Message::hasSharedPtrAttachment::attachmentSp
                    = SP((typename SP::element_type*)(att.attachment)
                        , [hasMemoryAttachment = att](void*) mutable {
                            hasMemoryAttachment.release();
                        }
                    );
            }
            res.Message::hasSharedPtrAttachment::len = app::hasMemoryAttachment::len;
            Message toReturn = res;
            res.Message::~Message();
            att.attachment = nullptr;
            return toReturn;
        };
    };

    /**
     * @brief internal use
     */
    auto toHmbdcSerialized() const {
        return hmbdcSerialized{static_cast<Message const&>(*this)};
    }
};

/**
 * @brief Any non-POD Message type M can be transferred over IPC or network
 * if "S M::toHmbdcSerialized() const" is implemented and S is POD or
 * an app::hasMemoryAttachment Mesage type;
 * AND
 * M "S::toHmbdcUnserialized()"  is implemented.
 */
HMBDC_CLASS_HAS_DECLARE(toHmbdcSerialized);     /// internal use
HMBDC_CLASS_HAS_DECLARE(toHmbdcUnserialized);   /// internal use


}}
