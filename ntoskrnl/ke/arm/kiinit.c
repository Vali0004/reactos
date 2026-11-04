/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARMv7 kernel entry and initialization, after the ARM64 one
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* SCTLR, CPACR and FPEXC bits */
#define SCTLR_V                 0x00002000  /* high vectors, off for VBAR */
#define SCTLR_TE                0x40000000  /* exceptions in Thumb state */
#define CPACR_CP10_CP11         0x00F00000  /* VFP and NEON for every mode */
#define FPEXC_EN                0x40000000

/* GLOBALS ********************************************************************/

/* KIPCR() masks the low bits of TPIDRPRW, the PCR has to be page aligned */
KIPCR DECLSPEC_ALIGN(PAGE_SIZE) KiInitialPcr;

/* Boot stack of the first processor */
UCHAR DECLSPEC_ALIGN(16) KiP0BootStackData[KERNEL_STACK_SIZE] = {0};
PVOID KiP0BootStack = &KiP0BootStackData[KERNEL_STACK_SIZE];

ULONG KeNumberProcessIds;
ULONG KeNumberTbEntries;
ULONG ProcessCount; // PERF

/* trap.s */
extern UCHAR KiArmVectorTable[];

/* The loader's console print, valid while its identity mapping is, as on ARM64 */
ULONG (*FrLdrDbgPrint)(const char *Format, ...);

#define KiEarlyTrace(...) \
    do { if (FrLdrDbgPrint) FrLdrDbgPrint(__VA_ARGS__); } while (0)

/* boot.s */
DECLSPEC_NORETURN
VOID
KiSwitchToBootStack(
    _In_ ULONG_PTR InitialStack);

/* FUNCTIONS ******************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
KiInitMachineDependent(VOID)
{
    /* Nothing to set up later for now */
}

static
VOID
KiInitializePcr(
    _Out_ PKIPCR Pcr,
    _In_ ULONG ProcessorNumber,
    _In_ PKTHREAD IdleThread)
{
    RtlZeroMemory(Pcr, sizeof(*Pcr));

    Pcr->Self = (PKPCR)Pcr;
    Pcr->CurrentPrcb = &Pcr->Prcb;
    Pcr->LockArray = Pcr->Prcb.LockQueue;
    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;
    Pcr->StallScaleFactor = INITIAL_STALL_COUNT;

    /* nothing may dispatch before the scheduler is set up, KD lowers back to this */
    Pcr->CurrentIrql = DISPATCH_LEVEL;

    Pcr->Prcb.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->Prcb.MinorVersion = PRCB_MINOR_VERSION;
    Pcr->Prcb.BuildType = 0;
#ifndef CONFIG_SMP
    Pcr->Prcb.BuildType |= PRCB_BUILD_UNIPROCESSOR;
#endif
#if DBG
    Pcr->Prcb.BuildType |= PRCB_BUILD_DEBUG;
#endif

    Pcr->Prcb.LegacyNumber = (UCHAR)ProcessorNumber;
    Pcr->Prcb.Number = ProcessorNumber;
    Pcr->Prcb.SetMember = AFFINITY_MASK(ProcessorNumber);
    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;
    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.StartCycles = KxQueryProcessorCycleTime();
}

static
VOID
KiInitializeCpu(
    _In_ PKIPCR Pcr)
{
    ULONG Value;

    /* The vector table takes Thumb exceptions through VBAR */
    _MoveToCoprocessor((ULONG)KiArmVectorTable, 15, 0, 12, 0, 0);
    Value = _MoveFromCoprocessor(15, 0, 1, 0, 0);
    Value = (Value & ~SCTLR_V) | SCTLR_TE;
    _MoveToCoprocessor(Value, 15, 0, 1, 0, 0);

    /* Windows on ARM is hard float, the kernel itself uses VFP registers */
    Value = _MoveFromCoprocessor(15, 0, 1, 0, 2);
    _MoveToCoprocessor(Value | CPACR_CP10_CP11, 15, 0, 1, 0, 2);
    __isb(_ARM_BARRIER_SY);
    __asm__ __volatile__("vmsr fpexc, %0" : : "r" (FPEXC_EN));

    /* MIDR tells the part and revision */
    Value = _MoveFromCoprocessor(15, 0, 0, 0, 0);
    Pcr->Prcb.ProcessorState.ArchState.Cp15_Cr0_CpuId = Value;
    __isb(_ARM_BARRIER_SY);
}

VOID
NTAPI
KiInitializeHandBuiltThread(
    _In_ PKTHREAD Thread,
    _In_ PKPROCESS Process,
    _In_ PVOID Stack)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    KeInitializeThread(Process, Thread, NULL, NULL, NULL, NULL, NULL, Stack);

    Thread->NextProcessor = Prcb->Number;
    Thread->IdealProcessor = Prcb->Number;
    Thread->Priority = HIGH_PRIORITY;
    Thread->State = Running;
    Thread->Affinity = AFFINITY_MASK(Prcb->Number);
    Thread->WaitIrql = DISPATCH_LEVEL;
    Process->ActiveProcessors |= AFFINITY_MASK(Prcb->Number);
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeKernel(
    _In_ PKPROCESS InitProcess,
    _In_ PKTHREAD InitThread,
    _In_ PVOID IdleStack,
    _In_ PKPRCB Prcb,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG_PTR PageDirectory[2];
    PVOID DpcStack;

    Prcb->MultiThreadSetMaster = Prcb;

    /* the portable parts, timers, bugcheck callbacks and the service tables */
    KiInitSystem();

    InitializeListHead(&KiProcessListHead);

    /* The idle process keeps the loader's page tables, MmInitSystem fills these in */
    PageDirectory[0] = 0;
    PageDirectory[1] = 0;
    KeInitializeProcess(InitProcess, 0, MAXULONG_PTR, PageDirectory, FALSE);
    InitProcess->QuantumReset = MAXCHAR;

    /* the table TTBR0 walks belongs to the idle process too */
    InitProcess->FirstLevelTable = _MoveFromCoprocessor(15, 0, 2, 0, 0) & ~0x3FFFUL;

    KiInitializeHandBuiltThread(InitThread, InitProcess, IdleStack);

    /* Phase 0 of the memory manager drops the identity mapping the loader runs on */
    KiEarlyTrace("KiInitializeKernel: executive phase 0 next, loader console ends here\n");
    FrLdrDbgPrint = NULL;

    ExpInitializeExecutive(0, LoaderBlock);

    KiTimeIncrementReciprocal = KiComputeReciprocal(KeMaximumIncrement,
                                                    &KiTimeIncrementShiftCount);

    Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
    Prcb->MinimumDpcRate = KiMinimumDpcRate;
    Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

    DpcStack = MmCreateKernelStack(FALSE, 0);
    if (!DpcStack)
        KeBugCheckEx(NO_PAGES_AVAILABLE, 1, 0, 0, 0);
    Prcb->DpcStack = DpcStack;
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartupBootStack(VOID)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock = KeLoaderBlock;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread = (PKTHREAD)KeLoaderBlock->Thread;
    PKPROCESS Process = Thread->ApcState.Process;
    PVOID KernelStack = (PVOID)KeLoaderBlock->KernelStack;

    Prcb->ParentNode = KeNodeBlock[0];
    Prcb->ParentNode->ProcessorMask |= Prcb->SetMember;

    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KiInitSpinLocks(Prcb, Prcb->Number);

    Prcb->CurrentThread = Thread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = Thread;

    ExInitPoolLookasidePointers();

    KeLowerIrql(APC_LEVEL);
    KiEarlyTrace("KiSystemStartupBootStack: on the boot stack\n");

    if (Prcb->Number == 0)
        KiInitializeKernel(Process, Thread, KernelStack, Prcb, LoaderBlock);
    else
        KiInitializeHandBuiltThread(Thread, Process, KernelStack);

    /* Become the idle thread */
    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(Thread, 0);

    KiAcquirePrcbLock(Prcb);
    if (!Prcb->NextThread)
        KiIdleSummary |= Prcb->SetMember;
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    Thread->Priority = 0;

    _enable();
    KeLowerIrql(DISPATCH_LEVEL);
    Thread->WaitIrql = DISPATCH_LEVEL;

    KiIdleLoop();
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartup(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKTHREAD InitialThread;
    PKIPCR Pcr;
    CCHAR Cpu;

    FrLdrDbgPrint = LoaderBlock->u.I386.CommonDataArea;
    KiEarlyTrace("KiSystemStartup: loader block %p\n", LoaderBlock);

    Cpu = KeNumberProcessors++;

    if (Cpu == 0)
    {
        KeLoaderBlock = LoaderBlock;
        LoaderBlock->KernelStack = (ULONG_PTR)KiP0BootStack;
        LoaderBlock->Thread = (ULONG_PTR)&KiInitialThread.Tcb;
        LoaderBlock->Process = (ULONG_PTR)&KiInitialProcess.Pcb;
        LoaderBlock->Prcb = (ULONG_PTR)&KiInitialPcr.Prcb;
    }

    /* Nothing may touch the PCR before TPIDRPRW points at it */
    Pcr = CONTAINING_RECORD(LoaderBlock->Prcb, KIPCR, Prcb);
    InitialThread = (PKTHREAD)LoaderBlock->Thread;
    KiInitializePcr(Pcr, Cpu, InitialThread);
    _MoveToCoprocessor((ULONG)Pcr, 15, 0, 13, 0, 4);
    KiEarlyTrace("KiSystemStartup: PCR %p\n", Pcr);

    KiProcessorBlock[Cpu] = &Pcr->Prcb;
    InitialThread->ApcState.Process = (PVOID)LoaderBlock->Process;

    KiInitializeCpu(Pcr);
    KiEarlyTrace("KiSystemStartup: processor %d set up\n", Cpu);

    if (Cpu == 0)
    {
        KeFeatureBits = 0;
        KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM;
        KeProcessorLevel = (USHORT)((Pcr->Prcb.ProcessorState.ArchState.Cp15_Cr0_CpuId >> 4) & 0xFFF);
        KeProcessorRevision = (USHORT)(Pcr->Prcb.ProcessorState.ArchState.Cp15_Cr0_CpuId & 0xF);
    }

    HalInitializeProcessor(Cpu, KeLoaderBlock);
    KeActiveProcessors |= AFFINITY_MASK(Cpu);
    KiEarlyTrace("KiSystemStartup: HAL processor init done\n");

    if (Cpu == 0)
    {
        KdInitSystem(0, KeLoaderBlock);
        KiEarlyTrace("KiSystemStartup: debugger initialized\n");
        if (KdPollBreakIn())
            DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
    }

    KfRaiseIrql(HIGH_LEVEL);
    KiEarlyTrace("KiSystemStartup: switching to the boot stack %p\n", (PVOID)LoaderBlock->KernelStack);
    KiSwitchToBootStack(LoaderBlock->KernelStack);
}

/* EOF */
