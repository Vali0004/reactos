/*
 * PROJECT:     ReactOS HAL extension for Qualcomm QSD8x50
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     VIC interrupt controller, GP timer clock and debug timer counter
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntddk.h>
#include <ddk/nthalext.h>
#include "qsd8x50csrt.h"

/* GLOBALS *******************************************************************/

/* vic registers, two banks of 32 lines, still to be checked on hardware */
#define VIC_INT_SELECT(n)       (0x000 + (n) * 4)
#define VIC_INT_ENCLEAR(n)      (0x020 + (n) * 4)
#define VIC_INT_ENSET(n)        (0x030 + (n) * 4)
#define VIC_INT_TYPE(n)         (0x040 + (n) * 4)
#define VIC_INT_POLARITY(n)     (0x050 + (n) * 4)
#define VIC_INT_MASTEREN        0x064
#define VIC_CONFIG              0x068
#define VIC_IRQ_STATUS(n)       (0x080 + (n) * 4)
#define VIC_INT_CLEAR(n)        (0x0B0 + (n) * 4)
#define VIC_SOFTINT(n)          (0x0C0 + (n) * 4)

#define VIC_MASTEREN_IRQ        0x1
#define VIC_LINES               64
#define VIC_BANKS               (VIC_LINES / 32)

/* timer block, the gp timer counts at 32k, the debug timer runs free */
#define GPT_MATCH_VAL           0x000
#define GPT_COUNT_VAL           0x004
#define GPT_ENABLE              0x008
#define GPT_CLEAR               0x00C
#define DGT_MATCH_VAL           0x010
#define DGT_COUNT_VAL           0x014
#define DGT_ENABLE              0x018
#define DGT_CLEAR               0x01C

#define TIMER_ENABLE_EN             0x1
#define TIMER_ENABLE_CLR_ON_MATCH   0x2

/* the vic has no priority scheme of its own, lines are masked by priority here */
#define QSD_MAX_PRIORITY        15

typedef struct _QSD_VIC
{
    PUCHAR Base;
    ULONG Threshold;
    ULONG Enabled[VIC_BANKS];
    ULONG Edge[VIC_BANKS];
    UCHAR Priority[VIC_LINES];
    ULONG SoftwareLine[3];
} QSD_VIC, *PQSD_VIC;

typedef struct _QSD_TIMER
{
    PUCHAR Base;
} QSD_TIMER, *PQSD_TIMER;

static POS_EXTENSION_HAL_EXTENSION_IMPORTS HalImports;
static OS_EXTENSION_HAL_EXTENSION_EXPORTS ExtExports;

static QSD_VIC Vic;
static QSD_TIMER Timer;

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

/* only enabled lines above the threshold reach the cpu */
static
VOID
VicApplyMask(
    _In_ PQSD_VIC Data)
{
    ULONG Bank, Line, Allowed;

    for (Bank = 0; Bank < VIC_BANKS; Bank++)
    {
        Allowed = 0;
        for (Line = 0; Line < 32; Line++)
        {
            if ((Data->Enabled[Bank] & (1UL << Line)) &&
                (Data->Priority[Bank * 32 + Line] > Data->Threshold))
            {
                Allowed |= 1UL << Line;
            }
        }

        Write32(Data->Base, VIC_INT_ENCLEAR(Bank), ~Allowed);
        Write32(Data->Base, VIC_INT_ENSET(Bank), Allowed);
    }
}

static
NTSTATUS
VicInitializeIoUnit(
    _In_ PVOID ControllerData)
{
    PQSD_VIC Data = ControllerData;
    ULONG Bank;

    /* everything off, irq rather than fiq, plain status mode instead of vectors */
    for (Bank = 0; Bank < VIC_BANKS; Bank++)
    {
        Write32(Data->Base, VIC_INT_ENCLEAR(Bank), 0xFFFFFFFF);
        Write32(Data->Base, VIC_INT_SELECT(Bank), 0);
        Write32(Data->Base, VIC_INT_CLEAR(Bank), 0xFFFFFFFF);
        Data->Enabled[Bank] = 0;
        Data->Edge[Bank] = 0;
    }

    Write32(Data->Base, VIC_CONFIG, 0);
    Write32(Data->Base, VIC_INT_MASTEREN, VIC_MASTEREN_IRQ);

    Data->Threshold = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VicInitializeLocalUnit(
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

    /* one scorpion core */
    if (ProcessorNumber != 0)
        return STATUS_NOT_SUPPORTED;

    *LocalId = 0;
    return STATUS_SUCCESS;
}

static
VOID
VicSetPriority(
    _In_ PVOID ControllerData,
    _In_ ULONG NewPriority)
{
    PQSD_VIC Data = ControllerData;

    Data->Threshold = (NewPriority > QSD_MAX_PRIORITY) ? QSD_MAX_PRIORITY : NewPriority;
    VicApplyMask(Data);
}

/* the highest priority line pending among the unmasked ones */
static
INTERRUPT_RESULT
VicAcceptAndGetSource(
    _In_ PVOID ControllerData,
    _Out_ PLONG Line,
    _Out_ PULONG OpaqueData)
{
    PQSD_VIC Data = ControllerData;
    ULONG Bank, Bit, Pending, Best = VIC_LINES;
    UCHAR BestPriority = 0;

    for (Bank = 0; Bank < VIC_BANKS; Bank++)
    {
        Pending = Read32(Data->Base, VIC_IRQ_STATUS(Bank));
        for (Bit = 0; Pending; Bit++, Pending >>= 1)
        {
            ULONG Number = Bank * 32 + Bit;

            if ((Pending & 1) && (Data->Priority[Number] >= BestPriority))
            {
                Best = Number;
                BestPriority = Data->Priority[Number];
            }
        }
    }

    if (Best == VIC_LINES)
        return InterruptBeginSpurious;

    /* edges and software requests are latched, clear them before the isr runs */
    if (Data->Edge[Best / 32] & (1UL << (Best % 32)))
        Write32(Data->Base, VIC_INT_CLEAR(Best / 32), 1UL << (Best % 32));

    *Line = (LONG)Best;
    *OpaqueData = Data->Threshold;

    /* the line keeps its own level blocked until the end of interrupt */
    Data->Threshold = BestPriority;
    VicApplyMask(Data);
    return InterruptBeginLine;
}

static
VOID
VicEndOfInterrupt(
    _In_ PVOID ControllerData,
    _In_ ULONG OpaqueToken)
{
    PQSD_VIC Data = ControllerData;

    Data->Threshold = OpaqueToken;
    VicApplyMask(Data);
}

static
NTSTATUS
VicSetLineState(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_LINE_STATE NewState)
{
    PQSD_VIC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;
    ULONG Bank = Number / 32, Bit = 1UL << (Number % 32);
    ULONG Value;

    if (Number >= VIC_LINES)
        return STATUS_INVALID_PARAMETER;

    if (NewState->Flags & INTERRUPT_LINE_ENABLED)
    {
        Data->Priority[Number] = (UCHAR)((NewState->Priority > QSD_MAX_PRIORITY) ? QSD_MAX_PRIORITY : NewState->Priority);

        Value = Read32(Data->Base, VIC_INT_TYPE(Bank));
        if (NewState->TriggerMode == Latched)
        {
            Value |= Bit;
            Data->Edge[Bank] |= Bit;
        }
        else
        {
            Value &= ~Bit;
            Data->Edge[Bank] &= ~Bit;
        }
        Write32(Data->Base, VIC_INT_TYPE(Bank), Value);

        Value = Read32(Data->Base, VIC_INT_POLARITY(Bank));
        if (NewState->Polarity == InterruptActiveLow)
            Value |= Bit;
        else
            Value &= ~Bit;
        Write32(Data->Base, VIC_INT_POLARITY(Bank), Value);

        Data->Enabled[Bank] |= Bit;
    }
    else
    {
        Data->Enabled[Bank] &= ~Bit;
    }

    VicApplyMask(Data);
    return STATUS_SUCCESS;
}

static
NTSTATUS
VicRequestInterrupt(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_TARGET Target,
    _In_ ULONG Vector,
    _In_ PINTERRUPT_LINE OutputLine)
{
    PQSD_VIC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(OutputLine);

    if (Number >= VIC_LINES)
        return STATUS_INVALID_PARAMETER;

    Write32(Data->Base, VIC_SOFTINT(Number / 32), 1UL << (Number % 32));
    return STATUS_SUCCESS;
}

/* gp timer clock */

static
NTSTATUS
GptInitialize(
    _In_ PVOID TimerData)
{
    PQSD_TIMER Data = TimerData;

    Write32(Data->Base, GPT_ENABLE, 0);
    Write32(Data->Base, GPT_CLEAR, 0);
    return STATUS_SUCCESS;
}

static
VOID
GptAcknowledgeInterrupt(
    _In_ PVOID TimerData)
{
    UNREFERENCED_PARAMETER(TimerData);
}

/* the count clears on a match, so a match value is a period */
static
NTSTATUS
GptArm(
    _In_ PVOID TimerData,
    _In_ TIMER_MODE Mode,
    _In_ ULONGLONG TickCount)
{
    PQSD_TIMER Data = TimerData;

    if (!TickCount || (TickCount > 0xFFFFFFFFULL))
        return STATUS_INVALID_PARAMETER;

    Write32(Data->Base, GPT_ENABLE, 0);
    Write32(Data->Base, GPT_CLEAR, 0);
    Write32(Data->Base, GPT_MATCH_VAL, (ULONG)TickCount);
    Write32(Data->Base, GPT_ENABLE,
            TIMER_ENABLE_EN | ((Mode == TimerModeOneShot) ? 0 : TIMER_ENABLE_CLR_ON_MATCH));
    return STATUS_SUCCESS;
}

static
VOID
GptStop(
    _In_ PVOID TimerData)
{
    PQSD_TIMER Data = TimerData;

    Write32(Data->Base, GPT_ENABLE, 0);
}

/* debug timer counter */

static
NTSTATUS
DgtInitialize(
    _In_ PVOID TimerData)
{
    PQSD_TIMER Data = TimerData;

    /* free running, no match interrupt */
    Write32(Data->Base, DGT_MATCH_VAL, 0xFFFFFFFF);
    Write32(Data->Base, DGT_ENABLE, TIMER_ENABLE_EN);
    return STATUS_SUCCESS;
}

static
ULONGLONG
DgtQueryCounter(
    _In_ PVOID TimerData)
{
    PQSD_TIMER Data = TimerData;

    return Read32(Data->Base, DGT_COUNT_VAL);
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
    _In_ PQSD8X50_CSRT_SHARED_INFO Info)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    INTERRUPT_INITIALIZATION_BLOCK Block;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;
    ULONG i;

    Address.QuadPart = Info->VicBase;
    Vic.Base = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    if (!Vic.Base)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < RTL_NUMBER_OF(Vic.SoftwareLine); i++)
    {
        if (Info->SoftwareLine[i] >= VIC_LINES)
            return STATUS_INVALID_PARAMETER;
        Vic.SoftwareLine[i] = Info->SoftwareLine[i];
    }

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = INTERRUPT_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.InitializeLocalUnit = VicInitializeLocalUnit;
    Block.FunctionTable.InitializeIoUnit = VicInitializeIoUnit;
    Block.FunctionTable.SetPriority = VicSetPriority;
    Block.FunctionTable.AcceptAndGetSource = VicAcceptAndGetSource;
    Block.FunctionTable.EndOfInterrupt = VicEndOfInterrupt;
    Block.FunctionTable.SetLineState = VicSetLineState;
    Block.FunctionTable.RequestInterrupt = VicRequestInterrupt;
    Block.InternalData = &Vic;
    Block.InternalDataSize = sizeof(Vic);
    Block.KnownType = InterruptControllerUnknown;
    Block.Identifier = 0;
    Block.Capabilities = INTERRUPT_CONTROLLER_HAS_PRIORITY_SCHEME;
    Block.MaxPriority = QSD_MAX_PRIORITY;
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
    for (i = 0; i < RTL_NUMBER_OF(Vic.SoftwareLine); i++)
    {
        Status = RegisterLines(Handle, Group, InterruptLineSoftwareOnly,
                               (LONG)Vic.SoftwareLine[i], (LONG)Vic.SoftwareLine[i] + 1,
                               0xFFFFFFFF, 2 + i);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return RegisterLines(Handle, Group, InterruptLineStandardPin, 0, VIC_LINES, Info->GsiBase, 5);
}

static
NTSTATUS
RegisterTimers(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group,
    _In_ PQSD8X50_CSRT_SHARED_INFO Info)
{
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    TIMER_INITIALIZATION_BLOCK Block;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;

    Address.QuadPart = Info->TimerBase;
    Timer.Base = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    if (!Timer.Base)
        return STATUS_INSUFFICIENT_RESOURCES;

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_TIMER;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_TIMER;

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.Initialize = GptInitialize;
    Block.FunctionTable.AcknowledgeInterrupt = GptAcknowledgeInterrupt;
    Block.FunctionTable.ArmTimer = GptArm;
    Block.FunctionTable.Stop = GptStop;
    Block.InternalData = &Timer;
    Block.InternalDataSize = sizeof(Timer);
    Block.CounterBitWidth = 32;
    Block.CounterFrequency = Info->GptFrequency;
    /* arming resets the count, so the debug timer keeps time instead */
    Block.Capabilities = TIMER_PERIODIC_CAPABLE | TIMER_ONE_SHOT_CAPABLE |
                         TIMER_GENERATES_LINE_BASED_INTERRUPTS;
    Block.Interrupt.LegacyIrq = -1;
    Block.Interrupt.Gsi = (LONG)(Info->GsiBase + Info->GptLine);
    Block.Interrupt.Polarity = InterruptActiveHigh;
    Block.Interrupt.Mode = Latched;
    Block.KnownType = TimerGp;
    Block.Identifier = 1;

    Descriptor.Uid = 6;
    Status = HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Block);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Block.Header.TableSize = sizeof(Block);
    Block.FunctionTable.Initialize = DgtInitialize;
    Block.FunctionTable.QueryCounter = DgtQueryCounter;
    Block.InternalData = &Timer;
    Block.InternalDataSize = sizeof(Timer);
    Block.CounterBitWidth = 32;
    Block.CounterFrequency = Info->DgtFrequency;
    Block.Capabilities = TIMER_COUNTER_READABLE;
    Block.Interrupt.LegacyIrq = -1;
    Block.Interrupt.Gsi = -1;
    Block.KnownType = TimerUnknown;
    Block.Identifier = 2;

    Descriptor.Uid = 7;
    return HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Block);
}

/* a group either carries the shared info or leaves the board values to the defaults */
static
NTSTATUS
GetSharedInfo(
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    _Out_ PQSD8X50_CSRT_SHARED_INFO Info)
{
    PQSD8X50_CSRT_SHARED_INFO Shared;

    if (ResourceGroup->SharedInfoLength >= sizeof(QSD8X50_CSRT_SHARED_INFO))
    {
        Shared = CSRT_GROUP_SHARED_INFO(ResourceGroup);
        if (Shared->Version != QSD8X50_CSRT_SHARED_INFO_VERSION)
            return STATUS_REVISION_MISMATCH;
        *Info = *Shared;
    }
    else
    {
        Info->Version = QSD8X50_CSRT_SHARED_INFO_VERSION;
        Info->VicBase = QSD8X50_DEFAULT_VIC_BASE;
        Info->TimerBase = QSD8X50_DEFAULT_TIMER_BASE;
        Info->GptLine = QSD8X50_DEFAULT_GPT_LINE;
        Info->GptFrequency = QSD8X50_DEFAULT_GPT_FREQUENCY;
        Info->DgtFrequency = QSD8X50_DEFAULT_DGT_FREQUENCY;
        Info->GsiBase = QSD8X50_DEFAULT_GSI_BASE;
        Info->SoftwareLine[0] = QSD8X50_DEFAULT_SOFTWARE_LINE0;
        Info->SoftwareLine[1] = QSD8X50_DEFAULT_SOFTWARE_LINE1;
        Info->SoftwareLine[2] = QSD8X50_DEFAULT_SOFTWARE_LINE2;
    }

    if (!Info->GptFrequency || !Info->DgtFrequency || (Info->GptLine >= VIC_LINES))
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

/* the vic group brings the timers along when it carries the shared info, otherwise they have their own group */
static
NTSTATUS
AddResourceGroup(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup)
{
    QSD8X50_CSRT_SHARED_INFO Info;
    NTSTATUS Status;

    if (ResourceGroup->VendorId != QSD8X50_CSRT_VENDOR_ID)
        return STATUS_NOT_SUPPORTED;

    Status = GetSharedInfo(ResourceGroup, &Info);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ResourceGroup->DeviceId == QSD8X50_CSRT_DEVICE_ID)
    {
        Status = RegisterInterruptController(Handle, ResourceGroup, &Info);
        if (!NT_SUCCESS(Status) || (ResourceGroup->SharedInfoLength < sizeof(Info)))
            return Status;

        return RegisterTimers(Handle, ResourceGroup, &Info);
    }

    if (ResourceGroup->DeviceId == QSD8X50_CSRT_TIMER_DEVICE_ID)
        return RegisterTimers(Handle, ResourceGroup, &Info);

    return STATUS_NOT_SUPPORTED;
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
