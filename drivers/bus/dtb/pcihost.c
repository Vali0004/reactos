/*
 * PROJECT:     ReactOS Device Tree Bus Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     PCI Express host bridge PDO for pci-host-ecam-generic
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include "dtb.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* One bus of ECAM holds 32 devices of 8 functions with 4 KB each */
#define DTB_ECAM_BUS_SIZE       (32 * 8 * PAGE_SIZE)
#define DTB_ECAM_FUNCTION_SIZE  PAGE_SIZE

/* phys.hi of a PCI ranges entry, see the PCI bus binding */
#define DTB_PCI_SPACE(Hi)       (((Hi) >> 24) & 3)
#define DTB_PCI_SPACE_IO        1
#define DTB_PCI_SPACE_MEM32     2
#define DTB_PCI_SPACE_MEM64     3
#define DTB_PCI_PREFETCHABLE    0x40000000

/* pci.sys matches *PNP0A08 and *PNP0A03 in the critical device database */
static const WCHAR DtbHostBridgeDeviceId[] = L"DTB\\PNP0A08\0";
static const WCHAR DtbHostBridgeHardwareIds[] = L"DTB\\PNP0A08\0*PNP0A08\0";
static const WCHAR DtbHostBridgeCompatibleIds[] = L"*PNP0A03\0";
static const WCHAR DtbHostBridgeInstanceId[] = L"0\0";
static const WCHAR DtbHostBridgeDescription[] = L"PCI Express host bridge";

/* FUNCTIONS *****************************************************************/

static
ULONG
DtbConfigAccess(
    _Inout_ PDTB_HOST_BRIDGE Bridge,
    _In_ ULONG Bus,
    _In_ ULONG Slot,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ BOOLEAN Read);

static
ULONG
DtbCells(
    _In_ LONG Node,
    _In_ PCSTR Name,
    _In_ ULONG Default)
{
    const VOID *Cells = FdtGetProperty(DtbBlob, Node, Name, NULL);

    return Cells ? FdtRead32(Cells) : Default;
}

/**
 * @brief Collects the memory windows the host bridge forwards from ranges.
 *
 * Each entry is the 3 cell PCI address, a CPU address in the parent's cells
 * and a size in the bridge's own cells. The first I/O entry becomes the port
 * translation, memory entries become the bridge's windows.
 */
static
VOID
DtbParseRanges(
    _Inout_ PDTB_HOST_BRIDGE Bridge)
{
    const UCHAR *Ranges, *Entry;
    ULONG Length, ChildCells, ParentCells, SizeCells, Stride, Hi, i;
    ULONG64 CpuAddress, Size;

    Ranges = FdtGetProperty(DtbBlob, Bridge->Node, "ranges", &Length);
    if (!Ranges)
        return;

    ChildCells = DtbCells(Bridge->Node, "#address-cells", 3);
    SizeCells = DtbCells(Bridge->Node, "#size-cells", 2);
    ParentCells = DtbCells(FdtParentNode(DtbBlob, Bridge->Node), "#address-cells", 2);
    Stride = (ChildCells + ParentCells + SizeCells) * 4;
    if ((ChildCells != 3) || !Stride)
        return;

    for (i = 0; ((i + 1) * Stride <= Length) && (Bridge->WindowCount < DTB_MAX_WINDOWS); i++)
    {
        Entry = Ranges + i * Stride;
        Hi = FdtRead32(Entry);
        CpuAddress = FdtReadCells(Entry + ChildCells * 4, ParentCells);
        Size = FdtReadCells(Entry + (ChildCells + ParentCells) * 4, SizeCells);

        if ((DTB_PCI_SPACE(Hi) == DTB_PCI_SPACE_IO) && Size && !Bridge->HasIo)
        {
            Bridge->HasIo = TRUE;
            Bridge->IoPciBase = FdtReadCells(Entry + 4, 2);
            Bridge->IoCpuBase = CpuAddress;
            Bridge->IoLength = Size;
            continue;
        }

        if (((DTB_PCI_SPACE(Hi) != DTB_PCI_SPACE_MEM32) &&
             (DTB_PCI_SPACE(Hi) != DTB_PCI_SPACE_MEM64)) || !Size)
        {
            continue;
        }

        /* A plain memory descriptor has a 32-bit length, large windows need MemoryLarge first */
        if (Size > MAXULONG)
        {
            DPRINT1("DTB: skipping the %I64x byte window at %I64x\n", Size, CpuAddress);
            continue;
        }

        Bridge->Windows[Bridge->WindowCount].Base = CpuAddress;
        Bridge->Windows[Bridge->WindowCount].Length = Size;
        Bridge->Windows[Bridge->WindowCount].Prefetchable = !!(Hi & DTB_PCI_PREFETCHABLE);
        Bridge->WindowCount++;
    }
}

/**
 * @brief Writes each root bus function's interrupt into its Interrupt Line register.
 *
 * PC firmware does this before the OS runs and pci.sys takes the line from
 * there. The line is the GIC INTID, which the HAL uses as the vector.
 * Functions behind bridges are not handled yet.
 */
static
VOID
DtbProgramInterruptLines(
    _Inout_ PDTB_HOST_BRIDGE Bridge)
{
    PCI_SLOT_NUMBER Slot;
    USHORT VendorId;
    UCHAR HeaderType, Pin, Line;
    ULONG Device, Function, IntId;

    for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
    {
        for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
        {
            Slot.u.AsULONG = 0;
            Slot.u.bits.DeviceNumber = Device;
            Slot.u.bits.FunctionNumber = Function;

            if ((DtbConfigAccess(Bridge, Bridge->StartBus, Slot.u.AsULONG, (PUCHAR)&VendorId,
                                 FIELD_OFFSET(PCI_COMMON_HEADER, VendorID), sizeof(VendorId), TRUE) != sizeof(VendorId)) ||
                (VendorId == PCI_INVALID_VENDORID))
            {
                if (Function == 0)
                    break;
                continue;
            }

            DtbConfigAccess(Bridge, Bridge->StartBus, Slot.u.AsULONG, &Pin,
                            FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.InterruptPin), 1, TRUE);
            if (Pin && (Pin <= 4) &&
                FdtPciInterruptMap(DtbBlob, Bridge->Node, Bridge->StartBus, Device, Function, Pin, &IntId))
            {
                if (IntId <= 0xFF)
                {
                    Line = (UCHAR)IntId;
                    DtbConfigAccess(Bridge, Bridge->StartBus, Slot.u.AsULONG, &Line,
                                    FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.InterruptLine), 1, FALSE);
                    DPRINT1("DTB: %02x:%02x.%x pin %c is INTID %lu\n",
                            Bridge->StartBus, Device, Function, 'A' + Pin - 1, IntId);
                }
                else
                {
                    DPRINT1("DTB: %02x:%02x.%x INTID %lu does not fit the Interrupt Line\n",
                            Bridge->StartBus, Device, Function, IntId);
                }
            }

            /* Only multifunction devices have functions past 0 */
            if (Function == 0)
            {
                DtbConfigAccess(Bridge, Bridge->StartBus, Slot.u.AsULONG, &HeaderType,
                                FIELD_OFFSET(PCI_COMMON_HEADER, HeaderType), 1, TRUE);
                if (!(HeaderType & PCI_MULTIFUNCTION))
                    break;
            }
        }
    }
}

NTSTATUS
DtbCreateHostBridge(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    PDTB_HOST_BRIDGE Bridge;
    PDEVICE_OBJECT Pdo;
    const UCHAR *BusRange;
    ULONG64 EcamBase;
    ULONG Length;
    LONG Node;
    NTSTATUS Status;

    *DeviceObject = NULL;

    Node = FdtFindCompatible(DtbBlob, FDT_NO_NODE, "pci-host-ecam-generic");
    if ((Node < 0) || !FdtGetReg(DtbBlob, Node, 0, &EcamBase, NULL))
        return STATUS_NO_SUCH_DEVICE;

    Status = IoCreateDevice(DriverObject,
                            sizeof(DTB_HOST_BRIDGE),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
        return Status;

    Bridge = Pdo->DeviceExtension;
    RtlZeroMemory(Bridge, sizeof(*Bridge));
    Bridge->Common.Type = DtbHostBridgePdo;
    Bridge->Common.Self = Pdo;
    Bridge->Node = Node;
    Bridge->Present = TRUE;
    Bridge->EcamBase = EcamBase;
    Bridge->StartBus = 0;
    Bridge->EndBus = 0xFF;
    KeInitializeSpinLock(&Bridge->ConfigLock);

    BusRange = FdtGetProperty(DtbBlob, Node, "bus-range", &Length);
    if (BusRange && (Length >= 8))
    {
        Bridge->StartBus = (UCHAR)FdtRead32(BusRange);
        Bridge->EndBus = (UCHAR)FdtRead32(BusRange + 4);
    }

    DtbParseRanges(Bridge);
    DtbProgramInterruptLines(Bridge);

    DPRINT1("DTB: PCI host bridge %s, ECAM %I64x, buses %u-%u, %lu memory windows, I/O %I64x at %I64x\n",
            FdtNodeName(DtbBlob, Node), EcamBase, Bridge->StartBus, Bridge->EndBus,
            Bridge->WindowCount, Bridge->IoLength, Bridge->IoCpuBase);

    Pdo->Flags |= DO_POWER_PAGABLE;
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    *DeviceObject = Pdo;
    return STATUS_SUCCESS;
}

/* Maps one bus of ECAM the first time something touches it */
static
PUCHAR
DtbEcamBus(
    _Inout_ PDTB_HOST_BRIDGE Bridge,
    _In_ ULONG Bus)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Mapping;
    KIRQL OldIrql;

    if ((Bus < Bridge->StartBus) || (Bus > Bridge->EndBus))
        return NULL;

    if (Bridge->EcamBus[Bus])
        return Bridge->EcamBus[Bus];

    Address.QuadPart = Bridge->EcamBase + (ULONG64)(Bus - Bridge->StartBus) * DTB_ECAM_BUS_SIZE;
    Mapping = MmMapIoSpace(Address, DTB_ECAM_BUS_SIZE, MmNonCached);
    if (!Mapping)
        return NULL;

    KeAcquireSpinLock(&Bridge->ConfigLock, &OldIrql);
    if (!Bridge->EcamBus[Bus])
    {
        Bridge->EcamBus[Bus] = Mapping;
        Mapping = NULL;
    }
    KeReleaseSpinLock(&Bridge->ConfigLock, OldIrql);

    /* Someone else mapped it first */
    if (Mapping)
        MmUnmapIoSpace(Mapping, DTB_ECAM_BUS_SIZE);

    return Bridge->EcamBus[Bus];
}

/* Config accesses go one naturally aligned piece at a time, ECAM wants that */
static
ULONG
DtbConfigAccess(
    _Inout_ PDTB_HOST_BRIDGE Bridge,
    _In_ ULONG Bus,
    _In_ ULONG Slot,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ BOOLEAN Read)
{
    PCI_SLOT_NUMBER SlotNumber;
    PUCHAR Function, Register;
    ULONG Done = 0, Piece;

    if ((Offset >= DTB_ECAM_FUNCTION_SIZE) || (Length > DTB_ECAM_FUNCTION_SIZE - Offset))
        return 0;

    Function = DtbEcamBus(Bridge, Bus);
    if (!Function)
        return 0;

    SlotNumber.u.AsULONG = Slot;
    Function += ((SlotNumber.u.bits.DeviceNumber * 8) + SlotNumber.u.bits.FunctionNumber) *
                DTB_ECAM_FUNCTION_SIZE;

    while (Done < Length)
    {
        Register = Function + Offset + Done;
        if (!((Offset + Done) & 3) && (Length - Done >= 4))
            Piece = 4;
        else if (!((Offset + Done) & 1) && (Length - Done >= 2))
            Piece = 2;
        else
            Piece = 1;

        if (Read)
        {
            if (Piece == 4)
                *(PULONG)(Buffer + Done) = READ_REGISTER_ULONG((PULONG)Register);
            else if (Piece == 2)
                *(PUSHORT)(Buffer + Done) = READ_REGISTER_USHORT((PUSHORT)Register);
            else
                Buffer[Done] = READ_REGISTER_UCHAR(Register);
        }
        else
        {
            if (Piece == 4)
                WRITE_REGISTER_ULONG((PULONG)Register, *(PULONG)(Buffer + Done));
            else if (Piece == 2)
                WRITE_REGISTER_USHORT((PUSHORT)Register, *(PUSHORT)(Buffer + Done));
            else
                WRITE_REGISTER_UCHAR(Register, Buffer[Done]);
        }

        Done += Piece;
    }

    return Done;
}

/* pci.sys passes the absolute bus number despite the BusOffset name */
static
ULONG
NTAPI
DtbPciReadConfig(
    _In_ PVOID Context,
    _In_ ULONG BusOffset,
    _In_ ULONG Slot,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return DtbConfigAccess(Context, BusOffset, Slot, Buffer, Offset, Length, TRUE);
}

static
ULONG
NTAPI
DtbPciWriteConfig(
    _In_ PVOID Context,
    _In_ ULONG BusOffset,
    _In_ ULONG Slot,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return DtbConfigAccess(Context, BusOffset, Slot, Buffer, Offset, Length, FALSE);
}

/* Interrupt routing comes from interrupt-map later, lines stay as the firmware left them */
static
VOID
NTAPI
DtbPciPinToLine(
    _In_ PVOID Context,
    _Inout_ PPCI_COMMON_CONFIG PciData)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PciData);
}

static
VOID
NTAPI
DtbPciLineToPin(
    _In_ PVOID Context,
    _Inout_ PPCI_COMMON_CONFIG PciNewData,
    _In_ PPCI_COMMON_CONFIG PciOldData)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PciNewData);
    UNREFERENCED_PARAMETER(PciOldData);
}

static
VOID
NTAPI
DtbInterfaceNop(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

/* PCI I/O space sits in CPU memory, the port translator moves resources between the two */
static
NTSTATUS
NTAPI
DtbTranslatePort(
    _Inout_opt_ PVOID Context,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Source,
    _In_ RESOURCE_TRANSLATION_DIRECTION Direction,
    _In_opt_ ULONG AlternativesCount,
    _In_reads_opt_(AlternativesCount) IO_RESOURCE_DESCRIPTOR Alternatives[],
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Target)
{
    PDTB_HOST_BRIDGE Bridge = Context;
    ULONG64 Start;

    UNREFERENCED_PARAMETER(AlternativesCount);
    UNREFERENCED_PARAMETER(Alternatives);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    *Target = *Source;

    if (Direction == TranslateChildToParent)
    {
        if (Source->Type != CmResourceTypePort)
            return STATUS_SUCCESS;

        Start = Source->u.Port.Start.QuadPart;
        if ((Start < Bridge->IoPciBase) ||
            (Start + Source->u.Port.Length > Bridge->IoPciBase + Bridge->IoLength))
        {
            return STATUS_UNSUCCESSFUL;
        }

        Target->Type = CmResourceTypeMemory;
        Target->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Target->u.Memory.Start.QuadPart = Bridge->IoCpuBase + (Start - Bridge->IoPciBase);
        Target->u.Memory.Length = Source->u.Port.Length;
        return STATUS_TRANSLATION_COMPLETE;
    }

    /* Parent to child undoes it for what the arbiter handed out */
    if (Source->Type != CmResourceTypeMemory)
        return STATUS_SUCCESS;

    Start = Source->u.Memory.Start.QuadPart;
    if ((Start < Bridge->IoCpuBase) ||
        (Start + Source->u.Memory.Length > Bridge->IoCpuBase + Bridge->IoLength))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Target->Type = CmResourceTypePort;
    Target->Flags = CM_RESOURCE_PORT_IO;
    Target->u.Port.Start.QuadPart = Bridge->IoPciBase + (Start - Bridge->IoCpuBase);
    Target->u.Port.Length = Source->u.Memory.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
DtbTranslatePortRequirements(
    _Inout_opt_ PVOID Context,
    _In_ PIO_RESOURCE_DESCRIPTOR Source,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG TargetCount,
    _Out_writes_(*TargetCount) PIO_RESOURCE_DESCRIPTOR *Target)
{
    PDTB_HOST_BRIDGE Bridge = Context;
    PIO_RESOURCE_DESCRIPTOR Result;
    ULONG64 Minimum, Maximum;

    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    *TargetCount = 0;
    *Target = NULL;

    if (Source->Type != CmResourceTypePort)
        return STATUS_SUCCESS;

    /* Clip the request to the window, nothing outside it decodes */
    Minimum = max((ULONG64)Source->u.Port.MinimumAddress.QuadPart, Bridge->IoPciBase);
    Maximum = min((ULONG64)Source->u.Port.MaximumAddress.QuadPart,
                  Bridge->IoPciBase + Bridge->IoLength - 1);
    if ((Maximum < Minimum) || (Maximum - Minimum + 1 < Source->u.Port.Length))
        return STATUS_UNSUCCESSFUL;

    Result = ExAllocatePoolWithTag(PagedPool, sizeof(*Result), TAG_DTB);
    if (!Result)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Result = *Source;
    Result->Type = CmResourceTypeMemory;
    Result->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Result->u.Memory.Length = Source->u.Port.Length;
    Result->u.Memory.Alignment = Source->u.Port.Alignment;
    Result->u.Memory.MinimumAddress.QuadPart = Bridge->IoCpuBase + (Minimum - Bridge->IoPciBase);
    Result->u.Memory.MaximumAddress.QuadPart = Bridge->IoCpuBase + (Maximum - Bridge->IoPciBase);

    *TargetCount = 1;
    *Target = Result;
    return STATUS_TRANSLATION_COMPLETE;
}

static
NTSTATUS
DtbQueryTranslator(
    _Inout_ PDTB_HOST_BRIDGE Bridge,
    _In_ PIO_STACK_LOCATION IoStack)
{
    PTRANSLATOR_INTERFACE Translator;

    if ((ULONG_PTR)IoStack->Parameters.QueryInterface.InterfaceSpecificData != CmResourceTypePort ||
        !Bridge->HasIo)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (IoStack->Parameters.QueryInterface.Size < sizeof(TRANSLATOR_INTERFACE))
        return STATUS_INVALID_PARAMETER;

    Translator = (PTRANSLATOR_INTERFACE)IoStack->Parameters.QueryInterface.Interface;
    Translator->Size = sizeof(TRANSLATOR_INTERFACE);
    Translator->Version = 1;
    Translator->Context = Bridge;
    Translator->InterfaceReference = DtbInterfaceNop;
    Translator->InterfaceDereference = DtbInterfaceNop;
    Translator->TranslateResources = DtbTranslatePort;
    Translator->TranslateResourceRequirements = DtbTranslatePortRequirements;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DtbQueryInterface(
    _Inout_ PDTB_HOST_BRIDGE Bridge,
    _In_ PIO_STACK_LOCATION IoStack)
{
    PPCI_BUS_INTERFACE_STANDARD BusInterface;
    ULONG Size;

    if (IsEqualGUID(IoStack->Parameters.QueryInterface.InterfaceType,
                    &GUID_TRANSLATOR_INTERFACE_STANDARD))
    {
        if ((ULONG_PTR)IoStack->Parameters.QueryInterface.InterfaceSpecificData == CmResourceTypeInterrupt)
            return DtbQueryInterruptTranslator(IoStack);
        return DtbQueryTranslator(Bridge, IoStack);
    }

    if (!IsEqualGUID(IoStack->Parameters.QueryInterface.InterfaceType,
                     &GUID_PCI_BUS_INTERFACE_STANDARD) &&
        !IsEqualGUID(IoStack->Parameters.QueryInterface.InterfaceType,
                     &GUID_PCI_BUS_INTERFACE_STANDARD2))
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* Everything through LineToPin is the minimum, the rest stays unimplemented */
    Size = IoStack->Parameters.QueryInterface.Size;
    if (Size < (ULONG)FIELD_OFFSET(PCI_BUS_INTERFACE_STANDARD, RootBusCapability))
        return STATUS_INVALID_PARAMETER;
    if (IoStack->Parameters.QueryInterface.Version > PCI_BUS_INTERFACE_STANDARD_VERSION)
        return STATUS_NOINTERFACE;

    BusInterface = (PPCI_BUS_INTERFACE_STANDARD)IoStack->Parameters.QueryInterface.Interface;
    RtlZeroMemory(BusInterface, Size);
    BusInterface->Size = (USHORT)FIELD_OFFSET(PCI_BUS_INTERFACE_STANDARD, RootBusCapability);
    BusInterface->Version = PCI_BUS_INTERFACE_STANDARD_VERSION;
    BusInterface->Context = Bridge;
    BusInterface->InterfaceReference = DtbInterfaceNop;
    BusInterface->InterfaceDereference = DtbInterfaceNop;
    BusInterface->ReadConfig = DtbPciReadConfig;
    BusInterface->WriteConfig = DtbPciWriteConfig;
    BusInterface->PinToLine = DtbPciPinToLine;
    BusInterface->LineToPin = DtbPciLineToPin;
    return STATUS_SUCCESS;
}

/* The boot configuration carries the bus numbers, pci.sys takes its root range from it */
static
NTSTATUS
DtbQueryResources(
    _In_ PDTB_HOST_BRIDGE Bridge,
    _Out_ PCM_RESOURCE_LIST *Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_RESOURCE_LIST List;
    ULONG Count, Size, i;

    Count = 1 + Bridge->WindowCount;
    Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors[Count]);
    List = ExAllocatePoolZero(PagedPool, Size, TAG_DTB);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;

    List->Count = 1;
    List->List[0].InterfaceType = PNPBus;
    List->List[0].BusNumber = 0;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = Count;

    Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
    Descriptor->Type = CmResourceTypeBusNumber;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->u.BusNumber.Start = Bridge->StartBus;
    Descriptor->u.BusNumber.Length = Bridge->EndBus - Bridge->StartBus + 1;

    for (i = 0; i < Bridge->WindowCount; i++)
    {
        Descriptor++;
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                            (Bridge->Windows[i].Prefetchable ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
        Descriptor->u.Memory.Start.QuadPart = Bridge->Windows[i].Base;
        Descriptor->u.Memory.Length = (ULONG)Bridge->Windows[i].Length;
    }

    *Resources = List;
    return STATUS_SUCCESS;
}

/* The same resources as fixed requirements, so the arbiters reserve them for the bridge */
static
NTSTATUS
DtbQueryResourceRequirements(
    _In_ PDTB_HOST_BRIDGE Bridge,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Requirements)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Count, Size, i;

    Count = 1 + Bridge->WindowCount;
    Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors[Count]);
    List = ExAllocatePoolZero(PagedPool, Size, TAG_DTB);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;

    List->ListSize = Size;
    List->InterfaceType = PNPBus;
    List->BusNumber = 0;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = Count;

    Descriptor = List->List[0].Descriptors;
    Descriptor->Type = CmResourceTypeBusNumber;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->u.BusNumber.MinBusNumber = Bridge->StartBus;
    Descriptor->u.BusNumber.MaxBusNumber = Bridge->EndBus;
    Descriptor->u.BusNumber.Length = Bridge->EndBus - Bridge->StartBus + 1;

    for (i = 0; i < Bridge->WindowCount; i++)
    {
        Descriptor++;
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                            (Bridge->Windows[i].Prefetchable ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
        Descriptor->u.Memory.Length = (ULONG)Bridge->Windows[i].Length;
        Descriptor->u.Memory.Alignment = 1;
        Descriptor->u.Memory.MinimumAddress.QuadPart = Bridge->Windows[i].Base;
        Descriptor->u.Memory.MaximumAddress.QuadPart =
            Bridge->Windows[i].Base + Bridge->Windows[i].Length - 1;
    }

    *Requirements = List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DtbQueryId(
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PULONG_PTR Information)
{
    PCWSTR Ids;
    SIZE_T Length;
    PWCHAR Buffer;

    switch (IdType)
    {
        case BusQueryDeviceID:
            Ids = DtbHostBridgeDeviceId;
            Length = sizeof(DtbHostBridgeDeviceId);
            break;
        case BusQueryHardwareIDs:
            Ids = DtbHostBridgeHardwareIds;
            Length = sizeof(DtbHostBridgeHardwareIds);
            break;
        case BusQueryCompatibleIDs:
            Ids = DtbHostBridgeCompatibleIds;
            Length = sizeof(DtbHostBridgeCompatibleIds);
            break;
        case BusQueryInstanceID:
            Ids = DtbHostBridgeInstanceId;
            Length = sizeof(DtbHostBridgeInstanceId);
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    Buffer = DtbAllocateMultiSz(Ids, Length);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
DtbHostBridgePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_HOST_BRIDGE Bridge = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_CAPABILITIES Capabilities;
    PDEVICE_RELATIONS Relations;
    ULONG_PTR Information = Irp->IoStatus.Information;
    NTSTATUS Status = Irp->IoStatus.Status;
    PWCHAR Text;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Status = STATUS_UNSUCCESSFUL;
            break;

        case IRP_MN_REMOVE_DEVICE:
            /* The bridge stays as long as the tree reports it */
            Status = STATUS_SUCCESS;
            if (!Bridge->Present)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                IoDeleteDevice(DeviceObject);
                return Status;
            }
            break;

        case IRP_MN_QUERY_ID:
            Status = DtbQueryId(IoStack->Parameters.QueryId.IdType, &Information);
            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (IoStack->Parameters.QueryDeviceText.DeviceTextType != DeviceTextDescription)
                break;
            Text = DtbAllocateMultiSz(DtbHostBridgeDescription, sizeof(DtbHostBridgeDescription));
            if (!Text)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Information = (ULONG_PTR)Text;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Capabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;
            Capabilities->UniqueID = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->Address = 0;
            Capabilities->UINumber = 0;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = DtbQueryResources(Bridge, (PCM_RESOURCE_LIST *)&Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = DtbQueryResourceRequirements(Bridge, (PIO_RESOURCE_REQUIREMENTS_LIST *)&Information);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IoStack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
                break;
            Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), TAG_DTB);
            if (!Relations)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            ObReferenceObject(DeviceObject);
            Relations->Count = 1;
            Relations->Objects[0] = DeviceObject;
            Information = (ULONG_PTR)Relations;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_INTERFACE:
            Status = DtbQueryInterface(Bridge, IoStack);
            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Information |= PNP_DEVICE_NOT_DISABLEABLE;
            Status = STATUS_SUCCESS;
            break;
    }

    return DtbCompleteIrp(Irp, Status, Information);
}
