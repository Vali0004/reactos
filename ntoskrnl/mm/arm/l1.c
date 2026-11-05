/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM first level table, kept in step with the software page directory
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* a coarse page table descriptor, domain 0 */
#define MI_ARM_L1_COARSE_TABLE  0x00000001
#define MI_ARM_COARSE_TABLE_SIZE 0x400

/* FUNCTIONS ******************************************************************/

/**
 * @brief Mirrors a software PDE into the first level table.
 *
 * A PDE maps one page table page, four 1KB coarse tables of 1MB each. The
 * four first level entries of its 4MB range point at them, or fault when the
 * PDE is not valid.
 */
VOID
NTAPI
MiArmSyncL1(
    _In_ PMMPDE PointerPde)
{
    ULONG PdeIndex, Entry, i;
    PULONG L1;

    /*
     * MmSystemPagePtes maps the system directory a second time, a store there
     * lands in the live directory too when it is the current one
     */
    if (((ULONG_PTR)PointerPde < PDE_BASE) || ((ULONG_PTR)PointerPde > PDE_TOP))
    {
        PMMPDE LivePde = (PMMPDE)PDE_BASE +
                         (((ULONG_PTR)PointerPde & (SYSTEM_PD_SIZE - 1)) / sizeof(MMPDE));

        if (LivePde->u.Long != PointerPde->u.Long)
            return;
        PointerPde = LivePde;
    }

    PdeIndex = (ULONG)(PointerPde - (PMMPDE)PDE_BASE);

    L1 = MI_ARM_L1_TABLE + (PdeIndex * (PDE_MAPPED_VA / _1MB));

    if (PointerPde->u.Hard.Valid)
    {
        Entry = (PFN_FROM_PTE(PointerPde) << PAGE_SHIFT) | MI_ARM_L1_COARSE_TABLE;
        for (i = 0; i < (PDE_MAPPED_VA / _1MB); i++)
            L1[i] = Entry + (i * MI_ARM_COARSE_TABLE_SIZE);

        /* nothing was cached for a range that faulted, the walker only needs to see the stores */
        __dsb(_ARM_BARRIER_ISHST);
        __isb(_ARM_BARRIER_SY);
    }
    else
    {
        for (i = 0; i < (PDE_MAPPED_VA / _1MB); i++)
            L1[i] = 0;

        /* the range may have live translations, drop them everywhere */
        __dsb(_ARM_BARRIER_ISHST);
        _MoveToCoprocessor(0, 15, 0, 8, 3, 0); /* TLBIALLIS */
        __dsb(_ARM_BARRIER_ISH);
        __isb(_ARM_BARRIER_SY);
    }
}

/* a kernel fault on an address whose pde this process' first level table lacks
 * returns TRUE once the pde is mirrored and the access can be retried */
BOOLEAN
NTAPI
MiArmFixKernelL1(
    _In_ PVOID Address)
{
    PMMPDE PointerPde = MiAddressToPde(Address);
    PULONG L1 = MI_ARM_L1_TABLE + ((ULONG_PTR)Address >> 20);
    ULONG Expected;

    if (!PointerPde->u.Hard.Valid)
        return MiSynchronizeSystemPde(PointerPde);

    Expected = (PFN_FROM_PTE(PointerPde) << PAGE_SHIFT) | MI_ARM_L1_COARSE_TABLE;
    Expected += (((ULONG_PTR)Address >> 20) & ((PDE_MAPPED_VA / _1MB) - 1)) * MI_ARM_COARSE_TABLE_SIZE;
    if (*L1 == Expected)
        return FALSE;

    MiArmSyncL1(PointerPde);
    return TRUE;
}

/* EOF */
