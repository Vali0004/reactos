/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HAL extension interface, CSRT layout and SoC resource registration
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* binary compatible with the windows 8 hal extension interface, from its public docs
 * interrupt controllers register as early windows 8 allowed, the size gives the revision */

#ifdef __cplusplus
extern "C" {
#endif

#include <pshpack1.h>

/* CORE SYSTEM RESOURCE TABLE ************************************************/

#define CSRT_SIGNATURE 'TRSC'

typedef struct _CSRT_RESOURCE_GROUP_HEADER
{
    ULONG Length;
    ULONG VendorId;
    ULONG SubvendorId;
    USHORT DeviceId;
    USHORT SubdeviceId;
    USHORT Revision;
    USHORT Reserved;
    ULONG SharedInfoLength;
} CSRT_RESOURCE_GROUP_HEADER, *PCSRT_RESOURCE_GROUP_HEADER;

typedef struct _CSRT_RESOURCE_DESCRIPTOR_HEADER
{
    ULONG Length;
    USHORT Type;
    USHORT Subtype;
    ULONG Uid;
} CSRT_RESOURCE_DESCRIPTOR_HEADER, *PCSRT_RESOURCE_DESCRIPTOR_HEADER;

#include <poppack.h>

#define CSRT_RD_TYPE_ANY                    0
#define CSRT_RD_TYPE_INTERRUPT              1
#define CSRT_RD_TYPE_TIMER                  2
#define CSRT_RD_TYPE_DMA                    3
#define CSRT_RD_TYPE_CACHE                  4

#define CSRT_RD_SUBTYPE_ANY                 0xFFFF
#define CSRT_RD_SUBTYPE_INTERRUPT_LINES     0
#define CSRT_RD_SUBTYPE_INTERRUPT_CONTROLLER 1
#define CSRT_RD_SUBTYPE_TIMER               0
#define CSRT_RD_SUBTYPE_DMA_CHANNEL         0
#define CSRT_RD_SUBTYPE_DMA_CONTROLLER      1

#define CSRT_RD_UID_ANY                     0
#define CSRT_RD_UID_RESERVED                0xFFFFFFFF

/* shared info sits between the group header and its first descriptor */
#define CSRT_GROUP_SHARED_INFO(Group) \
    ((PVOID)((PUCHAR)(Group) + sizeof(CSRT_RESOURCE_GROUP_HEADER)))

/* SOC RESOURCE BLOCKS *******************************************************/

typedef struct _SOC_INITIALIZATION_HEADER
{
    ULONG TableVersion;
    ULONG TableSize;
} SOC_INITIALIZATION_HEADER, *PSOC_INITIALIZATION_HEADER;

/* timers */

#define TIMER_INITIALIZATION_VERSION 1

typedef enum _TIMER_MODE
{
    TimerModeInvalid,
    TimerModePseudoPeriodic,
    TimerModePeriodic,
    TimerModeOneShot
} TIMER_MODE, *PTIMER_MODE;

typedef enum _KNOWN_TIMER_TYPE
{
    TimerInvalid,
    TimerAcpi,
    TimerCmosRtc,
    TimerHpet,
    Timer8254,
    TimerProcessor,
    TimerSfi,
    TimerApic,
    TimerHypervisor,
    TimerBrokenAcpi,
    TimerGp,
    TimerSp804,
    TimerCycleCounter,
    TimerUnknown = 0x1000
} KNOWN_TIMER_TYPE, *PKNOWN_TIMER_TYPE;

#define TIMER_PER_PROCESSOR                     0x00000001
#define TIMER_COUNTER_READABLE                  0x00000002
#define TIMER_COUNTER_WRITABLE                  0x00000004
#define TIMER_HAS_DIVISOR                       0x00000008
#define TIMER_PSEUDO_PERIODIC_CAPABLE           0x00000010
#define TIMER_ONE_SHOT_CAPABLE                  0x00000020
#define TIMER_PERIODIC_CAPABLE                  0x00000040
#define TIMER_ROLLOVER_INTERRUPTS_ONLY          0x00000080
#define TIMER_GENERATES_8259_INTERRUPTS         0x00000100
#define TIMER_GENERATES_LINE_BASED_INTERRUPTS   0x00000200
#define TIMER_GENERATES_MSI_INTERRUPTS          0x00000400
#define TIMER_GENERATES_INTERNAL_INTERRUPTS     0x00000800
#define TIMER_LOSSLESS_RATE_CHANGE              0x00001000
#define TIMER_PERFORMANCE_STATE_VARIANT         0x00002000
#define TIMER_IDLE_STATE_VARIANT                0x00004000
#define TIMER_VALID_CAPABILITIES                0x00007FFF
#define TIMER_UPDATABLE_CAPABILITIES \
    (TIMER_PERFORMANCE_STATE_VARIANT | TIMER_IDLE_STATE_VARIANT)

typedef NTSTATUS (*PTIMER_INITIALIZE)(_In_ PVOID TimerData);
typedef ULONGLONG (*PTIMER_QUERY_COUNTER)(_In_ PVOID TimerData);
typedef VOID (*PTIMER_ACKNOWLEDGE_INTERRUPT)(_In_ PVOID TimerData);
typedef NTSTATUS (*PTIMER_CORE_TIMER)(_In_ PVOID TimerData, _In_ TIMER_MODE Mode, _In_ ULONGLONG TickCount);
typedef VOID (*PTIMER_STOP)(_In_ PVOID TimerData);
typedef VOID (*PTIMER_SET_DIVISOR)(_In_ PVOID TimerData, _In_ ULONG Divisor);
typedef NTSTATUS (*PTIMER_SET_MESSAGE_INTERRUPT_ROUTING)(
    _In_ PVOID TimerData,
    _In_ BOOLEAN Enable,
    _In_ PHYSICAL_ADDRESS MessageAddress,
    _In_ ULONG MessageData);
typedef ULONG (*PTIMER_LOSSLESS_RATE_CHANGE)(_In_ PVOID TimerData, _In_ ULONGLONG NewRate);
typedef VOID (*PTIMER_SET_INTERRUPT_VECTOR)(_In_ PVOID TimerData, _In_ ULONG Vector);
typedef ULONGLONG (*PTIMER_FIXED_STALL)(_In_ PVOID TimerData, _In_ ULONGLONG Minimum100ns);

typedef struct _TIMER_FUNCTION_TABLE
{
    PTIMER_INITIALIZE Initialize;
    PTIMER_QUERY_COUNTER QueryCounter;
    PTIMER_ACKNOWLEDGE_INTERRUPT AcknowledgeInterrupt;
    PTIMER_CORE_TIMER ArmTimer;
    PTIMER_STOP Stop;
    PTIMER_SET_DIVISOR SetDivisor;
    PTIMER_SET_MESSAGE_INTERRUPT_ROUTING SetMessageInterruptRouting;
    PTIMER_LOSSLESS_RATE_CHANGE LosslessRateChange;
    PTIMER_SET_INTERRUPT_VECTOR SetInterruptVector;
    PTIMER_FIXED_STALL FixedStall;
} TIMER_FUNCTION_TABLE, *PTIMER_FUNCTION_TABLE;

typedef struct _TIMER_INTERRUPT
{
    LONG LegacyIrq;
    LONG Gsi;
    KINTERRUPT_POLARITY Polarity;
    KINTERRUPT_MODE Mode;
} TIMER_INTERRUPT, *PTIMER_INTERRUPT;

typedef struct _TIMER_INITIALIZATION_BLOCK
{
    SOC_INITIALIZATION_HEADER Header;
    TIMER_FUNCTION_TABLE FunctionTable;
    PVOID InternalData;
    ULONG InternalDataSize;
    ULONG CounterBitWidth;
    ULONGLONG CounterFrequency;
    ULONG MaxDivisor;
    ULONG Capabilities;
    TIMER_INTERRUPT Interrupt;
    KNOWN_TIMER_TYPE KnownType;
    ULONG Identifier;
} TIMER_INITIALIZATION_BLOCK, *PTIMER_INITIALIZATION_BLOCK;

/* interrupt controllers */

#define INTERRUPT_INITIALIZATION_VERSION 1
#define INTERRUPT_CPU_UNIT_ID 0xFFFFFFFF

typedef enum _KNOWN_CONTROLLER_TYPE
{
    InterruptControllerInvalid,
    InterruptControllerPic,
    InterruptControllerApic,
    InterruptControllerGic,
    InterruptControllerUnknown = 0x1000
} KNOWN_CONTROLLER_TYPE, *PKNOWN_CONTROLLER_TYPE;

typedef enum _SOC_INTERRUPT_TARGET_TYPE
{
    InterruptTargetInvalid,
    InterruptTargetAllIncludingSelf,
    InterruptTargetAllExcludingSelf,
    InterruptTargetSelfOnly,
    InterruptTargetPhysical,
    InterruptTargetLogicalFlat,
    InterruptTargetLogicalClustered,
    InterruptTargetRemapIndex
} SOC_INTERRUPT_TARGET_TYPE, *PSOC_INTERRUPT_TARGET_TYPE;

typedef enum _INTERRUPT_LINE_TYPE
{
    InterruptLineInvalidType,
    InterruptLineUnusable,
    InterruptLineStandardPin,
    InterruptLineProcessorLocal,
    InterruptLineSoftwareOnly,
    InterruptLineSoftwareOnlyProcessorLocal,
    InterruptLineOutputPin
} INTERRUPT_LINE_TYPE, *PINTERRUPT_LINE_TYPE;

typedef enum _INTERRUPT_RESULT
{
    InterruptBeginFatalError,
    InterruptBeginLine,
    InterruptBeginSpurious
} INTERRUPT_RESULT, *PINTERRUPT_RESULT;

#define INTERRUPT_CONTROLLER_HAS_LOCAL_UNIT         0x00000001
#define INTERRUPT_CONTROLLER_HAS_PRIORITY_SCHEME    0x00000002
#define INTERRUPT_CONTROLLER_LOGICAL_FLAT           0x00000004
#define INTERRUPT_CONTROLLER_LOGICAL_CLUSTERED      0x00000008
#define INTERRUPT_CONTROLLER_SHORTHAND_ALL          0x00000010
#define INTERRUPT_CONTROLLER_SHORTHAND_OTHERS       0x00000020
#define INTERRUPT_CONTROLLER_SHORTHAND_SELF         0x00000040
#define INTERRUPT_CONTROLLER_SUPPORTS_REMAPPING     0x00000080
#define INTERRUPT_CONTROLLER_REQUIRES_REMAPPING     0x00000100
#define INTERRUPT_CONTROLLER_VALID_CAPABILITIES     0x000001FF

#define INTERRUPT_LINE_ENABLED                      0x00000001
#define INTERRUPT_LINE_DELIVER_LOWEST_PRIORITY      0x00000002

typedef struct _INTERRUPT_LINE
{
    ULONG UnitId;
    LONG Line;
} INTERRUPT_LINE, *PINTERRUPT_LINE;

typedef struct _INTERRUPT_TARGET
{
    SOC_INTERRUPT_TARGET_TYPE Target;
    union
    {
        ULONG PhysicalTarget;
        ULONG LogicalFlatTarget;
        ULONG RemapIndex;
        struct
        {
            ULONG ClusterId;
            ULONG ClusterMask;
        };
    };
} INTERRUPT_TARGET, *PINTERRUPT_TARGET;

typedef struct _INTERRUPT_LINE_STATE
{
    KINTERRUPT_POLARITY Polarity;
    KINTERRUPT_MODE TriggerMode;
    ULONG Flags;
    INTERRUPT_LINE Routing;
    INTERRUPT_TARGET ProcessorTarget;
    ULONG Vector;
    ULONG Priority;
} INTERRUPT_LINE_STATE, *PINTERRUPT_LINE_STATE;

typedef NTSTATUS (*PINTERRUPT_INITIALIZE_LOCAL_UNIT)(
    _In_ PVOID ControllerData,
    _In_ ULONG ProcessorNumber,
    _In_ ULONG SpuriousVector,
    _In_ ULONG StubVector,
    _In_ ULONG LocalErrorVector,
    _Out_ PULONG LocalId);
typedef NTSTATUS (*PINTERRUPT_INITIALIZE_IO_UNIT)(_In_ PVOID ControllerData);
typedef VOID (*PINTERRUPT_SET_PRIORITY)(_In_ PVOID ControllerData, _In_ ULONG NewPriority);
typedef ULONG (*PINTERRUPT_GET_LOCAL_UNIT_ERROR)(_In_ PVOID ControllerData);
typedef VOID (*PINTERRUPT_CLEAR_LOCAL_UNIT_ERROR)(_In_ PVOID ControllerData);
typedef NTSTATUS (*PINTERRUPT_GET_LOGICAL_ID)(_In_ PVOID ControllerData, _Out_ PINTERRUPT_TARGET Target);
typedef NTSTATUS (*PINTERRUPT_SET_LOGICAL_ID)(_In_ PVOID ControllerData, _In_ PINTERRUPT_TARGET Target);
typedef INTERRUPT_RESULT (*PINTERRUPT_ACCEPT_AND_GET_SOURCE)(
    _In_ PVOID ControllerData,
    _Out_ PLONG Line,
    _Out_ PULONG OpaqueData);
typedef VOID (*PINTERRUPT_END_OF_INTERRUPT)(_In_ PVOID ControllerData, _In_ ULONG OpaqueToken);
typedef VOID (*PINTERRUPT_FAST_END_OF_INTERRUPT)(VOID);
typedef NTSTATUS (*PINTERRUPT_SET_LINE_STATE)(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_LINE_STATE NewState);
typedef NTSTATUS (*PINTERRUPT_REQUEST_INTERRUPT)(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE Line,
    _In_ PINTERRUPT_TARGET Target,
    _In_ ULONG Vector,
    _In_ PINTERRUPT_LINE OutputLine);
typedef NTSTATUS (*PINTERRUPT_START_PROCESSOR)(
    _In_ PVOID ControllerData,
    _In_ ULONG LocalUnitId,
    _In_ PVOID StartupCodeVirtual,
    _In_ ULONG StartupCodePhysical);
typedef NTSTATUS (*PINTERRUPT_GENERATE_MESSAGE)(
    _In_ PVOID ControllerData,
    _In_ PINTERRUPT_LINE_STATE LineState,
    _Out_ PULONGLONG MessageAddress,
    _Out_ PULONGLONG MessageData);
typedef NTSTATUS (*PINTERRUPT_CONVERT_ID)(
    _In_ PVOID ControllerData,
    _Inout_ PULONG PhysicalId,
    _Inout_ PINTERRUPT_TARGET LogicalTarget,
    _In_ BOOLEAN ToLogical);
typedef NTSTATUS (*PINTERRUPT_SAVE_LOCAL_INTERRUPTS)(_In_ PVOID ControllerData, _Inout_ PVOID SaveBuffer);
typedef NTSTATUS (*PINTERRUPT_REPLAY_LOCAL_INTERRUPTS)(_In_ PVOID ControllerData, _In_ PVOID SaveBuffer);
typedef NTSTATUS (*PINTERRUPT_DEINITIALIZE_LOCAL_UNIT)(_In_ PVOID ControllerData);
typedef NTSTATUS (*PINTERRUPT_DEINITIALIZE_IO_UNIT)(_In_ PVOID ControllerData);
typedef INTERRUPT_RESULT (*PINTERRUPT_QUERY_AND_GET_SOURCE)(
    _In_ PVOID ControllerData,
    _Out_ PLONG Line,
    _Out_ PULONG OpaqueData);

/* the base table, later revisions append entries after it */
typedef struct _INTERRUPT_FUNCTION_TABLE
{
    PINTERRUPT_INITIALIZE_LOCAL_UNIT InitializeLocalUnit;
    PINTERRUPT_INITIALIZE_IO_UNIT InitializeIoUnit;
    PINTERRUPT_SET_PRIORITY SetPriority;
    PINTERRUPT_GET_LOCAL_UNIT_ERROR GetLocalUnitError;
    PINTERRUPT_CLEAR_LOCAL_UNIT_ERROR ClearLocalUnitError;
    PINTERRUPT_GET_LOGICAL_ID GetLogicalId;
    PINTERRUPT_SET_LOGICAL_ID SetLogicalId;
    PINTERRUPT_ACCEPT_AND_GET_SOURCE AcceptAndGetSource;
    PINTERRUPT_END_OF_INTERRUPT EndOfInterrupt;
    PINTERRUPT_FAST_END_OF_INTERRUPT FastEndOfInterrupt;
    PINTERRUPT_SET_LINE_STATE SetLineState;
    PINTERRUPT_REQUEST_INTERRUPT RequestInterrupt;
    PINTERRUPT_START_PROCESSOR StartProcessor;
    PINTERRUPT_GENERATE_MESSAGE GenerateMessage;
    PINTERRUPT_CONVERT_ID ConvertId;
    PINTERRUPT_SAVE_LOCAL_INTERRUPTS SaveLocalInterrupts;
    PINTERRUPT_REPLAY_LOCAL_INTERRUPTS ReplayLocalInterrupts;
} INTERRUPT_FUNCTION_TABLE, *PINTERRUPT_FUNCTION_TABLE;

typedef struct _INTERRUPT_FUNCTION_TABLE_EX
{
    INTERRUPT_FUNCTION_TABLE Base;
    PINTERRUPT_DEINITIALIZE_LOCAL_UNIT DeinitializeLocalUnit;
    PINTERRUPT_DEINITIALIZE_IO_UNIT DeinitializeIoUnit;
    PINTERRUPT_QUERY_AND_GET_SOURCE QueryAndGetSource;
} INTERRUPT_FUNCTION_TABLE_EX, *PINTERRUPT_FUNCTION_TABLE_EX;

/* follows the function table, whose length depends on the revision */
typedef struct _INTERRUPT_CONTROLLER_DESCRIPTION
{
    PVOID InternalData;
    ULONG InternalDataSize;
    KNOWN_CONTROLLER_TYPE KnownType;
    ULONG Identifier;
    ULONG Capabilities;
    ULONG MaxPriority;
    ULONG MaxClusterSize;
    ULONG MaxClusters;
    ULONG InterruptReplayDataSize;
} INTERRUPT_CONTROLLER_DESCRIPTION, *PINTERRUPT_CONTROLLER_DESCRIPTION;

typedef struct _INTERRUPT_INITIALIZATION_BLOCK
{
    SOC_INITIALIZATION_HEADER Header;
    INTERRUPT_FUNCTION_TABLE FunctionTable;
    PVOID InternalData;
    ULONG InternalDataSize;
    KNOWN_CONTROLLER_TYPE KnownType;
    ULONG Identifier;
    ULONG Capabilities;
    ULONG MaxPriority;
    ULONG MaxClusterSize;
    ULONG MaxClusters;
    ULONG InterruptReplayDataSize;
} INTERRUPT_INITIALIZATION_BLOCK, *PINTERRUPT_INITIALIZATION_BLOCK;

typedef struct _INTERRUPT_LINE_DESCRIPTION
{
    ULONG ControllerIdentifier;
    LONG MinLine;
    LONG MaxLine;
    INTERRUPT_LINE_TYPE Type;
    ULONG Capabilities;
    ULONG OutputUnitId;
    ULONG GsiBase;
} INTERRUPT_LINE_DESCRIPTION, *PINTERRUPT_LINE_DESCRIPTION;

/* dma controllers and channels */

#define DMA_PORT_WIDTH_UNLIMITED    0x01
#define DMA_PORT_WIDTH_8BIT         0x02
#define DMA_PORT_WIDTH_16BIT        0x04
#define DMA_PORT_WIDTH_32BIT        0x08
#define DMA_PORT_WIDTH_64BIT        0x10

typedef enum _DMA_INTERRUPT_TYPE
{
    InterruptTypeCompletion,
    InterruptTypeError
} DMA_INTERRUPT_TYPE, *PDMA_INTERRUPT_TYPE;

typedef struct _DMA_REQUEST_LINE_BINDING_DESCRIPTION
{
    ULONG RequestLine;
    ULONG ChannelNumber;
} DMA_REQUEST_LINE_BINDING_DESCRIPTION, *PDMA_REQUEST_LINE_BINDING_DESCRIPTION;

typedef VOID (*PDMA_INITIALIZE_CONTROLLER)(_In_ PVOID ControllerContext);
typedef BOOLEAN (*PDMA_VALIDATE_REQUEST_LINE_BINDING)(
    _In_ PVOID ControllerContext,
    _In_ PDMA_REQUEST_LINE_BINDING_DESCRIPTION Binding);
typedef VOID (*PDMA_PROGRAM_CHANNEL)(
    _In_ PVOID ControllerContext,
    _In_ ULONG ChannelNumber,
    _In_ ULONG RequestLine,
    _In_ PHYSICAL_ADDRESS MemoryAddress,
    _In_ PHYSICAL_ADDRESS DeviceAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice,
    _In_ ULONG PortWidth);
typedef NTSTATUS (*PDMA_CONFIGURE_CHANNEL)(
    _In_ PVOID ControllerContext,
    _In_ ULONG ChannelNumber,
    _In_ ULONG FunctionNumber,
    _In_ PVOID Context);
typedef VOID (*PDMA_FLUSH_CHANNEL)(_In_ PVOID ControllerContext, _In_ ULONG ChannelNumber);
typedef BOOLEAN (*PDMA_HANDLE_INTERRUPT)(
    _In_ PVOID ControllerContext,
    _Out_ PULONG ChannelNumber,
    _Out_ PDMA_INTERRUPT_TYPE InterruptType);

typedef struct _DMA_FUNCTION_TABLE
{
    PDMA_INITIALIZE_CONTROLLER InitializeController;
    PDMA_VALIDATE_REQUEST_LINE_BINDING ValidateRequestLineBinding;
    PDMA_PROGRAM_CHANNEL ProgramChannel;
    PDMA_CONFIGURE_CHANNEL ConfigureChannel;
    PDMA_FLUSH_CHANNEL FlushChannel;
    PDMA_HANDLE_INTERRUPT HandleInterrupt;
} DMA_FUNCTION_TABLE, *PDMA_FUNCTION_TABLE;

typedef struct _DMA_INITIALIZATION_BLOCK
{
    ULONG ControllerId;
    ULONG ChannelCount;
    BOOLEAN CacheCoherent;
    ULONG DmaAddressWidth;
    ULONG SupportedPortWidths;
    ULONG MinimumRequestLine;
    ULONG MaximumRequestLine;
    BOOLEAN GeneratesInterrupt;
    LONG Gsi;
    KINTERRUPT_POLARITY InterruptPolarity;
    KINTERRUPT_MODE InterruptMode;
    PVOID InternalData;
    ULONG InternalDataSize;
    PDMA_FUNCTION_TABLE Operations;
} DMA_INITIALIZATION_BLOCK, *PDMA_INITIALIZATION_BLOCK;

typedef struct _DMA_CHANNEL_INITIALIZATION_BLOCK
{
    ULONG ControllerId;
    BOOLEAN GeneratesInterrupt;
    LONG Gsi;
    KINTERRUPT_POLARITY InterruptPolarity;
    KINTERRUPT_MODE InterruptMode;
    ULONG ChannelNumber;
} DMA_CHANNEL_INITIALIZATION_BLOCK, *PDMA_CHANNEL_INITIALIZATION_BLOCK;

/* EXTENSION ENTRY ***********************************************************/

typedef enum _OS_EXTENSION_INTERFACE
{
    OsExtensionHalExtension = 0
} OS_EXTENSION_INTERFACE;

typedef NTSTATUS (*PHALEXT_ADD_RESOURCE_GROUP)(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup);
typedef VOID (*PHALEXT_GET_SECURITY_COOKIE)(
    _Out_ PULONG *SecurityCookie,
    _Out_ PULONG *SecurityCookieComplement);

/* filled by the extension, read by the hal */
typedef struct _OS_EXTENSION_HAL_EXTENSION_EXPORTS
{
    PHALEXT_ADD_RESOURCE_GROUP AddResourceGroup;
    PHALEXT_GET_SECURITY_COOKIE GetSecurityCookie;
} OS_EXTENSION_HAL_EXTENSION_EXPORTS, *POS_EXTENSION_HAL_EXTENSION_EXPORTS;

typedef NTSTATUS (*PHALEXT_REGISTER_RESOURCE_DESCRIPTOR)(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    _In_ PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    _In_opt_ PVOID ResourceDescriptorInfo);
typedef PCSRT_RESOURCE_DESCRIPTOR_HEADER (*PHALEXT_GET_NEXT_RESOURCE_DESCRIPTOR)(
    _In_ ULONG Handle,
    _In_ PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    _In_opt_ PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    _In_ USHORT ResourceType,
    _In_ USHORT ResourceSubtype,
    _In_ ULONG ResourceId);
typedef PVOID (*PHALEXT_GET_ACPI_TABLE)(
    _In_ ULONG Handle,
    _In_ ULONG Signature,
    _In_opt_ PCSTR OemId,
    _In_opt_ PCSTR OemTableId);
typedef PVOID (*PHALEXT_MAP_IO_SPACE)(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType);
typedef VOID (*PHALEXT_UNMAP_IO_SPACE)(_In_ PVOID VirtualAddress, _In_ SIZE_T NumberOfBytes);
typedef NTSTATUS (*PHALEXT_REGISTER_PERMANENT_ADDRESS_USAGE)(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG SizeInBytes);
typedef VOID (*PHALEXT_SET_PROBLEM)(_In_ PVOID InternalData, _In_ ULONG ProblemCode, _In_ NTSTATUS ProblemStatus);
typedef NTSTATUS (*PHALEXT_FIX_INTERRUPT_LINE)(_In_ PINTERRUPT_LINE Line, _In_ PINTERRUPT_LINE_STATE State);
typedef NTSTATUS (*PHALEXT_UPDATE_TIMER_CAPABILITIES)(
    _In_ PVOID TimerData,
    _In_ ULONG SetCapabilities,
    _In_ ULONG ClearCapabilities);
typedef VOID (NTAPI *PHALEXT_RAISE_EXCEPTION)(_In_ PEXCEPTION_RECORD ExceptionRecord);

/* filled by the hal in this order, extensions index it directly */
typedef struct _OS_EXTENSION_HAL_EXTENSION_IMPORTS
{
    PHALEXT_REGISTER_RESOURCE_DESCRIPTOR RegisterResourceDescriptor;
    PHALEXT_GET_NEXT_RESOURCE_DESCRIPTOR GetNextResourceDescriptor;
    PHALEXT_GET_ACPI_TABLE GetAcpiTable;
    PHALEXT_MAP_IO_SPACE HalMapIoSpace;
    PHALEXT_UNMAP_IO_SPACE HalUnmapIoSpace;
    PHALEXT_REGISTER_PERMANENT_ADDRESS_USAGE HalRegisterPermanentAddressUsage;
    PHALEXT_SET_PROBLEM SetTimerProblem;
    PHALEXT_SET_PROBLEM SetInterruptProblem;
    PHALEXT_FIX_INTERRUPT_LINE FixInterruptLine;
    PHALEXT_UPDATE_TIMER_CAPABILITIES UpdateTimerCapabilities;
    PHALEXT_RAISE_EXCEPTION RtlRaiseException;
} OS_EXTENSION_HAL_EXTENSION_IMPORTS, *POS_EXTENSION_HAL_EXTENSION_IMPORTS;

/* the image entry point of every hal extension */
typedef NTSTATUS (*PHALEXT_INIT)(
    _In_ OS_EXTENSION_INTERFACE Interface,
    _Out_ POS_EXTENSION_HAL_EXTENSION_EXPORTS *Exports,
    _In_ POS_EXTENSION_HAL_EXTENSION_IMPORTS Imports);

#ifdef __cplusplus
}
#endif
