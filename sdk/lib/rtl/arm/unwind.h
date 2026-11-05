/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM unwinding shared by the dispatcher and RtlUnwindEx
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* one .pdata entry, a packed record or an rva of the .xdata record */
typedef struct _ARM_PDATA_ENTRY
{
    ULONG BeginAddress;
    ULONG UnwindData;
} ARM_PDATA_ENTRY, *PARM_PDATA_ENTRY;

/* r4 to r11 then d8 to d15, the layout language handlers expect */
typedef struct _ARM_NONVOLATILE
{
    ULONG R[8];
    ULONGLONG D[8];
} ARM_NONVOLATILE, *PARM_NONVOLATILE;

typedef struct _ARM_FRAME_INFO
{
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    PVOID FunctionEntry;
    PEXCEPTION_ROUTINE Handler;
    PVOID HandlerData;
    ULONG_PTR EstablisherFrame;
    ARM_NONVOLATILE NonVolatile;
} ARM_FRAME_INFO, *PARM_FRAME_INFO;

/* the context the dispatcher hands to handlers, RtlUnwindEx recognises it by the magic */
#define ARM_DISPATCH_MAGIC 'XDMA'

typedef struct _ARM_DISPATCH_CONTEXT
{
    CONTEXT Context;
    ULONG Magic;
    BOOLEAN PcIsUnwound;
} ARM_DISPATCH_CONTEXT, *PARM_DISPATCH_CONTEXT;

BOOLEAN
NTAPI
RtlpArmUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN PcIsUnwound,
    _Out_ PARM_FRAME_INFO Info);

VOID
NTAPI
RtlpArmFillDispatcherContext(
    _Out_ PDISPATCHER_CONTEXT DispatcherContext,
    _In_ PARM_FRAME_INFO Info,
    _In_ PCONTEXT ContextRecord,
    _In_ ULONG_PTR TargetPc,
    _In_ BOOLEAN PcIsUnwound);

/* loads every register from the context and jumps to its pc, debug_asm.S */
DECLSPEC_NORETURN
VOID
NTAPI
RtlpRestoreContext(
    _In_ PCONTEXT Context);
