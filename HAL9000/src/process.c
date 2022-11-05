#include "HAL9000.h"
#include "mutex.h"
#include "thread_internal.h"
#include "process_internal.h"
#include "vmm.h"
#include "um_application.h"
#include "bitmap.h"
#include "pte.h"
#include "pe_exports.h"

typedef struct _PROCESS_SYSTEM_DATA
{
    MUTEX           PidBitmapLock;

    _Guarded_by_(PidBitmapLock)
    BITMAP          PidBitmap;
    BYTE            PidBitmapBuffer[PCID_TOTAL_NO_OF_VALUES/BITS_PER_BYTE];

    PPROCESS        SystemProcess;

    LIST_ENTRY      ProcessList;
    MUTEX           ProcessListLock;
} PROCESS_SYSTEM_DATA, *PPROCESS_SYSTEM_DATA;

static PROCESS_SYSTEM_DATA m_processData;

static
__forceinline
PID
_ProcessSystemRetrieveNextPid(
    void
    )
{
    PID idx;

    MutexAcquire(&m_processData.PidBitmapLock);
    idx = BitmapScanAndFlip(&m_processData.PidBitmap, 1, FALSE);
    MutexRelease(&m_processData.PidBitmapLock);

    ASSERT(PCID_IS_VALID(idx));

    return idx;
}

static
__forceinline
void
_ProcessSystemFreePid(
    IN      PID             ProcessId
    )
{
    ASSERT(PCID_IS_VALID(ProcessId));

    MutexAcquire(&m_processData.PidBitmapLock);
    ASSERT_INFO(BitmapGetBitValue(&m_processData.PidBitmap, (DWORD) ProcessId),
                "How can we free a process ID which is not used?!");
    BitmapClearBit(&m_processData.PidBitmap, (DWORD) ProcessId);
    MutexRelease(&m_processData.PidBitmapLock);
}

__forceinline
static
void
_ProcessReference(
    INOUT   PPROCESS        Process
    )
{
    ASSERT(NULL != Process);

    RfcReference(&Process->RefCnt);
}

__forceinline
static
void
_ProcessDereference(
    INOUT   PPROCESS        Process
    )
{
    ASSERT(NULL != Process);

    RfcDereference(&Process->RefCnt);
}

static
STATUS
_No_competing_thread_
_ProcessInit(
    IN_Z        char*       Name,
    IN_OPT_Z    char*       Arguments,
    OUT_PTR     PPROCESS*   Process
    );

static
STATUS
_ProcessParseCommandLine(
    INOUT       PPROCESS    Process,
    IN_OPT_Z    char*       CommandLine
    );

// Called when the reference count reaches zero
static FUNC_FreeFunction            _ProcessDestroy;

_No_competing_thread_
void
ProcessSystemPreinit(
    void
    )
{
    memzero(&m_processData, sizeof(PROCESS_SYSTEM_DATA));

    ASSERT(ARRAYSIZE(m_processData.PidBitmapBuffer) == BitmapPreinit(&m_processData.PidBitmap, PCID_TOTAL_NO_OF_VALUES));

    BitmapInit(&m_processData.PidBitmap, m_processData.PidBitmapBuffer);

    // the value zero cannot be used when CR4.PCIDE == 1, i.e. when PCID are used
    BitmapSetBit(&m_processData.PidBitmap, 0);

    MutexInit(&m_processData.PidBitmapLock, FALSE);

    MutexInit(&m_processData.ProcessListLock, FALSE);
    InitializeListHead(&m_processData.ProcessList);
}

_No_competing_thread_
STATUS
ProcessSystemInitSystemProcess(
    void
    )
{
    STATUS status;
    PPROCESS pProcess;

    status = _ProcessInit("System", NULL, &pProcess);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_ProcessInit", status);
        return status;
    }

    // This must be set here (as early as possible), it will be used by many process and MMU functions
    // to validate they are called accordingly
    m_processData.SystemProcess = pProcess;

    LOG_TRACE_PROCESS("Will initialize virtual space for system process!\n");

    MmuInitAddressSpaceForSystemProcess();

    // When this function will be called only the BSP will be active and only its main
    // thread will be running
    ProcessInsertThreadInList(pProcess, GetCurrentThread());

    return status;
}

PPROCESS
ProcessRetrieveSystemProcess(
    void
    )
{
    return m_processData.SystemProcess;
}

STATUS
ProcessExecuteForEachProcessEntry(
    IN      PFUNC_ListFunction  Function,
    IN_OPT  PVOID               Context
    )
{
    STATUS status;

    if (NULL == Function)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    status = STATUS_SUCCESS;

    MutexAcquire(&m_processData.ProcessListLock);
    status = ForEachElementExecute(&m_processData.ProcessList,
                                   Function,
                                   Context,
                                   FALSE
                                   );
    MutexRelease(&m_processData.ProcessListLock);

    return status;
}

//  warning C26130: Missing annotation _Requires_lock_held_(Process->PagingData->Lock) or _No_competing_thread_ at function 'ProcessActivatePagingTables'.
// Otherwise it could be a race condition. Variable 'Process->PagingData->Data' should be protected by lock 'Process->PagingData->Lock'.
// I do remember having a look at this function a lot of times, it's OK (I don't remember what the problem is though :( )
#pragma warning(push)
#pragma warning(disable:26130)

void
ProcessActivatePagingTables(
    IN      PPROCESS            Process,
    IN      BOOLEAN             InvalidateAddressSpace
    )
{
    ASSERT(Process != NULL);

    ASSERT(PCID_IS_VALID(Process->Id));

    VmmChangeCr3(Process->PagingData->Data.BasePhysicalAddress,
                 (PCID)Process->Id,
                 InvalidateAddressSpace);
}
#pragma warning(pop)

STATUS
ProcessCreate(
    IN_Z        char*       PathToExe,
    IN_OPT_Z    char*       Arguments,
    OUT_PTR     PPROCESS*   Process
    )
{
    STATUS status;
    PPROCESS pProcess;

    if (PathToExe == NULL)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (Process == NULL)
    {
        return STATUS_INVALID_PARAMETER3;
    }

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pProcess = NULL;

    __try
    {
        // It's impossible to create a process (ATM) without specifying a full PATH
        // => it's ok to add +1
        // Even if it were a relative path (in the current folder) it's not a problem:
        // the commonlib implementation of strrchr returns a pointer to the initial string
        // if the character is not found
        status = _ProcessInit(strrchr(PathToExe, '\\') + 1,
                              Arguments,
                              &pProcess);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_ProcessInit", status);
            __leave;
        }
        LOG_TRACE_PROCESS("Successfully initialized process!\n");

        // This function must be called before MmuCreateAddressSpaceForProcess to be able to
        // determine the address from which the VA allocations should start (so they'll not
        // conflict with the PE image)
        status = UmApplicationRetrieveHeader(PathToExe, pProcess->HeaderInfo);
        if (!SUCCEEDED(status))
        {
            LOG_TRACE_USERMODE("[ERROR]UmApplicationRetrieveHeader failed with status 0x%x\n", status);
            __leave;
        }
        LOG_TRACE_PROCESS("Successfully retrieved process NT header!\n");

        status = MmuCreateAddressSpaceForProcess(pProcess);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("MmuCreateVirtualSpaceForProcess", status);
            __leave;
        }
        LOG_TRACE_PROCESS("Successfully created VA space for process!\n");

        // Start execution
        status = UmApplicationRun(pProcess, FALSE, NULL);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("UmApplicationRun", status);
            __leave;
        }

        LOG_TRACE_PROCESS("Successfully ran UM application!\n");
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (pProcess != NULL)
            {
                _ProcessDereference(pProcess);
                pProcess = NULL;
            }
        }
        else
        {
            ASSERT(pProcess != NULL);

            // Increment the reference count so the process will still be valid even if it
            // terminates (i.e. the number of threads reach 0) - this is required because
            // we return the process pointer and we expect an explicit call to ProcessCloseHandle
            // to be able to destroy the object (i.e. for its ref count to reach zero)
            _ProcessReference(pProcess);
            *Process = pProcess;
        }
    }

    LOG_FUNC_END;

    return status;
}

void
ProcessWaitForTermination(
    IN          PPROCESS    Process,
    OUT         STATUS*     TerminationStatus
    )
{
    ASSERT(Process != NULL);
    ASSERT(TerminationStatus != NULL);

    ExEventWaitForSignal(&Process->TerminationEvt);
    *TerminationStatus = Process->TerminationStatus;
}

void
ProcessCloseHandle(
    _Pre_valid_ _Post_invalid_
                PPROCESS    Process
    )
{
    ASSERT(Process != NULL);

    _ProcessDereference(Process);
}

const
char*
ProcessGetName(
    IN_OPT      PPROCESS    Process
    )
{
    return (Process == NULL) ? GetCurrentThread()->Process->ProcessName : Process->ProcessName;
}

PID
ProcessGetId(
    IN_OPT      PPROCESS    Process
    )
{
    return (Process == NULL) ? GetCurrentThread()->Process->Id : Process->Id;
}

BOOLEAN
ProcessIsSystem(
    IN_OPT      PPROCESS    Process
    )
{
    PID pid = (Process == NULL) ? GetCurrentThread()->Process->Id : Process->Id;

    return m_processData.SystemProcess->Id == pid;
}

void
ProcessTerminate(
    INOUT       PPROCESS    Process
    )
{
    PTHREAD pCurrentThread;
    BOOLEAN bFoundCurThreadInProcess;
    INTR_STATE oldState;

    if (NULL == Process)
    {
        Process = GetCurrentProcess();
    }

    ASSERT(Process != NULL);
    ASSERT(!ProcessIsSystem(Process));

    pCurrentThread = GetCurrentThread();
    bFoundCurThreadInProcess = FALSE;

    // Go through the list of threads and notify each thread of termination
    // For the current thread (if it belongs to the process being terminated)
    // explicitly call ThreadExit
    LockAcquire(&Process->ThreadListLock, &oldState);
    for (PLIST_ENTRY pEntry = Process->ThreadList.Flink;
         pEntry != &Process->ThreadList;
         pEntry = pEntry->Flink)
    {
        PTHREAD pThread = CONTAINING_RECORD(pEntry, THREAD, ProcessList);

        if (pThread == pCurrentThread)
        {
            LOG_TRACE_PROCESS("Current thread in process we are about to terminate, will use ThreadExit at the end of the loop!\n");
            bFoundCurThreadInProcess = TRUE;
            continue;
        }

        ThreadTerminate(pThread);
    }
    LockRelease(&Process->ThreadListLock, oldState);

    if (bFoundCurThreadInProcess)
    {
        /// TODO: find out if we should also dereference the process here
        /// the idea is that the calling thread certainly didn't close the process handle
        /// => there is that extra reference hanging in the air
        ThreadExit(STATUS_JOB_INTERRUPTED);
        NOT_REACHED;
    }
}

PPROCESS
GetCurrentProcess(
    void
    )
{
    PTHREAD pThread = GetCurrentThread();
    ASSERT(pThread != NULL);

    return pThread->Process;
}

static
STATUS
_No_competing_thread_
_ProcessInit(
    IN_Z        char*       Name,
    IN_OPT_Z    char*       Arguments,
    OUT_PTR     PPROCESS*   Process
    )
{
    PPROCESS pProcess;
    STATUS status;
    DWORD nameSize;
    BOOLEAN bRefCntInitialized;

    ASSERT(Name != NULL);
    ASSERT(Process != NULL);

    pProcess = NULL;
    status = STATUS_SUCCESS;

    // we add +1 because of the NULL terminator
    nameSize = (strlen(Name)+1)*sizeof(char);
    bRefCntInitialized = FALSE;

    __try
    {
        pProcess = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PROCESS), HEAP_PROCESS_TAG, 0);
        if (pProcess == NULL)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(PROCESS));
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        RfcPreInit(&pProcess->RefCnt);

        status = RfcInit(&pProcess->RefCnt, _ProcessDestroy, NULL);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("RfcInit", status);
            __leave;
        }

        // It's ok to call ProcessDerefence from this point (the reference counter is initialized)
        bRefCntInitialized = TRUE;

        status = ExEventInit(&pProcess->TerminationEvt,
                             ExEventTypeNotification,
                             FALSE);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("ExEventInit", status);
            __leave;
        }

        InitializeListHead(&pProcess->NextProcess);

        pProcess->HeaderInfo = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PE_NT_HEADER_INFO), HEAP_PROCESS_TAG, 0);
        if (NULL == pProcess->HeaderInfo)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(PE_NT_HEADER_INFO));
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        pProcess->ProcessName = ExAllocatePoolWithTag(PoolAllocateZeroMemory, nameSize, HEAP_PROCESS_TAG, 0);
        if (NULL == pProcess->ProcessName)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", nameSize);
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }
        strcpy(pProcess->ProcessName, Name);

        // Setup Process->FullCommandLine
        status = _ProcessParseCommandLine(pProcess, Arguments);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_ProcessParseCommandLine", status);
            __leave;
        }
        LOG_TRACE_PROCESS("Successfully parsed process command line!\n");

        InitializeListHead(&pProcess->ThreadList);
        LockInit(&pProcess->ThreadListLock);

        // Do this as late as possible - we want to interfere as little as possible
        // with the system management in case something goes wrong (PID + full process
        // list management)
        pProcess->Id = _ProcessSystemRetrieveNextPid();

        MutexAcquire(&m_processData.ProcessListLock);
        InsertTailList(&m_processData.ProcessList, &pProcess->NextProcess);
        MutexRelease(&m_processData.ProcessListLock);

        LOG_TRACE_PROCESS("Process with PID 0x%X created\n", pProcess->Id);
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (pProcess != NULL)
            {
                bRefCntInitialized ? _ProcessDereference(pProcess) : _ProcessDestroy(pProcess, NULL);
                pProcess = NULL;
            }
        }
        else
        {
            *Process = pProcess;
        }
    }

    return status;
}

static
STATUS
_ProcessParseCommandLine(
    INOUT       PPROCESS    Process,
    IN_OPT_Z    char*       CommandLine
    )
{
    DWORD fullCmdLineSize;
    STATUS status;
    char* pFullCmdLine;
    DWORD noOfArgs;

    ASSERT(Process != NULL);
    ASSERT(Process->ProcessName != NULL);

    // strlen(CommandLine) + 1 because we have to add a space char between the program name and the last of the command line
    // +1 at the end because strlen(Process->ProcessName) returns the length of the process name without the NULL terminator
    fullCmdLineSize = strlen(Process->ProcessName) +
                        (CommandLine != NULL ? strlen(CommandLine) + 1 : 0) +
                        1;
    pFullCmdLine = NULL;
    status = STATUS_SUCCESS;
    noOfArgs = 0;

    __try
    {
        pFullCmdLine = ExAllocatePoolWithTag(PoolAllocateZeroMemory, fullCmdLineSize, HEAP_PROCESS_TAG, 0);
        if (pFullCmdLine == NULL)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", fullCmdLineSize);
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        status = snprintf(pFullCmdLine, fullCmdLineSize,
                          CommandLine != NULL ? "%s %s" : "%s",
                          Process->ProcessName, CommandLine);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("snprintf", status);
            __leave;
        }

        LOG_TRACE_PROCESS("Full process command line is [%s]\n", pFullCmdLine);

        noOfArgs = ((CommandLine != NULL) ? strcelem(CommandLine, ' ') : 0);
        ASSERT(noOfArgs != INVALID_STRING_SIZE);

        LOG_TRACE_PROCESS("Process has %u arguments without the process name!\n", noOfArgs);
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (pFullCmdLine != NULL)
            {
                ExFreePoolWithTag(pFullCmdLine, HEAP_PROCESS_TAG);
                pFullCmdLine = NULL;
            }
        }
        else
        {
            Process->FullCommandLine = pFullCmdLine;

            // +1 for the ProcessName
            Process->NumberOfArguments = noOfArgs + 1;
        }
    }

    return status;
}

void
ProcessInsertThreadInList(
    INOUT   PPROCESS            Process,
    INOUT   struct _THREAD*     Thread
    )
{
    DWORD activeThreads;
    INTR_STATE oldState;

    ASSERT(Process != NULL);
    ASSERT(Thread != NULL);

    LOG_TRACE_PROCESS("Will insert thread [%s] in process [%s]\n",
                      ThreadGetName(Thread), ProcessGetName(Process));

    Thread->Process = Process;

    LockAcquire(&Process->ThreadListLock, &oldState);

    ASSERT(Process->NumberOfThreads < MAX_DWORD);
    Process->NumberOfThreads++;

    activeThreads = _InterlockedIncrement(&Process->ActiveThreads);
    ASSERT(activeThreads <= Process->NumberOfThreads);

    InsertTailList(&Process->ThreadList, &Thread->ProcessList);

    // While there are still threads running the process MUST not be
    // destroyed => reference it
    _ProcessReference(Process);

    LockRelease(&Process->ThreadListLock, oldState);
}

void
ProcessNotifyThreadTermination(
    IN      struct _THREAD*     Thread
    )
{
    PPROCESS pProcess;
    DWORD activeThreads;

    ASSERT(Thread != NULL);

    pProcess = Thread->Process;
    ASSERT(pProcess != NULL);

    activeThreads = _InterlockedDecrement(&pProcess->ActiveThreads);

    // Once there are no more active threads in the process we need set the process exit
    // status and signal the process termination event
    if (activeThreads == 0)
    {
        LOG_TRACE_PROCESS("Last thread [%s] is finishing in process [%s]\n",
                          Thread->Name, pProcess->ProcessName);

        pProcess->TerminationStatus = Thread->ExitStatus;
        ExEventSignal(&pProcess->TerminationEvt);
    }
}

void
ProcessRemoveThreadFromList(
    INOUT   struct _THREAD*     Thread
    )
{
    PPROCESS pProcess;
    DWORD remainingThreads;
    INTR_STATE oldState;

    ASSERT(Thread != NULL);

    pProcess = Thread->Process;
    ASSERT(pProcess != NULL);

    LockAcquire(&pProcess->ThreadListLock, &oldState);

    remainingThreads = _InterlockedDecrement(&pProcess->NumberOfThreads);
    ASSERT_INFO(remainingThreads != MAX_DWORD,
                "If the process already had ZERO threads what thread can we remove??");

    RemoveEntryList(&Thread->ProcessList);
    _ProcessDereference(pProcess);

    LockRelease(&pProcess->ThreadListLock, oldState);

    // If the number of threads in the process has reached 0 it's clear that it's
    // impossible for new threads to spawn => we can dereference the process
    // The process might not be destroyed yet if there is still an open handle to it
    if (remainingThreads == 0)
    {
        _ProcessDereference(pProcess);
    }
}

static
void
_ProcessDestroy(
    IN      PVOID                   Object,
    IN_OPT  PVOID                   Context
    )
{
    PPROCESS Process = (PPROCESS) CONTAINING_RECORD(Object, PROCESS, RefCnt);

    ASSERT(NULL != Process);
    ASSERT(!ProcessIsSystem(Process));
    ASSERT(NULL == Context);

    // It would be really weird if we could destroy the process VAS while there are
    // still some running threads
    ASSERT(Process->NumberOfThreads == 0);

    LOG_TRACE_PROCESS("Will destroy process with PID 0x%X\n", Process->Id);

    // It's ok to use the remove entry list function because when we create the process we call
    // InitializeListHead => the RemoveEntryList has no problem with an empty list as long as it
    // is initialized :)
    MutexAcquire(&m_processData.ProcessListLock);
    RemoveEntryList(&Process->NextProcess);
    MutexRelease(&m_processData.ProcessListLock);

    if (NULL != Process->FullCommandLine)
    {
        ExFreePoolWithTag(Process->FullCommandLine, HEAP_PROCESS_TAG);
        Process->FullCommandLine = NULL;
    }

    if (NULL != Process->ProcessName)
    {
        ExFreePoolWithTag(Process->ProcessName, HEAP_PROCESS_TAG);
        Process->ProcessName = NULL;
    }

    if (NULL != Process->HeaderInfo)
    {
        ExFreePoolWithTag(Process->HeaderInfo, HEAP_PROCESS_TAG);
        Process->HeaderInfo = NULL;
    }

    // Because the system process will never be destroyed it is ok to free
    // these memory addresses unconditionally
    MmuDestroyAddressSpaceForProcess(Process);

    if (Process->Id != 0)
    {
        // This should be done only after MmuDestroyVirtualSpaceForProcess, that
        // function is also responsible for destroying all cached translations for
        // this process ID
        _ProcessSystemFreePid(Process->Id);
    }

    ExFreePoolWithTag(Process, HEAP_PROCESS_TAG);
}
