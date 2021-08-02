#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/Compile.hpp"
#include "hmbdc/Exception.hpp"

#include <boost/align/align.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <stdexcept>
#include <string>
#include <iostream>
#include <utility> 
#include <mutex>

#include <sys/mman.h>
#include <fcntl.h>           /* For O_* constants */
#include <malloc.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */

namespace hmbdc { namespace os {

/**
 * @brief helping allocating object and its aggregated objects in a continouse memory
 * @details the class working with this cannot have significant dtor (dtor that does more than
 * just freeing memory)
 */
struct BasePtrAllocator {
    enum {
        fromHeap = false,
    };
    BasePtrAllocator()
    : cur_(NULL)
    , size_(0)
    , runCtor_(false)
    {}

    BasePtrAllocator(void* base, size_t size, bool runCtor /* = true */)
    : cur_((char*)base)
    , size_(size)
    , runCtor_(runCtor)
    {}

    BasePtrAllocator(BasePtrAllocator const&) = delete;
    BasePtrAllocator& operator = (BasePtrAllocator const&) = delete;
    virtual ~BasePtrAllocator(){}

    template <typename T, typename ...Args>
    T* allocate(size_t alignment, Args&&... args) {
        auto cur = boost::alignment::align(alignment, sizeof(T), cur_, size_);
        cur_ = ((char*)cur) + sizeof(T);
        T *res;
        if (runCtor_) {
            res = ::new (cur)T(std::forward<Args>(args)...);
            msync(cur, sizeof(T), MS_SYNC);
        } else {
            res = static_cast<T*>(cur);
        }
        return res;
    }

    void * memalign(size_t alignment, size_t size) {
        auto res = boost::alignment::align(alignment, size, cur_, size_);
        cur_ = ((char*)cur_) + size_;
        return res;
    }

    template <typename T> void unallocate(T* ptr){
        if (runCtor_) {
            ptr->~T();
        }
    }
    void free(void*){}

protected:
    void set(void* base, size_t size, bool runCtor = true) {
        cur_= base;
        size_ = size;
        runCtor_ = runCtor;
    }

private:
    void* cur_;
    size_t size_;
    bool runCtor_;
};

/**
 * @brief helping allocating object and its aggregated objects in a continouse shared memory
 * @details the class working with this cannot have significant dtor (dtor that does more than
 * just freeing memory)
 */
struct ShmBasePtrAllocator
: BasePtrAllocator {
    ShmBasePtrAllocator(char const* name, size_t offset, size_t len
        , int& ownership);
    
    ShmBasePtrAllocator(char const* name, size_t offset, size_t len, int&& ownership)
    : ShmBasePtrAllocator(name, offset, len, ownership)
    {}

    ~ShmBasePtrAllocator();

    template <typename T, typename ...Args>
    T* allocate(size_t alignment, Args&&... args) {
        std::lock_guard<decltype(lock_)> g(lock_);
        auto res = BasePtrAllocator::allocate<T>(alignment
            , std::forward<Args>(args)...);
        return res;
    }

    auto& fileLock() {
        return lock_;
    }

private:
    std::string name_;
    size_t len_;
    void* addr_;
    bool own_;
    boost::interprocess::file_lock lock_;
};

/**
 * @brief similar to ShmBasePtrAllocator but using dev memory
 * @details typically PCIe BAR
 */
struct DevMemBasePtrAllocator
: BasePtrAllocator {
    DevMemBasePtrAllocator(char const* devName, size_t offset, size_t len
        , int ownership);
    ~DevMemBasePtrAllocator();

    template <typename T, typename ...Args>
    T* allocate(size_t alignment, Args&&... args) {
        std::lock_guard<decltype(lock_)> g(lock_);
        return BasePtrAllocator::allocate<T>(alignment
            , std::forward<Args>(args)...);
    }

    auto& fileLock() {
        return lock_;
    }

private:
    size_t len_;
    void* addr_;
    bool own_;
    boost::interprocess::file_lock lock_;
};

/**
 * @brief the default vanilla allocate
 */
struct DefaultAllocator {
    enum {
        fromHeap = true,
    };
    template <typename ...NoUses>
    DefaultAllocator(NoUses&& ...){}

    template <typename T, typename ...Args>
    T* allocate(size_t alignment, Args&&... args) {
        // return new T(std::forward<Args>(args)...);
        auto space = memalign(alignment, sizeof(T));
        return new (space) T(std::forward<Args>(args)...);
    }

    void* memalign(size_t alignment, size_t size) {
        auto res = ::memalign(alignment, size);
        if (!res) {
            throw std::bad_alloc();
        }
        return res;
    }

    template <typename T> 
    void 
    unallocate(T* ptr){
        ptr->~T();
        this->free(ptr);
        // delete ptr;
    }
    void free(void* ptr){
        ::free(ptr);
    }
    static DefaultAllocator& instance() {
        static DefaultAllocator ins;
        return ins;
    }
};

inline
ShmBasePtrAllocator::
ShmBasePtrAllocator(char const* name, size_t offset, size_t len, int& ownership)
: BasePtrAllocator()
, len_(len)
, addr_(NULL)
, own_(false) {
    if (name[0] != '/') {
        name_ = std::string("/") + name;
    } else {
        name_ = name;
    }

    auto devName = std::string("/dev/shm") + name_;
    size_t retry = 3;

    while (true) {
        try {
            int oflags = O_RDWR;
            int fd = -1;
            if (ownership > 0) {
                shm_unlink(name_.c_str());
                oflags |= O_CREAT | O_EXCL;
                fd = shm_open(name, oflags, 0660);
                if (fd < 0) {
                    HMBDC_THROW(std::runtime_error, 
                        "error creating fresh ipc transport errno=" << errno);
                }
            } else if (ownership < 0) {
                fd = shm_open(name, oflags, 0660);
                if (fd < 0) {
                    HMBDC_THROW(std::runtime_error, 
                        "error attaching ipc transport errno=" << errno);
                }
            } else {
                oflags |= O_CREAT | O_EXCL;
                fd = shm_open(name, oflags, 0660);
                if (fd < 0 && errno == EEXIST) {
                    oflags = O_RDWR;
                    fd = shm_open(name, oflags, 0660);
                    if (fd < 0) {
                        HMBDC_THROW(std::runtime_error, 
                            "error attching ipc transport errno=" << errno);
                    }
                    ownership = -1;
                } else if (fd >= 0) {
                    ownership = 1;
                } else {
                    HMBDC_THROW(std::runtime_error, 
                            "error creating ipc transport errno=" << errno);
                }
            }

            if (fd >= 0) {
                if (ownership > 0) {
                    own_ = true;
                    if (ftruncate(fd, len)) {
                        close(fd);
                        HMBDC_THROW(std::runtime_error, 
                            "error when sizing ipc transport errno=" << errno);

                    }
                }
                struct stat st;
                stat(devName.c_str(), &st);
                if (len != (size_t)st.st_size) {
                    close(fd);
                    HMBDC_THROW(std::runtime_error, "file not expected size - unmatch?");
                }
                void* base = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED
                    , fd, offset);
                if (base != MAP_FAILED) {
                    set(base, len, own_);
                    addr_ = base;
                    close(fd);
                    lock_ = boost::interprocess::file_lock(devName.c_str());
                    return;
                }
            }

            HMBDC_THROW(std::runtime_error
                , "error when creating file errno=" << errno);
        } catch (std::exception const& e) {
            if (--retry == 0) throw;
            sleep(1);
        }
    }
}

inline
ShmBasePtrAllocator::
~ShmBasePtrAllocator() {
    munmap(addr_, len_);
    if (own_) {
        shm_unlink(name_.c_str());
    }
}

inline
DevMemBasePtrAllocator::
DevMemBasePtrAllocator(char const* devName, size_t offset, size_t len, int ownership)
: BasePtrAllocator()
, len_(len)
, addr_(NULL){
    if (offset % getpagesize() || len_ % getpagesize()) {
        HMBDC_THROW(std::out_of_range, "address not at page boundary devName="
            << devName << "@" << offset << ':' << len_);
    }
    if (ownership == 0) {
        HMBDC_THROW(std::out_of_range, "undefined ownership devName="
            << devName << "@" << offset << ':' << len_);
    }
    int oflags = O_RDWR | O_SYNC;
    int fd = open(devName, oflags);
    addr_ = (uint32_t*)mmap(NULL, len_, PROT_READ | PROT_WRITE, MAP_SHARED
        , fd, offset);
    if (addr_ == MAP_FAILED) {
        HMBDC_THROW(std::runtime_error
            , "cannot map RegRange " << devName << '@' << offset << ':' << len_);
    }
    own_ = ownership > 0;
    set(addr_, len, own_);
    close(fd);
    lock_ = boost::interprocess::file_lock(devName);
}

inline
DevMemBasePtrAllocator::
~DevMemBasePtrAllocator() {
    munmap(addr_, len_);
}

}}

