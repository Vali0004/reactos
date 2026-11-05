/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            lib/rtl/arm/debug_asm.S
 * PURPOSE:         Cross-privilege Debugging and Exception Support for ARM
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include <ksarm.h>

    TEXTAREA

    LEAF_ENTRY DbgBreakPoint
    __debugbreak
    bx lr
    LEAF_END DbgBreakPoint

    LEAF_ENTRY DbgUserBreakPoint
    __debugbreak
    bx lr
    LEAF_END DbgUserBreakPoint

    LEAF_ENTRY DbgBreakPointWithStatus
    ALTERNATE_ENTRY RtlpBreakWithStatusInstruction
    __debugbreak
    bx lr
    LEAF_END

//
// ULONG DebugService(Service, Argument1, Argument2, Argument3, Argument4)
//
// the trap takes the service in r12 and the arguments in r0-r3, the
// breakpoint after it is the one KdpStub and KdpTrap step over
//
    LEAF_ENTRY DebugService
    mov     r12, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp]
    __debugservice
    __debugbreak
    bx      lr
    LEAF_END DebugService

//
// VOID DebugService2(Argument1, Argument2, Service)
//
    LEAF_ENTRY DebugService2
    mov     r12, r2
    __debugservice
    __debugbreak
    bx      lr
    LEAF_END DebugService2

//
// VOID RtlCaptureContext(PCONTEXT ContextRecord)
//
// the context is the caller's, at the return address with its stack
//
    LEAF_ENTRY RtlCaptureContext
    str     r1, [r0, #CxR1]
    add     r1, r0, #CxR2
    stm     r1, {r2-r12}
    str     r0, [r0, #CxR0]
    str     sp, [r0, #CxSp]
    str     lr, [r0, #CxLr]
    str     lr, [r0, #CxPc]
    /* the mode comes along, the T bit reads as 0 but all callers are Thumb */
    mrs     r1, cpsr
    orr     r1, r1, #0x20
    str     r1, [r0, #CxCpsr]
    vmrs    r1, fpscr
    str     r1, [r0, #CxFpscr]
    add     r1, r0, #CxD
    vstm    r1, {d0-d15}
    mov32   r1, 0x200007
    str     r1, [r0, #CxContextFlags]
    ldr     r1, [r0, #CxR1]
    bx      lr
    LEAF_END RtlCaptureContext

//
// VOID RtlpRestoreContext(PCONTEXT ContextRecord)
//
// loads the registers of the context and resumes at its pc in thumb state,
// r0 and pc go through the target stack so every other register is free
//
    LEAF_ENTRY RtlpRestoreContext
    add     r1, r0, #CxD
    vldm    r1, {d0-d15}
    ldr     r1, [r0, #CxFpscr]
    vmsr    fpscr, r1
    ldr     r1, [r0, #CxSp]
    ldr     r2, [r0, #CxR0]
    ldr     r3, [r0, #CxPc]
    orr     r3, r3, #1
    stmdb   r1!, {r2, r3}
    mov     sp, r1
    ldr     lr, [r0, #CxLr]
    add     r1, r0, #CxR1
    ldm     r1, {r1-r12}
    pop     {r0, pc}
    LEAF_END RtlpRestoreContext

    END
/* EOF */
