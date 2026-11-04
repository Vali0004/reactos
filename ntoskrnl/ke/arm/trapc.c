/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 trap handlers, after the ARM64 ones
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* Thumb udf immediates Windows uses for its traps, 0xDExx */
#define KI_UDF_DIVIDE_BY_ZERO   0xF9
#define KI_UDF_RDPMCCNTR64      0xFA
#define KI_UDF_FASTFAIL         0xFB
#define KI_UDF_ASSERTION        0xFC
#define KI_UDF_DEBUG_SERVICE    0xFD
#define KI_UDF_BREAKPOINT       0xFE

/* Short descriptor fault status, FS[4] is bit 10 */
#define KI_FSR_STATUS(Fsr)      (((Fsr) & 0xF) | (((Fsr) >> 6) & 0x10))
#define KI_FSR_WRITE            0x800
#define KI_FS_ALIGNMENT         0x01
#define KI_FS_DEBUG             0x02
#define KI_FS_TRANSLATION_L1    0x05
#define KI_FS_TRANSLATION_L2    0x07
#define KI_FS_PERMISSION_L1     0x0D
#define KI_FS_PERMISSION_L2     0x0F

#define CPSRF_T 0x20

#define MAX_SYSCALL_ARGUMENTS 16

#define KiUserTrap(TrapFrame) (((TrapFrame)->Cpsr & CPSRM_MASK) == CPSRM_USER)

typedef ULONG_PTR (NTAPI *PKI_SYSTEM_SERVICE)(
    ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
    ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
    ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
    ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

/* FUNCTIONS ******************************************************************/

static
VOID
KiArmDispatchTrapException(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ NTSTATUS Code,
    _In_ ULONG ParameterCount,
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2,
    _In_ ULONG_PTR Parameter3)
{
    EXCEPTION_RECORD ExceptionRecord;

    RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
    ExceptionRecord.ExceptionCode = Code;
    ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Pc;
    ExceptionRecord.NumberParameters = ParameterCount;
    ExceptionRecord.ExceptionInformation[0] = Parameter1;
    ExceptionRecord.ExceptionInformation[1] = Parameter2;
    ExceptionRecord.ExceptionInformation[2] = Parameter3;

    KiDispatchException(&ExceptionRecord,
                        ExceptionFrame,
                        TrapFrame,
                        KiGetPreviousMode(TrapFrame),
                        TRUE);
}

/* Runs with the interrupt state of the code that trapped */
static
VOID
KiArmRestoreInterrupts(
    _In_ PKTRAP_FRAME TrapFrame)
{
    if (!(TrapFrame->Cpsr & CPSRC_INT))
        _enable();
}

/**
 * @brief Delivers user APCs before a return to user mode.
 *
 * Returns with interrupts off, the trap exit restores the user state.
 */
VOID
NTAPI
KiArmExitToUser(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KIRQL OldIrql;

    for (;;)
    {
        _disable();
        if (!Thread->ApcState.UserApcPending)
            break;

        OldIrql = KfRaiseIrql(APC_LEVEL);
        _enable();
        KiDeliverApc(UserMode, ExceptionFrame, TrapFrame);
        _disable();
        KeLowerIrql(OldIrql);
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE,
                     TrapFrame->R12,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }
}

/* Every trap leaves through here, back to the assembly with interrupts off */
static
PKTRAP_FRAME
KiArmTrapReturn(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKEXCEPTION_FRAME ExceptionFrame = (PKEXCEPTION_FRAME)TrapFrame - 1;

    if (KiUserTrap(TrapFrame))
        KiArmExitToUser(TrapFrame, ExceptionFrame);

    _disable();
    return TrapFrame;
}

static
VOID
KiArmHandleAbort(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ BOOLEAN Execute)
{
    ULONG Status, FaultCode;
    BOOLEAN Write;
    PVOID Address;
    NTSTATUS NtStatus;

    Status = KI_FSR_STATUS(TrapFrame->FaultStatus);
    Write = !Execute && (TrapFrame->FaultStatus & KI_FSR_WRITE);
    Address = (PVOID)TrapFrame->FaultAddress;

    switch (Status)
    {
        case KI_FS_TRANSLATION_L1:
        case KI_FS_TRANSLATION_L2:
        case KI_FS_PERMISSION_L1:
        case KI_FS_PERMISSION_L2:
            break;

        case KI_FS_DEBUG:
            KiArmRestoreInterrupts(TrapFrame);
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_BREAKPOINT, 1, BREAKPOINT_BREAK, 0, 0);
            return;

        default:
            /* Alignment and external aborts are not paging faults */
            KiArmRestoreInterrupts(TrapFrame);
            KiArmDispatchTrapException(TrapFrame,
                                       ExceptionFrame,
                                       (Status == KI_FS_ALIGNMENT) ? STATUS_DATATYPE_MISALIGNMENT : STATUS_ACCESS_VIOLATION,
                                       2,
                                       Write,
                                       (ULONG_PTR)Address,
                                       0);
            return;
    }

    /* Let the memory manager in with the interrupt state of the faulting code */
    KiArmRestoreInterrupts(TrapFrame);

    /* the x86 error code layout mm.h decodes */
    FaultCode = 0;
    if ((Status == KI_FS_PERMISSION_L1) || (Status == KI_FS_PERMISSION_L2))
        FaultCode |= 0x1;
    if (Write)
        FaultCode |= 0x2;
    if (Execute)
        FaultCode |= 0x10;

    NtStatus = MmAccessFault(FaultCode, Address, KiGetPreviousMode(TrapFrame), TrapFrame);
    if (NT_SUCCESS(NtStatus))
        return;

    switch (NtStatus)
    {
        case STATUS_ACCESS_VIOLATION:
        case STATUS_GUARD_PAGE_VIOLATION:
        case STATUS_STACK_OVERFLOW:
            KiArmDispatchTrapException(TrapFrame,
                                       ExceptionFrame,
                                       NtStatus,
                                       2,
                                       Execute ? EXCEPTION_EXECUTE_FAULT : Write,
                                       (ULONG_PTR)Address,
                                       0);
            break;

        default:
            KiArmDispatchTrapException(TrapFrame,
                                       ExceptionFrame,
                                       STATUS_IN_PAGE_ERROR,
                                       3,
                                       Execute ? EXCEPTION_EXECUTE_FAULT : Write,
                                       (ULONG_PTR)Address,
                                       (ULONG_PTR)NtStatus);
            break;
    }
}

PKTRAP_FRAME
NTAPI
KiArmDataAbortTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    ASSERT(TrapFrame->Reserved == 0xBADB0D00);
    KiArmHandleAbort(TrapFrame, ExceptionFrame, FALSE);
    return KiArmTrapReturn(TrapFrame);
}

PKTRAP_FRAME
NTAPI
KiArmPrefetchAbortTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    ASSERT(TrapFrame->Reserved == 0xBADB0D00);
    KiArmHandleAbort(TrapFrame, ExceptionFrame, TRUE);
    return KiArmTrapReturn(TrapFrame);
}

PKTRAP_FRAME
NTAPI
KiArmUndefinedTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    USHORT Instruction = 0;
    BOOLEAN Thumb;

    ASSERT(TrapFrame->Reserved == 0xBADB0D00);
    KiArmRestoreInterrupts(TrapFrame);

    /* lr is the next instruction, 2 past a 16-bit Thumb one */
    Thumb = (TrapFrame->Cpsr & CPSRF_T) != 0;
    TrapFrame->Pc -= Thumb ? 2 : 4;

    if (Thumb)
    {
        _SEH2_TRY
        {
            if (KiUserTrap(TrapFrame))
                ProbeForRead((PVOID)TrapFrame->Pc, sizeof(USHORT), sizeof(USHORT));
            Instruction = *(PUSHORT)TrapFrame->Pc;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Instruction = 0;
        }
        _SEH2_END;
    }

    if ((Instruction & 0xFF00) != 0xDE00)
    {
        /* A 32-bit Thumb instruction started 2 bytes earlier */
        if (Thumb)
            TrapFrame->Pc -= 2;
        KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_ILLEGAL_INSTRUCTION, 0, 0, 0, 0);
        return KiArmTrapReturn(TrapFrame);
    }

    switch (Instruction & 0xFF)
    {
        case KI_UDF_BREAKPOINT:
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_BREAKPOINT, 1, BREAKPOINT_BREAK, 0, 0);
            break;

        case KI_UDF_ASSERTION:
            TrapFrame->Pc += sizeof(USHORT);
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_ASSERTION_FAILURE, 0, 0, 0, 0);
            break;

        case KI_UDF_DEBUG_SERVICE:
            /* The debugger steps over the breakpoint that follows the service one */
            TrapFrame->Pc += sizeof(USHORT);
            KiArmDispatchTrapException(TrapFrame,
                                       ExceptionFrame,
                                       STATUS_BREAKPOINT,
                                       3,
                                       TrapFrame->R12,
                                       TrapFrame->R0,
                                       TrapFrame->R1);
            break;

        case KI_UDF_FASTFAIL:
            if (!KiUserTrap(TrapFrame))
                KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE, TrapFrame->R0, (ULONG_PTR)TrapFrame, (ULONG_PTR)ExceptionFrame, 0);
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_STACK_BUFFER_OVERRUN, 1, TrapFrame->R0, 0, 0);
            break;

        case KI_UDF_DIVIDE_BY_ZERO:
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_INTEGER_DIVIDE_BY_ZERO, 0, 0, 0, 0);
            break;

        default:
            KiArmDispatchTrapException(TrapFrame, ExceptionFrame, STATUS_ILLEGAL_INSTRUCTION, 0, 0, 0, 0);
            break;
    }

    return KiArmTrapReturn(TrapFrame);
}

/**
 * @brief Runs a system service.
 *
 * The number comes in r12, the first four arguments in r0-r3 and the rest
 * on the caller's stack, like a call.
 */
PKTRAP_FRAME
NTAPI
KiArmSystemServiceTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    ULONG_PTR Arguments[MAX_SYSCALL_ARGUMENTS];
    KPROCESSOR_MODE PreviousMode, OldMode;
    ULONG ServiceNumber, TableIndex, Count, i;
    PULONG_PTR StackArguments;
    PKTHREAD Thread;
    ULONG_PTR Result;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    ASSERT(TrapFrame->Reserved == 0xBADB0D00);

    Thread = KeGetCurrentThread();
    KeGetCurrentPrcb()->KeSystemCalls++;

    PreviousMode = KiGetPreviousMode(TrapFrame);
    OldMode = Thread->PreviousMode;
    Thread->PreviousMode = PreviousMode;
    TrapFrame->PreviousMode = PreviousMode;

    TrapFrame->TrapFrame = (ULONG_PTR)Thread->TrapFrame;
    Thread->TrapFrame = TrapFrame;

    KiArmRestoreInterrupts(TrapFrame);

    ServiceNumber = TrapFrame->R12;
    TableIndex = (ServiceNumber >> TABLE_OFFSET_BITS) & ((1 << TABLE_NUMBER_BITS) - 1);
    ServiceNumber &= SERVICE_NUMBER_MASK;

    if ((TableIndex == WIN32K_SERVICE_INDEX) && (PreviousMode == UserMode))
    {
        ULONG GdiBatchCount;

        _SEH2_TRY
        {
            GdiBatchCount = NtCurrentTeb()->GdiBatchCount;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            GdiBatchCount = 0;
        }
        _SEH2_END;

        if (GdiBatchCount)
            KeGdiFlushUserBatch();
    }

    /* FIXME: the first win32k call has to turn the thread into a GUI thread, which moves its stack */
    DescriptorTable = &((PKSERVICE_TABLE_DESCRIPTOR)Thread->ServiceTable)[TableIndex];
    if (ServiceNumber >= DescriptorTable->Limit)
    {
        Result = (ULONG_PTR)(LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
        goto Exit;
    }

    Count = DescriptorTable->Number[ServiceNumber] / sizeof(ULONG_PTR);
    ASSERT(Count <= MAX_SYSCALL_ARGUMENTS);

    Arguments[0] = TrapFrame->R0;
    Arguments[1] = TrapFrame->R1;
    Arguments[2] = TrapFrame->R2;
    Arguments[3] = TrapFrame->R3;
    for (i = 4; i < MAX_SYSCALL_ARGUMENTS; i++)
        Arguments[i] = 0;

    if (Count > 4)
    {
        StackArguments = (PULONG_PTR)TrapFrame->Sp;

        _SEH2_TRY
        {
            if (PreviousMode == UserMode)
                ProbeForRead(StackArguments, (Count - 4) * sizeof(ULONG_PTR), sizeof(ULONG_PTR));

            for (i = 4; i < Count; i++)
                Arguments[i] = StackArguments[i - 4];
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = (ULONG_PTR)(LONG_PTR)_SEH2_GetExceptionCode();
            _SEH2_YIELD(goto Exit);
        }
        _SEH2_END;
    }

    Result = ((PKI_SYSTEM_SERVICE)DescriptorTable->Base[ServiceNumber])(
        Arguments[0], Arguments[1], Arguments[2], Arguments[3],
        Arguments[4], Arguments[5], Arguments[6], Arguments[7],
        Arguments[8], Arguments[9], Arguments[10], Arguments[11],
        Arguments[12], Arguments[13], Arguments[14], Arguments[15]);

Exit:
    TrapFrame->R0 = Result;
    Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);
    Thread->PreviousMode = OldMode;
    return KiArmTrapReturn(TrapFrame);
}

static
VOID
KiArmDpcInterrupt(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread, OldThread;

    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        _disable();
        KiRetireDpcList(Prcb);
        _enable();
    }

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if (Prcb->NextThread)
    {
        KiAcquirePrcbLock(Prcb);

        OldThread = Prcb->CurrentThread;
        NewThread = Prcb->NextThread;
        Prcb->NextThread = NULL;
        Prcb->CurrentThread = NewThread;
        NewThread->State = Running;
        OldThread->WaitReason = WrDispatchInt;

        KxQueueReadyThread(OldThread, Prcb);
        KiSwapContext(APC_LEVEL, OldThread);
    }
}

/* Software interrupts and devices all come through the interrupt controller */
PKTRAP_FRAME
NTAPI
KiArmInterruptTrap(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKINTERRUPT Interrupt = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql, Irql;
    CHAR PreviousMode;
    ULONG Vector;

    ASSERT(TrapFrame->Reserved == 0xBADB0D00);

    Vector = HalGetInterruptSource();
    if (Vector >= 1020)
        return KiArmTrapReturn(TrapFrame);

    Pcr->Prcb.InterruptCount++;

    if ((Vector == APC_LEVEL) || (Vector == DISPATCH_LEVEL) || (Vector == IPI_LEVEL))
    {
        Irql = (KIRQL)Vector;
    }
    else
    {
        if (Vector < RTL_NUMBER_OF(Pcr->Idt))
            Interrupt = Pcr->Idt[Vector];

        if (!Interrupt)
        {
            HalBeginSystemInterrupt(HIGH_LEVEL, Vector, &OldIrql);
            HalEndSystemInterrupt(OldIrql, TrapFrame);
            DPRINT1("Unexpected interrupt %lu\n", Vector);
            return KiArmTrapReturn(TrapFrame);
        }

        Irql = Interrupt->Irql;
    }

    if (!HalBeginSystemInterrupt(Irql, Vector, &OldIrql))
        return KiArmTrapReturn(TrapFrame);

    /* Interrupt frames carry the IRQL they interrupted, the clock reads it */
    PreviousMode = TrapFrame->PreviousMode;
    TrapFrame->PreviousIrql = OldIrql;
    _enable();

    switch (Vector)
    {
        case APC_LEVEL:
            KiDeliverApc(KernelMode, NULL, TrapFrame);
            break;

        case DISPATCH_LEVEL:
            KiArmDpcInterrupt();
            break;

        case IPI_LEVEL:
            KiIpiServiceRoutine(TrapFrame, ExceptionFrame);
            break;

        default:
            /* The first object heads the list of the ones sharing the vector */
            Entry = &Interrupt->InterruptListEntry;
            do
            {
                Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT, InterruptListEntry);
                Interrupt->TrapFrame = TrapFrame;
                KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
                Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
                KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
                Entry = Entry->Flink;
            } while (Entry != &((PKINTERRUPT)Pcr->Idt[Vector])->InterruptListEntry);
            break;
    }

    _disable();
    HalEndSystemInterrupt(OldIrql, TrapFrame);
    TrapFrame->PreviousMode = PreviousMode;
    return KiArmTrapReturn(TrapFrame);
}

/**
 * @brief Switches the translation tables to another process.
 *
 * DirectoryTableBase[0] is the software directory as on x86, the table
 * the MMU walks is the process' first level table.
 */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    ULONG Ttbr0;

    UNREFERENCED_PARAMETER(OldProcess);

    /* normal write back walks, inner shareable */
    Ttbr0 = NewProcess->FirstLevelTable | 0x6A;

    __dsb(_ARM_BARRIER_ISH);
    _MoveToCoprocessor(Ttbr0, 15, 0, 2, 0, 0);        /* TTBR0 */
    __isb(_ARM_BARRIER_SY);

    /* no ASIDs yet, every user translation goes */
    _MoveToCoprocessor(0, 15, 0, 8, 3, 0);            /* TLBIALLIS */
    _MoveToCoprocessor(0, 15, 0, 7, 1, 6);            /* BPIALLIS */
    __dsb(_ARM_BARRIER_ISH);
    __isb(_ARM_BARRIER_SY);
}

/* EOF */
