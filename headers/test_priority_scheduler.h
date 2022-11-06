#pragma once

FUNC_ThreadStart            TestThreadPriorityMutex;
FUNC_ThreadPrepareTest      TestPrepareMutex;
FUNC_ThreadPostCreate       TestThreadPostCreateMutex;
FUNC_ThreadPostFinish       TestThreadPostFinishMutex;

FUNC_ThreadStart            TestThreadPriorityWakeup;
FUNC_ThreadPrepareTest      TestThreadPrepareWakeupEvent;
FUNC_ThreadPostCreate       TestThreadPostCreateWakeup;
FUNC_ThreadPostFinish       TestThreadPostFinishWakeup;

FUNC_ThreadStart            TestThreadPriorityExecution;
FUNC_ThreadPrepareTest      TestThreadPreparePriorityExecution;
FUNC_ThreadPostFinish       TestThreadPostPriorityExecution;
