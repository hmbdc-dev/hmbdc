#include "hmbdc/Copyright.hpp"
#pragma once 


#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#undef NETMAP_WITH_LIBS

#include "hmbdc/tips/netmap/Messages.hpp"
#include "hmbdc/tips/netmap/DefaultUserConfig.hpp"
#include "hmbdc/app/Base.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/comm/eth/Misc.h"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/numeric/BitMath.hpp"

#include <memory>
#include <type_traits>
#include <mutex>


#include <netinet/ether.h>      /* ether_aton */
#include <linux/if_packet.h>    /* sockaddr_ll */
#include <sys/sysctl.h> /* sysctl */
#include <ifaddrs.h>    /* getifaddrs */

#include <poll.h>    

namespace hmbdc { namespace tips { namespace netmap { 

struct NetContext;
struct Sender;

namespace sendtransportengine_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_net_queued_ts);

/**
 * @brief power a netmap port sending functions
 * @details this needs to be created using NetContext and start in an tips::Context
 * 
 */
struct SendTransportEngine 
: hmbdc::app::Client<SendTransportEngine> {
    SendTransportEngine(hmbdc::app::Config, size_t); 
    size_t bufferedMessageCount() const {
        return buffer_.remainingSize();
    }

    size_t subscribingPartyDetectedCount(uint16_t tag) const {
        return 0; //not supported
    }

    template <app::MessageTupleC Messages, typename Node>
    void advertiseFor(Node const& node, uint16_t mod, uint16_t res) {
        //only applies to connection oriented transport
    }

/**
 * @brief if the user choose no to have a Context to manage and run the engine
 * this method can be called from any thread from time to time to have the engine 
 * do its job
 * @details thread safe and non-blocking, if the engine is already being powered by a Context,
 * this method has no effect
 */
    void rotate() {
        SendTransportEngine::invokedCb(0);
    }

    void stop();

    ~SendTransportEngine();
    bool droppedCb() override;

    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        for (int i = nmd_->first_tx_ring; i <= nmd_->last_tx_ring; i++) {
            struct netmap_ring * txring = NETMAP_TXRING(nmd_->nifp, i);
            if (nm_ring_empty(txring))
                continue;

            sendPackets(txring);
        }
        if (hmbdc_unlikely(ioctl(nmd_->fd, NIOCTXSYNC, NULL) < 0)) {
            HMBDC_THROW(std::runtime_error, "IO error");
        }
    }

    void stoppedCb(std::exception const& e) override;

    char const* hmbdcName() const { 
        return this->hmbdcName_.c_str();
    }

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(this->schedPolicy_.c_str(), this->schedPriority_);
    }

    template <typename Message>
    void queue(Message&& msg) HMBDC_RESTRICT {
        auto n = 1;
        auto it = buffer_.claim(n);
        queue(it, std::forward<Message>(msg));
        buffer_.commit(it, n);
    }

    template <typename Message>
    bool tryQueue(Message&& msg) HMBDC_RESTRICT {
        auto n = 1;
        auto it = buffer_.tryClaim(n);
        if (it) {
            queue(it, std::forward<Message>(msg));
            buffer_.commit(it, n);
            return true;
        }
        return false;
    }

    template <typename M, typename... Messages>
    void queue(pattern::MonoLockFreeBuffer::iterator it, M&& msg, Messages&&... msgs) {
        using Message = typename std::decay<M>::type;
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        if (hmbdc_unlikely(sizeof(Message) > maxMessageSize_)) {
            HMBDC_THROW(std::out_of_range, "maxMessageSize too small to hold a message when constructing SendTransportEngine");        
        }
        auto s = *it;
        auto tmh = TransportMessageHeader::copyTo(s, std::forward<M>(msg));

        if constexpr (has_hmbdc_net_queued_ts<Message>::value) {
            tmh->template wrapped<Message>().hmbdc_net_queued_ts = hmbdc::time::SysTime::now();
        } else (void)tmh;
        queue(++it, std::forward<Messages>(msgs)...);
    }

    template <typename Message, typename ... Args>
    void queueInPlace(Args&&... args) HMBDC_RESTRICT {
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        if (hmbdc_unlikely(sizeof(Message) > maxMessageSize_)) {
            HMBDC_THROW(std::out_of_range, "maxMessageSize too small to hold a message when constructing SendTransportEngine");        
        }
        auto s = buffer_.claim();
        TransportMessageHeader::copyToInPlace<Message>(*s, std::forward<Args>(args)...);
        buffer_.commit(s);
    }

    // void queueBytes(uint16_t tag, void const* bytes, size_t len);
    void queue(pattern::MonoLockFreeBuffer::iterator it) {}

private:
    uint16_t outBufferSizePower2();
    void sendPackets(struct netmap_ring *);

    void getMacAddresses();
    static
    void initializePacket(hmbdc::comm::eth::pkt *, int, std::string, std::string, ether_addr, ether_addr, uint16_t, uint16_t);

    static 
    void updatePacket(hmbdc::comm::eth::pkt *, size_t, bool = true);

    hmbdc::app::Config const config_;
    std::string hmbdcName_;
    std::string schedPolicy_;
    int schedPriority_;
    size_t maxMessageSize_;


    struct nm_desc *nmd_;
    pattern::MonoLockFreeBuffer buffer_;
    int virtHeader_; //v hdr len


    ether_addr srcEthAddr_;
    ether_addr dstEthAddr_;
    hmbdc::comm::eth::pkt precalculatedPacketHead_;
    bool doChecksum_;
    hmbdc::time::Rater rater_;
    size_t maxSendBatch_;
    uint16_t mtu_;
};

} //sendtransportengine_detail

using SendTransportEngine = sendtransportengine_detail::SendTransportEngine;

}}}

namespace hmbdc { namespace tips { namespace netmap { 

namespace sendtransportengine_detail {

inline
uint16_t
SendTransportEngine::
outBufferSizePower2() {
    auto res = config_.getExt<uint16_t>("outBufferSizePower2");
    if (res) {
        return res;
    }
    res = hmbdc::numeric::log2Upper(16ul * 1024ul / (8ul + maxMessageSize_));
    HMBDC_LOG_N("auto set --outBufferSizePower2=", res);
    return res;
}

inline
SendTransportEngine::
SendTransportEngine(hmbdc::app::Config cfg, size_t maxMessageSize) 
: config_((cfg.setAdditionalFallbackConfig(hmbdc::app::Config(DefaultUserConfig))
    , cfg.resetSection("tx", false)))
, maxMessageSize_(maxMessageSize)
, nmd_(nullptr)
, buffer_(maxMessageSize + sizeof(TransportMessageHeader) + sizeof(hmbdc::app::MessageHead)
    , outBufferSizePower2())
, virtHeader_(0)
, srcEthAddr_{{0}}
, dstEthAddr_{{0}}
, doChecksum_(config_.getExt<bool>("doChecksum"))
, rater_(hmbdc::time::Duration::seconds(1u)
    , config_.getExt<size_t>("sendBytesPerSec")
    , config_.getExt<size_t>("sendBytesBurst")
    , config_.getExt<size_t>("sendBytesBurst") != 0ul //no rate control by default
) 
, maxSendBatch_(config_.getExt<size_t>("maxSendBatch")) 
, mtu_(config_.getExt<size_t>("mtu")) {
    mtu_ -= (8u + 20u);  // 8bytes udp header and 20bytes ip header
    config_(hmbdcName_, "hmbdcName")
        (schedPolicy_, "schedPolicy")
        (schedPriority_, "schedPriority")
    ;

    memcpy(&srcEthAddr_, ether_aton(config_.getExt<std::string>("srcEthAddr").c_str())
        , sizeof(srcEthAddr_));
    memcpy(&dstEthAddr_, ether_aton(config_.getExt<std::string>("dstEthAddr").c_str())
        , sizeof(dstEthAddr_));
    getMacAddresses();

    struct nmreq baseNmd;
    bzero(&baseNmd, sizeof(baseNmd));
    baseNmd.nr_flags |= NR_ACCEPT_VNET_HDR;
    config_(baseNmd.nr_tx_slots, "nmTxSlots");
    config_(baseNmd.nr_rx_slots, "nmRxSlots");
    config_(baseNmd.nr_tx_rings, "nmTxRings");
    config_(baseNmd.nr_rx_rings, "nmRxRings");

    auto nmport = config_.getExt<std::string>("netmapPort");
    uint32_t flags = config_.getExt<uint32_t>("nmOpenFlags");
    nmd_ = nm_open(nmport.c_str(), &baseNmd, flags, NULL);
    if (!nmd_) {
        HMBDC_THROW(std::runtime_error, "cannot open " << nmport);
    }

    if (nmd_->first_tx_ring != nmd_->last_tx_ring) {
        HMBDC_LOG_W("multiple tx rings exist on ", nmport, " the recv side could receive out of order messages."
              " to avoid it, use more specific netmapPort with the ring number. for example: netmap::p2p1-2");
    }

    struct nmreq req;
    memset(&req, 0, sizeof(req));
    bcopy(nmd_->req.nr_name, req.nr_name, sizeof(req.nr_name));
    req.nr_version = NETMAP_API;
    req.nr_cmd = NETMAP_VNET_HDR_GET;
    int err = ioctl(nmd_->fd, NIOCREGIF, &req);
    if (err) {
        HMBDC_THROW(std::runtime_error, "Unable to get virtio-net header length");
    }
    virtHeader_ = req.nr_arg1;

    initializePacket(&precalculatedPacketHead_
        , config_.getExt<uint16_t>("ttl")
        , config_.getExt<std::string>("srcIp")
        , config_.getExt<std::string>("dstIp")
        , srcEthAddr_
        , dstEthAddr_
        , config_.getExt<uint16_t>("srcPort")
        , config_.getExt<uint16_t>("dstPort")
    );
    sleep(config_.getExt<int>("nmResetWaitSec"));
    //cleanup rings
    if (hmbdc_unlikely(ioctl(nmd_->fd, NIOCTXSYNC, NULL) < 0)) {
        HMBDC_THROW(std::runtime_error, "IO error");
    }
    for (int i = nmd_->first_tx_ring; i <= nmd_->last_tx_ring; i++) {
        struct netmap_ring * txring = NETMAP_TXRING(nmd_->nifp, i);
        txring->head = txring->cur = txring->tail;
    }
}

inline
void 
SendTransportEngine::
stop() {
    buffer_.reset();
};

inline
SendTransportEngine::
~SendTransportEngine() {
    nm_close(nmd_);
}

/*virtual*/
inline
bool 
SendTransportEngine::
droppedCb() {
    buffer_.reset();
    return true;
}

inline
void 
SendTransportEngine::
stoppedCb(std::exception const& e) {
    HMBDC_LOG_C(e.what());
};

inline
void
SendTransportEngine::
sendPackets(struct netmap_ring * HMBDC_RESTRICT ring) HMBDC_RESTRICT {
    uint32_t cur = ring->cur;
    if (hmbdc_unlikely(cur == ring->tail)) return;
    pattern::MonoLockFreeBuffer::iterator begin, end;
    auto limit = maxSendBatch_ * ((ring->tail - cur) % ring->num_slots);
    if (hmbdc_unlikely(!(buffer_.peek(begin, end, limit)))) {
        return;
    }
    bool slotInited = false;
    auto it = begin;
    auto batch = maxSendBatch_;
    struct netmap_slot *slot = &ring->slot[cur];
    uint32_t slotLen = 0;
    char *p = NETMAP_BUF(ring, slot->buf_idx);
    hmbdc::comm::eth::pkt* currentPktPtr = (hmbdc::comm::eth::pkt*)(p + virtHeader_ 
        - sizeof(hmbdc::comm::eth::virt_header));
    uint16_t slotLenMax = std::min(mtu_, (uint16_t)ring->nr_buf_size);
    for (; it != end;) {
        auto th = reinterpret_cast<TransportMessageHeader*>(*it);
        if (hmbdc_likely(rater_.check(th->wireSize()))) {
            if (!slotInited) {
                auto wireSize = (uint16_t)(
                    sizeof(ether_header) + sizeof(ip) + sizeof(udphdr) + virtHeader_
                );
                memcpy(p, ((char*)&precalculatedPacketHead_) + sizeof(hmbdc::comm::eth::virt_header) 
                    - virtHeader_, wireSize);
                slotLen = wireSize;
                slotInited = true;
            }
            auto wireSize = th->wireSize();
            if (slotLen + wireSize <= slotLenMax) {
                memcpy(p + slotLen, th, (int)wireSize);
                slotLen += wireSize;
                rater_.commit();
                batch--;
                ++it;
            } else {
                batch = 0; //this batch is done
            }
            if (!batch) {
                slot->len = slotLen;
                size_t wireSizeExcludingHead = slotLen 
                    - (sizeof(ether_header) + sizeof(ip) + sizeof(udphdr) + virtHeader_);
                updatePacket(currentPktPtr, wireSizeExcludingHead, doChecksum_);
                cur = nm_ring_next(ring, cur);
                slotLen = 0;
                if (cur == ring->tail) break;
                slot = &ring->slot[cur];
                p = NETMAP_BUF(ring, slot->buf_idx);
                currentPktPtr = (hmbdc::comm::eth::pkt*)(p + virtHeader_ 
                    - sizeof(hmbdc::comm::eth::virt_header));
                batch = maxSendBatch_;
                slotInited = false;
            }
        } else {
            break;
        }
    }
    if (slotLen) {
        slot->len = slotLen;
        size_t wireSizeExcludingHead = slotLen 
            - (sizeof(ether_header) + sizeof(ip) + sizeof(udphdr) + virtHeader_);
        updatePacket(currentPktPtr, wireSizeExcludingHead, doChecksum_);
        cur = nm_ring_next(ring, cur);
    }

    ring->head = ring->cur = cur;
    buffer_.wasteAfterPeek(begin, it - begin, true);
}

inline
void 
SendTransportEngine::
getMacAddresses() {
    auto nmport = config_.getExt<std::string>("netmapPort");

    if (strncmp(nmport.c_str(), "vale", 4) == 0) return;
    
    if (nmport.find_first_of(":") == std::string::npos) {
        HMBDC_THROW(std::runtime_error
            , "wrong netmapPort format (examples: netmap:eth0, netmap:eth0-0)");
    }
    auto iface = nmport.substr(nmport.find_first_of(":")); 
    iface = iface.substr(1, iface.find_first_of("-^") - 1);


    struct ifaddrs *ifaphead, *ifap;
    int l = sizeof(ifap->ifa_name);

    if (getifaddrs(&ifaphead) != 0) {
        HMBDC_THROW(std::runtime_error, "getifaddrs failed for" << iface);
    }
    for (ifap = ifaphead; ifap; ifap = ifap->ifa_next) {
        struct sockaddr_ll *sll =
            (struct sockaddr_ll *)ifap->ifa_addr;
        uint8_t *mac;

        if (!sll || sll->sll_family != AF_PACKET)
            continue;
        if (strncmp(ifap->ifa_name, iface.c_str(), l) != 0)
            continue;
        mac = (uint8_t *)(sll->sll_addr);

        char srcEthAddrStr[20];
        sprintf(srcEthAddrStr, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2],
            mac[3], mac[4], mac[5]);
        memcpy(&srcEthAddr_, ether_aton(srcEthAddrStr), sizeof(srcEthAddr_)); //6 bytes
        break;
    }
    freeifaddrs(ifaphead);
    if (!ifap) {
        HMBDC_THROW(std::runtime_error, "no local interface named " << iface);
    }
}

inline
void 
SendTransportEngine::
initializePacket(hmbdc::comm::eth::pkt *pkt, int ttl, std::string srcIpStr, std::string dstIpStr
    , ether_addr srcEthAddr, ether_addr dstEthAddr, uint16_t srcPort, uint16_t dstPort) {
    struct ether_header *eh;
    struct ip *ip;
    struct udphdr *udp;
    uint32_t a, b, c, d;
    sscanf(srcIpStr.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d);
    auto srcIp = (a << 24u) + (b << 16u) + (c << 8u) + d;
    sscanf(dstIpStr.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d);
    auto dstIp = (a << 24u) + (b << 16u) + (c << 8u) + d;

    /* prepare the headers */
    eh = &pkt->eh;
    bcopy(&srcEthAddr, eh->ether_shost, 6);
    bcopy(&dstEthAddr, eh->ether_dhost, 6);

    eh->ether_type = htons(ETHERTYPE_IP);

#pragma GCC diagnostic push
#if defined __clang__ || __GNUC_PREREQ(9,0)       
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif        
    ip = &pkt->ipv4.ip;
    udp = &pkt->ipv4.udp;
    ip->ip_v = IPVERSION;
    ip->ip_hl = sizeof(*ip) >> 2;
    ip->ip_id = 0;
    ip->ip_tos = IPTOS_LOWDELAY;
    ip->ip_len = 0; //zero so chksum can happen in ip_sum
    ip->ip_id = 0;
    ip->ip_off = htons(IP_DF); /* Don't fragment */
    ip->ip_ttl = ttl;
    ip->ip_p = IPPROTO_UDP;
    ip->ip_dst.s_addr = htonl(dstIp); 
    ip->ip_src.s_addr = htonl(srcIp); 
    ip->ip_sum = 0;
    ip->ip_len = sizeof(*ip) + sizeof(udphdr); //ip->ip_len is unknown, put known part
    udp->source = htons(srcPort);
    udp->dest = htons(dstPort);
    udp->len = sizeof(udphdr); //put known part
    udp->check = 0;
    
    bzero(&pkt->vh, sizeof(pkt->vh));
}

inline
void 
SendTransportEngine::
updatePacket(hmbdc::comm::eth::pkt *packet, size_t payloadWireSize, bool doChecksum) {
    packet->ipv4.ip.ip_len += payloadWireSize; //already has sizeof(ip) + sizeof(udphdr);
    packet->ipv4.ip.ip_len = ntohs(packet->ipv4.ip.ip_len);
    if (doChecksum) {
        packet->ipv4.ip.ip_sum = hmbdc::comm::eth::wrapsum(hmbdc::comm::eth::checksum(
            &packet->ipv4.ip, sizeof(packet->ipv4.ip), 0));
    }
    
    packet->ipv4.udp.len += payloadWireSize;
    packet->ipv4.udp.len = htons(packet->ipv4.udp.len);
    if (doChecksum) {
        auto udp = &packet->ipv4.udp;
        packet->ipv4.udp.check = hmbdc::comm::eth::wrapsum(
            hmbdc::comm::eth::checksum(udp, sizeof(*udp), /* udp header */
                hmbdc::comm::eth::checksum(packet->ipv4.body, payloadWireSize, /* udp payload */ 
                    hmbdc::comm::eth::checksum(&packet->ipv4.ip.ip_src, 2 * sizeof(packet->ipv4.ip.ip_src), /* pseudo header */
                        IPPROTO_UDP + (u_int32_t)ntohs(udp->len)))));
    }
}

#pragma GCC diagnostic pop

} //sendtransportengine_detail

}}}
