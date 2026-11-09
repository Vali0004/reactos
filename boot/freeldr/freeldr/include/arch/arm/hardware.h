/*
 * PROJECT:         ReactOS Boot Loader
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            boot/freeldr/freeldr/include/arch/arm/hardware.h
 * PURPOSE:         Header for ARC definitions (to be cleaned up)
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#pragma once

typedef struct _PAGE_TABLE_ARM
{
    HARDWARE_PTE_ARMV6 Pte[1024];
} PAGE_TABLE_ARM, *PPAGE_TABLE_ARM;
C_ASSERT(sizeof(PAGE_TABLE_ARM) == PAGE_SIZE);

typedef struct _PAGE_DIRECTORY_ARM
{
    union
    {
        HARDWARE_PDE_ARMV6 Pde[4096];
        HARDWARE_LARGE_PTE_ARMV6 Pte[4096];
    };
} PAGE_DIRECTORY_ARM, *PPAGE_DIRECTORY_ARM;
C_ASSERT(sizeof(PAGE_DIRECTORY_ARM) == (4 * PAGE_SIZE));

// FIXME: sync with NDK
typedef enum _ARM_DOMAIN
{
    FaultDomain,
    ClientDomain,
    InvalidDomain,
    ManagerDomain
} ARM_DOMAIN;

#define PDE_SHIFT 20
