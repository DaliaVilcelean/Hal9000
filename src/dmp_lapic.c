#include "HAL9000.h"
#include "dmp_common.h"
#include "cpu.h"
#include "../../HAL/headers/lapic_registers.h"

static
void
_DumpLapicLogicalDestination(
    IN      PLAPIC          Apic
    );

static
void
_DumpLapicLvt(
    IN      LVT_REGISTER    LvtEntry,
    IN      BOOLEAN         ErrorRegister
    );

static
void
_DumpLapicLint(
    IN      LVT_REGISTER    Lint
    );

static
void
_DumpLapicTimer(
    IN      PLAPIC          Apic
    );

void
DumpLapic(
    IN      PVOID           LapicBaseAddress
    )
{
    PLAPIC pLapic;
    DWORD i;
    LAPIC_VERSION_REGISTER lapicVersion;
    LVT_REGISTER lvtReg;
    SVR_REGISTER svrReg;
    INTR_STATE oldState;

    ASSERT(NULL != LapicBaseAddress);

    pLapic = (PLAPIC) LapicBaseAddress;
    
    lapicVersion.Raw = pLapic->ApicVersion.Value;
    svrReg.Raw = pLapic->SpuriousInterruptVector.Value;

    oldState = DumpTakeLock();

    LOG("\nLAPIC on CPU %02x\n", CpuGetApicId() );
    _DumpLapicLogicalDestination(pLapic);
    LOG("Apic is SW %s\n", svrReg.ApicEnable ? "ENABLED" : "DISABLED");
    LOG("Spurious vector: 0x%x\n", svrReg.Vector );
    LOG("EOI broadcast suppression: 0x%x\n", svrReg.EOIBroadcastSuppresion );
    LOG("Focus processor checking: 0x%x\n", svrReg.FocusProcessorChecking );
    LOG("Lapic version: 0x%x\n", lapicVersion.Version );
    LOG("Max LVT entry: 0x%x\n", lapicVersion.MaxLvtEntry );
    LOG("Error status: 0x%x\n", pLapic->ErrorStatus.Value );
    LOG("Task priority: 0x%x\n", pLapic->TPR.Value );
    LOG("Processor priority: 0x%x\n", pLapic->PPR.Value );
    LOG("Arbitration priority: 0x%x\n", pLapic->APR.Value );

    for (i = 0; i < 8; ++i)
    {
        DWORD startIndex = i * BITS_FOR_STRUCTURE(DWORD);
        DWORD endIndex = ( i + 1 ) * BITS_FOR_STRUCTURE(DWORD) - 1;

        LOG("IRR [0x%02x - 0x%02x]: 0b%032b\n", 
            endIndex, startIndex, pLapic->IRR[i].Value);
        LOG("ISR [0x%02x - 0x%02x]: 0b%032b\n", 
            endIndex, startIndex, pLapic->ISR[i].Value);
        LOG("TMR [0x%02x - 0x%02x]: 0b%032b\n", 
            endIndex, startIndex, pLapic->TMR[i].Value);
    }

    LOG("LINT0:\n");
    lvtReg.Raw = pLapic->LvtLINT0.Value;
    _DumpLapicLint(lvtReg);

    LOG("LINT1:\n");
    lvtReg.Raw = pLapic->LvtLINT1.Value;
    _DumpLapicLint(lvtReg);

    LOG("CMCI:\n");
    lvtReg.Raw = pLapic->LvtCMCI.Value;
    _DumpLapicLvt(lvtReg, FALSE);

    LOG("Error:\n");
    lvtReg.Raw = pLapic->LvtError.Value;
    _DumpLapicLvt(lvtReg, TRUE);

    LOG("Performance counter:\n");
    lvtReg.Raw = pLapic->LvtPerformanceMonitoringCounters.Value;
    _DumpLapicLvt(lvtReg, FALSE);

    LOG("Thermal sensor:\n");
    lvtReg.Raw = pLapic->LvtThermalSensor.Value;
    _DumpLapicLvt(lvtReg, FALSE);

    _DumpLapicTimer(pLapic);

    DumpReleaseLock(oldState);
}

static
void
_DumpLapicLogicalDestination(
    IN      PLAPIC          Apic
    )
{
    LDR_REGISTER ldrRegister;

    ldrRegister.Raw = Apic->LogicalDestination.Value;

    LOG("Logical destination: 0x%02x\n", ldrRegister.LogicalApicId );
    LOG("Destination format: 0x%08x\n", Apic->DestinationFormat.Value );
}

static
void
_DumpLapicLvt(
    IN      LVT_REGISTER    LvtEntry,
    IN      BOOLEAN         ErrorOrTimerRegister
    )
{
    LOG("Vector: 0x%x\n", LvtEntry.Vector );
    if (!ErrorOrTimerRegister)
    {
        LOG("Delivery mode: 0b%b\n", LvtEntry.DeliveryMode );
    }
    LOG("Delivery status: [%s]\n", ( 0 == LvtEntry.DeliveryStatus ) ? "Idle" : "Send Pending" );
    LOG("Masked: [%s]\n", ( 0 == LvtEntry.Masked ) ? "FALSE" : "TRUE" );
}

static
void
_DumpLapicLint(
    IN      LVT_REGISTER    Lint
    )
{
    _DumpLapicLvt(Lint, FALSE);
    LOG("Pin polarity: 0x%x\n", Lint.PinPolarity );
    LOG("Remote IRR: 0x%x\n", Lint.RemoteIRR);
    LOG("Trigger mode: [%s]\n", ( 0 == Lint.TriggerMode ) ? "Edge" : "Level" );
}

static
void
_DumpLapicTimer(
    IN      PLAPIC          Apic
    )
{
    LVT_REGISTER lvtReg;

    ASSERT( NULL != Apic );

    LOG("Timer:\n");

    lvtReg.Raw = Apic->LvtTimer.Value;
    LOG("Timer mode: 0x%x\n", lvtReg.TimerMode);
    _DumpLapicLvt(lvtReg, TRUE);

    LOG("Divide configuration: 0x%x\n", Apic->TimerDivideConfiguration.Value );
    LOG("Initial count: 0x%x\n", Apic->TimerInitialCount.Value );
    LOG("Current count: 0x%x\n", Apic->TimerCurrentCount.Value );
}