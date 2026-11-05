/*
 * PROJECT:     ReactOS Kernel32
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     RaiseException entry that records the caller's registers
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <ksarm.h>

    IMPORT BaseRaiseExceptionWithContext

    TEXTAREA

/* the context sits above an 8 byte slot for the fifth argument */
#define RE_FRAME (CONTEXT_FRAME_LENGTH + 8)

//
// VOID RaiseException(DWORD Code, DWORD Flags, DWORD Count, const ULONG_PTR *Args)
//
// the context is the caller's at the return address, so unwinding starts in its frame
//
    NESTED_ENTRY RaiseException
    sub     sp, sp, #RE_FRAME
    str     r12, [sp, #(8 + CxR12)]
    add     r12, sp, #(8 + CxR0)
    stm     r12, {r0-r11}
    add     r12, sp, #RE_FRAME
    str     r12, [sp, #(8 + CxSp)]
    str     lr, [sp, #(8 + CxLr)]
    str     lr, [sp, #(8 + CxPc)]
    mrs     r12, cpsr
    orr     r12, r12, #0x20
    str     r12, [sp, #(8 + CxCpsr)]
    vmrs    r12, fpscr
    str     r12, [sp, #(8 + CxFpscr)]
    add     r12, sp, #(8 + CxD)
    vstm    r12, {d0-d15}
    mov32   r12, 0x200007
    str     r12, [sp, #(8 + CxContextFlags)]
    add     r12, sp, #8
    str     r12, [sp]
    bl      BaseRaiseExceptionWithContext

    /* only reached when the raise itself failed */
    ldr     lr, [sp, #(8 + CxLr)]
    add     sp, sp, #RE_FRAME
    bx      lr
    NESTED_END RaiseException

    END
/* EOF */
