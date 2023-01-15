#pragma once

#include "ref_cnt.h"

typedef
STATUS
(__cdecl FUNC_IpcProcessEvent)(
    IN_OPT  PVOID   Context
    );

typedef FUNC_IpcProcessEvent* PFUNC_IpcProcessEvent;

typedef struct _IPC_EVENT_CPU
{
    LIST_ENTRY              ListEntry;
    struct _IPC_EVENT*      Event;
} IPC_EVENT_CPU, *PIPC_EVENT_CPU;

_Ret_writes_maybenull_(NumberOfCpus)
PTR_SUCCESS
PIPC_EVENT_CPU
IpcGenerateEvent(
    IN      PFUNC_IpcProcessEvent   BroadcastFunction,
    IN_OPT  PVOID                   Context,
    IN_OPT  PFUNC_FreeFunction      FreeFunction,
    IN_OPT  PVOID                   FreeContext,
    IN      BOOLEAN                 WaitForHandling,
    IN_RANGE_LOWER(1)      
            DWORD                   NumberOfCpus
    );

void
IpcWaitForEventHandling(
    _Pre_valid_ _Post_ptr_invalid_
            PIPC_EVENT_CPU          Event
    );

STATUS
IpcProcessEvent(
    _Pre_valid_ _Post_ptr_invalid_
            PIPC_EVENT_CPU      CpuEvent,
    OUT     STATUS*             FunctionStatus
    );