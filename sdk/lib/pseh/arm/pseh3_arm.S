/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM Thumb-2 support routines for PSEH3, after pseh3_i386.S
 * COPYRIGHT:   Copyright 2014 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include "../i386/pseh3_asmdef.h"

    .syntax unified
    .thumb
    .text

/* user mode keeps the chain in the TEB, kernel mode in the KPCR, both at NtTib.ExceptionList */
    .global _SEH3$_ChainHead
    .p2align 2
    .thumb_func
_SEH3$_ChainHead:
    mrs r0, cpsr
    and r0, r0, #0x1f
    cmp r0, #0x10
    bne 1f
    mrc p15, 0, r0, c13, c0, 2
    bx lr
1:
    mrc p15, 0, r0, c13, c0, 4
    bfc r0, #0, #12
    bx lr

/*
 * int _SEH3$_RegisterFrameWithNonVolatiles(Frame, ScopeTable, AllocaFrame)
 * returns 0 now, 1 when re-entered for a filter or finally, 2 for the __except block
 */
    .global _SEH3$_RegisterFrameWithNonVolatiles
    .p2align 2
    .thumb_func
_SEH3$_RegisterFrameWithNonVolatiles:
    str r2, [r0, #SEH3_REGISTRATION_FRAME_AllocaFrame]
    str r1, [r0, #SEH3_REGISTRATION_FRAME_ScopeTable]
    str r0, [r0, #SEH3_REGISTRATION_FRAME_EndOfChain]
    mov r12, r0
    push {r12, lr}
    bl _SEH3$_ChainHead
    pop {r12, lr}
    /* link the frame in as the new head */
    ldr r1, [r0]
    str r1, [r12, #SEH3_REGISTRATION_FRAME_Next]
    str r12, [r0]
    b 2f

/* same, but nested below the registered head frame */
    .global _SEH3$_RegisterTryLevelWithNonVolatiles
    .p2align 2
    .thumb_func
_SEH3$_RegisterTryLevelWithNonVolatiles:
    str r2, [r0, #SEH3_REGISTRATION_FRAME_AllocaFrame]
    str r1, [r0, #SEH3_REGISTRATION_FRAME_ScopeTable]
    movs r1, #0
    str r1, [r0, #SEH3_REGISTRATION_FRAME_Handler]
    mov r12, r0
    push {r12, lr}
    bl _SEH3$_ChainHead
    pop {r12, lr}
    ldr r0, [r0]
    ldr r1, [r0, #SEH3_REGISTRATION_FRAME_EndOfChain]
    str r1, [r12, #SEH3_REGISTRATION_FRAME_Next]
    str r12, [r0, #SEH3_REGISTRATION_FRAME_EndOfChain]

2:
    /* the state the caller resumes with when the frame is re-entered */
    str sp, [r12, #SEH3_REGISTRATION_FRAME_Esp]
    str r11, [r12, #SEH3_REGISTRATION_FRAME_Ebp]
    str r4, [r12, #SEH3_REGISTRATION_FRAME_R4]
    str r5, [r12, #SEH3_REGISTRATION_FRAME_R5]
    str r6, [r12, #SEH3_REGISTRATION_FRAME_R6]
    str r7, [r12, #SEH3_REGISTRATION_FRAME_R7]
    str r8, [r12, #SEH3_REGISTRATION_FRAME_R8]
    str r9, [r12, #SEH3_REGISTRATION_FRAME_R9]
    str r10, [r12, #SEH3_REGISTRATION_FRAME_R10]
    str lr, [r12, #SEH3_REGISTRATION_FRAME_ReturnAddress]
    movs r0, #0
    bx lr

/*
 * long _SEH3$_InvokeEmbeddedFilterFromRegistration(Frame)
 * runs the filter or finally block of Frame inside its function, on a stack
 * below ours with the same distance to the alloca frame the function has
 */
    .global _SEH3$_InvokeEmbeddedFilterFromRegistration
    .p2align 2
    .thumb_func
_SEH3$_InvokeEmbeddedFilterFromRegistration:
    /* frame, its AllocaFrame and a pad keep the 8 byte alignment */
    ldr r1, [r0, #SEH3_REGISTRATION_FRAME_AllocaFrame]
    push {r0-r2, r4-r11, lr}
    mov r12, r0
    ldr r4, [r12, #SEH3_REGISTRATION_FRAME_R4]
    ldr r5, [r12, #SEH3_REGISTRATION_FRAME_R5]
    ldr r6, [r12, #SEH3_REGISTRATION_FRAME_R6]
    ldr r7, [r12, #SEH3_REGISTRATION_FRAME_R7]
    ldr r8, [r12, #SEH3_REGISTRATION_FRAME_R8]
    ldr r9, [r12, #SEH3_REGISTRATION_FRAME_R9]
    ldr r10, [r12, #SEH3_REGISTRATION_FRAME_R10]
    ldr r11, [r12, #SEH3_REGISTRATION_FRAME_Ebp]
    ldr r2, [r12, #SEH3_REGISTRATION_FRAME_Esp]
    subs r1, r1, r2
    adds r1, r1, #7
    bic r1, r1, #7
    /* _SEH3$_NESTED_FUNC_RETURN comes back with this sp */
    str sp, [r12, #SEH3_REGISTRATION_FRAME_AllocaFrame]
    sub sp, sp, r1
    ldr r2, [r12, #SEH3_REGISTRATION_FRAME_ReturnAddress]
    movs r0, #1
    bx r2

/* r0 holds the result, sp the value saved above */
    .global _SEH3$_InvokeEmbeddedFilterReturnClang
    .p2align 2
    .thumb_func
_SEH3$_InvokeEmbeddedFilterReturnClang:
    ldr r12, [sp]
    /* the block may have changed values the function keeps in registers */
    str r4, [r12, #SEH3_REGISTRATION_FRAME_R4]
    str r5, [r12, #SEH3_REGISTRATION_FRAME_R5]
    str r6, [r12, #SEH3_REGISTRATION_FRAME_R6]
    str r7, [r12, #SEH3_REGISTRATION_FRAME_R7]
    str r8, [r12, #SEH3_REGISTRATION_FRAME_R8]
    str r9, [r12, #SEH3_REGISTRATION_FRAME_R9]
    str r10, [r12, #SEH3_REGISTRATION_FRAME_R10]
    str r11, [r12, #SEH3_REGISTRATION_FRAME_Ebp]
    /* hand the function its own AllocaFrame back */
    ldr r1, [sp, #4]
    str r1, [r12, #SEH3_REGISTRATION_FRAME_AllocaFrame]
    pop {r1-r11, lr}
    bx lr

/* void _SEH3$_JumpToTarget(Frame), resumes the function with r0 = 2 */
    .global _SEH3$_JumpToTarget
    .p2align 2
    .thumb_func
_SEH3$_JumpToTarget:
    mov r12, r0
    ldr r4, [r12, #SEH3_REGISTRATION_FRAME_R4]
    ldr r5, [r12, #SEH3_REGISTRATION_FRAME_R5]
    ldr r6, [r12, #SEH3_REGISTRATION_FRAME_R6]
    ldr r7, [r12, #SEH3_REGISTRATION_FRAME_R7]
    ldr r8, [r12, #SEH3_REGISTRATION_FRAME_R8]
    ldr r9, [r12, #SEH3_REGISTRATION_FRAME_R9]
    ldr r10, [r12, #SEH3_REGISTRATION_FRAME_R10]
    ldr r11, [r12, #SEH3_REGISTRATION_FRAME_Ebp]
    ldr r1, [r12, #SEH3_REGISTRATION_FRAME_Esp]
    mov sp, r1
    ldr r2, [r12, #SEH3_REGISTRATION_FRAME_ReturnAddress]
    movs r0, #2
    bx r2
