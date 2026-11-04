/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Platform discovery from a flattened device tree
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <fdtarc.h>
#define NDEBUG
#include <debug.h>

/* Interrupt controller bindings, the GICv2 ones list the CPU interface second */
static PCSTR HalpGicV3Compatible[] = { "arm,gic-v3" };
static PCSTR HalpGicV2Compatible[] = { "arm,gic-400", "arm,cortex-a15-gic", "arm,cortex-a9-gic", "arm,cortex-a7-gic" };

/* The armv8 timer binding lists secure, non-secure, virtual and hypervisor interrupts */
#define HALP_DTB_TIMER_VIRTUAL  2

/* The platform came from the device tree, the HAL PnP device reports the DTB bus then */
BOOLEAN HalpDeviceTreePlatform;

/* FUNCTIONS *****************************************************************/

static
LONG
HalpDtbFindAny(
    _In_ const VOID *Fdt,
    _In_reads_(Count) PCSTR *Compatible,
    _In_ ULONG Count)
{
    LONG Node;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Node = FdtFindCompatible(Fdt, FDT_NO_NODE, Compatible[i]);
        if (Node >= 0)
            return Node;
    }

    return FDT_NO_NODE;
}

static
VOID
HalpDtbParseGic(
    _In_ const VOID *Fdt)
{
    ULONG64 Address, Size;
    LONG Node;

    Node = HalpDtbFindAny(Fdt, HalpGicV3Compatible, RTL_NUMBER_OF(HalpGicV3Compatible));
    if (Node >= 0)
    {
        HalpPlatform.GicVersion = 3;
        if (FdtGetReg(Fdt, Node, 0, &Address, NULL))
            HalpPlatform.GicdBase.QuadPart = Address;
        if (FdtGetReg(Fdt, Node, 1, &Address, &Size))
        {
            HalpPlatform.GicrBase.QuadPart = Address;
            HalpPlatform.GicrLength = (ULONG)Size;
        }
        return;
    }

    Node = HalpDtbFindAny(Fdt, HalpGicV2Compatible, RTL_NUMBER_OF(HalpGicV2Compatible));
    if (Node >= 0)
    {
        HalpPlatform.GicVersion = 2;
        if (FdtGetReg(Fdt, Node, 0, &Address, NULL))
            HalpPlatform.GicdBase.QuadPart = Address;
        if (FdtGetReg(Fdt, Node, 1, &Address, NULL))
            HalpPlatform.GiccBase.QuadPart = Address;
    }
}

/* Every cpu node's reg is its MPIDR affinity, cpu-map and friends have no device_type */
static
VOID
HalpDtbParseCpus(
    _In_ const VOID *Fdt)
{
    LONG Cpus, Node, Depth = 1;
    PCSTR Type;
    ULONG64 Mpidr;
    ULONG Cpu = 0;

    Cpus = FdtPathOffset(Fdt, "/cpus");
    if (Cpus < 0)
        return;

    for (Node = FdtNextNode(Fdt, Cpus, &Depth);
         (Node >= 0) && (Depth >= 2) && (Cpu < MAXIMUM_PROCESSORS);
         Node = FdtNextNode(Fdt, Node, &Depth))
    {
        if (Depth != 2)
            continue;

        Type = FdtGetProperty(Fdt, Node, "device_type", NULL);
        if (!Type || strcmp(Type, "cpu") || !FdtIsEnabled(Fdt, Node))
            continue;

        if (FdtGetReg(Fdt, Node, 0, &Mpidr, NULL))
            HalpProcessorMpidr[Cpu++] = Mpidr;
    }
}

static
VOID
HalpDtbParseTimer(
    _In_ const VOID *Fdt)
{
    LONG Node;
    ULONG IntId;

    Node = FdtFindCompatible(Fdt, FDT_NO_NODE, "arm,armv8-timer");
    if ((Node >= 0) && FdtGetGicInterrupt(Fdt, Node, HALP_DTB_TIMER_VIRTUAL, &IntId))
        HalpPlatform.TimerIntId = IntId;
}

/* PSCI through hvc maps to the same FADT boot flags the ACPI path fills in */
static
VOID
HalpDtbParsePsci(
    _In_ const VOID *Fdt)
{
    PCSTR Method;
    LONG Node;

    Node = FdtFindCompatible(Fdt, FDT_NO_NODE, "arm,psci-1.0");
    if (Node < 0)
        Node = FdtFindCompatible(Fdt, FDT_NO_NODE, "arm,psci-0.2");
    if (Node < 0)
        Node = FdtFindCompatible(Fdt, FDT_NO_NODE, "arm,psci");
    if (Node < 0)
        return;

    HalpPlatform.ArmBootFlags |= FADT_ARM_PSCI_COMPLIANT;

    Method = FdtGetProperty(Fdt, Node, "method", NULL);
    if (Method && !strcmp(Method, "hvc"))
        HalpPlatform.ArmBootFlags |= FADT_ARM_PSCI_USE_HVC;
}

static
VOID
HalpDtbParsePci(
    _In_ const VOID *Fdt)
{
    const UCHAR *BusRange;
    PHALP_ECAM_WINDOW Window;
    ULONG64 Address;
    ULONG Length;
    LONG Node;

    Node = FdtFindCompatible(Fdt, FDT_NO_NODE, "pci-host-ecam-generic");
    if ((Node < 0) || !FdtGetReg(Fdt, Node, 0, &Address, NULL))
        return;

    /* one host bridge, segment 0, reg starts at the first bus of bus-range */
    Window = &HalpPlatform.Ecam[0];
    Window->Base.QuadPart = Address;
    Window->Segment = 0;
    Window->StartBus = 0;
    Window->EndBus = 0xFF;

    BusRange = FdtGetProperty(Fdt, Node, "bus-range", &Length);
    if (BusRange && (Length >= 8))
    {
        Window->StartBus = (UCHAR)FdtRead32(BusRange);
        Window->EndBus = (UCHAR)FdtRead32(BusRange + 4);
    }
    HalpPlatform.EcamCount = 1;
}

static
UCHAR
HalpDtbConfigByte(
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Offset)
{
    PCI_SLOT_NUMBER Slot;
    UCHAR Value = 0xFF;

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = Device;
    Slot.u.bits.FunctionNumber = Function;
    HalpPciRawConfig(Bus, Slot, &Value, Offset, sizeof(Value), FALSE);
    return Value;
}

/* Finds the bridge whose secondary bus is Bus, parents always sit on lower bus numbers */
static
BOOLEAN
HalpDtbFindBridge(
    _In_ ULONG Bus,
    _Out_ PULONG ParentBus,
    _Out_ PULONG ParentDevice)
{
    ULONG Search, Device, Function;
    UCHAR HeaderType;

    for (Search = HalpPlatform.Ecam[0].StartBus; Search < Bus; Search++)
    {
        for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
        {
            for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
            {
                HeaderType = HalpDtbConfigByte(Search, Device, Function,
                                               FIELD_OFFSET(PCI_COMMON_HEADER, HeaderType));
                if (HeaderType == 0xFF)
                {
                    if (Function == 0)
                        break;
                    continue;
                }

                if (((HeaderType & ~PCI_MULTIFUNCTION) == PCI_BRIDGE_TYPE) &&
                    (HalpDtbConfigByte(Search, Device, Function,
                                       FIELD_OFFSET(PCI_COMMON_HEADER, u.type1.SecondaryBus)) == Bus))
                {
                    *ParentBus = Search;
                    *ParentDevice = Device;
                    return TRUE;
                }

                if ((Function == 0) && !(HeaderType & PCI_MULTIFUNCTION))
                    break;
            }
        }
    }

    return FALSE;
}

/**
 * @brief Works out the GIC INTID a PCI function's interrupt pin reaches.
 *
 * Behind each bridge the pin is swizzled by the device number until the root
 * bus, where the host bridge's interrupt-map takes over.
 */
static
BOOLEAN
HalpDtbRoutePciInterrupt(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Out_ PULONG IntId)
{
    ULONG Device = Slot.u.bits.DeviceNumber, Function = Slot.u.bits.FunctionNumber;
    ULONG Pin, ParentBus, ParentDevice, Depth;
    LONG HostBridge;

    HostBridge = FdtFindCompatible(HalpPlatform.DeviceTree, FDT_NO_NODE, "pci-host-ecam-generic");
    if (HostBridge < 0)
        return FALSE;

    Pin = HalpDtbConfigByte(Bus, Device, Function, FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.InterruptPin));
    if (!Pin || (Pin > 4))
        return FALSE;

    for (Depth = 0; Bus != HalpPlatform.Ecam[0].StartBus; Depth++)
    {
        if ((Depth >= 32) || !HalpDtbFindBridge(Bus, &ParentBus, &ParentDevice))
            return FALSE;

        Pin = ((Pin - 1 + Device) % 4) + 1;
        Bus = ParentBus;
        Device = ParentDevice;
        Function = 0;
    }

    return FdtPciInterruptMap(HalpPlatform.DeviceTree, HostBridge, Bus, Device, Function, Pin, IntId);
}

/**
 * @brief Narrows a PCI device's interrupt requirement to the line the device tree routes it to.
 *
 * pci.sys asks for any vector and leaves routing to the platform, without ACPI
 * the root arbiter would otherwise hand out an arbitrary legacy line.
 */
VOID
NTAPI
HalpDtbAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST List)
{
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    PIO_RESOURCE_LIST Alternative;
    PCI_SLOT_NUMBER Slot;
    ULONG i, j, IntId;

    if (!HalpDeviceTreePlatform || (List->InterfaceType != PCIBus))
        return;

    Slot.u.AsULONG = List->SlotNumber;
    if (!HalpDtbRoutePciInterrupt(List->BusNumber, Slot, &IntId))
        return;

    Alternative = List->List;
    for (i = 0; i < List->AlternativeLists; i++)
    {
        for (j = 0; j < Alternative->Count; j++)
        {
            Descriptor = &Alternative->Descriptors[j];
            if ((Descriptor->Type == CmResourceTypeInterrupt) &&
                !(Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                Descriptor->u.Interrupt.MinimumVector = IntId;
                Descriptor->u.Interrupt.MaximumVector = IntId;
            }
        }
        Alternative = (PIO_RESOURCE_LIST)&Alternative->Descriptors[Alternative->Count];
    }
}

/**
 * @brief Collects the GIC, timer, PSCI, processor and PCI details from the device tree.
 *
 * Only used when the firmware gave no ACPI tables. The blob stays mapped,
 * the HAL keeps it for later lookups.
 */
BOOLEAN
NTAPI
HalpDtbInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHYSICAL_ADDRESS Address;
    ULONG Size;
    PVOID Fdt;

    if (!FdtFindLoaderBlob(LoaderBlock, &Address, &Size))
        return FALSE;

    Fdt = HalpMapPhysicalMemory(Address, BYTES_TO_PAGES(Size), FALSE);
    if (!Fdt)
        return FALSE;

    if (!FdtCheckHeader(Fdt) || (FdtTotalSize(Fdt) > Size))
    {
        DPRINT1("The loader's device tree at %I64x is not usable\n", Address.QuadPart);
        HalpUnmapVirtualAddress(Fdt, BYTES_TO_PAGES(Size));
        return FALSE;
    }

    HalpPlatform.DeviceTree = Fdt;
    HalpDeviceTreePlatform = TRUE;

    /* SoC masters on device tree boards mostly do not snoop, Allwinner's leave dma-coherent out */
    KeSetDmaIoCoherency(KE_DMA_IO_NOT_COHERENT);

    HalpDtbParseGic(Fdt);
    HalpDtbParseCpus(Fdt);
    HalpDtbParseTimer(Fdt);
    HalpDtbParsePsci(Fdt);
    HalpDtbParsePci(Fdt);

    DPRINT1("Device tree: GICv%u GICD %I64x, timer %lu, PSCI flags %x, ECAM %I64x\n",
            HalpPlatform.GicVersion, HalpPlatform.GicdBase.QuadPart, HalpPlatform.TimerIntId,
            HalpPlatform.ArmBootFlags, HalpPlatform.Ecam[0].Base.QuadPart);
    return TRUE;
}
