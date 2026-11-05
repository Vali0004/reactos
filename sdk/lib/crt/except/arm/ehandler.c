/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C specific exception and unwind handler for ARM
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <precomp.h>
#include <winnt.h>

/* the sdk headers only declare it for the 64-bit targets */
NTSYSAPI
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _UNWIND_HISTORY_TABLE *HistoryTable);

LONG
__C_specific_handler_call(
    _In_ PVOID Argument,
    _In_ PVOID EstablisherFrame,
    _In_ PVOID Funclet,
    _In_ PUCHAR NonVolatileRegisters);

/* scope ranges are code rvas, the thumb bit is not part of the range */
#define SCOPE_BEGIN(t, i) ((t)->ScopeRecord[i].BeginAddress & ~1)
#define SCOPE_END(t, i)   ((t)->ScopeRecord[i].EndAddress & ~1)

/* the unwind target sits in a scope equivalent to the one at Index */
static
BOOLEAN
IsTargetInScope(
    _In_ PSCOPE_TABLE_ARM ScopeTable,
    _In_ ULONG Index,
    _In_ ULONG TargetPc)
{
    ULONG i;

    for (i = 0; i < ScopeTable->Count; i++)
    {
        if (TargetPc >= SCOPE_BEGIN(ScopeTable, i) &&
            TargetPc < SCOPE_END(ScopeTable, i) &&
            ScopeTable->ScopeRecord[i].JumpTarget == ScopeTable->ScopeRecord[Index].JumpTarget &&
            ScopeTable->ScopeRecord[i].HandlerAddress == ScopeTable->ScopeRecord[Index].HandlerAddress)
        {
            return TRUE;
        }
    }

    return FALSE;
}

_CRTIMP
EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    _In_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ void *EstablisherFrame,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Inout_ struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    ULONG ImageBase = DispatcherContext->ImageBase;
    PSCOPE_TABLE_ARM ScopeTable = DispatcherContext->HandlerData;
    ULONG ControlPc = (DispatcherContext->ControlPc & ~1) - ImageBase;
    EXCEPTION_POINTERS ExceptionPointers;
    LONG FilterResult;
    ULONG i;

    /* a return address belongs to the call before it */
    if (DispatcherContext->ControlPcIsUnwound)
        ControlPc -= 2;

    if (IS_UNWINDING(ExceptionRecord->ExceptionFlags))
    {
        ULONG TargetPc = (DispatcherContext->TargetPc & ~1) - ImageBase;

        for (i = DispatcherContext->ScopeIndex; i < ScopeTable->Count; i++)
        {
            if (ControlPc < SCOPE_BEGIN(ScopeTable, i) || ControlPc >= SCOPE_END(ScopeTable, i))
                continue;

            /* scopes around the unwind target stay active */
            if ((ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                IsTargetInScope(ScopeTable, i, TargetPc))
            {
                return ExceptionContinueSearch;
            }

            /* an __except block, the unwind ends when it targets it */
            if (ScopeTable->ScopeRecord[i].JumpTarget != 0)
            {
                if (TargetPc == (ScopeTable->ScopeRecord[i].JumpTarget & ~1))
                    return ExceptionContinueSearch;
                continue;
            }

            /* a __finally block, skipped if an unwind passes here again */
            DispatcherContext->ScopeIndex = i + 1;
            __C_specific_handler_call((PVOID)TRUE,
                                      EstablisherFrame,
                                      (PVOID)(ImageBase + ScopeTable->ScopeRecord[i].HandlerAddress),
                                      DispatcherContext->NonVolatileRegisters);
        }

        return ExceptionContinueSearch;
    }

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;

    for (i = DispatcherContext->ScopeIndex; i < ScopeTable->Count; i++)
    {
        if (ControlPc < SCOPE_BEGIN(ScopeTable, i) ||
            ControlPc >= SCOPE_END(ScopeTable, i) ||
            ScopeTable->ScopeRecord[i].JumpTarget == 0)
        {
            continue;
        }

        /* a filter of EXCEPTION_EXECUTE_HANDLER is stored as the value itself */
        if (ScopeTable->ScopeRecord[i].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
        {
            FilterResult = __C_specific_handler_call(&ExceptionPointers,
                                                     EstablisherFrame,
                                                     (PVOID)(ImageBase + ScopeTable->ScopeRecord[i].HandlerAddress),
                                                     DispatcherContext->NonVolatileRegisters);
            if (FilterResult < 0)
                return ExceptionContinueExecution;
            if (FilterResult == 0)
                continue;
        }

        /* continue in the __except block, this does not return */
        RtlUnwindEx(EstablisherFrame,
                    (PVOID)(ImageBase + ScopeTable->ScopeRecord[i].JumpTarget),
                    ExceptionRecord,
                    (PVOID)(LONG_PTR)(LONG)ExceptionRecord->ExceptionCode,
                    DispatcherContext->ContextRecord,
                    DispatcherContext->HistoryTable);
    }

    return ExceptionContinueSearch;
}

/* the c++ runtime for arm is not written yet, msvc programs bring their own */
DWORD
__CxxFrameHandler3(
    PEXCEPTION_RECORD rec,
    EXCEPTION_REGISTRATION_RECORD* ExceptionRegistrationFrame,
    PCONTEXT context,
    EXCEPTION_REGISTRATION_RECORD** _ExceptionRecord)
{
    UNIMPLEMENTED;
    __debugbreak();
    return 0;
}
