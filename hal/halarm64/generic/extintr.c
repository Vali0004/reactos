/*
 * PROJECT:     ReactOS ARM HAL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Interrupt handling through a controller a HAL extension registered
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "halextp.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* set once phase 0 picked an extension controller over the gic */
BOOLEAN HalpExtInterrupts;

static PHALEXT_INTERRUPT_CONTROLLER HalpExtController;

/* software interrupts go out on software only lines, one per kernel vector */
static const KIRQL HalpExtSoftwareVector[] = { APC_LEVEL, DISPATCH_LEVEL, IPI_LEVEL };
static INTERRUPT_LINE HalpExtSoftwareLine[IPI_LEVEL + 1];

/* interrupts nest, each level keeps the token its end of interrupt needs */
#define HALEXT_NEST_DEPTH 16
static ULONG HalpExtTokens[MAXIMUM_PROCESSORS][HALEXT_NEST_DEPTH];
static BOOLEAN HalpExtTokenSoftware[MAXIMUM_PROCESSORS][HALEXT_NEST_DEPTH];
static ULONG HalpExtDepth[MAXIMUM_PROCESSORS];

/* FUNCTIONS *****************************************************************/

/* extension priorities rise with importance, irqls map onto their range */
static
ULONG
HalpExtIrqlToPriority(
    _In_ KIRQL Irql)
{
    ULONG MaxPriority = HalpExtController->Description.MaxPriority;

    if (!MaxPriority || (MaxPriority >= HIGH_LEVEL))
        return Irql;

    return ((ULONG)Irql * MaxPriority) / HIGH_LEVEL;
}

/* a gsi belongs to the line range whose base covers it */
static
PINTERRUPT_LINE_DESCRIPTION
HalpExtFindGsi(
    _In_ ULONG Gsi,
    _Out_ PINTERRUPT_LINE Line)
{
    ULONG i;

    for (i = 0; i < HalpExtLineCount; i++)
    {
        PINTERRUPT_LINE_DESCRIPTION Range = &HalpExtLines[i];
        ULONG Count = (ULONG)(Range->MaxLine - Range->MinLine);

        if (Range->GsiBase == 0xFFFFFFFF)
            continue;

        if ((Gsi >= Range->GsiBase) && (Gsi < Range->GsiBase + Count))
        {
            Line->UnitId = Range->ControllerIdentifier;
            Line->Line = Range->MinLine + (LONG)(Gsi - Range->GsiBase);
            return Range;
        }
    }

    return NULL;
}

static
PINTERRUPT_LINE_DESCRIPTION
HalpExtFindLine(
    _In_ LONG Line)
{
    ULONG i;

    for (i = 0; i < HalpExtLineCount; i++)
    {
        if ((Line >= HalpExtLines[i].MinLine) && (Line < HalpExtLines[i].MaxLine))
            return &HalpExtLines[i];
    }

    return NULL;
}

/* the first three software only lines carry apc, dpc and ipi */
static
BOOLEAN
HalpExtPickSoftwareLines(VOID)
{
    ULONG i, Next = 0;
    LONG Line;

    for (i = 0; (i < HalpExtLineCount) && (Next < RTL_NUMBER_OF(HalpExtSoftwareVector)); i++)
    {
        PINTERRUPT_LINE_DESCRIPTION Range = &HalpExtLines[i];

        if ((Range->Type != InterruptLineSoftwareOnly) &&
            (Range->Type != InterruptLineSoftwareOnlyProcessorLocal))
        {
            continue;
        }

        for (Line = Range->MinLine;
             (Line < Range->MaxLine) && (Next < RTL_NUMBER_OF(HalpExtSoftwareVector));
             Line++)
        {
            KIRQL Vector = HalpExtSoftwareVector[Next++];

            HalpExtSoftwareLine[Vector].UnitId = Range->ControllerIdentifier;
            HalpExtSoftwareLine[Vector].Line = Line;
        }
    }

    return (Next == RTL_NUMBER_OF(HalpExtSoftwareVector));
}

static
NTSTATUS
HalpExtSetLine(
    _In_ PINTERRUPT_LINE Line,
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode,
    _In_ BOOLEAN Enable)
{
    INTERRUPT_LINE_STATE State;

    RtlZeroMemory(&State, sizeof(State));
    State.Polarity = InterruptActiveHigh;
    State.TriggerMode = Mode;
    State.Flags = Enable ? INTERRUPT_LINE_ENABLED : 0;
    State.Routing = *Line;
    State.ProcessorTarget.Target = InterruptTargetPhysical;
    State.ProcessorTarget.PhysicalTarget = 0;
    State.Vector = Vector;
    State.Priority = HalpExtIrqlToPriority(Irql);

    return HalpExtController->FunctionTable.Base.SetLineState(
        HalpExtController->Description.InternalData, Line, &State);
}

/* picks the extension controller when one registered, FALSE keeps the gic */
BOOLEAN
NTAPI
HalpExtInterruptInitialize(VOID)
{
    PVOID Data;
    NTSTATUS Status;

    if (!HalpExtControllerCount)
        return FALSE;

    HalpExtController = &HalpExtControllers[0];
    Data = HalpExtController->Description.InternalData;

    if (!HalpExtPickSoftwareLines() || !HalpExtController->FunctionTable.Base.RequestInterrupt)
    {
        DPRINT1("HALEXT: the controller has no software lines, keeping the gic\n");
        return FALSE;
    }

    if (HalpExtController->FunctionTable.Base.InitializeIoUnit)
    {
        HalpEarlyTrace("HAL: extension io unit\n");
        Status = HalpExtController->FunctionTable.Base.InitializeIoUnit(Data);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HALEXT: InitializeIoUnit failed %lx, keeping the gic\n", Status);
            return FALSE;
        }
    }

    HalpExtInterrupts = TRUE;

    DPRINT1("HALEXT: interrupts go through the extension controller\n");
    return TRUE;
}

VOID
NTAPI
HalpExtInterruptInitializeCpu(
    _In_ ULONG ProcessorNumber)
{
    ULONG LocalId, i;
    NTSTATUS Status;
    KIRQL Vector;

    Status = HalpExtController->FunctionTable.Base.InitializeLocalUnit(
        HalpExtController->Description.InternalData,
        ProcessorNumber,
        0xFFFFFFFF,
        0xFFFFFFFF,
        0xFFFFFFFF,
        &LocalId);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x7178, ProcessorNumber, Status, 0);

    HalpEarlyTrace("HAL: extension local unit done, software lines\n");

    /* software lines can be banked per cpu, so they are set after its local unit */
    for (i = 0; i < RTL_NUMBER_OF(HalpExtSoftwareVector); i++)
    {
        Vector = HalpExtSoftwareVector[i];
        HalpExtSetLine(&HalpExtSoftwareLine[Vector], Vector, Vector, Latched, TRUE);
    }

    HalpExtDepth[ProcessorNumber] = 0;
}

VOID
NTAPI
HalpExtSetPriorityMask(
    _In_ KIRQL Irql)
{
    if (HalpExtController->FunctionTable.Base.SetPriority)
    {
        HalpExtController->FunctionTable.Base.SetPriority(
            HalpExtController->Description.InternalData,
            HalpExtIrqlToPriority(Irql));
    }
}

/* returns the kernel vector, a gsi or one of the software levels */
ULONG
NTAPI
HalpExtAcknowledge(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    PINTERRUPT_LINE_DESCRIPTION Range;
    INTERRUPT_RESULT Result;
    ULONG Token, i;
    KIRQL Vector;
    LONG Line;

    Result = HalpExtController->FunctionTable.Base.AcceptAndGetSource(
        HalpExtController->Description.InternalData, &Line, &Token);
    if (Result != InterruptBeginLine)
        return GIC_SPURIOUS_INTID;

    if (HalpExtDepth[Cpu] < HALEXT_NEST_DEPTH)
    {
        HalpExtTokens[Cpu][HalpExtDepth[Cpu]] = Token;
        HalpExtTokenSoftware[Cpu][HalpExtDepth[Cpu]] = FALSE;
    }
    HalpExtDepth[Cpu]++;

    for (i = 0; i < RTL_NUMBER_OF(HalpExtSoftwareVector); i++)
    {
        Vector = HalpExtSoftwareVector[i];
        if (HalpExtSoftwareLine[Vector].Line == Line)
        {
            if (HalpExtDepth[Cpu] <= HALEXT_NEST_DEPTH)
                HalpExtTokenSoftware[Cpu][HalpExtDepth[Cpu] - 1] = TRUE;
            return Vector;
        }
    }

    Range = HalpExtFindLine(Line);
    if (!Range || (Range->GsiBase == 0xFFFFFFFF))
        return GIC_SPURIOUS_INTID;

    return Range->GsiBase + (ULONG)(Line - Range->MinLine);
}

/* ends the innermost interrupt right away */
VOID
NTAPI
HalpExtEndOfInterrupt(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG Depth;

    if (!HalpExtDepth[Cpu])
        return;

    Depth = --HalpExtDepth[Cpu];
    if (Depth < HALEXT_NEST_DEPTH)
    {
        HalpExtController->FunctionTable.Base.EndOfInterrupt(
            HalpExtController->Description.InternalData,
            HalpExtTokens[Cpu][Depth]);
    }
}

VOID
NTAPI
HalpExtRequestSoftwareInterrupt(
    _In_ KIRQL Irql,
    _In_ KAFFINITY Targets)
{
    INTERRUPT_TARGET Target;

    if ((Irql < APC_LEVEL) || (Irql > IPI_LEVEL) || ((Irql > DISPATCH_LEVEL) && (Irql < IPI_LEVEL)))
        return;

    RtlZeroMemory(&Target, sizeof(Target));
    if (Targets == AFFINITY_MASK(KeGetCurrentProcessorNumber()))
    {
        Target.Target = InterruptTargetSelfOnly;
    }
    else
    {
        Target.Target = InterruptTargetLogicalFlat;
        Target.LogicalFlatTarget = (ULONG)Targets;
    }

    HalpExtController->FunctionTable.Base.RequestInterrupt(
        HalpExtController->Description.InternalData,
        &HalpExtSoftwareLine[Irql],
        &Target,
        Irql,
        NULL);
}

BOOLEAN
NTAPI
HalpExtEnableInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode)
{
    INTERRUPT_LINE Line;

    if (!HalpExtFindGsi(Vector, &Line))
        return FALSE;

    return NT_SUCCESS(HalpExtSetLine(&Line, Vector, Irql, Mode, TRUE));
}

VOID
NTAPI
HalpExtDisableInterrupt(
    _In_ ULONG Vector)
{
    INTERRUPT_LINE Line;

    if (HalpExtFindGsi(Vector, &Line))
        HalpExtSetLine(&Line, Vector, PASSIVE_LEVEL, LevelSensitive, FALSE);
}

/* ends the innermost interrupt once its isr ran, software ones ended at accept */
VOID
NTAPI
HalpExtEndDeviceInterrupt(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();
    ULONG Depth = HalpExtDepth[Cpu];

    if (!Depth)
        return;

    if ((Depth <= HALEXT_NEST_DEPTH) && HalpExtTokenSoftware[Cpu][Depth - 1])
        return;

    HalpExtEndOfInterrupt();
}
