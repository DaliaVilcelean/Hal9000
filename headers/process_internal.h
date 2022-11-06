#pragma once

#include "list.h"
#include "ref_cnt.h"
#include "process.h"
#include "synch.h"
#include "ex_event.h"

#define PROCESS_MAX_PHYSICAL_FRAMES     16
#define PROCESS_MAX_OPEN_FILES          16

typedef struct _PROCESS
{
    REF_COUNT                       RefCnt;

    // The PIDs will also be used for the CR3 PCID
    PID                             Id;

    char*                           ProcessName;

    // Command line related

    // The command line also contains the ProcessName
    char*                           FullCommandLine;
    DWORD                           NumberOfArguments;

    // Signaled when the last thread is removed from the
    // process list
    EX_EVENT                        TerminationEvt;

    // Valid only if TerminationEvt is signaled. The status
    // of the process is given by the status of the last
    // exiting thread from the process.
    STATUS                          TerminationStatus;

    LOCK                            ThreadListLock;

    _Guarded_by_(ThreadListLock)
    LIST_ENTRY                      ThreadList;

    _Guarded_by_(ThreadListLock)
    volatile DWORD                  NumberOfThreads;

    // The difference between NumberOfThreads and ActiveThreads is the following
    // ActiveThreads represents number of threads in process which have not died have
    // NumberOfThreads includes the threads which died but have not yet been destroyed
    _Interlocked_
    volatile DWORD                  ActiveThreads;

    // Links all the processes in the global process list
    LIST_ENTRY                      NextProcess;

    // Pointer to the process' paging structures
    struct _PAGING_LOCK_DATA*       PagingData;

    // Pointer to the process' NT header information
    struct _PE_NT_HEADER_INFO*      HeaderInfo;

    // VaSpace used only for UM virtual memory allocations
    struct _VMM_RESERVATION_SPACE*  VaSpace;
} PROCESS, *PPROCESS;

//******************************************************************************
// Function:     ProcessSystemPreinit
// Description:  Basic global initialization. Initializes the PID bitmap, the
//               process list and their associated locks.
// Returns:      void
// Parameter:    void
//******************************************************************************
_No_competing_thread_
void
ProcessSystemPreinit(
    void
    );

//******************************************************************************
// Function:     ProcessSystemInitSystemProcess
// Description:  Initializes the System process.
// Returns:      STATUS
// Parameter:    void
//******************************************************************************
_No_competing_thread_
STATUS
ProcessSystemInitSystemProcess(
    void
    );

//******************************************************************************
// Function:     ProcessRetrieveSystemProcess
// Description:  Retrieves a pointer to the system process.
// Returns:      PPROCESS
// Parameter:    void
//******************************************************************************
PPROCESS
ProcessRetrieveSystemProcess(
    void
    );

//******************************************************************************
// Function:     ProcessInsertThreadInList
// Description:  Inserts the Thread in the Process thread list.
// Returns:      void
// Parameter:    INOUT PPROCESS Process
// Parameter:    INOUT struct _THREAD * Thread
//******************************************************************************
void
ProcessInsertThreadInList(
    INOUT   PPROCESS            Process,
    INOUT   struct _THREAD*     Thread
    );

//******************************************************************************
// Function:     ProcessNotifyThreadTermination
// Description:  Called when a thread terminates execution. If this was the last
//               active thread in the process it will signal the processes's
//               termination event.
// Returns:      void
// Parameter:    IN struct _THREAD * Thread
//******************************************************************************
void
ProcessNotifyThreadTermination(
    IN      struct _THREAD*     Thread
    );

//******************************************************************************
// Function:     ProcessRemoveThreadFromList
// Description:  Removes the Thread from its container process thread list.
//               Called when a thread is destroyed.
// Returns:      void
// Parameter:    INOUT struct _THREAD * Thread
//******************************************************************************
void
ProcessRemoveThreadFromList(
    INOUT   struct _THREAD*     Thread
    );

//******************************************************************************
// Function:     ProcessExecuteForEachProcessEntry
// Description:  Iterates over the all threads list and invokes Function on each
//               entry passing an additional optional Context parameter.
// Returns:      STATUS
// Parameter:    IN PFUNC_ListFunction Function
// Parameter:    IN_OPT PVOID Context
//******************************************************************************
STATUS
ProcessExecuteForEachProcessEntry(
    IN      PFUNC_ListFunction  Function,
    IN_OPT  PVOID               Context
    );

//******************************************************************************
// Function:     ProcessActivatePagingTables
// Description:  Performs a switch to the Process paging tables.
// Returns:      void
// Parameter:    IN PPROCESS Process
// Parameter:    IN BOOLEAN InvalidateAddressSpace - if TRUE all the cached
//               translations for the Process PCID will be flushed. This option
//               is useful when a process terminates and its PCID will be
//               later used by another process.
//******************************************************************************
void
ProcessActivatePagingTables(
    IN      PPROCESS            Process,
    IN      BOOLEAN             InvalidateAddressSpace
    );
