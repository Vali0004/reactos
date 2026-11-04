/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Clock and counter through timers a HAL extension registered
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "halextp.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* set once phase 0 picked extension timers over the generic timer */
BOOLEAN HalpExtTimer;

static PTIMER_INITIALIZATION_BLOCK HalpExtClock;
static PTIMER_INITIALIZATION_BLOCK HalpExtCounter;
static ULONG64 HalpExtClockPeriod;
static KINTERRUPT HalpExtClockInterrupt;

/* narrow counters are widened, the clock reads often enough to see every wrap */
static ULONG64 HalpExtCounterHigh;
static ULONG64 HalpExtCounterLast;
static ULONG64 HalpExtCounterMask;

/* FUNCTIONS *****************************************************************/

static
BOOLEAN
HalpExtCanDriveClock(
    _In_ PTIMER_INITIALIZATION_BLOCK Timer)
{
    if (!Timer->FunctionTable.ArmTimer || !Timer->CounterFrequency || (Timer->Interrupt.Gsi < 0))
        return FALSE;

    if (!(Timer->Capabilities & TIMER_GENERATES_LINE_BASED_INTERRUPTS))
        return FALSE;

    return (Timer->Capabilities & (TIMER_PERIODIC_CAPABLE | TIMER_ONE_SHOT_CAPABLE)) != 0;
}

/* picks a clock and a readable counter, FALSE keeps the generic timer */
BOOLEAN
NTAPI
HalpExtTimerInitialize(
    _Out_ PULONG64 Frequency)
{
    ULONG i;

    for (i = 0; i < HalpExtTimerCount; i++)
    {
        PTIMER_INITIALIZATION_BLOCK Timer = &HalpExtTimers[i];

        if (!HalpExtClock && HalpExtCanDriveClock(Timer))
            HalpExtClock = Timer;

        if ((Timer->Capabilities & TIMER_COUNTER_READABLE) && Timer->FunctionTable.QueryCounter &&
            Timer->CounterFrequency &&
            (!HalpExtCounter || (Timer->CounterFrequency > HalpExtCounter->CounterFrequency)))
        {
            HalpExtCounter = Timer;
        }
    }

    if (!HalpExtClock || !HalpExtCounter)
    {
        HalpExtClock = HalpExtCounter = NULL;
        return FALSE;
    }

    if (!NT_SUCCESS(HalpExtClock->FunctionTable.Initialize(HalpExtClock->InternalData)))
        return FALSE;

    if ((HalpExtCounter != HalpExtClock) &&
        !NT_SUCCESS(HalpExtCounter->FunctionTable.Initialize(HalpExtCounter->InternalData)))
    {
        return FALSE;
    }

    HalpExtCounterMask = (HalpExtCounter->CounterBitWidth >= 64) ? ~0ULL :
                         ((1ULL << HalpExtCounter->CounterBitWidth) - 1);
    HalpExtClockPeriod = (HALP_CLOCK_INCREMENT * HalpExtClock->CounterFrequency) / 10000000ULL;

    HalpExtTimer = TRUE;
    *Frequency = HalpExtCounter->CounterFrequency;
    DPRINT1("HALEXT: clock from timer %lu, counter from timer %lu at %I64u Hz\n",
            HalpExtClock->Identifier, HalpExtCounter->Identifier, *Frequency);
    return TRUE;
}

ULONG64
NTAPI
HalpExtReadCounter(VOID)
{
    ULONG64 Value;
    BOOLEAN Enabled;

    Value = HalpExtCounter->FunctionTable.QueryCounter(HalpExtCounter->InternalData) & HalpExtCounterMask;
    if (HalpExtCounterMask == ~0ULL)
        return Value;

    Enabled = KeDisableInterrupts();
    if (Value < HalpExtCounterLast)
        HalpExtCounterHigh += HalpExtCounterMask + 1;
    HalpExtCounterLast = Value;
    Value += HalpExtCounterHigh;
    KeRestoreInterrupts(Enabled);

    return Value;
}

static
BOOLEAN
NTAPI
HalpExtClockInterruptHandler(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PKTRAP_FRAME TrapFrame;

    UNREFERENCED_PARAMETER(ServiceContext);

    if (HalpExtClock->FunctionTable.AcknowledgeInterrupt)
        HalpExtClock->FunctionTable.AcknowledgeInterrupt(HalpExtClock->InternalData);

    /* a one shot clock is armed again for every tick */
    if (!(HalpExtClock->Capabilities & TIMER_PERIODIC_CAPABLE))
        HalpExtClock->FunctionTable.ArmTimer(HalpExtClock->InternalData, TimerModeOneShot, HalpExtClockPeriod);

    /* keeps narrow counters widened between callers */
    HalpExtReadCounter();

    TrapFrame = Interrupt->TrapFrame;
    KeUpdateSystemTime(TrapFrame, HALP_CLOCK_INCREMENT, TrapFrame->PreviousIrql);
    return TRUE;
}

VOID
NTAPI
HalpExtStartClockInterrupt(VOID)
{
    TIMER_MODE Mode;
    NTSTATUS Status;

    KeInitializeInterrupt(&HalpExtClockInterrupt,
                          HalpExtClockInterruptHandler,
                          NULL,
                          NULL,
                          (ULONG)HalpExtClock->Interrupt.Gsi,
                          CLOCK_LEVEL,
                          CLOCK_LEVEL,
                          HalpExtClock->Interrupt.Mode,
                          FALSE,
                          0,
                          FALSE);

    if (!KeConnectInterrupt(&HalpExtClockInterrupt))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7173, 2, HalpExtClock->Interrupt.Gsi, 0);

    Mode = (HalpExtClock->Capabilities & TIMER_PERIODIC_CAPABLE) ? TimerModePeriodic : TimerModeOneShot;
    Status = HalpExtClock->FunctionTable.ArmTimer(HalpExtClock->InternalData, Mode, HalpExtClockPeriod);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7173, 3, Status, 0);
}
