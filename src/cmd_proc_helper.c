#include "HAL9000.h"
#include "cmd_proc_helper.h"
#include "thread_internal.h"
#include "process_internal.h"
#include "print.h"
#include "display.h"
#include "dmp_process.h"
#include "strutils.h"
#include "test_process.h"

typedef struct _PROC_STAT_CTX
{
    PID             ProcessPid;
    BOOLEAN         FoundProcess;
} PROC_STAT_CTX, *PPROC_STAT_CTX;

#pragma warning(push)

// warning C4212: nonstandard extension used: function declaration used ellipsis
#pragma warning(disable:4212)

// warning C4029: declared formal parameter list different from definition
#pragma warning(disable:4029)

__forceinline
static
void
_CmdHelperPrintProcessFunctions(
    void
    )
{
    DWORD i;

    for (i = 0; i < PROCESS_TOTAL_NO_OF_TESTS; ++i)
    {
        printf("%2u. %s\n", i, PROCESS_TESTS[i].TestName);
    }
}

static FUNC_ListFunction _CmdProcessPrint;

void
(__cdecl CmdListProcesses)(
    IN      QWORD       NumberOfParameters
    )
{
    STATUS status;

    ASSERT(NumberOfParameters == 0);

    printColor(MAGENTA_COLOR, "%10s", "PID|");
    printColor(MAGENTA_COLOR, "%10s", "Arg count|");
    printColor(MAGENTA_COLOR, "%39s", "Command line|");
    printColor(MAGENTA_COLOR, "%9s", "Threads|");
    printColor(MAGENTA_COLOR, "%12s", "References|");

    status = ProcessExecuteForEachProcessEntry(_CmdProcessPrint, NULL);
    ASSERT(SUCCEEDED(status));
}

void
(__cdecl CmdProcessDump)(
    IN      QWORD       NumberOfParameters,
    IN      char*       PidString
    )
{
    PROC_STAT_CTX statCtx = { 0 };
    STATUS status;
    PID pid;

    ASSERT(NumberOfParameters == 1);
    atoi64(&pid, PidString, BASE_HEXA);

    statCtx.ProcessPid = pid;

    status = ProcessExecuteForEachProcessEntry(_CmdProcessPrint, &statCtx);
    ASSERT(SUCCEEDED(status));

    if (!statCtx.FoundProcess)
    {
        pwarn("Process with PID 0x%X does not exist!\n", statCtx.ProcessPid);
    }
}

void
(__cdecl CmdStartProcess)(
    IN          QWORD   NumberOfParameters,
    IN_Z        char*   ProcessPath
    )
{
    STATUS status;
    PPROCESS pProcess;

    ASSERT(NumberOfParameters == 1);

    status = ProcessCreate(ProcessPath,
                           NULL,
                           &pProcess);
    if (!SUCCEEDED(status))
    {
        perror("ProcessCreate failed with status 0x%x\n", status);
        return;
    }

    printf("Successfully created process [%s] with PID 0x%X\n", ProcessPath, pProcess->Id);

    ProcessCloseHandle(pProcess);
}

static
STATUS
(__cdecl _CmdProcessPrint) (
    IN      PLIST_ENTRY     ListEntry,
    IN_OPT  PVOID           FunctionContext
    )
{
    PPROCESS pProcess;

    ASSERT(NULL != ListEntry);

    pProcess = CONTAINING_RECORD(ListEntry, PROCESS, NextProcess);

    if (FunctionContext == NULL)
    {
        DWORD cmdLineLength = strlen(pProcess->FullCommandLine);
        ASSERT(cmdLineLength != INVALID_STRING_SIZE);

        printf("%9x%c", pProcess->Id, '|');
        printf("%9u%c", pProcess->NumberOfArguments, '|');
        printf(cmdLineLength > 38 ? "%35S...%c" : "%38S%c", pProcess->FullCommandLine, '|');
        printf("%8U%c", pProcess->NumberOfThreads, '|');
        printf("%11U%c", pProcess->RefCnt.ReferenceCount, '|');
    }
    else
    {
        PPROC_STAT_CTX pCtx = (PPROC_STAT_CTX) FunctionContext;

        if (pCtx->ProcessPid == pProcess->Id)
        {
            pCtx->FoundProcess = TRUE;

            DumpProcess(pProcess);
        }
    }

    return STATUS_SUCCESS;
}

#include "test_common.h"

void
(__cdecl CmdTestProcess)(
    IN          QWORD       NumberOfParameters,
    IN_Z        char*       TestName
    )
{
    BOOLEAN bFoundTest;

    ASSERT(NumberOfParameters == 1);

    bFoundTest = FALSE;

    for (DWORD i = 0; i < PROCESS_TOTAL_NO_OF_TESTS; ++i)
    {
        if (0 == stricmp(PROCESS_TESTS[i].TestName, TestName))
        {
            TestProcessFunctionality(&PROCESS_TESTS[i]);
            bFoundTest = TRUE;
            break;
        }
    }

    if (!bFoundTest)
    {
        LOG_WARNING("Test [%s] does not exist. Try one of the following tests:\n", TestName);
        _CmdHelperPrintProcessFunctions();
    }
    else
    {
        printf("Finished running test [%s]\n", TestName);
    }
}

#pragma warning(pop)
