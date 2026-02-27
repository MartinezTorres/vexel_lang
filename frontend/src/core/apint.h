#pragma once

#include "common.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vexel {

// Frontend-owned exact integer used for literal parsing, representability checks,
// and compile-time integer evaluation beyond 64-bit host limits.
class APInt {
public:
    APInt() = default;
    APInt(int64_t value);
    APInt(uint64_t value);

    static APInt parse_integer_literal(const std::string& lexeme, const SourceLocation& loc);

    std::string to_string() const;
    double to_double() const;
    std::vector<uint8_t> to_unsigned_le_bytes(size_t byte_count) const;

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

    friend bool operator==(const APInt& a, const APInt& b) {
        return a.negative_ == b.negative_ && a.limbs_ == b.limbs_;
    }
    friend bool operator!=(const APInt& a, const APInt& b) { return !(a == b); }
    friend bool operator<(const APInt& a, const APInt& b);
    friend bool operator<=(const APInt& a, const APInt& b) { return !(b < a); }
    friend bool operator>(const APInt& a, const APInt& b) { return b < a; }
    friend bool operator>=(const APInt& a, const APInt& b) { return !(a < b); }

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
    static APInt from_parts(bool negative, std::vector<uint32_t> limbs);
    static APInt from_twos_complement_words(std::vector<uint32_t> words, uint64_t bit_width);

    uint64_t abs_bit_length() const;
    uint64_t signed_bit_width() const;
    std::vector<uint32_t> to_twos_complement_words(uint64_t bit_width) const;

    bool negative_ = false;
    std::vector<uint32_t> limbs_;
};

} // namespace vexel
