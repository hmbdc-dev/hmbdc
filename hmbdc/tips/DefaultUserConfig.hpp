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
    "pumpConfigs"                   : [{}],             "__pumpConfigs"                     : ["an array, each element (captured in a {}) maps to a pump, and contains the configs for the pump, for example each rmcast transport's multicast address etc. Messages are handled by one and only one of the pumps depending on its tag value. Default value is single pump with default configs"],
    "pumpCpuAffinityHex"            : 0,                "__pumpCpuAffinityHex"              : "CPU mask that the pumps threads pinned on - use the same few N CPUs for multiple M process's Domains (M >> N) is recommended",
    "pumpSchedPolicy"               : "SCHED_OTHER",    "__pumpSchedPolicy"                 : "pump thread shced policy, SCHED_OTHER, SCHED_RR, SCHED_FIFO etc are supported",
    "pumpSchedPriority"             : 0,                "__pumpSchedPriority"               : "the priority value in within the sched policy",
    "pumpMaxBlockingTimeSec"        : 0.00001,          "__pumpMaxBlockingTimeSec"          : "starting from 0.0, increase this float if you want the Domain's pump to be more CPU friendly, decrease to reduce latency",
    "pumpRunMode"                   : "delayed",        "__pumpRunMode"                     : "delayed - user will call Domain<...>::startPumping() to start message pumping; manual - user will use own thread to call pumpOnce() each time to pump messages flow",
    "localDomainName"               : "tips-auto",      "__localDomainName"                 : "a string used to map the domain for local IPC use. By defauilt, it is auto figured from network protocol but it could be overrided with a specific unique value for the Domain. For example, it is used to specify the dev name such as a PCI/host shared mem - see ipc_property::use_dev_mem."
}
)|";
}}
