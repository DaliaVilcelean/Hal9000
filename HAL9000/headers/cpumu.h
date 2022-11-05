#pragma once

#include "register.h"
#include "tss.h"
#include "cpu.h"
#include "list.h"
#include "synch.h"
#include "cpu_structures.h"

#define STACK_DEFAULT_SIZE          (8*PAGE_SIZE)
#define STACK_GUARD_SIZE            (2*PAGE_SIZE)

typedef struct _THREADING_DATA
{
    DWORD               RunningThreadTicks;
    struct _THREAD*     IdleThread;
    struct _THREAD*     CurrentThread;
    struct _THREAD*     PreviousThread;

    BOOLEAN             YieldOnInterruptReturn;

    QWORD               IdleTicks;
    QWORD               KernelTicks;
} THREADING_DATA, *PTHREADING_DATA;

typedef struct _PCPU
{
    struct _PCPU                *Self;

    PVOID                       StackTop;
    DWORD                       StackSize;

    APIC_ID                     ApicId;
    APIC_ID                     LogicalApicId;
    BOOLEAN                     BspProcessor;

    // TSS base address
    TSS                         Tss;
    PVOID                       TssStacks[NO_OF_IST];
    BYTE                        NumberOfTssStacks;
    WORD                        TrSelector;

    LIST_ENTRY                  ListEntry;

    BOOLEAN                     ApicInitialized;

    THREADING_DATA              ThreadData;

    // IPC data
    LIST_ENTRY                  EventList;
    LOCK                        EventListLock;
    DWORD                       NoOfEventsInList;

    // Used to mark the fact that the VMM specialized functions for
    // allocating or freeing a VA reservation are working with the VA reservation
    // space metadata (if #PFs occur on these pages a mapping must be created on
    // the spot and they must be resolved)
    BOOLEAN                     VmmMemoryAccess;
    QWORD                       PageFaults;

    QWORD                       InterruptsTriggered[NO_OF_TOTAL_INTERRUPTS];
} PCPU, *PPCPU;
STATIC_ASSERT_INFO(FIELD_OFFSET(PCPU,StackTop) == 0x8, "Used by _syscall.yasm:30 on syscalls to determine the user thread's kernel stack!");

// This function should only be called when interrupts are disabled, else the CPU on which
// the thread is running may change between the moment GetCurrentPcpu() was called and the moment
// in which the pointer returned is actually used. This is an instance of a time of check to
// time of use race condition.
#define GetCurrentPcpu()    ((PCPU*)__readgsqword(FIELD_OFFSET(PCPU,Self)))
#define SetCurrentPcpu(pc)  (__writemsr(IA32_GS_BASE_MSR,(pc)))

void
CpuMuPreinit(
    void
    );

void
CpuMuValidateConfiguration(
    void
    );

STATUS
CpuMuSetMonitorFilterSize(
    IN          WORD        FilterSize
    );

STATUS
CpuMuAllocAndInitCpu(
    OUT_PTR     PPCPU*      PhysicalCpu,
    IN _Strict_type_match_
                APIC_ID     ApicId,
    IN          DWORD       StackSize,
    IN          BYTE        NoOfTssStacks
    );

STATUS
CpuMuAllocCpu(
    OUT_PTR     PPCPU*      PhysicalCpu,
    IN _Strict_type_match_
                APIC_ID     ApicId,
    IN          DWORD       StackSize,
    IN          BYTE        NumberOfTssStacks
    );

STATUS
CpuMuInitCpu(
    IN          PPCPU       PhysicalCpu,
    IN          BOOLEAN     ChangeStack
    );

void
CpuMuChangeStack(
    IN          PVOID       NewStack
    );

BOOLEAN
CpuMuIsPcidFeaturePresent(
    void
    );

STATUS
CpuMuActivateFpuFeatures(
    void
    );

__forceinline
IRQL
CpuMuRaiseIrql(
    IN IRQL Irql
    )
{
    IRQL prevIrql = __readcr8();

    ASSERT_INFO(prevIrql <= Irql, "Previous IRQL: 0x%x\nCurrent IRQL: 0x%x\n",
                prevIrql, Irql );

    __writecr8(Irql);

    return prevIrql;
}

__forceinline
IRQL
CpuMuLowerIrql(
    IN IRQL Irql
    )
{
    IRQL prevIrql = __readcr8();

    ASSERT_INFO(prevIrql >= Irql, "Previous IRQL: 0x%x\nCurrent IRQL: 0x%x\n",
                prevIrql, Irql );

    __writecr8(Irql);

    return prevIrql;
}

#define CURRENT_CPU_MASK        0x8000'0000'0000'0000ULL

__forceinline
extern
PVOID
CpuGetCurrent(void)
{
    // warning C4306: 'type cast': conversion from 'BYTE' to 'PVOID' of greater size
#pragma warning(suppress:4306)
    return (PVOID) ( CURRENT_CPU_MASK | CpuGetApicId());
}