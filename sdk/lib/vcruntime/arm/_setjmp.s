/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of _setjmp for ARM
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <kxarm.h>

    TEXTAREA

    LEAF_ENTRY _setjmpex

    /* Store r1 (->Frame) and r4 - r11 */
    stmia r0!, {r1,r4-r11}

    /* Store sp (->Sp), lr (->Pc), fpscr (->Fpscr) */
    mov r1, sp
    vmrs r2, fpscr
    stmia r0!, {r1,r2,lr}

    /* Store the nonvolatile d8-d15 */
    vstmia r0, {d8-d15}

    /* Return 0 */
    mov r0, #0
    bx lr

    LEAF_END _setjmpex

    IMPORT __intrinsic_setjmpex, WEAK _setjmpex
    IMPORT __intrinsic_setjmp, WEAK _setjmpex
    IMPORT _setjmp, WEAK _setjmpex
    IMPORT setjmp, WEAK _setjmpex

    END

/* EOF */
