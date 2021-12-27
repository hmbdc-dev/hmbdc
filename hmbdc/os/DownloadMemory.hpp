#include "hmbdc/Copyright.hpp"
#pragma once 

#include <fstream>
#include <stdint.h>
#include <stdlib.h>


namespace hmbdc { namespace os {
struct DownloadMemory {
    DownloadMemory()
    : addr_(nullptr)
    , fullLen_(0)
    , len_(0) {
    }

    void* open(size_t len) {
        addr_ = (char*)memalign(SMP_CACHE_BYTES, len);
        if (addr_) {
            fullLen_ = len;
            len_ = 0;
            return addr_;
        } 
        return nullptr;
    }

    explicit operator bool() const {
        return addr_;
    }

    bool writeDone() const {
        return fullLen_ == len_;
    }

    size_t write(void const* mem, size_t l) {
        auto wl = std::min(l, fullLen_ - len_);
        if (addr_) {
            memcpy(addr_ + len_, mem, wl);
        }
        len_ += wl;
        return wl;
    }

    size_t fullLen() const {
        return fullLen_;
    }

    void abort() {
        free(addr_);
        close();
    }

    void close() {
        addr_ = nullptr;
        fullLen_ = 0;
        len_ = 0;
    }

    DownloadMemory(DownloadMemory const&) = delete;
    DownloadMemory& operator = (DownloadMemory const&) = delete;
    ~DownloadMemory() {
        free(addr_);
    }

private:
    char* addr_;
    size_t fullLen_;
    size_t len_;

};

}}
