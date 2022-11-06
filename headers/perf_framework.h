#pragma once

typedef struct _PERFORMANCE_STATS
{
    QWORD               Mean;
    QWORD               Min;
    QWORD               Max;
} PERFORMANCE_STATS, *PPERFORMANCE_STATS;

typedef
void
(__cdecl FUNC_TestPerformance)(
    IN_OPT  PVOID       Context
    );

typedef FUNC_TestPerformance*   PFUNC_TestPerformance;

void
RunPerformanceFunction(
    IN      PFUNC_TestPerformance   Function,
    IN_OPT  PVOID                   Context,
    IN      DWORD                   IterationCount,
    IN      BOOLEAN                 MeasureInUs,
    OUT     PPERFORMANCE_STATS      PerfStats
    );

void
DisplayPerformanceStats(
    IN_READS(NumberOfStats)
            PPERFORMANCE_STATS      PerfStats,
    IN      DWORD                   NumberOfStats,
    IN_READS(NumberOfStats)
            char**                  StatNames
    );