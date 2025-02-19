#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pots/Messages.hpp"
#include "hmbdc/tips/Domain.hpp"
#include "hmbdc/tips/tcpcast/Protocol.hpp"

namespace hmbdc::pots {
    /**
     * @brief reliable domain that supports reliable message delivery
     * If the system is saturated due to buffer full - the sender is blocked when calling publish until
     * buffer is freed up
     * 
     */
    struct DefaultDomain
    : tips::Domain<
        std::tuple<Message>
        , tips::ipc_property<64, 0> // 64 IPC participant processes, configured sized msg
        , tips::net_property<tips::tcpcast::Protocol, 0> // configured sized msg
    > {
        /**
         * @brief Construct a new Default Domain object
         * 
         * @param ip the local net interface used in communication - can use loopback for testing 
         * @param udpServiceAddr, udpServicePort these pair identifies a domain, set this differently if you want to isolate communications 
         * for different domains
         */
        DefaultDomain(char const* ip
            , char const* udpServiceAddr = "232.43.212.235"
            , uint16_t udpServicePort = 4322)
        : Domain(app::Config{}
            .put("ifaceAddr", ip)
            .put("udpcastListenAddr", udpServiceAddr)
            .put("udpcastListenPort", udpServicePort)
            .put("udpcastDests", std::string(udpServiceAddr) + ":" + std::to_string(udpServicePort))
        ) {}
    };

    /**
     * @brief unreliable/realtime domain that supports latest message delivery
     * If the system is saturated due to buffer full - the oldest messages are discarded to make
     * buffers avaibale for new messages so the sender is never blocked
     */
    struct RtDomain
    : tips::Domain<
        std::tuple<Message>
        , tips::ipc_property<64, 0> // 64 IPC participant processes, configured sized msg
        , tips::net_property<tips::tcpcast::Protocol, 0> // configured sized msg
        , app::BlockingContextRt<std::tuple<Message>>
    > {
        /**
         * @brief Construct a new Default Domain object
         * 
         * @param ip the local net interface used in communication - can use loopback for testing 
         * @param udpServiceAddr, udpServicePort these pair identifies a domain, set this differently if you want to isolate communications
         */
        RtDomain(char const* ip
            , char const* udpServiceAddr = "232.43.212.235"
            , uint16_t udpServicePort = 4322)
        : Domain(app::Config{}
            .put("ifaceAddr", ip)
            .put("udpcastListenAddr", udpServiceAddr)
            .put("udpcastListenPort", udpServicePort)
            .put("udpcastDests", std::string(udpServiceAddr) + ":" + std::to_string(udpServicePort))
        ) {}
    };
}
