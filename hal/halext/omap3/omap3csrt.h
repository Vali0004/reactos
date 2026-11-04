/*
 * PROJECT:     ReactOS HAL extension for TI OMAP3
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CSRT resource group the firmware publishes for the extension
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* group ids, the loader finds the extension under VEN_OMAP&DEV_3430 */
#define OMAP3_CSRT_VENDOR_ID            'PAMO'
#define OMAP3_CSRT_DEVICE_ID            0x3430
#define OMAP3_CSRT_SHARED_INFO_VERSION  1

/* the shared info of the group, addresses are physical */
#include <pshpack1.h>
typedef struct _OMAP3_CSRT_SHARED_INFO
{
    ULONG Version;
    ULONG IntcBase;             /* 0x48200000 */
    ULONG TimerBase;            /* gptimer1, 0x48318000 */
    ULONG TimerGsi;             /* gsi of the timer line, GsiBase + 37 */
    ULONG TimerFrequency;       /* gptimer1 functional clock, 32768 from the 32k clock */
    ULONG CounterBase;          /* 32k sync counter value, 0x48320010 */
    ULONG GsiBase;              /* gsi of intc line 0, 32 or more */
    ULONG SoftwareLine[3];      /* unused intc lines for apc, dpc and ipi */
} OMAP3_CSRT_SHARED_INFO, *POMAP3_CSRT_SHARED_INFO;
#include <poppack.h>
