/*
 * PROJECT:     ReactOS msvcrt.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM exception handler call, after the Wine except_arm.c code
 * COPYRIGHT:   Copyright 2011-2024 Wine team
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

    TEXTAREA

//
// void *call_exc_handler(void *handler, ULONG_PTR frame, UINT flags, BYTE *nonvol_regs)
//
// the handler runs with the nonvolatile registers of its frame, r4-r11
//
    NESTED_ENTRY call_exc_handler
    push    {r1, r4-r11, lr}
    ldm     r3, {r4-r11}
    blx     r0
    pop     {r3-r11, pc}
    NESTED_END call_exc_handler

    END
