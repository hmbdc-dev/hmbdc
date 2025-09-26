#include "hmbdc/Copyright.hpp"
#pragma once

namespace hmbdc { namespace tips { namespace tcpcast {
/**
 * the send and recv engine config parameters and its default values used in this module
 */
constexpr char const*  const DefaultUserConfig = R"|(
{
    "ifaceAddr"                         : "127.0.0.1",              "__ifaceAddr"                     :"ip address for the NIC interface for IO, 0.0.0.0/0 pointing to the first intereface that is not a loopback",
    "tcpPort"                           : 0,                        "__tcpPort"                       :"tcp port number used when send/recv messages out - 0 means let the OS pick",
    "loopback"                          : false,                    "__loopback"                      :"set this to true if processes within the same host need to communicate using tcpcast",
    "mtu"                               : 1500,                     "__mtu"                           :"mtu, check ifconfig output for this value for each NIC in use",
    "multicastBoundToIface"             : true,                     "__multicastBoundToIface"         :"when doing multicast, the outgoing and incoming traffic is bound to a specific interface(ifaceAddr)",
    "schedPolicy"                       : "SCHED_OTHER",            "__schedPolicy"                   :"engine thread schedule policy - check man page for allowed values",
    "schedPriority"                     : 0,                        "__schedPriority"                 :"engine thread schedule priority - check man page for allowed values",
    "tcpKeepAlive"                      : 0,                        "__tcpKeepAlive"                  :"drop TCP connection when host is down",
    "udpcastDests"                      : "232.43.212.235:4321",    "__udpcastDests"                  :"this is the multicast address (default matches udpcastListenAddr udpcastListenPort), it could be a list of multicast address, unicast addresses",
    "tx" :
    {
        "hmbdcName"                     : "tcpcast-tx",             "__hmbdcName"                     :"engine thread name",
        "maxSendBatch"                  : 60,                       "__maxSendBatch"                  :"up to how many messages to send in a batch (used as a hint only)",
        "minRecvToStart"                : 0,                        "__minRecvToStart"                :"start send when there are that many recipients (processes) online, otherwise hold the message in buffer - NOTE: buffer might get full and blocking", 
        "nagling"                       : false,                    "__nagling"                       :"should the tcp channel do nagling",
        "outBufferSizePower2"           : 0,                        "__outBufferSizePower2"           :"2^outBufferSizePower2 is the number of message that can be buffered in the engine, default 0 means automatically calculated based on 1MB as the low bound",
        "sendBytesBurst"                : 0,                        "__sendBytesBurst"                :"rate control for how many bytes can be sent in a burst, us the OS buffer size (131071) as reference, 0 means no rate control.",
        "sendBytesPerSec"               : 100000000,                "__sendBytesPerSec"               :"rate control for how many bytes per second - it is turned off by sendBytesBurst==0",
        "tcpSendBufferBytes"            : 0,                        "__tcpSendBufferBytes"            :"OS buffer byte size for outgoing tcp, 0 means OS default value",
        "typeTagAdvertisePeriodSeconds" : 1,                        "__typeTagAdvertisePeriodSeconds" :"send engine advertise the message types it covers every so often",
        "ttl"                           : 1,                        "__ttl"                           :"the switch hop number",
        "udpSendBufferBytes"            : 0,                        "__udpSendBufferBytes"            :"OS buffer byte size for outgoing udp, 0 means OS default value",
        "waitForSlowReceivers"          : true,                     "__waitForSlowReceivers"          :"when true, a slow receiver on the network subscribe to the message might slow down (even block) the sender and other recv engines since the sender needs to wait for it; when false, the slow receiver would be disconnected when it is detected to be slow. in that case the receiver will receive a disconnect message and it by default will reconnect. some messages could be lost before the reconnection is done."    
    },
    "rx" :
    {
        "allowRecvWithinProcess"        : false,                    "__allowRecvWithinProcess"        :"if receiving from the local process, this is an independent tipslication level check from the OS level loopback",
        "cmdBufferSizePower2"           : 10,                       "__cmdBufferSizePower2"           :"2^cmdBufferSizePower2 is the engine command buffer size - rarely need to change",
        "heartbeatPeriodSeconds"        : 1,                        "__heartbeatPeriodSeconds"        :"the recv heart beat every so often so the send side know it is alive",
        "hmbdcName"                     : "tcpcast-rx",             "__hmbdcName"                     :"thread name",
        "maxTcpReadBytes"               : 131072,                   "__maxTcpReadBytes"               :"up to how many bytes to read in each time",
        "multicastProxies"              : "",                       "__multicastProxies"              :"when multicast is not available, one or more UDP proxies can be used to replace MC functions, list its UDP ip and port here",
        "tcpRecvBufferBytes"            : 0,                        "__tcpRecvBufferBytes"            :"OS buffer byte size for incoming tcp, 0 means OS default value",
        "udpcastListenAddr"             : "232.43.212.235",         "__udpcastListenAdd"              :"the udpcast receive engine listen to this address for messages - it can be set to ifaceAddr to listen to unicast UDP messages instead of a multicast address",
        "udpcastListenPort"             : 4321,                     "__udpcastListenPort"             :"the udpcast receive engine listen to this UDP port for messages",
        "udpRecvBufferBytes"            : 0,                        "__udpRecvBufferBytes"            :"OS buffer byte size for incoming udp, 0 means OS default value"
    }       
}
)|";
}}}

