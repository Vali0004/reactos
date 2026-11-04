/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Port I/O for 32-bit ARM
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/*
 * ARM has no I/O space. As on Windows for ARM, a port is the virtual address
 * of a memory mapped register, MmMapIoSpace gave the driver that address.
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

#undef READ_PORT_UCHAR
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef WRITE_PORT_UCHAR
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG
#undef READ_PORT_BUFFER_UCHAR
#undef READ_PORT_BUFFER_USHORT
#undef READ_PORT_BUFFER_ULONG
#undef WRITE_PORT_BUFFER_UCHAR
#undef WRITE_PORT_BUFFER_USHORT
#undef WRITE_PORT_BUFFER_ULONG

/* FUNCTIONS ******************************************************************/

UCHAR
NTAPI
READ_PORT_UCHAR(
    _In_ PUCHAR Port)
{
    return READ_REGISTER_UCHAR(Port);
}

VOID
NTAPI
WRITE_PORT_UCHAR(
    _In_ PUCHAR Port,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR(Port, Value);
}

/* every element comes from the same register, unlike a memory copy */
VOID
NTAPI
READ_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _Out_writes_all_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_REGISTER_UCHAR(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_REGISTER_UCHAR(Port, *Buffer++);
}

USHORT
NTAPI
READ_PORT_USHORT(
    _In_ PUSHORT Port)
{
    return READ_REGISTER_USHORT(Port);
}

VOID
NTAPI
WRITE_PORT_USHORT(
    _In_ PUSHORT Port,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT(Port, Value);
}

/* every element comes from the same register, unlike a memory copy */
VOID
NTAPI
READ_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _Out_writes_all_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_REGISTER_USHORT(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_REGISTER_USHORT(Port, *Buffer++);
}

ULONG
NTAPI
READ_PORT_ULONG(
    _In_ PULONG Port)
{
    return READ_REGISTER_ULONG(Port);
}

VOID
NTAPI
WRITE_PORT_ULONG(
    _In_ PULONG Port,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG(Port, Value);
}

/* every element comes from the same register, unlike a memory copy */
VOID
NTAPI
READ_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _Out_writes_all_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_REGISTER_ULONG(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_REGISTER_ULONG(Port, *Buffer++);
}

/* EOF */
