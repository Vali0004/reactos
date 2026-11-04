/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm/generic/spinlock.c
 * PURPOSE:         SpinLock Routines
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

#include <internal/spinlock.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock
#undef KeRaiseIrql
#undef KeLowerIrql

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeRaiseIrql(KIRQL NewIrql,
            PKIRQL OldIrql)
{
    /* Call the fastcall function */
    *OldIrql = KfRaiseIrql(NewIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
KeLowerIrql(KIRQL NewIrql)
{
    /* Call the fastcall function */
    KfLowerIrql(NewIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
KeAcquireSpinLock(PKSPIN_LOCK SpinLock,
                  PKIRQL OldIrql)
{
    /* Call the fastcall function */
    *OldIrql = KfAcquireSpinLock(SpinLock);
}

/*
 * @implemented
 */
KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to synch and acquire the lock */
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
NTAPI
KeReleaseSpinLock(PKSPIN_LOCK SpinLock,
                  KIRQL NewIrql)
{
    /* Call the fastcall function */
    KfReleaseSpinLock(SpinLock, NewIrql);
}

/*
 * @implemented
 */
KIRQL
FASTCALL
KfAcquireSpinLock(PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to dispatch and acquire the lock, DBG marks the owner like on x86 */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
FASTCALL
KfReleaseSpinLock(PKSPIN_LOCK SpinLock,
                  KIRQL OldIrql)
{
    /* Release the lock and lower IRQL back */
    KxReleaseSpinLock(SpinLock);
    KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
KIRQL
FASTCALL
KeAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    return OldIrql;
}

/*
 * @implemented
 */
KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(IN PKSPIN_LOCK SpinLock,
                               IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;
    KeRaiseIrql(DISPATCH_LEVEL, &LockHandle->OldIrql);
    KxAcquireSpinLock(LockHandle->LockQueue.Lock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(IN PKSPIN_LOCK SpinLock,
                                           IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;
    KeRaiseIrql(SYNCH_LEVEL, &LockHandle->OldIrql);
    KxAcquireSpinLock(LockHandle->LockQueue.Lock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeReleaseQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                        IN KIRQL OldIrql)
{
    KxReleaseSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    KxReleaseSpinLock(LockHandle->LockQueue.Lock);
    KeLowerIrql(LockHandle->OldIrql);
}

/*
 * @implemented
 */
BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                                         IN PKIRQL OldIrql)
{
    /* the irql goes up even when the lock is taken */
    KeRaiseIrql(SYNCH_LEVEL, OldIrql);
    return KeTryToAcquireSpinLockAtDpcLevel(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
}

/*
 * @implemented
 */
LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                             OUT PKIRQL OldIrql)
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
    return KeTryToAcquireSpinLockAtDpcLevel(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
}

/* EOF */
