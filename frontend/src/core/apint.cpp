#include "apint.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace vexel {

namespace {

constexpr uint32_t kLimbBits = 16;
constexpr uint32_t kLimbBase = uint32_t(1) << kLimbBits;
constexpr uint32_t kLimbMask = kLimbBase - 1;

void normalize_limbs(std::vector<uint32_t>& limbs) {
    while (!limbs.empty() && limbs.back() == 0) {
        limbs.pop_back();
    }
}

uint64_t bit_length_u32(uint32_t value) {
    uint64_t bits = 0;
    while (value != 0) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

uint64_t bit_length_limbs(const std::vector<uint32_t>& limbs) {
    if (limbs.empty()) return 0;
    return uint64_t(limbs.size() - 1) * kLimbBits + bit_length_u32(limbs.back());
}

int compare_abs_limbs(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

void add_small_inplace(std::vector<uint32_t>& limbs, uint32_t value) {
    uint64_t carry = value;
    size_t i = 0;
    while (carry != 0) {
        if (i == limbs.size()) limbs.push_back(0);
        uint64_t sum = uint64_t(limbs[i]) + carry;
        limbs[i] = uint32_t(sum & kLimbMask);
        carry = sum >> kLimbBits;
        ++i;
    }
}

void mul_small_inplace(std::vector<uint32_t>& limbs, uint32_t value) {
    if (limbs.empty() || value == 1) return;
    if (value == 0) {
        limbs.clear();
        return;
    }
    uint64_t carry = 0;
    for (uint32_t& limb : limbs) {
        uint64_t prod = uint64_t(limb) * value + carry;
        limb = uint32_t(prod & kLimbMask);
        carry = prod >> kLimbBits;
    }
    while (carry != 0) {
        limbs.push_back(uint32_t(carry & kLimbMask));
        carry >>= kLimbBits;
    }
}

uint32_t div_small_inplace(std::vector<uint32_t>& limbs, uint32_t divisor) {
    uint64_t remainder = 0;
    for (size_t i = limbs.size(); i-- > 0;) {
        uint64_t cur = (remainder << kLimbBits) | limbs[i];
        limbs[i] = uint32_t(cur / divisor);
        remainder = cur % divisor;
    }
    normalize_limbs(limbs);
    return uint32_t(remainder);
}

std::vector<uint32_t> add_abs_limbs(const std::vector<uint32_t>& a,
                                    const std::vector<uint32_t>& b) {
    const size_t n = std::max(a.size(), b.size());
    std::vector<uint32_t> out(n, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t sum = carry;
        if (i < a.size()) sum += a[i];
        if (i < b.size()) sum += b[i];
        out[i] = uint32_t(sum & kLimbMask);
        carry = sum >> kLimbBits;
    }
    if (carry != 0) out.push_back(uint32_t(carry));
    normalize_limbs(out);
    return out;
}

void sub_abs_inplace(std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t diff = int64_t(a[i]) - borrow - (i < b.size() ? int64_t(b[i]) : 0);
        if (diff < 0) {
            diff += int64_t(kLimbBase);
            borrow = 1;
        } else {
            borrow = 0;
        }
        a[i] = uint32_t(diff);
    }
    normalize_limbs(a);
}

void sub_small_inplace(std::vector<uint32_t>& limbs, uint32_t value) {
    int64_t borrow = value;
    size_t i = 0;
    while (borrow != 0 && i < limbs.size()) {
        int64_t diff = int64_t(limbs[i]) - borrow;
        if (diff < 0) {
            limbs[i] = uint32_t(diff + int64_t(kLimbBase));
            borrow = 1;
        } else {
            limbs[i] = uint32_t(diff);
            borrow = 0;
        }
        ++i;
    }
    normalize_limbs(limbs);
}

std::vector<uint32_t> sub_abs_limbs(const std::vector<uint32_t>& a,
                                    const std::vector<uint32_t>& b) {
    std::vector<uint32_t> out = a;
    sub_abs_inplace(out, b);
    return out;
}

std::vector<uint32_t> mul_abs_limbs(const std::vector<uint32_t>& a,
                                    const std::vector<uint32_t>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<uint32_t> out(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size(); ++j) {
            uint64_t accum = uint64_t(out[i + j]) + uint64_t(a[i]) * b[j] + carry;
            out[i + j] = uint32_t(accum & kLimbMask);
            carry = accum >> kLimbBits;
        }
        size_t pos = i + b.size();
        while (carry != 0) {
            if (pos == out.size()) out.push_back(0);
            uint64_t accum = uint64_t(out[pos]) + carry;
            out[pos] = uint32_t(accum & kLimbMask);
            carry = accum >> kLimbBits;
            ++pos;
        }
    }
    normalize_limbs(out);
    return out;
}

std::vector<uint32_t> shift_left_limbs(const std::vector<uint32_t>& limbs, uint64_t shift) {
    if (limbs.empty() || shift == 0) return limbs;
    const size_t limb_shift = static_cast<size_t>(shift / kLimbBits);
    const uint32_t bit_shift = uint32_t(shift % kLimbBits);
    std::vector<uint32_t> out(limb_shift, 0);
    uint64_t carry = 0;
    for (uint32_t limb : limbs) {
        uint64_t cur = (uint64_t(limb) << bit_shift) | carry;
        out.push_back(uint32_t(cur & kLimbMask));
        carry = cur >> kLimbBits;
    }
    if (carry != 0) out.push_back(uint32_t(carry));
    normalize_limbs(out);
    return out;
}

std::vector<uint32_t> shift_right_limbs(const std::vector<uint32_t>& limbs, uint64_t shift) {
    if (limbs.empty() || shift == 0) return limbs;
    const size_t limb_shift = static_cast<size_t>(shift / kLimbBits);
    if (limb_shift >= limbs.size()) return {};
    const uint32_t bit_shift = uint32_t(shift % kLimbBits);
    std::vector<uint32_t> out(limbs.size() - limb_shift, 0);
    if (bit_shift == 0) {
        for (size_t i = limb_shift; i < limbs.size(); ++i) {
            out[i - limb_shift] = limbs[i];
        }
        normalize_limbs(out);
        return out;
    }

    uint32_t carry = 0;
    const uint32_t low_mask = (uint32_t(1) << bit_shift) - 1;
    for (size_t i = limbs.size(); i-- > limb_shift;) {
        uint32_t cur = limbs[i];
        out[i - limb_shift] = (cur >> bit_shift) | (carry << (kLimbBits - bit_shift));
        carry = cur & low_mask;
    }
    normalize_limbs(out);
    return out;
}

bool test_bit_limbs(const std::vector<uint32_t>& limbs, uint64_t bit) {
    const size_t limb_index = static_cast<size_t>(bit / kLimbBits);
    if (limb_index >= limbs.size()) return false;
    const uint32_t bit_index = uint32_t(bit % kLimbBits);
    return (limbs[limb_index] & (uint32_t(1) << bit_index)) != 0;
}

void set_bit_limbs(std::vector<uint32_t>& limbs, uint64_t bit) {
    const size_t limb_index = static_cast<size_t>(bit / kLimbBits);
    const uint32_t bit_index = uint32_t(bit % kLimbBits);
    if (limb_index >= limbs.size()) limbs.resize(limb_index + 1, 0);
    limbs[limb_index] |= uint32_t(1) << bit_index;
}

void shl1_inplace(std::vector<uint32_t>& limbs) {
    if (limbs.empty()) return;
    uint32_t carry = 0;
    for (uint32_t& limb : limbs) {
        uint32_t next_carry = (limb >> (kLimbBits - 1)) & 1u;
        limb = ((limb << 1) & kLimbMask) | carry;
        carry = next_carry;
    }
    if (carry != 0) limbs.push_back(carry);
}

bool any_low_bits(const std::vector<uint32_t>& limbs, uint64_t shift) {
    const size_t full_limbs = static_cast<size_t>(shift / kLimbBits);
    const uint32_t bit_shift = uint32_t(shift % kLimbBits);
    for (size_t i = 0; i < std::min(full_limbs, limbs.size()); ++i) {
        if (limbs[i] != 0) return true;
    }
    if (bit_shift != 0 && full_limbs < limbs.size()) {
        const uint32_t mask = (uint32_t(1) << bit_shift) - 1;
        if ((limbs[full_limbs] & mask) != 0) return true;
    }
    return false;
}

void mask_top_bits_preserve_size(std::vector<uint32_t>& words, uint64_t bit_width) {
    if (bit_width == 0) {
        words.clear();
        return;
    }
    const uint32_t extra_bits = uint32_t(bit_width % kLimbBits);
    if (extra_bits != 0 && !words.empty()) {
        const uint32_t mask = (uint32_t(1) << extra_bits) - 1;
        words.back() &= mask;
    }
}

void mask_top_bits(std::vector<uint32_t>& words, uint64_t bit_width) {
    mask_top_bits_preserve_size(words, bit_width);
    normalize_limbs(words);
}

void twos_complement_negate_inplace(std::vector<uint32_t>& words, uint64_t bit_width) {
    for (uint32_t& word : words) {
        word = (~word) & kLimbMask;
    }
    add_small_inplace(words, 1);
    if (words.size() < static_cast<size_t>((bit_width + kLimbBits - 1) / kLimbBits)) {
        words.resize(static_cast<size_t>((bit_width + kLimbBits - 1) / kLimbBits), 0);
    }
    mask_top_bits_preserve_size(words, bit_width);
}

void divmod_abs_limbs(const std::vector<uint32_t>& dividend,
                     const std::vector<uint32_t>& divisor,
                     std::vector<uint32_t>& quotient,
                     std::vector<uint32_t>& remainder) {
    if (divisor.empty()) {
        throw CompileError("Division by zero in exact integer");
    }
    if (compare_abs_limbs(dividend, divisor) < 0) {
        quotient.clear();
        remainder = dividend;
        return;
    }

    quotient.clear();
    remainder.clear();
    const uint64_t bits = bit_length_limbs(dividend);
    for (uint64_t i = bits; i-- > 0;) {
        shl1_inplace(remainder);
        if (test_bit_limbs(dividend, i)) add_small_inplace(remainder, 1);
        if (compare_abs_limbs(remainder, divisor) >= 0) {
            sub_abs_inplace(remainder, divisor);
            set_bit_limbs(quotient, i);
        }
    }
    normalize_limbs(quotient);
    normalize_limbs(remainder);
}

std::vector<uint32_t> parse_hex_digits(const std::string& digits,
                                       const SourceLocation& loc) {
    std::vector<uint32_t> limbs;
    for (char c : digits) {
        mul_small_inplace(limbs, 16);
        if (c >= '0' && c <= '9') {
            add_small_inplace(limbs, uint32_t(c - '0'));
        } else if (c >= 'a' && c <= 'f') {
            add_small_inplace(limbs, uint32_t(10 + (c - 'a')));
        } else if (c >= 'A' && c <= 'F') {
            add_small_inplace(limbs, uint32_t(10 + (c - 'A')));
        } else {
            throw CompileError("Invalid hexadecimal literal: " + digits, loc);
        }
    }
    return limbs;
}

std::vector<uint32_t> parse_decimal_digits(const std::string& digits,
                                           const SourceLocation& loc) {
    std::vector<uint32_t> limbs;
    for (char c : digits) {
        if (c < '0' || c > '9') {
            throw CompileError("Invalid integer literal: " + digits, loc);
        }
        mul_small_inplace(limbs, 10);
        add_small_inplace(limbs, uint32_t(c - '0'));
    }
    return limbs;
}

uint64_t limbs_to_u64(const std::vector<uint32_t>& limbs) {
    uint64_t out = 0;
    for (size_t i = limbs.size(); i-- > 0;) {
        out <<= kLimbBits;
        out |= limbs[i];
    }
    return out;
}

} // namespace

APInt::APInt(int64_t value) {
    if (value < 0) {
        negative_ = true;
        uint64_t magnitude = value == std::numeric_limits<int64_t>::min()
                                 ? (uint64_t(1) << 63)
                                 : uint64_t(-value);
        while (magnitude != 0) {
            limbs_.push_back(uint32_t(magnitude & kLimbMask));
            magnitude >>= kLimbBits;
        }
    } else {
        uint64_t magnitude = uint64_t(value);
        while (magnitude != 0) {
            limbs_.push_back(uint32_t(magnitude & kLimbMask));
            magnitude >>= kLimbBits;
        }
    }
}

APInt::APInt(uint64_t value) {
    while (value != 0) {
        limbs_.push_back(uint32_t(value & kLimbMask));
        value >>= kLimbBits;
    }
}

APInt APInt::from_parts(bool negative, std::vector<uint32_t> limbs) {
    APInt out;
    normalize_limbs(limbs);
    out.negative_ = negative && !limbs.empty();
    out.limbs_ = std::move(limbs);
    return out;
}

APInt APInt::from_twos_complement_words(std::vector<uint32_t> words, uint64_t bit_width) {
    mask_top_bits(words, bit_width);
    if (bit_width == 0 || words.empty()) return APInt(uint64_t(0));
    const bool negative = test_bit_limbs(words, bit_width - 1);
    if (!negative) return from_parts(false, std::move(words));
    twos_complement_negate_inplace(words, bit_width);
    return from_parts(true, std::move(words));
}

uint64_t APInt::abs_bit_length() const {
    return bit_length_limbs(limbs_);
}

uint64_t APInt::signed_bit_width() const {
    if (limbs_.empty()) return 1;
    if (!negative_) return abs_bit_length() + 1;
    std::vector<uint32_t> minus_one = limbs_;
    sub_small_inplace(minus_one, 1);
    return std::max<uint64_t>(1, bit_length_limbs(minus_one) + 1);
}

std::vector<uint32_t> APInt::to_twos_complement_words(uint64_t bit_width) const {
    if (bit_width == 0) return {};
    const size_t word_count = static_cast<size_t>((bit_width + kLimbBits - 1) / kLimbBits);
    std::vector<uint32_t> words(word_count, 0);
    for (size_t i = 0; i < std::min(word_count, limbs_.size()); ++i) {
        words[i] = limbs_[i] & kLimbMask;
    }
    if (negative_) {
        twos_complement_negate_inplace(words, bit_width);
    } else {
        mask_top_bits_preserve_size(words, bit_width);
    }
    return words;
}

APInt APInt::parse_integer_literal(const std::string& lexeme, const SourceLocation& loc) {
    if (lexeme.empty()) {
        throw CompileError("Invalid empty integer literal", loc);
    }
    if (lexeme.size() > 2 && lexeme[0] == '0' && (lexeme[1] == 'x' || lexeme[1] == 'X')) {
        return from_parts(false, parse_hex_digits(lexeme.substr(2), loc));
    }
    return from_parts(false, parse_decimal_digits(lexeme, loc));
}

std::string APInt::to_string() const {
    if (limbs_.empty()) return "0";
    std::vector<uint32_t> temp = limbs_;
    std::vector<uint32_t> chunks;
    while (!temp.empty()) {
        chunks.push_back(div_small_inplace(temp, 1000000000u));
    }
    std::ostringstream os;
    if (negative_) os << '-';
    os << chunks.back();
    for (size_t i = chunks.size() - 1; i-- > 0;) {
        os << std::setw(9) << std::setfill('0') << chunks[i];
    }
    return os.str();
}

double APInt::to_double() const {
    double out = 0.0;
    for (size_t i = limbs_.size(); i-- > 0;) {
        out = (out * double(kLimbBase)) + double(limbs_[i]);
    }
    return negative_ ? -out : out;
}

std::vector<uint8_t> APInt::to_unsigned_le_bytes(size_t byte_count) const {
    if (negative_) {
        throw CompileError("Cannot extract unsigned bytes from negative exact integer");
    }
    std::vector<uint8_t> out(byte_count, 0);
    for (size_t i = 0; i < byte_count; ++i) {
        const size_t limb_index = i / 2;
        const uint32_t shift = uint32_t((i % 2) * 8);
        if (limb_index < limbs_.size()) {
            out[i] = uint8_t((limbs_[limb_index] >> shift) & 0xFFu);
        }
    }
    return out;
}

bool APInt::is_zero() const { return limbs_.empty(); }
bool APInt::is_negative() const { return negative_; }

bool APInt::fits_u64() const {
    return !negative_ && abs_bit_length() <= 64;
}

bool APInt::fits_i64() const {
    return signed_bit_width() <= 64;
}

uint64_t APInt::to_u64() const {
    if (!fits_u64()) {
        throw CompileError("Exact integer does not fit in uint64_t");
    }
    return limbs_to_u64(limbs_);
}

int64_t APInt::to_i64() const {
    if (!fits_i64()) {
        throw CompileError("Exact integer does not fit in int64_t");
    }
    if (!negative_) return int64_t(limbs_to_u64(limbs_));
    const uint64_t magnitude = limbs_to_u64(limbs_);
    if (magnitude == (uint64_t(1) << 63)) {
        return std::numeric_limits<int64_t>::min();
    }
    return -int64_t(magnitude);
}

bool APInt::fits_unsigned(uint64_t bits) const {
    return bits != 0 && !negative_ && abs_bit_length() <= bits;
}

bool APInt::fits_signed(uint64_t bits) const {
    return bits != 0 && signed_bit_width() <= bits;
}

APInt APInt::wrapped_unsigned(uint64_t bits) const {
    if (bits == 0) return APInt(uint64_t(0));
    return from_parts(false, to_twos_complement_words(bits));
}

APInt APInt::wrapped_signed(uint64_t bits) const {
    if (bits == 0) return APInt(uint64_t(0));
    return from_twos_complement_words(to_twos_complement_words(bits), bits);
}

bool operator<(const APInt& a, const APInt& b) {
    if (a.negative_ != b.negative_) return a.negative_;
    const int cmp = compare_abs_limbs(a.limbs_, b.limbs_);
    return a.negative_ ? (cmp > 0) : (cmp < 0);
}

APInt APInt::operator-() const {
    return from_parts(!negative_, limbs_);
}

APInt APInt::operator+(const APInt& other) const {
    if (negative_ == other.negative_) {
        return from_parts(negative_, add_abs_limbs(limbs_, other.limbs_));
    }
    const int cmp = compare_abs_limbs(limbs_, other.limbs_);
    if (cmp == 0) return APInt(uint64_t(0));
    if (cmp > 0) return from_parts(negative_, sub_abs_limbs(limbs_, other.limbs_));
    return from_parts(other.negative_, sub_abs_limbs(other.limbs_, limbs_));
}

APInt APInt::operator-(const APInt& other) const {
    return *this + (-other);
}

APInt APInt::operator*(const APInt& other) const {
    return from_parts(negative_ != other.negative_, mul_abs_limbs(limbs_, other.limbs_));
}

APInt APInt::operator/(const APInt& other) const {
    if (other.is_zero()) {
        throw CompileError("Division by zero in exact integer");
    }
    std::vector<uint32_t> quotient;
    std::vector<uint32_t> remainder;
    divmod_abs_limbs(limbs_, other.limbs_, quotient, remainder);
    return from_parts(negative_ != other.negative_, std::move(quotient));
}

APInt APInt::operator%(const APInt& other) const {
    if (other.is_zero()) {
        throw CompileError("Division by zero in exact integer");
    }
    std::vector<uint32_t> quotient;
    std::vector<uint32_t> remainder;
    divmod_abs_limbs(limbs_, other.limbs_, quotient, remainder);
    return from_parts(negative_, std::move(remainder));
}

APInt APInt::operator&(const APInt& other) const {
    const uint64_t width = std::max(signed_bit_width(), other.signed_bit_width());
    std::vector<uint32_t> lhs = to_twos_complement_words(width);
    std::vector<uint32_t> rhs = other.to_twos_complement_words(width);
    for (size_t i = 0; i < lhs.size(); ++i) lhs[i] &= rhs[i];
    return from_twos_complement_words(std::move(lhs), width);
}

APInt APInt::operator|(const APInt& other) const {
    const uint64_t width = std::max(signed_bit_width(), other.signed_bit_width());
    std::vector<uint32_t> lhs = to_twos_complement_words(width);
    std::vector<uint32_t> rhs = other.to_twos_complement_words(width);
    for (size_t i = 0; i < lhs.size(); ++i) lhs[i] |= rhs[i];
    return from_twos_complement_words(std::move(lhs), width);
}

APInt APInt::operator^(const APInt& other) const {
    const uint64_t width = std::max(signed_bit_width(), other.signed_bit_width());
    std::vector<uint32_t> lhs = to_twos_complement_words(width);
    std::vector<uint32_t> rhs = other.to_twos_complement_words(width);
    for (size_t i = 0; i < lhs.size(); ++i) lhs[i] ^= rhs[i];
    return from_twos_complement_words(std::move(lhs), width);
}

APInt APInt::operator~() const {
    return -(*this) - APInt(uint64_t(1));
}

APInt APInt::operator<<(uint64_t shift) const {
    return from_parts(negative_, shift_left_limbs(limbs_, shift));
}

APInt APInt::operator>>(uint64_t shift) const {
    if (shift == 0 || limbs_.empty()) return *this;
    if (!negative_) return from_parts(false, shift_right_limbs(limbs_, shift));
    std::vector<uint32_t> shifted = shift_right_limbs(limbs_, shift);
    if (any_low_bits(limbs_, shift)) add_small_inplace(shifted, 1);
    return from_parts(true, std::move(shifted));
}

} // namespace vexel
