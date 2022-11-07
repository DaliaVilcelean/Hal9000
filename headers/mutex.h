#pragma once

#include "list.h"
#include "synch.h"

typedef struct _MUTEX
{
    LOCK                MutexLock;

    BYTE                CurrentRecursivityDepth;
    BYTE                MaxRecursivityDepth;

    _Guarded_by_(MutexLock)
    LIST_ENTRY          WaitingList;
    struct _THREAD*     Holder;
} MUTEX, *PMUTEX;

//******************************************************************************
// Function:     MutexInit
// Description:  Initializes a mutex.
// Returns:      void
// Parameter:    OUT PMUTEX Mutex
// Parameter:    IN BOOLEAN Recursive - if TRUE the mutex may be acquired
//               several times by the same thread, else only once.
// NOTE:         A recursive mutex must be released as many times as it has been
//               acquired.
//******************************************************************************
_No_competing_thread_
void
MutexInit(
    OUT         PMUTEX      Mutex,
    IN          BOOLEAN     Recursive
    );

INT64
ThreadComparePriorityReadyList1
(IN PLIST_ENTRY lhs, IN PLIST_ENTRY rhs, IN_OPT  PVOID Context);

//******************************************************************************
// Function:     MutexAcquire
// Description:  Acquires a mutex. If the mutex is currently held the thread
//               is placed in a waiting list and its execution is blocked.
// Returns:      void
// Parameter:    INOUT PMUTEX Mutex
//******************************************************************************
ACQUIRES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_NOT_HELD_LOCK(*Mutex)
void
MutexAcquire(
    INOUT       PMUTEX      Mutex
    );

//******************************************************************************
// Function:     MutexRelease
// Description:  Releases a mutex. If there is a thread on the waiting list it
//               will be unblocked and placed as the lock's holder - this will
//               ensure fairness.
// Returns:      void
// Parameter:    INOUT PMUTEX Mutex
//******************************************************************************
RELEASES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_EXCL_LOCK(*Mutex)
void
MutexRelease(
    INOUT       PMUTEX      Mutex
    );


