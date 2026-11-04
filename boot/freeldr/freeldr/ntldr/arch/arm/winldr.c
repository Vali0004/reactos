/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     32-bit ARM page table setup and kernel hand off, after the ARM64 one
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * The kernel lays out ARMv7 short descriptors like x86 without PAE, see
 * ntoskrnl/include/internal/arm/mm.h. The loader builds that layout: a page
 * directory mapped into itself at PDE_BASE, one page table page per 4 MB
 * (four 1 KB coarse tables), and the first level table the MMU walks, kept
 * beside the directory and mapped at MI_ARM_L1_TABLE. The loader runs
 * identity mapped, the kernel sees the first GB of RAM at KSEG0.
 */

/* INCLUDES ***************************************************************/

#include <freeldr.h>
#include <cportlib/uartinfo.h>
#include "../../winldr.h"
#include "../../ntldropts.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

/* The kernel's layout, ntoskrnl/include/internal/arm/mm.h */
#define PTE_BASE                0xC0000000
#define PDE_BASE                0xC0300000
#define MI_ARM_L1_TABLE         0xC0800000

#define ARM_PDE_INDEX(Va)       ((ULONG_PTR)(Va) >> 22)
#define ARM_PTE_INDEX(Va)       (((ULONG_PTR)(Va) >> 12) & 0x3FF)

/* Small page descriptors without TEX remap, AP[0] gives kernel read write */
#define ARM_PTE_XN              0x001
#define ARM_PTE_SMALL_PAGE      0x002
#define ARM_PTE_AP0             0x010
#define ARM_PTE_NORMAL          (ARM_PTE_SMALL_PAGE | ARM_PTE_AP0 | 0x04C) /* TEX 001 C B */
#define ARM_PTE_NONCACHED       (ARM_PTE_SMALL_PAGE | ARM_PTE_AP0 | 0x040 | ARM_PTE_XN)
#define ARM_PTE_DEVICE          (ARM_PTE_SMALL_PAGE | ARM_PTE_AP0 | 0x004 | ARM_PTE_XN)

/* page tables and the directory are never executed */
#define ARM_PTE_TABLE           (ARM_PTE_NORMAL | ARM_PTE_XN)

/* A coarse table in domain 0 */
#define ARM_L1_COARSE_TABLE     0x001
#define ARM_COARSE_TABLE_SIZE   0x400
#define ARM_L1_SIZE             0x4000

/* TTBR0 with write back walks, inner shareable */
#define ARM_TTBR_WALK_CACHED    0x6A

VOID ArmSwitchTranslation(_In_ ULONG Ttbr0);

/* The UEFI framebuffer, see uefivid.c */
extern ULONG_PTR VramAddress;
extern ULONG VramSize;

/* The console copy of the screen, see vidfb.c */
PVOID VidFbGetShadowBuffer(VOID);
ULONG VidFbGetShadowSize(VOID);

extern PUCHAR ComPortAddress;

/* Set with DEBUGPORT=SCREEN, the hand off and the early kernel print on the loader console */
static BOOLEAN WinLdrpScreenTrace = FALSE;

#define WinLdrpTrace(...) \
    do { if (WinLdrpScreenTrace) WinLdrpEarlyPrint(__VA_ARGS__); } while (0)

/* GLOBALS ***************************************************************/

static PULONG PageDirectory;
static PULONG FirstLevelTable;
PFN_NUMBER SharedUserDataPfn;

/* FUNCTIONS **************************************************************/

static
VOID
MempSyncL1(
    _In_ ULONG PdeIndex)
{
    ULONG Entry, i;

    if (!(PageDirectory[PdeIndex] & ARM_PTE_SMALL_PAGE))
    {
        for (i = 0; i < 4; i++)
            FirstLevelTable[(PdeIndex * 4) + i] = 0;
        return;
    }

    Entry = (PageDirectory[PdeIndex] & ~(MM_PAGE_SIZE - 1)) | ARM_L1_COARSE_TABLE;
    for (i = 0; i < 4; i++)
        FirstLevelTable[(PdeIndex * 4) + i] = Entry + (i * ARM_COARSE_TABLE_SIZE);
}

static
VOID
MempSetPde(
    _In_ ULONG PdeIndex,
    _In_ ULONG_PTR TablePhysical)
{
    PageDirectory[PdeIndex] = (TablePhysical & ~(MM_PAGE_SIZE - 1)) | ARM_PTE_TABLE;
    MempSyncL1(PdeIndex);
}

static
PULONG
MempGetPageTable(
    _In_ ULONG_PTR VirtualAddress,
    _In_ BOOLEAN Create)
{
    ULONG PdeIndex = ARM_PDE_INDEX(VirtualAddress);
    PULONG Table;

    if (PageDirectory[PdeIndex] & ARM_PTE_SMALL_PAGE)
        return (PULONG)(PageDirectory[PdeIndex] & ~(MM_PAGE_SIZE - 1));

    if (!Create)
        return NULL;

    Table = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
    if (!Table)
        return NULL;

    RtlZeroMemory(Table, MM_PAGE_SIZE);
    MempSetPde(PdeIndex, (ULONG_PTR)Table);
    return Table;
}

static
BOOLEAN
MempMapSinglePage(
    _In_ ULONG_PTR VirtualAddress,
    _In_ ULONG_PTR PhysicalAddress,
    _In_ ULONG Attributes)
{
    PULONG Table;
    ULONG Index;

    Table = MempGetPageTable(VirtualAddress, TRUE);
    if (!Table)
    {
        ERR("No page table for %p\n", (PVOID)VirtualAddress);
        return FALSE;
    }

    Index = ARM_PTE_INDEX(VirtualAddress);
    if (Table[Index] & ARM_PTE_SMALL_PAGE)
    {
        ERR("%p is already mapped\n", (PVOID)VirtualAddress);
        return FALSE;
    }

    Table[Index] = (PhysicalAddress & ~(MM_PAGE_SIZE - 1)) | Attributes;
    return TRUE;
}

static
BOOLEAN
MempIsPageMapped(
    _In_ ULONG_PTR VirtualAddress)
{
    PULONG Table = MempGetPageTable(VirtualAddress, FALSE);

    return Table && (Table[ARM_PTE_INDEX(VirtualAddress)] & ARM_PTE_SMALL_PAGE);
}

static
PFN_NUMBER
MempMapRangeOfPages(
    _In_ ULONG_PTR VirtualAddress,
    _In_ ULONG_PTR PhysicalAddress,
    _In_ PFN_NUMBER cPages)
{
    PFN_NUMBER i;

    for (i = 0; i < cPages; i++)
    {
        if (!MempMapSinglePage(VirtualAddress, PhysicalAddress, ARM_PTE_NORMAL))
        {
            ERR("Failed to map page %ld from %p to %p\n",
                i, (PVOID)VirtualAddress, (PVOID)PhysicalAddress);
            return i;
        }
        VirtualAddress += MM_PAGE_SIZE;
        PhysicalAddress += MM_PAGE_SIZE;
    }

    return i;
}

static
BOOLEAN
MempAllocatePageTables(VOID)
{
    PULONG L1Map;
    PUCHAR L1Block;
    ULONG i;

    TRACE(">>> MempAllocatePageTables\n");

    PageDirectory = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);

    /* the first level table needs 16 KB alignment, the loader only aligns pages */
    L1Block = MmAllocateMemoryWithType(2 * ARM_L1_SIZE, LoaderMemoryData);
    L1Map = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
    if (!PageDirectory || !L1Block || !L1Map)
    {
        ERR("failed to allocate the page tables\n");
        return FALSE;
    }

    FirstLevelTable = (PULONG)ALIGN_UP_BY((ULONG_PTR)L1Block, ARM_L1_SIZE);
    RtlZeroMemory(PageDirectory, MM_PAGE_SIZE);
    RtlZeroMemory(FirstLevelTable, ARM_L1_SIZE);
    RtlZeroMemory(L1Map, MM_PAGE_SIZE);

    /* The directory maps itself, which puts the page tables at PTE_BASE */
    MempSetPde(ARM_PDE_INDEX(PTE_BASE), (ULONG_PTR)PageDirectory);

    /* The kernel reaches the first level table at MI_ARM_L1_TABLE */
    for (i = 0; i < (ARM_L1_SIZE / MM_PAGE_SIZE); i++)
    {
        L1Map[ARM_PTE_INDEX(MI_ARM_L1_TABLE + (i * MM_PAGE_SIZE))] =
            ((ULONG_PTR)FirstLevelTable + (i * MM_PAGE_SIZE)) | ARM_PTE_TABLE;
    }
    MempSetPde(ARM_PDE_INDEX(MI_ARM_L1_TABLE), (ULONG_PTR)L1Map);

    TRACE(">>> leave MempAllocatePageTables\n");
    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    ULONG_PTR Physical = StartPage * MM_PAGE_SIZE;

    TRACE(">>> MempSetupPaging(0x%lx, %ld, %p)\n",
          StartPage, NumberOfPages, PaToVa((PVOID)Physical));

    /* Identity mapping, the loader keeps running there after the switch */
    if (MempMapRangeOfPages(Physical, Physical, NumberOfPages) != NumberOfPages)
    {
        ERR("Failed to map pages %ld, %ld\n", StartPage, NumberOfPages);
        return FALSE;
    }

    if (KernelMapping)
    {
        if (MempMapRangeOfPages((ULONG_PTR)PaToVa((PVOID)Physical), Physical, NumberOfPages) != NumberOfPages)
        {
            ERR("Failed to map pages %ld, %ld\n", StartPage, NumberOfPages);
            return FALSE;
        }
    }

    return TRUE;
}

VOID
MempUnmapPage(
    _In_ PFN_NUMBER Page)
{
    UNREFERENCED_PARAMETER(Page);
}

/**
 * @brief Prints on the loader console and the debug port.
 *
 * The kernel calls this through the loader block until its own debugger is
 * up, it runs from the identity mapping of the loader.
 */
static
ULONG
WinLdrpEarlyPrint(
    _In_ PCSTR Format,
    ...)
{
    CHAR Buffer[256];
    va_list Args;
    PCHAR Current;

    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;

    /* Straight to the devices, DbgPrint would show it twice with a screen debug port */
    for (Current = Buffer; *Current != ANSI_NULL; Current++)
    {
        MachConsPutChar(*Current);
#if DBG
        if (*Current == '\n')
            Rs232PortPutByte('\r');
        Rs232PortPutByte(*Current);
#endif
    }

    return (ULONG)(Current - Buffer);
}

/* The loader console keeps drawing after the switch while the kernel prints through it */
static
VOID
WinLdrpMapFrameBuffer(VOID)
{
    ULONG_PTR Address, End, Copy;

    if (!VramAddress || !VramSize)
        return;

    End = ALIGN_UP_BY(VramAddress + VramSize, MM_PAGE_SIZE);
    for (Address = ALIGN_DOWN_BY(VramAddress, MM_PAGE_SIZE); Address < End; Address += MM_PAGE_SIZE)
    {
        if (!MempIsPageMapped(Address))
            MempMapSinglePage(Address, Address, ARM_PTE_NONCACHED);
    }

    /* Every character also lands in the copy, which can sit above the pages the loader maps */
    Copy = (ULONG_PTR)VidFbGetShadowBuffer();
    if (!Copy)
        return;

    End = ALIGN_UP_BY(Copy + VidFbGetShadowSize(), MM_PAGE_SIZE);
    for (Address = ALIGN_DOWN_BY(Copy, MM_PAGE_SIZE); Address < End; Address += MM_PAGE_SIZE)
    {
        if (!MempIsPageMapped(Address))
            MempMapSinglePage(Address, Address, ARM_PTE_NORMAL);
    }
}

/* The debug port stays reachable at its physical address */
static
VOID
WinLdrpMapDebugPorts(VOID)
{
    ULONG_PTR Page;

    if (!ComPortAddress)
        return;

    Page = (ULONG_PTR)ComPortAddress & ~(MM_PAGE_SIZE - 1);
    if (!MempIsPageMapped(Page))
        MempMapSinglePage(Page, Page, ARM_PTE_DEVICE);
}

static
BOOLEAN
WinLdrMapSpecialPages(VOID)
{
    if (!MempMapSinglePage(KI_USER_SHARED_DATA, SharedUserDataPfn * MM_PAGE_SIZE, ARM_PTE_NORMAL | ARM_PTE_XN))
    {
        ERR("Could not map KI_USER_SHARED_DATA\n");
        return FALSE;
    }

    WinLdrpMapDebugPorts();
    WinLdrpMapFrameBuffer();

    /* The page table for the 4 MB HAL address space, KI_USER_SHARED_DATA sits there too */
    MempGetPageTable(MM_HAL_VA_START, TRUE);

    return TRUE;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    TRACE("WinLdrSetProcessorContext\n");

    _disable();

    WinLdrpTrace("freeldr: switching to the kernel tables, L1 %p, directory %p\n",
                 FirstLevelTable, PageDirectory);
    ArmSwitchTranslation((ULONG)FirstLevelTable | ARM_TTBR_WALK_CACHED);
    WinLdrpTrace("freeldr: on the kernel tables\n");

    TRACE("leave WinLdrSetProcessorContext\n");
}

VOID
WinLdrSetupMachineDependent(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID SharedUserDataAddress;

    LoaderBlock->u.I386.MachineType = 0;

    /* The load options are already a kernel address, the loader still runs on its own tables */
    WinLdrpScreenTrace = (NtLdrGetOption(VaToPa(LoaderBlock->LoadOptions), "DEBUGPORT=SCREEN") != NULL);
    LoaderBlock->u.I386.CommonDataArea = WinLdrpScreenTrace ? (PVOID)WinLdrpEarlyPrint : NULL;
    WinLdrpTrace("freeldr: boot services left\n");

    SharedUserDataAddress = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderStartupPcrPage);
    if (SharedUserDataAddress == NULL)
    {
        UiMessageBox("Can't allocate SharedUserData page.");
        return;
    }
    SharedUserDataPfn = (ULONG_PTR)SharedUserDataAddress >> MM_PAGE_SHIFT;
    RtlZeroMemory(SharedUserDataAddress, MM_PAGE_SIZE);

    if (!MempAllocatePageTables())
    {
        FrLdrBugCheckWithMessage(MEMORY_INIT_FAILURE,
                                 __FILE__,
                                 __LINE__,
                                 "Failed to allocate the page tables.");
    }

    WinLdrMapSpecialPages();
    WinLdrpTrace("freeldr: page tables at %p, framebuffer %p mapped\n", PageDirectory, (PVOID)VramAddress);
}

VOID
MempDump(VOID)
{
}

/* EOF */
