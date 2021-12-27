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
 * struct MyMsgNotDeliverredWithinLocalProcess 
 * : app::hasTag<1001>
 * , do_not_send_via<INTER_PROCESS | INTER_THREAD> { ... };
 */
enum DisableSendMask {
    INTER_THREAD    = 1 << 0,
    INTER_PROCESS   = 1 << 1,
    OVER_NETWORK    = 1 << 2,
};

template <int disable_send_mask_in>
struct do_not_send_via {
    enum {
        mask = disable_send_mask_in,
    };
};

namespace messages_detail {
template <app::MessageC Message>
struct tag_base {
    using type = typename Message::hmbdc_tag_type;
    static constexpr size_t size = [](){
        if constexpr (is_derived_from_non_type_template_v<uint16_t, app::hasTag, type>) return 0;
        else return sizeof(type);
    }();
};
}

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
 * @tparam use_shm_pool Use the shm pool to hold the attachment for the IPC transfer
 * - see ipcShmForAttPoolSize config. If the attachment is allocated via Domain::allocateInShmFor0cpy,
 * no extra copy will happen in IPC transfer; If not, exact 1 copy will happen to copy the attachment 
 * to shm pool
 */
template <typename Message, typename T, bool use_shm_pool = false>
struct hasSharedPtrAttachment {
    enum {
        att_via_shm_pool = use_shm_pool,
    };

    using SP = std::shared_ptr<T>;
    static_assert(std::is_trivially_copyable<T>::value);

    const SP attachmentSp;          /// the attachment
    const size_t len;               /// the length of the POD data
    const bool isAttInShm;          /// the attachment is in the shm pool alreadyq
    uint8_t reserved[7] = {0};      /// it is imperative for the struct to have a multiple
                                    /// of 8 as its size so the inheritance packing is not
                                    /// wierd
    static_assert(sizeof(app::hasMemoryAttachment::clientData) == sizeof(std::shared_ptr<T>));

    /**
     * @brief ctor
     * 
     * @param isAttInShmIn if the attachment is allocated in the shm pool 
     * - via Domain::allocateInShmFor0cpy or Node::allocateInShmFor0cpy
     * @param attachmentSpIn    input shared_ptr
     * @param lenIn             byte size of the attachment
     */
    hasSharedPtrAttachment(SP attachmentSpIn = SP{}
        , size_t lenIn = sizeof(typename SP::element_type), bool isAttInShmIn = false)
    : attachmentSp(attachmentSpIn)
    , len(attachmentSpIn.get() ? lenIn : 0)
    , isAttInShm(isAttInShmIn) {
    }

    auto& operator = (hasSharedPtrAttachment const& other) {
        reset(other.isAttInShm, other.attachmentSp, other.len);
        return *this;
    }

    template <typename Message2, bool use_shm_pool2>
    auto& operator = (hasSharedPtrAttachment<Message2, T, use_shm_pool2> const& other) {
        reset(other.isAttInShm, other.attachmentSp, other.len);
        return *this;
    }


    /**
     * @brief reset to a new state
     * 
     * @param isAttInShmIn if the attachment is allocated in the shm pool 
     * - via Domain::allocateInShmFor0cpy or Node::allocateInShmFor0cpy
     * @param attachmentSpIn    input shared_ptr
     * @param lenIn             byte size of the attachment
     */
    void reset(bool isAttInShmIn = false, SP attachmentSpIn = SP{}, size_t lenIn = sizeof(T)) {
        const_cast<bool&>(isAttInShm) = use_shm_pool ? isAttInShmIn : false;
        const_cast<size_t&>(len) = attachmentSpIn.get() ? lenIn : 0;
        const_cast<SP&>(attachmentSp) = attachmentSpIn;
    }

    /**
     * @brief internal use
     * 
     */
    struct hmbdcSerialized
    : app::hasMemoryAttachment
    , messages_detail::tag_base<Message>::type {
    private:
        using tagBase = typename messages_detail::tag_base<Message>;
        static constexpr size_t meatOffsetInMessage = sizeof(SP) + sizeof(size_t) 
            + sizeof(size_t) + tagBase::size; /// size of hasSharedPtrAttachment + tagBase
        char meat[sizeof(Message) - meatOffsetInMessage];

    public:
        enum {
            att_via_shm_pool = Message::att_via_shm_pool,
        };
        bool hmbdcIsAttInShm = false;

        static size_t& getHmbdc0cpyShmRefCount(app::hasMemoryAttachment const& att) {
            return const_cast<size_t&>(*(reinterpret_cast<size_t const*>(att.attachment) - 1));
        }

        size_t& getHmbdc0cpyShmRefCount() {
            return *(reinterpret_cast<size_t*>(app::hasMemoryAttachment::attachment) - 1);
        }

        size_t const& getHmbdc0cpyShmRefCount() const {
            return *(reinterpret_cast<size_t const*>(app::hasMemoryAttachment::attachment) - 1);
        }
        
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
            hmbdcIsAttInShm = message.hasSharedPtrAttachment::isAttInShm;
        }

        Message fromHmbdcIpcable() {
            auto& att = (app::hasMemoryAttachment&)(*this);
            char resSpace[sizeof(Message)] = {0};
            auto& res = *(Message*)resSpace;
            memcpy((char*)&res + meatOffsetInMessage, meat, sizeof(meat));
            if constexpr (Message::hasRange) {
                res.resetTypeTagOffsetInRange(this->typeTagOffsetInRange);
            }

            if (att.attachment) {
                res.hasSharedPtrAttachment::reset(
                    hmbdcIsAttInShm
                    , SP((typename SP::element_type*)(att.attachment)
                        , [hasMemoryAttachment = att](void*) mutable {
                            hasMemoryAttachment.release();
                        }
                    )
                    , len
                );
            }
            Message toReturn = res;
            res.Message::~Message();
            att.attachment = nullptr;
            return toReturn;
        };
    };

    /**
     * @brief internal use
     */
    auto toHmbdcIpcable() const {
        return hmbdcSerialized{static_cast<Message const&>(*this)};
    }
};

/**
 * @brief Any non-POD Message type M can be transferred over IPC or network
 * if "S M::toHmbdcIpcable() const" is implemented and S is POD or
 * an app::hasMemoryAttachment Mesage type;
 * AND
 * M "S::fromHmbdcIpcable()"  is implemented.
 */
HMBDC_CLASS_HAS_DECLARE(toHmbdcIpcable);     /// internal use
HMBDC_CLASS_HAS_DECLARE(fromHmbdcIpcable);   /// internal use


}}
