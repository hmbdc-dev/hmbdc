#include "hmbdc/Copyright.hpp"
#pragma once 

#include <fstream>
#include <limits>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>


namespace hmbdc { namespace os {
struct DownloadFile {
    DownloadFile()
    : fd_(-1)
    , fullLen_(0)
    , len_(0) {
    }

    bool open(char const* dir, char const* filenamePreferred
        , size_t len = std::numeric_limits<size_t>::max()) {
        std::string fullPath = std::string(dir) + "/" + std::string(filenamePreferred);
        actualName_ = fullPath;
        int postfix = 1;

        do {
            fd_ = ::open(actualName_.c_str(), O_WRONLY | O_CREAT | O_EXCL
                , S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd_ != -1) {
                fullLen_ = len;
                len_ = 0;
                return true;
            } else {
                actualName_ = fullPath + " (" + std::to_string(++postfix) + ")";
            }
        } while(postfix < 256);
        return false;
    }

    explicit operator bool() const {
        return fd_ != -1;
    }

    bool writeDone() const {
        return fullLen_ == len_;
    }

    size_t write(void const* mem, size_t l) {
        auto wl = std::min(l, fullLen_ - len_);
        auto res = wl;
        if (fd_ != -1) {
            res = ::write(fd_, mem, wl);
        }
        if (res > 0) len_ += res;
        return res;
    }

    void close() {
        ::close(fd_);
        fd_ = -1;
    }

    char const* name() const {
        return actualName_.c_str();
    }

    size_t fullLen() const {
        return fullLen_;
    }

private:
    int fd_;
    std::string actualName_;
    size_t fullLen_;
    size_t len_;

};

}}
