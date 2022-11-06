#include "HAL9000.h"
#include "dmp_cpu.h"
#include "dmp_common.h"

#define MAX_REGISTER_NAME_LENGTH        3
#define MAX_LEAF_NAME_LENGTH            50

// +1 because of NULL terminator
static const char REGISTER_NAMES[RegisterR15 + 1][MAX_REGISTER_NAME_LENGTH + 1] = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
                                                                                    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
                                                                                    };

static const char CPUID_LEAF_NAMES[][MAX_LEAF_NAME_LENGTH] = { "Basic Information", "Feature Information", "Cache Information", "Reserved", "Deterministic Cache",
                                                               "Monitor Leaf", "Power Management Leaf", "Extended Feature Flags Leaf", "Reserved", "Direct Cache Access Leaf",
                                                                "Architectural Performance Monitoring Leaf",  "Extended Topology Enumeration Leaf", "Reserved", "Processor Extended State Leaf",
                                                                };

const
char*
RetrieveCpuIdLeafName(
    IN      DWORD                             Index
    )
{
    if (Index >= ARRAYSIZE(CPUID_LEAF_NAMES))
    {
        return "Unknown";
    }

    return CPUID_LEAF_NAMES[Index];
}

const
char*
RetrieveRegisterName(
    IN      GeneralPurposeRegisterIndexes     Index
    )
{
    if (Index > RegisterR15)
    {
        return NULL;
    }

    return REGISTER_NAMES[Index];
}

void
DumpRegisterArea(
    IN  REGISTER_AREA*                  RegisterArea
    )
{
    ASSERT(RegisterArea != NULL);

    for (DWORD i = 0; i <= RegisterR15; ++i)
    {
        QWORD regValue = RegisterArea->RegisterValues[i];

        LOG("%s: 0x%X\n", RetrieveRegisterName(i), regValue);
    }

    LOG("RIP: 0x%X\n", RegisterArea->Rip);
    LOG("Rflags: 0x%X\n", RegisterArea->Rflags);
}

void
DumpProcessorState(
    IN  COMPLETE_PROCESSOR_STATE*    ProcessorState
    )
{
    INTR_STATE intrState;

    ASSERT( NULL != ProcessorState);

    intrState = DumpTakeLock();
    LOG("\nProcessor State:\n");

    DumpRegisterArea(&ProcessorState->RegisterArea);

    DumpReleaseLock(intrState);
}

void
DumpControlRegisters(
    void
    )
{
    INTR_STATE intrState;

    intrState = DumpTakeLock();
    LOG("\nControl registers:\n");
    LOG("CR0: 0x%X\n", __readcr0());
    LOG("CR2: 0x%X\n", __readcr2());
    LOG("CR3: 0x%X\n", __readcr3());
    LOG("CR4: 0x%X\n", __readcr4());
    LOG("CR8: 0x%X\n", __readcr8());
    DumpReleaseLock(intrState);
}

void
DumpInterruptStack(
    IN  PINTERRUPT_STACK_COMPLETE       InterruptStack,
    IN  BOOLEAN                         ErrorCodeValid
    )
{
    INTR_STATE intrState;

    intrState = DumpTakeLock();
    LOG("\nInterrupt stack:\n");
    if (ErrorCodeValid)
    {
        LOG("Error code: 0x%x\n", InterruptStack->ErrorCode);
    }

    LOG("RIP: 0x%X\n",      InterruptStack->Registers.Rip);
    LOG("CS: 0x%X\n",       InterruptStack->Registers.CS);
    LOG("RFLAGS: 0x%X\n",   InterruptStack->Registers.RFLAGS);
    LOG("RSP: 0x%X\n",      InterruptStack->Registers.Rsp);
    LOG("SS: 0x%X\n",       InterruptStack->Registers.SS);

    DumpReleaseLock(intrState);
}

void
DumpFeatureInformation(
    IN  PCPUID_FEATURE_INFORMATION       FeatureInformation
    )
{
    ASSERT( NULL != FeatureInformation);
}

void
DumpCpuidValues(
    IN  DWORD               Index,
    IN  DWORD               SubIndex,
    IN  CPUID_INFO          Cpuid
    )
{
    LOG("Cpuid[0x%x]: [%s]\n", Index, RetrieveCpuIdLeafName(Index));
    LOG("Subindex: 0x%x\n", SubIndex);

    LOG("\t0x%08x EAX\n", Cpuid.eax);
    LOG("\t0x%08x EBX\n", Cpuid.ebx);
    LOG("\t0x%08x ECX\n", Cpuid.ecx);
    LOG("\t0x%08x EDX\n", Cpuid.edx);
}