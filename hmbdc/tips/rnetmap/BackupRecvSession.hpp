#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/Messages.hpp"
#include "hmbdc/tips/reliable/BackupRecvSessionT.hpp"

namespace hmbdc { namespace tips { namespace rnetmap {
template <typename OutputBuffer, typename AttachmentAllocator>
using BackupRecvSession = 
    hmbdc::tips::reliable::BackupRecvSessionT<TransportMessageHeader
        , SessionStarted
        , SessionDropped
        , SeqAlert
        , OutputBuffer
        , uint32_t
        , AttachmentAllocator>;
}}}
