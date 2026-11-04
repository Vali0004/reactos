/*
 * PROJECT:     ReactOS Device Tree Bus Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Driver entry and the bus FDO on the HAL's device tree PDO
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * The HAL reports DTB_HAL\DTB0000 instead of the ACPI PDO when the firmware
 * gave no ACPI tables. This driver takes that PDO and reports the devices
 * the device tree describes, the PCI Express host bridge and SoC USB hosts.
 */

/* INCLUDES ******************************************************************/

#include "dtb.h"
#define NDEBUG
#include <debug.h>

extern NTSYSAPI PLOADER_PARAMETER_BLOCK KeLoaderBlock;

/* GLOBALS *******************************************************************/

/* Private copy of the blob, the loader block is gone after boot drivers start */
PVOID DtbBlob;

DRIVER_INITIALIZE DriverEntry;

/* FUNCTIONS *****************************************************************/

NTSTATUS
DtbCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* Copies a MULTI_SZ, Length counts the bytes including the final terminator */
PWCHAR
DtbAllocateMultiSz(
    _In_ PCWSTR Strings,
    _In_ SIZE_T Length)
{
    PWCHAR Buffer;

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_DTB);
    if (Buffer)
        RtlCopyMemory(Buffer, Strings, Length);

    return Buffer;
}

static
VOID
NTAPI
DtbTranslatorNop(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

/**
 * @brief Turns a routed PCI line into the vector, IRQL and affinity the HAL hands out.
 *
 * The line already is the GIC INTID, the HAL keeps that as the vector and
 * picks the device IRQL for it. ACPI does this job when the firmware has it.
 */
static
NTSTATUS
NTAPI
DtbTranslateInterrupt(
    _Inout_opt_ PVOID Context,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Source,
    _In_ RESOURCE_TRANSLATION_DIRECTION Direction,
    _In_opt_ ULONG AlternativesCount,
    _In_reads_opt_(AlternativesCount) IO_RESOURCE_DESCRIPTOR Alternatives[],
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Target)
{
    KAFFINITY Affinity;
    KIRQL Irql;
    ULONG Vector;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(AlternativesCount);
    UNREFERENCED_PARAMETER(Alternatives);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    *Target = *Source;
    if (Source->Type != CmResourceTypeInterrupt)
        return STATUS_SUCCESS;

    /* Messages are already vectors, the HAL owns those */
    if (Source->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
        return STATUS_SUCCESS;

    if (Direction == TranslateParentToChild)
    {
        Target->u.Interrupt.Level = Source->u.Interrupt.Vector;
        Target->u.Interrupt.Vector = Source->u.Interrupt.Vector;
        Target->u.Interrupt.Affinity = MAXULONG_PTR;
        return STATUS_SUCCESS;
    }

    Vector = HalGetInterruptVector(Internal,
                                   0,
                                   Source->u.Interrupt.Level,
                                   Source->u.Interrupt.Vector,
                                   &Irql,
                                   &Affinity);
    if (!Vector)
        return STATUS_UNSUCCESSFUL;

    Target->u.Interrupt.Level = Irql;
    Target->u.Interrupt.Vector = Vector;
    Target->u.Interrupt.Affinity = Affinity;
    return STATUS_TRANSLATION_COMPLETE;
}

/* Lines and vectors are both GIC INTIDs, the requirement passes through unchanged */
static
NTSTATUS
NTAPI
DtbTranslateInterruptRequirements(
    _Inout_opt_ PVOID Context,
    _In_ PIO_RESOURCE_DESCRIPTOR Source,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG TargetCount,
    _Out_writes_(*TargetCount) PIO_RESOURCE_DESCRIPTOR *Target)
{
    PIO_RESOURCE_DESCRIPTOR Result;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    Result = ExAllocatePoolWithTag(PagedPool, sizeof(*Result), TAG_DTB);
    if (!Result)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Result = *Source;
    *TargetCount = 1;
    *Target = Result;
    return STATUS_TRANSLATION_COMPLETE;
}

NTSTATUS
DtbQueryInterruptTranslator(
    _In_ PIO_STACK_LOCATION IoStack)
{
    PTRANSLATOR_INTERFACE Translator;

    if (!IsEqualGUID(IoStack->Parameters.QueryInterface.InterfaceType,
                     &GUID_TRANSLATOR_INTERFACE_STANDARD) ||
        ((ULONG_PTR)IoStack->Parameters.QueryInterface.InterfaceSpecificData != CmResourceTypeInterrupt))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (IoStack->Parameters.QueryInterface.Size < sizeof(TRANSLATOR_INTERFACE))
        return STATUS_INVALID_PARAMETER;

    Translator = (PTRANSLATOR_INTERFACE)IoStack->Parameters.QueryInterface.Interface;
    Translator->Size = sizeof(TRANSLATOR_INTERFACE);
    Translator->Version = 1;
    Translator->Context = NULL;
    Translator->InterfaceReference = DtbTranslatorNop;
    Translator->InterfaceDereference = DtbTranslatorNop;
    Translator->TranslateResources = DtbTranslateInterrupt;
    Translator->TranslateResourceRequirements = DtbTranslateInterruptRequirements;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DtbCopyLoaderBlob(VOID)
{
    PHYSICAL_ADDRESS Address;
    ULONG Size;
    PVOID Mapping;

    if (!FdtFindLoaderBlob(KeLoaderBlock, &Address, &Size))
    {
        DPRINT1("DTB: the loader passed no device tree\n");
        return STATUS_NO_SUCH_DEVICE;
    }

    Mapping = MmMapIoSpace(Address, Size, MmCached);
    if (!Mapping)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!FdtCheckHeader(Mapping) || (FdtTotalSize(Mapping) > Size))
    {
        DPRINT1("DTB: the device tree at %I64x is not usable\n", Address.QuadPart);
        MmUnmapIoSpace(Mapping, Size);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    DtbBlob = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_DTB);
    if (DtbBlob)
        RtlCopyMemory(DtbBlob, Mapping, Size);

    MmUnmapIoSpace(Mapping, Size);
    return DtbBlob ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
DtbFdoQueryBusRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_FDO_EXTENSION FdoExtension = DeviceObject->DeviceExtension;
    PDEVICE_RELATIONS Previous, Relations;
    ULONG Count, PreviousCount;
    NTSTATUS Status;

    ULONG i;

    if (!FdoExtension->HostBridge)
    {
        Status = DtbCreateHostBridge(DeviceObject->DriverObject, &FdoExtension->HostBridge);
        if (!NT_SUCCESS(Status) && (Status != STATUS_NO_SUCH_DEVICE))
            DPRINT1("DTB: no PCI host bridge PDO, 0x%lx\n", Status);
    }

    if (!FdoExtension->PlatformEnumerated)
    {
        FdoExtension->PlatformCount = DtbCreatePlatformDevices(DeviceObject->DriverObject,
                                                               FdoExtension->Platform,
                                                               RTL_NUMBER_OF(FdoExtension->Platform));
        FdoExtension->PlatformEnumerated = TRUE;
    }

    /* Keep whatever a filter above already reported */
    Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
    PreviousCount = Previous ? Previous->Count : 0;
    Count = PreviousCount + (FdoExtension->HostBridge ? 1 : 0) + FdoExtension->PlatformCount;

    Relations = ExAllocatePoolWithTag(PagedPool,
                                      FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count ? Count : 1]),
                                      TAG_DTB);
    if (!Relations)
        return STATUS_INSUFFICIENT_RESOURCES;

    Relations->Count = Count;
    if (PreviousCount)
        RtlCopyMemory(Relations->Objects, Previous->Objects, PreviousCount * sizeof(PDEVICE_OBJECT));
    Count = PreviousCount;
    if (FdoExtension->HostBridge)
    {
        ObReferenceObject(FdoExtension->HostBridge);
        Relations->Objects[Count++] = FdoExtension->HostBridge;
    }
    for (i = 0; i < FdoExtension->PlatformCount; i++)
    {
        ObReferenceObject(FdoExtension->Platform[i]);
        Relations->Objects[Count++] = FdoExtension->Platform[i];
    }

    if (Previous)
        ExFreePool(Previous);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DtbFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_FDO_EXTENSION FdoExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(FdoExtension->LowerDevice, Irp) ?
                     Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            return DtbCompleteIrp(Irp, Status, 0);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IoStack->Parameters.QueryDeviceRelations.Type != BusRelations)
                break;

            Status = DtbFdoQueryBusRelations(DeviceObject, Irp);
            if (!NT_SUCCESS(Status))
                return DtbCompleteIrp(Irp, Status, 0);
            Irp->IoStatus.Status = Status;
            break;

        case IRP_MN_QUERY_INTERFACE:
            /* The PCI root is the only child, its interrupts get translated here like ACPI does */
            Status = DtbQueryInterruptTranslator(IoStack);
            if (NT_SUCCESS(Status))
                return DtbCompleteIrp(Irp, Status, 0);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            /* The platform bus never goes away */
            return DtbCompleteIrp(Irp, STATUS_UNSUCCESSFUL, 0);

        case IRP_MN_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoDetachDevice(FdoExtension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
DtbDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;

    if (Common->Type == DtbFdo)
        return DtbFdoPnp(DeviceObject, Irp);
    if (Common->Type == DtbPlatformPdo)
        return DtbPlatformPnp(DeviceObject, Irp);

    return DtbHostBridgePnp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
DtbDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);

    if (Common->Type == DtbFdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(((PDTB_FDO_EXTENSION)Common)->LowerDevice, Irp);
    }

    /* The host bridge has no power control of its own */
    Status = Irp->IoStatus.Status;
    if ((IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_SET_POWER) ||
        (IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_POWER))
    {
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
DtbDispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PDTB_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Common->Type == DtbFdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(((PDTB_FDO_EXTENSION)Common)->LowerDevice, Irp);
    }

    Status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
DtbAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDTB_FDO_EXTENSION FdoExtension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(DTB_FDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;

    FdoExtension = Fdo->DeviceExtension;
    RtlZeroMemory(FdoExtension, sizeof(*FdoExtension));
    FdoExtension->Common.Type = DtbFdo;
    FdoExtension->Common.Self = Fdo;

    FdoExtension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!FdoExtension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    Status = DtbCopyLoaderBlob();
    if (!NT_SUCCESS(Status))
        return Status;

    DriverObject->DriverExtension->AddDevice = DtbAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DtbDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DtbDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DtbDispatchSystemControl;

    DPRINT1("DTB: device tree bus driver loaded, %lu byte tree\n", FdtTotalSize(DtbBlob));
    return STATUS_SUCCESS;
}
