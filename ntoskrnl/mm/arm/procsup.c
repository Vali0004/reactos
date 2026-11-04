/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     BSD-3-Clause (https://spdx.org/licenses/BSD-3-Clause)
 * PURPOSE:     Process address spaces on ARM, after the i386 one
 * COPYRIGHT:   Copyright Jerome Gardou
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* contmem.c */
PVOID
NTAPI
MiFindContiguousMemory(
    IN PFN_NUMBER LowestPfn,
    IN PFN_NUMBER HighestPfn,
    IN PFN_NUMBER BoundaryPfn,
    IN PFN_NUMBER SizeInPages,
    IN MEMORY_CACHING_TYPE CacheType);

VOID
NTAPI
MiFreeContiguousMemory(
    IN PVOID BaseAddress);

#define MI_ARM_L1_PAGES         (MI_ARM_L1_SIZE / PAGE_SIZE)
#define MI_ARM_L1_COARSE_TABLE  0x00000001
#define MI_ARM_COARSE_TABLE_SIZE 0x400

/* FUNCTIONS ******************************************************************/

/* Fills a first level table from a page directory, each PDE gives four coarse tables */
static
VOID
MiArmBuildL1(
    _Out_ PULONG L1,
    _In_ PMMPTE PageDirectory)
{
    ULONG PdeIndex, Entry, i;

    for (PdeIndex = 0; PdeIndex < PDE_PER_PAGE; PdeIndex++)
    {
        if (!PageDirectory[PdeIndex].u.Hard.Valid)
        {
            for (i = 0; i < (PDE_MAPPED_VA / _1MB); i++)
                L1[(PdeIndex * (PDE_MAPPED_VA / _1MB)) + i] = 0;
            continue;
        }

        Entry = (PFN_FROM_PTE(&PageDirectory[PdeIndex]) << PAGE_SHIFT) | MI_ARM_L1_COARSE_TABLE;
        for (i = 0; i < (PDE_MAPPED_VA / _1MB); i++)
            L1[(PdeIndex * (PDE_MAPPED_VA / _1MB)) + i] = Entry + (i * MI_ARM_COARSE_TABLE_SIZE);
    }

    /* the table walker reads memory, not the data cache it was written through */
    __dsb(_ARM_BARRIER_ISHST);
}

BOOLEAN
MiArchCreateProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    PFN_NUMBER PdeIndex = DirectoryTableBase[0] >> PAGE_SHIFT;
    PFN_NUMBER HyperIndex = DirectoryTableBase[1] >> PAGE_SHIFT;
    PMMPTE PointerPte, L1Map;
    MMPTE PdePte, TempPte;
    PMMPTE PteTable;
    ULONG PdeOffset, i;
    PULONG L1;
    KIRQL OldIrql;

    /* The first level table, 16KB aligned, and the page table that maps it into the process */
    L1 = MiFindContiguousMemory(0, MmHighestPhysicalPage, MI_ARM_L1_PAGES, MI_ARM_L1_PAGES, MmCached);
    if (!L1)
        return FALSE;

    L1Map = MiFindContiguousMemory(0, MmHighestPhysicalPage, 0, 1, MmCached);
    if (!L1Map)
    {
        MiFreeContiguousMemory(L1);
        return FALSE;
    }
    RtlZeroMemory(L1Map, PAGE_SIZE);

    for (i = 0; i < MI_ARM_L1_PAGES; i++)
    {
        TempPte = ValidKernelPteLocal;
        TempPte.u.Hard.PageFrameNumber = MmGetPhysicalAddress((PUCHAR)L1 + (i * PAGE_SIZE)).QuadPart >> PAGE_SHIFT;
        L1Map[MiAddressToPteOffset((PUCHAR)MI_ARM_L1_TABLE + (i * PAGE_SIZE))] = TempPte;
    }

    /* Get a PTE  */
    PointerPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!PointerPte)
    {
        MiFreeContiguousMemory(L1Map);
        MiFreeContiguousMemory(L1);
        return FALSE;
    }
    PteTable = MiPteToAddress(PointerPte);

    /* Build a page table for hyper space */
    MI_MAKE_HARDWARE_PTE_KERNEL(&PdePte,
                                PointerPte,
                                MM_READWRITE,
                                HyperIndex);
    MI_WRITE_VALID_PTE(PointerPte, PdePte);

    /* Now write the PTE/PDE entry for the working set list index itself */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = Process->WorkingSetPage;
    PdeOffset = MiAddressToPteOffset(MmWorkingSetList);
    PteTable[PdeOffset] = TempPte;

    /* Now we map the page directory */
    MI_MAKE_HARDWARE_PTE_KERNEL(&PdePte,
                                PointerPte,
                                MM_READWRITE,
                                PdeIndex);
    *PointerPte = PdePte;
    KeInvalidateTlbEntry(PteTable);

    /* Now get the page directory (which we'll double map, so call it a page table) */
    PteTable = MiPteToAddress(PointerPte);

    /* Copy all the kernel mappings */
    PdeOffset = MiGetPdeOffset(MmSystemRangeStart);
    RtlCopyMemory(&PteTable[PdeOffset],
                  MiAddressToPde(MmSystemRangeStart),
                  PAGE_SIZE - PdeOffset * sizeof(MMPTE));

    /* Now write the PTE/PDE entry for hyperspace itself */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = HyperIndex;
    PdeOffset = MiGetPdeOffset(HYPER_SPACE);
    PteTable[PdeOffset] = TempPte;

    /* Sanity check */
    PdeOffset++;
    ASSERT(MiGetPdeOffset(MmHyperSpaceEnd) >= PdeOffset);

    /* The process sees its own first level table */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = MmGetPhysicalAddress(L1Map).QuadPart >> PAGE_SHIFT;
    PteTable[MiGetPdeOffset(MI_ARM_L1_TABLE)] = TempPte;

    /* Now do the x86 trick of making the PDE a page table itself */
    PdeOffset = MiGetPdeOffset(PTE_BASE);
    TempPte.u.Hard.PageFrameNumber = PdeIndex;
    PteTable[PdeOffset] = TempPte;

    /* The MMU walks the first level table, built from the finished directory */
    MiArmBuildL1(L1, PteTable);

    /* Let go of the system PTE */
    MiReleaseSystemPtes(PointerPte, 1, SystemPteSpace);

    Process->Pcb.FirstLevelTable = (ULONG_PTR)MmGetPhysicalAddress(L1).QuadPart;
    Process->Pcb.FirstLevelTableVa = L1;
    Process->Pcb.FirstLevelTableMapVa = L1Map;

    /* Insert us into the Mm process list */
    OldIrql = MiAcquireExpansionLock();
    InsertTailList(&MmProcessList, &Process->MmProcessLinks);
    MiReleaseExpansionLock(OldIrql);

    return TRUE;
}

/* The counterpart for MmDeleteProcessAddressSpace, the process no longer runs */
VOID
MiArchDeleteProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    if (Process->Pcb.FirstLevelTableMapVa)
        MiFreeContiguousMemory(Process->Pcb.FirstLevelTableMapVa);
    if (Process->Pcb.FirstLevelTableVa)
        MiFreeContiguousMemory(Process->Pcb.FirstLevelTableVa);

    Process->Pcb.FirstLevelTable = 0;
    Process->Pcb.FirstLevelTableVa = NULL;
    Process->Pcb.FirstLevelTableMapVa = NULL;
}

/* EOF */
