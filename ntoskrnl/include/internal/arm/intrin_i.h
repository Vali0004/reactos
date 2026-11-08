#pragma once

FORCEINLINE
ARM_STATUS_REGISTER
KeArmStatusRegisterGet(VOID)
{
    ARM_STATUS_REGISTER Value;
#if defined(_MSC_VER) && !defined(__clang__)
    Value.AsUlong = _ReadStatusReg(0);
#else
    __asm__ __volatile__ ("mrs %0, cpsr" : "=r"(Value.AsUlong) : : "cc");
#endif
    return Value;
}

FORCEINLINE
VOID
KeArmFlushTlb(VOID)
{
#if defined(_MSC_VER) && !defined(__clang__)
#else
    /* the new entries must reach the walker first, nothing may use the old ones after */
    __asm__ __volatile__ ("dsb ishst\n\t"
                          "mcr p15, 0, %0, c8, c7, 0\n\t"
                          "mcr p15, 0, %0, c7, c5, 6\n\t"
                          "dsb ish\n\t"
                          "isb sy" : : "r"(0) : "cc", "memory");
#endif
}

FORCEINLINE
VOID
KeArmInvalidateTlbEntry(IN PVOID Address)
{
#if defined(_MSC_VER) && !defined(__clang__)
#else
    /* the new entry must reach the walker first, nothing may use the old one after */
    __asm__ __volatile__ ("dsb ishst\n\t"
                          "mcr p15, 0, %0, c8, c7, 1\n\t"
                          "mcr p15, 0, %1, c7, c5, 6\n\t"
                          "dsb ish\n\t"
                          "isb sy" : : "r"((ULONG_PTR)Address & ~0xFFFUL), "r"(0) : "cc", "memory");
#endif
}

FORCEINLINE
VOID
KeArmFlushIcache(VOID)
{
#if defined(_MSC_VER) && !defined(__clang__)
#else
    __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 0" : : "r"(0) : "cc");
#endif
}
