#pragma once

#include "thread.h"

typedef
void
(__cdecl FUNC_ThreadPrepareTest)(
    OUT_OPT_PTR     PVOID*              Context,
    IN              DWORD               NumberOfThreads,
    IN              PVOID               PrepareContext
    );

typedef     FUNC_ThreadPrepareTest*             PFUNC_ThreadPrepareTest;

typedef
void
(__cdecl FUNC_ThreadPostCreate)(
    IN              PVOID               Context
    );

typedef     FUNC_ThreadPostCreate*              PFUNC_ThreadPostCreate;

typedef
void
(__cdecl FUNC_ThreadPostFinish)(
    IN              PVOID               Context,
    IN              DWORD               NumberOfThreads
    );

typedef     FUNC_ThreadPostFinish*              PFUNC_ThreadPostFinish;

typedef struct _THREAD_TEST
{
    char*                       TestName;
    PFUNC_ThreadStart           ThreadFunction;
    PFUNC_ThreadPrepareTest     ThreadPrepareFunction;
    PVOID                       PrepareFunctionContext;
    PFUNC_ThreadPostCreate      ThreadPostCreateFunction;
    PFUNC_ThreadPostFinish      ThreadPostFinishFunction;
    THREAD_PRIORITY             BasePriority;
    BOOLEAN                     IncrementPriorities;

    BOOLEAN                     IgnoreThreadCount;
    BOOLEAN                     ArrayOfContexts;
} THREAD_TEST, *PTHREAD_TEST;


extern const THREAD_TEST THREADS_TEST[];

extern const DWORD THREADS_TOTAL_NO_OF_TESTS;

void
TestThreadFunctionality(
    IN      THREAD_TEST*                ThreadTest,
    IN_OPT  PVOID                       ContextForTestFunction,
    IN      DWORD                       NumberOfThreads
    );

void
TestAllThreadFunctionalities(
    IN      DWORD                       NumberOfThreads
    );