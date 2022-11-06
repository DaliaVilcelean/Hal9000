#include "HAL9000.h"
#include "core.h"
#include "iomu.h"
#include "display.h"
#include "cpumu.h"

#define DEFAULT_SECONDS_TO_IDLE_STATE           60

typedef struct _CORE_DATA
{
    QWORD               MicrosecondsToIdle;

    volatile QWORD      ReadyThreadUs;
    volatile BYTE       SystemIdle;

    PVOID               DisplayBuffer;
    DWORD               DisplayBufferSize;
} CORE_DATA, *PCORE_DATA;
STATIC_ASSERT(FIELD_OFFSET(CORE_DATA, ReadyThreadUs) % sizeof(QWORD) == 0 );

static CORE_DATA m_coreData;

static
void
_CoreSystemIdle(
    void
    );

static
void
_CoreSystemRevertFromIdle(
    IN      QWORD       IdlePeriodUs
    );

void
CorePreinit(
    void
    )
{
    memzero(&m_coreData, sizeof(CORE_DATA));
}

STATUS
CoreInit(
    void
    )
{
    QWORD systemTimeUs;

    systemTimeUs = IomuGetSystemTimeUs();

    CoreSetIdlePeriod(DEFAULT_SECONDS_TO_IDLE_STATE);
    m_coreData.ReadyThreadUs = systemTimeUs;

    m_coreData.DisplayBufferSize = SCREEN_SIZE;
    m_coreData.DisplayBuffer = ExAllocatePoolWithTag(PoolAllocateZeroMemory, m_coreData.DisplayBufferSize, HEAP_CORE_TAG, 0);
    if (NULL == m_coreData.DisplayBuffer)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", m_coreData.DisplayBufferSize );
        return STATUS_HEAP_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

void
CoreUpdateIdleTime(
    IN  BOOLEAN             IdleScheduled
    )
{
    QWORD systemTimeUs;

    ASSERT( INTR_OFF == CpuIntrGetState() );
    
    systemTimeUs = IomuGetSystemTimeUs();

    if (IdleScheduled)
    {
        if (GetCurrentPcpu()->BspProcessor)
        {
            QWORD lastActivityUs = m_coreData.ReadyThreadUs;

            if (systemTimeUs < lastActivityUs)
            {
                ASSERT_INFO( systemTimeUs + IomuGetTimerInterrupTimeUs() >= lastActivityUs,
                            "System time is %U us\nLast activity time is %U us\n", 
                            systemTimeUs, lastActivityUs );
                return;
            }

            if (systemTimeUs - lastActivityUs >= m_coreData.MicrosecondsToIdle)
            {
                if (FALSE == _InterlockedCompareExchange8(&m_coreData.SystemIdle, TRUE, FALSE))
                {
                    _CoreSystemIdle();
                }
            }
        }
    }
    else
    {
        QWORD lastActivityUs = m_coreData.ReadyThreadUs;

        // Do not use strict comparison, there is no benefit in doing interlocked exchange operations
        // to change the old value with the same new value
        if (systemTimeUs <= lastActivityUs)
        {
            // the time from each processor to another might differ by up to
            // SmpGetTimerInterruptTimeUs() (scheduler clock tick), this is not a problem 
            // => simply do not lower last activity time
            return;
        }

        _InterlockedExchange64(&m_coreData.ReadyThreadUs, systemTimeUs);
        if (TRUE == _InterlockedExchange8(&m_coreData.SystemIdle, FALSE))
        {
            _CoreSystemRevertFromIdle(systemTimeUs - lastActivityUs);
        }
        
    }
}

DWORD
CoreSetIdlePeriod(
    IN  DWORD               SecondsForIdle
    )
{
    QWORD previousIdlePeriod = m_coreData.MicrosecondsToIdle / SEC_IN_US;
    ASSERT( previousIdlePeriod <= MAX_DWORD );

    if (0 == SecondsForIdle)
    {
        return (DWORD) previousIdlePeriod;
    }

    m_coreData.MicrosecondsToIdle = SEC_IN_US * SecondsForIdle;
    return (DWORD) previousIdlePeriod;
}

static
void
_CoreSystemIdle(
    void
    )
{
    STATUS status;

    status = DispStoreBuffer(m_coreData.DisplayBuffer, m_coreData.DisplayBufferSize);
    ASSERT(SUCCEEDED(status));

    DispClearScreen();

    LOGL("System is IDLE!\n");
}

static
void
_CoreSystemRevertFromIdle(
    IN      QWORD       IdlePeriodUs
    )
{
    STATUS status;
    QWORD uptimeInMs = IdlePeriodUs / MS_IN_US;

    status = DispRestoreBuffer(m_coreData.DisplayBuffer, m_coreData.DisplayBufferSize);
    ASSERT(SUCCEEDED(status));

    LOGL("System is no longer IDLE after %u.%03u sec!\n", uptimeInMs / 1000, uptimeInMs % 1000 );
}