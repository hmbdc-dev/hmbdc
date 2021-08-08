#include "hmbdc/Copyright.hpp"
#pragma once

namespace hmbdc { namespace tips { namespace rnetmap {
/**
 * the send and recv engine config parameters and its default values used in this module
 */
constexpr char const*  const DefaultUserConfig = R"|(
{
    "doChecksum"    : false,                            "__doChecksum"                   :"calculating checksum when sending or receiving",
    "loopback"      : false,                            "__loopback"                     :"set this to true if processes within the same host need to communicate, not effective when using loopback interface 127.0.0.1.",    
    "mtu"           : 1500,                             "__mtu"                          :"mtu, check ifconfig output for this value for each NIC in use",
    "nagling"       : false,                            "__nagling"                      :"should the backup tcp channel do nagling",
    "netmapPort"    : "netmapPort UNSPECIFIED",         "__netmapPort"                   :"the netmap device (i.e. netmap:eth0-2, netmap:ens4) for sending/receiving, no default value. When multiple tx rings exists for a device (like 10G NIC), the sender side must be specific on which tx ring to use",
    "nmResetWaitSec": 2,                                "__nmResetWaitSec"               :"when starting the engine, netmap dev is reset for read and write this is the delay for it to finish",
    "nmRxRings"     : "0",                              "__nmRxRings"                    :"only effective on software ports created on the fly:how many rx rings in the device, 0 mean use default, no change",
    "nmRxSlots"     : "0",                              "__nmRxSlots"                    :"only effective on software ports created on the fly:how many slots in a rx ring, 0 mean use default, no change",
    "nmTxRings"     : "0",                              "__nmTxRings"                    :"only effective on software ports created on the fly:how many tx rings in the device, 0 mean use default, no change",
    "nmTxSlots"     : "0",                              "__nmTxSlots"                    :"only effective on software ports created on the fly:how many slots in a tx ring, 0 mean use default, no change",
    "schedPolicy"   : "SCHED_OTHER",                    "__schedPolicy"                  :"engine thread schedule policy - check man page for allowed values",
    "schedPriority" : 0,                                "__schedPriority"                :"engine thread schedule priority - check man page for allowed values",
    "tcpIfaceAddr"  : "tcpIfaceAddr UNSPECIFIED",       "__tcpIfaceAddr"                 :"ip address for the NIC interface for TCP (backup) traffic IO, it needs  to be a different one from the netmapPort NIC. no default provided",
    "tcpPort"       : 0,                                "__tcpPort"                      :"tcp port number used as backup communication channel - 0 means let the OS pick",

    "tx" :   
    {
         "dstEthAddr"                    : "ff:ff:ff:ff:ff:ff",   "__dstEthAddr"                  :"used in the ethernet frame composed by the send engine",
         "dstIp"                         : "255.255.255.255",     "__dstIp"                       :"used in the ethernet frame composed by the send engine",
         "dstPort"                       : 1234,                  "__dstPort"                     :"used in the ethernet frame composed by the send engine",
         "hmbdcName"                     : "rnetmap-tx",          "__hmbdcName"                   :"engine thread name",
         "maxSendBatch"                  : 60,                    "__maxSendBatch"                :"up to how many messages to send in a batch (within one udp packet)",
         "minRecvToStart"                : 0,                     "__minRecvToStart"              :"start send when there are that many recipients (processes) online, otherwise hold the message in buffer - NOTE: buffer might get full and blocking", 
         "netRoundtripLatencyMicrosec"   : 40,                    "__netRoundtripLatencyMicrosec" :"the estmated round multicast network trip time",
         "nmOpenFlags"                   : 4096,                  "__nmOpenFlags"                 :"flags when open the netmap device for sending, default to be 0x1000, which is NETMAP_NO_TX_POLL",
         "outBufferSizePower2"           : 0,                     "__outBufferSizePower2"         :"2^outBufferSizePower2 is the number of message that can be buffered in the engine, default 0 means automatically calculated based on 512KB as the low bound",
         "recvReportDelayMicrosec"       : 1000,                  "__recvReportDelayMicrosec"     :"recv end needs to report the recved msg seq every so often",
         "replayHistoryForNewRecv"       : 0,                     "__replayHistoryForNewRecv"     :"when a new recipient comes up, it will get the previous N (default 0) messages replayed (just) for it. if N >= out buffer size (see outBufferSizePower2), the out buffer will keep all messages and any new recipients will get every message ever sent, the out buffer needs to be big enough to hold all sent messages though",
         "sendBytesBurst"                : 131071,                "__sendBytesBurst"              :"rate control for how many bytes can be sent in a burst, us the OS buffer size (131071) as reference, 0 means no rate control.",
         "sendBytesPerSec"               : 120000000,             "__sendBytesPerSec"             :"rate control for how many bytes per second - it is turned off by sendBytesBurst==0.",
         "srcEthAddr"                    : "00:00:00:00:00:00",   "__srcEthAddr"                  :"used in the ethernet frame composed by the send engine",
         "srcIp"                         : "tcpIfaceAddr",        "__srcIp"                       :"used in the ethernet frame composed by the send engine and by hmbdc. default points to the tcpIfaceAddr",
         "srcPort"                       : 1234,                  "__srcPort"                     :"used in the ethernet frame composed by the send engine",
         "tcpSendBufferBytes"            : 0,                     "__tcpSendBufferBytes"          :"OS buffer byte size for outgoing tcp, 0 means OS default value",
         "ttl"                           : 1,                     "__ttl"                         :"the switch hop number",
         "typeTagAdvertisePeriodSeconds" : 1,                     "__typeTagAdvertisePeriodSeconds" :"send engine advertise the type tags it covers every so often",
         "waitForSlowReceivers"          : true,                  "__waitForSlowReceivers"        :"when true, a slow receiver on the network subscribe to the message might slow down the sender and other recv engines since the sender needs to wait for it; when false, the slow receiver would be disconnected when it is detected to be slow. in that case the receiver will receive a disconnect message and it by default will reconnect some messages could be lost before the reconnection is done."
    },
    "rx" :
    {
         "allowRecvWithinProcess"        : false,       "__allowRecvWithinProcess"       :"if receiving from the local process, this is an independent application level check from the OS level loopback",
         "busyWait"                      : true,        "__busyWait"                     :"when true, busy wit for the packet to arrive",
         "cmdBufferSizePower2"           : 10,          "__cmdBufferSizePower2"          :"2^cmdBufferSizePower2 is the engine command buffer size - rarely need to change",
         "hmbdcName"                     : "rnetmap-rx","__hmbdcName"                    :"engine thread name",
         "maxTcpReadBytes"               : 131072,      "__maxTcpReadBytes"              :"up to how many bytes to read in from the backup channel each time",
         "nmOpenFlags"                   : 0,           "__nmOpenFlags"                  :"flags when open the netmap device for recving",
         "pollWaitTimeMillisec"          : 10,          "__pollWaitTimeMillisec"         :"when busyWait == false, this is the time limit used for each polling ",
         "tcpRecvBufferBytes"            : 0,           "__tcpRecvBufferBytes"           :"OS buffer byte size for incoming tcp, 0 means OS default value"
    }
}
)|";
}}}

