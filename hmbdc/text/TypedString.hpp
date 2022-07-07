#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/text/XferableString.hpp"
    
namespace hmbdc { namespace text {
/**
 * @brief to give a transferable string type a stronger type than a string
 * 
 * @tparam NAME[] the type's name for query
 * @tparam SIZE the char limit of the string
 */
template <char const NAME[], uint16_t SIZE>
struct TypedString : XferableString<SIZE> {
    TypedString(){}
    explicit TypedString(char const* s, size_t len = SIZE)
        : XferableString<SIZE>(s, len) {}
    explicit TypedString(std::string const& s)
        : XferableString<SIZE>(s) {}
    static constexpr char const* typeName() { return NAME; }
};

}}
