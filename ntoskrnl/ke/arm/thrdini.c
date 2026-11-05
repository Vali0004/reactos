/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 thread initialization and context switch support, after ARM64
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
KiThreadStartup(VOID);

/* trapc.c, delivers user APCs and returns with interrupts off */
VOID
NTAPI
KiArmExitToUser(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame);

/* The first switch to a user thread unwinds this, top down into user mode */
typedef struct _KUINIT_FRAME
{
    KSWITCH_FRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KUINIT_FRAME, *PKUINIT_FRAME;

typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME CtxSwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

/* the trap exit takes the exception frame right below the trap frame */
C_ASSERT(FIELD_OFFSET(KUINIT_FRAME, TrapFrame) ==
         FIELD_OFFSET(KUINIT_FRAME, ExceptionFrame) + sizeof(KEXCEPTION_FRAME));
C_ASSERT((sizeof(KUINIT_FRAME) % 8) == 0);
C_ASSERT((sizeof(KKINIT_FRAME) % 8) == 0);

/* cortex-a8 class cores have no generic timer, set once at startup */
BOOLEAN KiArmHasGenericTimer;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiInitializeContextThread(
    _In_ PKTHREAD Thread,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID StartContext,
    _In_opt_ PCONTEXT Context)
{
    PKSWITCH_FRAME CtxSwitchFrame;
    PKSTART_FRAME StartFrame;

    if (Context)
    {
        PKUINIT_FRAME InitFrame = ((PKUINIT_FRAME)Thread->InitialStack) - 1;
        PKEXCEPTION_FRAME ExceptionFrame = &InitFrame->ExceptionFrame;
        PKTRAP_FRAME TrapFrame = &InitFrame->TrapFrame;

        RtlZeroMemory(InitFrame, sizeof(*InitFrame));
        CtxSwitchFrame = &InitFrame->CtxSwitchFrame;
        StartFrame = &InitFrame->StartFrame;

        Thread->KernelStack = InitFrame;
        Thread->PreviousMode = UserMode;

        ASSERT((Context->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL);
        TrapFrame->PreviousMode = UserMode;
        TrapFrame->Reserved = 0xBADB0D00;
        KeContextToTrapFrame(Context,
                             ExceptionFrame,
                             TrapFrame,
                             Context->ContextFlags | CONTEXT_CONTROL,
                             UserMode);

        StartFrame->UserThread = TRUE;
    }
    else
    {
        PKKINIT_FRAME InitFrame = ((PKKINIT_FRAME)Thread->InitialStack) - 1;

        RtlZeroMemory(InitFrame, sizeof(*InitFrame));
        CtxSwitchFrame = &InitFrame->CtxSwitchFrame;
        StartFrame = &InitFrame->StartFrame;

        Thread->KernelStack = InitFrame;
        Thread->PreviousMode = KernelMode;
        StartFrame->UserThread = FALSE;
    }

    CtxSwitchFrame->Return = (ULONG)KiThreadStartup;
    CtxSwitchFrame->ApcBypass = TRUE;
    CtxSwitchFrame->ExceptionList = EXCEPTION_CHAIN_END;

    StartFrame->SystemRoutine = (ULONG)SystemRoutine;
    StartFrame->StartRoutine = (ULONG)StartRoutine;
    StartFrame->StartContext = (ULONG)StartContext;
}

/**
 * @brief Runs the system routine of a new thread, called from KiThreadStartup.
 *
 * System threads never come back. User threads return once their trap
 * frame is ready for the first switch to user mode, with interrupts off.
 */
VOID
NTAPI
KiArmStartThread(
    _In_ PKSTART_FRAME StartFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    KeLowerIrql(APC_LEVEL);
    _enable();

    ((PKSYSTEM_ROUTINE)StartFrame->SystemRoutine)((PKSTART_ROUTINE)StartFrame->StartRoutine,
                                                   (PVOID)StartFrame->StartContext);

    if (!StartFrame->UserThread)
        KeBugCheck(NO_USER_MODE_CONTEXT);

    /* user mode finds its TEB in TPIDRURW */
    _MoveToCoprocessor((ULONG)Thread->Teb, 15, 0, 13, 0, 2);

    /* the first instructions in user mode come as a user APC, LdrInitializeThunk */
    KeLowerIrql(PASSIVE_LEVEL);
    KiArmExitToUser(KeGetTrapFrame(Thread), (PKEXCEPTION_FRAME)(StartFrame + 1));
}

BOOLEAN
KiSwapContextResume(
    _In_ BOOLEAN ApcBypass,
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPROCESS OldProcess, NewProcess;
    ULONG64 CurrentCycleTime, ElapsedCycles;

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

    /* the kernel SEH chain is per thread, the PCR only holds the running one */
    ((PKSWITCH_FRAME)OldThread->KernelStack)->ExceptionList = Pcr->NtTib.ExceptionList;
    Pcr->NtTib.ExceptionList = ((PKSWITCH_FRAME)NewThread->KernelStack)->ExceptionList;

    CurrentCycleTime = KxQueryProcessorCycleTime();
    ElapsedCycles = CurrentCycleTime - Pcr->Prcb.StartCycles;
    ((PETHREAD)OldThread)->CycleTime += ElapsedCycles;
    Pcr->Prcb.StartCycles = CurrentCycleTime;

    /* user mode finds its TEB in TPIDRURW, the kernel keeps it in the PCR as well */
    Pcr->NtTib.Self = (PVOID)NewThread->Teb;
    _MoveToCoprocessor((ULONG)NewThread->Teb, 15, 0, 13, 0, 2);

    Pcr->Prcb.KeContextSwitches++;
    NewThread->ContextSwitches++;

    if (Pcr->Prcb.DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    /* The old stack is no longer in use, another processor may take the thread */
    KeMemoryBarrier();
    OldThread->SwapBusy = FALSE;

    if (NewThread->ApcState.KernelApcPending)
    {
        if ((NewThread->SpecialApcDisable == 0) && (ApcBypass == 0))
            return TRUE;

        HalRequestSoftwareInterrupt(APC_LEVEL);
    }

    return FALSE;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD OldThread, NewThread;

    for (;;)
    {
        _enable();
        YieldProcessor();
        YieldProcessor();
        _disable();

        if ((Prcb->DpcData[0].DpcQueueDepth) ||
            (Prcb->TimerRequest) ||
            (Prcb->DeferredReadyListHead.Next))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcList(Prcb);
        }

        if (Prcb->NextThread)
        {
            _enable();

            OldThread = Prcb->CurrentThread;
            NewThread = Prcb->NextThread;
            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;

            KiSwapContext(APC_LEVEL, OldThread);
        }
        else
        {
            /* The HAL returns with interrupts on */
            Prcb->PowerState.IdleFunction(&Prcb->PowerState);
        }
    }
}

/**
 * @brief Copies the current stack to a new kernel stack.
 *
 * Every frame record saves the caller's r11, the chain in the copy is moved
 * along with the thread's stack fields. KeSwitchKernelStack moves sp and r11
 * by the returned offset.
 */
LONG_PTR
NTAPI
KiSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit,
    _In_ PULONG_PTR CallerFrame,
    _Out_ PVOID *OldStackBase)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR OldBase, OldLimit, Saved;
    PULONG_PTR Frame;
    LONG_PTR Offset;
    BOOLEAN Enabled;
    SIZE_T Size;

    OldBase = (ULONG_PTR)Thread->StackBase;
    OldLimit = Thread->StackLimit;
    Size = OldBase - OldLimit;
    ASSERT(Size <= (ULONG_PTR)StackBase - (ULONG_PTR)StackLimit);

    Enabled = KeDisableInterrupts();

    RtlCopyMemory((PUCHAR)StackBase - Size, (PVOID)OldLimit, Size);
    Offset = (PUCHAR)StackBase - (PUCHAR)OldBase;

    /* User mode frame pointers end the chain, they never point into the stack */
    Frame = (PULONG_PTR)((ULONG_PTR)CallerFrame + Offset);
    for (;;)
    {
        Saved = Frame[0];
        if ((Saved < OldLimit) || (Saved >= OldBase))
            break;

        Frame[0] = Saved + Offset;
        Frame = (PULONG_PTR)(Saved + Offset);
    }

    if (((ULONG_PTR)Thread->TrapFrame >= OldLimit) && ((ULONG_PTR)Thread->TrapFrame < OldBase))
        Thread->TrapFrame = (PKTRAP_FRAME)((ULONG_PTR)Thread->TrapFrame + Offset);

    Thread->InitialStack = (PVOID)((ULONG_PTR)Thread->InitialStack + Offset);
    Thread->StackBase = StackBase;
    Thread->StackLimit = (ULONG_PTR)StackLimit;
    Thread->LargeStack = TRUE;

    KeRestoreInterrupts(Enabled);

    *OldStackBase = (PVOID)OldBase;
    return Offset;
}

/* EOF */
