#include "hmbdc/Copyright.hpp"
#pragma once

namespace hmbdc { namespace tips { namespace udpcast {
/**
 * the send and recv engine config parameters and its default values used in this module
 */
constexpr char const*  const DefaultUserConfig = R"|(
{
    "ifaceAddr"                 : "127.0.0.1",              "__ifaceAddr"                   :"ip address for the NIC interface for IO, 0.0.0.0/0 pointing to the first intereface that is not a loopback (127.0.0.1)",
    "mtu"                       : 1500,                     "__mtu"                         :"mtu, check ifconfig output for this value for each NIC in use",
    "schedPolicy"               : "SCHED_OTHER",            "__schedPolicy"                 :"engine thread schedule policy - check man page for allowed values",
    "schedPriority"             : 0,                        "__schedPriority"               :"engine thread schedule priority - check man page for allowed values",
    "tx" :           
    {
        "hmbdcName"             : "udpcast-tx",             "__hmbdcName"             :"engine thread name",
        "loopback"              : false,                    "__loopback"              :"should the message be visible in local machine. not effective when using loopback interface.",
        "maxSendBatch"          : 60,                       "__maxSendBatch"          :"up to how many messages to send in a batch (within one udp packet)",
        "udpcastDests"          : "232.43.212.234:4321",    "__udpcastDests"          :"list UDP address port pairs all udpcast traffic go to (each of them), for example \"127.0.0.1:3241 192.168.0.1:3241\" - can be a mix of multicast addresses and unicast addresses",
        "outBufferSizePower2"   : 0,                        "__outBufferSizePower2"   :"2^outBufferSizePower2 is the number of message that can be buffered in the engine, default 0 means automatically calculated based on 1MB as the low bound",
        "sendBytesBurst"        : 0,                        "__sendBytesBurst"        :"rate control for how many bytes can be sent in a burst, us the OS buffer size (131071) as reference, 0 means no rate control",
        "sendBytesPerSec"       : 100000000,                "__sendBytesPerSec"       :"rate control for how many bytes per second - it is turned off by sendBytesBurst==0",
        "ttl"                   : 1,                        "__ttl"                   :"the switch hop number",
        "udpSendBufferBytes"    : 0,                        "__udpSendBufferBytes"    :"OS buffer byte size for outgoing udp, 0 means OS default value"
    },
    "rx" :                               
    {
        "hmbdcName"             : "udpcast-rx",         "__hmbdcName"               :"engine thread name",
        "udpcastListenAddr"     : "232.43.212.234",     "__udpcastListenAdd"        :"the receive engine listen to this address for messages - it can be set to ifaceAddr to listen to unicast UDP messages instead of a multicast address",
        "udpcastListenPort"     : 4321,                 "__udpcastListenPort"       :"the receive engine listen to this UDP port for messages",
        "udpRecvBufferBytes"    : 0,                    "__udpRecvBufferBytes"      :"OS buffer byte size for incoming udp, 0 means OS default value"
    }
}
)|";
}}}
