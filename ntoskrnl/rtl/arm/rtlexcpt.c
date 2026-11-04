/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/rtl/arm/rtlexcpt.c
 * PURPOSE:         ARM Exception Helper Routines for Stack Walking
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/* the kernel stack of the current thread, frames outside it end a walk */
BOOLEAN
NTAPI
RtlpCaptureStackLimits(IN ULONG_PTR Ebp,
                       IN ULONG_PTR *StackBegin,
                       IN ULONG_PTR *StackEnd)
{
    PKTHREAD Thread = KeGetCurrentThread();

    if (!Thread)
        return FALSE;

    *StackBegin = Thread->StackLimit;
    *StackEnd = (ULONG_PTR)Thread->StackBase;

    return (Ebp >= *StackBegin) && (Ebp < *StackEnd);
}

/**
 * @brief Walks the kernel frame chain of the caller.
 *
 * Windows ARM code keeps a {r11, lr} record at r11 in every frame. Only the
 * kernel stack is walked, a request for user frames returns nothing.
 */
ULONG
NTAPI
RtlWalkFrameChain(OUT PVOID *Callers,
                  IN ULONG Count,
                  IN ULONG Flags)
{
    ULONG_PTR Frame, StackBegin, StackEnd;
    ULONG i = 0;

    /* bit 0 asks for the user mode frames */
    if (Flags & 1)
        return 0;

    Frame = (ULONG_PTR)__builtin_frame_address(0);
    if (!RtlpCaptureStackLimits(Frame, &StackBegin, &StackEnd))
        return 0;

    while ((i < Count) &&
           !(Frame & 3) &&
           (Frame >= StackBegin) &&
           (Frame + 2 * sizeof(ULONG_PTR) <= StackEnd))
    {
        PULONG_PTR Record = (PULONG_PTR)Frame;

        if (!Record[1])
            break;
        Callers[i++] = (PVOID)Record[1];

        /* the chain only climbs */
        if (Record[0] <= Frame)
            break;
        Frame = Record[0];
    }

    return i;
}

/* EOF */
