#pragma once

namespace hmbdc { namespace tips { namespace rmcast {
/**
 * the send and recv engine config parameters and its default values used in this module
 */
constexpr char const*  const DefaultUserConfig = R"|(
{
    "ifaceAddr"         : "127.0.0.1",                  "__ifaceAddr"                   :"ip address for the NIC interface for multicast IO, 0.0.0.0/0 pointing to the first intereface that is not a loopback",
    "loopback"          : false,                        "__loopback"                    :"set this to true if processes within the same host need to communicate using, not effective when using loopback interface 127.0.0.1.",
    "mcastAddr"         : "232.43.212.236",             "__mcastAddr"                   :"multicast address for IO",
    "mcastPort"         : 4322,                         "__mcastPort"                   :"multicast port to for IO",
    "mtu"               : 1500,                         "__mtu"                         :"mtu, check ifconfig output for this value for each NIC in use",
    "nagling"           : false,                        "__nagling"                     :"should the backup tcp channel do nagling",
    "schedPolicy"       : "SCHED_OTHER",                "__schedPolicy"                 :"engine thread schedule policy - check man page for allowed values",
    "schedPriority"     : 0,                            "__schedPriority"               :"engine thread schedule priority - check man page for allowed values",
    "tcpIfaceAddr"      : "ifaceAddr",                  "__tcpIfaceAddr"                :"ip address for the NIC interface for TCP (backup) traffic IO, default points to the same as ifaceAddr",
    "tcpPort"           : 0,                            "__tcpPort"                     :"tcp port number used as backup communication channel - 0 means let the OS pick",

    "tx" :                               
    {
        "hmbdcName"                     : "rmcast-tx",  "__hmbdcName"                   :"engine thread name",
        "maxSendBatch"                  : 60,           "__maxSendBatch"                :"up to how many messages to send in a batch (within one udp packet)",
        "minRecvToStart"                : 0,            "__minRecvToStart"              :"start send when there are that many recipients (processes) online, otherwise hold the message in buffer - NOTE: buffer might get full and blocking",
        "netRoundtripLatencyMicrosec"   : 40,           "__netRoundtripLatencyMicrosec" :"the estmated round multicast network trip time",
        "outBufferSizePower2"           : 0,            "__outBufferSizePower2"         :"2^outBufferSizePower2 is the number of message that can be buffered in the engine, default 0 means automatically calculated based on 128KB as the low bound",
        "recvReportDelayMicrosec"       : 1000,         "__recvReportDelayMicrosec"     :"recv end needs to report the recved msg seq every so often",
        "replayHistoryForNewRecv"       : 0,            "__replayHistoryForNewRecv"     :"when a new recipient comes up, it will get the previous N (default 0) messages replayed (just) for it. if N >= out buffer size (see outBufferSizePower2), the out buffer will keep all messages and any new recipients will get every message ever sent, the out buffer needs to be big enough to hold all sent messages though",        
        "sendBytesBurst"                : 0,            "__sendBytesBurst"              :"rate control for how many bytes can be sent in a burst, us the OS buffer size (131071) as reference, 0 means no rate control.",
        "sendBytesPerSec"               : 120000000,    "__sendBytesPerSec"             :"rate control for how many bytes per second - it is turned off by sendBytesBurst==0. user is STRONGLY recommneded to use rate control to garantee QoS if the throughput could be so high and saturate the network",
        "tcpSendBufferBytes"            : 0,            "__tcpSendBufferBytes"          :"OS buffer byte size for outgoing tcp, 0 means OS default value",
        "ttl"                           : 1,            "__ttl"                         :"the switch hop number",
        "typeTagAdvertisePeriodSeconds" : 1,            "__typeTagAdvertisePeriodSeconds" :"send engine advertise the message tags it covers every so often",
        "udpSendBufferBytes"            : 0,            "__udpSendBufferBytes"          :"OS buffer byte size for outgoing udp, 0 means OS default value",
        "waitForSlowReceivers"          : true,         "__waitForSlowReceivers"        :"when true, a slow receiver on the network subscribe to the message might slow down the sender and other recv engines since the sender needs to wait for it; when false, the slow receiver would be disconnected when it is detected to be slow. in that case the receiver will receive a disconnect message and it by default will reconnect some messages could be lost before the reconnection is done."
    },
    "rx" :                               
    {
        "allowRecvWithinProcess"        : false,        "__allowRecvWithinProcess"      :"if receiving from the local process, this is an independent application level check from the OS level loopback",
        "cmdBufferSizePower2"           : 10,           "__cmdBufferSizePower2"         :"2^cmdBufferSizePower2 is the engine command buffer size - rarely need to change",
        "hmbdcName"                     : "rmcast-rx",  "__hmbdcName"                   :"thread name",
        "maxTcpReadBytes"               : 131072,       "__maxTcpReadBytes"             :"up to how many bytes to read in from the backup channel each time",
        "tcpRecvBufferBytes"            : 0,            "__tcpRecvBufferBytes"          :"OS buffer byte size for incoming tcp, 0 means OS default value",
        "udpRecvBufferBytes"            : 0,            "__udpRecvBufferBytes"          :"OS buffer byte size for incoming udp, 0 means OS default value"
    }
}
)|";
}}}

