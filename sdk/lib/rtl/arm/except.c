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

#include "unwind.h"

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
    PULONG_PTR Frame;

    /* r11 frame records are {previous r11, lr}, ours leads to the routine that asked */
    Frame = (PULONG_PTR)__builtin_frame_address(0);
    Frame = Frame ? (PULONG_PTR)Frame[0] : NULL;
    *CallersAddress = Frame ? (PVOID)Frame[1] : NULL;
    if (CallersCaller)
    {
        Frame = Frame ? (PULONG_PTR)Frame[0] : NULL;
        *CallersCaller = Frame ? (PVOID)Frame[1] : NULL;
    }
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

static
BOOLEAN
RtlpArmIsUserMode(VOID)
{
    ULONG Cpsr;

    __asm__ __volatile__("mrs %0, cpsr" : "=r" (Cpsr));
    return (Cpsr & 0x1F) == 0x10;
}

/* faults report the instruction that faulted, everything else was raised by a call */
static
BOOLEAN
RtlpArmIsHardwareException(
    _In_ NTSTATUS Code)
{
    switch (Code)
    {
        case STATUS_ACCESS_VIOLATION:
        case STATUS_DATATYPE_MISALIGNMENT:
        case STATUS_ILLEGAL_INSTRUCTION:
        case STATUS_PRIVILEGED_INSTRUCTION:
        case STATUS_INTEGER_DIVIDE_BY_ZERO:
        case STATUS_INTEGER_OVERFLOW:
        case STATUS_ARRAY_BOUNDS_EXCEEDED:
        case STATUS_IN_PAGE_ERROR:
        case STATUS_GUARD_PAGE_VIOLATION:
        case STATUS_STACK_OVERFLOW:
        case STATUS_BREAKPOINT:
        case STATUS_SINGLE_STEP:
        case STATUS_FLOAT_DENORMAL_OPERAND:
        case STATUS_FLOAT_DIVIDE_BY_ZERO:
        case STATUS_FLOAT_INEXACT_RESULT:
        case STATUS_FLOAT_INVALID_OPERATION:
        case STATUS_FLOAT_OVERFLOW:
        case STATUS_FLOAT_STACK_CHECK:
        case STATUS_FLOAT_UNDERFLOW:
            return TRUE;
    }

    return FALSE;
}

/* TRUE when the handler took the exception, Stop when the search has to end */
static
BOOLEAN
RtlpArmCheckDisposition(
    _In_ EXCEPTION_DISPOSITION Disposition,
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _Out_ PBOOLEAN Stop)
{
    EXCEPTION_RECORD ExceptionRecord2;

    *Stop = FALSE;

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
                *Stop = TRUE;
            return FALSE;

        case ExceptionNestedException:
            return FALSE;

        default:
            ExceptionRecord2.ExceptionRecord = ExceptionRecord;
            ExceptionRecord2.ExceptionCode = STATUS_INVALID_DISPOSITION;
            ExceptionRecord2.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
            ExceptionRecord2.NumberParameters = 0;
            RtlRaiseException(&ExceptionRecord2);
            return FALSE;
    }
}

/* runs one chain handler, TRUE when the exception is dealt with */
static
BOOLEAN
RtlpArmCallChainHandler(
    _In_ PEXCEPTION_REGISTRATION_RECORD RegistrationFrame,
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context,
    _Out_ PBOOLEAN Stop)
{
    EXCEPTION_DISPOSITION Disposition;
    ULONG_PTR StackLow, StackHigh;

    *Stop = FALSE;
    RtlpGetStackLimits(&StackLow, &StackHigh);

    /* registration frames live on the stack */
    if (((ULONG_PTR)RegistrationFrame < StackLow) ||
        (((ULONG_PTR)RegistrationFrame + sizeof(*RegistrationFrame)) > StackHigh) ||
        ((ULONG_PTR)RegistrationFrame & 0x3))
    {
        ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
        *Stop = TRUE;
        return FALSE;
    }

    Disposition = RegistrationFrame->Handler(ExceptionRecord, RegistrationFrame, Context, NULL);
    return RtlpArmCheckDisposition(Disposition, ExceptionRecord, Context, Stop);
}

/* MSVC images unwind through .pdata, the system through the chain, both are walked in stack order */
static
BOOLEAN
RtlpArmDispatchTables(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context)
{
    ARM_DISPATCH_CONTEXT Dispatch;
    DISPATCHER_CONTEXT DispatcherContext;
    ARM_FRAME_INFO Info;
    CONTEXT Current, Frame;
    PEXCEPTION_REGISTRATION_RECORD RegistrationFrame;
    EXCEPTION_DISPOSITION Disposition;
    ULONG_PTR StackLow, StackHigh;
    BOOLEAN PcIsUnwound, Stop, Handled = FALSE;

    RtlpGetStackLimits(&StackLow, &StackHigh);

    /* a raised exception starts at a return address, a fault at the faulting instruction */
    PcIsUnwound = !RtlpArmIsHardwareException(ExceptionRecord->ExceptionCode);

    Dispatch.Context = *Context;
    Dispatch.Magic = ARM_DISPATCH_MAGIC;
    Dispatch.PcIsUnwound = PcIsUnwound;

    Current = *Context;
    RegistrationFrame = *RtlpArmExceptionListHead();

    for (;;)
    {
        Frame = Current;
        if (!RtlpArmUnwindFrame(&Current, PcIsUnwound, &Info) ||
            (Info.EstablisherFrame < Frame.Sp) || (Info.EstablisherFrame > StackHigh))
        {
            break;
        }

        /* chain handlers of system frames below this one come first */
        while (!RtlpArmIsChainEnd(RegistrationFrame) &&
               ((ULONG_PTR)RegistrationFrame < Info.EstablisherFrame))
        {
            if (RtlpArmCallChainHandler(RegistrationFrame, ExceptionRecord, Context, &Stop))
            {
                Handled = TRUE;
                goto Done;
            }
            if (Stop)
                goto Done;
            RegistrationFrame = RegistrationFrame->Next;
        }

        if (Info.Handler)
        {
            RtlpArmFillDispatcherContext(&DispatcherContext, &Info, &Dispatch.Context, 0, PcIsUnwound);
            Disposition = Info.Handler(ExceptionRecord, (PVOID)Info.EstablisherFrame, Context, &DispatcherContext);
            if (RtlpArmCheckDisposition(Disposition, ExceptionRecord, Context, &Stop))
            {
                Handled = TRUE;
                goto Done;
            }
            if (Stop)
                goto Done;
        }

        PcIsUnwound = TRUE;
    }

    /* what is left of the chain sits above the last frame the tables reached */
    while (!RtlpArmIsChainEnd(RegistrationFrame))
    {
        if (RtlpArmCallChainHandler(RegistrationFrame, ExceptionRecord, Context, &Stop))
        {
            Handled = TRUE;
            break;
        }
        if (Stop)
            break;
        RegistrationFrame = RegistrationFrame->Next;
    }

Done:
    Dispatch.Magic = 0;
    return Handled;
}

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
    BOOLEAN Stop;

    /* Perform vectored exception handling for user mode */
    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, Context))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, Context);
        return TRUE;
    }

    /* user mode runs third party images with unwind tables, the kernel only has the chain */
    if (RtlpArmIsUserMode())
        return RtlpArmDispatchTables(ExceptionRecord, Context);

    for (RegistrationFrame = *RtlpArmExceptionListHead();
         !RtlpArmIsChainEnd(RegistrationFrame);
         RegistrationFrame = RegistrationFrame->Next)
    {
        if (RtlpArmCallChainHandler(RegistrationFrame, ExceptionRecord, Context, &Stop))
            return TRUE;
        if (Stop)
            return FALSE;
    }

    /* Nobody handled it */
    return FALSE;
}

/*
 * @implemented
 *
 * Runs the termination handlers of every frame newer than TargetFrame and
 * removes those frames, all of them when TargetFrame is NULL. TargetIp
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
         !RtlpArmIsChainEnd(RegistrationFrame);
         RegistrationFrame = *Head)
    {
        /* The stack grows down, frames at or above the target are older,
           longjmp passes the stack pointer of its setjmp here */
        if (TargetFrame && ((ULONG_PTR)RegistrationFrame >= (ULONG_PTR)TargetFrame))
            break;

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
    UNREFERENCED_PARAMETER(ProcessHandle);

    /* the ABI keeps sp 8 byte aligned at public interfaces */
    if ((ULONG_PTR)StackBase & 0x7)
        RtlRaiseStatus(STATUS_BAD_INITIAL_STACK);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));
    ThreadContext->ContextFlags = CONTEXT_FULL;

    /* the start parameter is the first argument, everything here is Thumb */
    ThreadContext->R0 = (ULONG)ThreadStartParam;
    ThreadContext->Sp = (ULONG)StackBase;
    ThreadContext->Pc = (ULONG)ThreadStartAddress & ~1;
    ThreadContext->Cpsr = 0x10 | 0x20; /* user mode, T */
}

/* EOF */
