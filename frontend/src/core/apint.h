#pragma once

#include "common.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <string>

namespace vexel {

// Frontend-owned exact integer used for literal parsing, representability checks,
// and compile-time integer evaluation beyond 64-bit host limits.
class APInt {
public:
    APInt() = default;
    APInt(int64_t value);
    APInt(uint64_t value);
    explicit APInt(const boost::multiprecision::cpp_int& value);

    static APInt parse_integer_literal(const std::string& lexeme, const SourceLocation& loc);

    std::string to_string() const;

    bool is_zero() const;
    bool is_negative() const;

    bool fits_u64() const;
    bool fits_i64() const;
    uint64_t to_u64() const;
    int64_t to_i64() const;

    bool fits_unsigned(uint64_t bits) const;
    bool fits_signed(uint64_t bits) const;

    APInt wrapped_unsigned(uint64_t bits) const;
    APInt wrapped_signed(uint64_t bits) const;

    const boost::multiprecision::cpp_int& raw() const { return value_; }
    boost::multiprecision::cpp_int& raw() { return value_; }

    friend bool operator==(const APInt& a, const APInt& b) { return a.value_ == b.value_; }
    friend bool operator!=(const APInt& a, const APInt& b) { return a.value_ != b.value_; }
    friend bool operator<(const APInt& a, const APInt& b) { return a.value_ < b.value_; }
    friend bool operator<=(const APInt& a, const APInt& b) { return a.value_ <= b.value_; }
    friend bool operator>(const APInt& a, const APInt& b) { return a.value_ > b.value_; }
    friend bool operator>=(const APInt& a, const APInt& b) { return a.value_ >= b.value_; }

    APInt operator-() const;
    APInt operator+(const APInt& other) const;
    APInt operator-(const APInt& other) const;
    APInt operator*(const APInt& other) const;
    APInt operator/(const APInt& other) const;
    APInt operator%(const APInt& other) const;
    APInt operator&(const APInt& other) const;
    APInt operator|(const APInt& other) const;
    APInt operator^(const APInt& other) const;
    APInt operator~() const;
    APInt operator<<(uint64_t shift) const;
    APInt operator>>(uint64_t shift) const;

private:
    boost::multiprecision::cpp_int value_{0};
};

} // namespace vexel

