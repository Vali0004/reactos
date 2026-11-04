/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AArch32 versions of the system registers the HAL uses
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * The HAL names its registers like ARM64 does. On AArch32 each of them is a
 * fixed CP15 encoding, the IDs below only select the access, they are not
 * system register numbers.
 */

#pragma once

#define ARM64_MPIDR_EL1         1
#define ARM64_CNTFRQ_EL0        2
#define ARM64_CNTV_CTL_EL0      3
#define ARM64_CNTV_CVAL_EL0     4
#define ARM64_CNTVCT            5
#define ARM64_ICC_PMR_EL1       6
#define ARM64_ICC_RPR_EL1       7
#define ARM64_ICC_SGI1R_EL1     8
#define ARM64_ICC_IAR1_EL1      9
#define ARM64_ICC_EOIR1_EL1     10
#define ARM64_ICC_BPR1_EL1      11
#define ARM64_ICC_CTLR_EL1      12
#define ARM64_ICC_SRE_EL1       13
#define ARM64_ICC_IGRPEN1_EL1   14

/* the barrier options are the same encodings */
#define _ARM64_BARRIER_SY       _ARM_BARRIER_SY
#define _ARM64_BARRIER_ISH      _ARM_BARRIER_ISH
#define _ARM64_BARRIER_ISHST    _ARM_BARRIER_ISHST

#define HALP_MRC(Value, CRn, CRm, Op2) \
    __asm__ __volatile__("mrc p15, 0, %0, " #CRn ", " #CRm ", " #Op2 : "=r" (Value))
#define HALP_MCR(Value, CRn, CRm, Op2) \
    __asm__ __volatile__("mcr p15, 0, %0, " #CRn ", " #CRm ", " #Op2 : : "r" (Value) : "memory")
#define HALP_MRRC(Low, High, Op1, CRm) \
    __asm__ __volatile__("mrrc p15, " #Op1 ", %0, %1, " #CRm : "=r" (Low), "=r" (High))
#define HALP_MCRR(Low, High, Op1, CRm) \
    __asm__ __volatile__("mcrr p15, " #Op1 ", %0, %1, " #CRm : : "r" (Low), "r" (High) : "memory")

FORCEINLINE
ULONG64
KeArm64ReadRegister(
    _In_ ULONG Register)
{
    ULONG Low = 0, High = 0;

    switch (Register)
    {
        case ARM64_MPIDR_EL1:       HALP_MRC(Low, c0, c0, 5); break;
        case ARM64_CNTFRQ_EL0:      HALP_MRC(Low, c14, c0, 0); break;
        case ARM64_CNTV_CTL_EL0:    HALP_MRC(Low, c14, c3, 1); break;
        case ARM64_CNTV_CVAL_EL0:   HALP_MRRC(Low, High, 3, c14); break;
        case ARM64_CNTVCT:          HALP_MRRC(Low, High, 1, c14); break;
        case ARM64_ICC_PMR_EL1:     HALP_MRC(Low, c4, c6, 0); break;
        case ARM64_ICC_RPR_EL1:     HALP_MRC(Low, c12, c11, 3); break;
        case ARM64_ICC_IAR1_EL1:    HALP_MRC(Low, c12, c12, 0); break;
        case ARM64_ICC_BPR1_EL1:    HALP_MRC(Low, c12, c12, 3); break;
        case ARM64_ICC_CTLR_EL1:    HALP_MRC(Low, c12, c12, 4); break;
        case ARM64_ICC_SRE_EL1:     HALP_MRC(Low, c12, c12, 5); break;
        case ARM64_ICC_IGRPEN1_EL1: HALP_MRC(Low, c12, c12, 7); break;
        default:                    __debugbreak(); break;
    }

    return ((ULONG64)High << 32) | Low;
}

FORCEINLINE
VOID
KeArm64WriteRegister(
    _In_ ULONG Register,
    _In_ ULONG64 Value)
{
    ULONG Low = (ULONG)Value, High = (ULONG)(Value >> 32);

    switch (Register)
    {
        case ARM64_CNTV_CTL_EL0:    HALP_MCR(Low, c14, c3, 1); break;
        case ARM64_CNTV_CVAL_EL0:   HALP_MCRR(Low, High, 3, c14); break;
        case ARM64_ICC_PMR_EL1:     HALP_MCR(Low, c4, c6, 0); break;
        case ARM64_ICC_SGI1R_EL1:   HALP_MCRR(Low, High, 0, c12); break;
        case ARM64_ICC_EOIR1_EL1:   HALP_MCR(Low, c12, c12, 1); break;
        case ARM64_ICC_BPR1_EL1:    HALP_MCR(Low, c12, c12, 3); break;
        case ARM64_ICC_CTLR_EL1:    HALP_MCR(Low, c12, c12, 4); break;
        case ARM64_ICC_SRE_EL1:     HALP_MCR(Low, c12, c12, 5); break;
        case ARM64_ICC_IGRPEN1_EL1: HALP_MCR(Low, c12, c12, 7); break;
        default:                    __debugbreak(); break;
    }
}

/* GICv3 takes 32-bit accesses to its 64-bit registers, low word first */
FORCEINLINE
ULONG64
HalpReadRegister64(
    _In_ volatile ULONG64 *Register)
{
    ULONG Low = READ_REGISTER_ULONG((PULONG)Register);
    ULONG High = READ_REGISTER_ULONG((PULONG)Register + 1);

    return ((ULONG64)High << 32) | Low;
}

FORCEINLINE
VOID
HalpWriteRegister64(
    _In_ volatile ULONG64 *Register,
    _In_ ULONG64 Value)
{
    WRITE_REGISTER_ULONG((PULONG)Register, (ULONG)Value);
    WRITE_REGISTER_ULONG((PULONG)Register + 1, (ULONG)(Value >> 32));
}

#define READ_REGISTER_ULONG64(Register) HalpReadRegister64(Register)
#define WRITE_REGISTER_ULONG64(Register, Value) HalpWriteRegister64((Register), (Value))
