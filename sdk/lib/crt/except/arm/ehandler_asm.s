/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Funclet call helper for the ARM C specific handler
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

    TEXTAREA

//
// LONG __C_specific_handler_call(PVOID Argument, PVOID EstablisherFrame,
//                                PVOID Funclet, PUCHAR NonVolatileRegisters)
//
// filter and termination funclets run with the nonvolatile registers of the
// function they belong to, r4 to r11 then d8 to d15, the frame in r1
//
    NESTED_ENTRY __C_specific_handler_call
    push    {r4-r11, r12, lr}
    vpush   {d8-d15}
    add     r12, r3, #32
    vldm    r12, {d8-d15}
    ldm     r3, {r4-r11}
    orr     r2, r2, #1
    blx     r2
    vpop    {d8-d15}
    pop     {r4-r11, r12, pc}
    NESTED_END __C_specific_handler_call

    END
/* EOF */
