#include "test_common.h"
#include "test_thread.h"
#include "test_timer.h"
#include "ex_timer.h"
#include "iomu.h"

#pragma warning(push)

// warning C4201: nonstandard extension used: nameless struct/union
#pragma warning(disable:4201)

// warning C4200: nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable:4200)

#pragma pack(push, 1)
typedef struct _TIMER_TEST_CTX
{
    WORD            TimesToWait;
    INT32           UsToSleep;
    BYTE            TimerType;
    BYTE            __Reserved;
} TIMER_TEST_CTX, *PTIMER_TEST_CTX;
STATIC_ASSERT(sizeof(TIMER_TEST_CTX) == sizeof(PVOID));
STATIC_ASSERT(FIELD_OFFSET(TIMER_TEST_CTX, UsToSleep) == SLEEP_TIME_BIT_OFFSET / BITS_PER_BYTE);
STATIC_ASSERT(FIELD_OFFSET(TIMER_TEST_CTX, TimerType) == TIMER_TYPE_BIT_OFFSET / BITS_PER_BYTE);
#pragma pack(pop)

typedef
_Struct_size_bytes_(sizeof(TID_WAKEUP) + NumberOfEntries * sizeof(QWORD))
struct _TID_WAKEUP
{
    volatile DWORD      CurrentIndex;
    DWORD               NumberOfEntries;

    _Field_size_part_(NumberOfEntries, CurrentIndex)
    QWORD               SystemWakeTimeUs[0];
} TID_WAKEUP, *PTID_WAKEUP;

typedef struct _TIMER_TEST_MULTIPLE_CTX
{
    BOOLEAN             ThreadsShareTimer;
    union
    {
        // Used when the threads share the same timer
        // "TestThreadTimerMultipleThreads"
        struct
        {
            EX_TIMER    Timer;
        } Same;

        // Used when each thread has its own timer
        // "TestThreadTimerMultipleTimers"
        struct
        {
            QWORD       TimeToSleep;
            DWORD       TimesToSleep;
            DWORD       ThreadIndex;
            PTID_WAKEUP WakeupArray;
        } Different;
    };
} TIMER_TEST_MULTIPLE_CTX, *PTIMER_TEST_MULTIPLE_CTX;

#pragma warning(pop)

static
void
_TestThreadSleepWaitTimer(
    IN          QWORD           Timestamp,
    IN          EX_TIMER_TYPE   TimerType,
    _When_(TimerType == ExTimerTypeRelativePeriodic, IN)
    _When_(TimerType != ExTimerTypeRelativePeriodic, _Reserved_)
                DWORD           TimesToWait
)
{
    EX_TIMER timer;
    STATUS status;
    DWORD timesToWait;

    timesToWait = (TimerType == ExTimerTypeRelativePeriodic) ? TimesToWait : 1;

    status = ExTimerInit(&timer,
                         TimerType,
                         Timestamp);
    ASSERT(SUCCEEDED(status));

    ExTimerStart(&timer);

    for (DWORD i = 0; i < TimesToWait; ++i)
    {
        ExTimerWait(&timer);
    }

    ExTimerStop(&timer);
    ExTimerUninit(&timer);
}

STATUS
(__cdecl TestThreadTimerSleep)(
    IN_OPT      PVOID       Context
    )
{
    PTIMER_TEST_CTX pCtx;
    QWORD timeStamp;

    pCtx = (PTIMER_TEST_CTX) Context;

    ASSERT(pCtx != NULL);

    timeStamp = (pCtx->TimerType == ExTimerTypeAbsolute) ? IomuGetSystemTimeUs() + pCtx->UsToSleep : pCtx->UsToSleep;

    _TestThreadSleepWaitTimer(timeStamp,
                              pCtx->TimerType,
                              pCtx->TimesToWait);
    LOG_TEST_PASS;

    return STATUS_SUCCESS;
}

STATUS
(__cdecl TestThreadTimerMultiple)(
    IN_OPT      PVOID       Context
    )
{
    PTIMER_TEST_MULTIPLE_CTX pTimer;

    pTimer = (PTIMER_TEST_MULTIPLE_CTX) Context;

    ASSERT(pTimer != NULL);

    if (pTimer->ThreadsShareTimer)
    {
        ExTimerWait(&pTimer->Same.Timer);

        LOG_TEST_PASS;
    }
    else
    {
        EX_TIMER timer;
        STATUS status;

        // Each thread has its own timer, create it here
        status = ExTimerInit(&timer,
                             ExTimerTypeRelativePeriodic,
                             pTimer->Different.TimeToSleep);
        ASSERT(SUCCEEDED(status));

        ExTimerStart(&timer);

        for (DWORD i = 0; i < pTimer->Different.TimesToSleep; ++i)
        {
            ExTimerWait(&timer);

            // Record the time each thread wakes up
            DWORD curIdx = _InterlockedIncrement(&pTimer->Different.WakeupArray->CurrentIndex) - 1;

            // Store the system time in which the current thread has woken up in an array
            pTimer->Different.WakeupArray->SystemWakeTimeUs[curIdx] = IomuGetSystemTimeUs();
        }

        ExTimerStop(&timer);
        ExTimerUninit(&timer);
    }

    return STATUS_SUCCESS;
}

void
(__cdecl TestThreadTimerPrepare)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    )
{
    BOOLEAN bPrepareArray;

    ASSERT(Context != NULL);

    // warning C4305: 'type cast': truncation from 'const PVOID' to 'BOOLEAN'
#pragma warning(suppress:4305)
    bPrepareArray = (BOOLEAN) PrepareContext;

    if (bPrepareArray)
    {
        // "TestThreadTimerMultipleTimers"
        // Each thread has its own timer, but we need to hold a shared structure which will tell each thread what kind
        // of timer it has to create (i.e. its timeout period) and how many times it should wait for it.
        // This shared structure will also keep the system time in us when each thread is woken up.

        PTID_WAKEUP pWakeupTimes = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
                                                         sizeof(TID_WAKEUP) + (sizeof(QWORD) * NumberOfThreads * TIMER_TEST_NO_OF_ITERATIONS),
                                                         HEAP_TEST_TAG,
                                                         0);

        pWakeupTimes->NumberOfEntries = NumberOfThreads * TIMER_TEST_NO_OF_ITERATIONS;

        PTIMER_TEST_MULTIPLE_CTX* pTimersCtx = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
                                                   sizeof(PTIMER_TEST_MULTIPLE_CTX) * NumberOfThreads,
                                                   HEAP_TEST_TAG,
                                                   0);

        for (DWORD i = 0; i < NumberOfThreads; ++i)
        {
            pTimersCtx[i] = ExAllocatePoolWithTag(PoolAllocatePanicIfFail, sizeof(TIMER_TEST_MULTIPLE_CTX), HEAP_TEST_TAG, 0);

            pTimersCtx[i]->ThreadsShareTimer = FALSE;
            pTimersCtx[i]->Different.TimeToSleep = TIMER_TEST_SLEEP_TIME_IN_US + i * TIMER_TEST_SLEEP_INCREMENT;
            pTimersCtx[i]->Different.TimesToSleep = TIMER_TEST_NO_OF_ITERATIONS;
            pTimersCtx[i]->Different.ThreadIndex = i;
            pTimersCtx[i]->Different.WakeupArray = pWakeupTimes;
        }

        *Context = pTimersCtx;
    }
    else
    {
        // "TestThreadTimerMultipleThreads"
        // All the threads use the same timer, initialize and start it here

        PTIMER_TEST_MULTIPLE_CTX pTimerCtx;
        STATUS status;

        pTimerCtx = ExAllocatePoolWithTag(PoolAllocatePanicIfFail, sizeof(TIMER_TEST_MULTIPLE_CTX), HEAP_TEST_TAG, 0);

        pTimerCtx->ThreadsShareTimer = TRUE;

        status = ExTimerInit(&pTimerCtx->Same.Timer, ExTimerTypeRelativeOnce, TIMER_TEST_SLEEP_TIME_IN_US);
        ASSERT(SUCCEEDED(status));

        *Context = pTimerCtx;

        ExTimerStart(&pTimerCtx->Same.Timer);
    }
}


void
(__cdecl TestThreadTimerMultipleThreadsPostFinish)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    )
{
    PTIMER_TEST_MULTIPLE_CTX pContext;

    UNREFERENCED_PARAMETER(NumberOfThreads);

    pContext = (PTIMER_TEST_MULTIPLE_CTX)Context;

    ASSERT(pContext != NULL);

    ExTimerUninit(&pContext->Same.Timer);
}


void
(__cdecl TestThreadTimerMultipleTimersPostFinish)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    )
{
    PTIMER_TEST_MULTIPLE_CTX* pContexts;
    PTIMER_TEST_MULTIPLE_CTX pFirstContext;

    UNREFERENCED_PARAMETER(NumberOfThreads);

    pContexts = (PTIMER_TEST_MULTIPLE_CTX*) Context;

    ASSERT(pContexts != NULL);

    pFirstContext = pContexts[0];

    ASSERT(pFirstContext != NULL);

    __try
    {
        for (DWORD i = 1; i < pFirstContext->Different.WakeupArray->NumberOfEntries; ++i)
        {
            QWORD prevWakeTimeUs = pContexts[0]->Different.WakeupArray->SystemWakeTimeUs[i-1];

            LOG_TEST_LOG("Thread woke up at %U us system time\n",
                         pContexts[0]->Different.WakeupArray->SystemWakeTimeUs[i]);

            if (pContexts[0]->Different.WakeupArray->SystemWakeTimeUs[i] < prevWakeTimeUs)
            {
                LOG_ERROR("Previous thread woke up at %U system time and the current one woke up at %U\n"
                          "These values should be monotonically increasing!\n",
                          prevWakeTimeUs, pContexts[0]->Different.WakeupArray->SystemWakeTimeUs[i]);
                __leave;
            }
        }

        LOG_TEST_PASS;
    }
    __finally
    {

    }
}
