#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rmcast/Messages.hpp"
#include "hmbdc/tips/reliable/BackupRecvSessionT.hpp"

#include <netinet/in.h>
namespace hmbdc { namespace tips { namespace rmcast {
template <typename OutputBuffer, typename AttachmentAllocator>
using BackupRecvSession = 
    hmbdc::tips::reliable::BackupRecvSessionT<TransportMessageHeader
        , SessionStarted
        , SessionDropped
        , SeqAlert
        , OutputBuffer
        , sockaddr_in
        , AttachmentAllocator>;
}}}
