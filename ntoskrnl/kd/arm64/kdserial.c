/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PL011 and 16550 serial ports for the kernel debugger on ARM64
 * COPYRIGHT:   Copyright 2026 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <drivers/acpi/acpi.h>
#include <fdtarc.h>
#define NDEBUG
#include <debug.h>

/* PL011 registers */
#define UART_DR             0x00
#define UART_FR             0x18
#define UART_CR             0x30

#define UART_FR_RXFE        0x10
#define UART_FR_TXFF        0x20

#define UART_CR_UARTEN      0x001
#define UART_CR_TXE         0x100
#define UART_CR_RXE         0x200

/* 16550 registers in register units, the stride comes from the firmware */
#define NS16550_RBR         0
#define NS16550_THR         0
#define NS16550_LSR         5

#define NS16550_LSR_DR      0x01
#define NS16550_LSR_THRE    0x20

/* GLOBALS *******************************************************************/

/* One debug port, its kind and register layout */
typedef enum _KDP_UART_KIND
{
    KdpUartPl011,
    KdpUartSbsa,
    KdpUart16550
} KDP_UART_KIND;

static KDP_UART_KIND KdpUartKind = KdpUartPl011;
static ULONG KdpUartShift;
static BOOLEAN KdpUartWide;

/* FUNCTIONS *****************************************************************/

/**
 * @brief Finds the PL011 or SBSA UART the firmware lists in DBG2.
 *
 * @param[out] HasControl
 * FALSE for the SBSA subset, it has no control register.
 */
static
BOOLEAN
KdpFindDbg2Uart(
    _Out_ PULONG64 PhysicalAddress,
    _Out_ PBOOLEAN HasControl)
{
    PDBG2_TABLE Table;
    PDBG2_DEVICE_INFO Entry;
    PGEN_ADDR BaseRegister;
    ULONG Offset, Remaining;
    BOOLEAN Found = FALSE;

    Table = KdGetAcpiTablePhase0(KeLoaderBlock, DBG2_SIGNATURE);
    if (!Table)
        return FALSE;

    Offset = Table->DeviceInfoOffset;
    for (Remaining = Table->DeviceInfoCount; Remaining != 0; Remaining--)
    {
        if ((Table->Header.Length < sizeof(*Table)) ||
            (Offset > Table->Header.Length - sizeof(*Entry)))
        {
            break;
        }

        Entry = (PDBG2_DEVICE_INFO)((PUCHAR)Table + Offset);
        if ((Entry->Length < sizeof(*Entry)) ||
            (Entry->Length > Table->Header.Length - Offset))
        {
            break;
        }
        Offset += Entry->Length;

        if ((Entry->PortType != DBG2_PORT_TYPE_SERIAL) ||
            (Entry->RegisterCount == 0) ||
            (Entry->BaseAddressOffset > Entry->Length - sizeof(*BaseRegister)))
        {
            continue;
        }

        if ((Entry->PortSubtype != DBG2_SERIAL_PL011) &&
            (Entry->PortSubtype != DBG2_SERIAL_SBSA_32BIT) &&
            (Entry->PortSubtype != DBG2_SERIAL_SBSA))
        {
            continue;
        }

        /* Only memory mapped ports */
        BaseRegister = (PGEN_ADDR)((PUCHAR)Entry + Entry->BaseAddressOffset);
        if ((BaseRegister->AddressSpaceID != 0) || (BaseRegister->Address.QuadPart == 0))
            continue;

        *PhysicalAddress = BaseRegister->Address.QuadPart;
        *HasControl = (Entry->PortSubtype == DBG2_SERIAL_PL011);
        Found = TRUE;
        break;
    }

    KdUnmapVirtualAddress(Table, BYTES_TO_PAGES(Table->Header.Length));
    return Found;
}

/**
 * @brief Finds the UART the device tree names in stdout-path.
 *
 * Boards without ACPI have no DBG2, the console the firmware used is the
 * debug port then.
 */
static
BOOLEAN
KdpFindFdtUart(
    _Out_ PULONG64 PhysicalAddress)
{
    PHYSICAL_ADDRESS Address;
    ULONG Size, Pages;
    const VOID *Value;
    PVOID Fdt;
    LONG Node;
    BOOLEAN Found = FALSE;

    if (!FdtFindLoaderBlob(KeLoaderBlock, &Address, &Size))
        return FALSE;

    Pages = BYTES_TO_PAGES(Size);
    Fdt = KdMapPhysicalMemory64(Address, Pages);
    if (!Fdt)
        return FALSE;

    if (FdtCheckHeader(Fdt) && (FdtTotalSize(Fdt) <= Size))
    {
        Node = FdtStdoutNode(Fdt);
        if ((Node >= 0) && FdtGetReg(Fdt, Node, 0, PhysicalAddress, NULL))
        {
            Found = TRUE;
            if (FdtIsCompatible(Fdt, Node, "arm,pl011"))
            {
                KdpUartKind = KdpUartPl011;
            }
            else if (FdtIsCompatible(Fdt, Node, "arm,sbsa-uart"))
            {
                KdpUartKind = KdpUartSbsa;
            }
            else if (FdtIsCompatible(Fdt, Node, "snps,dw-apb-uart") ||
                     FdtIsCompatible(Fdt, Node, "ns16550a") ||
                     FdtIsCompatible(Fdt, Node, "ns16550"))
            {
                /* SoC 16550s like the DesignWare one space their registers out */
                KdpUartKind = KdpUart16550;
                Value = FdtGetProperty(Fdt, Node, "reg-shift", NULL);
                KdpUartShift = Value ? FdtRead32(Value) : 0;
                Value = FdtGetProperty(Fdt, Node, "reg-io-width", NULL);
                KdpUartWide = Value && (FdtRead32(Value) == 4);
            }
            else
            {
                Found = FALSE;
            }
        }
    }

    KdUnmapVirtualAddress(Fdt, Pages);
    return Found;
}

/**
 * @brief Maps the UART at the top of the HAL range.
 *
 * The loader only maps the port in the user half, which goes away with the
 * first address space switch. The HAL hands out its mappings from the bottom
 * of the same range.
 */
static
PUCHAR
KdpMapUart(
    _In_ ULONG64 PhysicalAddress)
{
    PMMPTE PointerPte;
    MMPTE TempPte;

    for (PointerPte = MiAddressToPte(MM_HAL_VA_END);
         PointerPte >= MiAddressToPte(MM_HAL_VA_START);
         PointerPte--)
    {
        if (PointerPte->u.Long == 0)
            break;
    }

    if (PointerPte < MiAddressToPte(MM_HAL_VA_START))
        return NULL;

#ifdef _M_ARM
    /* armv7 small page: XN, shareable device (B), kernel read/write (AP0) */
    TempPte.u.Long = ((ULONG)PhysicalAddress & ~(PAGE_SIZE - 1)) | 0x1 | 0x2 | 0x4 | 0x10;
    *PointerPte = TempPte;

    __dsb(_ARM_BARRIER_ISHST);
    __isb(_ARM_BARRIER_SY);
#else
    TempPte.u.Long = 0;
    TempPte.u.Hard.Valid = 1;
    TempPte.u.Hard.NotLargePage = 1;
    TempPte.u.Hard.Accessed = 1;
    TempPte.u.Hard.CacheType = MI_CACHE_TYPE_DEVICE;
    TempPte.u.Hard.PrivilegedNoExecute = 1;
    TempPte.u.Hard.UserNoExecute = 1;
    TempPte.u.Hard.Writable = 1;
    TempPte.u.Hard.PageFrameNumber = PhysicalAddress >> PAGE_SHIFT;
    *PointerPte = TempPte;

    __dsb(_ARM64_BARRIER_ISHST);
    __isb(_ARM64_BARRIER_SY);
#endif

    return (PUCHAR)MiPteToAddress(PointerPte) + BYTE_OFFSET(PhysicalAddress);
}

BOOLEAN
NTAPI
KdPortInitializeEx(
    _In_ PCPPORT PortInformation,
    _In_ ULONG ComPortNumber)
{
    ULONG64 PhysicalAddress;
    BOOLEAN HasControl = TRUE;
    PUCHAR Base;

    UNREFERENCED_PARAMETER(ComPortNumber);

    /* There is no fixed port on ARM64, without one from DBG2 or the device tree nothing gets touched */
    if (PortInformation->Address)
        PhysicalAddress = (ULONG_PTR)PortInformation->Address;
    else if (KdpFindDbg2Uart(&PhysicalAddress, &HasControl))
        KdpUartKind = HasControl ? KdpUartPl011 : KdpUartSbsa;
    else if (!KdpFindFdtUart(&PhysicalAddress))
        return FALSE;

    Base = KdpMapUart(PhysicalAddress);
    if (!Base)
        return FALSE;

    /* The firmware already set the line up, only make sure a PL011 runs */
    if (KdpUartKind == KdpUartPl011)
    {
        WRITE_REGISTER_ULONG((PULONG)(Base + UART_CR),
                             READ_REGISTER_ULONG((PULONG)(Base + UART_CR)) |
                             UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
    }

    PortInformation->Address = Base;
    return TRUE;
}

static
UCHAR
Kdp16550Read(
    _In_ PUCHAR Base,
    _In_ ULONG Register)
{
    PUCHAR Address = Base + (Register << KdpUartShift);

    return KdpUartWide ? (UCHAR)READ_REGISTER_ULONG((PULONG)Address) : READ_REGISTER_UCHAR(Address);
}

static
VOID
Kdp16550Write(
    _In_ PUCHAR Base,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    PUCHAR Address = Base + (Register << KdpUartShift);

    if (KdpUartWide)
        WRITE_REGISTER_ULONG((PULONG)Address, Value);
    else
        WRITE_REGISTER_UCHAR(Address, Value);
}

BOOLEAN
NTAPI
KdPortGetByteEx(
    _In_ PCPPORT PortInformation,
    _Out_ PUCHAR ByteReceived)
{
    PUCHAR Base = PortInformation->Address;

    if (KdpUartKind == KdpUart16550)
    {
        if (!(Kdp16550Read(Base, NS16550_LSR) & NS16550_LSR_DR))
            return FALSE;
        *ByteReceived = Kdp16550Read(Base, NS16550_RBR);
        return TRUE;
    }

    if (READ_REGISTER_ULONG((PULONG)(Base + UART_FR)) & UART_FR_RXFE)
        return FALSE;

    *ByteReceived = (UCHAR)READ_REGISTER_ULONG((PULONG)(Base + UART_DR));
    return TRUE;
}

VOID
NTAPI
KdPortPutByteEx(
    _In_ PCPPORT PortInformation,
    _In_ UCHAR ByteToSend)
{
    PUCHAR Base = PortInformation->Address;

    if (KdpUartKind == KdpUart16550)
    {
        while (!(Kdp16550Read(Base, NS16550_LSR) & NS16550_LSR_THRE))
            YieldProcessor();
        Kdp16550Write(Base, NS16550_THR, ByteToSend);
        return;
    }

    while (READ_REGISTER_ULONG((PULONG)(Base + UART_FR)) & UART_FR_TXFF)
        YieldProcessor();

    WRITE_REGISTER_ULONG((PULONG)(Base + UART_DR), ByteToSend);
}
