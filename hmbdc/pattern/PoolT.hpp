#include "hmbdc/Copyright.hpp"
#pragma once 
#include "hmbdc/pattern/PoolConsumer.hpp"
#include <stdint.h>
#include <memory>
#include <vector>

namespace hmbdc { namespace pattern { 

template <typename Buffer = void*>
struct PoolT {
    PoolT(PoolT const&) = delete;
    PoolT& operator = (PoolT const&) = delete;
    ~PoolT();
    using ptr = std::shared_ptr<PoolT>;
    void addConsumer(PoolConsumer&, uint64_t = 0xfffffffffffffffful);
    uint32_t consumerSize() const;
    void start(uint16_t, uint64_t = 0, bool = true);
    void startAll(uint64_t = 0);
    void startThruRecycling(uint16_t, uint64_t = 0);
    void startAt(uint16_t, uint64_t, std::vector<uint16_t> const&);
    void runOnce(uint16_t);
    void stop();
    void join();
    static
    ptr create(Buffer& lfb, uint32_t maxConsumerSize) {
        return std::shared_ptr<PoolT>(
                new PoolT(lfb, maxConsumerSize)
        );
    }

private:
    PoolT(Buffer&, uint32_t);
    void* impl_;
};

}}

#include "hmbdc/pattern/PoolT.ipp"