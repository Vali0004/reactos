/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HAL extension state shared inside the HAL
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

#include <ddk/nthalext.h>

typedef struct _HALEXT_INTERRUPT_CONTROLLER
{
    INTERRUPT_FUNCTION_TABLE_EX FunctionTable;
    INTERRUPT_CONTROLLER_DESCRIPTION Description;
} HALEXT_INTERRUPT_CONTROLLER, *PHALEXT_INTERRUPT_CONTROLLER;

extern TIMER_INITIALIZATION_BLOCK HalpExtTimers[];
extern ULONG HalpExtTimerCount;
extern HALEXT_INTERRUPT_CONTROLLER HalpExtControllers[];
extern ULONG HalpExtControllerCount;
extern INTERRUPT_LINE_DESCRIPTION HalpExtLines[];
extern ULONG HalpExtLineCount;
