#include "apint.h"

#include <algorithm>
#include <limits>

namespace vexel {

namespace {

boost::multiprecision::cpp_int parse_hex_digits(const std::string& digits,
                                                const SourceLocation& loc) {
    using boost::multiprecision::cpp_int;
    cpp_int value = 0;
    for (char c : digits) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value += static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value += static_cast<unsigned>(10 + (c - 'a'));
        } else if (c >= 'A' && c <= 'F') {
            value += static_cast<unsigned>(10 + (c - 'A'));
        } else {
            throw CompileError("Invalid hexadecimal literal: " + digits, loc);
        }
    }
    return value;
}

boost::multiprecision::cpp_int parse_decimal_digits(const std::string& digits,
                                                    const SourceLocation& loc) {
    using boost::multiprecision::cpp_int;
    cpp_int value = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') {
            throw CompileError("Invalid integer literal: " + digits, loc);
        }
        value *= 10;
        value += static_cast<unsigned>(c - '0');
    }
    return value;
}

} // namespace

APInt::APInt(int64_t value) : value_(value) {}
APInt::APInt(uint64_t value) : value_(value) {}
APInt::APInt(const boost::multiprecision::cpp_int& value) : value_(value) {}

APInt APInt::parse_integer_literal(const std::string& lexeme, const SourceLocation& loc) {
    if (lexeme.empty()) {
        throw CompileError("Invalid empty integer literal", loc);
    }
    if (lexeme.size() > 2 && lexeme[0] == '0' && (lexeme[1] == 'x' || lexeme[1] == 'X')) {
        return APInt(parse_hex_digits(lexeme.substr(2), loc));
    }
    return APInt(parse_decimal_digits(lexeme, loc));
}

std::string APInt::to_string() const {
    return value_.convert_to<std::string>();
}

bool APInt::is_zero() const { return value_ == 0; }
bool APInt::is_negative() const { return value_ < 0; }

bool APInt::fits_u64() const {
    if (value_ < 0) return false;
    const boost::multiprecision::cpp_int maxv = std::numeric_limits<uint64_t>::max();
    return value_ <= maxv;
}

bool APInt::fits_i64() const {
    const boost::multiprecision::cpp_int minv = std::numeric_limits<int64_t>::min();
    const boost::multiprecision::cpp_int maxv = std::numeric_limits<int64_t>::max();
    return value_ >= minv && value_ <= maxv;
}

uint64_t APInt::to_u64() const {
    if (!fits_u64()) {
        throw CompileError("Exact integer does not fit in uint64_t");
    }
    return value_.convert_to<uint64_t>();
}

int64_t APInt::to_i64() const {
    if (!fits_i64()) {
        throw CompileError("Exact integer does not fit in int64_t");
    }
    return value_.convert_to<int64_t>();
}

bool APInt::fits_unsigned(uint64_t bits) const {
    if (bits == 0) return false;
    if (value_ < 0) return false;
    using boost::multiprecision::cpp_int;
    cpp_int maxv = (cpp_int(1) << bits) - 1;
    return value_ <= maxv;
}

bool APInt::fits_signed(uint64_t bits) const {
    if (bits == 0) return false;
    using boost::multiprecision::cpp_int;
    cpp_int minv = -(cpp_int(1) << (bits - 1));
    cpp_int maxv = (cpp_int(1) << (bits - 1)) - 1;
    return value_ >= minv && value_ <= maxv;
}

APInt APInt::wrapped_unsigned(uint64_t bits) const {
    if (bits == 0) return APInt(uint64_t(0));
    using boost::multiprecision::cpp_int;
    cpp_int mod = cpp_int(1) << bits;
    cpp_int out = value_ % mod;
    if (out < 0) out += mod;
    return APInt(out);
}

APInt APInt::wrapped_signed(uint64_t bits) const {
    if (bits == 0) return APInt(uint64_t(0));
    using boost::multiprecision::cpp_int;
    APInt raw = wrapped_unsigned(bits);
    cpp_int sign_bit = cpp_int(1) << (bits - 1);
    cpp_int full = cpp_int(1) << bits;
    cpp_int v = raw.value_;
    if ((v & sign_bit) != 0) {
        v -= full;
    }
    return APInt(v);
}

APInt APInt::operator-() const { return APInt(-value_); }
APInt APInt::operator+(const APInt& other) const { return APInt(value_ + other.value_); }
APInt APInt::operator-(const APInt& other) const { return APInt(value_ - other.value_); }
APInt APInt::operator*(const APInt& other) const { return APInt(value_ * other.value_); }
APInt APInt::operator/(const APInt& other) const { return APInt(value_ / other.value_); }
APInt APInt::operator%(const APInt& other) const { return APInt(value_ % other.value_); }
APInt APInt::operator&(const APInt& other) const { return APInt(value_ & other.value_); }
APInt APInt::operator|(const APInt& other) const { return APInt(value_ | other.value_); }
APInt APInt::operator^(const APInt& other) const { return APInt(value_ ^ other.value_); }
APInt APInt::operator~() const { return APInt(~value_); }
APInt APInt::operator<<(uint64_t shift) const { return APInt(value_ << shift); }
APInt APInt::operator>>(uint64_t shift) const { return APInt(value_ >> shift); }

} // namespace vexel
