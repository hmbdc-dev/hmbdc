#include "hmbdc/Copyright.hpp"
#pragma once

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#undef NETMAP_WITH_LIBS

#include "hmbdc/tips/rnetmap/Transport.hpp"
#include "hmbdc/tips/rnetmap/Messages.hpp"
#include "hmbdc/tips/reliable/BackupSendServerT.hpp"
#include "hmbdc/app/Base.hpp"
#include "hmbdc/comm/eth/Misc.h"
#include "hmbdc/comm/inet/Misc.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"


#include <boost/bind.hpp>
#include <memory>

#include <iostream>

#include <netinet/ether.h>      /* ether_aton */
#include <linux/if_packet.h>    /* sockaddr_ll */
#include <ifaddrs.h>    /* getifaddrs */

#include <poll.h> 

namespace hmbdc { namespace tips { namespace rnetmap {

namespace nmsendtransport_detail {

using Buffer = hmbdc::pattern::LockFreeBufferT<2>;
using ToCleanupAttQueue = reliable::ToCleanupAttQueue;

struct NmSendTransport 
: Transport {
    NmSendTransport(hmbdc::app::Config const& cfg
        , size_t maxMessageSize
        , Buffer& buffer
        , hmbdc::time::Rater& rater
        , ToCleanupAttQueue& toCleanupAttQueue)
    : Transport(cfg)
    , nmd_(nullptr)
    , virtHeader_(0)
    , srcEthAddr_{{0}}
    , dstEthAddr_{{0}}
    , doChecksum_(config_.getExt<bool>("doChecksum"))
    , maxMessageSize_(maxMessageSize)
    , buffer_(buffer)
    , rater_(rater)
    , toCleanupAttQueue_(toCleanupAttQueue)
    , maxSendBatch_(config_.getExt<size_t>("maxSendBatch"))
    , adPending_(false)
    , seqAlertPending_(false)
    , seqAlert_(nullptr)
    , startSending_(false) {
        if (maxMessageSize_ > mtu_) {
            HMBDC_THROW(std::out_of_range, "mtu needs to >= " << maxMessageSize_);
        }
        if (maxMessageSize_ > TransportMessageHeader::maxPayloadSize()) {
            HMBDC_THROW(std::out_of_range, "maxMessageSize_ needs to <=" << TransportMessageHeader::maxPayloadSize());
        }

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

        auto nmport = cfg.getExt<std::string>("netmapPort");
        nmd_ = nm_open(nmport.c_str(), &baseNmd
            , cfg.getExt<uint64_t>("nmOpenFlags"), NULL);
        if (!nmd_) {
            HMBDC_THROW(std::runtime_error, "cannot open " << nmport);
        }
        if (nmd_->first_tx_ring != nmd_->last_tx_ring) {
            HMBDC_THROW(std::out_of_range
                , "multiple tx rings exist on " << nmport
                    << ". use more specific netmapPort. for example: netmap::p2p1-2 ");
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
        auto srcIpStr = config_.getExt<std::string>("srcIp");
        if (srcIpStr == "tcpIfaceAddr") {
            srcIpStr = hmbdc::comm::inet::getLocalIpMatchMask(config_.getExt<std::string>("tcpIfaceAddr"));
        }
        initializePacket(&precalculatedPacketHead_
            , config_.getExt<uint16_t>("ttl")
            , srcIpStr
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
        struct netmap_ring * txring = NETMAP_TXRING(nmd_->nifp, nmd_->first_tx_ring);
        txring->head = txring->cur = txring->tail;

        auto addr = seqAlertBuf_;
        auto h = new (addr) TransportMessageHeader;
        new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<SeqAlert>();
        h->messagePayloadLen = sizeof(app::MessageWrap<SeqAlert>);
        h->setSeq(std::numeric_limits<HMBDC_SEQ_TYPE>::max());
        seqAlert_ = &(h->wrapped<SeqAlert>());
    }

    ~NmSendTransport(){
        nm_close(nmd_);
    }

    void startSend(){
        startSending_ = true;
    }

    template <typename AdvertisingMessages>
    void setAds(AdvertisingMessages const& ads) {
        decltype(adBufs_) newAdBufs;
        for (auto const& ad : ads) {
            newAdBufs.emplace_back();
            auto addr = newAdBufs.rbegin()->data();
            auto h = new (addr) TransportMessageHeader;
            new (addr + sizeof(TransportMessageHeader)) 
                app::MessageWrap<TypeTagBackupSource>(ad);
            h->messagePayloadLen = sizeof(app::MessageWrap<TypeTagBackupSource>);
            h->setSeq(std::numeric_limits<HMBDC_SEQ_TYPE>::max());
        }
        std::swap(adBufs_, newAdBufs);
    }

    void setAdPending() {
        adPending_ = true;
    }

    void setSeqAlertPending() {
        seqAlertPending_ = true;
    }

    void runOnce(size_t sessionCount) HMBDC_RESTRICT {
        struct netmap_ring * txring = NETMAP_TXRING(nmd_->nifp, nmd_->first_tx_ring);
        if (!nm_ring_empty(txring)) {
            sendPackets(txring, sessionCount);
        }
        if (hmbdc_unlikely(ioctl(nmd_->fd, NIOCTXSYNC, NULL) < 0)) {
            HMBDC_THROW(std::runtime_error, "IO error");
        }
    }

private:
    struct nm_desc *nmd_;
    int virtHeader_; //v hdr len
    ether_addr srcEthAddr_;
    ether_addr dstEthAddr_;
    hmbdc::comm::eth::pkt precalculatedPacketHead_;
    bool doChecksum_;

    size_t maxMessageSize_;

    Buffer& buffer_;
    hmbdc::time::Rater& HMBDC_RESTRICT rater_;
    ToCleanupAttQueue& HMBDC_RESTRICT toCleanupAttQueue_;
    size_t maxSendBatch_;
    std::vector<std::array<char, sizeof(TransportMessageHeader)
        + sizeof(app::MessageWrap<TypeTagBackupSource>)>> adBufs_;
    bool adPending_;
    char seqAlertBuf_[sizeof(TransportMessageHeader) +  sizeof(app::MessageWrap<SeqAlert>)];
    bool seqAlertPending_;
    SeqAlert* seqAlert_;
    bool startSending_;

    void sendPackets(struct netmap_ring * HMBDC_RESTRICT ring, size_t sessionCount) HMBDC_RESTRICT {
        uint32_t cur = ring->cur;
        size_t slotRemaining = 0;
        bool slotNotInited = true;
        auto batch = maxSendBatch_;
        hmbdc::comm::eth::pkt* currentPktPtr = nullptr;
        size_t slotWireSize = 0;
        struct netmap_slot *slot = &ring->slot[cur];
        typename Buffer::iterator begin, end;
        auto limit = maxSendBatch_ * ((ring->tail - ring->cur) % ring->num_slots);
        buffer_.peek(0, begin, end, limit);
        if (hmbdc_unlikely(startSending_ && !sessionCount)) {
            buffer_.wasteAfterPeek(0u, end - begin);
            begin = end;
        }
        auto it = begin;
        for(;;) {
            bool processBuffer = false;
            TransportMessageHeader* th = nullptr;
            size_t wireSize = 0;
            if (hmbdc_unlikely(adPending_)) {
                for (auto& adBuf : adBufs_) {
                    th = reinterpret_cast<TransportMessageHeader*>(adBuf.data());
                    wireSize += sizeof(adBuf);
                }
                adPending_ = false;
            } else if (it != end && startSending_) {
                th = reinterpret_cast<TransportMessageHeader*>(*it);
                wireSize = th->wireSize();
                processBuffer = true;
            } else if (hmbdc_unlikely(seqAlertPending_)) {
                if (begin == end) {
                    th = reinterpret_cast<TransportMessageHeader*>(seqAlertBuf_);
                    wireSize = sizeof(seqAlertBuf_);
                }
                seqAlertPending_ = false;
            } 
            //else wireSize == 0
            bool raterOk = wireSize && (!processBuffer || rater_.check(wireSize));
            if (!raterOk) break;
            char *p = NETMAP_BUF(ring, slot->buf_idx);
            if (slotNotInited) {
                slotRemaining = std::min((size_t)ring->nr_buf_size, mtu_);
                auto headWireSize = (uint16_t)(
                    sizeof(ether_header) + sizeof(::ip) + sizeof(udphdr) + virtHeader_
                );
                memcpy(p, ((char*)&precalculatedPacketHead_) + sizeof(hmbdc::comm::eth::virt_header) 
                    - virtHeader_, headWireSize);
                currentPktPtr = (hmbdc::comm::eth::pkt*)(p + virtHeader_ - sizeof(hmbdc::comm::eth::virt_header));
                slotRemaining -= headWireSize;
                slotWireSize = headWireSize;
                slotNotInited = false;
            }
            if (slotRemaining >= wireSize) {
                if (hmbdc_likely(processBuffer)) {
                     if (hmbdc_unlikely(th->typeTag() == app::MemorySeg::typeTag)) {
                        auto l = (int)(wireSize - th->wireSizeMemorySeg());
                        memcpy(p + slotWireSize, th->wireBytes(), l);
                        memcpy(p + slotWireSize + l, th->wireBytesMemorySeg(), th->wireSizeMemorySeg());
                    } else {
                        if (hmbdc_unlikely(th->typeTag() == app::StartMemorySegTrain::typeTag)) {
                            auto& trainHead = th->template wrapped<app::StartMemorySegTrain>();
                            auto itActual = it; itActual.seq_ += trainHead.segCount + 1;
                            auto actual = static_cast<TransportMessageHeader*>(*itActual);
                            toCleanupAttQueue_.push_back(std::make_tuple(itActual.seq_
                                , &actual->template wrapped<app::hasMemoryAttachment>()
                                , trainHead.att.afterConsumedCleanupFunc));
                        }
                        memcpy(p + slotWireSize, th, (int)wireSize);
                    }

                    seqAlert_->expectSeq = it.seq_ + 1;
                    ++it;
                    rater_.commit();
                } else {
                    memcpy(p + slotWireSize, th, (int)wireSize);
                }
                slotRemaining -= wireSize;
                slotWireSize += wireSize;
                batch--;
            } else {
                batch = 0; //this batch is done
            }
            if (!batch || it == end) {
                size_t wireSizeExcludingHead = slotWireSize 
                    - (sizeof(ether_header) + sizeof(::ip) + sizeof(udphdr) + virtHeader_);
                updatePacket(currentPktPtr, wireSizeExcludingHead, doChecksum_);
                slot->len = slotWireSize;
                cur = nm_ring_next(ring, cur);
                slot = &ring->slot[cur];
                slotWireSize = 0;
                if (cur == ring->tail || !raterOk) break;
                //new slot now
                batch = maxSendBatch_;
                slotNotInited = true;
            }
            if (it == end) break;
        }

        if (slotWireSize) {
            size_t wireSizeExcludingHead = slotWireSize 
                - (sizeof(ether_header) + sizeof(::ip) + sizeof(udphdr) + virtHeader_);
            updatePacket(currentPktPtr, wireSizeExcludingHead, doChecksum_);
            slot->len = slotWireSize;
            cur = nm_ring_next(ring, cur);
        }

        ring->head = ring->cur = cur;
        buffer_.wasteAfterPeek(0u, it - begin);
    }

    void getMacAddresses() {
        auto nmport = config_.getExt<std::string>("netmapPort");

        if (strncmp(nmport.c_str(), "vale", 4) == 0) return;
        
        if (nmport.find_first_of(":") == std::string::npos) {
            HMBDC_THROW(std::runtime_error
                , "wrong netmapPort format " << nmport << " (examples: netmap:eth0, netmap:eth0-0)");
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
    static
    void initializePacket(hmbdc::comm::eth::pkt *pkt, int ttl, std::string srcIpStr, std::string dstIpStr
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

    static 
    void updatePacket(hmbdc::comm::eth::pkt *packet, size_t payloadWireSize, bool doChecksum = true) {
        packet->ipv4.ip.ip_len += payloadWireSize; //already has sizeof(ip) + sizeof(udphdr);
        packet->ipv4.ip.ip_len = ntohs(packet->ipv4.ip.ip_len);
        if (doChecksum) {
            packet->ipv4.ip.ip_sum = hmbdc::comm::eth::wrapsum(
                hmbdc::comm::eth::checksum(&packet->ipv4.ip, sizeof(packet->ipv4.ip), 0));
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
};

#pragma GCC diagnostic pop
} //nmsendtransport_detail
using NmSendTransport = nmsendtransport_detail::NmSendTransport;
}}}
