/*
 * PROJECT:     ReactOS HAL extension for NVIDIA Tegra 3
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CSRT group and timer block the extension drives
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* the surface rt firmware publishes an empty group, VEN_NVDA&DEV_000A */
#define TEGRA3_CSRT_VENDOR_ID           'ADVN'
#define TEGRA3_CSRT_TIMER_DEVICE_ID     0x000A

/* timer block, the counter ticks at 1 MHz once the firmware set its divider */
#define TEGRA3_TIMER_BASE               0x60005000
#define TEGRA3_TIMER_SIZE               0x400
#define TEGRA3_TIMERUS_CNTR_1US         0x010
#define TEGRA3_TIMERUS_FREQUENCY        1000000

/* the avp keeps tmr1 and tmr2, tmr3 is free for the clock */
#define TEGRA3_TMR3                     0x050
#define TEGRA3_TMR3_GSI                 (32 + 41)

/* per timer registers */
#define TEGRA3_TMR_PTV                  0x000
#define TEGRA3_TMR_PCR                  0x004
#define TEGRA3_TMR_PTV_EN               0x80000000
#define TEGRA3_TMR_PTV_PER              0x40000000
#define TEGRA3_TMR_PTV_MAX              0x1FFFFFFF
#define TEGRA3_TMR_PCR_INTR_CLR         0x40000000
