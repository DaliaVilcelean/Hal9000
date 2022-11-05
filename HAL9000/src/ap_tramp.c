#include "HAL9000.h"
#include "cpumu.h"
#include "synch.h"
#include "ap_tramp.h"
#include "mmu.h"
#include "gdtmu.h"
#include "idt.h"
#include "thread_internal.h"

#define LOW_MEMORY_CONFIG_START         0x1000
#define LOW_MEMORY_CONFIG_SIZE          0x1000

#define LOW_MEMORY_CODE_START           0x2000
#define LOW_MEMORY_CODE_SIZE            0x1000

#define LOW_MEMORY_STACK_START          0x3000
#define LOW_MEMORY_STACK_SIZE           0x1000

#define PREDEFINED_SYSTEM_CONFIG_SIZE   0x1C
#define PREDEFINED_AP_CONFIG_ENTRY_SIZE 0x10

extern void PM32_to_PM64();
extern void PM32_to_PM64End();

extern void TrampolineStart();
extern void PM32_to_PM64_PlaceHolder();

extern void LowGdtTable();
extern void LowGdtTableEnd();

#pragma pack(push,1)

#pragma warning(push)

// warning C4200: nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable:4200)

typedef struct _SYSTEM_CONFIG
{
    GDT32               LowGdt;
    GDT                 HighGdt;
    DWORD               Pml4PhysicalAddress;
    WORD                Code32Selector;
    WORD                Data32Selector;
    WORD                Code64Selector;
    WORD                Data64Selector;
} SYSTEM_CONFIG, *PSYSTEM_CONFIG;
STATIC_ASSERT(sizeof(SYSTEM_CONFIG) == PREDEFINED_SYSTEM_CONFIG_SIZE);

typedef struct _AP_CONFIG_ENTRY
{
    DWORD               StackPhysicalAddress;
    DWORD               __Reserved0;
    QWORD               StackVirtualAddress;
} AP_CONFIG_ENTRY, *PAP_CONFIG_ENTRY;
STATIC_ASSERT(sizeof(AP_CONFIG_ENTRY) == PREDEFINED_AP_CONFIG_ENTRY_SIZE);

typedef struct _LOW_MEMORY_CONFIG
{
    SYSTEM_CONFIG       SystemConfig;
    AP_CONFIG_ENTRY     ApConfig[0];
} LOW_MEMORY_CONFIG, *PLOW_MEMORY_CONFIG;
#pragma warning(pop)
#pragma pack(pop)

STATUS
ApTrampSetupLowerMemory(
    IN      PLIST_ENTRY     CpuList,
    OUT     DWORD*          ApStartAddress
    )
{
    STATUS status;
    PBYTE pLowMemoryCode;
    QWORD initialCodeSize;
    QWORD placeholderCodeSize;
    QWORD lowGdtSize;
    PLOW_MEMORY_CONFIG pConfig;
    PLIST_ENTRY pCurEntry;
    PHYSICAL_ADDRESS cr3;
    GDT highGdt;

    ASSERT(NULL != CpuList);

    status = STATUS_SUCCESS;
    pLowMemoryCode = NULL;
    initialCodeSize = 0;
    placeholderCodeSize = 0;
    lowGdtSize = 0;
    pConfig = NULL;
    pCurEntry = NULL;
    cr3 = NULL;
    __sgdt(&highGdt);

    pLowMemoryCode = (PBYTE) LOW_MEMORY_CODE_START;
    MmuMapMemoryInternal((PHYSICAL_ADDRESS) LOW_MEMORY_CODE_START,
                         LOW_MEMORY_CODE_SIZE,
                         PAGE_RIGHTS_ALL,
                         pLowMemoryCode,
                         TRUE,
                         FALSE,
                         NULL
                         );

    MmuMapMemoryInternal((PHYSICAL_ADDRESS)LOW_MEMORY_CODE_START,
                         LOW_MEMORY_CODE_SIZE,
                         PAGE_RIGHTS_ALL,
                         (PVOID) PA2VA(pLowMemoryCode),
                         TRUE,
                         FALSE,
                         NULL
                         );

    initialCodeSize = (QWORD) PM32_to_PM64_PlaceHolder - (QWORD) TrampolineStart;
    ASSERT(initialCodeSize <= LOW_MEMORY_CODE_SIZE);

    // warning C4152: nonstandard extension, function/data pointer conversion in expression
#pragma warning(suppress:4152)
    memcpy(pLowMemoryCode, TrampolineStart, initialCodeSize);

    placeholderCodeSize = (QWORD)PM32_to_PM64End - (QWORD)PM32_to_PM64;
    ASSERT(initialCodeSize + placeholderCodeSize <= LOW_MEMORY_CODE_SIZE);

    // warning C4152: nonstandard extension, function/data pointer conversion in expression
#pragma warning(suppress:4152)
    memcpy(pLowMemoryCode + initialCodeSize, PM32_to_PM64, placeholderCodeSize);

    __try
    {
        // setup SYSTEM_CONFIG
        pConfig = MmuMapSystemMemory((PVOID)LOW_MEMORY_CONFIG_START, LOW_MEMORY_CONFIG_SIZE);
        if (NULL == pConfig)
        {
            status = STATUS_MEMORY_CANNOT_BE_MAPPED;
            __leave;
        }

        // Determine how much memory we have reserved for the LowGdt
        lowGdtSize = (QWORD)LowGdtTableEnd - (QWORD)LowGdtTable;
        ASSERT(IsAddressAligned(lowGdtSize, sizeof(SEGMENT_DESCRIPTOR)));
        ASSERT(lowGdtSize <= MAX_WORD + 1);
        ASSERT(initialCodeSize + placeholderCodeSize + lowGdtSize <= LOW_MEMORY_CODE_SIZE);

        memcpy(&pConfig->SystemConfig.HighGdt, &highGdt, sizeof(GDT));
#pragma warning(suppress:4152)
        memcpy(pLowMemoryCode + initialCodeSize + placeholderCodeSize, pConfig->SystemConfig.HighGdt.Base, min(lowGdtSize, pConfig->SystemConfig.HighGdt.Limit + 1));

        pConfig->SystemConfig.LowGdt.Base = (DWORD)((QWORD)pLowMemoryCode + initialCodeSize + placeholderCodeSize);
        pConfig->SystemConfig.LowGdt.Limit = (WORD)(lowGdtSize - 1);
        cr3 = __readcr3();
        ASSERT((QWORD)cr3 <= MAX_DWORD);

        // warning C4311: 'type cast': pointer truncation from 'PHYSICAL_ADDRESS' to 'DWORD'
#pragma warning(suppress:4311)
        pConfig->SystemConfig.Pml4PhysicalAddress = (DWORD)cr3;

        LOGL("CR3: 0x%X\n", cr3);
        LOGL("Low GDT base: 0x%x\n", pConfig->SystemConfig.LowGdt.Base);
        LOGL("Low GDT limit: 0x%x\n", pConfig->SystemConfig.LowGdt.Limit);
        LOGL("High GDT base: 0x%X\n", pConfig->SystemConfig.HighGdt.Base);
        LOGL("High GDT limit: 0x%x\n", pConfig->SystemConfig.HighGdt.Limit);

        pConfig->SystemConfig.Code64Selector = GdtMuGetCS64Supervisor();
        pConfig->SystemConfig.Data64Selector = GdtMuGetDS64Supervisor();

        pConfig->SystemConfig.Code32Selector = GdtMuGetCS32Supervisor();
        pConfig->SystemConfig.Data32Selector = GdtMuGetDS32Supervisor();

        // setup AP_CONFIG_ENTRY for each CPU
        for (pCurEntry = CpuList->Flink;
             pCurEntry != CpuList;
             pCurEntry = pCurEntry->Flink)
        {
            PPCPU pCpu = CONTAINING_RECORD(pCurEntry, PCPU, ListEntry);

            if (!pCpu->BspProcessor)
            {
                APIC_ID apicId = pCpu->ApicId;

                pConfig->ApConfig[apicId].StackPhysicalAddress = LOW_MEMORY_STACK_START + LOW_MEMORY_STACK_SIZE * apicId;

                // warning C4312: 'type cast': conversion from 'DWORD' to 'PHYSICAL_ADDRESS' of greater size
#pragma warning(suppress:4312)
                MmuMapMemoryInternal((PHYSICAL_ADDRESS)(pConfig->ApConfig[apicId].StackPhysicalAddress - LOW_MEMORY_STACK_SIZE),
                                     LOW_MEMORY_STACK_SIZE,
                                     PAGE_RIGHTS_READWRITE,
                                     (PVOID)((QWORD)pConfig->ApConfig[apicId].StackPhysicalAddress - LOW_MEMORY_STACK_SIZE),
                                     TRUE,
                                     FALSE,
                                     NULL
                );

                pConfig->ApConfig[apicId].StackVirtualAddress = (QWORD)pCpu->StackTop;

                LOGL("Cpu at: 0x%X\n", pCpu);
                LOGL("Setup AP config for apic id 0x%x\n", apicId);
                LOGL("Physical Stack PA: 0x%x\n", pConfig->ApConfig[apicId].StackPhysicalAddress);
                LOGL("Virtual Stack VA: 0x%X\n", pConfig->ApConfig[apicId].StackVirtualAddress);
            }
        }
    }
    __finally
    {
        // there is no need of a cleanup because after all APs will be started
        // all unnecessary memory will be unmapped
        *ApStartAddress = LOW_MEMORY_CODE_START;
    }

    return status;
}

void
ApTrampCleanupLowerMemory(
    IN      PLIST_ENTRY     CpuList
    )
{
    PLIST_ENTRY pCurEntry;

    ASSERT( NULL != CpuList );

    MmuUnmapSystemMemory((PVOID) LOW_MEMORY_CODE_START,
                   LOW_MEMORY_CODE_SIZE
                   );

    MmuUnmapSystemMemory((PVOID) PA2VA(LOW_MEMORY_CODE_START),
                   LOW_MEMORY_CODE_SIZE
                   );

    // setup AP_CONFIG_ENTRY for each CPU
    for(pCurEntry = CpuList->Flink;
        pCurEntry != CpuList;
        pCurEntry = pCurEntry->Flink)
    {
        PPCPU pCpu = CONTAINING_RECORD(pCurEntry, PCPU, ListEntry);

        if (!pCpu->BspProcessor)
        {
            APIC_ID apicId = pCpu->ApicId;
            QWORD stackVirtualAddress = LOW_MEMORY_STACK_START + LOW_MEMORY_STACK_SIZE * apicId;

            MmuUnmapSystemMemory((PVOID) ( stackVirtualAddress - LOW_MEMORY_STACK_SIZE ),
                           LOW_MEMORY_STACK_SIZE
                           );
        }
    }
}

void    
ApInitCpu(
    IN      struct _PCPU*   Cpu
    )
{
    STATUS status;

    CHECK_STACK_ALIGNMENT;

    status = STATUS_SUCCESS;

    LOGPL("Hello C!, CPU at: 0x%X\n", Cpu);

    // we need to reload GDT with new one
    GdtMuReload(GdtMuGetCS64Supervisor(), GdtMuGetDS64Supervisor(), FALSE, FALSE);

    __try
    {
        status = CpuMuActivateFpuFeatures();
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("CpuMuActivateFpuFeatures", status);
            __leave;
        }

    // reload IDT
    IdtReload();

    status = CpuMuInitCpu( (PCPU*) Cpu, FALSE);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("CpuMuInitCpu", status );
            __leave;
    }

    MmuActivateProcessIds();

    status = ThreadSystemInitIdleForCurrentCPU();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("ThreadSystemInitIdleForCurrentCPU", status);
            __leave;
    }

    // exit main thread
    ThreadExit(STATUS_SUCCESS);
    }
    __finally
    {
    NOT_REACHED;
    }
}