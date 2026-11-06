#include "vx_runtime.h"
uint8_t vx_mapper_slot;
static uint8_t mapper_first, mapper_ready;
#ifdef __SDCC
uint8_t vx_mapper_transfer(uint8_t segment, uint16_t offset, uint8_t value, uint8_t write) VX_ABI VX_RESIDENT;
#elif VX_MAPPER_SEGMENTS
static uint8_t mapper_memory[VX_MAPPER_SEGMENTS * 16384UL];
#endif

void vx_mapper_init(uint8_t slot, uint8_t first_segment, uint8_t count) VX_ABI VX_RESIDENT {
    if (mapper_ready || !VX_MAPPER_SEGMENTS || count != VX_MAPPER_SEGMENTS ||
        (uint16_t)first_segment + count > 256u) { vx_trap(22); return; }
    vx_mapper_slot = slot; mapper_first = first_segment; mapper_ready = 1;
}
static uint8_t mapper_check(uint32_t offset) VX_ABI VX_RESIDENT {
    if (!mapper_ready || offset >= VX_MAPPER_SEGMENTS * 16384UL) { vx_trap(22); return 0; }
    return 1;
}
uint8_t vx_mapper_read(uint32_t offset) VX_ABI VX_RESIDENT {
    if (!mapper_check(offset)) return 0;
#ifdef __SDCC
    return vx_mapper_transfer((uint8_t)(mapper_first + (offset >> 14)), (uint16_t)(offset & 16383UL), 0, 0);
#elif VX_MAPPER_SEGMENTS
    return mapper_memory[offset];
#else
    return 0;
#endif
}
void vx_mapper_write(uint32_t offset, uint8_t value) VX_ABI VX_RESIDENT {
    if (!mapper_check(offset)) return;
#ifdef __SDCC
    (void)vx_mapper_transfer((uint8_t)(mapper_first + (offset >> 14)), (uint16_t)(offset & 16383UL), value, 1);
#elif VX_MAPPER_SEGMENTS
    mapper_memory[offset] = value;
#else
    (void)value;
#endif
}
