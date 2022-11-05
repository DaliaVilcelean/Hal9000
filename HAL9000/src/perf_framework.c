#include "HAL9000.h"
#include "perf_framework.h"
#include "iomu.h"
#include "rtc.h"

void
RunPerformanceFunction(
    IN      PFUNC_TestPerformance   Function,
    IN_OPT  PVOID                   Context,
    IN      DWORD                   IterationCount,
    IN      BOOLEAN                 MeasureInUs,
    OUT     PPERFORMANCE_STATS      PerfStats
    )
{
    QWORD startTick;
    QWORD endTick;
    QWORD* allTimes;
    QWORD totalTime;
    QWORD minTime;
    QWORD maxTime;
    QWORD meanTime;
    DWORD i;

    ASSERT( NULL != Function );
    ASSERT( NULL != PerfStats );

    allTimes = NULL;

    allTimes = ExAllocatePoolWithTag(PoolAllocateZeroMemory | PoolAllocatePanicIfFail,
                                     sizeof(QWORD) * IterationCount,
                                     HEAP_TEST_TAG,
                                     0
                                     );

    for (i = 0; i < IterationCount; ++i)
    {
        BOOLEAN logState;

        logState = LogSetState(FALSE);
        startTick = RtcGetTickCount();
        Function(Context);
        endTick = RtcGetTickCount();
        LogSetState(logState);

        if (endTick == startTick)
        {
            /// TODO: why the fuck does this happen?
            LOG_ERROR("End tick: 0x%X\nStart tick: 0x%X\n", endTick, startTick);
            endTick = endTick + 1;
        }
        ASSERT_INFO( endTick > startTick, 
                    "End tick: 0x%X\nStart tick: 0x%X\n", endTick, startTick );
        allTimes[i] = endTick - startTick;
    }


    ASSERT(NULL != allTimes);

    totalTime = minTime = maxTime = allTimes[0];

    for (i = 1; i < IterationCount; ++i)
    {
        if (allTimes[i] < minTime)
        {
            minTime = allTimes[i];
        }
        else if (allTimes[i] > maxTime)
        {
            maxTime = allTimes[i];
        }

        ASSERT (MAX_QWORD - allTimes[i] >= totalTime);

        totalTime = totalTime + allTimes[i];
    }
    meanTime = totalTime / IterationCount;

    PerfStats->Min = MeasureInUs ? IomuTickCountToUs(minTime) : minTime;
    PerfStats->Max = MeasureInUs ? IomuTickCountToUs(maxTime) : maxTime;
    PerfStats->Mean = MeasureInUs ? IomuTickCountToUs(meanTime) : meanTime;

    if (NULL != allTimes)
    {
        ExFreePoolWithTag(allTimes, HEAP_TEST_TAG);
        allTimes = NULL;
    }
}

void
DisplayPerformanceStats(
    IN_READS(NumberOfStats)
            PPERFORMANCE_STATS      PerfStats,
    IN      DWORD                   NumberOfStats,
    IN_READS(NumberOfStats)
            char**                  StatNames
    )
{
    BOOLEAN bExit;

    bExit = FALSE;

    for (DWORD i = 0; i < NumberOfStats; ++i)
    {
        LOG("%s performance\n", StatNames[i]);
        LOG("Mean time: 0x%X\n", PerfStats[i].Mean);
        LOG("Min time: 0x%X\n", PerfStats[i].Min);
        LOG("Max time: 0x%X\n", PerfStats[i].Max);

        if ((PerfStats[i].Mean == 0)
            || (PerfStats[i].Min == 0)
            || (PerfStats[i].Max == 0)
            )
        {
            bExit = TRUE;
        }
    }

    if (bExit)
    {
        return;
    }

    if (NumberOfStats == 2)
    {
        QWORD speedUp;

        LOG("[%s] versus [%s]\n", StatNames[1], StatNames[0]);

        speedUp = (PerfStats[0].Mean * 1000) / PerfStats[1].Mean;
        LOG("Mean time speed-up: %2u.%03u\n", speedUp / 1000, speedUp % 1000);

        speedUp = (PerfStats[0].Max * 1000) / PerfStats[1].Min;
        LOG("Greatest speed-up: %2u.%03u\n", speedUp / 1000, speedUp % 1000);

        speedUp = (PerfStats[0].Min * 1000) / PerfStats[1].Max;
        LOG("Lowest speed-up: %2u.%03u\n", speedUp / 1000, speedUp % 1000);
    }
}