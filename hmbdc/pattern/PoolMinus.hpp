#include "hmbdc/Copyright.hpp"
#pragma once 
#include "hmbdc/pattern/PoolConsumer.hpp"
#include <stdint.h>
#include <memory>
#include <vector>

namespace hmbdc { namespace pattern { 
struct PoolMinus {
    PoolMinus(PoolMinus const&) = delete;
    PoolMinus& operator = (PoolMinus const&) = delete;
    using ptr = std::shared_ptr<PoolMinus>;
    void addConsumer(PoolConsumer&, uint64_t poolThreadAffinityIn = 0xfffffffffffffffful);
    uint32_t consumerSize() const;
    void start(uint16_t threadCount, uint64_t cpuAffinityMask = 0, bool noUse = true);
    void startThruRecycling(uint16_t threadCount, uint64_t cpuAffinityMask = 0) {
        start(threadCount, cpuAffinityMask);
    }
    void startAt(uint16_t threadCount, uint64_t cpuAffinityMask, std::vector<uint16_t> const&) {
        start(threadCount, cpuAffinityMask);
    }
    void runOnce(uint16_t threadSerialNumber);
    void stop();
    void join();
    static
    ptr create(uint32_t maxConsumerSize) {
        return std::shared_ptr<PoolMinus>(
                new PoolMinus(maxConsumerSize)
        );
    }
    ~PoolMinus();

private:
    PoolMinus(uint32_t);
    void* impl_;
};

}}