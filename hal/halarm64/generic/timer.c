/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Generic timer, clock interrupt and stall support
 * COPYRIGHT:   Copyright 2026 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* CNTV_CTL_EL0 bits */
#define CNTV_CTL_ENABLE     0x1
#define CNTV_CTL_IMASK      0x2

/* GLOBALS *******************************************************************/

ULONG64 HalpTimerFrequency;

#ifdef _M_ARM
/* cortex-a8 class cores have no generic timer, they wait for an extension timer */
static BOOLEAN HalpNoGenericTimer;

static
BOOLEAN
HalpHasGenericTimer(VOID)
{
    static BOOLEAN Checked;
    ULONG Pfr1;

    if (!Checked)
    {
        __asm__ __volatile__("mrc p15, 0, %0, c0, c1, 1" : "=r" (Pfr1));
        HalpNoGenericTimer = ((Pfr1 >> 16) & 0xF) == 0;
        Checked = TRUE;
    }

    return !HalpNoGenericTimer;
}
#else
#define HalpHasGenericTimer() TRUE
#endif
static ULONG64 HalpClockPeriod;
static ULONG HalpCurrentTimeIncrement = HALP_CLOCK_INCREMENT;
static KINTERRUPT HalpClockInterrupt;

/* FUNCTIONS *****************************************************************/

FORCEINLINE
ULONG64
HalpReadCounter(VOID)
{
    if (HalpExtTimer)
        return HalpExtReadCounter();

#ifdef _M_ARM
    /* the pmu cycle counter the kernel started, only until a real timer is picked */
    if (!HalpHasGenericTimer())
    {
        ULONG Cycles;

        __asm__ __volatile__("mrc p15, 0, %0, c9, c13, 0" : "=r" (Cycles));
        return Cycles;
    }
#endif

    __isb(_ARM64_BARRIER_SY);
    return KeArm64ReadRegister(ARM64_CNTVCT);
}

/* Converts a duration in 100 ns units into counter ticks */
static
ULONG64
HalpTimeToTicks(
    _In_ ULONG64 Time)
{
    return (Time * HalpTimerFrequency) / 10000000ULL;
}

VOID
NTAPI
HalpInitializeClock(VOID)
{
    if (HalpExtTimerInitialize(&HalpTimerFrequency))
    {
        KeSetTimeIncrement(HALP_CLOCK_INCREMENT, HALP_CLOCK_INCREMENT);
        return;
    }

    /* no extension timer and no generic timer leaves nothing to keep time with */
    if (!HalpHasGenericTimer())
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7173, 4, 0, 0);

    HalpTimerFrequency = KeArm64ReadRegister(ARM64_CNTFRQ_EL0) & 0xFFFFFFFF;
    if (!HalpTimerFrequency)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7173, 0, 0, 0);

    HalpClockPeriod = HalpTimeToTicks(HalpCurrentTimeIncrement);

    /* Keep the timer quiet until the clock interrupt is connected */
    KeArm64WriteRegister(ARM64_CNTV_CTL_EL0, CNTV_CTL_IMASK);

    KeSetTimeIncrement(HALP_CLOCK_INCREMENT, HALP_CLOCK_INCREMENT);
}

static
BOOLEAN
NTAPI
HalpClockInterruptHandler(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PKTRAP_FRAME TrapFrame;
    ULONG64 Compare;

    UNREFERENCED_PARAMETER(ServiceContext);

    /* Stay on the grid from the previous deadline so the clock does not drift */
    Compare = KeArm64ReadRegister(ARM64_CNTV_CVAL_EL0) + HalpClockPeriod;
    if ((LONG64)(Compare - HalpReadCounter()) <= 0)
        Compare = HalpReadCounter() + HalpClockPeriod;
    KeArm64WriteRegister(ARM64_CNTV_CVAL_EL0, Compare);

    TrapFrame = Interrupt->TrapFrame;
    KeUpdateSystemTime(TrapFrame, HalpCurrentTimeIncrement, TrapFrame->PreviousIrql);
    return TRUE;
}

VOID
NTAPI
HalpStartClockInterrupt(VOID)
{
    if (HalpExtTimer)
    {
        HalpExtStartClockInterrupt();
        return;
    }

    KeInitializeInterrupt(&HalpClockInterrupt,
                          HalpClockInterruptHandler,
                          NULL,
                          NULL,
                          HalpPlatform.TimerIntId,
                          CLOCK_LEVEL,
                          CLOCK_LEVEL,
                          LevelSensitive,
                          FALSE,
                          0,
                          FALSE);

    KeArm64WriteRegister(ARM64_CNTV_CVAL_EL0, HalpReadCounter() + HalpClockPeriod);
    KeArm64WriteRegister(ARM64_CNTV_CTL_EL0, CNTV_CTL_ENABLE);

    if (!KeConnectInterrupt(&HalpClockInterrupt))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7173, 1, HalpPlatform.TimerIntId, 0);
}

VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONG64 Start, Ticks;

    /* before a timer is known the wait is a rough spin */
    if (!HalpExtTimer && !HalpHasGenericTimer())
    {
        volatile ULONG Spin;

        for (Spin = 0; Spin < MicroSeconds * 200; Spin++)
            YieldProcessor();
        return;
    }

    Ticks = ((ULONG64)MicroSeconds * HalpTimerFrequency + 999999) / 1000000;
    Start = HalpReadCounter();

    while ((HalpReadCounter() - Start) < Ticks)
        YieldProcessor();
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Value;

    if (PerformanceFrequency)
        PerformanceFrequency->QuadPart = HalpTimerFrequency;

    Value.QuadPart = HalpReadCounter();
    return Value;
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ volatile PLONG Count,
    _In_ ULONGLONG NewCount)
{
    UNREFERENCED_PARAMETER(NewCount);

    /* The architected counter is shared by every processor */
    InterlockedDecrement(Count);
    while (*Count)
        YieldProcessor();
}

ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    /* The clock runs at one fixed rate */
    UNREFERENCED_PARAMETER(Increment);
    return HalpCurrentTimeIncrement;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    return Interval;
}

VOID
NTAPI
HalStartProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}
