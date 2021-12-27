#include "hmbdc/Copyright.hpp"
#pragma once 


#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#undef NETMAP_WITH_LIBS

#include "hmbdc/tips/netmap/Messages.hpp"
#include "hmbdc/tips/netmap/DefaultUserConfig.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/app/Base.hpp"
#include "hmbdc/comm/eth/Misc.h"
#include "hmbdc/Compile.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/MetaUtils.hpp"


#include <boost/lexical_cast.hpp>
#include <memory>
#include <type_traits>
#include <mutex>

#include <netinet/ether.h>      /* ether_aton */
#include <linux/if_packet.h>    /* sockaddr_ll */
#include <ifaddrs.h>    /* getifaddrs */
#include <poll.h>    

namespace hmbdc { namespace tips { namespace netmap { 
template <typename OutBuffer>
struct RecvTransportEngine 
: app::Client<RecvTransportEngine<OutBuffer>> {
    RecvTransportEngine(app::Config cfg, OutBuffer& outBuffer)
    : config_((cfg.setAdditionalFallbackConfig(app::Config{DefaultUserConfig})
        , cfg.resetSection("rx", false)))
    , data_(nullptr)
    , outBuffer_(outBuffer)
    , maxMessageSize_(outBuffer.maxItemSize())
    , doChecksum_(config_.getExt<bool>("doChecksum"))
    , busyWait_(true)
    , pollWaitTimeMillisec_(0) {
        config_(hmbdcName_, "hmbdcName")
            (schedPolicy_, "schedPolicy")
            (schedPriority_, "schedPriority")
        ;

        busyWait_ = config_.getExt<bool>("busyWait");
        if (!busyWait_) pollWaitTimeMillisec_ = config_.getExt<int>("pollWaitTimeMillisec");

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

    void rotate() {
        RecvTransportEngine::invokedCb(0);
    }


    ~RecvTransportEngine() {
        if (nmd_) nm_close(nmd_);
    }

    void invokedCb(size_t) HMBDC_RESTRICT override  {
        syncNetmap();
        for (int i = nmd_->first_rx_ring; i <= nmd_->last_rx_ring; i++) {
            struct netmap_ring * rxring = NETMAP_RXRING(nmd_->nifp, i);
            if (nm_ring_empty(rxring))
                continue;

            recvPackets(rxring);
        }
    }

    void stoppedCb(std::exception const& e) override {
        HMBDC_LOG_C(e.what());
    };

    char const* hmbdcName() const { 
        return this->hmbdcName_.c_str();
    }

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(this->schedPolicy_.c_str(), this->schedPriority_);
    }

    template <app::MessageTupleC Messages, typename CcNode>
    void subscribeFor(CcNode const& node, uint16_t mod, uint16_t res) {
        subscriptions_.markSubsFor<Messages>(node, mod, res);
    }

    template <app::MessageC Message>
    void subscribe() {
        subscriptions_.add(Message::typeTag);
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
            auto* p = reinterpret_cast<hmbdc::comm::eth::pkt*>(buf + virtHeader_ 
                - sizeof(hmbdc::comm::eth::virt_header));

            if (hmbdc_likely(p->ipv4.ip.ip_p == IPPROTO_UDP 
                && p->ipv4.ip.ip_off == htons(IP_DF))) {
                if (!data_) data_ = (uint8_t*)p->ipv4.body;
                while (data_ + sizeof(TransportMessageHeader) < buf + slot->len) {
                    auto header = reinterpret_cast<TransportMessageHeader*>(data_);
                    if (data_ + header->wireSize() <= buf + slot->len) {
                        if (subscriptions_.check(header->typeTag())) {
                            auto l = std::min(maxMessageSize_, (size_t)header->messagePayloadLen());
                            outBuffer_.put(header->payload(), l);
                        }
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
    bool verifyChecksum(comm::eth::pkt* HMBDC_RESTRICT packet, size_t payloadWireSize) {
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
#pragma GCC diagnostic push
#ifdef __clang__        
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif  
            auto udp = &packet->ipv4.udp;
            if (tmp != hmbdc::comm::eth::wrapsum(
                hmbdc::comm::eth::checksum(udp, sizeof(*udp), /* udp header */
                    hmbdc::comm::eth::checksum(packet->ipv4.body, payloadWireSize, /* udp payload */ 
                        hmbdc::comm::eth::checksum(&packet->ipv4.ip.ip_src, 2 * sizeof(packet->ipv4.ip.ip_src), /* pseudo header */
                            IPPROTO_UDP + (u_int32_t)ntohs(udp->len)))))) {
                return false;
            }
            packet->ipv4.udp.check = tmp;
            return true;
#pragma GCC diagnostic pop
        }
    }
    app::Config config_;
    std::string hmbdcName_;
    std::string schedPolicy_;
    int schedPriority_;
    uint8_t* data_;

    OutBuffer& HMBDC_RESTRICT outBuffer_;
    size_t maxMessageSize_;
    TypeTagSet subscriptions_;


    struct nm_desc *nmd_;
    int virtHeader_; //v hdr len
    bool doChecksum_;
    struct pollfd pfd_;
    bool busyWait_;
    int pollWaitTimeMillisec_;
};

}}}

