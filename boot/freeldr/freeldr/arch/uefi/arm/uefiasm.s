/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     32-bit ARM UEFI stack switching helpers, after the ARM64 ones
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

    IMPORT UefiServiceStack
    IMPORT BasicStack
    IMPORT ExecuteLoaderCleanly
    IMPORT UefiExitBootServices

    TEXTAREA

//
// VOID
// ArmEnableVfp(VOID);
//
// U-Boot leaves VFP and NEON off, the hard float code of the loader needs them.
//
    LEAF_ENTRY ArmEnableVfp
    mrc     p15, 0, r0, c1, c0, 2       // CPACR
    orr     r0, r0, #0x00F00000         // cp10 and cp11, all modes
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #0x40000000             // FPEXC.EN
    vmsr    fpexc, r0
    bx      lr
    LEAF_END ArmEnableVfp

//
// VOID
// _exituefi(VOID);
//
// Boot services are left on the stack the firmware gave us.
//
    LEAF_ENTRY _exituefi
    push    {r11, lr}
    mov     r11, sp
    ldr     r12, =UefiServiceStack
    ldr     r12, [r12]
    bic     r12, r12, #7
    mov     sp, r12
    bl      UefiExitBootServices
    mov     sp, r11
    pop     {r11, lr}
    bx      lr
    LEAF_END _exituefi

//
// DECLSPEC_NORETURN
// VOID
// _changestack(VOID);
//
// Continues the loader on its own stack, the firmware stack is kept for
// leaving boot services.
//
    LEAF_ENTRY _changestack
    mov     r0, sp
    ldr     r12, =BasicStack
    ldr     r12, [r12]
    bic     r12, r12, #7
    mov     sp, r12
    movs    r11, #0
    bl      ExecuteLoaderCleanly
    udf     #0xFE
    LEAF_END _changestack

    END
