/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     32-bit ARM translation table switch
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

/* SCTLR bits */
#define SCTLR_M     0x00000001
#define SCTLR_C     0x00000004
#define SCTLR_Z     0x00000800
#define SCTLR_I     0x00001000
#define SCTLR_TRE   0x10000000
#define SCTLR_AFE   0x20000000

    TEXTAREA

//
// VOID
// ArmSwitchTranslation(
//     _In_ ULONG Ttbr0);
//
// Installs the kernel's first level table. The firmware may run with TEX
// remap or the access flag, the kernel tables need both off, which cannot
// change under a live translation. The loader is identity mapped in both,
// so the MMU goes off for the switch. Interrupts are off.
//
    LEAF_ENTRY ArmSwitchTranslation
    push    {r4-r11, lr}
    mov     r4, r0

    /* everything written so far has to reach memory before the caches go off */
    bl      HalpArmCleanDcache

    mrc     p15, 0, r5, c1, c0, 0
    bic     r6, r5, #(SCTLR_M | SCTLR_C)
    mcr     p15, 0, r6, c1, c0, 0
    isb

    /* TTBR0 alone covers all 4 GB, domain 0 is a client */
    movs    r0, #0
    mcr     p15, 0, r0, c2, c0, 2       // TTBCR
    mcr     p15, 0, r4, c2, c0, 0       // TTBR0
    movs    r0, #1
    mcr     p15, 0, r0, c3, c0, 0       // DACR
    movs    r0, #0
    mcr     p15, 0, r0, c8, c7, 0       // TLBIALL
    mcr     p15, 0, r0, c7, c5, 0       // ICIALLU
    mcr     p15, 0, r0, c7, c5, 6       // BPIALL
    dsb     sy
    isb

    bic     r5, r5, #(SCTLR_TRE | SCTLR_AFE)
    movw    r0, #(SCTLR_M | SCTLR_C | SCTLR_Z | SCTLR_I)
    orr     r5, r5, r0
    mcr     p15, 0, r5, c1, c0, 0
    isb

    pop     {r4-r11, lr}
    bx      lr
    LEAF_END ArmSwitchTranslation

//
// Cleans and invalidates every data cache level by set and way.
//
    LEAF_ENTRY HalpArmCleanDcache
    push    {r4-r11}
    dmb
    mrc     p15, 1, r0, c0, c0, 1       // CLIDR
    ubfx    r3, r0, #24, #3             // level of coherency
    lsl     r3, r3, #1
    beq     ArmCleanDcacheFinished
    movs    r10, #0
LABEL(ArmCleanDcacheLevel)
    add     r2, r10, r10, lsr #1
    lsr     r1, r0, r2
    and     r1, r1, #7
    cmp     r1, #2
    blt     ArmCleanDcacheSkip
    mcr     p15, 2, r10, c0, c0, 0      // CSSELR
    isb
    mrc     p15, 1, r1, c0, c0, 0       // CCSIDR
    and     r2, r1, #7
    add     r2, r2, #4
    movw    r4, #0x3FF
    ands    r4, r4, r1, lsr #3
    clz     r5, r4
    movw    r7, #0x7FFF
    ands    r7, r7, r1, lsr #13
LABEL(ArmCleanDcacheSet)
    mov     r9, r4
LABEL(ArmCleanDcacheWay)
    lsl     r6, r9, r5
    orr     r11, r10, r6
    lsl     r6, r7, r2
    orr     r11, r11, r6
    mcr     p15, 0, r11, c7, c14, 2     // DCCISW
    subs    r9, r9, #1
    bge     ArmCleanDcacheWay
    subs    r7, r7, #1
    bge     ArmCleanDcacheSet
LABEL(ArmCleanDcacheSkip)
    add     r10, r10, #2
    cmp     r3, r10
    bgt     ArmCleanDcacheLevel
LABEL(ArmCleanDcacheFinished)
    movs    r10, #0
    mcr     p15, 2, r10, c0, c0, 0
    dsb     sy
    isb
    pop     {r4-r11}
    bx      lr
    LEAF_END HalpArmCleanDcache

    END
