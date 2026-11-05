/*
 * PROJECT:     ReactOS HAL extension for Qualcomm QSD8x50
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CSRT resource group the firmware publishes for the extension
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* group ids as HtcLeoPkg publishes them, the vic under VEN_QCOM&DEV_8250, the timers under DEV_0001 */
#define QSD8X50_CSRT_VENDOR_ID              'MOCQ'
#define QSD8X50_CSRT_DEVICE_ID              0x8250
#define QSD8X50_CSRT_TIMER_DEVICE_ID        0x0001
#define QSD8X50_CSRT_SHARED_INFO_VERSION    1

/* the shared info of the group, addresses are physical */
#include <pshpack1.h>
typedef struct _QSD8X50_CSRT_SHARED_INFO
{
    ULONG Version;
    ULONG VicBase;              /* 0xAC000000 */
    ULONG TimerBase;            /* gp and debug timer block, 0xAC100000 */
    ULONG GptLine;              /* vic line of the gp timer */
    ULONG GptFrequency;         /* 32768 */
    ULONG DgtFrequency;         /* debug timer after its divider, firmware leaves it running */
    ULONG GsiBase;              /* gsi of vic line 0, 32 or more */
    ULONG SoftwareLine[3];      /* unused vic lines for apc, dpc and ipi */
} QSD8X50_CSRT_SHARED_INFO, *PQSD8X50_CSRT_SHARED_INFO;
#include <poppack.h>

/* groups without shared info get the qsd8250 values, still to be checked on hardware */
#define QSD8X50_DEFAULT_VIC_BASE        0xAC000000
#define QSD8X50_DEFAULT_TIMER_BASE      0xAC100000
#define QSD8X50_DEFAULT_GPT_LINE        7
#define QSD8X50_DEFAULT_GPT_FREQUENCY   32768
#define QSD8X50_DEFAULT_DGT_FREQUENCY   4800000
#define QSD8X50_DEFAULT_GSI_BASE        32
#define QSD8X50_DEFAULT_SOFTWARE_LINE0  61
#define QSD8X50_DEFAULT_SOFTWARE_LINE1  62
#define QSD8X50_DEFAULT_SOFTWARE_LINE2  63
