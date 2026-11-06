; Resident mapper transfer, ABI 0. The adapter owns mapper page-2 selection.
; BIOS remains in page 0; code, stack and temporaries remain in page 3.
    .module vx_mapper_platform
    .globl _vx_mapper_transfer, _vx_mapper_slot, _vx_cartridge_slot
    .area _HOME
_vx_mapper_transfer::
    push ix
    ld ix,#0
    add ix,sp
    push iy
    ld a,i
    push af
    di
    ld a,4(ix)
    out (0xfe),a
    ld a,(_vx_mapper_slot)
    ld h,#0x80
    push ix
    call 0x0024
    pop ix
    di
    ld l,5(ix)
    ld a,6(ix)
    or #0x80
    ld h,a
    ld a,8(ix)
    or a
    jr z,00100$
    ld a,7(ix)
    ld (hl),a
00100$:
    ld a,(hl)
    push af
    ld a,(_vx_cartridge_slot)
    ld h,#0x80
    call 0x0024
    di
    pop bc
    pop af
    ld l,b
    pop iy
    pop ix
    jp po,00101$
    ei
00101$:
    ret
