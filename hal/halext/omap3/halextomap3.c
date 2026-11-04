/*
 * PROJECT:     ReactOS HAL extension for TI OMAP3
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     INTC interrupt controller, GP timer clock and 32k sync counter
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntddk.h>
#include <ddk/nthalext.h>
#include "omap3csrt.h"

/* GLOBALS *******************************************************************/

/* intc registers, from the omap3 trm */
#define INTC_SYSCONFIG          0x010
#define INTC_SYSSTATUS          0x014
#define INTC_SIR_IRQ            0x040
#define INTC_CONTROL            0x048
#define INTC_THRESHOLD          0x068
#define INTC_MIR_CLEAR(n)       (0x088 + ((n) * 0x20))
#define INTC_MIR_SET(n)         (0x08C + ((n) * 0x20))
#define INTC_ISR_SET(n)         (0x090 + ((n) * 0x20))
#define INTC_ISR_CLEAR(n)       (0x094 + ((n) * 0x20))
#define INTC_ILR(m)             (0x100 + ((m) * 4))

#define INTC_SYSCONFIG_SOFTRESET    0x2
#define INTC_SYSSTATUS_RESETDONE    0x1
#define INTC_CONTROL_NEWIRQAGR      0x1
#define INTC_SIR_ACTIVEIRQ          0x7F
#define INTC_SIR_SPURIOUS           0xFFFFFF80
#define INTC_THRESHOLD_DISABLED     0xFF
#define INTC_LINES                  96
#define INTC_BANKS                  (INTC_LINES / 32)

/* gp timer registers */
#define GPT_TIOCP_CFG           0x010
#define GPT_TISTAT              0x014
#define GPT_TISR                0x018
#define GPT_TIER                0x01C
#define GPT_TCLR                0x024
#define GPT_TCRR                0x028
#define GPT_TLDR                0x02C
#define GPT_TTGR                0x030
#define GPT_TSICR               0x040

#define GPT_TIOCP_SOFTRESET     0x2
#define GPT_TISTAT_RESETDONE    0x1
#define GPT_OVF_IT              0x2
#define GPT_TCLR_ST             0x1
#define GPT_TCLR_AR             0x2
#define GPT_TSICR_POSTED        0x4

/* windows priorities run 0 to 15, intc priorities run 63 down to 0 */
#define OMAP3_MAX_PRIORITY      15
#define OMAP3_INTC_PRIORITY(p)  ((OMAP3_MAX_PRIORITY - (p)) * 4)

typedef struct _OMAP3_INTC
{
    PUCHAR Base;
    ULONG Threshold;
    ULONG SoftwareLine[3];
} OMAP3_INTC, *POMAP3_INTC;

typedef struct _OMAP3_TIMER
{
    PUCHAR Base;
} OMAP3_TIMER, *POMAP3_TIMER;

typedef struct _OMAP3_COUNTER
{
    volatile ULONG *Register;
} OMAP3_COUNTER, *POMAP3_COUNTER;

static POS_EXTENSION_HAL_EXTENSION_IMPORTS HalImports;
static OS_EXTENSION_HAL_EXTENSION_EXPORTS ExtExports;

static OMAP3_INTC Intc;
static OMAP3_TIMER Timer;
static OMAP3_COUNTER Counter;

/* the hal reads this through GetSecurityCookie, it has to exist */
static ULONG SecurityCookie = 0xBB40E64E;
static ULONG SecurityCookieComplement = ~0xBB40E64EUL;

/* FUNCTIONS *****************************************************************/

FORCEINLINE
ULONG
Read32(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return *(volatile ULONG *)(Base + Offset);
}

FORCEINLINE
VOID
Write32(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *(volatile ULONG *)(Base + Offset) = Value;
}

/* interrupt controller */

static
NTSTATUS
IntcInitializeIoUnit(
    _In_ PVOID ControllerData)
{
    POMAP3_INTC Data = ControllerData;
    ULONG i;

    Write32(Data->Base, INTC_SYSCONFIG, INTC_SYSCONFIG_SOFTRESET);
    while (!(Read32(Data->Base, INTC_SYSSTATUS) & INTC_SYSSTATUS_RESETDONE))
        YieldProcessor();

    /* everything masked and at the lowest priority until the hal enables it */
    for (i = 0; i < INTC_BANKS; i++)
    {
        Write32(Data->Base, INTC_MIR_SET(i), 0xFFFFFFFF);
        Write32(Data->Base, INTC_ISR_CLEAR(i), 0xFFFFFFFF);
    }

    for (i = 0; i < INTC_LINES; i++)
        Write32(Data->Base, INTC_ILR(i), OMAP3_INTC_PRIORITY(0) << 2);

    Data->Threshold = INTC_THRESHOLD_DISABLED;
    Write32(Data->Base, INTC_THRESHOLD, Data->Threshold);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcInitializeLocalUnit(
    _In_ PVOID ControllerData,
    _In_ ULONG ProcessorNumber,
    _In_ ULONG SpuriousVector,
    _In_ ULONG StubVector,
    _In_ ULONG LocalErrorVector,
    _Out_ PULONG LocalId)
{
    UNREFERENCED_PARAMETER(ControllerData);
    UNREFERENCED_PARAMETER(SpuriousVector);
    UNREFERENCED_PARAMETER(StubVector);
    UNREFERENCED_PARAMETER(LocalErrorVector);

    /* one cortex-a8, the intc has no per cpu part */
    if (ProcessorNumber != 0)
        return STATUS_NOT_SUPPORTED;

    *LocalId = 0;
    return STATUS_SUCCESS;
}

/* lines at or below the priority stay masked */
static
VOID
IntcSetPriority(
    _In_ PVOID ControllerData,
    _In_ ULONG NewPriority)
{
    POMAP3_INTC Data = ControllerData;

    if (NewPriority > OMAP3_MAX_PRIORITY)
        NewPriority = OMAP3_MAX_PRIORITY;

    Data->Threshold = OMAP3_INTC_PRIORITY(NewPriority);
    Write32(Data->Base, INTC_THRESHOLD, Data->Threshold);
}

static
INTERRUPT_RESULT
IntcAcceptAndGetSource(
    _In_ PVOID ControllerData,
    _Out_ PLONG Line,
    _Out_ PULONG OpaqueData)
{
    POMAP3_INTC Data = ControllerData;
    ULONG Sir, Active, i;

    Sir = Read32(Data->Base, INTC_SIR_IRQ);
    if (Sir & INTC_SIR_SPURIOUS)
    {
        Write32(Data->Base, INTC_CONTROL, INTC_CONTROL_NEWIRQAGR);
        return InterruptBeginSpurious;
    }

    Active = Sir & INTC_SIR_ACTIVEIRQ;
    *Line = (LONG)Active;

    /* the line keeps its own level blocked until the end of interrupt */
    *OpaqueData = Data->Threshold;
    Data->Threshold = (Read32(Data->Base, INTC_ILR(Active)) >> 2) & 0x3F;
    Write32(Data->Base, INTC_THRESHOLD, Data->Threshold);

    /* software requests are edges the controller only latches in isr_set */
    for (i = 0; i < RTL_NUMBER_OF(Data->SoftwareLine); i++)
    {
        if (Data->SoftwareLine[i] == Active)
            Write32(Data->Base, INTC_ISR_CLEAR(Active / 32), 1UL << (Active % 32));
    }

    /* lets a higher priority line through while this one is serviced */
    Write32(Data->Base, INTC_CONTROL, INTC_CONTROL_NEWIRQAGR);
    return InterruptBeginLine;
}

static
VOID
IntcEndOfInterrupt(
    _In_ PVOID ControllerData,
    _In_ ULONG OpaqueToken)
{
    POMAP3_INTC Data = ControllerData;

    Data->Threshold = OpaqueToken;
    Write32(Data->Base, INTC_THRESHOLD, Data->Threshold);
}

static
NTSTATUS
IntcSetLineState(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_LINE_STATE NewState)
{
    POMAP3_INTC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;
    ULONG Priority;

    if (Number >= INTC_LINES)
        return STATUS_INVALID_PARAMETER;

    if (NewState->Flags & INTERRUPT_LINE_ENABLED)
    {
        Priority = (NewState->Priority > OMAP3_MAX_PRIORITY) ? OMAP3_MAX_PRIORITY : NewState->Priority;
        Write32(Data->Base, INTC_ILR(Number), OMAP3_INTC_PRIORITY(Priority) << 2);
        Write32(Data->Base, INTC_MIR_CLEAR(Number / 32), 1UL << (Number % 32));
    }
    else
    {
        Write32(Data->Base, INTC_MIR_SET(Number / 32), 1UL << (Number % 32));
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcRequestInterrupt(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_TARGET Target,
    _In_ ULONG Vector,
    _In_ PINTERRUPT_LINE OutputLine)
{
    POMAP3_INTC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(OutputLine);

    if (Number >= INTC_LINES)
        return STATUS_INVALID_PARAMETER;

    Write32(Data->Base, INTC_ISR_SET(Number / 32), 1UL << (Number % 32));
    return STATUS_SUCCESS;
}

/* gp timer clock */

static
NTSTATUS
TimerInitialize(
    _In_ PVOID TimerData)
{
    POMAP3_TIMER Data = TimerData;

    Write32(Data->Base, GPT_TIOCP_CFG, GPT_TIOCP_SOFTRESET);
    while (!(Read32(Data->Base, GPT_TISTAT) & GPT_TISTAT_RESETDONE))
        YieldProcessor();

    /* non posted writes land before the next access, no busy bits to poll */
    Write32(Data->Base, GPT_TSICR, Read32(Data->Base, GPT_TSICR) & ~GPT_TSICR_POSTED);
    Write32(Data->Base, GPT_TCLR, 0);
    Write32(Data->Base, GPT_TISR, GPT_OVF_IT);
    return STATUS_SUCCESS;
}

static
ULONGLONG
TimerQueryCounter(
    _In_ PVOID TimerData)
{
    POMAP3_TIMER Data = TimerData;

    return Read32(Data->Base, GPT_TCRR);
}

static
VOID
TimerAcknowledgeInterrupt(
    _In_ PVOID TimerData)
{
    POMAP3_TIMER Data = TimerData;

    Write32(Data->Base, GPT_TISR, GPT_OVF_IT);
}

/* the timer counts up and interrupts on overflow, the load value sets the period */
static
NTSTATUS
TimerArm(
    _In_ PVOID TimerData,
    _In_ TIMER_MODE Mode,
    _In_ ULONGLONG TickCount)
{
    POMAP3_TIMER Data = TimerData;
    ULONG Load;

    if (!TickCount || (TickCount > 0xFFFFFFFFULL))
        return STATUS_INVALID_PARAMETER;

    Load = (ULONG)(0x100000000ULL - TickCount);

    Write32(Data->Base, GPT_TCLR, 0);
    Write32(Data->Base, GPT_TLDR, Load);
    Write32(Data->Base, GPT_TCRR, Load);
    Write32(Data->Base, GPT_TISR, GPT_OVF_IT);
    Write32(Data->Base, GPT_TIER, GPT_OVF_IT);
    Write32(Data->Base, GPT_TCLR, GPT_TCLR_ST | ((Mode == TimerModeOneShot) ? 0 : GPT_TCLR_AR));
    return STATUS_SUCCESS;
}

static
VOID
TimerStop(
    _In_ PVOID TimerData)
{
    POMAP3_TIMER Data = TimerData;

    Write32(Data->Base, GPT_TCLR, 0);
    Write32(Data->Base, GPT_TIER, 0);
    Write32(Data->Base, GPT_TISR, GPT_OVF_IT);
}

/* 32k sync counter */

static
NTSTATUS
CounterInitialize(
    _In_ PVOID TimerData)
{
    UNREFERENCED_PARAMETER(TimerData);
    return STATUS_SUCCESS;
}

static
ULONGLONG
CounterQuery(
    _In_ PVOID TimerData)
{
    POMAP3_COUNTER Data = TimerData;

    return *Data->Register;
}

/* registration */

static
NTSTATUS
RegisterLines(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group,
    _In_ INTERRUPT_LINE_TYPE Type,
    _In_ LONG MinLine,
    _In_ LONG MaxLine,
    _In_ ULONG GsiBase,
    _In_ ULONG Uid)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    INTERRUPT_LINE_DESCRIPTION Lines;

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_INTERRUPT;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_INTERRUPT_LINES;
    Descriptor.Uid = Uid;

    Lines.ControllerIdentifier = 0;
    Lines.MinLine = MinLine;
    Lines.MaxLine = MaxLine;
    Lines.Type = Type;
    Lines.Capabilities = 0;
    Lines.OutputUnitId = 0;
    Lines.GsiBase = GsiBase;

    return HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Lines);
}

static
NTSTATUS
RegisterInterruptController(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group,
    _In_ POMAP3_CSRT_SHARED_INFO Info)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    INTERRUPT_INITIALIZATION_BLOCK Block;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;
    ULONG i;

    Address.QuadPart = Info->IntcBase;
    Intc.Base = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    if (!Intc.Base)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < RTL_NUMBER_OF(Intc.SoftwareLine); i++)
    {
        if (Info->SoftwareLine[i] >= INTC_LINES)
            return STATUS_INVALID_PARAMETER;
        Intc.SoftwareLine[i] = Info->SoftwareLine[i];
    }

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = INTERRUPT_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.InitializeLocalUnit = IntcInitializeLocalUnit;
    Block.FunctionTable.InitializeIoUnit = IntcInitializeIoUnit;
    Block.FunctionTable.SetPriority = IntcSetPriority;
    Block.FunctionTable.AcceptAndGetSource = IntcAcceptAndGetSource;
    Block.FunctionTable.EndOfInterrupt = IntcEndOfInterrupt;
    Block.FunctionTable.SetLineState = IntcSetLineState;
    Block.FunctionTable.RequestInterrupt = IntcRequestInterrupt;
    Block.InternalData = &Intc;
    Block.InternalDataSize = sizeof(Intc);
    Block.KnownType = InterruptControllerUnknown;
    Block.Identifier = 0;
    Block.Capabilities = INTERRUPT_CONTROLLER_HAS_PRIORITY_SCHEME;
    Block.MaxPriority = OMAP3_MAX_PRIORITY;
    Block.MaxClusterSize = 1;
    Block.MaxClusters = 1;

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_INTERRUPT;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_INTERRUPT_CONTROLLER;
    Descriptor.Uid = 1;

    Status = HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Block);
    if (!NT_SUCCESS(Status))
        return Status;

    /* the software lines go first so the hal finds them before the pin range */
    for (i = 0; i < RTL_NUMBER_OF(Intc.SoftwareLine); i++)
    {
        Status = RegisterLines(Handle, Group, InterruptLineSoftwareOnly,
                               (LONG)Intc.SoftwareLine[i], (LONG)Intc.SoftwareLine[i] + 1,
                               0xFFFFFFFF, 2 + i);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return RegisterLines(Handle, Group, InterruptLineStandardPin, 0, INTC_LINES, Info->GsiBase, 5);
}

static
NTSTATUS
RegisterTimers(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group,
    _In_ POMAP3_CSRT_SHARED_INFO Info)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    TIMER_INITIALIZATION_BLOCK Block;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;
    PUCHAR CounterPage;

    Address.QuadPart = Info->TimerBase;
    Timer.Base = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    Address.QuadPart = Info->CounterBase & ~0xFFFULL;
    CounterPage = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    if (!Timer.Base || !CounterPage)
        return STATUS_INSUFFICIENT_RESOURCES;
    Counter.Register = (volatile ULONG *)(CounterPage + (Info->CounterBase & 0xFFF));

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_TIMER;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_TIMER;

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.Initialize = TimerInitialize;
    Block.FunctionTable.QueryCounter = TimerQueryCounter;
    Block.FunctionTable.AcknowledgeInterrupt = TimerAcknowledgeInterrupt;
    Block.FunctionTable.ArmTimer = TimerArm;
    Block.FunctionTable.Stop = TimerStop;
    Block.InternalData = &Timer;
    Block.InternalDataSize = sizeof(Timer);
    Block.CounterBitWidth = 32;
    Block.CounterFrequency = Info->TimerFrequency;
    /* arming reloads the count, so the sync counter keeps time instead */
    Block.Capabilities = TIMER_PERIODIC_CAPABLE | TIMER_ONE_SHOT_CAPABLE |
                         TIMER_GENERATES_LINE_BASED_INTERRUPTS;
    Block.Interrupt.LegacyIrq = -1;
    Block.Interrupt.Gsi = (LONG)Info->TimerGsi;
    Block.Interrupt.Polarity = InterruptActiveHigh;
    Block.Interrupt.Mode = LevelSensitive;
    Block.KnownType = TimerGp;
    Block.Identifier = 1;

    Descriptor.Uid = 6;
    Status = HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Block);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.Initialize = CounterInitialize;
    Block.FunctionTable.QueryCounter = CounterQuery;
    Block.InternalData = &Counter;
    Block.InternalDataSize = sizeof(Counter);
    Block.CounterBitWidth = 32;
    Block.CounterFrequency = 32768;
    Block.Capabilities = TIMER_COUNTER_READABLE;
    Block.Interrupt.LegacyIrq = -1;
    Block.Interrupt.Gsi = -1;
    Block.KnownType = TimerUnknown;
    Block.Identifier = 2;

    Descriptor.Uid = 7;
    return HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Block);
}

static
NTSTATUS
AddResourceGroup(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup)
{
    POMAP3_CSRT_SHARED_INFO Info;
    NTSTATUS Status;

    if ((ResourceGroup->VendorId != OMAP3_CSRT_VENDOR_ID) ||
        (ResourceGroup->DeviceId != OMAP3_CSRT_DEVICE_ID))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (ResourceGroup->SharedInfoLength < sizeof(OMAP3_CSRT_SHARED_INFO))
        return STATUS_INVALID_PARAMETER;

    Info = CSRT_GROUP_SHARED_INFO(ResourceGroup);
    if (Info->Version != OMAP3_CSRT_SHARED_INFO_VERSION)
        return STATUS_REVISION_MISMATCH;

    Status = RegisterInterruptController(Handle, ResourceGroup, Info);
    if (!NT_SUCCESS(Status))
        return Status;

    return RegisterTimers(Handle, ResourceGroup, Info);
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
