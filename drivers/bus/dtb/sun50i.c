/*
 * PROJECT:     ReactOS Device Tree Bus Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Allwinner H616/H618 USB host clocks, resets, PHY and VBUS
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * U-Boot tears USB down when the OS takes over, so the host controllers come
 * with their bus clocks gated, held in reset and the PHY parked. This brings
 * a port up again the way PureDarwin's PDSun50iUSB does. The register blocks
 * and the VBUS switch come from the device tree, only the bit layout of the
 * clock controller and the PHY "pmu" blocks is H616 specific.
 */

/* INCLUDES ******************************************************************/

#include "dtb.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* CCU, one clock register per port and the bus gate and reset register they share */
#define CCU_USB_CLK(Port)       (0xA70 + (Port) * 4)
#define CCU_USB_OHCI_GATE       (1UL << 31)
#define CCU_USB_PHY_RESET       (1UL << 30)
#define CCU_USB_PHY_GATE        (1UL << 29)
#define CCU_USB_BGR             0xA8C
#define CCU_BGR_OHCI(Port)      (1UL << (Port))
#define CCU_BGR_EHCI(Port)      (1UL << (4 + (Port)))
#define CCU_BGR_OHCI_RST(Port)  (1UL << (16 + (Port)))
#define CCU_BGR_EHCI_RST(Port)  (1UL << (20 + (Port)))

/* Per port PHY block right after each host controller pair */
#define PMU_HCI_ICR             0x00
#define PMU_PASSBY              ((1UL << 10) | (1UL << 9) | (1UL << 8) | (1UL << 0))
#define PMU_PHY_CTL             0x10
#define PMU_PHY_CTL_SIDDQ       (1UL << 3)

/* Pin controller, each bank takes 0x24 bytes with four config registers */
#define PIO_BANK_SIZE           0x24
#define PIO_CFG(Pin)            (((Pin) / 8) * 4)
#define PIO_DATA                0x10
#define PIO_FUNCTION_OUTPUT     1

/* H616 PHYs only leave SIDDQ while PHY2 runs */
#define SUN50I_CLOCK_PHY        2

#define SUN50I_MAX_PORTS        4

/* The PHY blocks are 0x14 bytes, one mapping size for both directions */
#define SUN50I_PMU_MAP_SIZE     0x20

static ULONG DtbSun50iPortsUp;

/* FUNCTIONS *****************************************************************/

static
PUCHAR
DtbSun50iMap(
    _In_ ULONG64 Address,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Physical;

    Physical.QuadPart = Address;
    return MmMapIoSpace(Physical, Size, MmNonCached);
}

static
VOID
DtbSun50iSet(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Bits)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + Offset), READ_REGISTER_ULONG((PULONG)(Base + Offset)) | Bits);
}

static
VOID
DtbSun50iClear(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Bits)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + Offset), READ_REGISTER_ULONG((PULONG)(Base + Offset)) & ~Bits);
}

/* reg-names gives each reg entry a name, the PHY lists its pmuN blocks that way */
static
BOOLEAN
DtbSun50iRegByName(
    _In_ LONG Node,
    _In_ PCSTR Name,
    _Out_ PULONG64 Address,
    _Out_ PULONG64 Size)
{
    PCSTR Names, End;
    ULONG Length, Index = 0;

    Names = FdtGetProperty(DtbBlob, Node, "reg-names", &Length);
    if (!Names)
        return FALSE;

    for (End = Names + Length; Names < End; Names += strlen(Names) + 1, Index++)
    {
        if (!strcmp(Names, Name))
            return FdtGetReg(DtbBlob, Node, Index, Address, Size);
    }

    return FALSE;
}

static
PUCHAR
DtbSun50iMapPmu(
    _In_ LONG Phy,
    _In_ ULONG Port)
{
    CHAR Name[8];
    ULONG64 Address, Size;

    RtlStringCbPrintfA(Name, sizeof(Name), "pmu%lu", Port);
    if (!DtbSun50iRegByName(Phy, Name, &Address, &Size) || (Size > SUN50I_PMU_MAP_SIZE))
        return NULL;

    return DtbSun50iMap(Address, SUN50I_PMU_MAP_SIZE);
}

/**
 * @brief Switches the port's 5 V on through the PHY's usbN_vbus-supply regulator.
 *
 * Only a regulator-fixed on a pin of this pin controller is understood.
 */
static
VOID
DtbSun50iVbusOn(
    _In_ LONG Phy,
    _In_ ULONG Port)
{
    CHAR Name[24];
    const UCHAR *Supply, *Gpio;
    ULONG Length, Bank, Pin, Config;
    ULONG64 PioBase;
    LONG Regulator, Pio;
    BOOLEAN ActiveHigh;
    PUCHAR Bank0;

    RtlStringCbPrintfA(Name, sizeof(Name), "usb%lu_vbus-supply", Port);
    Supply = FdtGetProperty(DtbBlob, Phy, Name, &Length);
    if (!Supply || (Length < 4))
        return;

    Regulator = FdtNodeByPhandle(DtbBlob, FdtRead32(Supply));
    if ((Regulator < 0) || !FdtIsCompatible(DtbBlob, Regulator, "regulator-fixed"))
        return;

    /* gpio is <&pio bank pin flags> */
    Gpio = FdtGetProperty(DtbBlob, Regulator, "gpio", &Length);
    if (!Gpio)
        Gpio = FdtGetProperty(DtbBlob, Regulator, "gpios", &Length);
    if (!Gpio || (Length < 16))
        return;

    Pio = FdtNodeByPhandle(DtbBlob, FdtRead32(Gpio));
    if ((Pio < 0) || !FdtGetReg(DtbBlob, Pio, 0, &PioBase, NULL))
        return;

    Bank = FdtRead32(Gpio + 4);
    Pin = FdtRead32(Gpio + 8);
    ActiveHigh = (FdtGetProperty(DtbBlob, Regulator, "enable-active-high", NULL) != NULL);

    Bank0 = DtbSun50iMap(PioBase + Bank * PIO_BANK_SIZE, PIO_BANK_SIZE);
    if (!Bank0)
        return;

    Config = READ_REGISTER_ULONG((PULONG)(Bank0 + PIO_CFG(Pin)));
    Config &= ~(0xFUL << ((Pin % 8) * 4));
    Config |= PIO_FUNCTION_OUTPUT << ((Pin % 8) * 4);
    WRITE_REGISTER_ULONG((PULONG)(Bank0 + PIO_CFG(Pin)), Config);

    if (ActiveHigh)
        DtbSun50iSet(Bank0, PIO_DATA, 1UL << Pin);
    else
        DtbSun50iClear(Bank0, PIO_DATA, 1UL << Pin);

    DPRINT1("DTB: sun50i usb%lu VBUS on P%c%lu\n", Port, 'A' + Bank, Pin);
    MmUnmapIoSpace(Bank0, PIO_BANK_SIZE);
}

/**
 * @brief Brings the USB port of an H616 EHCI or OHCI node up, once per port.
 *
 * Bus clocks first, then resets, then the PHY and finally VBUS.
 */
VOID
DtbSun50iUsbPowerOn(
    _In_ LONG HostNode)
{
    const UCHAR *Phys;
    ULONG Length, Port;
    ULONG64 CcuBase;
    LARGE_INTEGER Delay;
    PUCHAR Ccu, Pmu, ClockPmu;
    LONG Phy, CcuNode;

    if (!FdtIsCompatible(DtbBlob, HostNode, "allwinner,sun50i-h616-ehci") &&
        !FdtIsCompatible(DtbBlob, HostNode, "allwinner,sun50i-h616-ohci"))
    {
        return;
    }

    /* phys = <&usbphy port> */
    Phys = FdtGetProperty(DtbBlob, HostNode, "phys", &Length);
    if (!Phys || (Length < 8))
        return;
    Phy = FdtNodeByPhandle(DtbBlob, FdtRead32(Phys));
    Port = FdtRead32(Phys + 4);
    if ((Phy < 0) || (Port >= SUN50I_MAX_PORTS) || (DtbSun50iPortsUp & (1UL << Port)))
        return;

    CcuNode = FdtFindCompatible(DtbBlob, FDT_NO_NODE, "allwinner,sun50i-h616-ccu");
    if ((CcuNode < 0) || !FdtGetReg(DtbBlob, CcuNode, 0, &CcuBase, NULL))
        return;

    Ccu = DtbSun50iMap(CcuBase, PAGE_SIZE);
    Pmu = DtbSun50iMapPmu(Phy, Port);
    ClockPmu = (Port == SUN50I_CLOCK_PHY) ? Pmu : DtbSun50iMapPmu(Phy, SUN50I_CLOCK_PHY);
    if (!Ccu || !Pmu || !ClockPmu)
    {
        DPRINT1("DTB: sun50i usb%lu registers not mapped\n", Port);
        goto Exit;
    }

    DtbSun50iSet(Ccu, CCU_USB_BGR, CCU_BGR_OHCI(Port) | CCU_BGR_EHCI(Port));
    DtbSun50iSet(Ccu, CCU_USB_CLK(Port), CCU_USB_OHCI_GATE);
    DtbSun50iSet(Ccu, CCU_USB_BGR, CCU_BGR_OHCI_RST(Port) | CCU_BGR_EHCI_RST(Port));
    DtbSun50iSet(Ccu, CCU_USB_CLK(Port), CCU_USB_PHY_GATE | CCU_USB_PHY_RESET);

    /* PHY2's PMU only answers with the EHCI2 bus clock on */
    DtbSun50iSet(Ccu, CCU_USB_CLK(SUN50I_CLOCK_PHY), CCU_USB_PHY_GATE | CCU_USB_PHY_RESET);
    DtbSun50iSet(Ccu, CCU_USB_BGR, CCU_BGR_EHCI(SUN50I_CLOCK_PHY));
    DtbSun50iClear(ClockPmu, PMU_PHY_CTL, PMU_PHY_CTL_SIDDQ);

    DtbSun50iClear(Pmu, PMU_PHY_CTL, PMU_PHY_CTL_SIDDQ);
    DtbSun50iSet(Pmu, PMU_HCI_ICR, PMU_PASSBY);

    DtbSun50iVbusOn(Phy, Port);

    /* Let VBUS settle before a controller looks at its ports */
    Delay.QuadPart = -50 * 10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    DtbSun50iPortsUp |= 1UL << Port;
    DPRINT1("DTB: sun50i usb%lu clocks, resets and PHY on\n", Port);

Exit:
    if (ClockPmu && (ClockPmu != Pmu))
        MmUnmapIoSpace(ClockPmu, SUN50I_PMU_MAP_SIZE);
    if (Pmu)
        MmUnmapIoSpace(Pmu, SUN50I_PMU_MAP_SIZE);
    if (Ccu)
        MmUnmapIoSpace(Ccu, PAGE_SIZE);
}
