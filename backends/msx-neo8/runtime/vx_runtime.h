#ifndef VX_RUNTIME_H
#define VX_RUNTIME_H
#include <stdint.h>
#ifndef VX_FIXED_RUNTIME
#define VX_FIXED_RUNTIME 0
#endif
#ifndef VX_FLOAT_RUNTIME
#define VX_FLOAT_RUNTIME 0
#endif
#ifndef VX_MATH_RUNTIME
#define VX_MATH_RUNTIME 0
#endif
#ifndef VX_MAPPER_SEGMENTS
#define VX_MAPPER_SEGMENTS 0
#endif
#ifdef __SDCC
#define VX_ABI __sdcccall(0)
#define VX_RESIDENT __nonbanked
#else
#define VX_ABI
#define VX_RESIDENT
#endif

/* Pointers here implement the target; none are introduced into Vexel. */
extern volatile uint8_t vx_fault;
extern volatile uint16_t vx_segment_b;
void vx_trap(uint8_t reason) VX_ABI VX_RESIDENT;
uint16_t vx_bank_b(uint16_t segment) VX_ABI VX_RESIDENT;
void vx_msx_putchar(uint8_t character) VX_ABI VX_RESIDENT;
/* The embedding adapter grants exclusive ownership of consecutive physical
   RAM segments. No allocation is inferred from total installed memory. */
void vx_mapper_init(uint8_t slot, uint8_t first_segment, uint8_t count) VX_ABI VX_RESIDENT;
uint8_t vx_mapper_read(uint32_t offset) VX_ABI VX_RESIDENT;
void vx_mapper_write(uint32_t offset, uint8_t value) VX_ABI VX_RESIDENT;
int32_t vx_signed(uint32_t value, uint8_t bits) VX_ABI VX_RESIDENT;
uint32_t vx_mul(uint32_t a, uint32_t b) VX_ABI VX_RESIDENT;
uint32_t vx_rescale(uint32_t a, uint8_t bits, uint8_t is_signed,
                    uint8_t from_fraction, uint8_t to_fraction) VX_ABI VX_RESIDENT;
uint32_t vx_fixed_mul(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI VX_RESIDENT;
uint32_t vx_fixed_div(uint32_t a, uint32_t b, uint8_t bits,
                      uint8_t is_signed, uint8_t fraction) VX_ABI VX_RESIDENT;
uint32_t vx_float_binary(uint32_t a, uint32_t b, uint8_t bits, uint8_t operation) VX_ABI VX_RESIDENT;
uint32_t vx_math(uint32_t a, uint32_t b, uint8_t bits, uint8_t operation) VX_ABI VX_RESIDENT;
uint32_t vx_float_convert(uint32_t raw, uint8_t from_bits, uint8_t from_kind, uint8_t from_fraction,
                          uint8_t to_bits, uint8_t to_kind, uint8_t to_fraction) VX_ABI VX_RESIDENT;
uint32_t vx_divmod(uint32_t a, uint32_t b, uint8_t bits,
                   uint8_t is_signed, uint8_t remainder) VX_ABI VX_RESIDENT;
uint32_t vx_shift(uint32_t a, uint32_t count, uint8_t bits,
                  uint8_t right, uint8_t is_signed) VX_ABI VX_RESIDENT;
#endif
