#include "HAL9000.h"
#include "ex.h"
#include "mmu.h"
#include "iomu.h"

_Always_(_When_(IsBooleanFlagOn(Flags, PoolAllocatePanicIfFail), RET_NOT_NULL))
PTR_SUCCESS
PVOID
ExAllocatePoolWithTag(
    IN      DWORD                   Flags,
    IN      DWORD                   AllocationSize,
    IN      DWORD                   Tag,
    IN      DWORD                   AllocationAlignment
    )
{
    return MmuAllocatePoolWithTag(Flags,AllocationSize,Tag,AllocationAlignment);
}

void
ExFreePoolWithTag(
    _Pre_notnull_ _Post_ptr_invalid_
            PVOID                   MemoryAddress,
    IN      DWORD                   Tag
    )
{
    MmuFreePoolWithTag(MemoryAddress, Tag);
}

void
ExGetSystemInformation(
    OUT     PSYSTEM_INFORMATION     SystemInformation
    )
{
    QWORD tickFreq;

    ASSERT( NULL != SystemInformation );

    tickFreq = 0;

    IomuGetSystemTicks(&tickFreq);

    SystemInformation->CpuFrequency = tickFreq;
    SystemInformation->SystemUptimeUs = IomuGetSystemTimeUs();

    SystemInformation->TotalPhysicalMemory = MmuGetTotalSystemMemory();
    SystemInformation->HighestPhysicalAddress = MmuGetHighestPhysicalMemoryAddressPresent();
}