/*
 * PROJECT:         EFI Windows Loader
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            boot/freeldr/freeldr/windows/conversion.c
 * PURPOSE:         Physical <-> Virtual addressing mode conversions (arch-specific)
 * PROGRAMMERS:     Aleksey Bragin (aleksey@reactos.org)
 */

#pragma once

#if defined(_M_ARM)
/*
 * ARM RAM rarely starts at 0, an OR would put a board's RAM at 1 GB onto the
 * page tables at 0xC0000000. The first GB of RAM goes to KSEG0 instead,
 * meminit.c keeps the loader inside it. RAM stays below KSEG0, so either
 * direction leaves an address that is already converted alone, like x86.
 */
extern PFN_NUMBER MmArmRamBasePage;

FORCEINLINE
PVOID
VaToPa(PVOID Va)
{
    if ((ULONG_PTR)Va < KSEG0_BASE)
        return Va;
    return (PVOID)((ULONG_PTR)Va - KSEG0_BASE + (MmArmRamBasePage << MM_PAGE_SHIFT));
}

FORCEINLINE
PVOID
PaToVa(PVOID Pa)
{
    if ((ULONG_PTR)Pa >= KSEG0_BASE)
        return Pa;
    return (PVOID)((ULONG_PTR)Pa - (MmArmRamBasePage << MM_PAGE_SHIFT) + KSEG0_BASE);
}
#else
/* Arch-specific addresses translation implementation */
FORCEINLINE
PVOID
VaToPa(PVOID Va)
{
    return (PVOID)((ULONG_PTR)Va & ~KSEG0_BASE);
}

FORCEINLINE
PVOID
PaToVa(PVOID Pa)
{
    return (PVOID)((ULONG_PTR)Pa | KSEG0_BASE);
}
#endif
