/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM table based unwinding for images that carry .pdata
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

#include "unwind.h"

/* FUNCTIONS ****************************************************************/

/* bytes of code the entry covers, both forms count halfwords */
static
ULONG
RtlpArmFunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PARM_PDATA_ENTRY Entry)
{
    if (Entry->UnwindData & 3)
        return ((Entry->UnwindData >> 2) & 0x7FF) * 2;

    return (*(PULONG)(ImageBase + Entry->UnwindData) & 0x3FFFF) * 2;
}

PVOID
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc,
    _Out_ PULONG_PTR ImageBase,
    _Inout_opt_ PVOID HistoryTable)
{
    PARM_PDATA_ENTRY Table, Entry;
    ULONG Size, Count, Low, High, Middle, Rva;

    UNREFERENCED_PARAMETER(HistoryTable);

    if (!RtlPcToFileHeader((PVOID)ControlPc, (PVOID *)ImageBase))
        return NULL;

    Table = RtlImageDirectoryEntryToData((PVOID)*ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &Size);
    if (!Table)
        return NULL;

    /* the thumb bit is not part of the rva */
    Rva = (ULONG)((ControlPc & ~1) - *ImageBase);
    Count = Size / sizeof(ARM_PDATA_ENTRY);

    /* the last entry starting at or below the address */
    Low = 0;
    High = Count;
    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if ((Table[Middle].BeginAddress & ~1) <= Rva)
            Low = Middle + 1;
        else
            High = Middle;
    }

    if (!Low)
        return NULL;

    Entry = &Table[Low - 1];
    if (Rva - (Entry->BeginAddress & ~1) >= RtlpArmFunctionLength(*ImageBase, Entry))
        return NULL;

    return Entry;
}

/* pops one word into a register slot and records where it came from */
static
ULONG
RtlpArmPop(
    _Inout_ PCONTEXT Context)
{
    ULONG Value = *(PULONG)Context->Sp;

    Context->Sp += 4;
    return Value;
}

/* pops r0 to r12 and lr in register order, lr becomes the return address */
static
VOID
RtlpArmPopMask(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Mask,
    _In_ BOOLEAN Lr,
    _Inout_ PBOOLEAN PcRestored)
{
    PULONG Regs = &Context->R0;
    ULONG i;

    for (i = 0; i < 13; i++)
    {
        if (Mask & (1 << i))
            Regs[i] = RtlpArmPop(Context);
    }

    if (Lr)
    {
        Context->Pc = RtlpArmPop(Context);
        *PcRestored = TRUE;
    }
}

static
VOID
RtlpArmPopDouble(
    _Inout_ PCONTEXT Context,
    _In_ ULONG First,
    _In_ ULONG Last)
{
    ULONG i;

    for (i = First; i <= Last && i < 32; i++)
    {
        Context->D[i] = *(PULONGLONG)Context->Sp;
        Context->Sp += 8;
    }
}

/* runs the unwind codes from Start until an end code, undoing the prolog */
static
BOOLEAN
RtlpArmExecuteCodes(
    _Inout_ PCONTEXT Context,
    _In_ PUCHAR Codes,
    _In_ ULONG Size,
    _In_ ULONG Start,
    _Out_ PBOOLEAN PcRestored)
{
    ULONG i = Start, Value;
    UCHAR Code, Next;

    *PcRestored = FALSE;

    while (i < Size)
    {
        Code = Codes[i++];
        Next = (i < Size) ? Codes[i] : 0;

        if (Code <= 0x7F)
        {
            Context->Sp += (Code & 0x7F) * 4;
        }
        else if (Code <= 0xBF)
        {
            i++;
            RtlpArmPopMask(Context, ((Code & 0x1F) << 8) | Next, (Code & 0x20) != 0, PcRestored);
        }
        else if (Code <= 0xCF)
        {
            Context->Sp = (&Context->R0)[Code & 0x0F];
        }
        else if (Code <= 0xDF)
        {
            ULONG Last = (Code & 0x03) + ((Code <= 0xD7) ? 4 : 8);
            ULONG Mask = ((1 << (Last + 1)) - 1) & ~0x0F;

            RtlpArmPopMask(Context, Mask, (Code & 0x04) != 0, PcRestored);
        }
        else if (Code <= 0xE7)
        {
            RtlpArmPopDouble(Context, 8, (Code & 0x07) + 8);
        }
        else if (Code <= 0xEB)
        {
            i++;
            Context->Sp += (((Code & 0x03) << 8) | Next) * 4;
        }
        else if (Code <= 0xED)
        {
            i++;
            RtlpArmPopMask(Context, Next, (Code & 0x01) != 0, PcRestored);
        }
        else if (Code == 0xEE)
        {
            /* microsoft specific, nothing to undo */
            i++;
        }
        else if (Code == 0xEF)
        {
            i++;
            Value = *(PULONG)Context->Sp;
            Context->Sp += (Next & 0x0F) * 4;
            Context->Pc = Value;
            *PcRestored = TRUE;
        }
        else if (Code == 0xF5 || Code == 0xF6)
        {
            ULONG Base = (Code == 0xF6) ? 16 : 0;

            i++;
            RtlpArmPopDouble(Context, Base + (Next >> 4), Base + (Next & 0x0F));
        }
        else if (Code == 0xF7 || Code == 0xF9)
        {
            if (i + 2 > Size)
                return FALSE;
            Context->Sp += ((Codes[i] << 8) | Codes[i + 1]) * 4;
            i += 2;
        }
        else if (Code == 0xF8 || Code == 0xFA)
        {
            if (i + 3 > Size)
                return FALSE;
            Context->Sp += ((Codes[i] << 16) | (Codes[i + 1] << 8) | Codes[i + 2]) * 4;
            i += 3;
        }
        else if (Code == 0xFB || Code == 0xFC)
        {
            /* a nop in the prolog */
        }
        else if (Code >= 0xFD)
        {
            /* end of the codes */
            return TRUE;
        }
        else
        {
            /* reserved codes */
            return FALSE;
        }
    }

    return TRUE;
}

/* bytes of the instruction an unwind code stands for, 0 for the end code */
static
ULONG
RtlpArmCodeInstructionSize(
    _In_ UCHAR Code)
{
    if (Code <= 0x7F) return 2;
    if (Code <= 0xBF) return 4;
    if (Code <= 0xCF) return 2;
    if (Code <= 0xD7) return 2;
    if (Code <= 0xEB) return 4;
    if (Code <= 0xEE) return 2;
    if (Code == 0xEF) return 4;
    if (Code <= 0xF6) return 4;
    if (Code <= 0xF8) return 2;
    if (Code <= 0xFA) return 4;
    if (Code == 0xFB) return 2;
    if (Code == 0xFC) return 4;
    if (Code == 0xFD) return 2;
    if (Code == 0xFE) return 4;
    return 0;
}

/* bytes the code takes in the stream */
static
ULONG
RtlpArmCodeLength(
    _In_ UCHAR Code)
{
    if ((Code >= 0x80 && Code <= 0xBF) || (Code >= 0xE8 && Code <= 0xEF) || Code == 0xF5 || Code == 0xF6)
        return 2;
    if (Code == 0xF7 || Code == 0xF9)
        return 3;
    if (Code == 0xF8 || Code == 0xFA)
        return 4;
    return 1;
}

/* walks the codes from Start to the end code, returns how many and their instruction bytes */
static
ULONG
RtlpArmCountCodes(
    _In_ PUCHAR Codes,
    _In_ ULONG Size,
    _In_ ULONG Start,
    _Out_opt_ PULONG Sizes,
    _In_ ULONG MaxSizes,
    _Out_ PULONG Bytes)
{
    ULONG i = Start, Count = 0;

    *Bytes = 0;
    while (i < Size)
    {
        UCHAR Code = Codes[i];
        ULONG Length = RtlpArmCodeInstructionSize(Code);

        if (Code == 0xFF)
            break;
        if (Sizes && Count < MaxSizes)
            Sizes[Count] = Length;
        Count++;
        *Bytes += Length;
        if (Code >= 0xFD)
            break;
        i += RtlpArmCodeLength(Code);
    }

    return Count;
}

/* the index of the code Skip codes after Start */
static
ULONG
RtlpArmSkipCodes(
    _In_ PUCHAR Codes,
    _In_ ULONG Size,
    _In_ ULONG Start,
    _In_ ULONG Skip)
{
    while (Skip-- && Start < Size && Codes[Start] < 0xFD)
        Start += RtlpArmCodeLength(Codes[Start]);

    return Start;
}

/* inside an epilog the instructions run in code order, skip the ones done */
static
ULONG
RtlpArmEpilogStart(
    _In_ PUCHAR Codes,
    _In_ ULONG Size,
    _In_ ULONG Start,
    _In_ ULONG Done)
{
    while (Start < Size && Codes[Start] < 0xFD)
    {
        ULONG Length = RtlpArmCodeInstructionSize(Codes[Start]);

        if (Done < Length)
            break;
        Done -= Length;
        Start += RtlpArmCodeLength(Codes[Start]);
    }

    return Start;
}

/* undoes a packed prolog, the pc is taken to be past it in the body */
static
VOID
RtlpArmUnwindPacked(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Data,
    _Out_ PBOOLEAN PcRestored)
{
    ULONG H = (Data >> 15) & 1;
    ULONG Reg = (Data >> 16) & 7;
    ULONG R = (Data >> 19) & 1;
    ULONG L = (Data >> 20) & 1;
    ULONG C = (Data >> 21) & 1;
    ULONG StackAdjust = Data >> 22;
    ULONG Mask = 0;

    /* adjustments from 0x3F4 up are folded into the push, the low bits count the words */
    if (StackAdjust >= 0x3F4)
        StackAdjust = (StackAdjust & 3) + 1;
    Context->Sp += StackAdjust * 4;

    if (R && Reg != 7)
        RtlpArmPopDouble(Context, 8, 8 + Reg);

    if (!R)
        Mask |= ((1 << (Reg + 5)) - 1) & ~0x0F;
    if (C)
        Mask |= 1 << 11;

    RtlpArmPopMask(Context, Mask, (BOOLEAN)L, PcRestored);

    if (H)
        Context->Sp += 16;
}

PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_ PVOID FunctionEntry,
    _Inout_ PCONTEXT Context,
    _Outptr_ PVOID *HandlerData,
    _Out_ PULONG_PTR EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    PARM_PDATA_ENTRY Entry = FunctionEntry;
    PEXCEPTION_ROUTINE Handler = NULL;
    BOOLEAN PcRestored, InBody = TRUE;
    ULONG Start = 0;
    PULONG Xdata;
    ULONG Header, EpilogCount, CodeWords;
    PULONG Scopes;
    PUCHAR Codes;

    UNREFERENCED_PARAMETER(HandlerType);
    UNREFERENCED_PARAMETER(ContextPointers);

    *HandlerData = NULL;

    if (!Entry)
    {
        /* a leaf keeps the return address in lr */
        Context->Pc = Context->Lr;
        *EstablisherFrame = Context->Sp;
        return NULL;
    }

    if (Entry->UnwindData & 3)
    {
        RtlpArmUnwindPacked(Context, Entry->UnwindData, &PcRestored);
    }
    else
    {
        Xdata = (PULONG)(ImageBase + Entry->UnwindData);
        Header = Xdata[0];
        EpilogCount = (Header >> 23) & 0x1F;
        CodeWords = Header >> 28;
        Scopes = &Xdata[1];

        if (!EpilogCount && !CodeWords)
        {
            EpilogCount = Xdata[1] & 0xFFFF;
            CodeWords = (Xdata[1] >> 16) & 0xFF;
            Scopes++;
        }

        /* with E set the count is the code index of the only epilog, no scope words follow */
        Codes = (PUCHAR)((Header & (1 << 21)) ? Scopes : Scopes + EpilogCount);

        /* inside the prolog only what already ran is undone, inside an epilog what is left */
        {
            ULONG Offset = (ULONG)((ControlPc & ~1) - ImageBase - (Entry->BeginAddress & ~1));
            ULONG Length = (Header & 0x3FFFF) * 2;
            ULONG Size = CodeWords * 4, Bytes, Count, Ran, i;
            ULONG Sizes[32];

            Count = RtlpArmCountCodes(Codes, Size, 0, Sizes, RTL_NUMBER_OF(Sizes), &Bytes);
            if (Offset < Bytes && Count <= RTL_NUMBER_OF(Sizes))
            {
                /* the prolog runs the codes backwards, count from the last one */
                for (Ran = 0, i = Count; i > 0 && Offset >= Sizes[i - 1]; i--)
                {
                    Offset -= Sizes[i - 1];
                    Ran++;
                }
                Start = RtlpArmSkipCodes(Codes, Size, 0, Count - Ran);
                InBody = FALSE;
            }
            else if (Header & (1 << 21))
            {
                ULONG Index = EpilogCount;

                RtlpArmCountCodes(Codes, Size, Index, NULL, 0, &Bytes);
                if (Offset >= Length - Bytes)
                {
                    Start = RtlpArmEpilogStart(Codes, Size, Index, Offset - (Length - Bytes));
                    InBody = FALSE;
                }
            }
            else
            {
                for (i = 0; i < EpilogCount; i++)
                {
                    ULONG EpilogOffset = (Scopes[i] & 0x3FFFF) * 2;
                    ULONG Index = Scopes[i] >> 24;

                    RtlpArmCountCodes(Codes, Size, Index, NULL, 0, &Bytes);
                    if (Offset >= EpilogOffset && Offset < EpilogOffset + Bytes)
                    {
                        Start = RtlpArmEpilogStart(Codes, Size, Index, Offset - EpilogOffset);
                        InBody = FALSE;
                        break;
                    }
                }
            }
        }

        if (!RtlpArmExecuteCodes(Context, Codes, CodeWords * 4, Start, &PcRestored))
        {
            Context->Pc = 0;
            return NULL;
        }

        if (InBody && (Header & (1 << 20)))
        {
            PULONG HandlerRva = (PULONG)(Codes + CodeWords * 4);

            *HandlerData = HandlerRva + 1;
            Handler = (PEXCEPTION_ROUTINE)(ImageBase + *HandlerRva);
        }
    }

    if (!PcRestored)
        Context->Pc = Context->Lr;

    *EstablisherFrame = Context->Sp;
    return Handler;
}

/* clang builds the system without tables but keeps r11 as a frame chain,
 * [r11] holds the caller's r11 and [r11 + 4] the return address */
static
BOOLEAN
RtlpArmUnwindFramePointer(
    _Inout_ PCONTEXT Context)
{
    ULONG_PTR Low, High, Frame = Context->R11;

    RtlpGetStackLimits(&Low, &High);
    if ((Frame < Context->Sp) || (Frame + 8 > High) || (Frame & 3))
        return FALSE;

    Context->Pc = ((PULONG)Frame)[1];
    Context->R11 = ((PULONG)Frame)[0];
    Context->Sp = Frame + 8;
    return TRUE;
}

/* moves Context one frame up, filling in what the dispatchers need */
BOOLEAN
NTAPI
RtlpArmUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN PcIsUnwound,
    _Out_ PARM_FRAME_INFO Info)
{
    ULONG_PTR ControlPc = Context->Pc & ~1;
    ULONG_PTR LookupPc = PcIsUnwound ? ControlPc - 2 : ControlPc;
    ULONG_PTR Establisher;

    RtlZeroMemory(Info, sizeof(*Info));
    Info->ControlPc = ControlPc;

    /* the frame's own registers, the language handler reads them through the dispatcher context */
    RtlCopyMemory(Info->NonVolatile.R, &Context->R4, sizeof(Info->NonVolatile.R));
    RtlCopyMemory(Info->NonVolatile.D, &Context->D[8], sizeof(Info->NonVolatile.D));

    Info->FunctionEntry = RtlLookupFunctionEntry(LookupPc, &Info->ImageBase, NULL);
    if (Info->FunctionEntry)
    {
        Info->Handler = RtlVirtualUnwind(1 /* exception handler */,
                                         Info->ImageBase,
                                         ControlPc,
                                         Info->FunctionEntry,
                                         Context,
                                         &Info->HandlerData,
                                         &Establisher,
                                         NULL);
        if (!Context->Pc)
            return FALSE;
    }
    else
    {
        if (!RtlpArmUnwindFramePointer(Context))
            return FALSE;
        Establisher = Context->Sp;
    }

    Info->EstablisherFrame = Establisher;
    return TRUE;
}

VOID
NTAPI
RtlpArmFillDispatcherContext(
    _Out_ PDISPATCHER_CONTEXT DispatcherContext,
    _In_ PARM_FRAME_INFO Info,
    _In_ PCONTEXT ContextRecord,
    _In_ ULONG_PTR TargetPc,
    _In_ BOOLEAN PcIsUnwound)
{
    DispatcherContext->ControlPc = Info->ControlPc;
    DispatcherContext->ImageBase = (DWORD)Info->ImageBase;
    DispatcherContext->FunctionEntry = Info->FunctionEntry;
    DispatcherContext->EstablisherFrame = (DWORD)Info->EstablisherFrame;
    DispatcherContext->TargetPc = TargetPc;
    DispatcherContext->ContextRecord = ContextRecord;
    DispatcherContext->LanguageHandler = Info->Handler;
    DispatcherContext->HandlerData = Info->HandlerData;
    DispatcherContext->HistoryTable = NULL;
    DispatcherContext->ScopeIndex = 0;
    DispatcherContext->ControlPcIsUnwound = PcIsUnwound;
    DispatcherContext->NonVolatileRegisters = (PBYTE)&Info->NonVolatile;
    DispatcherContext->Reserved = 0;
}

VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ PCONTEXT ContextRecord,
    _In_opt_ PVOID HistoryTable)
{
    EXCEPTION_RECORD LocalRecord;
    DISPATCHER_CONTEXT DispatcherContext;
    ARM_FRAME_INFO Info;
    CONTEXT Current, Frame;
    PARM_DISPATCH_CONTEXT Dispatch;
    BOOLEAN PcIsUnwound = TRUE;
    ULONG_PTR Low, High;

    UNREFERENCED_PARAMETER(HistoryTable);

    if (!ExceptionRecord)
    {
        RtlZeroMemory(&LocalRecord, sizeof(LocalRecord));
        LocalRecord.ExceptionCode = STATUS_UNWIND;
        LocalRecord.ExceptionAddress = (PVOID)_ReturnAddress();
        ExceptionRecord = &LocalRecord;
    }

    ExceptionRecord->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (!TargetFrame)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    /* from the dispatcher the walk starts where the exception was raised, the frames
     * between are about to go anyway, otherwise it starts at the caller */
    Dispatch = CONTAINING_RECORD(ContextRecord, ARM_DISPATCH_CONTEXT, Context);
    if (ContextRecord && (Dispatch->Magic == ARM_DISPATCH_MAGIC))
    {
        Current = Dispatch->Context;
        PcIsUnwound = Dispatch->PcIsUnwound;
    }
    else
    {
        RtlCaptureContext(&Current);
    }

    RtlpGetStackLimits(&Low, &High);

    for (;;)
    {
        Frame = Current;
        if (!RtlpArmUnwindFrame(&Current, PcIsUnwound, &Info) ||
            (Info.EstablisherFrame < Frame.Sp) || (Info.EstablisherFrame > High))
        {
            DPRINT1("RtlUnwindEx: lost the frame chain at %p\n", (PVOID)Frame.Pc);
            RtlRaiseStatus(STATUS_BAD_STACK);
        }

        /* system frames below this one registered their handlers in the chain */
        RtlUnwind((PVOID)Info.EstablisherFrame, NULL, ExceptionRecord, NULL);

        if (TargetFrame && (Info.EstablisherFrame == (ULONG_PTR)TargetFrame))
            ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;

        if (Info.Handler)
        {
            RtlpArmFillDispatcherContext(&DispatcherContext, &Info, &Frame, (ULONG_PTR)TargetIp, PcIsUnwound);
            Info.Handler(ExceptionRecord, (PVOID)Info.EstablisherFrame, &Frame, &DispatcherContext);
        }

        if (TargetFrame && (Info.EstablisherFrame >= (ULONG_PTR)TargetFrame))
            break;

        PcIsUnwound = TRUE;
    }

    /* resume in the target frame, the frame context is the state its body had */
    Frame.Pc = (ULONG)TargetIp;
    Frame.R0 = (ULONG)ReturnValue;
    RtlpRestoreContext(&Frame);
}

/* EOF */
