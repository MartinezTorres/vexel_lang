#include "vx_runtime.h"

/* IEEE binary16/binary32 using integer arithmetic only. All finite arithmetic
 * retains guard/round/sticky bits and rounds once, to nearest/even. */
typedef struct {
    uint32_t mantissa;
    int16_t exponent;
    uint8_t negative, special;
} vx_float_parts;

static uint8_t fp_fraction(uint8_t bits) VX_ABI { return bits == 16 ? 10 : 23; }
static int16_t fp_bias(uint8_t bits) VX_ABI { return bits == 16 ? 15 : 127; }
static uint32_t fp_sign(uint8_t bits) VX_ABI { return 1UL << (bits - 1); }
static uint32_t fp_infinity(uint8_t bits) VX_ABI { return bits == 16 ? 0x7c00UL : 0x7f800000UL; }
static uint32_t fp_nan(uint8_t bits) VX_ABI { return bits == 16 ? 0x7e00UL : 0x7fc00000UL; }
static uint32_t fp_jam(uint32_t value, uint16_t count) VX_ABI {
    if (!count) return value;
    if (count >= 32) return value != 0;
    return (value >> count) | ((value << (32 - count)) != 0);
}
static void fp_unpack(uint32_t raw, uint8_t bits, vx_float_parts* out) VX_ABI {
    uint8_t p = fp_fraction(bits);
    uint16_t maximum = bits == 16 ? 31 : 255;
    out->negative = (raw & fp_sign(bits)) != 0;
    out->exponent = (int16_t)((raw >> p) & maximum);
    out->mantissa = raw & ((1UL << p) - 1);
    out->special = out->exponent == maximum ? (out->mantissa ? 2 : 1) : 0;
    if (out->special) return;
    if (out->exponent) out->mantissa |= 1UL << p;
    else out->exponent = 1;
}
static void fp_normal(vx_float_parts* value, uint8_t p) VX_ABI {
    if (!value->mantissa) return;
    while (value->mantissa < (1UL << p)) { value->mantissa <<= 1; --value->exponent; }
}
static uint32_t fp_pack(uint8_t negative, int16_t exponent, uint32_t mantissa, uint8_t bits) VX_ABI {
    uint8_t p = fp_fraction(bits), low;
    uint32_t sign = negative ? fp_sign(bits) : 0;
    if (!mantissa) return sign;
    while (mantissa >= (1UL << (p + 4))) { mantissa = fp_jam(mantissa,1); ++exponent; }
    while (mantissa < (1UL << (p + 3)) && exponent > 1) { mantissa <<= 1; --exponent; }
    if (exponent < 1) { mantissa = fp_jam(mantissa,(uint16_t)(1 - exponent)); exponent = 1; }
    low = (uint8_t)(mantissa & 7); mantissa >>= 3;
    if (low > 4 || (low == 4 && (mantissa & 1))) ++mantissa;
    if (mantissa >= (1UL << (p + 1))) { mantissa >>= 1; ++exponent; }
    if (exponent >= (bits == 16 ? 31 : 255)) return sign | fp_infinity(bits);
    if (mantissa < (1UL << p)) return sign | mantissa;
    return sign | ((uint32_t)exponent << p) | (mantissa & ((1UL << p) - 1));
}
static uint32_t fp_product(uint32_t a, uint32_t b, uint8_t shift) VX_ABI {
    uint32_t high = 0, low = 0, add_high = 0, old;
    while (b) {
        if (b & 1) { old = low; low += a; high += add_high + (low < old); }
        add_high = (add_high << 1) | (a >> 31); a <<= 1; b >>= 1;
    }
    return (low >> shift) | (high << (32 - shift)) | ((low & ((1UL << shift) - 1)) != 0);
}
static uint32_t fp_quotient(uint32_t a, uint32_t b, uint8_t shift) VX_ABI {
    uint32_t high = a >> (32 - shift), low = a << shift, remainder = 0, quotient = 0;
    uint8_t i, carry;
    for (i = 0; i < 64; ++i) {
        carry = (remainder >> 31) != 0;
        remainder = (remainder << 1) | (high >> 31);
        high = (high << 1) | (low >> 31); low <<= 1; quotient <<= 1;
        if (carry || remainder >= b) { remainder -= b; quotient |= 1; }
    }
    return quotient | (remainder != 0);
}

/* 0 add, 1 subtract, 2 multiply, 3 divide, 4 remainder;
 * 5 equal, 6 unequal, 7 less, 8 less/equal, 9 greater, 10 greater/equal. */
uint32_t vxi_float_binary(uint32_t left, uint32_t right, uint8_t bits, uint8_t operation) VX_ABI {
    vx_float_parts a, b;
    uint8_t p = fp_fraction(bits), negative;
    uint32_t x, y;
    int16_t exponent;
    int8_t order;
    fp_unpack(left,bits,&a); fp_unpack(right,bits,&b);
    if (operation >= 5) {
        if (a.special == 2 || b.special == 2) return operation == 11 ? 2 : operation == 6;
        x = left & (fp_sign(bits) - 1); y = right & (fp_sign(bits) - 1);
        if (!x && !y) order = 0;
        else if (a.negative != b.negative) order = a.negative ? -1 : 1;
        else { order = x < y ? -1 : x > y ? 1 : 0; if (a.negative) order = -order; }
        if (operation == 11) return order < 0 ? 0xffffffffUL : (uint32_t)order;
        return operation == 5 ? order == 0 : operation == 6 ? order != 0 : operation == 7 ? order < 0 :
               operation == 8 ? order <= 0 : operation == 9 ? order > 0 : order >= 0;
    }
    if (a.special == 2 || b.special == 2) return fp_nan(bits);
    if (operation <= 1) {
        if (operation == 1) { b.negative = !b.negative; right ^= fp_sign(bits); }
        if (a.special || b.special) {
            if (a.special && b.special && a.negative != b.negative) return fp_nan(bits);
            return a.special ? left : right;
        }
        x = a.mantissa << 3; y = b.mantissa << 3;
        exponent = a.exponent > b.exponent ? a.exponent : b.exponent;
        x = fp_jam(x,(uint16_t)(exponent - a.exponent)); y = fp_jam(y,(uint16_t)(exponent - b.exponent));
        if (a.negative == b.negative) return fp_pack(a.negative,exponent,x+y,bits);
        if (x == y) return 0;
        return x > y ? fp_pack(a.negative,exponent,x-y,bits) : fp_pack(b.negative,exponent,y-x,bits);
    }
    negative = a.negative != b.negative;
    if (operation == 2) {
        if ((a.special && !b.special && !b.mantissa) || (b.special && !a.special && !a.mantissa)) return fp_nan(bits);
        if (a.special || b.special) return (negative ? fp_sign(bits) : 0) | fp_infinity(bits);
        if (!a.mantissa || !b.mantissa) return negative ? fp_sign(bits) : 0;
        fp_normal(&a,p); fp_normal(&b,p);
        return fp_pack(negative,(int16_t)(a.exponent+b.exponent-fp_bias(bits)),fp_product(a.mantissa,b.mantissa,p-3),bits);
    }
    if (operation == 3) {
        if ((a.special && b.special) || (!a.special && !a.mantissa && !b.special && !b.mantissa)) return fp_nan(bits);
        if (a.special || (!b.special && !b.mantissa)) return (negative ? fp_sign(bits) : 0) | fp_infinity(bits);
        if (b.special || !a.mantissa) return negative ? fp_sign(bits) : 0;
        fp_normal(&a,p); fp_normal(&b,p);
        return fp_pack(negative,(int16_t)(a.exponent-b.exponent+fp_bias(bits)),fp_quotient(a.mantissa,b.mantissa,p+3),bits);
    }
    if (a.special || (!b.special && !b.mantissa)) return fp_nan(bits);
    if (b.special || !a.mantissa) return left;
    fp_normal(&a,p); fp_normal(&b,p);
    if (a.exponent < b.exponent) return left;
    while (a.exponent > b.exponent) {
        if (a.mantissa >= b.mantissa) a.mantissa -= b.mantissa;
        a.mantissa <<= 1; --a.exponent;
    }
    if (a.mantissa >= b.mantissa) a.mantissa -= b.mantissa;
    return fp_pack(a.negative,a.exponent,a.mantissa << 3,bits);
}

/* Kinds: unsigned integer/fixed = 0, signed integer/fixed = 1,
 * float = 2, Boolean = 3. Fraction is zero for integer and float kinds. */
uint32_t vxi_float_convert(uint32_t raw, uint8_t from_bits, uint8_t from_kind, uint8_t from_fraction,
                           uint8_t to_bits, uint8_t to_kind, uint8_t to_fraction) VX_ABI {
    vx_float_parts value;
    uint8_t p, highest = 0;
    int16_t shift, exponent;
    uint32_t magnitude, scan, mask;
    if (from_kind == 2) {
        if (to_kind == 2 && from_bits == to_bits) return raw;
        fp_unpack(raw,from_bits,&value);
        if (to_kind == 3) return value.special || value.mantissa != 0;
        if (to_kind == 2) {
            if (value.special == 2) return fp_nan(to_bits);
            if (value.special) return (value.negative ? fp_sign(to_bits) : 0) | fp_infinity(to_bits);
            if (!value.mantissa) return value.negative ? fp_sign(to_bits) : 0;
            p = fp_fraction(from_bits); fp_normal(&value,p);
            shift = (int16_t)fp_fraction(to_bits) + 3 - p;
            magnitude = shift >= 0 ? value.mantissa << shift : fp_jam(value.mantissa,(uint16_t)-shift);
            exponent = value.exponent-fp_bias(from_bits)+fp_bias(to_bits);
            return fp_pack(value.negative,exponent,magnitude,to_bits);
        }
        if (value.special) { vx_trap(6); return 0; }
        shift = value.exponent-fp_bias(from_bits)-fp_fraction(from_bits)+to_fraction;
        magnitude = shift >= 32 || shift <= -32 ? 0 : shift >= 0 ? value.mantissa << shift : value.mantissa >> -shift;
        return value.negative ? 0UL - magnitude : magnitude;
    }
    mask = 0xffffffffUL >> (32 - from_bits); raw &= mask;
    value.negative = from_kind == 1 && (raw & (1UL << (from_bits - 1)));
    magnitude = value.negative ? (0UL - raw) & mask : raw;
    if (!magnitude) return 0;
    scan = magnitude;
    while (scan >>= 1) ++highest;
    p = fp_fraction(to_bits); shift = (int16_t)highest - p - 3;
    magnitude = shift >= 0 ? fp_jam(magnitude,(uint16_t)shift) : magnitude << -shift;
    exponent = (int16_t)highest + fp_bias(to_bits) - from_fraction;
    return fp_pack(value.negative,exponent,magnitude,to_bits);
}
