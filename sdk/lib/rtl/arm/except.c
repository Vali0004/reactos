/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS Run-Time Library
 * PURPOSE:           Exception dispatching for ARM
 * FILE:              lib/rtl/arm/except.c
 * PROGRAMERS:
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *CallersAddress,
    _Out_ PVOID *CallersCaller)
{
    ASSERT(FALSE);
}

/*
 * 32-bit ARM has no unwind tables from clang, exception handlers are found
 * through the registration chain PSEH3 keeps in NtTib.ExceptionList, like on
 * x86. User mode reaches it through the TEB, kernel mode through the KPCR.
 */
static
PEXCEPTION_REGISTRATION_RECORD *
RtlpArmExceptionListHead(VOID)
{
    ULONG Cpsr, Base;

    __asm__ __volatile__("mrs %0, cpsr" : "=r" (Cpsr));
    if ((Cpsr & 0x1F) == 0x10)
    {
        /* TPIDRURW holds the TEB */
        __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 2" : "=r" (Base));
    }
    else
    {
        /* TPIDRPRW holds the KPCR, the low bits are reserved */
        __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 4" : "=r" (Base));
        Base &= ~0xFFFUL;
    }

    return &((PNT_TIB)Base)->ExceptionList;
}

/* the chain may end in EXCEPTION_CHAIN_END or in NULL for threads that never set it */
#define RtlpArmIsChainEnd(Frame) \
    (((Frame) == EXCEPTION_CHAIN_END) || ((Frame) == NULL))

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context)
{
    PEXCEPTION_REGISTRATION_RECORD RegistrationFrame;
    EXCEPTION_RECORD ExceptionRecord2;
    EXCEPTION_DISPOSITION Disposition;
    ULONG_PTR StackLow, StackHigh;

    /* Perform vectored exception handling for user mode */
    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, Context))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, Context);
        return TRUE;
    }

    RtlpGetStackLimits(&StackLow, &StackHigh);

    for (RegistrationFrame = *RtlpArmExceptionListHead();
         !RtlpArmIsChainEnd(RegistrationFrame);
         RegistrationFrame = RegistrationFrame->Next)
    {
        /* Registration frames live on the stack */
        if (((ULONG_PTR)RegistrationFrame < StackLow) ||
            (((ULONG_PTR)RegistrationFrame + sizeof(*RegistrationFrame)) > StackHigh) ||
            ((ULONG_PTR)RegistrationFrame & 0x3))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return FALSE;
        }

        Disposition = RegistrationFrame->Handler(ExceptionRecord,
                                                 RegistrationFrame,
                                                 Context,
                                                 NULL);
        switch (Disposition)
        {
            case ExceptionContinueExecution:
                if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                {
                    ExceptionRecord2.ExceptionRecord = ExceptionRecord;
                    ExceptionRecord2.ExceptionCode = STATUS_NONCONTINUABLE_EXCEPTION;
                    ExceptionRecord2.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                    ExceptionRecord2.NumberParameters = 0;
                    RtlRaiseException(&ExceptionRecord2);
                }
                RtlCallVectoredContinueHandlers(ExceptionRecord, Context);
                return TRUE;

            case ExceptionContinueSearch:
                if (ExceptionRecord->ExceptionFlags & EXCEPTION_STACK_INVALID)
                    return FALSE;
                break;

            default:
                ExceptionRecord2.ExceptionRecord = ExceptionRecord;
                ExceptionRecord2.ExceptionCode = STATUS_INVALID_DISPOSITION;
                ExceptionRecord2.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                ExceptionRecord2.NumberParameters = 0;
                RtlRaiseException(&ExceptionRecord2);
                break;
        }
    }

    /* Nobody handled it */
    return FALSE;
}

/*
 * @implemented
 *
 * Runs the termination handlers of every frame registered after TargetFrame
 * and removes those frames, all of them when TargetFrame is NULL. TargetIp
 * and ReturnValue are not used, PSEH3 resumes the target frame itself.
 */
VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    PEXCEPTION_REGISTRATION_RECORD *Head, RegistrationFrame;
    EXCEPTION_RECORD LocalRecord;
    CONTEXT Context;

    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ReturnValue);

    if (!ExceptionRecord)
    {
        ExceptionRecord = &LocalRecord;
        RtlZeroMemory(&LocalRecord, sizeof(LocalRecord));
        LocalRecord.ExceptionCode = STATUS_UNWIND;
    }

    ExceptionRecord->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (!TargetFrame)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    RtlZeroMemory(&Context, sizeof(Context));

    Head = RtlpArmExceptionListHead();
    for (RegistrationFrame = *Head;
         !RtlpArmIsChainEnd(RegistrationFrame) && (RegistrationFrame != TargetFrame);
         RegistrationFrame = *Head)
    {
        /* PSEH3 unlinks its own frame while unwinding, others are taken off here */
        RegistrationFrame->Handler(ExceptionRecord, RegistrationFrame, &Context, NULL);
        if (*Head == RegistrationFrame)
            *Head = RegistrationFrame->Next;
    }
}

VOID
NTAPI
RtlInitializeContext(
    IN HANDLE ProcessHandle,
    OUT PCONTEXT ThreadContext,
    IN PVOID ThreadStartParam  OPTIONAL,
    IN PTHREAD_START_ROUTINE ThreadStartAddress,
    IN PVOID StackBase)
{
}

/* EOF */
