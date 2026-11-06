/*
 * PROJECT:     ReactOS HAL extension for NVIDIA Tegra 3
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Tegra timer block through the extension interface, the hal keeps the gic
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntddk.h>
#include <ddk/nthalext.h>
#include "tegra3csrt.h"

/* GLOBALS *******************************************************************/

typedef struct _TEGRA3_TIMER
{
    PUCHAR Base;
    ULONG Timer;
} TEGRA3_TIMER, *PTEGRA3_TIMER;

static POS_EXTENSION_HAL_EXTENSION_IMPORTS HalImports;
static OS_EXTENSION_HAL_EXTENSION_EXPORTS ExtExports;
static TEGRA3_TIMER ClockTimer;
static TEGRA3_TIMER CounterTimer;
static PUCHAR TimerBlock;

static ULONG SecurityCookie = 0xBB40E64E;
static ULONG SecurityCookieComplement = ~0xBB40E64EUL;

/* FUNCTIONS *****************************************************************/

FORCEINLINE ULONG Read32(_In_ PUCHAR Base, _In_ ULONG Offset)
{
    return *(volatile ULONG *)(Base + Offset);
}

FORCEINLINE VOID Write32(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Value)
{
    *(volatile ULONG *)(Base + Offset) = Value;
}

/* tmr3, one shot, counts down in microseconds */

static
NTSTATUS
ClockInitialize(
    _In_ PVOID Data)
{
    PTEGRA3_TIMER Timer = Data;

    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PTV, 0);
    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PCR, TEGRA3_TMR_PCR_INTR_CLR);
    return STATUS_SUCCESS;
}

static
VOID
ClockAcknowledgeInterrupt(
    _In_ PVOID Data)
{
    PTEGRA3_TIMER Timer = Data;

    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PCR, TEGRA3_TMR_PCR_INTR_CLR);
}

static
NTSTATUS
ClockArm(
    _In_ PVOID Data,
    _In_ TIMER_MODE Mode,
    _In_ ULONGLONG TickCount)
{
    PTEGRA3_TIMER Timer = Data;
    ULONG Value;

    if (Mode != TimerModeOneShot)
        return STATUS_NOT_SUPPORTED;

    /* the timer fires after the value plus one ticks */
    if (!TickCount)
        TickCount = 1;
    Value = (TickCount > TEGRA3_TMR_PTV_MAX) ? TEGRA3_TMR_PTV_MAX : (ULONG)(TickCount - 1);

    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PTV, TEGRA3_TMR_PTV_EN | Value);
    return STATUS_SUCCESS;
}

static
VOID
ClockStop(
    _In_ PVOID Data)
{
    PTEGRA3_TIMER Timer = Data;

    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PTV, 0);
    Write32(Timer->Base, Timer->Timer + TEGRA3_TMR_PCR, TEGRA3_TMR_PCR_INTR_CLR);
}

/* timerus, free running 32 bit microseconds */

static
NTSTATUS
CounterInitialize(
    _In_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    return STATUS_SUCCESS;
}

static
ULONGLONG
CounterQuery(
    _In_ PVOID Data)
{
    PTEGRA3_TIMER Timer = Data;

    return Read32(Timer->Base, TEGRA3_TIMERUS_CNTR_1US);
}

/* registration */

static
NTSTATUS
AddResourceGroup(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    TIMER_INITIALIZATION_BLOCK Timer;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;

    if ((Group->VendorId != TEGRA3_CSRT_VENDOR_ID) || (Group->DeviceId != TEGRA3_CSRT_TIMER_DEVICE_ID))
        return STATUS_NOT_SUPPORTED;

    if (!TimerBlock)
    {
        Address.QuadPart = TEGRA3_TIMER_BASE;
        TimerBlock = HalImports->HalMapIoSpace(Address, TEGRA3_TIMER_SIZE, MmNonCached);
        if (!TimerBlock)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    ClockTimer.Base = TimerBlock;
    ClockTimer.Timer = TEGRA3_TMR3;
    CounterTimer.Base = TimerBlock;
    CounterTimer.Timer = TEGRA3_TIMERUS_CNTR_1US;

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_TIMER;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_TIMER;

    /* tmr3 drives the clock, the hal acks it before the tick runs */
    RtlZeroMemory(&Timer, sizeof(Timer));
    Timer.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Timer.Header.TableSize = sizeof(Timer);
    Timer.FunctionTable.Initialize = ClockInitialize;
    Timer.FunctionTable.AcknowledgeInterrupt = ClockAcknowledgeInterrupt;
    Timer.FunctionTable.ArmTimer = ClockArm;
    Timer.FunctionTable.Stop = ClockStop;
    Timer.InternalData = &ClockTimer;
    Timer.InternalDataSize = sizeof(ClockTimer);
    Timer.CounterBitWidth = 29;
    Timer.CounterFrequency = TEGRA3_TIMERUS_FREQUENCY;
    Timer.Capabilities = TIMER_ONE_SHOT_CAPABLE | TIMER_GENERATES_LINE_BASED_INTERRUPTS;
    Timer.Interrupt.LegacyIrq = -1;
    Timer.Interrupt.Gsi = TEGRA3_TMR3_GSI;
    Timer.Interrupt.Polarity = InterruptActiveHigh;
    Timer.Interrupt.Mode = LevelSensitive;
    Timer.KnownType = TimerUnknown;
    Timer.Identifier = 3;

    Descriptor.Uid = 1;
    Status = HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Timer);
    if (!NT_SUCCESS(Status))
        return Status;

    /* timerus is the performance counter, it raises no interrupt */
    RtlZeroMemory(&Timer, sizeof(Timer));
    Timer.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Timer.Header.TableSize = sizeof(Timer);
    Timer.FunctionTable.Initialize = CounterInitialize;
    Timer.FunctionTable.QueryCounter = CounterQuery;
    Timer.InternalData = &CounterTimer;
    Timer.InternalDataSize = sizeof(CounterTimer);
    Timer.CounterBitWidth = 32;
    Timer.CounterFrequency = TEGRA3_TIMERUS_FREQUENCY;
    Timer.Capabilities = TIMER_COUNTER_READABLE;
    Timer.Interrupt.LegacyIrq = -1;
    Timer.Interrupt.Gsi = -1;
    Timer.KnownType = TimerUnknown;
    Timer.Identifier = 0x10;

    Descriptor.Uid = 2;
    return HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Timer);
}

static
VOID
GetSecurityCookie(
    _Out_ PULONG *Cookie,
    _Out_ PULONG *Complement)
{
    *Cookie = &SecurityCookie;
    *Complement = &SecurityCookieComplement;
}

NTSTATUS
HalExtensionInit(
    _In_ OS_EXTENSION_INTERFACE Interface,
    _Out_ POS_EXTENSION_HAL_EXTENSION_EXPORTS *Exports,
    _In_ POS_EXTENSION_HAL_EXTENSION_IMPORTS Imports)
{
    if (Interface != OsExtensionHalExtension)
        return STATUS_NOINTERFACE;

    HalImports = Imports;
    ExtExports.AddResourceGroup = AddResourceGroup;
    ExtExports.GetSecurityCookie = GetSecurityCookie;
    *Exports = &ExtExports;
    return STATUS_SUCCESS;
}
