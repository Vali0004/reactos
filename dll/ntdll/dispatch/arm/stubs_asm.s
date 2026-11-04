
#include <ksarm.h>

    IMPORT LdrpInit
    IMPORT ZwContinue
    IMPORT RtlRaiseStatus

    TEXTAREA

//
// VOID
// LdrInitializeThunk(
//     _In_ PVOID NormalContext,
//     _In_ PVOID SystemArgument1,
//     _In_ PVOID SystemArgument2,
//     _In_ PCONTEXT Context);
//
// every thread starts with this APC, KiUserApcDispatcher continues with
// the thread's context once the loader returns
//
    LEAF_ENTRY LdrInitializeThunk
    mov r0, r3
    b LdrpInit
    LEAF_END LdrInitializeThunk

//
// the kernel leaves the interrupted CONTEXT at sp, the normal routine in r3
// and its three arguments in r0-r2
//
    LEAF_ENTRY KiUserApcDispatcher
    mov r12, r3
    mov r3, sp
    blx r12

    mov r0, sp
    movs r1, #1
    bl ZwContinue

    /* only a failure comes back */
    bl RtlRaiseStatus
    __assertfail
    LEAF_END KiUserApcDispatcher

    END
