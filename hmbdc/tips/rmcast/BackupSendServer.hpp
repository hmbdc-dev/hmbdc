#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rmcast/BackupSendSession.hpp"
#include "hmbdc/tips/reliable/BackupSendServerT.hpp"

namespace hmbdc { namespace tips { namespace rmcast {	
using ToCleanupAttQueue = reliable::ToCleanupAttQueue;
using AsyncBackupSendServer 
    = reliable::AsyncBackupSendServerT<TypeTagBackupSource, BackupSendSession>;
}}}
