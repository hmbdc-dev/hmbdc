#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/BackupSendSession.hpp"
#include "hmbdc/tips/reliable/BackupSendServerT.hpp"

namespace hmbdc { namespace tips { namespace rnetmap {
using ToCleanupAttQueue = reliable::ToCleanupAttQueue;
using AsyncBackupSendServer 
    = reliable::AsyncBackupSendServerT<TypeTagBackupSource, BackupSendSession>;
}}}
