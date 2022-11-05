#include "HAL9000.h"
#include "dmp_common.h"
#include "dmp_process.h"
#include "thread_internal.h"
#include "process_internal.h"

void
DumpProcess(
    IN  PPROCESS    Process
    )
{
    INTR_STATE oldState;

    ASSERT(Process != NULL);

    oldState = DumpTakeLock();

    _Benign_race_begin_

    LOG("Process %U - 0x%X\n", Process->Id, Process->Id);
    LOG("----------------------\n");
    LOG("Name is [%s]\n", Process->ProcessName);
    LOG("Command line is [%s]\n", Process->FullCommandLine);
    LOG("Number of arguments %u\n", Process->NumberOfArguments);
    LOG("Process VA space at 0x%X\n", Process->VaSpace);
    LOG("Number of threads: %u\n", Process->NumberOfThreads);
    LOG("Reference count is: %u\n", Process->RefCnt.ReferenceCount);

    _Benign_race_end_

    DumpReleaseLock(oldState);
}