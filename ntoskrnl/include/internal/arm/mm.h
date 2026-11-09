/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM memory management definitions
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * ARMv7 short descriptors, laid out like x86 without PAE so ARM3 walks the
 * tables it knows. A page table page holds the four 1KB coarse tables of a
 * 4MB range, 1024 PTEs. The software page directory is one page of 1024 PDEs
 * mapped into itself at PDE_BASE, every PDE is a small page PTE mapping a page
 * table page into the PTE window. The hardware first level table (4096 entries
 * of 1MB) is kept beside it, MiArmSyncL1 mirrors each PDE into its 4 entries.
 */

#pragma once

#define _MI_PAGING_LEVELS 2
#define _MI_HAS_NO_EXECUTE 1

/* Memory layout base addresses */
#define MI_USER_PROBE_ADDRESS                   (PVOID)0x7FFF0000
#define MI_DEFAULT_SYSTEM_RANGE_START           (PVOID)0x80000000
#define HYPER_SPACE                                    0xC0400000
#define HYPER_SPACE_END                                0xC07FFFFF
#define MI_SYSTEM_CACHE_WS_START                (PVOID)0xC0C00000
#define MI_SYSTEM_CACHE_START                   (PVOID)0xC1000000
#define MI_PAGED_POOL_START                     (PVOID)0xE1000000
#define MI_NONPAGED_POOL_END                    (PVOID)0xFFBE0000
#define MI_DEBUG_MAPPING                        (PVOID)0xFFBFF000
#define MI_HIGHEST_SYSTEM_ADDRESS               (PVOID)0xFFFFFFFF

/* The per process 16KB hardware first level table, in the range x86 PAE gives hyperspace */
#define MI_ARM_L1_TABLE                         (PULONG)0xC0800000
#define MI_ARM_L1_ENTRIES                       4096
#define MI_ARM_L1_SIZE                          (MI_ARM_L1_ENTRIES * sizeof(ULONG))

/* Misc address definitions */
#define MM_HIGHEST_VAD_ADDRESS \
    (PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE))
#define MI_MAPPING_RANGE_START              (ULONG)HYPER_SPACE
#define MI_MAPPING_RANGE_END                (MI_MAPPING_RANGE_START + \
                                                  MI_HYPERSPACE_PTES * PAGE_SIZE)
#define MI_DUMMY_PTE                        (PMMPTE)((ULONG_PTR)MI_MAPPING_RANGE_END + \
                                                  PAGE_SIZE)
#define MI_VAD_BITMAP                       (PMMPTE)((ULONG_PTR)MI_DUMMY_PTE + \
                                                  PAGE_SIZE)
#define MI_WORKING_SET_LIST                 (PMMPTE)((ULONG_PTR)MI_VAD_BITMAP + \
                                                  PAGE_SIZE)

/* Memory sizes */
#define MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING   ((255 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_TUNING          ((19 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST           ((32 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST     ((256 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_INIT_PAGED_POOLSIZE              (32 * _1MB)
#define MI_MAX_INIT_NONPAGED_POOL_SIZE          (128 * _1MB)
#define MI_MAX_NONPAGED_POOL_SIZE               (128 * _1MB)
#define MI_SYSTEM_VIEW_SIZE                     (32 * _1MB)
#define MI_SESSION_VIEW_SIZE                    (48 * _1MB)
#define MI_SESSION_POOL_SIZE                    (16 * _1MB)
#define MI_SESSION_IMAGE_SIZE                   (8 * _1MB)
#define MI_SESSION_WORKING_SET_SIZE             (4 * _1MB)
#define MI_SESSION_SIZE                         (MI_SESSION_VIEW_SIZE + \
                                                 MI_SESSION_POOL_SIZE + \
                                                 MI_SESSION_IMAGE_SIZE + \
                                                 MI_SESSION_WORKING_SET_SIZE)
#define MI_MIN_ALLOCATION_FRAGMENT              (4 * _1KB)
#define MI_ALLOCATION_FRAGMENT                  (64 * _1KB)
#define MI_MAX_ALLOCATION_FRAGMENT              (2  * _1MB)

/* Misc constants, the software protection sits above the 2 type bits */
#define MM_PTE_SOFTWARE_PROTECTION_BITS         5
#define MI_MIN_SECONDARY_COLORS                 8
#define MI_SECONDARY_COLORS                     64
#define MI_MAX_SECONDARY_COLORS                 1024
#define MI_MAX_FREE_PAGE_LISTS                  4
#define MI_HYPERSPACE_PTES                     (256 - 1)
#define MI_ZERO_PTES                           (32)
#define MI_MAX_ZERO_BITS                        21
#define SESSION_POOL_LOOKASIDES                 26

/* MMPTE related defines */
#define MM_EMPTY_PTE_LIST  ((ULONG)0xFFFFF)
#define MM_EMPTY_LIST  ((ULONG_PTR)-1)

/* Easy accessing PFN in PTE */
#define PFN_FROM_PTE(v) ((v)->u.Hard.PageFrameNumber)

/*
 * Small page attributes without TEX remap: TEX C B
 *   normal write back  001 1 1
 *   normal uncached    001 0 0
 *   device             000 0 1
 */
#define MI_MAKE_PTE_CACHED(Pte)                                        \
    do                                                                 \
    {                                                                  \
        (Pte)->u.Hard.CacheAttributes = 1;                             \
        (Pte)->u.Hard.Cached = 1;                                      \
        (Pte)->u.Hard.Buffered = 1;                                    \
    } while (0)
#define MI_MAKE_PTE_NON_CACHED(Pte)                                    \
    do                                                                 \
    {                                                                  \
        (Pte)->u.Hard.CacheAttributes = 0;                             \
        (Pte)->u.Hard.Cached = 0;                                      \
        (Pte)->u.Hard.Buffered = 1;                                    \
    } while (0)
#define MI_MAKE_PTE_WRITE_COMBINE(Pte)                                 \
    do                                                                 \
    {                                                                  \
        (Pte)->u.Hard.CacheAttributes = 1;                             \
        (Pte)->u.Hard.Cached = 0;                                      \
        (Pte)->u.Hard.Buffered = 0;                                    \
    } while (0)

/* There is no hardware dirty or accessed tracking with the access flag off */
#define MI_MAKE_DIRTY_PAGE(x)
#define MI_MAKE_CLEAN_PAGE(x)
#define MI_MAKE_ACCESSED_PAGE(x)
#define MI_IS_PAGE_LARGE(x)        FALSE
#define MI_IS_PAGE_WRITEABLE(x)    ((x)->u.Hard.ReadOnly == 0)
#define MI_IS_PAGE_COPY_ON_WRITE(x) FALSE
#define MI_IS_PAGE_EXECUTABLE(x)   ((x)->u.Hard.NoExecute == 0)
/* a read-only page cannot have been written through, a writable one may have */
#define MI_IS_PAGE_DIRTY(x)        MI_IS_PAGE_WRITEABLE(x)
#define MI_MAKE_OWNER_PAGE(x)      ((x)->u.Hard.Owner = 1)
#define MI_MAKE_WRITE_PAGE(x)      ((x)->u.Hard.ReadOnly = 0)

/* The trap handler encodes the abort like an x86 page fault error code */
#define MI_IS_NOT_PRESENT_FAULT(FaultCode)  !BooleanFlagOn(FaultCode, 0x00000001)
#define MI_IS_WRITE_ACCESS(FaultCode)        BooleanFlagOn(FaultCode, 0x00000002)
#define MI_IS_INSTRUCTION_FETCH(FaultCode)   BooleanFlagOn(FaultCode, 0x00000010)

/* PDEs and PTEs have the same format */
#define MI_WRITE_VALID_PPE MI_WRITE_VALID_PTE

/* One page directory page */
#define PD_COUNT 1

/* The number of PTEs on one page of the PT */
#define PTE_PER_PAGE (PAGE_SIZE / sizeof(MMPTE))

/* The number of PDEs on one page of the PD */
#define PDE_PER_PAGE (PAGE_SIZE / sizeof(MMPDE))

/* Maximum number of PDEs */
#define PDE_PER_SYSTEM (PD_COUNT * PDE_PER_PAGE)

#define PPE_PER_PAGE 1

/* Maximum number of pages for 4 GB of virtual space */
#define MI_MAX_PAGES ((1ull << 32) / PAGE_SIZE)

/* Base addresses for page tables */
#define PTE_BASE (ULONG_PTR)0xC0000000
#define PTE_TOP  (ULONG_PTR)(PTE_BASE + (MI_MAX_PAGES * sizeof(MMPTE)) - 1)
#define PTE_MASK (PTE_TOP - PTE_BASE)

#define MI_SYSTEM_PTE_BASE (PVOID)MiAddressToPte(NULL)

/* Base addreses for page directories */
#define PDE_BASE (ULONG_PTR)MiPteToPde(PTE_BASE)
#define PDE_TOP  (ULONG_PTR)(PDE_BASE + (PDE_PER_SYSTEM * sizeof(MMPDE)) - 1)
#define PDE_MASK (PDE_TOP - PDE_BASE)

/* The size of the virtual memory area that is mapped using a single PDE */
#define PDE_MAPPED_VA (PTE_PER_PAGE * PAGE_SIZE)

/* Maps the virtual address to the corresponding PTE */
#define MiAddressToPte(Va) \
    ((PMMPTE)(PTE_BASE + ((((ULONG_PTR)(Va)) / PAGE_SIZE) * sizeof(MMPTE))))

/* Maps the virtual address to the corresponding PDE */
#define MiAddressToPde(Va) \
    ((PMMPDE)(PDE_BASE + ((MiAddressToPdeOffset(Va)) * sizeof(MMPDE))))

/* Takes the PTE index (for one PD page) from the virtual address */
#define MiAddressToPteOffset(Va) \
    ((((ULONG_PTR)(Va)) & (PDE_MAPPED_VA - 1)) / PAGE_SIZE)

/* Takes the PDE offset (within all PDs pages) from the virtual address */
#define MiAddressToPdeOffset(Va) (((ULONG_PTR)(Va)) / PDE_MAPPED_VA)

#define MiGetPdeOffset MiAddressToPdeOffset

/* Convert a PTE/PDE into a corresponding address */
#define MiPteToAddress(_Pte) ((PVOID)((ULONG)(_Pte) << 10))
#define MiPdeToAddress(_Pde) ((PVOID)((ULONG)(_Pde) << 20))

/* Translate between P*Es */
#define MiPdeToPte(_Pde) ((PMMPTE)MiPteToAddress(_Pde))
#define MiPteToPde(_Pte) ((PMMPDE)MiAddressToPte(_Pte))

/* Check P*E boundaries */
#define MiIsPteOnPdeBoundary(PointerPte) \
    ((((ULONG_PTR)(PointerPte)) & (PAGE_SIZE - 1)) == 0)

//
// Decodes a Prototype PTE into the underlying PTE
//
#define MiProtoPteToPte(x)                  \
    (PMMPTE)((ULONG_PTR)MmPagedPoolStart +  \
             (((x)->u.Proto.ProtoAddressHigh << 9) | (x)->u.Proto.ProtoAddressLow << 2))

//
// Decodes a Prototype PTE into the underlying PTE
//
#define MiSubsectionPteToSubsection(x)                              \
    ((x)->u.Subsect.WhichPool == PagedPool) ?                       \
        (PMMPTE)((ULONG_PTR)MmSubsectionBase +                      \
                 (((x)->u.Subsect.SubsectionAddressHigh << 6) |     \
                   (x)->u.Subsect.SubsectionAddressLow << 3)) :     \
        (PMMPTE)((ULONG_PTR)MmNonPagedPoolEnd -                     \
                (((x)->u.Subsect.SubsectionAddressHigh << 6) |      \
                  (x)->u.Subsect.SubsectionAddressLow << 3))

/* mm/arm/procsup.c */
VOID
MiArchDeleteProcessAddressSpace(
    _In_ PEPROCESS Process);

/* Mirrors a software PDE into the 4 first level entries of its range, mm/arm/l1.c */
/* a walker that cannot snoop the data cache has to find table entries in memory, cortex-a8 is one */
FORCEINLINE
VOID
MiArmCleanTableEntry(
    _In_ PVOID Entry)
{
    ULONG Mmfr3;

    __asm__ __volatile__("mrc p15, 0, %0, c0, c1, 7" : "=r" (Mmfr3));
    if (((Mmfr3 >> 20) & 0xF) == 0)
    {
        _MoveToCoprocessor((ULONG_PTR)Entry, 15, 0, 7, 10, 1);    /* DCCMVAC */
        __dsb(_ARM_BARRIER_ISHST);
    }
}

VOID
NTAPI
MiArmSyncL1(
    _In_ PMMPDE PointerPde);

/* repairs a kernel pde missing from the first level table on a fault, mm/arm/l1.c */
BOOLEAN
NTAPI
MiArmFixKernelL1(
    _In_ PVOID Address);
