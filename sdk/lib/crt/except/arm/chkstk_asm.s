/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Implementation of _chkstk and _alloca_probe
 * COPYRIGHT:   Copyright 2014 Timo Kreuzer (timo.kreuzer@reactos.org)
 *              Copyright 2014 Yuntian Zhang (yuntian.zh@gmail.com)
 *              Copyright 2019 Mohamed Mediouni (mmediouni@gmx.fr)
 *
 * REFERENCES:  https://github.com/wine-mirror/wine/commit/2b095beace7b457586bd33b3b1c81df116215193
 */

/* INCLUDES ******************************************************************/

#include <kxarm.h>

/* CODE **********************************************************************/
    TEXTAREA

//
// r4 comes in as the frame size in words and leaves in bytes, r0-r3 are kept
// every page from sp down to the new frame is touched in order, so the stack
// guard page moves along instead of being jumped over
//
    LEAF_ENTRY __chkstk
    lsl r4, r4, #2
    push {r5, r6}
    add r12, sp, #8
    mov r5, r4
1:
    cmp r5, #4096
    ble 2f
    sub r12, r12, #4096
    ldr r6, [r12]
    sub r5, r5, #4096
    b 1b
2:
    sub r12, r12, r5
    ldr r6, [r12]
    pop {r5, r6}
    bx lr
    LEAF_END __chkstk

    LEAF_ENTRY __alloca_probe
    __assertfail
    bx lr
    LEAF_END __alloca_probe

    END
/* EOF */
