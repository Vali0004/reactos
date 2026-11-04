/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 exception vectors, trap entry and exit
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * Every exception ends up in SVC mode with a KEXCEPTION_FRAME right below a
 * KTRAP_FRAME on the SVC stack, sp pointing at the exception frame. srsdb
 * stores the return address and SPSR of the exception mode straight into
 * TrPc and TrCpsr, the last two words of the trap frame. Exceptions are taken
 * in Thumb state, SCTLR.TE is set by the kernel.
 */

#include <ksarm.h>

    IMPORT KiArmUndefinedTrap
    IMPORT KiArmSystemServiceTrap
    IMPORT KiArmPrefetchAbortTrap
    IMPORT KiArmDataAbortTrap
    IMPORT KiArmInterruptTrap

#define KI_TRAP_STACK_SIZE (KTRAP_FRAME_LENGTH + KEXCEPTION_FRAME_LENGTH)

/* IFSR, IFAR, DFSR and DFAR */
#define KI_FAULT_NONE 0
#define KI_FAULT_INSTRUCTION 1
#define KI_FAULT_DATA 2

    TEXTAREA

    /* VBAR takes 32 byte alignment, each slot is one 32-bit Thumb branch */
    ALIGN 32
    EXPORT KiArmVectorTable
LABEL(KiArmVectorTable)
    b.w KiArmResetVector
    b.w KiUndefinedInstructionException
    b.w KiSoftwareInterruptException
    b.w KiPrefetchAbortException
    b.w KiDataAbortException
    b.w KiArmResetVector
    b.w KiInterruptException
    b.w KiFastInterruptException

    LEAF_ENTRY KiArmResetVector
    b KiArmResetVector
    LEAF_END KiArmResetVector

/*
 * Fills in the frames of an exception that just switched to SVC mode.
 * In: sp at the exception frame, r0-r3 and r12 stored, lr the SVC lr of
 * the interrupted code, r0 what to read from the fault registers.
 * Uses r0-r3 only, the non-volatiles go into the exception frame.
 */
    LEAF_ENTRY KiArmCompleteTrapFrame

    /* non-volatiles of the interrupted code */
    str r4, [sp, #ExR4]
    str r5, [sp, #ExR5]
    str r6, [sp, #ExR6]
    str r7, [sp, #ExR7]
    str r8, [sp, #ExR8]
    str r9, [sp, #ExR9]
    str r10, [sp, #ExR10]
    str r11, [sp, #ExR11]
    add r4, sp, #KEXCEPTION_FRAME_LENGTH
    str r4, [sp, #ExTrapFrame]

    /* the trap frame sits on top, r4 keeps pointing at it */
    str r11, [r4, #TrR11]

    /* a kernel trap continues on this stack, its sp is the one before the frames */
    add r1, sp, #KI_TRAP_STACK_SIZE
    str r1, [r4, #TrSp]
    ldr r1, [r4, #TrCpsr]
    and r1, r1, #CPSRM_MASK
    cmp r1, #CPSRM_USER
    bne KiArmCompleteKernel

    /* System mode shares sp and lr with user mode */
    cps #CPSRM_SYS
    mov r2, sp
    mov r3, lr
    cps #CPSRM_SVC
    str r2, [r4, #TrSp]
    str r3, [r4, #TrLr]
    movs r1, #1 // UserMode
    b KiArmCompleteFault

LABEL(KiArmCompleteKernel)
    movs r1, #0 // KernelMode

LABEL(KiArmCompleteFault)
    strb r1, [r4, #TrPreviousMode]
    movs r1, #0
    strb r1, [r4, #TrExceptionActive]
    strb r1, [r4, #TrDebugRegistersValid]
    str r1, [r4, #TrVfpState]

    /* the fault registers are read before anything can fault again */
    cmp r0, #KI_FAULT_INSTRUCTION
    bne KiArmCompleteData
    mrc p15, 0, r1, c5, c0, 1
    mrc p15, 0, r2, c6, c0, 2
    b KiArmCompleteStore
LABEL(KiArmCompleteData)
    cmp r0, #KI_FAULT_DATA
    bne KiArmCompleteNone
    mrc p15, 0, r1, c5, c0, 0
    mrc p15, 0, r2, c6, c0, 0
    b KiArmCompleteStore
LABEL(KiArmCompleteNone)
    movs r1, #0
    movs r2, #0
LABEL(KiArmCompleteStore)
    str r1, [r4, #TrFaultStatus]
    str r2, [r4, #TrFaultAddress]

    /* trapc.c asserts this marker */
    mov32 r1, 0xBADB0D00
    str r1, [r4, #TrReserved]
    bx lr

    LEAF_END KiArmCompleteTrapFrame

/*
 * The vector entries. lr is adjusted to the preferred return address, the
 * undefined instruction one is fixed in C since Thumb and ARM differ there.
 * The SVC lr of the interrupted code is live until it is saved in TrLr.
 */
#ifdef _MSC_VER
    MACRO
    KI_TRAP_ENTRY $LrAdjust
    sub lr, lr, #$LrAdjust
    srsdb sp!, #CPSRM_SVC
    cps #CPSRM_SVC
    sub sp, sp, #(KI_TRAP_STACK_SIZE - 8)
    str r0, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR0)]
    str r1, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR1)]
    str r2, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR2)]
    str r3, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR3)]
    str r12, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR12)]
    str lr, [sp, #(KEXCEPTION_FRAME_LENGTH + TrLr)]
    MEND
#else
.macro KI_TRAP_ENTRY LrAdjust
    sub lr, lr, #\LrAdjust
    srsdb sp!, #CPSRM_SVC
    cps #CPSRM_SVC
    sub sp, sp, #(KI_TRAP_STACK_SIZE - 8)
    str r0, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR0)]
    str r1, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR1)]
    str r2, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR2)]
    str r3, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR3)]
    str r12, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR12)]
    str lr, [sp, #(KEXCEPTION_FRAME_LENGTH + TrLr)]
.endm
#endif

    NESTED_ENTRY KiUndefinedInstructionException
    KI_TRAP_ENTRY 0
    movs r0, #KI_FAULT_NONE
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmUndefinedTrap
    /* the handler returns the trap frame to leave with, its exception frame sits below */
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiUndefinedInstructionException

    NESTED_ENTRY KiSoftwareInterruptException
    KI_TRAP_ENTRY 0
    movs r0, #KI_FAULT_NONE
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmSystemServiceTrap
    /* the handler returns the trap frame to leave with, its exception frame sits below */
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiSoftwareInterruptException

/*
 * The kernel Zw stubs branch here with the service number in r12. An svc in
 * SVC mode would overwrite lr, so this builds the frame the vector builds,
 * returning to the caller in Thumb state.
 */
    NESTED_ENTRY KiZwSystemService
    /* TrPc and TrCpsr, where srsdb would put them, r0 is kept on top */
    sub sp, sp, #8
    push {r0}
    mrs r0, cpsr
    orr r0, r0, #0x20 // T, mrs reads it as 0
    str r0, [sp, #8]
    bic r0, lr, #1
    str r0, [sp, #4]
    pop {r0}
    cpsid i
    sub sp, sp, #(KI_TRAP_STACK_SIZE - 8)
    str r0, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR0)]
    str r1, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR1)]
    str r2, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR2)]
    str r3, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR3)]
    str r12, [sp, #(KEXCEPTION_FRAME_LENGTH + TrR12)]
    str lr, [sp, #(KEXCEPTION_FRAME_LENGTH + TrLr)]
    movs r0, #KI_FAULT_NONE
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmSystemServiceTrap
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiZwSystemService

    NESTED_ENTRY KiPrefetchAbortException
    KI_TRAP_ENTRY 4
    movs r0, #KI_FAULT_INSTRUCTION
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmPrefetchAbortTrap
    /* the handler returns the trap frame to leave with, its exception frame sits below */
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiPrefetchAbortException

    NESTED_ENTRY KiDataAbortException
    KI_TRAP_ENTRY 8
    movs r0, #KI_FAULT_DATA
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmDataAbortTrap
    /* the handler returns the trap frame to leave with, its exception frame sits below */
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiDataAbortException

    NESTED_ENTRY KiInterruptException
    KI_TRAP_ENTRY 4
    movs r0, #KI_FAULT_NONE
    bl KiArmCompleteTrapFrame
    add r0, sp, #KEXCEPTION_FRAME_LENGTH
    mov r1, sp
    bl KiArmInterruptTrap
    /* the handler returns the trap frame to leave with, its exception frame sits below */
    sub r0, r0, #KEXCEPTION_FRAME_LENGTH
    mov sp, r0
    b KiArmTrapExit
    NESTED_END KiInterruptException

    /* FIQs are never enabled */
    LEAF_ENTRY KiFastInterruptException
    b KiFastInterruptException
    LEAF_END KiFastInterruptException

/*
 * VOID KiExceptionExit(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
 * Leaves a trap through the given frames, the exception frame supplies the
 * non-volatiles. The frames need not be the ones the trap entry built.
 */
    LEAF_ENTRY KiExceptionExit
    ldr r11, [r1, #ExR11]
    ldr r4, [r1, #ExR4]
    ldr r5, [r1, #ExR5]
    ldr r6, [r1, #ExR6]
    ldr r7, [r1, #ExR7]
    ldr r8, [r1, #ExR8]
    ldr r9, [r1, #ExR9]
    ldr r10, [r1, #ExR10]
    mov sp, r0
    b KiArmTrapFrameExit
    LEAF_END KiExceptionExit

/* back from a C handler, sp at the exception frame the entry built */
    LEAF_ENTRY KiArmTrapExit
    /* the exception frame owns r11, KeContextToTrapFrame writes it there */
    ldr r11, [sp, #ExR11]
    ldr r4, [sp, #ExR4]
    ldr r5, [sp, #ExR5]
    ldr r6, [sp, #ExR6]
    ldr r7, [sp, #ExR7]
    ldr r8, [sp, #ExR8]
    ldr r9, [sp, #ExR9]
    ldr r10, [sp, #ExR10]
    add sp, sp, #KEXCEPTION_FRAME_LENGTH
    ALTERNATE_ENTRY KiArmTrapFrameExit

    /* sp at the trap frame, nothing may interrupt the unwinding */
    cpsid i
    ldr r0, [sp, #TrCpsr]
    and r0, r0, #CPSRM_MASK
    cmp r0, #CPSRM_USER
    bne KiArmExitKernel

    ldr r1, [sp, #TrSp]
    ldr r2, [sp, #TrLr]
    cps #CPSRM_SYS
    mov sp, r1
    mov lr, r2
    cps #CPSRM_SVC
    b KiArmExitCommon

LABEL(KiArmExitKernel)
    /* the kernel lr is the SVC one, sp comes back by itself with rfe */
    ldr lr, [sp, #TrLr]

LABEL(KiArmExitCommon)
    ldr r12, [sp, #TrR12]
    ldr r3, [sp, #TrR3]
    ldr r2, [sp, #TrR2]
    ldr r1, [sp, #TrR1]
    ldr r0, [sp, #TrR0]
    add sp, sp, #TrPc
    rfeia sp!
    LEAF_END KiArmTrapExit

    END
/* EOF */
