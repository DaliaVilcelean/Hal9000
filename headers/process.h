#pragma once

#include "process_defs.h"

typedef struct _PROCESS* PPROCESS;

//******************************************************************************
// Function:     ProcessCreate
// Description:  Creates a new process to execute the application found at
//               PathToExe with Arguments (may be NULL). The function returns a
//               pointer (handle) to the process structure.
// Returns:      STATUS
// Parameter:    IN_Z char * PathToExe
// Parameter:    IN_OPT_Z char * Arguments
// Parameter:    OUT_PTR PPROCESS * Process
// NOTE:         All the processes's threads may terminate, but the process data
//               structure will not be un-allocated until the handle receive in
//               Process is closed with ProcessCloseHandle.
//******************************************************************************
STATUS
ProcessCreate(
    IN_Z        char*       PathToExe,
    IN_OPT_Z    char*       Arguments,
    OUT_PTR     PPROCESS*   Process
    );

//******************************************************************************
// Function:     ProcessWaitForTermination
// Description:  Blocks until the process received as a parameter terminates
//               execution.
// Returns:      void
// Parameter:    IN PPROCESS Process
// Parameter:    OUT STATUS* TerminationStatus - Corresponds to the status of
//               the last exiting thread.
//******************************************************************************
void
ProcessWaitForTermination(
    IN          PPROCESS    Process,
    OUT         STATUS*     TerminationStatus
    );

//******************************************************************************
// Function:     ProcessCloseHandle
// Description:  Closes a process handle received from ProcessCreate. This is
//               necessary for the structure to be destroyed when it is no
//               longer needed.
// Returns:      void
// Parameter:    PPROCESS    Process
//******************************************************************************
void
ProcessCloseHandle(
    _Pre_valid_ _Post_invalid_
                PPROCESS    Process
    );

//******************************************************************************
// Function:     ProcessGetName
// Description:  Returns the name of the currently executing process (if the
//               parameter is NULL) or of the specified process
// Returns:      const char*
// Parameter:    IN_OPT PPROCESS Process
//******************************************************************************
const
char*
ProcessGetName(
    IN_OPT      PPROCESS    Process
    );

//******************************************************************************
// Function:     ProcessGetName
// Description:  Returns the PID of the currently executing process (if the
//               parameter is NULL) or of the specified process
// Returns:      PID
// Parameter:    IN_OPT PPROCESS Process
//******************************************************************************
PID
ProcessGetId(
    IN_OPT      PPROCESS    Process
    );

//******************************************************************************
// Function:     ProcessIsSystem
// Description:  Checks if a process or the currently executing process (if the
//               parameter is NULL) is the system process.
// Returns:      BOOLEAN
// Parameter:    IN_OPT PPROCESS Process
//******************************************************************************
BOOLEAN
ProcessIsSystem(
    IN_OPT      PPROCESS    Process
    );

//******************************************************************************
// Function:     ProcessTerminate
// Description:  Signals a process for termination (the current process will be
//               terminated if the parameter is NULL).
// Returns:      void
// Parameter:    INOUT PPROCESS Process
//******************************************************************************
void
ProcessTerminate(
    INOUT       PPROCESS    Process
    );

//******************************************************************************
// Function:     GetCurrentProcess
// Description:  Retrieves the executing process.
// Returns:      PPROCESS
// Parameter:    void
//******************************************************************************
PPROCESS
GetCurrentProcess(
    void
    );
