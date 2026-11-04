/*
 * PROJECT:     ReactOS Flattened Device Tree library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Read only access to a flattened device tree blob
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

#include <ntdef.h>

/* Header magic, every field in the blob is big endian */
#define FDT_MAGIC               0xD00DFEED

/* Structure block tokens */
#define FDT_BEGIN_NODE          1
#define FDT_END_NODE            2
#define FDT_PROP                3
#define FDT_NOP                 4
#define FDT_END                 9

/* Node offsets are byte offsets into the structure block, negative means none */
#define FDT_NO_NODE             (-1)

/* Interrupt specifier types of the ARM GIC binding */
#define FDT_GIC_SPI             0
#define FDT_GIC_PPI             1

typedef struct _FDT_HEADER
{
    ULONG Magic;
    ULONG TotalSize;
    ULONG OffsetStructure;
    ULONG OffsetStrings;
    ULONG OffsetReserveMap;
    ULONG Version;
    ULONG LastCompatibleVersion;
    ULONG BootCpuId;
    ULONG SizeStrings;
    ULONG SizeStructure;
} FDT_HEADER, *PFDT_HEADER;

/* The loader hands the blob over in an ARC "Device Tree" adapter node */
typedef struct _FDT_BIOS_DATA
{
    PHYSICAL_ADDRESS Address;
    ULONG Size;
} FDT_BIOS_DATA, *PFDT_BIOS_DATA;

#define FDT_ADAPTER_IDENTIFIER  "Device Tree"

ULONG
FdtRead32(
    _In_ const VOID *Cell);

ULONG64
FdtReadCells(
    _In_ const VOID *Cells,
    _In_ ULONG Count);

BOOLEAN
FdtCheckHeader(
    _In_ const VOID *Fdt);

ULONG
FdtTotalSize(
    _In_ const VOID *Fdt);

ULONG
FdtUsedSize(
    _In_ const VOID *Fdt);

VOID
FdtSetTotalSize(
    _Inout_ VOID *Fdt,
    _In_ ULONG Size);

LONG
FdtNextNode(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _Inout_opt_ PLONG Depth);

PCSTR
FdtNodeName(
    _In_ const VOID *Fdt,
    _In_ LONG Node);

const VOID *
FdtGetProperty(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ PCSTR Name,
    _Out_opt_ PULONG Length);

LONG
FdtParentNode(
    _In_ const VOID *Fdt,
    _In_ LONG Node);

LONG
FdtPathOffset(
    _In_ const VOID *Fdt,
    _In_ PCSTR Path);

LONG
FdtFindCompatible(
    _In_ const VOID *Fdt,
    _In_ LONG After,
    _In_ PCSTR Compatible);

BOOLEAN
FdtIsCompatible(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ PCSTR Compatible);

BOOLEAN
FdtIsEnabled(
    _In_ const VOID *Fdt,
    _In_ LONG Node);

BOOLEAN
FdtGetReg(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ ULONG Index,
    _Out_ PULONG64 Address,
    _Out_opt_ PULONG64 Size);

BOOLEAN
FdtGetGicInterrupt(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ ULONG Index,
    _Out_ PULONG IntId);

LONG
FdtStdoutNode(
    _In_ const VOID *Fdt);

BOOLEAN
FdtPciInterruptMap(
    _In_ const VOID *Fdt,
    _In_ LONG HostBridge,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Pin,
    _Out_ PULONG IntId);

LONG
FdtNodeByPhandle(
    _In_ const VOID *Fdt,
    _In_ ULONG Phandle);
