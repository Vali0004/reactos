/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 boot stack switch, after the ARM64 one
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <ksarm.h>

/* KeInitializeThread builds the idle thread's frames right below its initial stack */
#define BOOT_STACK_RESERVE  (KTRAP_FRAME_LENGTH + KEXCEPTION_FRAME_LENGTH + \
                             KSTART_FRAME_LENGTH + KSWITCH_FRAME_LENGTH)

    IMPORT KiSystemStartupBootStack

    TEXTAREA

//
// DECLSPEC_NORETURN
// VOID
// KiSwitchToBootStack(
//     _In_ ULONG_PTR InitialStack);
//
// Every exception is taken on the SVC stack, the banked stacks of the other
// modes are never used and stay unset.
//
    LEAF_ENTRY KiSwitchToBootStack
    sub     r0, r0, #BOOT_STACK_RESERVE
    mov     sp, r0
    movs    r11, #0
    movs    lr, #0
    b       KiSystemStartupBootStack
    LEAF_END KiSwitchToBootStack

    END
