#include "hmbdc/Copyright.hpp"
#pragma once

namespace hmbdc { namespace tips {
/**
 * the default Domain config
 */
constexpr char const*  const DefaultUserConfig = R"|(
{
    "ifaceAddr"                     : "127.0.0.1",      "__ifaceAddr"                       : "network interface this Dmain tied to, other related net params are listed in individual net transport DefaultUserConfig, can override here",
    "ipcMessageQueueSizePower2Num"  : 20,               "__ipcMessageQueueSizePower2Num"    : "IPC tansport buffer depth - 2^20 (1M) by default",
    "ipcMaxMessageSizeRuntime"      : 1000,             "__ipcMaxMessageSizeRuntime"        : "if not listed in code's ipc_property (==0), use this value",
    "ipcTransportOwnership"         : "optional",       "__ipcTransportOwnership"           : "the shm of the IPC transport is owned by a single Domain object per domain per host (the first Domain object gets constructed) - when this Domain is destroyed, the IPC shm is destroyed. This setting is to coordinate the Domain creation. 'optional': this Domain is either creating the shm IPC transport or attach to the existing one; 'own': must recreate and own; 'attach': only attach to existing one.",
    "ipcPurgeIntervalSeconds"       : 0,                "__ipcPurgeIntervalSeconds"         : "if not 0, a purger is to run to remove dead or stagnant IPC party so other parties are not impacted. This is the period of doing the purge",
    "ipcShmForAttPoolSize"          : 134217728,        "__ipcShmForAttPoolSize"            : "large IPCable message or 0cpy messages could use this shm pool to pass around the attachment. This is the pool size in bytes.",
    "netMaxMessageSizeRuntime"      : 1000,             "__netMaxMessageSizeRuntime"        : "if not listed in code's (==0), use this as network transport's buffer width in bytes",
    "pumpHmbdcName"                 : "tipspump",       "__pumpHmbdcName"                   : "pump thread uses this as thread name",
    "pumpCount"                     : 1,                "__pumpCount"                       : "thread count for Domain activities - only increase it when the traffic cannot be handled by 1 thread",
    "pumpCpuAffinityHex"            : 1,                "__pumpCpuAffinityHex"              : "CPU mask that the pumps threads pinned on - use the same few N CPUs for multiple M process's Domains (M >> N) is recommended",
    "pumpMaxBlockingTimeSec"        : 0.00001,          "__pumpMaxBlockingTimeSec"          : "starting from 0.0, increase this float if you want the Domain's pump to be more CPU friendly, decrease to reduce latency",
    "pumpRunMode"                   : "delayed",        "__pumpRunMode"                     : "delayed - user will call Domain<...>::startPumping() to start message pumping; manual - user will use own thread to call runOnce() each time to pump messages flow",
    "tipsDomainNonNet"              : "tips-local",     "__tipsDomainNonNet"                : "a string used in IPC transport name when network is disabled"
}
)|";
}}
