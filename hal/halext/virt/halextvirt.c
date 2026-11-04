/*
 * PROJECT:     ReactOS HAL extension for the QEMU virt machine
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GICv2 and the virtual generic timer through the extension interface
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <ntddk.h>
#include <ddk/nthalext.h>

/* GLOBALS *******************************************************************/

/* exercises the hal extension path on hardware qemu has, VEN_QEMU&DEV_0001 */
#define VIRT_CSRT_VENDOR_ID     'UMEQ'
#define VIRT_CSRT_DEVICE_ID     0x0001

#include <pshpack1.h>
typedef struct _VIRT_CSRT_SHARED_INFO
{
    ULONG Version;
    ULONG DistributorBase;
    ULONG CpuInterfaceBase;
    ULONG TimerGsi;
} VIRT_CSRT_SHARED_INFO, *PVIRT_CSRT_SHARED_INFO;
#include <poppack.h>

#define GICD_CTLR               0x000
#define GICD_TYPER              0x004
#define GICD_ISENABLER(n)       (0x100 + ((n) * 4))
#define GICD_ICENABLER(n)       (0x180 + ((n) * 4))
#define GICD_IPRIORITYR         0x400
#define GICD_ITARGETSR          0x800
#define GICD_ICFGR(n)           (0xC00 + ((n) * 4))
#define GICD_SGIR               0xF00

#define GICC_CTLR               0x000
#define GICC_PMR                0x004
#define GICC_IAR                0x00C
#define GICC_EOIR               0x010

#define GIC_SPURIOUS            1020
#define GIC_SGIR_SELF           (2UL << 24)

#define VIRT_MAX_PRIORITY       15
#define VIRT_GIC_PRIORITY(p)    ((UCHAR)((VIRT_MAX_PRIORITY - (p)) << 4))

#define CNTV_CTL_ENABLE         0x1
#define CNTV_CTL_IMASK          0x2

typedef struct _VIRT_GIC
{
    PUCHAR Distributor;
    PUCHAR CpuInterface;
    ULONG LineCount;
} VIRT_GIC, *PVIRT_GIC;

static POS_EXTENSION_HAL_EXTENSION_IMPORTS HalImports;
static OS_EXTENSION_HAL_EXTENSION_EXPORTS ExtExports;
static VIRT_GIC Gic;
static ULONG TimerData;

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

FORCEINLINE VOID Write8(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ UCHAR Value)
{
    *(volatile UCHAR *)(Base + Offset) = Value;
}

/* gic */

static
NTSTATUS
GicInitializeIoUnit(
    _In_ PVOID ControllerData)
{
    PVIRT_GIC Data = ControllerData;
    ULONG i;

    Write32(Data->Distributor, GICD_CTLR, 0);
    Data->LineCount = ((Read32(Data->Distributor, GICD_TYPER) & 0x1F) + 1) * 32;

    for (i = 32; i < Data->LineCount; i += 32)
        Write32(Data->Distributor, GICD_ICENABLER(i / 32), 0xFFFFFFFF);

    for (i = 32; i < Data->LineCount; i++)
    {
        Write8(Data->Distributor, GICD_IPRIORITYR + i, VIRT_GIC_PRIORITY(0));
        Write8(Data->Distributor, GICD_ITARGETSR + i, 1);
    }

    Write32(Data->Distributor, GICD_CTLR, 1);
    return STATUS_SUCCESS;
}

static
NTSTATUS
GicInitializeLocalUnit(
    _In_ PVOID ControllerData,
    _In_ ULONG ProcessorNumber,
    _In_ ULONG SpuriousVector,
    _In_ ULONG StubVector,
    _In_ ULONG LocalErrorVector,
    _Out_ PULONG LocalId)
{
    PVIRT_GIC Data = ControllerData;
    ULONG i;

    UNREFERENCED_PARAMETER(SpuriousVector);
    UNREFERENCED_PARAMETER(StubVector);
    UNREFERENCED_PARAMETER(LocalErrorVector);

    /* the banked sgis and ppis start masked at the lowest priority */
    Write32(Data->Distributor, GICD_ICENABLER(0), 0xFFFF0000);
    for (i = 0; i < 32; i++)
        Write8(Data->Distributor, GICD_IPRIORITYR + i, VIRT_GIC_PRIORITY(0));
    Write32(Data->Distributor, GICD_ISENABLER(0), 0x0000FFFF);

    Write32(Data->CpuInterface, GICC_PMR, 0);
    Write32(Data->CpuInterface, GICC_CTLR, 1);

    *LocalId = ProcessorNumber;
    return STATUS_SUCCESS;
}

static
VOID
GicSetPriority(
    _In_ PVOID ControllerData,
    _In_ ULONG NewPriority)
{
    PVIRT_GIC Data = ControllerData;

    if (NewPriority > VIRT_MAX_PRIORITY)
        NewPriority = VIRT_MAX_PRIORITY;

    Write32(Data->CpuInterface, GICC_PMR, VIRT_GIC_PRIORITY(NewPriority));
}

static
INTERRUPT_RESULT
GicAcceptAndGetSource(
    _In_ PVOID ControllerData,
    _Out_ PLONG Line,
    _Out_ PULONG OpaqueData)
{
    PVIRT_GIC Data = ControllerData;
    ULONG Iar = Read32(Data->CpuInterface, GICC_IAR);

    if ((Iar & 0x3FF) >= GIC_SPURIOUS)
        return InterruptBeginSpurious;

    *Line = (LONG)(Iar & 0x3FF);
    *OpaqueData = Iar;
    return InterruptBeginLine;
}

static
VOID
GicEndOfInterrupt(
    _In_ PVOID ControllerData,
    _In_ ULONG OpaqueToken)
{
    PVIRT_GIC Data = ControllerData;

    Write32(Data->CpuInterface, GICC_EOIR, OpaqueToken);
}

static
NTSTATUS
GicSetLineState(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_LINE_STATE NewState)
{
    PVIRT_GIC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;
    ULONG Priority, Config;

    if (Number >= 1020)
        return STATUS_INVALID_PARAMETER;

    if (!(NewState->Flags & INTERRUPT_LINE_ENABLED))
    {
        if (Number >= 16)
            Write32(Data->Distributor, GICD_ICENABLER(Number / 32), 1UL << (Number % 32));
        return STATUS_SUCCESS;
    }

    Priority = (NewState->Priority > VIRT_MAX_PRIORITY) ? VIRT_MAX_PRIORITY : NewState->Priority;
    Write8(Data->Distributor, GICD_IPRIORITYR + Number, VIRT_GIC_PRIORITY(Priority));

    if (Number >= 32)
    {
        Config = Read32(Data->Distributor, GICD_ICFGR(Number / 16));
        Config &= ~(2UL << ((Number % 16) * 2));
        if (NewState->TriggerMode == Latched)
            Config |= 2UL << ((Number % 16) * 2);
        Write32(Data->Distributor, GICD_ICFGR(Number / 16), Config);
    }

    Write32(Data->Distributor, GICD_ISENABLER(Number / 32), 1UL << (Number % 32));
    return STATUS_SUCCESS;
}

static
NTSTATUS
GicRequestInterrupt(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_TARGET Target,
    _In_ ULONG Vector,
    _In_ PINTERRUPT_LINE OutputLine)
{
    PVIRT_GIC Data = ControllerData;
    ULONG Number = (ULONG)Line->Line;

    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(OutputLine);

    if (Number >= 16)
        return STATUS_INVALID_PARAMETER;

    if (Target->Target == InterruptTargetLogicalFlat)
        Write32(Data->Distributor, GICD_SGIR, ((Target->LogicalFlatTarget & 0xFF) << 16) | Number);
    else
        Write32(Data->Distributor, GICD_SGIR, GIC_SGIR_SELF | Number);

    return STATUS_SUCCESS;
}

/* virtual generic timer */

FORCEINLINE ULONGLONG ReadCntvct(VOID)
{
    ULONG Low, High;

    __asm__ __volatile__("isb\n\tmrrc p15, 1, %0, %1, c14" : "=r" (Low), "=r" (High));
    return ((ULONGLONG)High << 32) | Low;
}

FORCEINLINE VOID WriteCntvCval(_In_ ULONGLONG Value)
{
    __asm__ __volatile__("mcrr p15, 3, %0, %1, c14" : : "r" ((ULONG)Value), "r" ((ULONG)(Value >> 32)) : "memory");
}

FORCEINLINE VOID WriteCntvCtl(_In_ ULONG Value)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c3, 1\n\tisb" : : "r" (Value) : "memory");
}

FORCEINLINE ULONG ReadCntfrq(VOID)
{
    ULONG Value;

    __asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r" (Value));
    return Value;
}

static
NTSTATUS
TimerInitialize(
    _In_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    WriteCntvCtl(CNTV_CTL_IMASK);
    return STATUS_SUCCESS;
}

static
ULONGLONG
TimerQueryCounter(
    _In_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    return ReadCntvct();
}

/* the line stays up until the compare moves, masking it ends this tick */
static
VOID
TimerAcknowledgeInterrupt(
    _In_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    WriteCntvCtl(CNTV_CTL_ENABLE | CNTV_CTL_IMASK);
}

static
NTSTATUS
TimerArm(
    _In_ PVOID Data,
    _In_ TIMER_MODE Mode,
    _In_ ULONGLONG TickCount)
{
    UNREFERENCED_PARAMETER(Data);

    if (Mode != TimerModeOneShot)
        return STATUS_NOT_SUPPORTED;

    WriteCntvCval(ReadCntvct() + TickCount);
    WriteCntvCtl(CNTV_CTL_ENABLE);
    return STATUS_SUCCESS;
}

static
VOID
TimerStop(
    _In_ PVOID Data)
{
    UNREFERENCED_PARAMETER(Data);
    WriteCntvCtl(CNTV_CTL_IMASK);
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
AddResourceGroup(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group)
{
    PVIRT_CSRT_SHARED_INFO Info;
    CSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    INTERRUPT_INITIALIZATION_BLOCK Interrupt;
    TIMER_INITIALIZATION_BLOCK Timer;
    PHYSICAL_ADDRESS Address;
    NTSTATUS Status;

    if ((Group->VendorId != VIRT_CSRT_VENDOR_ID) || (Group->DeviceId != VIRT_CSRT_DEVICE_ID))
        return STATUS_NOT_SUPPORTED;

    if (Group->SharedInfoLength < sizeof(VIRT_CSRT_SHARED_INFO))
        return STATUS_INVALID_PARAMETER;

    Info = CSRT_GROUP_SHARED_INFO(Group);
    if (Info->Version != 1)
        return STATUS_REVISION_MISMATCH;

    Address.QuadPart = Info->DistributorBase;
    Gic.Distributor = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    Address.QuadPart = Info->CpuInterfaceBase;
    Gic.CpuInterface = HalImports->HalMapIoSpace(Address, 0x1000, MmNonCached);
    if (!Gic.Distributor || !Gic.CpuInterface)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&Interrupt, sizeof(Interrupt));
    Interrupt.Header.TableVersion = INTERRUPT_INITIALIZATION_VERSION;
    Interrupt.Header.TableSize = sizeof(Interrupt);
    Interrupt.FunctionTable.InitializeLocalUnit = GicInitializeLocalUnit;
    Interrupt.FunctionTable.InitializeIoUnit = GicInitializeIoUnit;
    Interrupt.FunctionTable.SetPriority = GicSetPriority;
    Interrupt.FunctionTable.AcceptAndGetSource = GicAcceptAndGetSource;
    Interrupt.FunctionTable.EndOfInterrupt = GicEndOfInterrupt;
    Interrupt.FunctionTable.SetLineState = GicSetLineState;
    Interrupt.FunctionTable.RequestInterrupt = GicRequestInterrupt;
    Interrupt.InternalData = &Gic;
    Interrupt.InternalDataSize = sizeof(Gic);
    Interrupt.KnownType = InterruptControllerGic;
    Interrupt.Capabilities = INTERRUPT_CONTROLLER_HAS_LOCAL_UNIT | INTERRUPT_CONTROLLER_HAS_PRIORITY_SCHEME;
    Interrupt.MaxPriority = VIRT_MAX_PRIORITY;
    Interrupt.MaxClusterSize = 8;
    Interrupt.MaxClusters = 1;

    Descriptor.Length = sizeof(Descriptor);
    Descriptor.Type = CSRT_RD_TYPE_INTERRUPT;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_INTERRUPT_CONTROLLER;
    Descriptor.Uid = 1;
    Status = HalImports->RegisterResourceDescriptor(Handle, Group, &Descriptor, &Interrupt);
    if (!NT_SUCCESS(Status))
        return Status;

    /* sgis 1 to 3 carry the software interrupts, gsis are the gic intids */
    Status = RegisterLines(Handle, Group, InterruptLineSoftwareOnlyProcessorLocal, 1, 4, 0xFFFFFFFF, 2);
    if (NT_SUCCESS(Status))
        Status = RegisterLines(Handle, Group, InterruptLineProcessorLocal, 16, 32, 16, 3);
    if (NT_SUCCESS(Status))
        Status = RegisterLines(Handle, Group, InterruptLineStandardPin, 32, 1020, 32, 4);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Timer, sizeof(Timer));
    Timer.Header.TableVersion = TIMER_INITIALIZATION_VERSION;
    Timer.Header.TableSize = sizeof(Timer);
    Timer.FunctionTable.Initialize = TimerInitialize;
    Timer.FunctionTable.QueryCounter = TimerQueryCounter;
    Timer.FunctionTable.AcknowledgeInterrupt = TimerAcknowledgeInterrupt;
    Timer.FunctionTable.ArmTimer = TimerArm;
    Timer.FunctionTable.Stop = TimerStop;
    Timer.InternalData = &TimerData;
    Timer.InternalDataSize = sizeof(TimerData);
    Timer.CounterBitWidth = 64;
    Timer.CounterFrequency = ReadCntfrq();
    Timer.Capabilities = TIMER_PER_PROCESSOR | TIMER_COUNTER_READABLE | TIMER_ONE_SHOT_CAPABLE |
                         TIMER_GENERATES_LINE_BASED_INTERRUPTS;
    Timer.Interrupt.LegacyIrq = -1;
    Timer.Interrupt.Gsi = (LONG)Info->TimerGsi;
    Timer.Interrupt.Polarity = InterruptActiveHigh;
    Timer.Interrupt.Mode = LevelSensitive;
    Timer.KnownType = TimerProcessor;
    Timer.Identifier = 1;

    Descriptor.Type = CSRT_RD_TYPE_TIMER;
    Descriptor.Subtype = CSRT_RD_SUBTYPE_TIMER;
    Descriptor.Uid = 5;
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
