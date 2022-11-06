#pragma once

#include "ex_timer.h"

#define TIMER_TEST_SLEEP_TIME_IN_US     (50 * MS_IN_US)
#define TIMER_TEST_SLEEP_INCREMENT      (10 * MS_IN_US)
#define TIMER_TEST_NO_OF_ITERATIONS     10

#define SLEEP_TIME_BIT_OFFSET           0x10
#define TIMER_TYPE_BIT_OFFSET           (SLEEP_TIME_BIT_OFFSET + 0x20)

#define DEFINE_TEST(Type,Us,Times)      ( ( ((QWORD)(Type)) << TIMER_TYPE_BIT_OFFSET) \
                                          | ((QWORD)((Us) & MAX_DWORD) << SLEEP_TIME_BIT_OFFSET) \
                                          | ((Times) & MAX_WORD))

#define TEST_TIMER_ABSOLUTE             DEFINE_TEST(ExTimerTypeAbsolute, TIMER_TEST_SLEEP_TIME_IN_US, 1)
#define TEST_TIMER_ABSOLUTE_PASSED      DEFINE_TEST(ExTimerTypeAbsolute, -TIMER_TEST_SLEEP_TIME_IN_US, 1)
#define TEST_TIMER_PERIODIC_MULTIPLE    DEFINE_TEST(ExTimerTypeRelativePeriodic, TIMER_TEST_SLEEP_TIME_IN_US, TIMER_TEST_NO_OF_ITERATIONS)
#define TEST_TIMER_PERIODIC_ONCE        DEFINE_TEST(ExTimerTypeRelativePeriodic, TIMER_TEST_SLEEP_TIME_IN_US, 1)
#define TEST_TIMER_PERIODIC_ZERO        DEFINE_TEST(ExTimerTypeRelativePeriodic, 0, 1)
#define TEST_TIMER_RELATIVE             DEFINE_TEST(ExTimerTypeRelativeOnce, TIMER_TEST_SLEEP_TIME_IN_US, 1)
#define TEST_TIMER_RELATIVE_ZERO        DEFINE_TEST(ExTimerTypeRelativeOnce, 0, 1)

FUNC_ThreadStart                        TestThreadTimerSleep;
FUNC_ThreadStart                        TestThreadTimerMultiple;

FUNC_ThreadPrepareTest                  TestThreadTimerPrepare;
FUNC_ThreadPostFinish                   TestThreadTimerMultipleTimersPostFinish;
FUNC_ThreadPostFinish					TestThreadTimerMultipleThreadsPostFinish;
