/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM user mode callbacks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* KiCallUserMode keeps the nonvolatile registers here, see callout.s */
typedef struct _KI_CALLOUT_STATE
{
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG Lr;
    PVOID *OutputBuffer;
    PULONG OutputLength;
    PKTRAP_FRAME TrapFrame;
    PVOID InitialStack;
    PVOID CallbackStack;
    ULONGLONG D8[8];
} KI_CALLOUT_STATE, *PKI_CALLOUT_STATE;

/* callout.s uses these offsets */
C_ASSERT(FIELD_OFFSET(KI_CALLOUT_STATE, R11) == 0x1C);
C_ASSERT(FIELD_OFFSET(KI_CALLOUT_STATE, Lr) == 0x20);
C_ASSERT(FIELD_OFFSET(KI_CALLOUT_STATE, OutputBuffer) == 0x24);
C_ASSERT(FIELD_OFFSET(KI_CALLOUT_STATE, OutputLength) == 0x28);
C_ASSERT(FIELD_OFFSET(KI_CALLOUT_STATE, D8) == 0x38);
C_ASSERT(sizeof(KI_CALLOUT_STATE) == 0x78);

/* the frames a trap from user mode leaves below the kernel stack top */
typedef struct _KI_USER_TRAP_STACK
{
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KI_USER_TRAP_STACK, *PKI_USER_TRAP_STACK;

C_ASSERT(sizeof(KI_USER_TRAP_STACK) == 0xC0);

DECLSPEC_NORETURN
VOID
KiArmCallbackReturn(
    _In_ PKI_CALLOUT_STATE Callout,
    _In_ NTSTATUS Status);

/* FUNCTIONS *****************************************************************/

/**
 * @brief Enters KiUserCallbackDispatcher, called by KiCallUserMode.
 *
 * The callback trap frames are built in the space KiCallUserMode left right
 * below the callout state, the trap exit leaves the kernel stack pointer at
 * the callout state, so traps of the callback land under it. Only returns
 * when the stack cannot be grown.
 */
NTSTATUS
NTAPI
KiArmUserModeCallout(
    _Inout_ PKI_CALLOUT_STATE Callout)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;
    PKI_USER_TRAP_STACK Stack;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT((Thread->ApcStateIndex == OriginalApcEnvironment) &&
           (Thread->CombinedApcDisable == 0));

    /* the callback gets a full kernel stack of its own */
    if (((ULONG_PTR)Callout - KERNEL_STACK_SIZE) < (ULONG_PTR)Thread->StackLimit)
    {
        Status = MmGrowKernelStack(Callout);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Callout->InitialStack = Thread->InitialStack;
    Callout->CallbackStack = Thread->CallbackStack;
    Callout->TrapFrame = TrapFrame;

    /* user mode resumes with its own nonvolatile state, in the dispatcher */
    Stack = (PKI_USER_TRAP_STACK)Callout - 1;
    Stack->TrapFrame = *TrapFrame;
    Stack->ExceptionFrame = *((PKEXCEPTION_FRAME)TrapFrame - 1);
    Stack->ExceptionFrame.TrapFrame = (ULONG)&Stack->TrapFrame;
    Stack->TrapFrame.Lr = 0;
    Stack->TrapFrame.Pc = (ULONG)KeUserCallbackDispatcher & ~1;
    Stack->TrapFrame.Cpsr |= CPSRC_THUMB;

    _disable();
    Thread->CallbackStack = Callout;
    Thread->InitialStack = Callout;

    KiExceptionExit(&Stack->TrapFrame, &Stack->ExceptionFrame);
}

NTSTATUS
NTAPI
KeUserModeCallback(
    _In_ ULONG RoutineIndex,
    _In_ PVOID Argument,
    _In_ ULONG ArgumentLength,
    _Out_ PVOID *Result,
    _Out_ PULONG ResultLength)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;
    ULONG OldSp, OldR0, OldR1, OldR2;
    ULONG_PTR UserArguments;
    ULONG GdiBatchCount;
    NTSTATUS Status;

    ASSERT(Thread->ApcState.KernelApcInProgress == FALSE);
    ASSERT(KeGetPreviousMode() == UserMode);

    OldSp = TrapFrame->Sp;
    OldR0 = TrapFrame->R0;
    OldR1 = TrapFrame->R1;
    OldR2 = TrapFrame->R2;

    /* the arguments go on the user stack */
    UserArguments = ALIGN_DOWN_BY(OldSp - ArgumentLength, 8);
    _SEH2_TRY
    {
        ProbeForWrite((PVOID)UserArguments, ArgumentLength, sizeof(ULONG));
        RtlCopyMemory((PVOID)UserArguments, Argument, ArgumentLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* KiUserCallbackDispatcher(RoutineIndex, Argument, ArgumentLength) */
    TrapFrame->Sp = UserArguments;
    TrapFrame->R0 = RoutineIndex;
    TrapFrame->R1 = UserArguments;
    TrapFrame->R2 = ArgumentLength;

    Status = KiCallUserMode(Result, ResultLength);

    /* a callback that popped the stack hands back its user state */
    TrapFrame = Thread->TrapFrame;
    if (Status != STATUS_CALLBACK_POP_STACK)
    {
        TrapFrame->Sp = OldSp;
        TrapFrame->R0 = OldR0;
        TrapFrame->R1 = OldR1;
        TrapFrame->R2 = OldR2;
    }

    _SEH2_TRY
    {
        GdiBatchCount = ((PTEB)Thread->Teb)->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        GdiBatchCount = 0;
    }
    _SEH2_END;

    /* the flush may call back again, keep it off what the caller sees */
    if (GdiBatchCount)
    {
        TrapFrame->Sp -= 256;
        KeGdiFlushUserBatch();
        TrapFrame->Sp += 256;
    }

    return Status;
}

NTSTATUS
NTAPI
NtCallbackReturn(
    _In_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKI_CALLOUT_STATE Callout = Thread->CallbackStack;
    PKTRAP_FRAME CallbackTrapFrame, TrapFrame;

    if (!Callout)
        return STATUS_NO_CALLBACK_ACTIVE;

    *Callout->OutputBuffer = Result;
    *Callout->OutputLength = ResultLength;

    CallbackTrapFrame = Thread->TrapFrame;
    TrapFrame = Callout->TrapFrame;

    _disable();

    /* the exception frame of a trap sits right below its trap frame */
    if (CallbackStatus == STATUS_CALLBACK_POP_STACK)
    {
        *TrapFrame = *CallbackTrapFrame;
        *((PKEXCEPTION_FRAME)TrapFrame - 1) = *((PKEXCEPTION_FRAME)CallbackTrapFrame - 1);
        ((PKEXCEPTION_FRAME)TrapFrame - 1)->TrapFrame = (ULONG)TrapFrame;
    }

    Thread->InitialStack = Callout->InitialStack;
    Thread->TrapFrame = TrapFrame;
    Thread->CallbackStack = Callout->CallbackStack;

    _enable();

    KiArmCallbackReturn(Callout, CallbackStatus);
}

/* EOF */
