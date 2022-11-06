#pragma once

#include "HAL9000.h"
#include "print.h"

#define TEST_MAX_NO_OF_HEAP_ALLOCATIONS     50

#define LOG_TEST_LOG(buf,...)               LogEx(LogLevelTrace, LogComponentTest, "[TEST]"##buf, __VA_ARGS__)
#define LOG_TEST_PASS                       LOG_TEST_LOG("\n[PASS]\n");

void
TestRunAllFunctional(
    void
    );

void
TestRunAllPerformance(
    void
    );