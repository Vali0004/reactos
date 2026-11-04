/*
 * PROJECT:     ReactOS Flattened Device Tree library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Read only access to a flattened device tree blob
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <fdt.h>
#include <string.h>

/* Deepest nesting FdtParentNode follows, real trees stay far below it */
#define FDT_MAX_DEPTH           32

/* FUNCTIONS *****************************************************************/

ULONG
FdtRead32(
    _In_ const VOID *Cell)
{
    const UCHAR *Bytes = Cell;

    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) |
           ((ULONG)Bytes[2] << 8) | (ULONG)Bytes[3];
}

/* One or two cells make one number, more do not fit and are cut to the low two */
ULONG64
FdtReadCells(
    _In_ const VOID *Cells,
    _In_ ULONG Count)
{
    const UCHAR *Bytes = Cells;
    ULONG64 Value = 0;
    ULONG i;

    for (i = 0; i < Count; i++)
        Value = (Value << 32) | FdtRead32(Bytes + i * 4);

    return Value;
}

static
const UCHAR *
FdtStructure(
    _In_ const VOID *Fdt)
{
    return (const UCHAR *)Fdt + FdtRead32(&((const FDT_HEADER *)Fdt)->OffsetStructure);
}

static
PCSTR
FdtString(
    _In_ const VOID *Fdt,
    _In_ ULONG Offset)
{
    return (PCSTR)Fdt + FdtRead32(&((const FDT_HEADER *)Fdt)->OffsetStrings) + Offset;
}

static
ULONG
FdtStructureSize(
    _In_ const VOID *Fdt)
{
    return FdtRead32(&((const FDT_HEADER *)Fdt)->SizeStructure);
}

static
ULONG
FdtAlign4(
    _In_ ULONG Offset)
{
    return (Offset + 3) & ~3UL;
}

BOOLEAN
FdtCheckHeader(
    _In_ const VOID *Fdt)
{
    const FDT_HEADER *Header = Fdt;
    ULONG TotalSize;

    if (!Fdt || (FdtRead32(&Header->Magic) != FDT_MAGIC))
        return FALSE;

    /* Version 17 is the first with the structure block size */
    if (FdtRead32(&Header->LastCompatibleVersion) > 17 || FdtRead32(&Header->Version) < 17)
        return FALSE;

    TotalSize = FdtRead32(&Header->TotalSize);
    if ((FdtRead32(&Header->OffsetStructure) + FdtStructureSize(Fdt) > TotalSize) ||
        (FdtRead32(&Header->OffsetStrings) + FdtRead32(&Header->SizeStrings) > TotalSize))
    {
        return FALSE;
    }

    return TRUE;
}

ULONG
FdtTotalSize(
    _In_ const VOID *Fdt)
{
    return FdtRead32(&((const FDT_HEADER *)Fdt)->TotalSize);
}

/**
 * @brief Returns how much of the blob the blocks take up.
 *
 * Firmware often pads the blob, copying only this much keeps every block.
 */
ULONG
FdtUsedSize(
    _In_ const VOID *Fdt)
{
    const FDT_HEADER *Header = Fdt;
    const UCHAR *Reserve;
    ULONG End, Offset, TotalSize;

    TotalSize = FdtRead32(&Header->TotalSize);
    End = FdtRead32(&Header->OffsetStructure) + FdtStructureSize(Fdt);
    if (FdtRead32(&Header->OffsetStrings) + FdtRead32(&Header->SizeStrings) > End)
        End = FdtRead32(&Header->OffsetStrings) + FdtRead32(&Header->SizeStrings);

    /* The reserve map has no size, it ends with an all zero entry */
    Offset = FdtRead32(&Header->OffsetReserveMap);
    Reserve = (const UCHAR *)Fdt;
    while (Offset + 16 <= TotalSize)
    {
        Offset += 16;
        if (!FdtReadCells(Reserve + Offset - 16, 2) && !FdtReadCells(Reserve + Offset - 8, 2))
            break;
    }
    if (Offset > End)
        End = Offset;

    return (End < TotalSize) ? End : TotalSize;
}

VOID
FdtSetTotalSize(
    _Inout_ VOID *Fdt,
    _In_ ULONG Size)
{
    PUCHAR Bytes = (PUCHAR)&((PFDT_HEADER)Fdt)->TotalSize;

    Bytes[0] = (UCHAR)(Size >> 24);
    Bytes[1] = (UCHAR)(Size >> 16);
    Bytes[2] = (UCHAR)(Size >> 8);
    Bytes[3] = (UCHAR)Size;
}

/* Returns the offset of the token after the one at Offset, or 0 at the end */
static
ULONG
FdtNextToken(
    _In_ const VOID *Fdt,
    _In_ ULONG Offset,
    _Out_ PULONG Token)
{
    const UCHAR *Structure = FdtStructure(Fdt);
    ULONG Size = FdtStructureSize(Fdt);
    ULONG Length;

    if (Offset + 4 > Size)
    {
        *Token = FDT_END;
        return 0;
    }

    *Token = FdtRead32(Structure + Offset);
    Offset += 4;

    switch (*Token)
    {
        case FDT_BEGIN_NODE:
            /* The name follows, NUL terminated and padded */
            while ((Offset < Size) && Structure[Offset])
                Offset++;
            return FdtAlign4(Offset + 1);

        case FDT_PROP:
            if (Offset + 8 > Size)
                return 0;
            Length = FdtRead32(Structure + Offset);
            return FdtAlign4(Offset + 8 + Length);

        case FDT_END_NODE:
        case FDT_NOP:
            return Offset;

        default:
            *Token = FDT_END;
            return 0;
    }
}

/**
 * @brief Walks the tree in document order.
 *
 * Pass FDT_NO_NODE to start at the root. Depth, when given, follows the
 * nesting so callers can tell children from siblings.
 */
LONG
FdtNextNode(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _Inout_opt_ PLONG Depth)
{
    ULONG Offset, Next, Token;

    if (Node < 0)
    {
        if (Depth)
            *Depth = 0;
        return 0;
    }

    /* Step over the node's own BEGIN_NODE first */
    Offset = FdtNextToken(Fdt, (ULONG)Node, &Token);

    while (Offset)
    {
        Next = FdtNextToken(Fdt, Offset, &Token);
        switch (Token)
        {
            case FDT_BEGIN_NODE:
                if (Depth)
                    (*Depth)++;
                return (LONG)Offset;

            case FDT_END_NODE:
                if (Depth)
                    (*Depth)--;
                break;

            case FDT_END:
                return FDT_NO_NODE;
        }
        Offset = Next;
    }

    return FDT_NO_NODE;
}

PCSTR
FdtNodeName(
    _In_ const VOID *Fdt,
    _In_ LONG Node)
{
    return (PCSTR)FdtStructure(Fdt) + Node + 4;
}

const VOID *
FdtGetProperty(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ PCSTR Name,
    _Out_opt_ PULONG Length)
{
    const UCHAR *Structure = FdtStructure(Fdt);
    ULONG Offset, Token;

    if (Node < 0)
        return NULL;

    /* Properties come right after the name, before any child node */
    Offset = FdtNextToken(Fdt, (ULONG)Node, &Token);
    while (Offset)
    {
        ULONG Next = FdtNextToken(Fdt, Offset, &Token);

        if (Token == FDT_PROP)
        {
            if (!strcmp(FdtString(Fdt, FdtRead32(Structure + Offset + 8)), Name))
            {
                if (Length)
                    *Length = FdtRead32(Structure + Offset + 4);
                return Structure + Offset + 12;
            }
        }
        else if (Token != FDT_NOP)
        {
            break;
        }

        Offset = Next;
    }

    return NULL;
}

LONG
FdtParentNode(
    _In_ const VOID *Fdt,
    _In_ LONG Node)
{
    LONG Stack[FDT_MAX_DEPTH];
    LONG Current, Depth = 0;

    for (Current = FdtNextNode(Fdt, FDT_NO_NODE, &Depth);
         Current >= 0;
         Current = FdtNextNode(Fdt, Current, &Depth))
    {
        if ((Depth < 0) || (Depth >= FDT_MAX_DEPTH))
            return FDT_NO_NODE;

        Stack[Depth] = Current;
        if (Current == Node)
            return (Depth > 0) ? Stack[Depth - 1] : FDT_NO_NODE;
    }

    return FDT_NO_NODE;
}

/* A path component without a unit address matches "name@unit" too */
static
BOOLEAN
FdtNameMatches(
    _In_ PCSTR NodeName,
    _In_ PCSTR Component,
    _In_ SIZE_T Length)
{
    if (strncmp(NodeName, Component, Length))
        return FALSE;

    return (NodeName[Length] == '\0') ||
           ((NodeName[Length] == '@') && !memchr(Component, '@', Length));
}

LONG
FdtPathOffset(
    _In_ const VOID *Fdt,
    _In_ PCSTR Path)
{
    LONG Node = 0, Child, Depth, Wanted;
    PCSTR End;
    SIZE_T Length;

    if (*Path != '/')
        return FDT_NO_NODE;

    for (Wanted = 1; *Path; Wanted++)
    {
        while (*Path == '/')
            Path++;
        if (!*Path)
            break;

        End = strchr(Path, '/');
        Length = End ? (SIZE_T)(End - Path) : strlen(Path);

        /* Look through the direct children of the current node */
        Depth = Wanted - 1;
        for (Child = FdtNextNode(Fdt, Node, &Depth);
             Child >= 0 && Depth >= Wanted;
             Child = FdtNextNode(Fdt, Child, &Depth))
        {
            if ((Depth == Wanted) && FdtNameMatches(FdtNodeName(Fdt, Child), Path, Length))
                break;
        }

        if ((Child < 0) || (Depth < Wanted))
            return FDT_NO_NODE;

        Node = Child;
        Path += Length;
    }

    return Node;
}

/* compatible is a list of NUL terminated strings, any of them may match */
BOOLEAN
FdtIsCompatible(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ PCSTR Compatible)
{
    PCSTR List, End;
    ULONG Length;

    List = FdtGetProperty(Fdt, Node, "compatible", &Length);
    if (!List)
        return FALSE;

    for (End = List + Length; List < End; List += strlen(List) + 1)
    {
        if (!strcmp(List, Compatible))
            return TRUE;
    }

    return FALSE;
}

LONG
FdtFindCompatible(
    _In_ const VOID *Fdt,
    _In_ LONG After,
    _In_ PCSTR Compatible)
{
    LONG Node;

    for (Node = FdtNextNode(Fdt, After, NULL); Node >= 0; Node = FdtNextNode(Fdt, Node, NULL))
    {
        if (FdtIsCompatible(Fdt, Node, Compatible) && FdtIsEnabled(Fdt, Node))
            return Node;
    }

    return FDT_NO_NODE;
}

BOOLEAN
FdtIsEnabled(
    _In_ const VOID *Fdt,
    _In_ LONG Node)
{
    PCSTR Status = FdtGetProperty(Fdt, Node, "status", NULL);

    return !Status || !strcmp(Status, "okay") || !strcmp(Status, "ok");
}

static
ULONG
FdtCellCount(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ PCSTR Name,
    _In_ ULONG Default)
{
    const VOID *Cells = FdtGetProperty(Fdt, Node, Name, NULL);

    return Cells ? FdtRead32(Cells) : Default;
}

/* Bus levels a reg address is translated through, real trees nest a handful */
#define FDT_MAX_TRANSLATION_DEPTH   16

/**
 * @brief Maps an address on Bus to its parent through Bus's ranges.
 *
 * An empty ranges is an identity mapping. A missing one means the bus is not
 * memory mapped, like /cpus, and the walk stops there with the address kept.
 *
 * @return
 * TRUE while the address made it through, Done is set when the walk is over.
 */
static
BOOLEAN
FdtTranslateOneLevel(
    _In_ const VOID *Fdt,
    _In_ LONG Bus,
    _Inout_ PULONG64 Address,
    _Out_ PBOOLEAN Done)
{
    const UCHAR *Ranges, *Entry;
    ULONG Length, ChildCells, ParentCells, SizeCells, Stride, Offset;
    ULONG64 ChildBase, ParentBase, Size;
    LONG Parent;

    *Done = FALSE;

    /* The root's children are already in CPU addresses */
    Parent = FdtParentNode(Fdt, Bus);
    if (Parent < 0)
    {
        *Done = TRUE;
        return TRUE;
    }

    Ranges = FdtGetProperty(Fdt, Bus, "ranges", &Length);
    if (!Ranges)
    {
        *Done = TRUE;
        return TRUE;
    }
    if (!Length)
        return TRUE;

    ChildCells = FdtCellCount(Fdt, Bus, "#address-cells", 2);
    SizeCells = FdtCellCount(Fdt, Bus, "#size-cells", 1);
    ParentCells = FdtCellCount(Fdt, Parent, "#address-cells", 2);
    Stride = (ChildCells + ParentCells + SizeCells) * 4;
    if (!Stride)
        return FALSE;

    for (Offset = 0; Offset + Stride <= Length; Offset += Stride)
    {
        Entry = Ranges + Offset;
        ChildBase = FdtReadCells(Entry, ChildCells);
        ParentBase = FdtReadCells(Entry + ChildCells * 4, ParentCells);
        Size = FdtReadCells(Entry + (ChildCells + ParentCells) * 4, SizeCells);

        if ((*Address >= ChildBase) && (*Address - ChildBase < Size))
        {
            *Address = ParentBase + (*Address - ChildBase);
            return TRUE;
        }
    }

    /* The bus is mapped, just not at this address */
    return FALSE;
}

/**
 * @brief Reads one address and size pair from a node's reg property.
 *
 * The cell counts come from the parent, and the address is translated
 * through every parent bus's ranges up to the root, so it comes back as a
 * CPU address. Addresses wider than two cells keep their low two, which
 * drops the space code of PCI style addresses.
 */
BOOLEAN
FdtGetReg(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ ULONG Index,
    _Out_ PULONG64 Address,
    _Out_opt_ PULONG64 Size)
{
    const UCHAR *Reg;
    ULONG Length, AddressCells, SizeCells, Stride, Depth;
    ULONG64 Value;
    BOOLEAN Done;
    LONG Parent, Bus;

    Parent = FdtParentNode(Fdt, Node);
    AddressCells = FdtCellCount(Fdt, Parent, "#address-cells", 2);
    SizeCells = FdtCellCount(Fdt, Parent, "#size-cells", 1);

    Reg = FdtGetProperty(Fdt, Node, "reg", &Length);
    Stride = (AddressCells + SizeCells) * 4;
    if (!Reg || !Stride || ((Index + 1) * Stride > Length))
        return FALSE;

    Reg += Index * Stride;
    Value = FdtReadCells(Reg, AddressCells);
    if (Size)
        *Size = FdtReadCells(Reg + AddressCells * 4, SizeCells);

    for (Bus = Parent, Depth = 0; Bus >= 0; Bus = FdtParentNode(Fdt, Bus), Depth++)
    {
        if ((Depth >= FDT_MAX_TRANSLATION_DEPTH) || !FdtTranslateOneLevel(Fdt, Bus, &Value, &Done))
            return FALSE;
        if (Done)
            break;
    }

    *Address = Value;
    return TRUE;
}

/**
 * @brief Turns entry Index of a node's interrupts into a GIC INTID.
 *
 * Only the three cell GIC binding is understood: type, number and flags.
 */
BOOLEAN
FdtGetGicInterrupt(
    _In_ const VOID *Fdt,
    _In_ LONG Node,
    _In_ ULONG Index,
    _Out_ PULONG IntId)
{
    const UCHAR *Interrupts;
    ULONG Length, Type, Number;

    Interrupts = FdtGetProperty(Fdt, Node, "interrupts", &Length);
    if (!Interrupts || ((Index + 1) * 12 > Length))
        return FALSE;

    Interrupts += Index * 12;
    Type = FdtRead32(Interrupts);
    Number = FdtRead32(Interrupts + 4);

    if (Type == FDT_GIC_SPI)
        *IntId = Number + 32;
    else if (Type == FDT_GIC_PPI)
        *IntId = Number + 16;
    else
        return FALSE;

    return TRUE;
}

/**
 * @brief Finds the console node /chosen names in stdout-path.
 *
 * The path may be an alias and may carry ":options" after it.
 */
LONG
FdtStdoutNode(
    _In_ const VOID *Fdt)
{
    CHAR Path[128];
    PCSTR Stdout, Alias;
    SIZE_T Length;
    LONG Chosen;

    Chosen = FdtPathOffset(Fdt, "/chosen");
    Stdout = FdtGetProperty(Fdt, Chosen, "stdout-path", NULL);
    if (!Stdout)
        Stdout = FdtGetProperty(Fdt, Chosen, "linux,stdout-path", NULL);
    if (!Stdout)
        return FDT_NO_NODE;

    Length = strcspn(Stdout, ":");
    if (Length >= sizeof(Path))
        return FDT_NO_NODE;
    memcpy(Path, Stdout, Length);
    Path[Length] = '\0';

    if (Path[0] != '/')
    {
        Alias = FdtGetProperty(Fdt, FdtPathOffset(Fdt, "/aliases"), Path, NULL);
        if (!Alias)
            return FDT_NO_NODE;
        return FdtPathOffset(Fdt, Alias);
    }

    return FdtPathOffset(Fdt, Path);
}

/* Older trees name the property linux,phandle */
LONG
FdtNodeByPhandle(
    _In_ const VOID *Fdt,
    _In_ ULONG Phandle)
{
    const VOID *Value;
    LONG Node;

    if (!Phandle || (Phandle == 0xFFFFFFFF))
        return FDT_NO_NODE;

    for (Node = FdtNextNode(Fdt, FDT_NO_NODE, NULL); Node >= 0; Node = FdtNextNode(Fdt, Node, NULL))
    {
        Value = FdtGetProperty(Fdt, Node, "phandle", NULL);
        if (!Value)
            Value = FdtGetProperty(Fdt, Node, "linux,phandle", NULL);
        if (Value && (FdtRead32(Value) == Phandle))
            return Node;
    }

    return FDT_NO_NODE;
}

/**
 * @brief Looks a PCI function's interrupt pin up in a host bridge's interrupt-map.
 *
 * Each entry is the masked child address and pin, the parent's phandle, then
 * the parent's unit address and interrupt specifier in its own cell counts.
 * Only GIC parents with the three cell binding are understood.
 */
BOOLEAN
FdtPciInterruptMap(
    _In_ const VOID *Fdt,
    _In_ LONG HostBridge,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Pin,
    _Out_ PULONG IntId)
{
    const UCHAR *Map, *Mask, *Entry, *Spec;
    ULONG Length, MaskLength, Child[4], MaskCells[4], ParentAddressCells, ParentInterruptCells;
    ULONG Offset, i, Type, Number;
    LONG Parent;
    BOOLEAN Match;

    Map = FdtGetProperty(Fdt, HostBridge, "interrupt-map", &Length);
    if (!Map)
        return FALSE;

    Child[0] = (Bus << 16) | (Device << 11) | (Function << 8);
    Child[1] = 0;
    Child[2] = 0;
    Child[3] = Pin;

    Mask = FdtGetProperty(Fdt, HostBridge, "interrupt-map-mask", &MaskLength);
    for (i = 0; i < 4; i++)
        MaskCells[i] = (Mask && (MaskLength >= 16)) ? FdtRead32(Mask + i * 4) : 0xFFFFFFFF;

    for (Offset = 0; Offset + 5 * 4 <= Length; )
    {
        Entry = Map + Offset;

        Parent = FdtNodeByPhandle(Fdt, FdtRead32(Entry + 16));
        if (Parent < 0)
            return FALSE;
        ParentAddressCells = FdtCellCount(Fdt, Parent, "#address-cells", 0);
        ParentInterruptCells = FdtCellCount(Fdt, Parent, "#interrupt-cells", 3);
        if ((ParentInterruptCells < 2) ||
            (Offset + (5 + ParentAddressCells + ParentInterruptCells) * 4 > Length))
        {
            return FALSE;
        }

        Match = TRUE;
        for (i = 0; i < 4; i++)
        {
            if ((Child[i] & MaskCells[i]) != (FdtRead32(Entry + i * 4) & MaskCells[i]))
                Match = FALSE;
        }

        if (Match)
        {
            Spec = Entry + (5 + ParentAddressCells) * 4;
            Type = FdtRead32(Spec);
            Number = FdtRead32(Spec + 4);
            if (Type == FDT_GIC_SPI)
                *IntId = Number + 32;
            else if (Type == FDT_GIC_PPI)
                *IntId = Number + 16;
            else
                return FALSE;
            return TRUE;
        }

        Offset += (5 + ParentAddressCells + ParentInterruptCells) * 4;
    }

    return FALSE;
}
