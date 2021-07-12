#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Client.hpp"

#include <iostream>
#include <unistd.h>

namespace hmbdc { namespace app {

template <typename Buffer>
struct StuckClientPurger
: Client<StuckClientPurger<Buffer>> {
    StuckClientPurger(uint32_t secondsBewteenPurges
        , Buffer& buffer)
    : secondsBewteenPurges_(secondsBewteenPurges)
    , secondsCurrent_(0)
    , buffer_(buffer){
    }

    void invokedCb(size_t) override {
        sleep(1);
        secondsCurrent_++;
        if (secondsBewteenPurges_ == secondsCurrent_) {
            auto res = buffer_.purge();
            if (res) {
                std::cerr << "purgedMask=" << std::hex << res << std::dec << std::endl;
            }
            buffer_.tryPut(MessageWrap<Flush>());
            secondsCurrent_ = 0;
        }
    }

    void stoppedCb(std::exception const& e) override {
        std::cerr << e.what() << std::endl;
    };

    char const* hmbdcName() const { 
        return "purger"; 
    }

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple("SCHED_IDLE", 0);
    }

private:
    uint32_t secondsBewteenPurges_;
    uint32_t secondsCurrent_;
    Buffer& buffer_;
};

}}
