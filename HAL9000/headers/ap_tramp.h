#pragma once

#include "list.h"

STATUS
ApTrampSetupLowerMemory(
    IN      PLIST_ENTRY     CpuList,
    OUT     DWORD*          ApStartAddress
    );

void
ApTrampCleanupLowerMemory(
    IN      PLIST_ENTRY     CpuList
    );

void
ApInitCpu(
    IN      struct _PCPU*   Cpu
    );