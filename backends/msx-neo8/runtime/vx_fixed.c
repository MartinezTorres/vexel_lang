#include "vx_runtime.h"

/* Full 64-bit intermediate represented by two 32-bit limbs. These routines
 * need no long long support or compiler-provided arithmetic library. */
uint32_t vxi_fixed_mul(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI {
    uint32_t mask = 0xffffffffUL >> (32 - bits), sign = 1UL << (bits - 1);
    uint32_t high = 0, low = 0, add_high = 0, old_low;
    uint8_t negative_a, negative_b;
    a &= mask; b &= mask;
    negative_a = is_signed && (a & sign); negative_b = is_signed && (b & sign);
    if (negative_a) a = (0UL - a) & mask;
    if (negative_b) b = (0UL - b) & mask;
    while (b) {
        if (b & 1) {
            old_low = low; low += a;
            high += add_high + (low < old_low);
        }
        b >>= 1; add_high = (add_high << 1) | (a >> 31); a <<= 1;
    }
    if (fraction == 32) low = high;
    else if (fraction) low = (low >> fraction) | (high << (32 - fraction));
    return negative_a != negative_b ? 0UL - low : low;
}

uint32_t vxi_fixed_div(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI {
    uint32_t mask = 0xffffffffUL >> (32 - bits), sign = 1UL << (bits - 1);
    uint32_t high = 0, q = 0, r = 0;
    uint8_t negative_a, negative_b, carry, i;
    a &= mask; b &= mask;
    if (!b) vx_trap(1);
    negative_a = is_signed && (a & sign); negative_b = is_signed && (b & sign);
    if (negative_a) a = (0UL - a) & mask;
    if (negative_b) b = (0UL - b) & mask;
    if (fraction == 32) { high = a; a = 0; }
    else if (fraction) { high = a >> (32 - fraction); a <<= fraction; }
    for (i = 0; i < 64; ++i) {
        carry = (r >> 31) != 0;
        r = (r << 1) | (high >> 31);
        high = (high << 1) | (a >> 31); a <<= 1; q <<= 1;
        if (carry || r >= b) { r -= b; q |= 1; }
    }
    return negative_a != negative_b ? 0UL - q : q;
}

