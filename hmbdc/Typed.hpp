#include "hmbdc/Copyright.hpp"
#pragma once
#include <iostream>

namespace hmbdc {
template <char const NAME[], typename T>
struct Typed {
	Typed(){}

    static constexpr char const* typeName() { return NAME; }

	explicit Typed(T v)
	: v_(v){}

	explicit operator T() {
		return v_;
	}

	bool operator <  (Typed t) const { return v_ <  t.v_;}
	bool operator >  (Typed t) const { return v_ >  t.v_;}
	bool operator <= (Typed t) const { return v_ <= t.v_;}
	bool operator >= (Typed t) const { return v_ >= t.v_;}
	bool operator == (Typed t) const { return v_ == t.v_;}

	Typed operator+(Typed t) const {return Typed(v_ + t.v_);}
	Typed operator-(Typed t) const {return Typed(v_ - t.v_);}
	Typed operator*(Typed t) const {return Typed(v_ * t.v_);}
	Typed operator/(Typed t) const {return Typed(v_ / t.v_);}

	Typed& operator+=(Typed t) {v_ += t.v_; return *this;}
	Typed& operator-=(Typed t) {v_ -= t.v_; return *this;}
	Typed& operator*=(Typed t) {v_ *= t.v_; return *this;}
	Typed& operator/=(Typed t) {v_ /= t.v_; return *this;}

    friend
    std::ostream& operator << (std::ostream& os, Typed const& t) {
        return os << t.v_;
    }
    friend
    std::istream& operator >> (std::istream& is, Typed& t) {
        return is >> t.v_;
    }
private:
	T v_;
};
}


