#include "HAL9000.h"
#include "lapic_system.h"
#include "io.h"
#include "cpumu.h"

#define APIC_TIMER_DIVIDE_VALUE                 64

typedef struct _APIC_DATA
{
    PVOID                   LocalApicAddress;
    DWORD                   DividedBusFrequency;

    DWORD                   InitialTimerCount;

    BYTE                    ErrorVector;
    BYTE                    SpuriousVector;
} APIC_DATA, *PAPIC_DATA;

static APIC_DATA            m_apicData;
//******************************************************************************
// Function:    ApicMapRegister
// Description: This function MUST be called before any other APIC function.
// Returns:       STATUS
// Parameter:     void
//******************************************************************************
STATUS
static
_LapicSystemMapRegister(
    IN      PHYSICAL_ADDRESS                ApicPhysicalAddress,
    OUT_PTR PVOID*                          ApicVirtualAddress
    );

static
STATUS
_LapicInstallInterruptRoutines(
    void
    );

static FUNC_InterruptFunction               _ApicSpuriousIsr;
static FUNC_InterruptFunction               _ApicErrorIsr;

__forceinline
STATUS
_LapicInstallInterruptRoutine(
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

STATUS
LapicSystemInit(
    void
    )
{
    STATUS status;
    PHYSICAL_ADDRESS apicBaseAddress;
    DWORD cpuFrequency;

    status = _LapicInstallInterruptRoutines();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_LapicInstallInterruptRoutines", status );
        return status;
    }
    LOGL("_LapicInstallInterruptRoutines succeeeded\n");

    apicBaseAddress = LapicGetBasePhysicalAddress();

    status = _LapicSystemMapRegister(apicBaseAddress, &m_apicData.LocalApicAddress);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_LapicSystemMapRegister", status);
        return status;
    }
    LOGL("_LapicSystemMapRegister succeeded\n");

    LapicDetermineDividedBusFrequency(m_apicData.LocalApicAddress, &cpuFrequency );
    LOGL("CPU frequency: 0x%x\n", cpuFrequency );

    m_apicData.DividedBusFrequency = cpuFrequency / APIC_TIMER_DIVIDE_VALUE;
    LOGL("Divided bus frequency: 0x%x\n", m_apicData.DividedBusFrequency );

    return status;
}

STATUS
LapicSystemInitializeCpu(
    IN      BYTE                            TimerInterruptVector
    )
{
    STATUS status;
    PPCPU pCpu;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pCpu = GetCurrentPcpu();

    ASSERT(NULL != pCpu);
    ASSERT(NULL != m_apicData.LocalApicAddress);

    LapicInitialize(m_apicData.LocalApicAddress);

    // before enabling LAPIC we should specify our logical destination
    LapicSetLogicalApicId(m_apicData.LocalApicAddress,
                          pCpu->LogicalApicId,
                          APIC_DESTINATION_FORMAT_FLAT_MODEL
                          );

    // SW enable APIC
    /// This must be done before any other settings are applied
    // 10.4.7.2 Local APIC State After It Has Been Software Disabled
    // The mask bits for all the LVT entries are set. Attempts to reset these bits will be ignored.
    LapicSystemSetState(TRUE);
    LOGL("Apic is SW enabled\n");

    LapicConfigureLvtRegisters(m_apicData.LocalApicAddress, m_apicData.ErrorVector );
    LOGPL("LAPIC registers configured\n");

    LOGPL("Will configure timer using interrupt vector 0x%02x\n", TimerInterruptVector );
    LapicConfigureTimer(m_apicData.LocalApicAddress,TimerInterruptVector,ApicDivideBy64);
    LOGPL("LAPIC timer configured\n");

    pCpu->ApicInitialized = TRUE;

    LOG_FUNC_END;

    return status;
}

void
LapicSystemSetState(
    IN      BOOLEAN                         Enable
    )
{
    ASSERT( NULL != m_apicData.LocalApicAddress);

    LapicSetState(m_apicData.LocalApicAddress, m_apicData.SpuriousVector, Enable);
}

BOOLEAN
LapicSystemGetState(
    void
    )
{
    ASSERT(NULL != m_apicData.LocalApicAddress);

    return LapicGetState(m_apicData.LocalApicAddress);
}

void
LapicSystemSendEOI(
    IN      BYTE                            Vector
    )
{
    ASSERT(NULL != m_apicData.LocalApicAddress);

    LapicSendEOI(m_apicData.LocalApicAddress,
                 Vector
                 );
}

void
LapicSystemSetTimer(
    IN      DWORD                           Microseconds
    )
{
    DWORD timerCount;

    ASSERT( NULL != m_apicData.LocalApicAddress );

    timerCount = 0;

    if (Microseconds != 0)
    {
        ASSERT(Microseconds < MAX_QWORD / m_apicData.DividedBusFrequency);
        timerCount = ((QWORD)m_apicData.DividedBusFrequency * Microseconds) / SEC_IN_US;

        m_apicData.InitialTimerCount = timerCount;

        LOGL("DividedBusFrequency: 0x%x\n", m_apicData.DividedBusFrequency);
        LOGL("timerCount: 0x%x\n", timerCount);
    }

    LapicSetTimerInterval(m_apicData.LocalApicAddress, timerCount);
}

void
LapicSystemSendIpi(
    _When_(ApicDestinationShorthandNone == DeliveryMode, IN)
    _When_(ApicDestinationShorthandNone != DeliveryMode, _Reserved_)
            APIC_ID                         ApicId,
    IN      _Strict_type_match_
            APIC_DELIVERY_MODE              DeliveryMode,
    IN      _Strict_type_match_
            APIC_DESTINATION_SHORTHAND      DestinationShorthand,
    IN      _Strict_type_match_
            APIC_DESTINATION_MODE           DestinationMode,
    IN_OPT  BYTE*                           Vector
    )
{
    ASSERT(NULL != m_apicData.LocalApicAddress);

    LapicSendIpi(m_apicData.LocalApicAddress,
                 ApicId,
                 DeliveryMode,
                 DestinationShorthand,
                 DestinationMode,
                 Vector
                 );
}

BYTE
LapicSystemGetPpr(
    void
    )
{
    ASSERT( NULL != m_apicData.LocalApicAddress );

    return LapicGetPpr(m_apicData.LocalApicAddress);
}

QWORD
LapicSystemGetTimerElapsedUs(
    void
    )
{
    DWORD lapicTimerCount;
    DWORD ticksCounted;
    QWORD elapsedUs;

    lapicTimerCount = 0;
    ticksCounted = 0;
    elapsedUs = 0;

    ASSERT( NULL != m_apicData.LocalApicAddress );

    lapicTimerCount = LapicGetTimerCount(m_apicData.LocalApicAddress);

    ASSERT( m_apicData.InitialTimerCount >= lapicTimerCount );

    ticksCounted = m_apicData.InitialTimerCount - lapicTimerCount;

    elapsedUs = ( (QWORD) ticksCounted * SEC_IN_US ) /  m_apicData.DividedBusFrequency;

    return elapsedUs;
}

BOOLEAN
LapicSystemIsInterruptServiced(
    IN      BYTE                            Vector
    )
{
    return LapicIsInterruptServiced(m_apicData.LocalApicAddress, Vector);
}

STATUS
static
_LapicSystemMapRegister(
    IN      PHYSICAL_ADDRESS                ApicPhysicalAddress,
    OUT_PTR PVOID*                          ApicVirtualAddress
    )
{
    PVOID apicVirtualAddress;

    ASSERT( NULL != ApicPhysicalAddress );
    ASSERT( NULL != ApicVirtualAddress );

    apicVirtualAddress = IoMapMemory(ApicPhysicalAddress,
                                     PAGE_SIZE,
                                     PAGE_RIGHTS_READWRITE);
    if (NULL == apicVirtualAddress)
    {
        return STATUS_MEMORY_CANNOT_BE_MAPPED;
    }

    // we return the apic base address
    *ApicVirtualAddress = apicVirtualAddress;

    return STATUS_SUCCESS;
}

static
STATUS
_LapicInstallInterruptRoutines(
    void
)
{
    STATUS status;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;

    // install APIC timer ISR
    status = _LapicInstallInterruptRoutine(_ApicSpuriousIsr, IrqlErrorLevel, &m_apicData.SpuriousVector);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_LapicInstallInterruptRoutine", status);
        return status;
    }

    status = _LapicInstallInterruptRoutine(_ApicErrorIsr, IrqlErrorLevel, &m_apicData.ErrorVector);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_LapicInstallInterruptRoutine", status);
        return status;
    }

    LOG_FUNC_END;

    return status;
}

static
BOOLEAN
(__cdecl _ApicSpuriousIsr)(
    IN      PDEVICE_OBJECT           Device
    )
{
    ASSERT( NULL != Device );

    LOG_FUNC_START;

    LOG_FUNC_END;

    return FALSE;
}

static
BOOLEAN
(__cdecl _ApicErrorIsr)(
    IN      PDEVICE_OBJECT           Device
    )
{
    DWORD error;

    ASSERT( NULL != Device );

    LOG_FUNC_START;

    error = LapicGetErrorRegister(m_apicData.LocalApicAddress);
    LOGL("Error: 0x%x\n", error );

    LOG_FUNC_END;

    return FALSE;
}