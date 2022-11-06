#include "HAL9000.h"
#include "ipc.h"
#include "synch.h"
#include "smp.h"

#pragma warning(push)

// warning C4200: nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable:4200)

typedef struct _IPC_EVENT
{
    REF_COUNT               RefCnt;
    PFUNC_IpcProcessEvent   Function;
    PVOID                   Context;

    BOOLEAN                 SignalTermination;

    // valid only if SignalTermination is TRUE
    EVENT                   TerminationEvent;

    PFUNC_FreeFunction      FreeFunction;
    PVOID                   FreeFunctionContext;

    IPC_EVENT_CPU           CpuEvents[0];
} IPC_EVENT, *PIPC_EVENT;

#pragma warning(pop)

static FUNC_FreeFunction _IpcFreeEvent;

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
    )
{
    STATUS status;
    PIPC_EVENT pEvent;
    DWORD noOfAdditionalReferences;
    DWORD i;
    DWORD totalAllocationSize;

    if (NULL == BroadcastFunction)
    {
        return NULL;
    }

    if (0 == NumberOfCpus)
    {
        LOG_ERROR("We can't send an IPI to 0 processors! :(\n");
        return NULL;
    }

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pEvent = NULL;
    noOfAdditionalReferences = WaitForHandling ? NumberOfCpus : NumberOfCpus - 1;
    totalAllocationSize = sizeof(IPC_EVENT) + NumberOfCpus * sizeof(IPC_EVENT_CPU);

    __try
    {
        pEvent = ExAllocatePoolWithTag(PoolAllocateZeroMemory, totalAllocationSize, HEAP_IPC_TAG, 0);
        if (NULL == pEvent)
        {
            LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag\n", totalAllocationSize);
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }
        LOG_TRACE_CPU("Allocated event at: 0x%X\n", pEvent);

        pEvent->SignalTermination = WaitForHandling;
        if (pEvent->SignalTermination)
        {
            status = EvtInitialize(&pEvent->TerminationEvent, EventTypeSynchronization, FALSE);
            if (!SUCCEEDED(status))
            {
                LOGL("EvtInitialize failed with status: 0x%x\n", status);
                __leave;
            }
        }
        pEvent->Function = BroadcastFunction;
        pEvent->Context = Context;
        pEvent->FreeFunction = FreeFunction;
        pEvent->FreeFunctionContext = FreeContext;

        for (i = 0; i < NumberOfCpus; ++i)
        {
            pEvent->CpuEvents[i].Event = pEvent;
        }

        RfcPreInit(&pEvent->RefCnt);

        status = RfcInit(&pEvent->RefCnt, _IpcFreeEvent, NULL);
        if (!SUCCEEDED(status))
        {
            LOGL("RfcInit failed with status: 0x%x\n", status);
            __leave;
        }

        for (i = 0; i < noOfAdditionalReferences; ++i)
        {
            RfcReference(&pEvent->RefCnt);
        }
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (NULL != pEvent)
            {
                ExFreePoolWithTag(pEvent, HEAP_IPC_TAG);
                pEvent = NULL;
            }
        }

        LOG_FUNC_END;
    }

    return NULL != pEvent ? pEvent->CpuEvents : NULL;
}

void
IpcWaitForEventHandling(
    _Pre_valid_ _Post_ptr_invalid_
            PIPC_EVENT_CPU          CpuEvent
    )
{
    ASSERT(NULL != CpuEvent);

    LOG_FUNC_START;

    ASSERT( NULL != CpuEvent->Event );

    ASSERT(CpuEvent->Event->SignalTermination);
    EvtWaitForSignal(&CpuEvent->Event->TerminationEvent);

    LOG_TRACE_CPU("Event was processed\n");

    RfcDereference(&CpuEvent->Event->RefCnt);

    LOG_FUNC_END;
}

STATUS
IpcProcessEvent(
    _Pre_valid_ _Post_ptr_invalid_
            PIPC_EVENT_CPU      CpuEvent,
    OUT     STATUS*             FunctionStatus
    )
{
    PIPC_EVENT pEvent;
    STATUS funcStatus;
    DWORD newRefCount;
    BOOLEAN signalTermination;

    if (NULL == CpuEvent)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (NULL == FunctionStatus)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    LOG_FUNC_START;

    pEvent = CpuEvent->Event;
    ASSERT( NULL != pEvent );

    LOG_TRACE_CPU("Will process event at 0x%X\n", pEvent );

    ASSERT( NULL != pEvent->Function );

    funcStatus = pEvent->Function(pEvent->Context);

    // must be used before RfcDereference because the event
    // could become invalid if the ref count gets to 0
    signalTermination = pEvent->SignalTermination;

    newRefCount = RfcDereference(&pEvent->RefCnt);
    if (signalTermination && (1 == newRefCount))
    {
        EvtSignal(&pEvent->TerminationEvent);
    }
    *FunctionStatus = funcStatus;

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

static
void
_IpcFreeEvent(
    IN      PVOID       Object,
    IN_OPT  PVOID       Context
    )
{
    PIPC_EVENT pEvent;

    ASSERT(NULL != Object);
    ASSERT(NULL == Context);

    LOG_FUNC_START;

    pEvent = (PIPC_EVENT)Object;

    if (NULL != pEvent->FreeFunction)
    {
        pEvent->FreeFunction(pEvent->Context, pEvent->FreeFunctionContext);
    }

    LOG_TRACE_CPU("Will deallocate event object at 0x%X\n", pEvent);
    ExFreePoolWithTag(pEvent, HEAP_IPC_TAG);
    pEvent = NULL;

    LOG_FUNC_END;
}