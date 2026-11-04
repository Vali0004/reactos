/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 context switching and thread startup, after the ARM64 ones
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <ksarm.h>

    IMPORT KiSwapContextResume
    IMPORT KiArmStartThread
    IMPORT KiArmTrapExit
    IMPORT KiSwitchKernelStack

    TEXTAREA

//
// BOOLEAN
// KiSwapContext(
//     _In_ KIRQL WaitIrql,
//     _In_ PKTHREAD OldThread);
//
// The new thread is already PRCB.CurrentThread. Returns TRUE when the new
// thread has kernel APCs to deliver.
//
    NESTED_ENTRY KiSwapContext
    sub     sp, sp, #KSWITCH_FRAME_LENGTH
    strb    r0, [sp, #SwApcBypass]
    add     r12, sp, #SwR4
    stm     r12, {r4-r11}
    str     lr, [sp, #SwReturn]

    /* the KIPCR sits in TPIDRPRW, the low bits are reserved */
    mrc     p15, 0, r2, c13, c0, 4
    bfc     r2, #0, #12
    ldr     r2, [r2, #PcCurrentThread]

    /* The new thread may still be leaving another processor */
LABEL(KiSwapContextWait)
    ldrb    r3, [r2, #ThSwapBusy]
    cbz     r3, KiSwapContextReady
    yield
    b       KiSwapContextWait

LABEL(KiSwapContextReady)
    dmb
    mov     r3, sp
    str     r3, [r1, #ThKernelStack]
    ldr     r3, [r2, #ThKernelStack]
    mov     sp, r3

    /* r1 and r2 still hold the old and new thread */
    ldrb    r0, [sp, #SwApcBypass]
    bl      KiSwapContextResume

    add     r12, sp, #SwR4
    ldm     r12, {r4-r11}
    ldr     lr, [sp, #SwReturn]
    add     sp, sp, #KSWITCH_FRAME_LENGTH
    bx      lr
    NESTED_END KiSwapContext

//
// First code of every thread, the context switch returns here with sp at
// the KSTART_FRAME. Only user threads come back, their exception frame
// follows the start frame.
//
    NESTED_ENTRY KiThreadStartup
    movs    r11, #0
    mov     r0, sp
    bl      KiArmStartThread
    add     sp, sp, #KSTART_FRAME_LENGTH
    b       KiArmTrapExit
    NESTED_END KiThreadStartup

//
// PVOID
// KeSwitchKernelStack(
//     _In_ PVOID StackBase,
//     _In_ PVOID StackLimit);
//
// Returns the old stack base, the caller continues on the new stack.
//
    NESTED_ENTRY KeSwitchKernelStack
    push    {r11, lr}
    mov     r11, sp
    sub     sp, sp, #8
    mov     r2, r11
    mov     r3, sp
    bl      KiSwitchKernelStack

    /* The old base still sits on the old stack */
    ldr     r1, [sp]
    add     sp, sp, r0
    add     r11, r11, r0
    add     sp, sp, #8
    pop     {r11, lr}
    mov     r0, r1
    bx      lr
    NESTED_END KeSwitchKernelStack

    END
