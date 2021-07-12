#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/Messages.hpp"
#include "hmbdc/tips/reliable/BackupSendSessionT.hpp"

namespace hmbdc { namespace tips { namespace rnetmap {
using BackupSendSession 
    = hmbdc::tips::reliable::BackupSendSessionT<TransportMessageHeader>;
}}}
