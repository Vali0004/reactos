/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HAL extension loading and SoC resource registration
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "halextp.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define HALEXT_MAX_EXTENSIONS   8
#define HALEXT_MAX_TIMERS       8
#define HALEXT_MAX_CONTROLLERS  2
#define HALEXT_MAX_LINES        8

static POS_EXTENSION_HAL_EXTENSION_EXPORTS HalpExtExports[HALEXT_MAX_EXTENSIONS];
static ULONG HalpExtCount;

TIMER_INITIALIZATION_BLOCK HalpExtTimers[HALEXT_MAX_TIMERS];
ULONG HalpExtTimerCount;

HALEXT_INTERRUPT_CONTROLLER HalpExtControllers[HALEXT_MAX_CONTROLLERS];
ULONG HalpExtControllerCount;

INTERRUPT_LINE_DESCRIPTION HalpExtLines[HALEXT_MAX_LINES];
ULONG HalpExtLineCount;

DMA_INITIALIZATION_BLOCK HalpExtDmaControllers[HALEXT_MAX_CONTROLLERS];
ULONG HalpExtDmaControllerCount;

/* registration is only open while phase 0 hands out resource groups */
static BOOLEAN HalpExtRegistrationOpen;

/* IMPORTS FOR EXTENSIONS ****************************************************/

static
PCSRT_RESOURCE_DESCRIPTOR_HEADER
HalpExtGetNextResourceDescriptor(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    _In_opt_ PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    _In_ USHORT ResourceType,
    _In_ USHORT ResourceSubtype,
    _In_ ULONG ResourceId)
{
    PCSRT_RESOURCE_DESCRIPTOR_HEADER Descriptor;
    PUCHAR End;

    UNREFERENCED_PARAMETER(Handle);

    End = (PUCHAR)ResourceGroup + ResourceGroup->Length;
    if (ResourceDescriptor)
    {
        Descriptor = (PCSRT_RESOURCE_DESCRIPTOR_HEADER)((PUCHAR)ResourceDescriptor + ResourceDescriptor->Length);
    }
    else
    {
        Descriptor = (PCSRT_RESOURCE_DESCRIPTOR_HEADER)((PUCHAR)ResourceGroup +
                                                        sizeof(*ResourceGroup) +
                                                        ResourceGroup->SharedInfoLength);
    }

    while (((PUCHAR)Descriptor + sizeof(*Descriptor)) <= End)
    {
        if ((Descriptor->Length < sizeof(*Descriptor)) || (((PUCHAR)Descriptor + Descriptor->Length) > End))
            break;

        if (((ResourceType == CSRT_RD_TYPE_ANY) || (Descriptor->Type == ResourceType)) &&
            ((ResourceSubtype == CSRT_RD_SUBTYPE_ANY) || (Descriptor->Subtype == ResourceSubtype)) &&
            ((ResourceId == CSRT_RD_UID_ANY) || (Descriptor->Uid == ResourceId)))
        {
            return Descriptor;
        }

        Descriptor = (PCSRT_RESOURCE_DESCRIPTOR_HEADER)((PUCHAR)Descriptor + Descriptor->Length);
    }

    return NULL;
}

static
NTSTATUS
HalpExtRegisterTimer(
    _In_ PTIMER_INITIALIZATION_BLOCK Block)
{
    if ((Block->Header.TableVersion != TIMER_INITIALIZATION_VERSION) ||
        (Block->Header.TableSize != sizeof(*Block)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Block->Capabilities & ~TIMER_VALID_CAPABILITIES) ||
        !Block->FunctionTable.Initialize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (HalpExtTimerCount >= HALEXT_MAX_TIMERS)
        return STATUS_INSUFFICIENT_RESOURCES;

    HalpExtTimers[HalpExtTimerCount++] = *Block;
    DPRINT1("HALEXT: timer type %lu at %I64u Hz registered\n", Block->KnownType, Block->CounterFrequency);
    return STATUS_SUCCESS;
}

/* the table grew over time, the block size says how many entries this one has */
static
NTSTATUS
HalpExtRegisterInterruptController(
    _In_ PSOC_INITIALIZATION_HEADER Header)
{
    PHALEXT_INTERRUPT_CONTROLLER Controller;
    ULONG TableSize;

    if (Header->TableVersion != INTERRUPT_INITIALIZATION_VERSION)
        return STATUS_INVALID_PARAMETER;

    if (Header->TableSize < sizeof(INTERRUPT_INITIALIZATION_BLOCK))
        return STATUS_INVALID_PARAMETER;

    TableSize = Header->TableSize - sizeof(*Header) - sizeof(INTERRUPT_CONTROLLER_DESCRIPTION);
    if ((TableSize < sizeof(INTERRUPT_FUNCTION_TABLE)) ||
        (TableSize > sizeof(INTERRUPT_FUNCTION_TABLE_EX)) ||
        (TableSize % sizeof(PVOID)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (HalpExtControllerCount >= HALEXT_MAX_CONTROLLERS)
        return STATUS_INSUFFICIENT_RESOURCES;

    Controller = &HalpExtControllers[HalpExtControllerCount];
    RtlZeroMemory(Controller, sizeof(*Controller));
    RtlCopyMemory(&Controller->FunctionTable, Header + 1, TableSize);
    RtlCopyMemory(&Controller->Description,
                  (PUCHAR)(Header + 1) + TableSize,
                  sizeof(Controller->Description));

    if (!Controller->FunctionTable.Base.InitializeLocalUnit ||
        !Controller->FunctionTable.Base.AcceptAndGetSource ||
        !Controller->FunctionTable.Base.EndOfInterrupt ||
        !Controller->FunctionTable.Base.SetLineState)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HalpExtControllerCount++;
    DPRINT1("HALEXT: interrupt controller type %lu id %lu registered\n",
            Controller->Description.KnownType, Controller->Description.Identifier);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpExtRegisterResourceDescriptor(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    _In_ PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    _In_opt_ PVOID ResourceDescriptorInfo)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(ResourceGroup);

    if (!HalpExtRegistrationOpen)
        return STATUS_TOO_LATE;

    if (!ResourceDescriptor || !ResourceDescriptorInfo)
        return STATUS_INVALID_PARAMETER;

    switch (ResourceDescriptor->Type)
    {
        case CSRT_RD_TYPE_TIMER:
            if (ResourceDescriptor->Subtype != CSRT_RD_SUBTYPE_TIMER)
                break;
            return HalpExtRegisterTimer(ResourceDescriptorInfo);

        case CSRT_RD_TYPE_INTERRUPT:
            if (ResourceDescriptor->Subtype == CSRT_RD_SUBTYPE_INTERRUPT_CONTROLLER)
                return HalpExtRegisterInterruptController(ResourceDescriptorInfo);

            if (ResourceDescriptor->Subtype == CSRT_RD_SUBTYPE_INTERRUPT_LINES)
            {
                if (HalpExtLineCount >= HALEXT_MAX_LINES)
                    return STATUS_INSUFFICIENT_RESOURCES;
                HalpExtLines[HalpExtLineCount++] = *(PINTERRUPT_LINE_DESCRIPTION)ResourceDescriptorInfo;
                return STATUS_SUCCESS;
            }
            break;

        case CSRT_RD_TYPE_DMA:
            if (ResourceDescriptor->Subtype == CSRT_RD_SUBTYPE_DMA_CONTROLLER)
            {
                if (HalpExtDmaControllerCount >= HALEXT_MAX_CONTROLLERS)
                    return STATUS_INSUFFICIENT_RESOURCES;
                HalpExtDmaControllers[HalpExtDmaControllerCount++] = *(PDMA_INITIALIZATION_BLOCK)ResourceDescriptorInfo;
                return STATUS_SUCCESS;
            }

            /* channels need the hal dma adapter, which only knows system dma for now */
            if (ResourceDescriptor->Subtype == CSRT_RD_SUBTYPE_DMA_CHANNEL)
                return STATUS_NOT_SUPPORTED;
            break;
    }

    return STATUS_NOT_SUPPORTED;
}

static
PVOID
HalpExtGetAcpiTable(
    _In_ ULONG Handle,
    _In_ ULONG Signature,
    _In_opt_ PCSTR OemId,
    _In_opt_ PCSTR OemTableId)
{
    PDESCRIPTION_HEADER Table;

    UNREFERENCED_PARAMETER(Handle);

    Table = HalAcpiGetTable(NULL, Signature);
    if (!Table)
        return NULL;

    if (OemId && strncmp((PCSTR)Table->OEMID, OemId, sizeof(Table->OEMID)))
        return NULL;

    if (OemTableId && strncmp((PCSTR)Table->OEMTableID, OemTableId, sizeof(Table->OEMTableID)))
        return NULL;

    return Table;
}

static
PVOID
HalpExtMapIoSpace(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PHYSICAL_ADDRESS PageAddress;
    ULONG Offset;
    PVOID Base;

    Offset = PhysicalAddress.LowPart & (PAGE_SIZE - 1);
    PageAddress.QuadPart = PhysicalAddress.QuadPart - Offset;

    Base = HalpMapPhysicalMemory(PageAddress,
                                 BYTES_TO_PAGES(Offset + NumberOfBytes),
                                 CacheType != MmCached);
    if (!Base)
        return NULL;

    return (PUCHAR)Base + Offset;
}

static
VOID
HalpExtUnmapIoSpace(
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T NumberOfBytes)
{
    ULONG Offset = (ULONG_PTR)VirtualAddress & (PAGE_SIZE - 1);

    HalpUnmapVirtualAddress((PUCHAR)VirtualAddress - Offset, BYTES_TO_PAGES(Offset + NumberOfBytes));
}

/* the arm hal keeps no address usage list yet, the claim only has to be legal */
static
NTSTATUS
HalpExtRegisterPermanentAddressUsage(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG SizeInBytes)
{
    if (!HalpExtRegistrationOpen)
        return STATUS_TOO_LATE;

    DPRINT("HALEXT: claims %I64x + %lx\n", PhysicalAddress.QuadPart, SizeInBytes);
    return STATUS_SUCCESS;
}

static
VOID
HalpExtSetTimerProblem(
    _In_ PVOID TimerData,
    _In_ ULONG ProblemCode,
    _In_ NTSTATUS ProblemStatus)
{
    DPRINT1("HALEXT: timer %p problem %lu status %lx\n", TimerData, ProblemCode, ProblemStatus);
}

static
VOID
HalpExtSetInterruptProblem(
    _In_ PVOID ControllerData,
    _In_ ULONG ProblemCode,
    _In_ NTSTATUS ProblemStatus)
{
    DPRINT1("HALEXT: interrupt controller %p problem %lu status %lx\n", ControllerData, ProblemCode, ProblemStatus);
}

static
NTSTATUS
HalpExtFixInterruptLine(
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_LINE_STATE State)
{
    UNREFERENCED_PARAMETER(Line);
    UNREFERENCED_PARAMETER(State);
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
HalpExtUpdateTimerCapabilities(
    _In_ PVOID TimerData,
    _In_ ULONG SetCapabilities,
    _In_ ULONG ClearCapabilities)
{
    ULONG i;

    if ((SetCapabilities | ClearCapabilities) & ~TIMER_UPDATABLE_CAPABILITIES)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < HalpExtTimerCount; i++)
    {
        if (HalpExtTimers[i].InternalData == TimerData)
        {
            HalpExtTimers[i].Capabilities |= SetCapabilities;
            HalpExtTimers[i].Capabilities &= ~ClearCapabilities;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

static OS_EXTENSION_HAL_EXTENSION_IMPORTS HalpExtImports =
{
    HalpExtRegisterResourceDescriptor,
    HalpExtGetNextResourceDescriptor,
    HalpExtGetAcpiTable,
    HalpExtMapIoSpace,
    HalpExtUnmapIoSpace,
    HalpExtRegisterPermanentAddressUsage,
    HalpExtSetTimerProblem,
    HalpExtSetInterruptProblem,
    HalpExtFixInterruptLine,
    HalpExtUpdateTimerCapabilities,
    RtlRaiseException
};

/* FUNCTIONS *****************************************************************/

/* hands every csrt group to the extension the loader matched it with */
static
VOID
HalpExtAddResourceGroups(
    _In_ PDESCRIPTION_HEADER Csrt,
    _In_ PHAL_EXTENSION_MODULE_ENTRY Module,
    _In_ ULONG Handle)
{
    PCSRT_RESOURCE_GROUP_HEADER Group;
    PUCHAR End;
    NTSTATUS Status;

    Group = (PCSRT_RESOURCE_GROUP_HEADER)(Csrt + 1);
    End = (PUCHAR)Csrt + Csrt->Length;

    while (((PUCHAR)Group + sizeof(*Group)) <= End)
    {
        if ((Group->Length < sizeof(*Group)) || (((PUCHAR)Group + Group->Length) > End))
            break;

        if ((Group->VendorId == Module->VendorId) && (Group->DeviceId == Module->DeviceId))
        {
            Status = HalpExtExports[Handle]->AddResourceGroup(Handle, Group);
            if (!NT_SUCCESS(Status))
                DPRINT1("HALEXT: %wZ refused a group, %lx\n", &Module->LdrEntry->BaseDllName, Status);
        }

        Group = (PCSRT_RESOURCE_GROUP_HEADER)((PUCHAR)Group + Group->Length);
    }
}

/* starts the extensions the loader brought, on the boot cpu in phase 0
 * after the acpi tables are known and before the built-in providers are picked */
VOID
NTAPI
HalpExtInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension = LoaderBlock->Extension;
    PDESCRIPTION_HEADER Csrt;
    PLIST_ENTRY Entry;
    PHAL_EXTENSION_MODULE_ENTRY Module;
    PHALEXT_INIT ExtensionInit;
    NTSTATUS Status;
    ULONG Handle;

    /* an older loader does not have the list */
    if (!Extension ||
        (Extension->Size < RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, HalExtensionModuleList)) ||
        !Extension->HalExtensionModuleList.Flink ||
        IsListEmpty(&Extension->HalExtensionModuleList))
    {
        return;
    }

    Csrt = HalAcpiGetTable(LoaderBlock, CSRT_SIGNATURE);
    if (!Csrt)
        return;

    HalpExtRegistrationOpen = TRUE;

    for (Entry = Extension->HalExtensionModuleList.Flink;
         Entry != &Extension->HalExtensionModuleList;
         Entry = Entry->Flink)
    {
        Module = CONTAINING_RECORD(Entry, HAL_EXTENSION_MODULE_ENTRY, ListEntry);
        if (HalpExtCount >= HALEXT_MAX_EXTENSIONS)
            break;

        Handle = HalpExtCount;
        ExtensionInit = (PHALEXT_INIT)Module->LdrEntry->EntryPoint;
        Status = ExtensionInit(OsExtensionHalExtension, &HalpExtExports[Handle], &HalpExtImports);
        if (!NT_SUCCESS(Status) || !HalpExtExports[Handle] || !HalpExtExports[Handle]->AddResourceGroup)
        {
            DPRINT1("HALEXT: %wZ failed to start, %lx\n", &Module->LdrEntry->BaseDllName, Status);
            continue;
        }

        HalpExtCount++;
        HalpExtAddResourceGroups(Csrt, Module, Handle);
    }

    HalpExtRegistrationOpen = FALSE;
}
