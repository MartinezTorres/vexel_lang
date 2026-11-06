; Resident routines, SDCC Z80 ABI 0. A/B/C/D mean 8 KiB NEO windows.
    .module vx_platform
    .globl _vx_bank_b, _vx_segment_b, _vx_msx_putchar
    .globl ___sdcc_enter_ix
    .area _HOME

; SDCC size-optimized prologues call this instead of setting IX inline.
; Exchange the helper return address with the saved frame pointer, then return
; with IX and SP both addressing the old IX. No banked library is required.
___sdcc_enter_ix::
    ex (sp),ix
    push ix
    ld ix,#2
    add ix,sp
    ret

_vx_bank_b::
    ld hl,#2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ; Preserve caller interrupt state across shadow + 16-bit mapper update.
    ld a,i
    push af
    di
    ld hl,(_vx_segment_b)
    ld (_vx_segment_b),de
    ld (0x6800),de
    pop af
    jp po,00100$
    ei
00100$:
    ret

_vx_msx_putchar::
    ld hl,#2
    add hl,sp
    ld a,(hl)
    push ix
    push iy
    call 0x00a2
    pop iy
    pop ix
    ret
