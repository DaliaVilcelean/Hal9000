#pragma once

#include "list.h"
#include "ref_cnt.h"
#include "ex_event.h"
#include "thread.h"
#include <mutex.h>

typedef enum _THREAD_STATE
{
    // currently executing on a CPU
    ThreadStateRunning,

    // in ready list, i.e. it is ready to be executed
    // when it will be scheduled
    ThreadStateReady,

    // it is waiting for a resource (mutex, ex event, ex timer)
    // and cannot be scheduled until it is unblocked by another
    // thread
    ThreadStateBlocked,

    // the thread has already executed its last quanta - it will
    // be destroyed before the next thread in the ready list resumes
    // execution
    ThreadStateDying,
    ThreadStateReserved = ThreadStateDying + 1
} THREAD_STATE;

typedef DWORD           THREAD_FLAGS;

#define THREAD_FLAG_FORCE_TERMINATE_PENDING         0x1
#define THREAD_FLAG_FORCE_TERMINATED                0x2

typedef struct _THREAD
{
    REF_COUNT               RefCnt;

    struct _THREAD          *Self;

    TID                     Id;
    char*                   Name;

    // Currently the thread priority is not used for anything
    THREAD_PRIORITY         Priority;

    // the real ( original ) priority
    THREAD_PRIORITY         RealPriority;
    // the list of mutexes held by thread
    LIST_ENTRY              AcquiredMutexesList;

    PMUTEX WaitedMutex; // the mutex thread waits for
    THREAD_STATE            State;

    // valid only if State == ThreadStateTerminated
    STATUS                  ExitStatus;
    EX_EVENT                TerminationEvt;

    volatile THREAD_FLAGS   Flags;

    // Lock which ensures there are no race conditions between a thread that
    // blocks and a thread on another CPU which wants to unblock it
    LOCK                    BlockLock;

    // List of all the threads in the system (including those blocked or dying)
    LIST_ENTRY              AllList;

    // List of the threads ready to run
    LIST_ENTRY              ReadyList;

    // List of the threads in the same process
    LIST_ENTRY              ProcessList;

    // Incremented on each clock tick for the running thread
    QWORD                   TickCountCompleted;

    // Counts the number of ticks the thread has currently run without being
    // de-scheduled, i.e. if the thread yields the CPU to another thread the
    // count will be reset to 0, else if the thread yields, but it will
    // scheduled again the value will be incremented.
    QWORD                   UninterruptedTicks;

    // Incremented if the thread yields the CPU before the clock
    // ticks, i.e. by yielding or by blocking
    QWORD                   TickCountEarly;

    // The highest valid address for the kernel stack (its initial value)
    PVOID                   InitialStackBase;

    // The size of the kernel stack
    DWORD                   StackSize;

    // The current kernel stack pointer (it gets updated on each thread switch,
    // its used when resuming thread execution)
    PVOID                   Stack;

    // MUST be non-NULL for all threads which belong to user-mode processes
    PVOID                   UserStack;

    struct _PROCESS*        Process;
} THREAD, *PTHREAD;

//******************************************************************************
// Function:     ThreadSystemPreinit
// Description:  Basic global initialization. Initializes the all threads list,
//               the ready list and all the locks protecting the global
//               structures.
// Returns:      void
// Parameter:    void
//******************************************************************************
void
_No_competing_thread_
ThreadSystemPreinit(
    void
    );

//******************************************************************************
// Function:     ThreadSystemInitMainForCurrentCPU
// Description:  Call by each CPU to initialize the main execution thread. Has a
//               different flow than any other thread creation because some of
//               the thread information already exists and it is currently
//               running.
// Returns:      STATUS
// Parameter:    void
//******************************************************************************
STATUS
ThreadSystemInitMainForCurrentCPU(
    void
    );

//******************************************************************************
// Function:     ThreadSystemInitIdleForCurrentCPU
// Description:  Called by each CPU to spawn the idle thread. Execution will not
//               continue until after the idle thread is first scheduled on the
//               CPU. This function is also responsible for enabling interrupts
//               on the processor.
// Returns:      STATUS
// Parameter:    void
//******************************************************************************
STATUS
ThreadSystemInitIdleForCurrentCPU(
    void
    );

//******************************************************************************
// Function:     ThreadCreateEx
// Description:  Same as ThreadCreate except it also takes an additional
//               parameter, the process to which the thread should belong. This
//               function must be called for creating user-mode threads.
// Returns:      STATUS
// Parameter:    IN_Z char * Name
// Parameter:    IN THREAD_PRIORITY Priority
// Parameter:    IN PFUNC_ThreadStart Function
// Parameter:    IN_OPT PVOID Context
// Parameter:    OUT_PTR PTHREAD * Thread
// Parameter:    INOUT struct _PROCESS * Process
//******************************************************************************
STATUS
ThreadCreateEx(
    IN_Z        char*               Name,
    IN          THREAD_PRIORITY     Priority,
    IN          PFUNC_ThreadStart   Function,
    IN_OPT      PVOID               Context,
    OUT_PTR     PTHREAD*            Thread,
    INOUT       struct _PROCESS*    Process
    );

//******************************************************************************
// Function:     ThreadTick
// Description:  Called by the timer interrupt at each timer tick. It keeps
//               track of thread statistics and triggers the scheduler when a
//               time slice expires.
// Returns:      void
// Parameter:    void
//******************************************************************************
void
ThreadTick(
    void
    );

//******************************************************************************
// Function:     ThreadBlock
// Description:  Transitions the running thread into the blocked state. The
//               thread will not run again until it is unblocked (ThreadUnblock)
// Returns:      void
// Parameter:    void
//******************************************************************************
void
ThreadBlock(
    void
    );

//******************************************************************************
// Function:     ThreadUnblock
// Description:  Transitions thread, which must be in the blocked state, to the
//               ready state, allowing it to resume running. This is called when
//               the resource on which the thread is waiting for becomes
//               available.
// Returns:      void
// Parameter:    IN PTHREAD Thread
//******************************************************************************
void
ThreadUnblock(
    IN      PTHREAD              Thread
    );

//******************************************************************************
// Function:     ThreadYieldOnInterrupt
// Description:  Returns TRUE if the thread must yield the CPU at the end of
//               this interrupt. FALSE otherwise.
// Returns:      BOOLEAN
// Parameter:    void
//******************************************************************************
BOOLEAN
ThreadYieldOnInterrupt(
    void
    );

//******************************************************************************
// Function:     ThreadTerminate
// Description:  Signals a thread to terminate.
// Returns:      void
// Parameter:    INOUT PTHREAD Thread
// NOTE:         This function does not cause the thread to instantly terminate,
//               if you want to wait for the thread to terminate use
//               ThreadWaitForTermination.
// NOTE:         This function should be used only in EXTREME cases because it
//               will not free the resources acquired by the thread.
//******************************************************************************
void
ThreadTerminate(
    INOUT   PTHREAD             Thread
    );

//******************************************************************************
// Function:     ThreadTakeBlockLock
// Description:  Takes the block lock for the executing thread. This is required
//               to avoid a race condition which would happen if a thread is
//               unblocked while in the process of being blocked (thus still
//               running on the CPU).
// Returns:      void
// Parameter:    void
//******************************************************************************
void
ThreadTakeBlockLock(
    void
    );

//******************************************************************************
// Function:     ThreadExecuteForEachThreadEntry
// Description:  Iterates over the all threads list and invokes Function on each
//               entry passing an additional optional Context parameter.
// Returns:      STATUS
// Parameter:    IN PFUNC_ListFunction Function
// Parameter:    IN_OPT PVOID Context
//******************************************************************************
STATUS
ThreadExecuteForEachThreadEntry(
    IN      PFUNC_ListFunction  Function,
    IN_OPT  PVOID               Context
    );


//******************************************************************************O
// Function:     GetCurrentThread
// Description:  Returns the running thread.
// Returns:      void
//******************************************************************************
#define GetCurrentThread()      ((THREAD*)__HALreadfsqword(FIELD_OFFSET(THREAD, Self)))

//******************************************************************************
// Function:     SetCurrentThread
// Description:  Sets the current running thread.
// Returns:      void
// Parameter:    IN PTHREAD Thread
//******************************************************************************
void
SetCurrentThread(
    IN      PTHREAD     Thread
    );

//******************************************************************************
// Function:     ThreadSetPriority
// Description:  Sets the thread's priority to new priority. If the
//               current thread no longer has the highest priority, yields.
// Returns:      void
// Parameter:    IN THREAD_PRIORITY NewPriority
//******************************************************************************
void
ThreadSetPriority(
    IN      THREAD_PRIORITY     NewPriority
    );
void
ThreadRecomputePriority();
void 
ThreadDonatePriority();
