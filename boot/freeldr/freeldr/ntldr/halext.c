/*
 * PROJECT:     FreeLoader
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Loads the HAL extensions the CSRT asks for
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>
#include <ndk/rtlfuncs.h>
#include <drivers/acpi/acpi.h>
#include <ddk/nthalext.h>
#include "winldr.h"
#include "registry.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#define TAG_WLDR_HALEXT 'xHlW'

PRSDP_DESCRIPTOR FindAcpiBios(VOID);

/* FUNCTIONS *****************************************************************/

/* the loader runs identity mapped, so table addresses are usable as they are */
static
PDESCRIPTION_HEADER
WinLdrFindAcpiTable(
    _In_ ULONG Signature)
{
    PRSDP_DESCRIPTOR Rsdp;
    PDESCRIPTION_HEADER Root, Table;
    ULONG Count, Index, EntrySize;
    BOOLEAN UseXsdt;

    Rsdp = FindAcpiBios();
    if (!Rsdp)
        return NULL;

    UseXsdt = (Rsdp->revision >= 2) && (Rsdp->xsdt_physical_address != 0);
    Root = UseXsdt ? (PDESCRIPTION_HEADER)(ULONG_PTR)Rsdp->xsdt_physical_address
                   : (PDESCRIPTION_HEADER)(ULONG_PTR)Rsdp->rsdt_physical_address;
    if (!Root)
        return NULL;

    EntrySize = UseXsdt ? sizeof(ULONGLONG) : sizeof(ULONG);
    Count = (Root->Length - sizeof(DESCRIPTION_HEADER)) / EntrySize;

    for (Index = 0; Index < Count; Index++)
    {
        PUCHAR Entry = (PUCHAR)(Root + 1) + (Index * EntrySize);
        ULONGLONG Address;

        /* xsdt entries are only 4 byte aligned */
        if (UseXsdt)
            RtlCopyMemory(&Address, Entry, sizeof(Address));
        else
            Address = *(PULONG)Entry;

        Table = (PDESCRIPTION_HEADER)(ULONG_PTR)Address;
        if (Table && (Table->Signature == Signature))
            return Table;
    }

    return NULL;
}

/* returns FALSE only when a listed extension failed to load */
static
BOOLEAN
WinLdrLoadHalExtension(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCSTR BootPath,
    _In_ PCSRT_RESOURCE_GROUP_HEADER Group)
{
    WCHAR KeyName[64];
    WCHAR FileName[64];
    WCHAR FilePathBuffer[96];
    UNICODE_STRING FilePath;
    PHAL_EXTENSION_MODULE_ENTRY Entry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY Link;
    ULONG Size;
    HKEY Key;
    LONG Error;

    /* groups with the same ids go to one extension, which walks them all */
    for (Link = LoaderBlock->Extension->HalExtensionModuleList.Flink;
         Link != &LoaderBlock->Extension->HalExtensionModuleList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, HAL_EXTENSION_MODULE_ENTRY, ListEntry);
        if ((Entry->VendorId == Group->VendorId) && (Entry->DeviceId == Group->DeviceId))
            return TRUE;
    }

    /* the database names groups by the four vendor characters and the device id */
    RtlStringCbPrintfW(KeyName, sizeof(KeyName),
                       L"Control\\OSExtensionDatabase\\VEN_%c%c%c%c&DEV_%04X",
                       (CHAR)(Group->VendorId & 0xFF),
                       (CHAR)((Group->VendorId >> 8) & 0xFF),
                       (CHAR)((Group->VendorId >> 16) & 0xFF),
                       (CHAR)((Group->VendorId >> 24) & 0xFF),
                       Group->DeviceId);

    Error = RegOpenKey(CurrentControlSetKey, KeyName, &Key);
    if (Error != ERROR_SUCCESS)
    {
        TRACE("No HAL extension for %S\n", KeyName);
        return TRUE;
    }

    Size = sizeof(FileName) - sizeof(WCHAR);
    Error = RegQueryValue(Key, L"HALExtension", NULL, (PUCHAR)FileName, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return TRUE;
    FileName[Size / sizeof(WCHAR)] = UNICODE_NULL;

    RtlStringCbPrintfW(FilePathBuffer, sizeof(FilePathBuffer), L"system32\\%s", FileName);
    RtlInitUnicodeString(&FilePath, FilePathBuffer);

    if (!WinLdrLoadDeviceDriver(&LoaderBlock->LoadOrderListHead, BootPath, &FilePath, 0, &LdrEntry))
    {
        ERR("Cannot load HAL extension '%wZ'\n", &FilePath);
        return FALSE;
    }

    Entry = FrLdrHeapAlloc(sizeof(*Entry), TAG_WLDR_HALEXT);
    if (!Entry)
        return FALSE;

    Entry->VendorId = Group->VendorId;
    Entry->DeviceId = Group->DeviceId;
    Entry->Reserved = 0;
    Entry->LdrEntry = PaToVa(LdrEntry);
    InsertTailList(&LoaderBlock->Extension->HalExtensionModuleList, &Entry->ListEntry);

    TRACE("Loaded HAL extension '%wZ'\n", &FilePath);
    return TRUE;
}

BOOLEAN
WinLdrLoadHalExtensions(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCSTR BootPath)
{
    PDESCRIPTION_HEADER Csrt;
    PCSRT_RESOURCE_GROUP_HEADER Group;
    PUCHAR End;
    BOOLEAN Success = TRUE;

    Csrt = WinLdrFindAcpiTable(CSRT_SIGNATURE);
    if (!Csrt)
        return TRUE;

    Group = (PCSRT_RESOURCE_GROUP_HEADER)(Csrt + 1);
    End = (PUCHAR)Csrt + Csrt->Length;

    while (((PUCHAR)Group + sizeof(*Group)) <= End)
    {
        /* a short or overrunning group ends the walk, the hal checks it again */
        if ((Group->Length < sizeof(*Group)) || (((PUCHAR)Group + Group->Length) > End))
            break;

        if (!WinLdrLoadHalExtension(LoaderBlock, BootPath, Group))
            Success = FALSE;

        Group = (PCSRT_RESOURCE_GROUP_HEADER)((PUCHAR)Group + Group->Length);
    }

    return Success;
}
