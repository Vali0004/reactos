/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm/cpu.c
 * PURPOSE:         Implements routines for ARM CPU support
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG KeFixedTbEntries;
ULONG KiDmaIoCoherency;
ULONG KeIcacheFlushCount = 0;
ULONG KeDcacheFlushCount;
ULONG KeLargestCacheLine = 64; // FIXME: It depends

/* FUNCTIONS ******************************************************************/

VOID
KiFlushSingleTb(IN BOOLEAN Invalid,
                IN PVOID Virtual)
{
    //
    // Just invalidate it
    //
    KeArmInvalidateTlbEntry(Virtual);
}

VOID
KeFlushTb(VOID)
{
    //
    // Flush the entire TLB
    //
    KeArmFlushTlb();
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    //
    // Rename?
    //
    KeFlushTb();
}

VOID
FASTCALL
KeZeroPages(IN PVOID Address,
            IN ULONG Size)
{
    /* Not using XMMI in this routine */
    RtlZeroMemory(Address, Size);
}

VOID
NTAPI
KiSaveProcessorControlState(OUT PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Special = &ProcessorState->SpecialRegisters;
    PKARM_ARCH_STATE Arch = &ProcessorState->ArchState;

    /* the thread ID registers, user TEB and kernel PCR */
    Special->Cp15_Cr13_UsrRW = _MoveFromCoprocessor(15, 0, 13, 0, 2);
    Special->Cp15_Cr13_UsrRO = _MoveFromCoprocessor(15, 0, 13, 0, 3);
    Special->Cp15_Cr13_SvcRW = _MoveFromCoprocessor(15, 0, 13, 0, 4);

    /* what the debugger shows of the MMU and the last abort */
    Arch->Cp15_Cr0_CpuId = _MoveFromCoprocessor(15, 0, 0, 0, 0);
    Arch->Cp15_Cr1_Control = _MoveFromCoprocessor(15, 0, 1, 0, 0);
    Arch->Cp15_Cr1_AuxControl = _MoveFromCoprocessor(15, 0, 1, 0, 1);
    Arch->Cp15_Cr1_Cpacr = _MoveFromCoprocessor(15, 0, 1, 0, 2);
    Arch->Cp15_Cr2_Ttb0 = _MoveFromCoprocessor(15, 0, 2, 0, 0);
    Arch->Cp15_Cr2_Ttb1 = _MoveFromCoprocessor(15, 0, 2, 0, 1);
    Arch->Cp15_Cr2_TtbControl = _MoveFromCoprocessor(15, 0, 2, 0, 2);
    Arch->Cp15_Cr3_Dacr = _MoveFromCoprocessor(15, 0, 3, 0, 0);
    Arch->Cp15_Cr5_Dfsr = _MoveFromCoprocessor(15, 0, 5, 0, 0);
    Arch->Cp15_Cr5_Ifsr = _MoveFromCoprocessor(15, 0, 5, 0, 1);
    Arch->Cp15_Cr6_Dfar = _MoveFromCoprocessor(15, 0, 6, 0, 0);
    Arch->Cp15_Cr6_Ifar = _MoveFromCoprocessor(15, 0, 6, 0, 2);
    Arch->Cp15_Cr10_PrimaryMemoryRemap = _MoveFromCoprocessor(15, 0, 10, 2, 0);
    Arch->Cp15_Cr10_NormalMemoryRemap = _MoveFromCoprocessor(15, 0, 10, 2, 1);
    Arch->Cp15_Cr12_VBARns = _MoveFromCoprocessor(15, 0, 12, 0, 0);
}

VOID
NTAPI
KiRestoreProcessorControlState(PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Special = &ProcessorState->SpecialRegisters;

    /* the MMU state is not the debugger's to change, only the thread ID registers come back */
    _MoveToCoprocessor(Special->Cp15_Cr13_UsrRW, 15, 0, 13, 0, 2);
    _MoveToCoprocessor(Special->Cp15_Cr13_UsrRO, 15, 0, 13, 0, 3);
    _MoveToCoprocessor(Special->Cp15_Cr13_SvcRW, 15, 0, 13, 0, 4);
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    //
    // Invalidate D cache and I cache
    //
    KeArmInvalidateAllCaches();
    return TRUE;
}


/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    /* Return the global variable */
    return KeLargestCacheLine;
}

/*
 * @implemented
 */
VOID
NTAPI
KeFlushEntireTb(IN BOOLEAN Invalid,
                IN BOOLEAN AllProcessors)
{
    KIRQL OldIrql;

    //
    // Raise the IRQL for the TB Flush
    //
    OldIrql = KeRaiseIrqlToSynchLevel();

    //
    // Flush the TB for the Current CPU
    //
    KeFlushCurrentTb();

    //
    // Return to Original IRQL
    //
    KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetDmaIoCoherency(IN ULONG Coherency)
{
    //
    // Save the coherency globally
    //
    KiDmaIoCoherency = Coherency;
}

/*
 * @implemented
 */
ULONG
NTAPI
KeGetDmaIoCoherency(VOID)
{
    return KiDmaIoCoherency;
}

/*
 * @implemented
 */
VOID
__cdecl
KeSaveStateForHibernate(IN PKPROCESSOR_STATE State)
{
    //
    // Capture the context
    //
    RtlCaptureContext(&State->ContextFrame);

    //
    // Capture the control state
    //
    KiSaveProcessorControlState(State);
}

VOID
NTAPI
KeFlushIoBuffers(
    _In_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    ULONG_PTR Address, End;
    ULONG Ctr, LineSize;

    /* DMA is coherent on the supported machines, a CPU copy may be code that runs later */
    if (DmaOperation || !ReadOperation)
        return;

    if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
    {
        /* smallest D-cache line from CTR.DminLine, in words */
        Ctr = _MoveFromCoprocessor(15, 0, 0, 0, 1);
        LineSize = 4 << ((Ctr >> 16) & 0xF);

        Address = (ULONG_PTR)Mdl->MappedSystemVa & ~(LineSize - 1);
        End = (ULONG_PTR)Mdl->MappedSystemVa + Mdl->ByteCount;
        for (; Address < End; Address += LineSize)
            _MoveToCoprocessor(Address, 15, 0, 7, 11, 1);    /* DCCMVAU */
        __dsb(_ARM_BARRIER_ISH);
    }

    /* the I-cache and branch predictor forget whatever they held for these pages */
    KiArmSweepIcache();
    __dsb(_ARM_BARRIER_ISH);
    __isb(_ARM_BARRIER_SY);
}

/* SYSTEM CALLS NOT VALID ON THIS CPU *****************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtVdmControl(IN ULONG ControlCode,
             IN PVOID ControlData)
{
    //
    // Does not exist on ARM
    //
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetLdtEntries(IN ULONG Selector1,
                IN LDT_ENTRY LdtEntry1,
                IN ULONG Selector2,
                IN LDT_ENTRY LdtEntry2)
{
    //
    // Does not exist on ARM
    //
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Hands the kernel debugger to another processor.
 *
 * Only the boot processor runs on ARM so far, the debugger stays where it is.
 */
KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    ASSERT(ProcessorIndex == 0);
    UNREFERENCED_PARAMETER(ProcessorIndex);
    return ContinueSuccess;
}
