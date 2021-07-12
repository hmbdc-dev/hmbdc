#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/Messages.hpp"

#include <memory.h>

namespace hmbdc { namespace tips { namespace reliable {
template <typename Buffer, typename TransportMessageHeader, typename AttachmentAllocator>
struct AttBufferAdaptor {
    private:
    Buffer& buffer_;
    std::unique_ptr<uint8_t[]> hasMemoryAttachmentMessageWrapSpace_;
    app::MessageWrap<app::hasMemoryAttachment>* hasMemoryAttachmentMessageWrap_;
    app::hasMemoryAttachment* hasMemoryAttachment_ = nullptr;
    size_t attTrainCount_ = 0;
    AttachmentAllocator alloc_;

    void abortAtt() {
        hasMemoryAttachment_->release();
        hasMemoryAttachment_ = nullptr;
    }

    public:
    AttBufferAdaptor(Buffer& buffer) 
    : buffer_(buffer)
    , hasMemoryAttachmentMessageWrapSpace_{new uint8_t[maxItemSize()] {0}}
    , hasMemoryAttachmentMessageWrap_((app::MessageWrap<app::hasMemoryAttachment>*)
        hasMemoryAttachmentMessageWrapSpace_.get())
    {}

    size_t maxItemSize() const {
        return buffer_.maxItemSize();
    }
    void put(TransportMessageHeader* h) {
        // HMBDC_LOG_D(h->getSeq(), '-', comment, port, h->messagePayloadLen, ':', h->typeTag(), '=', std::this_thread::get_id());
        if (hmbdc_unlikely(h->typeTag() == app::StartMemorySegTrain::typeTag)) {
            if (hmbdc_likely(!hasMemoryAttachment_)) {
                hasMemoryAttachment_ = &hasMemoryAttachmentMessageWrap_->get<app::hasMemoryAttachment>();
                *hasMemoryAttachment_ = 
                    h->template wrapped<app::StartMemorySegTrain>().att;
                attTrainCount_ = h->template wrapped<app::StartMemorySegTrain>().segCount;
                alloc_(h->typeTag(), hasMemoryAttachment_);
                // hasMemoryAttachment_->attachment = malloc(hasMemoryAttachment_->len);
                hasMemoryAttachment_->len = 0;
                // hasMemoryAttachment_->afterConsumedCleanupFunc = app::hasMemoryAttachment::free;
            }
        } else if (hmbdc_unlikely(hasMemoryAttachment_
            && h->typeTag() == app::MemorySeg::typeTag)) {
            if (attTrainCount_) {
                auto seg = &h->template wrapped<app::MemorySeg>(); //all attachment seg
                auto segLen = h->messagePayloadLen - (uint16_t)sizeof(app::MessageHead);
                memcpy((char*)hasMemoryAttachment_->attachment + hasMemoryAttachment_->len, seg, segLen);
                hasMemoryAttachment_->len += segLen;
                attTrainCount_--;
            } else {
                abortAtt();
            }
        } else if (hmbdc_unlikely(hasMemoryAttachment_)) {
                if (hmbdc_likely(!attTrainCount_) 
                    && h->flag == app::hasMemoryAttachment::flag) {
                    auto l = std::min<size_t>(buffer_.maxItemSize(), h->messagePayloadLen);
                    auto hsm = *hasMemoryAttachment_;
                    memcpy(hasMemoryAttachmentMessageWrapSpace_.get(), h->payload(), l);
                    *hasMemoryAttachment_ = hsm;
                    buffer_.putAtt(hasMemoryAttachmentMessageWrap_, buffer_.maxItemSize());
                    hasMemoryAttachment_ = nullptr;
                } else {
                    abortAtt();
                }
        } else {
            buffer_.put(h->payload(), h->messagePayloadLen);
        }
    }
        
    // template <typename T> void put(T& item) {buffer_.put(&item, sizeof(T));}
    template <typename T> void putSome(T& item) {
        buffer_.put(&item, std::min(sizeof(item), maxItemSize()));
    }
};
}}}
