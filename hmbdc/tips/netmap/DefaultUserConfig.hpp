#include "hmbdc/Copyright.hpp"
#pragma once

namespace hmbdc { namespace tips { namespace netmap {
/**
 * the send and recv engine config parameters and its default values used in this module
 */
constexpr char const* const DefaultUserConfig = R"|(
{
    "doChecksum"    : false,                            "__doChecksum"           :"calculating checksum when sending or receiving",
    "netmapPort"    : "netmapPort UNSPECIFIED",         "__netmapPort"           :"the default netmap device (i.e. netmap:eth0-2, netmap:ens4) for sending/receiving, no default value. When multiple tx rings exists for a device (like 10G NIC), the sender side NEEDS TO be specific on which tx ring to use to avoid out of order message on receiving side",
    "nmResetWaitSec": 2,                                "__nmResetWaitSec"       :"when starting the engine, netmap dev is reset for read and write this is the delay for it to finish",
    "schedPolicy"   : "SCHED_OTHER",                    "__schedPolicy"          :"engine thread schedule policy - check man page for allowed values",
    "schedPriority" : 0,                                "__schedPriority"        :"engine thread schedule priority - check man page for allowed values",
    "nmTxRings"     : "0",                              "__nmTxRings"            :"only effective on software ports created on the fly:how many tx rings in the device, 0 mean use default, no change",
    "nmTxSlots"     : "0",                              "__nmTxSlots"            :"only effective on software ports created on the fly:how many slots in a tx ring, 0 mean use default, no change",
    "nmRxRings"     : "0",                              "__nmRxRings"            :"only effective on software ports created on the fly:how many rx rings in the device, 0 mean use default, no change",
    "nmRxSlots"     : "0",                              "__nmRxSlots"            :"only effective on software ports created on the fly:how many slots in a rx ring, 0 mean use default, no change",
    "tx" :   
    {
         "dstEthAddr"           : "ff:ff:ff:ff:ff:ff",  "__dstEthAddr"           :"used in the ethernet frame composed by the send engine",
         "dstIp"                : "10.1.0.1",           "__dstIp"                :"used in the ethernet frame composed by the send engine",
         "dstPort"              : 1234,                 "__dstPort"              :"used in the ethernet frame composed by the send engine",
         "hmbdcName"            : "netmap-tx",          "__hmbdcName"            :"engine thread name",
         "maxSendBatch"         : 60,                   "__maxSendBatch"         :"up to how many messages to send in a batch (within one udp packet)",
         "mtu"                  : 1500,                 "__mtu"                  :"mtu, check ifconfig output for this value for each NIC in use",
         "nmOpenFlags"          : 4096,                 "__nmOpenFlags"          :"flags when open the netmap device for sending, default to be 0x1000, which is NETMAP_NO_TX_POLL",
         "outBufferSizePower2"  : 0,                    "__outBufferSizePower2"  :"2^outBufferSizePower2 is the number of message that can be buffered in the engine, default 0 means automatically calculated based on 1MB as the low bound",
         "sendBytesBurst"       : 0,                    "__sendBytesBurst"       :"rate control for how many bytes can be sent in a burst, us the OS buffer size (131071) as reference, 0 means no rate control.",
         "sendBytesPerSec"      : 110000000,            "__sendBytesPerSec"      :"rate control for how many bytes per second - it is turned off by sendBytesBurst==0.",
         "srcEthAddr"           : "00:00:00:00:00:00",  "__srcEthAddr"           :"used in the ethernet frame composed by the send engine",
         "srcIp"                : "10.0.0.1",           "__srcIp"                :"used in the ethernet frame composed by the send engine",
         "srcPort"              : 1234,                 "__srcPort"              :"used in the ethernet frame composed by the send engine",
         "ttl"                  : 1,                    "__ttl"                  :"the switch hop number"
    },
    "rx" :
    {
         "busyWait"             : true,                 "__busyWait"             :"when true, busy wait for the packet to arrive",
         "hmbdcName"            : "netmap-rx",          "__hmbdcName"            :"engine thread name",
         "nmOpenFlags"          : 0,                    "__nmOpenFlags"          :"flags when open the netmap device for recving",
         "pollWaitTimeMillisec" : 10,                   "__pollWaitTimeMillisec" :"when busyWait == false, this is the time limit used for each polling "
    }
}
)|";
}}}

