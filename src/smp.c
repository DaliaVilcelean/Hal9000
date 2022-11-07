#include "HAL9000.h"
#include "smp.h"
#include "acpi_interface.h"
#include "cpumu.h"
#include "lapic_system.h"
#include "synch.h"
#include "ap_tramp.h"
#include "pit.h"
#include "io.h"
#include "ex_event.h"
#include "hw_fpu.h"
#include "thread.h"
#include <thread_internal.h>

extern void ApAsmStub();

#define SIPI_VECTOR_SHIFT                       12

#define INIT_SLEEP                              (10*MS_IN_US)   // 10 ms = 10000us sleep
#define SIPI_SLEEP                              (200)           // 200 us sleep

STATIC_ASSERT(SmpIpiSendToCpu == ApicDestinationShorthandNone);
STATIC_ASSERT(SmpIpiSendToSelf == ApicDestinationShorthandSelf);
STATIC_ASSERT(SmpIpiSendToAllIncludingSelf == ApicDestinationShorthandAll);
STATIC_ASSERT(SmpIpiSendToAllExcludingSelf == ApicDestinationShorthandAllExcludingSelf);

typedef struct _SMP_DATA
{
    RW_SPINLOCK             CpuLock;

    _Guarded_by_(CpuLock)
    LIST_ENTRY              CpuList;

    DWORD                   NoOfCpus;
    volatile DWORD          NoOfActiveCpus;

    BYTE                    SipiVector;
    EX_EVENT                ApStartupEvent;

    BYTE                    ApicTimerVector;
    BYTE                    IpcIpiVector;
    BYTE                    AssertIpiVector;
} SMP_DATA, *PSMP_DATA;

static SMP_DATA m_smpData;

__forceinline
STATUS
_SmpInstallInterruptRoutine(
    IN      PFUNC_InterruptFunction     Function,
    IN _Strict_type_match_
            IRQL                        Irql,
    OUT     PBYTE                       Vector
    )
{
    IO_INTERRUPT ioInterrupt;

    memzero(&ioInterrupt, sizeof(IO_INTERRUPT));

    ioInterrupt.Type = IoInterruptTypeLapic;
    ioInterrupt.Exclusive = TRUE;
    ioInterrupt.ServiceRoutine = Function;
    ioInterrupt.Irql = Irql;

    return IoRegisterInterruptEx(&ioInterrupt, NULL, Vector);
}

__forceinline
void
static
_SmpSendIpcIpi(
    IN _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    IN      SMP_DESTINATION         Destination
    )
{
    BYTE vector = m_smpData.IpcIpiVector;
    APIC_DESTINATION_SHORTHAND apicShorthand;
    APIC_DESTINATION_MODE apicDestinationMode;
    APIC_ID apicId;

    apicShorthand = SendMode <= SmpIpiSendToAllExcludingSelf ? SendMode : ApicDestinationShorthandNone;
    apicDestinationMode = SendMode != SmpIpiSendToGroup ? ApicDestinationModePhysical : ApicDestinationModeLogical;

    apicId = 0;
    if ( SmpIpiSendToCpu == SendMode )
    {
        apicId = Destination.Cpu.ApicId;
    }
    else if (SmpIpiSendToGroup == SendMode)
    {
        apicId = Destination.Group.Affinity;
    }

    LapicSystemSendIpi(apicId, ApicDeliveryModeFixed, apicShorthand, apicDestinationMode, &vector);
}

__forceinline
static
BOOLEAN
_SmpDoesCpuMatchDestination(
    IN _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    IN      SMP_DESTINATION         Destination,
    IN      PPCPU                   Cpu
    )
{
    switch (SendMode)
    {
    case SmpIpiSendToAllExcludingSelf:
        return Cpu->ApicId != CpuGetApicId();
    case SmpIpiSendToAllIncludingSelf:
        return TRUE;
    case SmpIpiSendToSelf:
        return Cpu->ApicId == CpuGetApicId();
    case SmpIpiSendToCpu:
        return Cpu->ApicId == Destination.Cpu.ApicId;
    case SmpIpiSendToGroup:
        return IsBooleanFlagOn( Destination.Group.Affinity, Cpu->LogicalApicId );
    }

    NOT_REACHED;

    return FALSE;
}

static
void
_SmpSignalAllAPs(
    IN      BYTE            SipiVector
    );

static
STATUS
_SmpInstallInterruptRoutines(
    void
    );

static
STATUS
_SmpRetrieveMatchingCpus(
    IN      SMP_DESTINATION         Destination,
    IN _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    IN      DWORD                   MaximumCpuMatches,
    _Out_writes_to_opt_(MaximumCpuMatches,*NumberOfCpusMatching)
            PPCPU*                  CpuList,
    OUT     DWORD*                  NumberOfCpusMatching
    );

static
void
_SmpSetupInitialApStack(
    IN      PVOID                   InitialStack,
    IN      PCPU*                   CorrespondingCpu,
    OUT_PTR PVOID*                  ResultingStack
    );

static FUNC_InterruptFunction       _SmpApicTimerIsr;
static FUNC_InterruptFunction       _SmpAssertIpiIsr;
static FUNC_InterruptFunction       _SmpIpcIpiIsr;

_No_competing_thread_
void
SmpPreinit(
    void
    )
{
    memzero(&m_smpData, sizeof(SMP_DATA));

    InitializeListHead(&m_smpData.CpuList);
    RwSpinlockInit(&m_smpData.CpuLock);
}

STATUS
_No_competing_thread_
SmpInit(
    void
    )
{
    STATUS status;
    DWORD noOfCpus;
    BOOLEAN bRestartSearch;
    ACPI_MADT_LOCAL_APIC* pDummy;

    status = STATUS_SUCCESS;
    noOfCpus = 0;
    bRestartSearch = TRUE;

    status = ExEventInit(&m_smpData.ApStartupEvent, ExEventTypeSynchronization, FALSE);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("EvtInitialize", status);
        return status;
    }

    // install ISRs
    status = _SmpInstallInterruptRoutines();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpInstallInterruptRoutines", status);
        return status;
    }

    LOGL("SMP interrupt routines successfully registered!\n");

#pragma warning(suppress:4127)
    while (TRUE)
    {
        status = AcpiRetrieveNextCpu(bRestartSearch, &pDummy);
        if (STATUS_NO_MORE_OBJECTS == status)
        {
            LOGL("Reached end of CPU list\n");
            status = STATUS_SUCCESS;
            break;
        }

        if (!SUCCEEDED(status))
        {
            // we failed, why?
            LOG_FUNC_ERROR("AcpiRetrieveNextCpu", status);
            break;
        }

        ASSERT(NULL != pDummy);

        noOfCpus = noOfCpus + 1;
        bRestartSearch = FALSE;
    }

    m_smpData.NoOfCpus = noOfCpus;

    LOGL("The system has %u CPUs\n", m_smpData.NoOfCpus );

    return status;
}

STATUS
_No_competing_thread_
SmpSetupLowerMemory(
    IN          BYTE        NumberOfTssStacks
    )
{
    STATUS status;
    PPCPU pCpu;
    BOOLEAN bRestartSearch;
    ACPI_MADT_LOCAL_APIC* pEntry;
    DWORD noOfCpus;
    DWORD apTrampCodeAddress;
    PEVENT pWakeUpEvent;

    status = STATUS_SUCCESS;
    pCpu = NULL;
    bRestartSearch = TRUE;
    pEntry = NULL;
    noOfCpus = 0;
    apTrampCodeAddress = 0;
    pWakeUpEvent = NULL;

#pragma warning(suppress:4127)
    while (TRUE)
    {
        ASSERT(NULL == pCpu);

        status = AcpiRetrieveNextCpu(bRestartSearch, &pEntry);
        if (STATUS_NO_MORE_OBJECTS == status)
        {
            LOGL("Reached end of CPU list\n");
            status = STATUS_SUCCESS;
            break;
        }

        if (!SUCCEEDED(status))
        {
            // we failed, why?
            LOG_FUNC_ERROR("AcpiRetrieveNextCpu", status);
            break;
        }

        ASSERT(NULL != pEntry);

        if (CpuGetApicId() == pEntry->Id)
        {
            // this is the BSP
            pCpu = GetCurrentPcpu();
            ASSERT(NULL != pCpu);

            pCpu->BspProcessor = TRUE;
        }
        else
        {
            // this is an AP
            status = CpuMuAllocCpu(&pCpu,
            // C28039: The type of actual parameter 'pEntry->Id' should exactly match the type 'APIC_ID'
#pragma warning(suppress: 28039)
                                   (APIC_ID) pEntry->Id,
                                   STACK_DEFAULT_SIZE,
                                   NumberOfTssStacks
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("CpuMuAllocAndInitCpu", status);
                return status;
            }

            _SmpSetupInitialApStack(pCpu->StackTop, pCpu, &pCpu->StackTop);
        }


        InsertTailList(&m_smpData.CpuList, &pCpu->ListEntry);
        noOfCpus = noOfCpus + 1;

        pCpu = NULL;
        bRestartSearch = FALSE;
    }

    status = ApTrampSetupLowerMemory(&m_smpData.CpuList, &apTrampCodeAddress);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpSetupLowerMemory", status);
        return status;
    }

    ASSERT(IsAddressAligned(apTrampCodeAddress, PAGE_SIZE));
    ASSERT(apTrampCodeAddress < MB_SIZE);

    m_smpData.SipiVector = (BYTE)(apTrampCodeAddress >> SIPI_VECTOR_SHIFT);

    ASSERT(m_smpData.NoOfCpus == noOfCpus);

    return status;
}

void
_No_competing_thread_
SmpWakeupAps(
    void
    )
{
    if (m_smpData.NoOfCpus > 1)
    {
        LOGL("Will signal APs\n");
        _SmpSignalAllAPs(m_smpData.SipiVector);
        LOGL("APs were signaled\n");

        ExEventWaitForSignal(&m_smpData.ApStartupEvent);

        LOGL("Aps have waken UP\n");
    }
    else
    {
        LOGL("This is not a SMP system! :(\n");
    }
}

void
_No_competing_thread_
SmpCleanupLowerMemory(
    void
    )
{
    ApTrampCleanupLowerMemory(&m_smpData.CpuList);
}

void
SmpSendPanic(
    void
    )
{
    BYTE vector = m_smpData.AssertIpiVector;

    LapicSystemSendIpi(0, ApicDeliveryModeFixed, ApicDestinationShorthandAllExcludingSelf, ApicDestinationModePhysical, &vector);
}



STATUS
SmpSendGenericIpi(

    )
{
    SMP_DESTINATION dest = { 0 };
   

    return SmpSendGenericIpiEx(ThreadYieldForIpi,
                                NULL,
                               NULL,
                               NULL,
                               FALSE,
                               SmpIpiSendToAllExcludingSelf,
                               dest
                               );
}

STATUS
SmpSendGenericIpiEx(
    IN      PFUNC_IpcProcessEvent   BroadcastFunction,
    IN_OPT  PVOID                   Context,
    IN_OPT  PFUNC_FreeFunction      FreeFunction,
    IN_OPT  PVOID                   FreeContext,
    IN      BOOLEAN                 WaitForHandling,
    IN _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    _When_(SendMode == SmpIpiSendToCpu || SendMode == SmpIpiSendToGroup, IN)
            SMP_DESTINATION         Destination
    )
{
    PIPC_EVENT_CPU pCpuEvents;
    STATUS status;
    DWORD noOfMatchingCpus;
    DWORD i;
    PPCPU* pMatchingCpus;
    DWORD tempResult;
    BOOLEAN bSelfInDestination;
    APIC_ID apicId;

    LOG_FUNC_START;

    if (NULL == BroadcastFunction)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (SendMode > SmpIpiSendMax)
    {
        return STATUS_INVALID_PARAMETER6;
    }

    status = STATUS_SUCCESS;
    pCpuEvents = NULL;
    pMatchingCpus = NULL;
    bSelfInDestination = FALSE;

    LOG_TRACE_CPU("Send mode 0x%x to destination 0x%02x [0x%02]\n",
                  SendMode, Destination.Cpu.ApicId, Destination.Group.Affinity );

    status = _SmpRetrieveMatchingCpus(Destination,
                                      SendMode,
                                      0,
                                      NULL,
                                      &noOfMatchingCpus
                                      );
    if (STATUS_BUFFER_TOO_SMALL == status)
    {
        status = STATUS_SUCCESS;
    }
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpRetrieveMatchingCpus", status );
        return status;
    }

    if (0 == noOfMatchingCpus)
    {
        LOG_WARNING("There are no CPUs which match IPI destination! :(\n");
        return STATUS_CPU_NO_MATCHES;
    }

    apicId = CpuGetApicId();

    __try
    {
        // in the future we may have a preallocated array of pointers allocated in each
        // PCPU structure. This would remove the need of memory allocation when sending
        // IPIs and because the number of CPUs is limited not much memory is wasted
        // E.g: even if there are 256 CPUs * sizeof(PVOID) => only 2KB of memory is used
        // per CPU => 262KB extra
        pMatchingCpus = ExAllocatePoolWithTag(0,
                                              sizeof(PCPU*) * noOfMatchingCpus,
                                              HEAP_TEMP_TAG,
                                              0
        );
        ASSERT(NULL != pMatchingCpus);

        status = _SmpRetrieveMatchingCpus(Destination,
                                          SendMode,
                                          noOfMatchingCpus,
                                          pMatchingCpus,
                                          &tempResult
        );
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_SmpRetrieveMatchingCpus", status);
            __leave;
        }
        ASSERT(tempResult == noOfMatchingCpus);

        pCpuEvents = IpcGenerateEvent(BroadcastFunction,
                                      Context,
                                      FreeFunction,
                                      FreeContext,
                                      WaitForHandling,
                                      noOfMatchingCpus
        );
        if (NULL == pCpuEvents)
        {
            status = STATUS_UNSUCCESSFUL;
            LOG_FUNC_ERROR("IpcGenerateEvent", status);
            __leave;
        }
        LOG_TRACE_CPU("Successfully generated event at 0x%X\n", pCpuEvents);

        for (i = 0; i < noOfMatchingCpus; ++i)
        {
            INTR_STATE intrState;
            PCPU* pCpu = pMatchingCpus[i];

            LOG_TRACE_CPU("Will process CPU 0x%02x [0x%02x]\n", pCpu->ApicId, pCpu->LogicalApicId);

            if (apicId == pCpu->ApicId)
            {
                bSelfInDestination = TRUE;
            }

            LockAcquire(&pCpu->EventListLock, &intrState);
            InsertTailList(&pCpu->EventList, &(pCpuEvents[i].ListEntry));
            pCpu->NoOfEventsInList++;
            LockRelease(&pCpu->EventListLock, intrState);
        }

        // notify processors
        _SmpSendIpcIpi(SendMode, Destination);

        if (WaitForHandling)
        {
            /// if we sent the event to ourselves as well and we want to wait for handling completion
            /// it is MANDATORY for INTERRUPTS to be turned ON and for our processor priority
            /// to be strictly below IrqlIpiLevel
            if (bSelfInDestination)
            {
                ASSERT(INTR_ON == CpuIntrGetState());
                ASSERT(LapicSystemGetPpr() < IrqlIpiLevel);
            }

            // if WaitForHandling is FALSE it is possible for the event to already be
            // de-allocated => not safe to call function
            IpcWaitForEventHandling(pCpuEvents);
        }
        pCpuEvents = NULL;
    }
    __finally
    {
        if( NULL != pMatchingCpus )
        {
            ExFreePoolWithTag(pMatchingCpus, HEAP_TEMP_TAG);
            pMatchingCpus = NULL;
        }

        ASSERT( NULL == pCpuEvents );

        LOG_FUNC_END;
    }

    return status;
}

STATUS
SmpCpuInit(
    void
    )
{
    STATUS status;

    status = STATUS_SUCCESS;

    // initialize APIC
    status = LapicSystemInitializeCpu(m_smpData.ApicTimerVector);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("ApicMapRegister", status);
        return status;
    }

    // make sure TPR is 0
    __writecr8(0);

    return STATUS_SUCCESS;
}

void
_No_competing_thread_
SmpGetCpuList(
    OUT_PTR      PLIST_ENTRY*     CpuList
    )
{
    *CpuList = &m_smpData.CpuList;
}

DWORD
SmpGetNumberOfActiveCpus(
    void
    )
{
    return m_smpData.NoOfActiveCpus;
}

void
SmpNotifyCpuWakeup(
    void
    )
{
    DWORD noOfActiveCpus;

    LOG_FUNC_START_CPU;

    noOfActiveCpus = _InterlockedIncrement(&m_smpData.NoOfActiveCpus);

    LOGPL("Number of active CPUs: %u/%u\n", noOfActiveCpus, m_smpData.NoOfCpus );

    if( noOfActiveCpus == m_smpData.NoOfCpus )
    {
        LOGPL("All CPUs woke up, will signal BSP\n");
        ExEventSignal(&m_smpData.ApStartupEvent);
    }

    LOG_FUNC_END_CPU;
}

static
void
_SmpSignalAllAPs(
    IN      BYTE            SipiVector
    )
{
    LapicSystemSendIpi(0, ApicDeliveryModeINIT, ApicDestinationShorthandAllExcludingSelf, ApicDestinationModePhysical, NULL);
    PitSleep(INIT_SLEEP);

    LapicSystemSendIpi(0, ApicDeliveryModeSIPI, ApicDestinationShorthandAllExcludingSelf, ApicDestinationModePhysical, &SipiVector);
    PitSleep(SIPI_SLEEP);

    LapicSystemSendIpi(0, ApicDeliveryModeSIPI, ApicDestinationShorthandAllExcludingSelf, ApicDestinationModePhysical, &SipiVector);
    PitSleep(SIPI_SLEEP);
}

static
STATUS
_SmpInstallInterruptRoutines(
    void
    )
{
    STATUS status;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;

    // install APIC timer ISR
    status = _SmpInstallInterruptRoutine( _SmpApicTimerIsr, IrqlClockLevel, &m_smpData.ApicTimerVector );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpInstallInterruptRoutine", status);
        return status;
    }

    status = _SmpInstallInterruptRoutine(_SmpAssertIpiIsr, IrqlAssertLevel, &m_smpData.AssertIpiVector );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpInstallInterruptRoutine", status);
        return status;
    }

    status = _SmpInstallInterruptRoutine(_SmpIpcIpiIsr, IrqlIpiLevel, &m_smpData.IpcIpiVector );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_SmpInstallInterruptRoutine", status);
        return status;
    }

    LOG_FUNC_END;

    return status;
}

static
STATUS
_SmpRetrieveMatchingCpus(
    IN      SMP_DESTINATION         Destination,
    IN  _Strict_type_match_
            SMP_IPI_SEND_MODE       SendMode,
    IN      DWORD                   MaximumCpuMatches,
    _Out_writes_to_opt_(MaximumCpuMatches,*NumberOfCpusMatching)
            PPCPU*                  CpuList,
    OUT     DWORD*                  NumberOfCpusMatching
    )
{
    PLIST_ENTRY pEntry;
    INTR_STATE oldState;
    DWORD noOfMatches;

    if (NULL == NumberOfCpusMatching)
    {
        return STATUS_INVALID_PARAMETER5;
    }

    noOfMatches = 0;

    RwSpinlockAcquireShared(&m_smpData.CpuLock, &oldState);
    for(pEntry = m_smpData.CpuList.Flink;
        pEntry != &m_smpData.CpuList;
        pEntry = pEntry->Flink)
    {
        PCPU* pCpu = CONTAINING_RECORD(pEntry, PCPU, ListEntry);

        if(_SmpDoesCpuMatchDestination(SendMode,
                                       Destination,
                                       pCpu))
        {
            noOfMatches++;

            if (noOfMatches <= MaximumCpuMatches)
            {
                if (NULL != CpuList)
                {
                    LOG_TRACE_CPU("Will insert CPU 0x%02x [0x%02x] into list\n", pCpu->ApicId, pCpu->LogicalApicId );
                    CpuList[noOfMatches - 1] = pCpu;
                }
            }
        }
    }
    RwSpinlockReleaseShared(&m_smpData.CpuLock, oldState);

    *NumberOfCpusMatching = noOfMatches;

    return noOfMatches > MaximumCpuMatches ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

//  STACK TOP
//  -----------------------------------------------------------------
//  |                                                               |
//  |       Shadow Space                                            |
//  |                                                               |
//  |     1st Param = (PCPU) Cpu                                    |
//  -----------------------------------------------------------------
//  |     Dummy Function RA = 0xDEADC0DE'DEADC0DE                   |
//  -----------------------------------------------------------------
//  |     ApInitCpu                                                 |
//  -----------------------------------------------------------------
//  |                                                               |
//  |       Shadow Space                                            |
//  |                                                               |
//  |                                                               |
//  -----------------------------------------------------------------
//  |     Function RA = ApAsmStub                                   |
//  -----------------------------------------------------------------
//  |     HalActivateFpu                                            |
//  -----------------------------------------------------------------
static
void
_SmpSetupInitialApStack(
    IN      PVOID                   InitialStack,
    IN      PCPU*                   CorrespondingCpu,
    OUT_PTR PVOID*                  ResultingStack
    )
{
    PBYTE pStackTop;

    ASSERT(InitialStack != NULL);
    ASSERT(IsAddressAligned(InitialStack, PAGE_SIZE));
    ASSERT(CorrespondingCpu != NULL);
    ASSERT(ResultingStack != NULL);

    pStackTop = InitialStack;

    // setup 'return' stack
    // 4 * sizeof(PVOID) for arguments
    // sizeof(PVOID) for RA
    // sizeof(PVOID) for proper alignment
    pStackTop = (pStackTop - SHADOW_STACK_SIZE - sizeof(PVOID) - sizeof(PVOID));
    *((PQWORD)pStackTop) = (QWORD)ApInitCpu;
    *((PQWORD)pStackTop + 1) = 0xDEADC0DE'DEADC0DE;
    *((PQWORD)pStackTop + 2) = (QWORD)CorrespondingCpu;

    pStackTop = (pStackTop - SHADOW_STACK_SIZE - sizeof(PVOID) - sizeof(PVOID));

    *((PQWORD)pStackTop) = (QWORD)HalActivateFpu;
    *((PQWORD)pStackTop + 1) = (QWORD)ApAsmStub;

    *ResultingStack = pStackTop;
}

static
BOOLEAN
(__cdecl _SmpApicTimerIsr)(
    IN        PDEVICE_OBJECT           Device
    )
{
    ASSERT( NULL != Device );

    LOGPL("What are we doing here??\n");

    return FALSE;
}

static
BOOLEAN
(__cdecl _SmpAssertIpiIsr)(
    IN        PDEVICE_OBJECT           Device
    )
{
    ASSERT( NULL != Device );

    AssertInfo("Received ASSERT IPI\n");
    NOT_REACHED;

    return FALSE;
}

static
BOOLEAN
(__cdecl _SmpIpcIpiIsr)(
    IN        PDEVICE_OBJECT           Device
    )
{
    PCPU* pCpu;
    INTR_STATE dummy;
    PLIST_ENTRY pListEntry;
    PIPC_EVENT_CPU pCpuEvent;
    STATUS status;
    STATUS funcStatus;

    ASSERT(NULL != Device);

    LOG_FUNC_START;

    pListEntry = NULL;
    status = STATUS_SUCCESS;
    pCpuEvent = NULL;

    pCpu = GetCurrentPcpu();
    ASSERT( NULL != pCpu );

    LockAcquire(&pCpu->EventListLock, &dummy);
    if (pCpu->NoOfEventsInList > 0)
    {
        LOG_TRACE_CPU("Will remove event from list at 0x%X\n", &pCpu->EventList);
        pListEntry = RemoveHeadList(&pCpu->EventList);
        ASSERT( pListEntry != &pCpu->EventList );
        pCpu->NoOfEventsInList--;
    }
    LockRelease(&pCpu->EventListLock, INTR_OFF);

    ASSERT(NULL != pListEntry);

    pCpuEvent = CONTAINING_RECORD(pListEntry, IPC_EVENT_CPU, ListEntry);
    LOG_TRACE_CPU("Removed element at 0x%X\n", pCpuEvent);

    __try
    {
        status = IpcProcessEvent(pCpuEvent, &funcStatus);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IpcProcessEvent", status);
            __leave;
        }

        status = funcStatus;
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("Event processing failed", status);
            __leave;
        }

        pListEntry = NULL;
    }
    __finally
    {
        LOG_FUNC_END;
    }

    return SUCCEEDED(status);
}