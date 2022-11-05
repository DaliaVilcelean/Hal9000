#include "test_common.h"
#include "test_process.h"
#include "process.h"
#include "iomu.h"

#define MAX_PROCESSES_TO_SPAWN          16

const PROCESS_TEST PROCESS_TESTS[] =
{
    // Project 2: Userprog

    // arguments
    { "TestUserArgsNone", "Args", NULL},
    { "TestUserArgsOne", "Args", "Argument"},
    { "TestUserArgsMany", "Args", "Johnny is a good kid"},
    { "TestUserArgsAll", "Args", "a b c d e f g h i j k l m n o p r s t u v q x y z"},

    // bad-actions
    { "BadJumpKernel", "BadJumpKernel", NULL},
    { "BadJumpNoncanonical", "BadJumpNoncanonical", NULL},
    { "BadJumpNull", "BadJumpNull", NULL},

    { "BadReadIoPort", "BadReadIoPort", NULL},
    { "BadReadKernel", "BadReadKernel", NULL},
    { "BadReadMsr", "BadReadMsr", NULL},
    { "BadReadNonCanonical", "BadReadNonCanonical", NULL},
    { "BadReadNull", "BadReadNull", NULL},

    { "BadWriteIoPort", "BadWriteIoPort", NULL},
    { "BadWriteKernel", "BadWriteKernel", NULL},
    { "BadWriteMsr", "BadWriteMsr", NULL},
    { "BadWriteNonCanonical", "BadWriteNonCanonical", NULL},
    { "BadWriteNull", "BadWriteNull", NULL},

    // file-syscalls
    { "FileCloseBad", "FileCloseBad", NULL},
    { "FileCloseNormal", "FileCloseNormal", NULL},
    { "FileCloseStdout", "FileCloseStdout", NULL},
    { "FileCloseTwice", "FileCloseTwice", NULL},

    { "FileCreateBadPointer", "FileCreateBadPointer", NULL},
    { "FileCreateEmptyPath", "FileCreateEmptyPath", NULL},
    { "FileCreateExistent", "FileCreateExistent", NULL},
    { "FileCreateMissing", "FileCreateMissing", NULL},
    { "FileCreateNormal", "FileCreateNormal", NULL},
    { "FileCreateNull", "FileCreateNull", NULL},
    { "FileCreateTwice", "FileCreateTwice", NULL},

    { "FileReadBadHandle", "FileReadBadHandle", NULL},
    { "FileReadBadPointer", "FileReadBadPointer", NULL},
    { "FileReadKernel", "FileReadKernel", NULL},
    { "FileReadNormal", "FileReadNormal", NULL},
    { "FileReadStdout", "FileReadStdout", NULL},
    { "FileReadZero", "FileReadZero", NULL},

    // process-syscalls
    { "ProcessCloseFile", "ProcessCloseFile", NULL},
    { "ProcessCloseNormal", "ProcessCloseNormal", NULL},
    { "ProcessCloseParentHandle", "ProcessCloseParentHandle", NULL},
    { "ProcessCloseTwice", "ProcessCloseTwice", NULL},

    { "ProcessCreateBadPointer", "ProcessCreateBadPointer", NULL},
    { "ProcessCreateMissingFile", "ProcessCreateMissingFile", NULL},
    { "ProcessCreateMultiple", "ProcessCreateMultiple", NULL},
    { "ProcessCreateOnce", "ProcessCreateOnce", NULL},
    { "ProcessCreateWithArguments", "ProcessCreateWithArguments", NULL},

    { "ProcessExit", "ProcessExit", NULL},
    { "ProcessGetPid", "ProcessGetPid", NULL},

    { "ProcessWaitBadHandle", "ProcessWaitBadHandle", NULL},
    { "ProcessWaitClosedHandle", "ProcessWaitClosedHandle", NULL},
    { "ProcessWaitNormal", "ProcessWaitNormal", NULL},
    { "ProcessWaitTerminated", "ProcessWaitTerminated", NULL},

    // thread-syscalls
    { "ThreadCloseTwice", "ThreadCloseTwice", NULL},

    { "ThreadCreateBadPointer", "ThreadCreateBadPointer", NULL},
    { "ThreadCreateMultiple", "ThreadCreateMultiple", NULL},
    { "ThreadCreateOnce", "ThreadCreateOnce", NULL},
    { "ThreadCreateWithArguments", "ThreadCreateWithArguments", NULL},

    { "ThreadExit", "ThreadExit", NULL},
    { "ThreadGetTid", "ThreadGetTid", NULL},

    { "ThreadWaitBadHandle", "ThreadWaitBadHandle", NULL},
    { "ThreadWaitClosedHandle", "ThreadWaitClosedHandle", NULL},
    { "ThreadWaitNormal", "ThreadWaitNormal", NULL},
    { "ThreadWaitTerminated", "ThreadWaitTerminated", NULL},

    // Project 3: Virtual Memory

    // process-quota
    { "ProcessQuotaGood", "ProcessQuotaGood", NULL},
    { "ProcessQuotaJustRight", "ProcessQuotaJustRight", NULL},
    { "ProcessQuotaMore", "ProcessQuotaMore", NULL},

    // swap
    { "SwapLinear", "SwapLinear", NULL},
    { "SwapMultiple", "SwapLinear", NULL, 4},
    { "SwapMultipleShared", "SwapMultipleShared", "0"},
    { "SwapZeros", "SwapZeros", NULL},
    { "SwapZerosWritten", "SwapZerosWritten", NULL},

    // syscalls
    { "VirtualAllocAccessFail", "VirtualAllocAccessFail", NULL},
    { "VirtualAllocHugeEager", "VirtualAllocHugeEager", NULL},
    { "VirtualAllocHugeLazy", "VirtualAllocHugeLazy", NULL},
    { "VirtualAllocNormal", "VirtualAllocNormal", NULL},
    { "VirtualAllocWriteExec", "VirtualAllocWriteExec", NULL},
    { "VirtualAllocZeros", "VirtualAllocZeros", NULL },

    //{ "VirtualFreeInvalid", "VirtualFreeInvalid", NULL },
    //{ "VirtualFreeMore", "VirtualFreeMore", NULL },

    { "VirtualSharedDifferentSize", "VirtualSharedNormal", "0 1 8 3" },
    { "VirtualSharedHugeEager", "VirtualSharedNormal", "0 1 4096 4" },
    { "VirtualSharedHugeLazy", "VirtualSharedNormal", "0 1 4096 5" },
    { "VirtualSharedLessAccess", "VirtualSharedNormal", "0 1 8 1" },
    { "VirtualSharedMoreAccess", "VirtualSharedNormal", "0 0 8 2" },
    { "VirtualSharedNormal", "VirtualSharedNormal", "0 1 8 0" },

    // stack-growth
    { "StackGrowthVariables", "StackGrowthVariables", NULL },
    { "StackGrowthRecursiveFunction", "StackGrowthRecursiveFunction", NULL },
};

const DWORD PROCESS_TOTAL_NO_OF_TESTS = ARRAYSIZE(PROCESS_TESTS);


void
TestProcessFunctionality(
    IN      PROCESS_TEST*               ProcessTest
    )
{
    STATUS status;
    STATUS terminationStatus;
    PPROCESS pProcesses[MAX_PROCESSES_TO_SPAWN];
    char fullPath[MAX_PATH];
    const char* pSystemPartition;
    DWORD noOfProcesses;

    pSystemPartition = IomuGetSystemPartitionPath();
    noOfProcesses = (ProcessTest->NumberOfProcesses == 0) ? 1 : ProcessTest->NumberOfProcesses;
    ASSERT(noOfProcesses <= MAX_PROCESSES_TO_SPAWN);

    LOG_TEST_LOG("Test [%s] START!\n", ProcessTest->TestName);

    __try
    {
        if (pSystemPartition == NULL)
        {
            LOG_ERROR("Cannot run user tests without knowing the system partition!\n");
            __leave;
        }

        snprintf(fullPath, MAX_PATH,
                 "%s%s\\%s.exe", pSystemPartition, "APPLICATIONS",
                 ProcessTest->ProcessName);

        printf("Full path is [%s]\n", fullPath);

        for (DWORD i = 0; i < noOfProcesses; ++i)
        {
            status = ProcessCreate(fullPath,
                                   ProcessTest->ProcessCommandLine,
                                   &pProcesses[i]);
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("ProcessCreate", status);
                __leave;
            }
        }

        for (DWORD i = 0; i < noOfProcesses; ++i)
        {
            ProcessWaitForTermination(pProcesses[i], &terminationStatus);

            ProcessCloseHandle(pProcesses[i]);
            pProcesses[i] = NULL;
        }
    }
    __finally
    {
        LOG_TEST_LOG("Test [%s] END!\n", ProcessTest->TestName);
    }
}

void
TestAllProcessFunctionalities(
    void
    )
{
    for (DWORD i = 0; i < PROCESS_TOTAL_NO_OF_TESTS; ++i)
    {
        TestProcessFunctionality(&PROCESS_TESTS[i]);
    }
}
