/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AArch32 TLB, cache and firmware helpers, after the ARM64 ones
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

    TEXTAREA

//
// VOID
// HalpFlushTbEntry(
//     _In_ PVOID VirtualAddress);
//
    LEAF_ENTRY HalpFlushTbEntry
    bfc     r0, #0, #12
    dsb     ishst
    /* without the mp extensions the broadcast forms are undefined, ID_MMFR3 says */
    mrc     p15, 0, r1, c0, c1, 7       // ID_MMFR3
    ubfx    r1, r1, #12, #4
    cmp     r1, #2
    blo     HalpFlushTbLocal
    mcr     p15, 0, r0, c8, c3, 3       // TLBIMVAAIS
    mcr     p15, 0, r0, c7, c1, 6       // BPIALLIS
    b       HalpFlushTbDone
LABEL(HalpFlushTbLocal)
    mcr     p15, 0, r0, c8, c7, 1       // TLBIMVA, hal mappings are global
    mcr     p15, 0, r0, c7, c5, 6       // BPIALL
LABEL(HalpFlushTbDone)
    dsb     ish
    isb
    bx      lr
    LEAF_END HalpFlushTbEntry

//
// VOID
// HalpPublishPteWrites(VOID);
//
    LEAF_ENTRY HalpPublishPteWrites
    dsb     ishst
    isb
    bx      lr
    LEAF_END HalpPublishPteWrites

//
// VOID
// HalpSweepDcacheRange(
//     _In_ PVOID BaseAddress,
//     _In_ SIZE_T Length);
//
// Cleans and invalidates a buffer to the point of coherency, so a DMA
// master that does not snoop the caches sees it and leaves no stale line.
//
    LEAF_ENTRY HalpSweepDcacheRange
    cbz     r1, HalpSweepDcacheDone
    mrc     p15, 0, r2, c0, c0, 1       // CTR, DminLine is the log2 of words
    ubfx    r2, r2, #16, #4
    movs    r3, #4
    lsl     r3, r3, r2
    add     r1, r0, r1
    sub     r2, r3, #1
    bic     r0, r0, r2
LABEL(HalpSweepDcacheNext)
    mcr     p15, 0, r0, c7, c14, 1      // DCCIMVAC
    add     r0, r0, r3
    cmp     r0, r1
    blo     HalpSweepDcacheNext
LABEL(HalpSweepDcacheDone)
    dsb     sy
    bx      lr
    LEAF_END HalpSweepDcacheRange

//
// VOID
// HalpWaitForInterrupt(VOID);
//
    LEAF_ENTRY HalpWaitForInterrupt
    dsb     sy
    wfi
    bx      lr
    LEAF_END HalpWaitForInterrupt

//
// ULONG_PTR
// HalpCallFirmware(
//     _In_ ULONG_PTR Function,
//     _In_ ULONG_PTR Argument1,
//     _In_ ULONG_PTR Argument2,
//     _In_ ULONG_PTR Argument3,
//     _In_ BOOLEAN UseHvc);
//
// PSCI call through the SMC32 convention, the conduit comes from the firmware.
//
    LEAF_ENTRY HalpCallFirmware
    ldr     r12, [sp]
    cmp     r12, #0
    beq     HalpCallFirmwareSmc
    hvc     #0
    bx      lr
LABEL(HalpCallFirmwareSmc)
    smc     #0
    bx      lr
    LEAF_END HalpCallFirmware

//
// VOID
// HalSweepDcache(VOID);
//
// Cleans and invalidates every data cache level by set and way, the
// ARMv7 sequence from CLIDR, CSSELR and CCSIDR.
//
    LEAF_ENTRY HalSweepDcache
    push    {r4-r11}
    dmb
    mrc     p15, 1, r0, c0, c0, 1       // CLIDR
    ubfx    r3, r0, #24, #3             // level of coherency
    lsl     r3, r3, #1
    beq     HalSweepDcacheFinished
    movs    r10, #0                     // cache level << 1
LABEL(HalSweepDcacheLevel)
    add     r2, r10, r10, lsr #1
    lsr     r1, r0, r2
    and     r1, r1, #7                  // cache type of this level
    cmp     r1, #2
    blt     HalSweepDcacheSkip          // no data cache here
    mcr     p15, 2, r10, c0, c0, 0      // CSSELR
    isb
    mrc     p15, 1, r1, c0, c0, 0       // CCSIDR
    and     r2, r1, #7
    add     r2, r2, #4                  // log2 of the line size
    movw    r4, #0x3FF
    ands    r4, r4, r1, lsr #3          // ways - 1
    clz     r5, r4                      // way shift
    movw    r7, #0x7FFF
    ands    r7, r7, r1, lsr #13         // sets - 1
LABEL(HalSweepDcacheSet)
    mov     r9, r4
LABEL(HalSweepDcacheWay)
    lsl     r6, r9, r5
    orr     r11, r10, r6
    lsl     r6, r7, r2
    orr     r11, r11, r6
    mcr     p15, 0, r11, c7, c14, 2     // DCCISW
    subs    r9, r9, #1
    bge     HalSweepDcacheWay
    subs    r7, r7, #1
    bge     HalSweepDcacheSet
LABEL(HalSweepDcacheSkip)
    add     r10, r10, #2
    cmp     r3, r10
    bgt     HalSweepDcacheLevel
LABEL(HalSweepDcacheFinished)
    movs    r10, #0
    mcr     p15, 2, r10, c0, c0, 0
    dsb     sy
    isb
    pop     {r4-r11}
    bx      lr
    LEAF_END HalSweepDcache

//
// VOID
// HalSweepIcache(VOID);
//
    LEAF_ENTRY HalSweepIcache
    movs    r0, #0
    dsb     ish
    mrc     p15, 0, r1, c0, c1, 7       // ID_MMFR3, broadcast needs the mp extensions
    ubfx    r1, r1, #12, #4
    cbz     r1, HalSweepIcacheLocal
    mcr     p15, 0, r0, c7, c1, 0       // ICIALLUIS
    mcr     p15, 0, r0, c7, c1, 6       // BPIALLIS
    b       HalSweepIcacheDone
LABEL(HalSweepIcacheLocal)
    mcr     p15, 0, r0, c7, c5, 0       // ICIALLU
    mcr     p15, 0, r0, c7, c5, 6       // BPIALL
LABEL(HalSweepIcacheDone)
    dsb     ish
    isb
    bx      lr
    LEAF_END HalSweepIcache

    END
