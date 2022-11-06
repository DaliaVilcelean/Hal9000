#include "test_common.h"
#include "test_thread.h"
#include "test_priority_scheduler.h"
#include "ex_event.h"
#include "mutex.h"
#include "thread_internal.h"
#include "checkin_queue.h"
#include "pit.h"

#define PRIORITY_SCHEDULER_NO_OF_ITERATIONS             16

#pragma warning(push)

// warning C4200: nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable:4200)
typedef struct _TEST_PRIORITY_EXEC_CTX
{
    BOOLEAN                 MultipleThreads;

    LOCK                    Lock;
    DWORD                   CurrentIndex;
    DWORD                   MaxIndex;

    TID                     WakeupTids[0];
} TEST_PRIORITY_EXEC_CTX, *PTEST_PRIORITY_EXEC_CTX;
#pragma warning(pop)

typedef struct _TEST_PRIORITY_WAKEUP_CTX
{
    CHECKIN_QUEUE           SynchronizationContext;

    EX_EVENT                WakeupEvent;
} TEST_PRIORITY_WAKEUP_CTX, *PTEST_PRIORITY_WAKEUP_CTX;

typedef struct _TEST_PRIORITY_MUTEX_CTX
{
    CHECKIN_QUEUE           SynchronizationContext;

    MUTEX                   Mutex;
} TEST_PRIORITY_MUTEX_CTX, *PTEST_PRIORITY_MUTEX_CTX;

void
(__cdecl TestPrepareMutex)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    )
{
    PMUTEX pMutex;
    BOOLEAN acquireMutex;
    PTEST_PRIORITY_MUTEX_CTX pWakeupCtx;

    ASSERT(NULL != Context);
    ASSERT(NumberOfThreads > 0);

    pWakeupCtx = (PTEST_PRIORITY_MUTEX_CTX)Context;

    pWakeupCtx = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
        sizeof(TEST_PRIORITY_MUTEX_CTX),
        HEAP_TEST_TAG, 0);

    DWORD bufferSize = CheckinQueuePreInit(&pWakeupCtx->SynchronizationContext, NumberOfThreads);

    PBYTE buffer = (PBYTE)ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
        bufferSize, HEAP_TEST_TAG, 0);

    CheckinQueueInit(&pWakeupCtx->SynchronizationContext, buffer);

    pMutex = &pWakeupCtx->Mutex;

    // warning C4305: 'type cast': truncation from 'const PVOID' to 'BOOLEAN'
#pragma warning(suppress:4305)
    acquireMutex = (BOOLEAN) PrepareContext;

    MutexInit(pMutex, FALSE);

    if (acquireMutex)
    {
        MutexAcquire(pMutex);
    }

    *Context = pWakeupCtx;
}

void
(__cdecl TestThreadPostCreateMutex)(
    IN              PVOID               Context
    )
{
    PTEST_PRIORITY_MUTEX_CTX pContext;
    PMUTEX pMutex;

    pContext = (PTEST_PRIORITY_MUTEX_CTX)Context;
    ASSERT(pContext != NULL);
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    pMutex = (PMUTEX)&pContext->Mutex;
    ASSERT(pMutex != NULL);

    // wait on all threads to finish creation and mark presence before blocking on the mutex.
    CheckinQueueWaitOn(&pContext->SynchronizationContext, TRUE, 0);

    // little sleep to make sure even the last thread got blocked on the mutex.
    PitSleep(10);

    MutexRelease(pMutex);
}

STATUS
(__cdecl TestThreadPriorityMutex)(
    IN_OPT      PVOID       Context
    )
{
    PTEST_PRIORITY_MUTEX_CTX pContext;
    PMUTEX pMutex;

    pContext = (PTEST_PRIORITY_MUTEX_CTX)Context;
    ASSERT(pContext != NULL);
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    pMutex = (PMUTEX)&pContext->Mutex;
    ASSERT(pMutex != NULL);

    // mark my presence
    CheckinQueueMarkPresence(&pContext->SynchronizationContext);

    MutexAcquire(pMutex);

    LOG_TEST_LOG("Thread [%s] with priority %u received MUTEX!\n",
                 ThreadGetName(NULL), ThreadGetPriority(NULL));

    MutexRelease(pMutex);

    return STATUS_SUCCESS;
}

void
(__cdecl TestThreadPostFinishMutex)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    )
{
    PTEST_PRIORITY_MUTEX_CTX pContext;

    ASSERT(NULL != Context);
    pContext = (PTEST_PRIORITY_MUTEX_CTX)Context;
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    UNREFERENCED_PARAMETER(NumberOfThreads);

    ExFreePoolWithTag((PVOID)pContext->SynchronizationContext.Array, HEAP_TEST_TAG);

    CheckinQueueUninit(&pContext->SynchronizationContext);

    // This Context is freed outside in TestThreadFunctionality!!!!
    //ExFreePoolWithTag((PVOID)pContext, HEAP_TEST_TAG);

    pContext = NULL;
}

void
(__cdecl TestThreadPrepareWakeupEvent)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    )
{
    STATUS status;
    PTEST_PRIORITY_WAKEUP_CTX pWakeupCtx;

    ASSERT( NULL != Context );
    ASSERT(PrepareContext == NULL);
    ASSERT(NumberOfThreads > 0);

    pWakeupCtx = (PTEST_PRIORITY_WAKEUP_CTX)Context;

    pWakeupCtx = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
        sizeof(TEST_PRIORITY_WAKEUP_CTX),
        HEAP_TEST_TAG, 0);

    DWORD bufferSize = CheckinQueuePreInit(&pWakeupCtx->SynchronizationContext, NumberOfThreads);

    PBYTE buffer = (PBYTE)ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
        bufferSize, HEAP_TEST_TAG, 0);

    CheckinQueueInit(&pWakeupCtx->SynchronizationContext, buffer);

    status = ExEventInit(&pWakeupCtx->WakeupEvent,
                         ExEventTypeSynchronization,
                         FALSE);
    ASSERT(SUCCEEDED(status));

    *Context = pWakeupCtx;
}

void
(__cdecl TestThreadPostCreateWakeup)(
    IN              PVOID               Context
    )
{
    PTEST_PRIORITY_WAKEUP_CTX pContext;
    PEX_EVENT pWakeupEvent;

    pContext = (PTEST_PRIORITY_WAKEUP_CTX)Context;
    ASSERT(pContext != NULL);
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    pWakeupEvent = (PEX_EVENT)&pContext->WakeupEvent;
    ASSERT(pWakeupEvent != NULL);

    // wait on all threads to finish creation and mark presence before blocking on the event.
    CheckinQueueWaitOn(&pContext->SynchronizationContext, TRUE, 0);

    // little sleep to make sure even the last thread got blocked on the event.
    PitSleep(10);

    ExEventSignal(pWakeupEvent);
}

STATUS
(__cdecl TestThreadPriorityWakeup)(
    IN_OPT      PVOID       Context
    )
{
    PTEST_PRIORITY_WAKEUP_CTX pContext;
    PEX_EVENT pWakeupEvent;

    pContext = (PTEST_PRIORITY_WAKEUP_CTX)Context;
    ASSERT(pContext != NULL);
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    pWakeupEvent = (PEX_EVENT)&pContext->WakeupEvent;
    ASSERT(pWakeupEvent != NULL);

    // mark my presence
    CheckinQueueMarkPresence(&pContext->SynchronizationContext);

    ExEventWaitForSignal(pWakeupEvent);

    LOG_TEST_LOG("Thread [%s] with priority %u woke up!\n",
                 ThreadGetName(NULL), ThreadGetPriority(NULL));

    ExEventSignal(pWakeupEvent);

    return STATUS_SUCCESS;
}

void
(__cdecl TestThreadPostFinishWakeup)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    )
{
    PTEST_PRIORITY_WAKEUP_CTX pContext;

    ASSERT(NULL != Context);
    pContext = (PTEST_PRIORITY_WAKEUP_CTX)Context;
    ASSERT(pContext->SynchronizationContext.Array != NULL);

    UNREFERENCED_PARAMETER(NumberOfThreads);

    ExFreePoolWithTag((PVOID)pContext->SynchronizationContext.Array, HEAP_TEST_TAG);

    CheckinQueueUninit(&pContext->SynchronizationContext);

    // This Context is freed outside in TestThreadFunctionality!!!!
    //ExFreePoolWithTag((PVOID)pContext, HEAP_TEST_TAG);

    pContext = NULL;
}

STATUS
(__cdecl TestThreadPriorityExecution)(
    IN_OPT      PVOID       Context
    )
{
    PTEST_PRIORITY_EXEC_CTX pCtx;
    TID tid;
    THREAD_PRIORITY priority;
    BOOLEAN bFailed;

    ASSERT(Context != NULL);

    pCtx = (PTEST_PRIORITY_EXEC_CTX) Context;

    tid = ThreadGetId(NULL);
    priority = ThreadGetPriority(NULL);
    bFailed = FALSE;

    for (QWORD i = 0; i < PRIORITY_SCHEDULER_NO_OF_ITERATIONS; ++i)
    {
        QWORD uninterruptedTicks = GetCurrentThread()->UninterruptedTicks;
        INTR_STATE oldState;

        LockAcquire(&pCtx->Lock, &oldState);
        pCtx->WakeupTids[pCtx->CurrentIndex++] = tid;
        LockRelease(&pCtx->Lock, oldState);
        ThreadYield();

        if (pCtx->MultipleThreads)
        {
            if (uninterruptedTicks != 0)
            {
                LOG_ERROR("The thread should not have any uninterrupted ticks, it should have yielded the CPU"
                          "in a RR fashion to the next thread in list!\n");
                bFailed = TRUE;
                break;
            }
        }
        else
        {
            if (uninterruptedTicks < i)
            {
                LOG_ERROR("The thread has %U uninterrupted ticks and it should have at least %U\n",
                          uninterruptedTicks, i);
                bFailed = TRUE;
                break;
            }
        }
    }

    if (!pCtx->MultipleThreads && !bFailed)
    {
        // In the case of the round-robin test we still need to make some checks from the perl .check script
        // however, in the case of the single thread high priority we can determine if it was uninterrupted that it
        // was not de-scheduled when it yielded the CPU
        LOG_TEST_PASS;
    }

    return STATUS_SUCCESS;
}

void
(__cdecl TestThreadPreparePriorityExecution)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    )
{
    BOOLEAN bMultipleThreads;
    PTEST_PRIORITY_EXEC_CTX pNewContext;

    ASSERT(Context != NULL);

    // warning C4305: 'type cast': truncation from 'const PVOID' to 'BOOLEAN'
#pragma warning(suppress:4305)
    bMultipleThreads = (BOOLEAN) PrepareContext;

    pNewContext = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
                                        sizeof(TEST_PRIORITY_EXEC_CTX) + sizeof(TID) * NumberOfThreads *PRIORITY_SCHEDULER_NO_OF_ITERATIONS,
                                        HEAP_TEST_TAG,
                                        0);

    pNewContext->CurrentIndex = 0;
    pNewContext->MaxIndex = NumberOfThreads * PRIORITY_SCHEDULER_NO_OF_ITERATIONS;
    pNewContext->MultipleThreads = bMultipleThreads;
    LockInit(&pNewContext->Lock);

    *Context = pNewContext;
}

void
(__cdecl TestThreadPostPriorityExecution)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    )
{
    PTEST_PRIORITY_EXEC_CTX pCtx;

    UNREFERENCED_PARAMETER(NumberOfThreads);

    pCtx = (PTEST_PRIORITY_EXEC_CTX) Context;
    ASSERT(pCtx != NULL);

    if (pCtx->MultipleThreads)
    {
        for (DWORD i = 0; i < pCtx->MaxIndex; ++i)
        {
            LOG_TEST_LOG("Thread 0x%X with priority %u has %u uninterrupted ticks!\n",
                         pCtx->WakeupTids[i], ThreadPriorityMaximum, 0);
        }
    }
}