#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/Transport.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/comm/inet/Misc.hpp"
#include "hmbdc/MetaUtils.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include <memory>
#include <type_traits>

namespace hmbdc { namespace tips { namespace rnetmap {

namespace nmrecvtransport_detail {
/**
 * @brief impl class
 * 
 * @tparam OutputBuffer type of buffer to hold resulting network messages
 * @tparam MsgArbitrator arbitrator to decide drop or keep messages, suited to arbitrate 
 * between different recv transport. By default, keeping all
 */
template <typename OutputBuffer, typename Ep2SessionDict>
struct NmRecvTransport 
: Transport {
    using SELF = NmRecvTransport;

    NmRecvTransport(hmbdc::app::Config const& cfg
        , hmbdc::pattern::MonoLockFreeBuffer& cmdBuffer
        , TypeTagSet const& subscriptions
        , Ep2SessionDict& sessionDict)
	: Transport(cfg)
    , doChecksum_(config_.getExt<bool>("doChecksum"))
    , busyWait_(true)
    , pollWaitTimeMillisec_(0)
    , data_(nullptr) 
    , cmdBuffer_(cmdBuffer)
    , bufCur_(nullptr)
    , subscriptions_(subscriptions)
    , sessionDict_(sessionDict) {
        auto nmport = config_.getExt<std::string>("netmapPort");
        busyWait_ = config_.getExt<bool>("busyWait");
        if (!busyWait_) pollWaitTimeMillisec_ = config_.getExt<int>("pollWaitTimeMillisec");
        
        struct nmreq baseNmd;
        bzero(&baseNmd, sizeof(baseNmd));
        baseNmd.nr_flags |= NR_ACCEPT_VNET_HDR;
        config_(baseNmd.nr_tx_slots, "nmTxSlots");
        config_(baseNmd.nr_rx_slots, "nmRxSlots");
        config_(baseNmd.nr_tx_rings, "nmTxRings");
        config_(baseNmd.nr_rx_rings, "nmRxRings");

        nmd_ = nm_open(nmport.c_str(), &baseNmd, config_.getExt<int>("nmOpenFlags"), NULL);
        if (!nmd_) {
            HMBDC_THROW(std::runtime_error, "cannot open " << nmport);
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

        //setting up poll - might be useful or not
        pfd_.fd = nmd_->fd;
        pfd_.events = POLLIN;
        sleep(config_.getExt<int>("nmResetWaitSec"));
        //cleanup rings
        if (hmbdc_unlikely(ioctl(nmd_->fd, NIOCRXSYNC, NULL) < 0)) {
            HMBDC_THROW(std::runtime_error, "IO error");
        }
        for (int i = nmd_->first_rx_ring; i <= nmd_->last_rx_ring; i++) {
            struct netmap_ring * rxring = NETMAP_TXRING(nmd_->nifp, i);
            if (nm_ring_empty(rxring))
                continue;
            rxring->head = rxring->cur = rxring->tail;
        }
    }

    ~NmRecvTransport(){
        nm_close(nmd_);
    }

    void runOnce() HMBDC_RESTRICT {
        syncNetmap();
        for (int i = nmd_->first_rx_ring; i <= nmd_->last_rx_ring; i++) {
            struct netmap_ring * rxring = NETMAP_RXRING(nmd_->nifp, i);
            if (nm_ring_empty(rxring))
                continue;

            recvPackets(rxring);
        }
    }

private:
    /**
     * @brief sync using busy wait or poll depending on config
     * @details it turns out busy wait performance is very poor when using vale
     * poll works mostly, but it works well only when an enough timeout is given
     * less than 10 milli wont work well
     */
    void syncNetmap() HMBDC_RESTRICT {
        if (hmbdc_likely(busyWait_)) {
            if (hmbdc_unlikely(ioctl(nmd_->fd, NIOCRXSYNC, NULL) < 0)) {
                HMBDC_THROW(std::runtime_error, "IO error");
            } else {
                return;
            }
        } else {
            auto res = poll(&pfd_, 1, pollWaitTimeMillisec_);
            if (hmbdc_unlikely( res < 0)) {
                HMBDC_THROW(std::runtime_error, "IO error errno=" << errno);
            } else {
                return;
            }
        }
    }

    void recvPackets(struct netmap_ring * HMBDC_RESTRICT ring) HMBDC_RESTRICT {
        auto cur = ring->cur;

        while(cur != ring->tail) {
            struct netmap_slot *slot = &ring->slot[cur];
            auto *buf = (uint8_t*)NETMAP_BUF(ring, slot->buf_idx);
            auto* p = reinterpret_cast<hmbdc::comm::eth::pkt*>(
                buf + virtHeader_ - sizeof(hmbdc::comm::eth::virt_header));

            if (p->ipv4.ip.ip_p == IPPROTO_UDP 
                && p->ipv4.ip.ip_off == htons(IP_DF)) {
                if (!data_) {
                    data_ = (uint8_t*)p->ipv4.body;
#pragma GCC diagnostic push
#if defined __clang__ || __GNUC_PREREQ(9,0)       
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
                    auto ip = &p->ipv4.ip;
                    senderEndpoint_ = ip->ip_src.s_addr;
                } 
                while (data_ + sizeof(TransportMessageHeader) < buf + slot->len) {
                    auto header = reinterpret_cast<TransportMessageHeader*>(data_);
                    // HMBDC_LOG_DEBUG(header->typeTag());
                    // HMBDC_LOG_DEBUG(seq);
                    if (data_ + header->wireSize() <= buf + slot->len) {
                        if (hmbdc_unlikely(header->typeTag() == TypeTagBackupSource::typeTag)) {
                            auto it = cmdBuffer_.claim();
                            char* b = static_cast<char*>(*it);
                            size_t l = header->messagePayloadLen;
                            l = std::min(cmdBuffer_.maxItemSize(), l);
                            memcpy(b, header->payload(), l);
                            auto& bts = reinterpret_cast<app::MessageWrap<TypeTagBackupSource>*>(b)->payload;
                            bts.sendFrom = senderEndpoint_;
                            cmdBuffer_.commit(it);
                        } else {
                            auto session = sessionDict_.find(senderEndpoint_);
                            if (hmbdc_unlikely(session != sessionDict_.end())) {
                                auto a = session->second->accept(header);
                                if (a == 0) {
                                    return; //wait for backup
                                } 
                            }
                        } //else drop and move to next msg
                        data_ += header->wireSize();
                    } else {
                        break;
                    }
                }
            }
            data_ = nullptr;
            cur = nm_ring_next(ring, cur);
        }
        
        ring->head = ring->cur = cur;
    }   

    static 
    bool verifyChecksum(hmbdc::comm::eth::pkt* HMBDC_RESTRICT packet, size_t payloadWireSize) {
        {
            auto tmp = packet->ipv4.ip.ip_sum;
            packet->ipv4.ip.ip_sum = 0;
            if (tmp != hmbdc::comm::eth::wrapsum(
                hmbdc::comm::eth::checksum(&packet->ipv4.ip, sizeof(packet->ipv4.ip), 0))) {
                return false;
            }
            packet->ipv4.ip.ip_sum = tmp;
        }
        
        {
            auto tmp = packet->ipv4.udp.check;
            packet->ipv4.udp.check = 0;

            auto udp = &packet->ipv4.udp;
            if (tmp != hmbdc::comm::eth::wrapsum(
                hmbdc::comm::eth::checksum(udp, sizeof(*udp), /* udp header */
                    hmbdc::comm::eth::checksum(packet->ipv4.body, payloadWireSize, /* udp payload */ 
                        hmbdc::comm::eth::checksum(&packet->ipv4.ip.ip_src, 2 * sizeof(packet->ipv4.ip.ip_src), /* pseudo header */
                            IPPROTO_UDP + (u_int32_t)ntohs(udp->len)))))) {
                return false;
            }
#pragma GCC diagnostic pop            
            packet->ipv4.udp.check = tmp;
            return true;
        }
    }

    struct nm_desc *nmd_;
    int virtHeader_; //v hdr len
    bool doChecksum_;
    struct pollfd pfd_;
    bool busyWait_;
    int pollWaitTimeMillisec_;
    uint8_t* data_;

    hmbdc::pattern::MonoLockFreeBuffer& cmdBuffer_;
    size_t maxItemSize_;
    uint32_t senderEndpoint_;
    char buf_[4*1024];
    char* bufCur_;
    TypeTagSet const& subscriptions_;
    Ep2SessionDict& sessionDict_;
};

} //nmrecvtransport_detail
template <typename OutputBuffer, typename Ep2SessionDict>
using NmRecvTransport = nmrecvtransport_detail::NmRecvTransport<OutputBuffer, Ep2SessionDict>;
}}}
