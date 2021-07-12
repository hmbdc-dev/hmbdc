#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rmcast/Messages.hpp"
#include "hmbdc/tips/reliable/BackupSendSessionT.hpp"

namespace hmbdc { namespace tips { namespace rmcast {
using BackupSendSession 
    = hmbdc::tips::reliable::BackupSendSessionT<TransportMessageHeader>;
}}}
