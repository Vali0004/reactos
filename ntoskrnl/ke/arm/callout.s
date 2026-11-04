/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM user mode callout entry and return
 * COPYRIGHT:   Copyright 2026 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <ksarm.h>

/* KI_CALLOUT_STATE in callout.c */
#define CoR4                0x00
#define CoR11               0x1C
#define CoLr                0x20
#define CoOutputBuffer      0x24
#define CoOutputLength      0x28
#define CoD8                0x38
#define CALLOUT_LENGTH      0x78

/* room for the exception and trap frames a trap from user mode leaves */
#define USER_TRAP_LENGTH    (KEXCEPTION_FRAME_LENGTH + KTRAP_FRAME_LENGTH)

    IMPORT KiArmUserModeCallout

    TEXTAREA

//
// NTSTATUS
// KiCallUserMode(
//     _In_ PVOID *OutputBuffer,
//     _In_ PULONG OutputLength);
//
// returns through KiArmCallbackReturn once user mode is done, or right away
// when the callout cannot be set up. d8-d15 are kept since the callback may
// use them before the system service returns to its caller
//
    NESTED_ENTRY KiCallUserMode
    sub     sp, sp, #CALLOUT_LENGTH
    add     r12, sp, #CoR4
    stm     r12, {r4-r11, lr}
    add     r12, sp, #CoD8
    vstm    r12, {d8-d15}
    str     r0, [sp, #CoOutputBuffer]
    str     r1, [sp, #CoOutputLength]

    /* r11 and lr form a frame record, so the frame chain stays walkable */
    add     r11, sp, #CoR11

    /* the callback trap frames go right below, the C code runs under them */
    mov     r0, sp
    sub     sp, sp, #USER_TRAP_LENGTH
    bl      KiArmUserModeCallout

    /* only a failure comes back here */
    add     sp, sp, #USER_TRAP_LENGTH
    ldr     r11, [sp, #CoR11]
    ldr     lr, [sp, #CoLr]
    add     sp, sp, #CALLOUT_LENGTH
    bx      lr
    NESTED_END KiCallUserMode

//
// DECLSPEC_NORETURN
// VOID
// KiArmCallbackReturn(
//     _In_ PKI_CALLOUT_STATE Callout,
//     _In_ NTSTATUS Status);
//
// drops everything below the callout state and returns from KiCallUserMode
//
    LEAF_ENTRY KiArmCallbackReturn
    mov     sp, r0
    add     r12, sp, #CoD8
    vldm    r12, {d8-d15}
    add     r12, sp, #CoR4
    ldm     r12, {r4-r11, lr}
    mov     r0, r1
    add     sp, sp, #CALLOUT_LENGTH
    bx      lr
    LEAF_END KiArmCallbackReturn

    END
/* EOF */
