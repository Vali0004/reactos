/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/ex/arm/ioport.s
* PURPOSE:         Low level port communication functions for ARM
* PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
*                  Jonathan McAvin (mcavinjohn@gmail.com)
*/

/* INCLUDES *****************************************************************/

#include <ksarm.h>

    TEXTAREA

/*
 * Registers live in device memory. A dmb after each read keeps later normal
 * accesses from passing it, one before each write keeps earlier ones ahead.
 */
    LEAF_ENTRY READ_REGISTER_UCHAR
    ldrb r0, [r0]
    dmb
    bx lr
    LEAF_END READ_REGISTER_UCHAR

    LEAF_ENTRY WRITE_REGISTER_UCHAR
    dmb
    strb r1, [r0]
    bx lr
    LEAF_END WRITE_REGISTER_UCHAR

    LEAF_ENTRY READ_REGISTER_USHORT
    ldrh r0, [r0]
    dmb
    bx lr
    LEAF_END READ_REGISTER_USHORT

    LEAF_ENTRY WRITE_REGISTER_USHORT
    dmb
    strh r1, [r0]
    bx lr
    LEAF_END WRITE_REGISTER_USHORT

    LEAF_ENTRY READ_REGISTER_ULONG
    ldr r0, [r0]
    dmb
    bx lr
    LEAF_END READ_REGISTER_ULONG

    LEAF_ENTRY WRITE_REGISTER_ULONG
    dmb
    str r1, [r0]
    bx lr
    LEAF_END WRITE_REGISTER_ULONG

    /* (Register, Buffer, Count) */
    LEAF_ENTRY READ_REGISTER_BUFFER_UCHAR
    cbz r2, 2f
1:
    ldrb r3, [r0]
    strb r3, [r1], #1
    subs r2, r2, #1
    bne 1b
    dmb
2:
    bx lr
    LEAF_END READ_REGISTER_BUFFER_UCHAR

    LEAF_ENTRY WRITE_REGISTER_BUFFER_UCHAR
    cbz r2, 2f
    dmb
1:
    ldrb r3, [r1], #1
    strb r3, [r0]
    subs r2, r2, #1
    bne 1b
2:
    bx lr
    LEAF_END WRITE_REGISTER_BUFFER_UCHAR

    /* (Register, Buffer, Count) */
    LEAF_ENTRY READ_REGISTER_BUFFER_USHORT
    cbz r2, 2f
1:
    ldrh r3, [r0]
    strh r3, [r1], #2
    subs r2, r2, #1
    bne 1b
    dmb
2:
    bx lr
    LEAF_END READ_REGISTER_BUFFER_USHORT

    LEAF_ENTRY WRITE_REGISTER_BUFFER_USHORT
    cbz r2, 2f
    dmb
1:
    ldrh r3, [r1], #2
    strh r3, [r0]
    subs r2, r2, #1
    bne 1b
2:
    bx lr
    LEAF_END WRITE_REGISTER_BUFFER_USHORT

    /* (Register, Buffer, Count) */
    LEAF_ENTRY READ_REGISTER_BUFFER_ULONG
    cbz r2, 2f
1:
    ldr r3, [r0]
    str r3, [r1], #4
    subs r2, r2, #1
    bne 1b
    dmb
2:
    bx lr
    LEAF_END READ_REGISTER_BUFFER_ULONG

    LEAF_ENTRY WRITE_REGISTER_BUFFER_ULONG
    cbz r2, 2f
    dmb
1:
    ldr r3, [r1], #4
    str r3, [r0]
    subs r2, r2, #1
    bne 1b
2:
    bx lr
    LEAF_END WRITE_REGISTER_BUFFER_ULONG

    END
/* EOF */
