#pragma once

#include "list.h"
#include "ipc.h"

typedef BYTE CPU_AFFINITY;

typedef union _SMP_DESTINATION
{
    struct
    {
        // Used for SmpIpiSendToCpu
        _Strict_type_match_ APIC_ID ApicId;
    } Cpu;
    struct
    {
        // Used for SmpIpiSendToGroup
        _Strict_type_match_ CPU_AFFINITY Affinity;
    } Group;

    // for SmpIpiSendToAllExcludingSelf
    // SmpIpiSendToAllIncludingSelf and
    // SmpIpiSendToSelf
    BYTE        __Reserved;
} SMP_DESTINATION, *PSMP_DESTINATION;

typedef enum _SMP_IPI_SEND_MODE
{
    // uses physical address
    SmpIpiSendToCpu,

    // sends to running CPU
    SmpIpiSendToSelf,
    SmpIpiSendToAllIncludingSelf,
    SmpIpiSendToAllExcludingSelf,

    // uses logical address
    SmpIpiSendToGroup,

    SmpIpiSendMax = SmpIpiSendToGroup
} SMP_IPI_SEND_MODE;

_No_competing_thread_
void
SmpPreinit(
    void
    );

STATUS
_No_competing_thread_
SmpInit(
    void
    );

STATUS
_No_competing_thread_
SmpSetupLowerMemory(
    IN          BYTE        NumberOfTssStacks
    );

void
_No_competing_thread_
SmpWakeupAps(
    void
    );

void
_No_competing_thread_
SmpCleanupLowerMemory(
    void
    );

void
SmpSendPanic(
    void
    );

// Calls SmpSendGenericIpiEx with SmpIpiSendToAllExcludingSelf causing the
// BroadcastFunction to be executed on each CPU except the one that is calling
// the function.
STATUS
SmpSendGenericIpi(
    IN      PFUNC_IpcProcessEvent   BroadcastFunction,
    IN_OPT  PVOID                   Context,
    IN_OPT  PFUNC_FreeFunction      FreeFunction,
    IN_OPT  PVOID                   FreeContext,
    IN      BOOLEAN                 WaitForHandling
    );

//******************************************************************************
// Function:     SmpSendGenericIpiEx
// Description:  The current CPU sends an IPI to the processors specified by
//               SendMode and Destination to execute the BroadcastFunction.
// Returns:      STATUS
// Parameter:    IN PFUNC_IpcProcessEvent BroadcastFunction - Function to execute
//               on each destination CPU.
// Parameter:    IN_OPT PVOID Context - The context to be passed to the
//               BroadcastFunction.
// Parameter:    IN_OPT PFUNC_FreeFunction FreeFunction - The function to free
//               the context sent to the BroadcastFunction.
// Parameter:    IN_OPT PVOID FreeContext - The context to be sent to the
//               FreeFunction.
// Parameter:    IN BOOLEAN WaitForHandling - if TRUE waits until each CPU
//               executes the  BroadcastFunction.
// Parameter:    IN SMP_IPI_SEND_MODE SendMode - together with Destination
//               specifies the target processors for IPI delivery.
// Parameter:    IN SMP_DESTINATION Destination - species the destination
//               processor(s).
//******************************************************************************
STATUS
SmpSendGenericIpiEx(
    IN      PFUNC_IpcProcessEvent   BroadcastFunction,
    IN_OPT  PVOID                   Context,
    IN_OPT  PFUNC_FreeFunction      FreeFunction,
    IN_OPT  PVOID                   FreeContext,
    IN      BOOLEAN                 WaitForHandling,
    IN _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    _When_(SendMode == SmpIpiSendToCpu || SendMode == SmpIpiSendToGroup, IN)
            SMP_DESTINATION         Destination
    );

STATUS
SmpCpuInit(
    void
    );

void
_No_competing_thread_
SmpGetCpuList(
    OUT_PTR      PLIST_ENTRY*     CpuList
    );

DWORD
SmpGetNumberOfActiveCpus(
    void
    );

void
SmpNotifyCpuWakeup(
    void
    );