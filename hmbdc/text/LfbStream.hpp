#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/Exception.hpp"
#include <memory>
#include <thread>
#include <iostream>
#include <string.h>
#include <stdexcept>
#include <type_traits>

namespace hmbdc { namespace text {

namespace lfb_stream {

struct Streamable{
    virtual void dump(std::ostream& os) const = 0;
    virtual ~Streamable() = default;
};

namespace lfbstream_detail {

template <uint32_t SIZE_LIMIT, typename T>
struct TypedStreamable
: Streamable {
    using RT = std::decay_t<T>;
    TypedStreamable(RT const& m)
    : payload(m){}
    TypedStreamable(RT&& m)
    : payload(std::move(m)){}
    RT payload;
    virtual void dump(std::ostream& os) const override {os << payload;}
};

template<uint32_t SIZE_LIMIT, std::size_t N >
struct TypedStreamable<SIZE_LIMIT, const char (&) [N]>
: Streamable {
private:
    const char * payload;
public:
    TypedStreamable(char const* s) 
    : payload(s){}
    virtual void dump(std::ostream& os) const override {os << payload;}
};

template<uint32_t SIZE_LIMIT>
struct TypedStreamable<SIZE_LIMIT, const char* &>
: Streamable {
private:
    char payload[SIZE_LIMIT - sizeof(Streamable)];
public:
    TypedStreamable(char const* s) {
        strncpy(payload, s, sizeof(payload));
        payload[sizeof(payload) - 1] = 0;
    }
    virtual void dump(std::ostream& os) const override {os << payload;}
};

template<uint32_t SIZE_LIMIT, std::size_t N >
struct TypedStreamable<SIZE_LIMIT, char (&) [N]>
: Streamable {
private:
    char payload[SIZE_LIMIT - sizeof(Streamable)];
public:
    TypedStreamable(char const* s) {
        strncpy(payload, s, sizeof(payload));
        payload[sizeof(payload) - 1] = 0;
    }
    virtual void dump(std::ostream& os) const override {os << payload;}
};

template <uint32_t SIZE_LIMIT>
struct TypedStreamable<SIZE_LIMIT, char const*>
: Streamable {
private:
    char payload[SIZE_LIMIT - sizeof(Streamable)];
public:
    TypedStreamable(char const* m) {
        strncpy(payload, m, sizeof(payload));
        payload[sizeof(payload) - 1] = 0;
    }
    virtual void dump(std::ostream& os) const override {os << payload;}
};

} // lfbstream_detail

template <typename Buffer, typename BufferItem, uint32_t STREAMABLE_TYPE_TAG>
struct OStringStream {
private:
    using ThisT = OStringStream<Buffer, BufferItem, STREAMABLE_TYPE_TAG>;
    using BufItem = typename Buffer::value_type;
    enum {
        PAYLOAD_SIZE = sizeof(BufferItem::payload),
    };
    static_assert(PAYLOAD_SIZE % 8 == 0);

public:

    OStringStream(Buffer& q) 
    : buffer_(q){
        if (q.maxItemSize() < sizeof(BufferItem)) {
            HMBDC_THROW(std::out_of_range, 
                "buffer not able to hold item " << q.maxItemSize() << '<' << sizeof(BufferItem));
        }
    }

    virtual ~OStringStream(){}

    Buffer& buf() { return buffer_;}
    Buffer const & buf() const { return buffer_;}

    template <typename ...Args>
    ThisT& operator()(Args&&... args) {
        auto sz = sizeof...(Args);
        auto it = buffer_.claim(sz);
        stream(it, std::forward<Args>(args)...);
        buffer_.commit(it, sz);
        return *this;
    }
    
    template <typename Message>
    static void dump(std::ostream& os, Message& m, bool callStreamableDtor = true) {
        auto ptr = reinterpret_cast<Streamable*>(m.payload);
        ptr->dump(os);
        if (callStreamableDtor) ptr->~Streamable();
    }

private:
    template <typename Arg, typename ...Args>
    void stream(typename Buffer::iterator it, Arg&& arg, Args&&... args) {
        using Actual = lfbstream_detail::TypedStreamable<PAYLOAD_SIZE, Arg>;
        static_assert(sizeof(Actual) <= PAYLOAD_SIZE, "one of item too big");
        auto ptr = static_cast<BufferItem*>(*it);
        ptr->typeTag = STREAMABLE_TYPE_TAG;
        new (&ptr->payload) Actual(std::forward<Arg>(arg));
        stream(++it, std::forward<Args>(args)...);
    }
    void stream(typename Buffer::iterator) {}

    Buffer& buffer_;
};

}}}
