/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of __longjmp_noframe for ARM
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <kxarm.h>

    TEXTAREA

    LEAF_ENTRY __longjmp_noframe

    ldmia r0!, {r1,r4-r11}
    ldmia r0!, {r1,r2,r3}
    mov sp, r1
    mov lr, r3
    vmsr fpscr, r2

    /* Load the nonvolatile d8-d15 */
    vldmia r0, {d8-d15}

    /* Return 1 */
    mov r0, #1
    bx lr

    LEAF_END __longjmp_noframe

    END

/* EOF */
