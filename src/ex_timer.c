#include "HAL9000.h"
#include "ex_timer.h"
#include "iomu.h"
#include "thread_internal.h"
#include "ex_event.h"
#include "list.h"
#include "lock_common.h"
#include "common_lib.h"

void ExTimerSystemPreinit()
{
    //initializare lock
    LockInit(&m_globalTimerList.TimerListLock);

    //initializare lista
    InitializeListHead(&m_globalTimerList.TimerListHead);
}

STATUS
ExTimerInit(
    OUT     PEX_TIMER       Timer,
    IN      EX_TIMER_TYPE   Type,
    IN      QWORD           Time
)
{
    STATUS status;
    INTR_STATE oldState;

    if (NULL == Timer)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (Type > ExTimerTypeMax)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    status = STATUS_SUCCESS;

    memzero(Timer, sizeof(EX_TIMER));

    Timer->Type = Type;
    if (Timer->Type != ExTimerTypeAbsolute)
    {
        // relative time

        // if the time trigger time has already passed the timer will
        // be signaled after the first scheduler tick
        Timer->TriggerTimeUs = IomuGetSystemTimeUs() + Time;
        Timer->ReloadTimeUs = Time;
    }
    else
    {
        // absolute
        Timer->TriggerTimeUs = Time;
    }

    //initializarea event
    ExEventInit(&Timer->TimerEvent, ExEventTypeNotification, FALSE);

    LockAcquire(&m_globalTimerList.TimerListLock, &oldState);
    InsertOrderedList(&m_globalTimerList.TimerListHead, &Timer->TimerListElem, ExTimerCompareListElems, NULL);
    LockRelease(&m_globalTimerList.TimerListLock, oldState);

    return status;
}

void
ExTimerStart(
    IN      PEX_TIMER       Timer
)
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }

    Timer->TimerStarted = TRUE;
}

void
ExTimerStop(
    IN      PEX_TIMER       Timer
)
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }

    Timer->TimerStarted = FALSE;
    ExEventSignal(&Timer->TimerEvent);
}

void
ExTimerWait(
    INOUT   PEX_TIMER       Timer
)
{
    ASSERT(Timer != NULL);

    if (Timer->TimerUninited)
    {
        return;
    }
    /*
    while (IomuGetSystemTimeUs() < Timer->TriggerTimeUs && Timer->TimerStarted)
    {
        ThreadYield();
    }
    */
    ExEventWaitForSignal(&Timer->TimerEvent);
}

void
ExTimerUninit(
    INOUT   PEX_TIMER       Timer
)
{
    INTR_STATE oldState;

    ASSERT(Timer != NULL);

    ExTimerStop(Timer);

    Timer->TimerUninited = TRUE;

    LockAcquire(&m_globalTimerList.TimerListLock, &oldState);
    RemoveEntryList(&Timer->TimerListElem);
    LockRelease(&m_globalTimerList.TimerListLock, oldState);
}

INT64
ExTimerCompareTimers(
    IN      PEX_TIMER     FirstElem,
    IN      PEX_TIMER     SecondElem
)
{
    return FirstElem->TriggerTimeUs - SecondElem->TriggerTimeUs;
}

INT64 ExTimerCompareListElems(IN PLIST_ENTRY t1, IN PLIST_ENTRY t2, IN PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    PEX_TIMER aux_timer = CONTAINING_RECORD(t1, EX_TIMER, TimerListElem);
    PEX_TIMER aux_timer2 = CONTAINING_RECORD(t2, EX_TIMER, TimerListElem);

    return ExTimerCompareTimers(aux_timer, aux_timer2);
}

void
ExTimerCheck(IN PEX_TIMER timer)
{
    if (IomuGetSystemTimeUs() >= timer->TriggerTimeUs)
        ExEventSignal(&timer->TimerEvent);
}

void
ExTimerCheckAll()
{
    INTR_STATE oldState;
    //STATUS status = STATUS_SUCCESS;
    LIST_ENTRY* pCurListEntry;

    LockAcquire(&m_globalTimerList.TimerListLock, &oldState);

    //status = ForEachElementExecute(&m_globalTimerList.TimerListHead, ExTimerCheck, NULL, FALSE);
    pCurListEntry = m_globalTimerList.TimerListHead.Flink;
    while (pCurListEntry != &m_globalTimerList.TimerListHead)
    {
        PEX_TIMER aux_timer = CONTAINING_RECORD(pCurListEntry, EX_TIMER, TimerListElem);
        ExTimerCheck(aux_timer);
        pCurListEntry = pCurListEntry->Flink;
    }

    LockRelease(&m_globalTimerList.TimerListLock, oldState);
}