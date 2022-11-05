#include "test_common.h"
#include "test_thread.h"
#include "test_timer.h"
#include "test_priority_scheduler.h"
#include "test_priority_donation.h"

#include "mutex.h"


FUNC_ThreadStart                TestThreadYield;

FUNC_ThreadStart                TestMutexes;

FUNC_ThreadStart                TestCpuIntense;

static FUNC_ThreadPrepareTest   _ThreadTestPassContext;

const THREAD_TEST THREADS_TEST[] =
{
    // Tests just for fun
    { "ThreadYield", TestThreadYield, NULL, NULL, NULL, NULL, FALSE, FALSE },
    { "Mutex", TestMutexes, TestPrepareMutex, (PVOID) FALSE, NULL, NULL, FALSE, FALSE },
    { "CpuIntense", TestCpuIntense, NULL, NULL, NULL, NULL, FALSE, FALSE },

    // Actual tests used for validating the project

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                                  TIMER TESTS                                                   //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Tests to see if an absolute timer set 50 ms in the future works properly
    {   "TestThreadTimerAbsolute", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_ABSOLUTE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Tests to see if an absolute timer set 50 ms in the past works properly (i.e. the ExTimerWait should
    // return instantly)
    {   "TestThreadTimerAbsolutePassed", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_ABSOLUTE_PASSED, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Tests to see if a relative periodic timer with a 50ms period works properly if waited on for 10 times
    {   "TestThreadTimerPeriodicMultiple", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_PERIODIC_MULTIPLE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Same as the "TestThreadTimerPeriodicMultiple" except it only waits once for the timer
    {   "TestThreadTimerPeriodicOnce", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_PERIODIC_ONCE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Checks to see if a periodic timer with period 0 works properly (i.e. ExTimerWait should return instantly
    // each time)
    {   "TestThreadTimerPeriodicZero", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_PERIODIC_ZERO, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Spawns multiple threads (default 16) to wait for the same relative once timer with a period of 50 ms.
    // The test validates that each thread is woken up properly after the timer expires.
    {   "TestThreadTimerMultipleThreads", TestThreadTimerMultiple,
        TestThreadTimerPrepare, (PVOID)FALSE, NULL, TestThreadTimerMultipleThreadsPostFinish,
        ThreadPriorityDefault, FALSE, FALSE, FALSE},

    // Spawns multiple threads (default 16) to each wait for a different relative periodic timer with a different
    // period. Thread[i] waits for a timer of period 50ms + i * 10ms 10 times.
    // The test validates that the time in which the threads are woken up is monotonically increasing.
    // The following wakeup times are valid:
    // T[0] woke up at 10000 us system time
    // T[1] woke up at 11000 us system time
    // T[2] woke up at 12000 us system time
    // T[0] woke up at 12000 us system time
    // ...

    // The following wakeup times are invalid:
    // T[0] woke up at 10000 us system time
    // T[1] woke up at 11000 us system time
    // T[2] woke up at 12000 us system time
    // T[0] woke up at 11750 us system time
    // ...
    {   "TestThreadTimerMultipleTimers", TestThreadTimerMultiple,
        TestThreadTimerPrepare, (PVOID)TRUE, NULL, TestThreadTimerMultipleTimersPostFinish,
        ThreadPriorityDefault, FALSE, FALSE, TRUE},

    // Tests to see if a relative once timer works properly for a 50ms timeout period
    {   "TestThreadTimerRelative", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_RELATIVE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Tests to see if a relative once timer works properly for a 0ms timeout period (i.e. ExTimerWait should return
    // instantly)
    {   "TestThreadTimerRelativeZero", TestThreadTimerSleep,
        _ThreadTestPassContext, (PVOID)TEST_TIMER_RELATIVE_ZERO, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                           PRIORITY SCHEDULER TESTS                                             //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Spawns multiple threads (default 16) with increasing priorities (from ThreadPriorityLowest to
    // ThreadPriorityLowest + no of threads - 1) and checks to see if the threads waiting for an EX_EVENT are woken
    // up according to their priorities (i.e. the higher priorities threads should be woken up first)
    {   "TestThreadPriorityWakeup", TestThreadPriorityWakeup,
        TestThreadPrepareWakeupEvent, NULL, TestThreadPostCreateWakeup, TestThreadPostFinishWakeup,
        ThreadPriorityLowest, TRUE, FALSE, FALSE},

    // Same as "TestThreadPriorityWakeup" except MUTEXes are used instead of EX_EVENTs
    {   "TestThreadPriorityMutex", TestThreadPriorityMutex,
        TestPrepareMutex, (PVOID)TRUE, TestThreadPostCreateMutex, TestThreadPostFinishMutex,
        ThreadPriorityLowest, TRUE, FALSE, FALSE},

    // Spawns a highest priority thread and validates that the thread is not de-scheduled even if it tries to yield
    // the CPU using the ThreadYield() function.
    {   "TestThreadPriorityYield", TestThreadPriorityExecution,
        TestThreadPreparePriorityExecution, (PVOID) FALSE, NULL, TestThreadPostPriorityExecution,
        ThreadPriorityMaximum, FALSE, TRUE, FALSE},

    // Spawns multiple threads (default 16) with highest priority and validates that each thread is relinquishes
    // the CPU in a round-robin fashion when callin the ThreadYield() function (i.e. all threads with the same priority
    // should be scheduled equally).
    {   "TestThreadPriorityRoundRobin", TestThreadPriorityExecution,
        TestThreadPreparePriorityExecution, (PVOID) TRUE, NULL, TestThreadPostPriorityExecution,
        ThreadPriorityMaximum, FALSE, FALSE, FALSE},

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                            PRIORITY DONATION TESTS                                             //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // 1. A default priority thread is spawned
    // 2. The default priority thread initializes and acquires a mutex
    // 3. The default priority thread creates a higher priority thread
    // 4. The higher priority thread tries to acquire the mutex taken by the lower priority thread and donates its
    //    priority.
    // 5. The default priority thread has now inherited the priority of the newly spawned thread. (THIS IS CHECKED)
    {   "TestThreadPriorityDonationOnce", TestThreadPriorityDonationBasic,
        _ThreadTestPassContext, (PVOID) FALSE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // First, the same steps are taken as in "TestThreadPriorityDonationOnce", then:
    // 6. The initial thread tries to lower its priority to ThreadPriorityLowest. Its priority should still remain the
    //    one of the donators thread. (THIS IS CHECKED)
    {   "TestThreadPriorityDonationLower", TestThreadPriorityDonationBasic,
        _ThreadTestPassContext, (PVOID)TRUE, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // The main thread (ThreadPriorityDefault) creates and acquires 2 mutexes and spawns 2 additional threads with
    // higher priorities: ThreadPriorityDefault + 4 and ThreadPriorityDefault + 8.
    // The first spawned thread tries to acquire the first mutex, while the second one the second mutex. As a result,
    // after each thread is spawned the main thread receives a priority donation.
    // The main thread then releases the second mutex and validates if its priority is that of the first thread spawned
    // after which it releases the first mutex and validates its priority to be the one with which it started execution.
    {   "TestThreadPriorityDonationMulti", TestThreadPriorityDonationMultiple,
        _ThreadTestPassContext, (PVOID) TestPriorityDonationMultipleOneThreadPerLock, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Same as "TestThreadPriorityDonationMulti", except the mutexes are released in a different order: the second mutex
    // acquired is also the second one released and the first mutex acquired is the first one released.
    {   "TestThreadPriorityDonationMultiInverted", TestThreadPriorityDonationMultiple,
        _ThreadTestPassContext, (PVOID) TestPriorityDonationMultipleOneThreadPerLockInverseRelease, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Same as "TestThreadPriorityDonationMulti" except there are now 3 threads waiting on two mutexes. The two higher
    // priority ones are waiting for the second mutex, while the first spawned thread is waiting on the first mutex.
    {   "TestThreadPriorityDonationMultiThreads", TestThreadPriorityDonationMultiple,
        _ThreadTestPassContext, (PVOID)TestPriorityDonationMultipleTwoThreadsPerLock, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Same as "TestThreadPriorityDonationMultiThreads", except the mutexes are released in a different order (as in
    // the "TestThreadPriorityDonationMultiInverted" test).
    {   "TestThreadPriorityDonationMultiThreadsInverted", TestThreadPriorityDonationMultiple,
        _ThreadTestPassContext, (PVOID)TestPriorityDonationMultipleTwoThreadsPerLockInverseRelease, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // The main thread with ThreadPriorityDefault initializes 4 mutexes and acquires the first one (i.e. Mutex[0]). It
    // then spawns 3 additional threads T[i]: each with priority ThreadPriorityDefault + i (where i is from 1 to 3).
    // Each thread will acquire Mutex[i] and Mutex[i+1%4], i.e.:
    // T[1] takes Mutex[1] and then tries to take Mutex[0]
    // T[2] takes Mutex[2] and then tries to take Mutex[1]
    // T[3] takes Mutex[3] and then tries to take Mutex[2]

    // As a result of these operations the main thread will receive T[1]'s priority because it holds Mutex[0].
    // T[1] also receives T[2]'s priority because it holds Mutex[1] and as a result T[2]'s priority will be donated to
    // the main thread.
    // The same happens for T[2]...
    {   "TestThreadPriorityDonationNest", TestThreadPriorityDonationChain,
        _ThreadTestPassContext, (PVOID) 3, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},

    // Same scenario as in "TestThreadPriorityDonationNest", except the number of additional threads is 7.
    {   "TestThreadPriorityDonationChain", TestThreadPriorityDonationChain,
        _ThreadTestPassContext, (PVOID) 7, NULL, NULL,
        ThreadPriorityDefault, FALSE, TRUE, FALSE},
};

const DWORD THREADS_TOTAL_NO_OF_TESTS = ARRAYSIZE(THREADS_TEST);

#define CPU_INTENSE_MEMORY_SIZE         PAGE_SIZE

typedef struct _TEST_THREAD_INFO
{
    PTHREAD                             Thread;
    STATUS                              Status;
} TEST_THREAD_INFO, *PTEST_THREAD_INFO;

void
TestThreadFunctionality(
    IN      THREAD_TEST*                ThreadTest,
    IN_OPT  PVOID                       ContextForTestFunction,
    IN      DWORD                       NumberOfThreads
    )
{
    DWORD i;
    STATUS status;
    char threadName[MAX_PATH];
    PTEST_THREAD_INFO pThreadsInfo;
    PVOID pCtx;
    THREAD_PRIORITY currentPriority;
    BYTE priorityIncrement;
    DWORD noOfThreads;

    LOG_FUNC_START_THREAD;

    UNREFERENCED_PARAMETER(ContextForTestFunction);

    ASSERT(NULL != ThreadTest);
    ASSERT(NULL != ThreadTest->ThreadFunction);
    ASSERT(NULL != ThreadTest->TestName);

    pThreadsInfo = NULL;
    pCtx = NULL;
    currentPriority = ThreadTest->BasePriority;
    priorityIncrement = ThreadTest->IncrementPriorities ? 1 : 0;

    noOfThreads = ThreadTest->IgnoreThreadCount ? 1 : NumberOfThreads;
    ASSERT(noOfThreads > 0);

    pThreadsInfo = ExAllocatePoolWithTag(PoolAllocatePanicIfFail | PoolAllocateZeroMemory,
                                         sizeof(TEST_THREAD_INFO) * noOfThreads,
                                         HEAP_TEST_TAG,
                                         0);

    if (NULL != ThreadTest->ThreadPrepareFunction)
    {
        ThreadTest->ThreadPrepareFunction(&pCtx, noOfThreads, ThreadTest->PrepareFunctionContext);
    }

    LOG_TEST_LOG("Test [%s] START!\n", ThreadTest->TestName);
    LOG_TEST_LOG("Will create %d threads for running test [%s]\n", noOfThreads, ThreadTest->TestName);
    for (i = 0; i < noOfThreads; ++i)
    {
        snprintf(threadName, MAX_PATH, "%s-%02x", ThreadTest->TestName, i );
        status = ThreadCreate(threadName,
                              currentPriority,
                              ThreadTest->ThreadFunction,
                              ThreadTest->ArrayOfContexts ? ((PVOID*) pCtx)[i] : pCtx,
                              &pThreadsInfo[i].Thread );
        ASSERT(SUCCEEDED(status));

        // increment priority with wrap around
        currentPriority = (currentPriority + priorityIncrement) % ThreadPriorityReserved;
    }

    if (ThreadTest->ThreadPostCreateFunction != NULL)
    {
        ThreadTest->ThreadPostCreateFunction(pCtx);
    }

    for (i = 0; i < noOfThreads; ++i)
    {
        ThreadWaitForTermination(pThreadsInfo[i].Thread, &pThreadsInfo[i].Status);

        LOG_TEST_LOG("Thread [%s] terminated with status: 0x%x\n",
                     ThreadGetName(pThreadsInfo[i].Thread), pThreadsInfo[i].Status);

        ThreadCloseHandle(pThreadsInfo[i].Thread);
        pThreadsInfo[i].Thread = NULL;
    }

    if (ThreadTest->ThreadPostFinishFunction != NULL)
    {
        ThreadTest->ThreadPostFinishFunction(pCtx, noOfThreads);
    }

    LOG_TEST_LOG("Test [%s] END!\n", ThreadTest->TestName);

    if (pCtx != NULL)
    {
        if (ThreadTest->ArrayOfContexts)
        {
            for (i = 0; i < noOfThreads; ++i)
            {
                ExFreePoolWithTag(((PVOID*)pCtx)[i], HEAP_TEST_TAG);
                ((PVOID*)pCtx)[i] = NULL;
            }
        }

        ExFreePoolWithTag(pCtx, HEAP_TEST_TAG);
        pCtx = NULL;
    }

    ASSERT(NULL != pThreadsInfo)
    ExFreePoolWithTag(pThreadsInfo, HEAP_TEST_TAG);
    pThreadsInfo = NULL;

    LOG_FUNC_END_THREAD;
}

void
TestAllThreadFunctionalities(
    IN      DWORD                       NumberOfThreads
    )
{
    for (DWORD i = 0; i < THREADS_TOTAL_NO_OF_TESTS; ++i)
    {
        TestThreadFunctionality(&THREADS_TEST[i], NULL, NumberOfThreads );
    }
}

STATUS
(__cdecl TestThreadYield)(
    IN_OPT      PVOID       Context
    )
{
    DWORD i,j;

    LOG_FUNC_START_THREAD;

    UNREFERENCED_PARAMETER(Context);

    for (i = 0; i < 0x100; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            printf("[%d]This is cool\n", i);
        }
        ThreadYield();
    }

    LOG_FUNC_END_THREAD;

    return STATUS_SUCCESS;
}

STATUS
(__cdecl TestMutexes)(
    IN_OPT      PVOID       Context
    )
{
    PMUTEX pMutex = (PMUTEX) Context;
    static QWORD __value = 0;
    DWORD i;

    ASSERT( NULL != Context );
    UNREFERENCED_PARAMETER(pMutex);


    for (i = 0; i < 0x10000; ++i)
    {
        MutexAcquire(pMutex);
        __value++;
        MutexRelease(pMutex);
    }

    LOGTPL("Value: 0x%x\n", __value );

    return STATUS_SUCCESS;
}

STATUS
(__cdecl TestCpuIntense)(
    IN_OPT      PVOID       Context
    )
{
    volatile BYTE* pMemory;

    ASSERT( NULL == Context );

    pMemory = ExAllocatePoolWithTag(PoolAllocatePanicIfFail | PoolAllocateZeroMemory, CPU_INTENSE_MEMORY_SIZE, HEAP_TEST_TAG, 0 );

    for (DWORD j = 0; j < 0x10000; ++j)
    {
        for (DWORD i = 0; i < CPU_INTENSE_MEMORY_SIZE; ++i)
        {
            pMemory[i] = pMemory[i] ^ 0x63;
        }
    }

    ExFreePoolWithTag((PVOID)pMemory, HEAP_TEST_TAG);

    return STATUS_SUCCESS;
}

static
void
(__cdecl _ThreadTestPassContext)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    )
{
    PVOID pNewContext;
    ASSERT(NULL != Context);

    UNREFERENCED_PARAMETER(NumberOfThreads);

    pNewContext = ExAllocatePoolWithTag(PoolAllocatePanicIfFail, sizeof(PVOID), HEAP_TEST_TAG, 0);

    memcpy(pNewContext, &PrepareContext, sizeof(PVOID));

    *Context = pNewContext;
}