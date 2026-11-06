#include "vx_runtime.h"
uint8_t vx_cartridge_slot;
#ifndef __SDCC
#include <stdlib.h>
#if VX_FIXED_RUNTIME
#include "vx_fixed.c"
#endif
#if VX_FLOAT_RUNTIME
#include "vx_float.c"
#endif
#if VX_MATH_RUNTIME
#include "vx_math.c"
#endif
#endif

volatile uint8_t vx_fault;
volatile uint16_t vx_segment_b;

void vx_trap(uint8_t reason) VX_ABI VX_RESIDENT {
    vx_fault = reason;
#ifdef __SDCC
    __asm di __endasm;
    for (;;) { __asm halt __endasm; }
#else
    abort();
#endif
}

#ifndef __SDCC
uint16_t vx_bank_b(uint16_t segment) {
    uint16_t old = vx_segment_b;
    vx_segment_b = segment;
    return old;
}
#endif

int32_t vx_signed(uint32_t value, uint8_t bits) VX_ABI VX_RESIDENT {
    uint32_t mask = 0xffffffffUL >> (32 - bits);
    value &= mask;
    if (value & (1UL << (bits - 1)))
        return -1L - (int32_t)(mask ^ value);
    return (int32_t)value;
}

/* Deliberately self-contained: no implicit, banked SDCC library helpers. */
uint32_t vx_mul(uint32_t a, uint32_t b) VX_ABI VX_RESIDENT {
    uint32_t result = 0;
    while (b) {
        if (b & 1) result += a;
        b >>= 1;
        a <<= 1;
    }
    return result;
}

#if VX_FIXED_RUNTIME
uint32_t vx_rescale(uint32_t a, uint8_t bits, uint8_t is_signed,
                    uint8_t from_fraction, uint8_t to_fraction) VX_ABI VX_RESIDENT {
    uint32_t mask = 0xffffffffUL >> (32 - bits);
    uint8_t negative, shift;
    a &= mask;
    negative = is_signed && (a & (1UL << (bits - 1)));
    if (negative) a = (0UL - a) & mask;
    if (to_fraction >= from_fraction) {
        shift = to_fraction - from_fraction;
        a = shift >= 32 ? 0 : a << shift;
    } else {
        shift = from_fraction - to_fraction;
        a = shift >= 32 ? 0 : a >> shift;
    }
    return negative ? 0UL - a : a;
}

/* The large fixed-point helpers share one banked ROM module. */
extern uint32_t vxi_fixed_mul(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t) VX_ABI;
extern uint32_t vxi_fixed_div(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t) VX_ABI;
#ifdef __SDCC
extern const uint16_t __ML_SEGMENT_B_vx_fixed;
#endif
uint32_t vx_fixed_mul(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI VX_RESIDENT {
    uint16_t previous;
    uint32_t result;
#ifdef __SDCC
    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_vx_fixed);
#else
    previous = vx_bank_b(0);
#endif
    result = vxi_fixed_mul(a,b,bits,is_signed,fraction);
    (void)vx_bank_b(previous);
    return result;
}

uint32_t vx_fixed_div(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI VX_RESIDENT {
    uint16_t previous;
    uint32_t result;
#ifdef __SDCC
    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_vx_fixed);
#else
    previous = vx_bank_b(0);
#endif
    result = vxi_fixed_div(a,b,bits,is_signed,fraction);
    (void)vx_bank_b(previous);
    return result;
}

#endif

#if VX_FLOAT_RUNTIME
extern uint32_t vxi_float_binary(uint32_t, uint32_t, uint8_t, uint8_t) VX_ABI;
extern uint32_t vxi_float_convert(uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) VX_ABI;
#ifdef __SDCC
extern const uint16_t __ML_SEGMENT_B_vx_float;
#endif
uint32_t vx_float_binary(uint32_t a, uint32_t b, uint8_t bits, uint8_t operation) VX_ABI VX_RESIDENT {
    uint16_t previous;
    uint32_t result;
#ifdef __SDCC
    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_vx_float);
#else
    previous = vx_bank_b(0);
#endif
    result = vxi_float_binary(a,b,bits,operation);
    (void)vx_bank_b(previous);
    return result;
}
uint32_t vx_float_convert(uint32_t raw, uint8_t from_bits, uint8_t from_kind, uint8_t from_fraction,
                          uint8_t to_bits, uint8_t to_kind, uint8_t to_fraction) VX_ABI VX_RESIDENT {
    uint16_t previous;
    uint32_t result;
#ifdef __SDCC
    previous = vx_bank_b((uint16_t)&__ML_SEGMENT_B_vx_float);
#else
    previous = vx_bank_b(0);
#endif
    result = vxi_float_convert(raw,from_bits,from_kind,from_fraction,to_bits,to_kind,to_fraction);
    (void)vx_bank_b(previous);
    return result;
}
#endif

#if VX_MATH_RUNTIME
extern uint32_t vxi_math(uint32_t, uint32_t, uint8_t, uint8_t) VX_ABI;
#ifdef __SDCC
extern const uint16_t __ML_SEGMENT_B_vx_math;
#endif
uint32_t vx_math(uint32_t a, uint32_t b, uint8_t bits, uint8_t operation) VX_ABI VX_RESIDENT {
    uint32_t result;
#ifdef __SDCC
    uint16_t previous=vx_bank_b((uint16_t)&__ML_SEGMENT_B_vx_math);
#endif
    result=vxi_math(a,b,bits,operation);
#ifdef __SDCC
    vx_bank_b(previous);
#endif
    return result;
}
#endif

uint32_t vx_divmod(uint32_t a, uint32_t b, uint8_t bits,
                   uint8_t is_signed, uint8_t remainder) VX_ABI VX_RESIDENT {
    uint32_t mask = 0xffffffffUL >> (32 - bits);
    uint32_t sign = 1UL << (bits - 1);
    uint32_t q = 0, r = 0;
    uint8_t negative_a, negative_b, i;
    a &= mask;
    b &= mask;
    if (!b) vx_trap(1);
    negative_a = is_signed && (a & sign);
    negative_b = is_signed && (b & sign);
    if (negative_a) a = (0UL - a) & mask;
    if (negative_b) b = (0UL - b) & mask;
    for (i = 0; i < 32; ++i) {
        uint8_t carry = (r >> 31) != 0;
        r = (r << 1) | (a >> 31);
        a <<= 1;
        q <<= 1;
        if (carry || r >= b) { r -= b; q |= 1; }
    }
    if (remainder) return negative_a ? (0UL - r) & mask : r;
    return negative_a != negative_b ? (0UL - q) & mask : q;
}

uint32_t vx_shift(uint32_t a, uint32_t count, uint8_t bits,
                  uint8_t right, uint8_t is_signed) VX_ABI VX_RESIDENT {
    uint32_t mask = 0xffffffffUL >> (32 - bits);
    a &= mask;
    if (count >= bits) vx_trap(2);
    if (!right) return (a << count) & mask;
    if (is_signed && (a & (1UL << (bits - 1))))
        return (a >> count) | (mask ^ (mask >> count));
    return a >> count;
}
