#pragma once

typedef struct _PROCESS_TEST
{
    char*                       TestName;
    char*                       ProcessName;
    char*                       ProcessCommandLine;
    DWORD                       NumberOfProcesses;
} PROCESS_TEST, *PPROCESS_TEST;

extern const PROCESS_TEST PROCESS_TESTS[];

extern const DWORD PROCESS_TOTAL_NO_OF_TESTS;

void
TestProcessFunctionality(
    IN      PROCESS_TEST*               ProcessTest
    );

void
TestAllProcessFunctionalities(
    void
    );
