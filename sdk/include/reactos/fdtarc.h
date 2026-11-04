/*
 * PROJECT:     ReactOS Flattened Device Tree library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Finds the device tree the loader passed in the ARC tree
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

#include <fdt.h>

/* ndk/kefuncs.h only declares it when arc.h came first */
NTSYSAPI
PCONFIGURATION_COMPONENT_DATA
NTAPI
KeFindConfigurationNextEntry(
    _In_ PCONFIGURATION_COMPONENT_DATA Child,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_opt_ PULONG ComponentKey,
    _Inout_ PCONFIGURATION_COMPONENT_DATA *NextLink);

/**
 * @brief Returns the physical address and size of the loader's device tree.
 *
 * The loader adds it like the ACPI tables, as a MultiFunctionAdapter whose
 * configuration data is an FDT_BIOS_DATA after the resource list header.
 */
FORCEINLINE
BOOLEAN
FdtFindLoaderBlob(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PPHYSICAL_ADDRESS Address,
    _Out_ PULONG Size)
{
    PCONFIGURATION_COMPONENT_DATA Entry, Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PFDT_BIOS_DATA Data;

    if (!LoaderBlock || !LoaderBlock->ConfigurationRoot)
        return FALSE;

    for (Entry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                              AdapterClass,
                                              MultiFunctionAdapter,
                                              NULL,
                                              &Next);
         Entry != NULL;
         Entry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                              AdapterClass,
                                              MultiFunctionAdapter,
                                              NULL,
                                              &Next))
    {
        Next = Entry;
        if (!Entry->ComponentEntry.Identifier ||
            _stricmp(Entry->ComponentEntry.Identifier, FDT_ADAPTER_IDENTIFIER) ||
            (Entry->ComponentEntry.ConfigurationDataLength <
             sizeof(CM_PARTIAL_RESOURCE_LIST) + sizeof(FDT_BIOS_DATA)))
        {
            continue;
        }

        ResourceList = Entry->ConfigurationData;
        Data = (PFDT_BIOS_DATA)(ResourceList + 1);
        *Address = Data->Address;
        *Size = Data->Size;
        return (Data->Address.QuadPart != 0) && (Data->Size != 0);
    }

    return FALSE;
}
