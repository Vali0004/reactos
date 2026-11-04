/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     32-bit ARM definitions for the UEFI loader, after the ARM64 ones
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'

/* UEFI disks have no BIOS drive numbers */
UCHAR DriveMapGetBiosDriveNumber(PCSTR DeviceName);

VOID StallExecutionProcessor(ULONG Microseconds);

extern UCHAR FrldrBootDrive;
extern ULONG FrldrBootPartition;
