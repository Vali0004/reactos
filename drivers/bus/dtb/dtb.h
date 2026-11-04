/*
 * PROJECT:     ReactOS Device Tree Bus Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared definitions
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

#include <ntifs.h>
#include <ndk/ketypes.h>
/* arc.h first, kefuncs.h only declares the configuration lookups after it */
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <wdmguid.h>
#include <ntstrsafe.h>
#include <fdtarc.h>

#define TAG_DTB 'BTDd'

/* PCI host bridge windows taken from ranges, a root rarely has more */
#define DTB_MAX_WINDOWS     8

typedef struct _DTB_WINDOW
{
    ULONG64 Base;
    ULONG64 Length;
    BOOLEAN Prefetchable;
} DTB_WINDOW, *PDTB_WINDOW;

typedef enum _DTB_EXTENSION_TYPE
{
    DtbFdo,
    DtbHostBridgePdo,
    DtbPlatformPdo
} DTB_EXTENSION_TYPE;

/* SoC devices reported straight from their nodes, a board has a handful */
#define DTB_MAX_PLATFORM_DEVICES    16

typedef struct _DTB_COMMON_EXTENSION
{
    DTB_EXTENSION_TYPE Type;
    PDEVICE_OBJECT Self;
} DTB_COMMON_EXTENSION, *PDTB_COMMON_EXTENSION;

typedef struct _DTB_FDO_EXTENSION
{
    DTB_COMMON_EXTENSION Common;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT HostBridge;
    BOOLEAN PlatformEnumerated;
    ULONG PlatformCount;
    PDEVICE_OBJECT Platform[DTB_MAX_PLATFORM_DEVICES];
} DTB_FDO_EXTENSION, *PDTB_FDO_EXTENSION;

typedef enum _DTB_PLATFORM_KIND
{
    DtbPlatformEhci,
    DtbPlatformOhci
} DTB_PLATFORM_KIND;

typedef struct _DTB_PLATFORM_DEVICE
{
    DTB_COMMON_EXTENSION Common;
    LONG Node;
    DTB_PLATFORM_KIND Kind;
    ULONG64 Base;
    ULONG64 Length;
    ULONG IntId;
    ULONG Address;
} DTB_PLATFORM_DEVICE, *PDTB_PLATFORM_DEVICE;

typedef struct _DTB_HOST_BRIDGE
{
    DTB_COMMON_EXTENSION Common;
    LONG Node;
    BOOLEAN Present;
    UCHAR StartBus;
    UCHAR EndBus;
    ULONG64 EcamBase;
    PUCHAR EcamBus[256];
    KSPIN_LOCK ConfigLock;
    ULONG WindowCount;
    DTB_WINDOW Windows[DTB_MAX_WINDOWS];
    /* PCI I/O space, memory mapped at IoCpuBase on the CPU side */
    BOOLEAN HasIo;
    ULONG64 IoPciBase;
    ULONG64 IoCpuBase;
    ULONG64 IoLength;
} DTB_HOST_BRIDGE, *PDTB_HOST_BRIDGE;

/* dtb.c */
extern PVOID DtbBlob;

NTSTATUS
DtbCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information);

PWCHAR
DtbAllocateMultiSz(
    _In_ PCWSTR Strings,
    _In_ SIZE_T Length);

NTSTATUS
DtbQueryInterruptTranslator(
    _In_ PIO_STACK_LOCATION IoStack);

/* platdev.c */
ULONG
DtbCreatePlatformDevices(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_writes_(Maximum) PDEVICE_OBJECT *Devices,
    _In_ ULONG Maximum);

NTSTATUS
DtbPlatformPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);

/* sun50i.c */
VOID
DtbSun50iUsbPowerOn(
    _In_ LONG HostNode);

/* pcihost.c */
NTSTATUS
DtbCreateHostBridge(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PDEVICE_OBJECT *DeviceObject);

NTSTATUS
DtbHostBridgePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);
