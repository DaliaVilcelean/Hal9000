#include "HAL9000.h"
#include "acpi_interface.h"
#include "cpumu.h"
#include "list.h"

#include "accommon.h"
#include "io.h"

#define APIC_PROCESSOR_ACTIVE                   0x1

#define APIC_PIC_METHOD_APIC                    0x1

#define ACPI_INIT_FLAGS     (       ACPI_NO_HARDWARE_INIT   \
                                |   ACPI_NO_EVENT_INIT      \
                                |   ACPI_NO_HANDLER_INIT    \
                                |   ACPI_NO_ACPI_ENABLE     \
                                )

typedef struct _ACPI_CPU_ENTRY
{
    ACPI_MADT_LOCAL_APIC        Data;
    LIST_ENTRY                  ListEntry;
} ACPI_CPU_ENTRY, *PACPI_CPU_ENTRY;

typedef struct _ACPI_IO_APIC_ENTRY
{
    ACPI_MADT_IO_APIC           Data;
    LIST_ENTRY                  ListEntry;
} ACPI_IO_APIC_ENTRY, *PACPI_IO_APIC_ENTRY;

typedef struct _ACPI_INTERRUPT_OVERRIDE_ENTRY
{
    ACPI_MADT_INTERRUPT_OVERRIDE    Data;
    LIST_ENTRY                      ListEntry;
} ACPI_INTERRUPT_OVERRIDE_ENTRY, *PACPI_INTERRUPT_OVERRIDE_ENTRY;

typedef struct _ACPI_MCFG_ENTRY
{
    ACPI_MCFG_ALLOCATION        Data;
    LIST_ENTRY                  ListEntry;
} ACPI_MCFG_ENTRY, *PACPI_MCFG_ENTRY;

typedef struct _ACPI_PRT_ENTRY
{
    ACPI_PCI_ROUTING_TABLE      Data;
    WORD                        SegmentNumber;
    BYTE                        BusNumber;
    LIST_ENTRY                  ListEntry;
} ACPI_PRT_ENTRY, *PACPI_PRT_ENTRY;

typedef struct _ACPI_INTERFACE_DATA
{
    LIST_ENTRY                  CpuList;
    LIST_ENTRY                  IoApicList;
    LIST_ENTRY                  IntOverrideList;
    LIST_ENTRY                  McfgList;
    LIST_ENTRY                  PrtList;
} ACPI_INTERFACE_DATA, *PACPI_INTERFACE_DATA;

static ACPI_INTERFACE_DATA      m_acpiData;

static
STATUS
_AcpiInterfaceParseMadt(
    void
    );

static
STATUS
_AcpiInterfaceParseMcfg(
    void
    );

static
STATUS
_AcpiInterfaceParsePrts(
    void
    );

static
ACPI_STATUS
_AcpiInterfaceDeviceWalkCallback(
    ACPI_HANDLE                     Object,
    UINT32                          NestingLevel,
    void                            *Context,
    void                            **ReturnValue
    );

static
STATUS
_AcpiRetrieveBusAndSegmentNumber(
    IN      ACPI_HANDLE         Object,
    OUT     BYTE*               BusNumber,
    OUT     WORD*               SegmentNumber,
    OUT     BOOLEAN*            FoundInParent
    );

void
AcpiInterfacePreinit(
    void
    )
{
    InitializeListHead(&m_acpiData.CpuList);
    InitializeListHead(&m_acpiData.IoApicList);
    InitializeListHead(&m_acpiData.IntOverrideList);
    InitializeListHead(&m_acpiData.McfgList);
    InitializeListHead(&m_acpiData.PrtList);
}

STATUS
AcpiInterfaceInit(
    void
    )
{
    ACPI_STATUS acpiStatus;
    STATUS status;

    LOG_FUNC_START;


    acpiStatus = AcpiInitializeTables(NULL, 16, TRUE);
    if (ACPI_FAILURE(acpiStatus))
    {
        LOG_FUNC_ERROR("AcpiInitializeTables", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Successfully initialized ACPI tables\n");



    status = _AcpiInterfaceParseMadt();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_AcpiInterfaceParseMadt", status );
        return status;
    }
    LOGL("Successfully parsed MADT\n");

    status = _AcpiInterfaceParseMcfg();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_AcpiInterfaceParseMcfg", status);
        if( status != STATUS_DEVICE_DOES_NOT_EXIST )
        {
            // we can get along without PCI Express support
            return status;
        }

        status = STATUS_SUCCESS;
    }
    else
    {
        LOGL("Successfully parsed MCFG\n");
    }


    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

void
AcpiShutdown(
    void
    )
{
    ACPI_STATUS acpiStatus;

    CpuIntrDisable();

    // Prepare to enter a system sleep state. This function should be 
    // called before a call to AcpiEnterSleepState.
    acpiStatus = AcpiEnterSleepStatePrep(5);
    ASSERT_INFO(AE_OK == acpiStatus,
                "AcpiEnterSleepStatePrep failed with ACPI status 0x%x\n",
                acpiStatus
                );
    LOGL("Successfully entered prep sleep\n");

    // This function must be called with interrupts disabled.
    // Enter S5 sleep state a.k.a shutdown
    acpiStatus = AcpiEnterSleepState(5);
    ASSERT_INFO(AE_OK == acpiStatus,
                "AcpiEnterSleepState failed with ACPI status 0x%x\n",
                acpiStatus
                );
    LOGL("We should never get here\n");

    NOT_REACHED;
}

STATUS
AcpiInterfaceLateInit(
    void
    )
{
    STATUS status;
    ACPI_STATUS acpiStatus;

    // see 10.1.2.1 of ACPICA documentation
    acpiStatus = AcpiInitializeSubsystem();
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiInitializeTables", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Successfully initialized ACPI subsystem\n");

    acpiStatus = AcpiLoadTables();
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiLoadTables", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Successfully loaded ACPI tables\n");

    acpiStatus = AcpiEnableSubsystem(ACPI_INIT_FLAGS);
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiEnableSubsystem", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Successfully enabled ACPI subsystem\n");

    acpiStatus = AcpiInitializeObjects(ACPI_INIT_FLAGS);
    if (ACPI_FAILURE(acpiStatus))
    {
        LOG_FUNC_ERROR("AcpiInitializeObjects", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Successfully initialized ACPI objects\n");

    status = _AcpiInterfaceParsePrts();
    if (!SUCCEEDED(status))
    {
        LOG_WARNING("_AcpiInterfaceParsePrts failed with status 0x%x\n", status);
    }
    LOGL("Successfully parsed PRTs\n");

    return status;
}

STATUS
AcpiRetrieveNextCpu(
    IN      BOOLEAN                     RestartSearch,
    OUT_PTR ACPI_MADT_LOCAL_APIC**      AcpiEntry
    )
{
    PACPI_CPU_ENTRY pEntry;

    static PLIST_ENTRY __pCurEntry = NULL;

    if (NULL == AcpiEntry)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (RestartSearch)
    {
        __pCurEntry = m_acpiData.CpuList.Flink;
    }

    if (__pCurEntry == &m_acpiData.CpuList)
    {
        return STATUS_NO_MORE_OBJECTS;
    }

    pEntry = CONTAINING_RECORD(__pCurEntry, ACPI_CPU_ENTRY, ListEntry);
    __pCurEntry = __pCurEntry->Flink;

    *AcpiEntry = &pEntry->Data;

    return STATUS_SUCCESS;
}

STATUS
AcpiRetrieveNextIoApic(
    IN      BOOLEAN                     RestartSearch,
    OUT_PTR ACPI_MADT_IO_APIC**         AcpiEntry
    )
{
    PACPI_IO_APIC_ENTRY pEntry;

    static PLIST_ENTRY __pCurEntry = NULL;

    if (NULL == AcpiEntry)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (RestartSearch)
    {
        __pCurEntry = m_acpiData.IoApicList.Flink;
    }

    if (__pCurEntry == &m_acpiData.IoApicList)
    {
        return STATUS_NO_MORE_OBJECTS;
    }

    pEntry = CONTAINING_RECORD(__pCurEntry, ACPI_IO_APIC_ENTRY , ListEntry);
    __pCurEntry = __pCurEntry->Flink;

    *AcpiEntry = &pEntry->Data;

    return STATUS_SUCCESS;
}

STATUS
AcpiRetrieveNextInterruptOverride(
    IN      BOOLEAN                         RestartSearch,
    OUT_PTR ACPI_MADT_INTERRUPT_OVERRIDE**  AcpiEntry
    )
{
    PACPI_INTERRUPT_OVERRIDE_ENTRY pEntry;

    static PLIST_ENTRY __pCurEntry = NULL;

    if (NULL == AcpiEntry)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (RestartSearch)
    {
        __pCurEntry = m_acpiData.IntOverrideList.Flink;
    }

    if (__pCurEntry == &m_acpiData.IntOverrideList)
    {
        return STATUS_NO_MORE_OBJECTS;
    }

    pEntry = CONTAINING_RECORD(__pCurEntry, ACPI_INTERRUPT_OVERRIDE_ENTRY, ListEntry);
    __pCurEntry = __pCurEntry->Flink;

    *AcpiEntry = &pEntry->Data;

    return STATUS_SUCCESS;
}

STATUS
AcpiRetrieveNextMcfgEntry(
    IN      BOOLEAN                     RestartSearch,
    OUT_PTR ACPI_MCFG_ALLOCATION**      AcpiEntry
    )
{
    PACPI_MCFG_ENTRY pEntry;

    static PLIST_ENTRY __pCurEntry = NULL;

    if (NULL == AcpiEntry)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (RestartSearch)
    {
        __pCurEntry = m_acpiData.McfgList.Flink;
    }

    if (__pCurEntry == &m_acpiData.McfgList)
    {
        return STATUS_NO_MORE_OBJECTS;
    }

    pEntry = CONTAINING_RECORD(__pCurEntry, ACPI_MCFG_ENTRY, ListEntry);
    __pCurEntry = __pCurEntry->Flink;

    *AcpiEntry = &pEntry->Data;

    return STATUS_SUCCESS;
}

STATUS
AcpiRetrieveNextPrtEntry(
    IN      BOOLEAN                     RestartSearch,
    OUT_PTR ACPI_PCI_ROUTING_TABLE**    AcpiEntry,
    OUT     BYTE*                       BusNumber,
    OUT     WORD*                       SegmentNumber
    )
{
    PACPI_PRT_ENTRY pEntry;

    static PLIST_ENTRY __pCurEntry = NULL;

    if (NULL == AcpiEntry)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (RestartSearch)
    {
        __pCurEntry = m_acpiData.PrtList.Flink;
    }

    if (__pCurEntry == &m_acpiData.PrtList)
    {
        return STATUS_NO_MORE_OBJECTS;
    }

    pEntry = CONTAINING_RECORD(__pCurEntry, ACPI_PRT_ENTRY, ListEntry);
    __pCurEntry = __pCurEntry->Flink;

    *AcpiEntry = &pEntry->Data;
    *BusNumber = pEntry->BusNumber;
    *SegmentNumber = pEntry->SegmentNumber;

    return STATUS_SUCCESS;
}

static
STATUS
_AcpiInterfaceParseMadt(
    void
    )
{
    ACPI_TABLE_HEADER* table;
    ACPI_STATUS acpiStatus;
    DWORD actualTableLength;
    DWORD offsetInTable;
    ACPI_SUBTABLE_HEADER* pHeader;
    PBYTE pData;

    acpiStatus = AcpiGetTable(ACPI_SIG_MADT, 1, &table);
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiGetTable", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }

    offsetInTable = 0;
    actualTableLength = table->Length - sizeof(ACPI_TABLE_MADT);
    pData = (BYTE*)table + sizeof(ACPI_TABLE_MADT);
    while (offsetInTable < actualTableLength)
    {
        pHeader = (ACPI_SUBTABLE_HEADER*)&(pData[offsetInTable]);
        if (ACPI_MADT_TYPE_LOCAL_APIC == pHeader->Type)
        {
            ACPI_MADT_LOCAL_APIC* pProc = (ACPI_MADT_LOCAL_APIC*)pHeader;
            if (pProc->LapicFlags & APIC_PROCESSOR_ACTIVE)
            {
                PACPI_CPU_ENTRY pEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(ACPI_CPU_ENTRY), HEAP_ACPIIF_TAG, 0);
                if (NULL == pEntry)
                {
                    LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(ACPI_CPU_ENTRY));
                    return STATUS_HEAP_NO_MORE_MEMORY;
                }

                memcpy(&pEntry->Data, pProc, sizeof(ACPI_MADT_LOCAL_APIC));

                InsertTailList(&m_acpiData.CpuList, &pEntry->ListEntry);
            }
        }
        else if (ACPI_MADT_TYPE_IO_APIC == pHeader->Type)
        {
            ACPI_MADT_IO_APIC* pIoapic = (ACPI_MADT_IO_APIC*)pHeader;
            PACPI_IO_APIC_ENTRY pEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(ACPI_IO_APIC_ENTRY), HEAP_ACPIIF_TAG, 0);
            if (NULL == pEntry)
            {
                LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(ACPI_IO_APIC_ENTRY));
                return STATUS_HEAP_NO_MORE_MEMORY;
            }

            LOG("\nIO APIC\n");
            LOG("IOAPIC at phys address: 0x%x\n", pIoapic->Address);
            LOG("IRQ base: 0x%x\n", pIoapic->GlobalIrqBase);
            LOG("ID: 0x%x\n", pIoapic->Id);

            memcpy(&pEntry->Data, pIoapic, sizeof(ACPI_MADT_IO_APIC));

            InsertTailList(&m_acpiData.IoApicList, &pEntry->ListEntry);
        }
        else if (ACPI_MADT_TYPE_INTERRUPT_OVERRIDE == pHeader->Type)
        {
            ACPI_MADT_INTERRUPT_OVERRIDE* pIntOverride = (ACPI_MADT_INTERRUPT_OVERRIDE*)pHeader;
            PACPI_INTERRUPT_OVERRIDE_ENTRY pEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(ACPI_INTERRUPT_OVERRIDE_ENTRY), HEAP_ACPIIF_TAG, 0);
            if (NULL == pEntry)
            {
                LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(ACPI_INTERRUPT_OVERRIDE_ENTRY));
                return STATUS_HEAP_NO_MORE_MEMORY;
            }

            LOG("\nInterrupt override\n");
            LOG("Bus: 0x%x\n", pIntOverride->Bus);  
            LOG("SourceIrq: 0x%x\n", pIntOverride->SourceIrq);
            LOG("GlobalIrq: 0x%x\n", pIntOverride->GlobalIrq); 
            LOG("IntiFlags: 0x%x\n", pIntOverride->IntiFlags);

            memcpy(&pEntry->Data, pIntOverride, sizeof(ACPI_MADT_INTERRUPT_OVERRIDE));

            InsertTailList(&m_acpiData.IntOverrideList, &pEntry->ListEntry);
        }
        else if (ACPI_MADT_TYPE_LOCAL_APIC_NMI == pHeader->Type)
        {
            ACPI_MADT_LOCAL_APIC_NMI* pApicNmi = (ACPI_MADT_LOCAL_APIC_NMI*)pHeader;

            LOG("\nLocal NMI\n");
            LOG("Processor id: 0x%x\n", pApicNmi->ProcessorId);
            LOG("IntiFlags id: 0x%x\n", pApicNmi->IntiFlags);
            LOG("Lint id: 0x%x\n", pApicNmi->Lint);
        }
        else
        {
            LOG("pProc->Header.Type: 0x%x\n", pHeader->Type);
        }

        offsetInTable = offsetInTable + pHeader->Length;
    }

    return STATUS_SUCCESS;
}

static
STATUS
_AcpiInterfaceParseMcfg(
    void
    )
{
    ACPI_TABLE_HEADER* table;
    ACPI_STATUS acpiStatus;
    DWORD actualTableLength;
    DWORD offsetInTable;
    ACPI_MCFG_ALLOCATION* pMcfgEntry;
    PBYTE pData;

    acpiStatus = AcpiGetTable(ACPI_SIG_MCFG, 1, &table);
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiGetTable", acpiStatus);
        return acpiStatus == AE_NOT_FOUND ? STATUS_DEVICE_DOES_NOT_EXIST : STATUS_UNSUCCESSFUL;
    }

    actualTableLength = table->Length - sizeof(ACPI_TABLE_MCFG);
    pData = (BYTE*)table + sizeof(ACPI_TABLE_MCFG);
    for (offsetInTable = 0; offsetInTable < actualTableLength; offsetInTable += sizeof(ACPI_MCFG_ALLOCATION))
    {
        PACPI_MCFG_ENTRY pEntry;

        pMcfgEntry = (ACPI_MCFG_ALLOCATION*) &pData[offsetInTable];

        pEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(ACPI_MCFG_ENTRY), HEAP_ACPIIF_TAG, 0);
        if (NULL == pEntry)
        {
            LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(ACPI_MCFG_ENTRY));
            return STATUS_HEAP_NO_MORE_MEMORY;
        }

        memcpy(&pEntry->Data, pMcfgEntry, sizeof(ACPI_MCFG_ALLOCATION));

        InsertTailList(&m_acpiData.McfgList, &pEntry->ListEntry);

        LOG("MCFG entry:\n");
        LOG("Base address: 0x%X\n", pMcfgEntry->Address );
        LOG("Segment: 0x%x\n", pMcfgEntry->PciSegment );
        LOG("Bus: %u -> %u\n", pMcfgEntry->StartBusNumber, pMcfgEntry->EndBusNumber );
    }

    return STATUS_SUCCESS;
}

static
STATUS
_AcpiInterfaceParsePrts(
    void
    )
{
    ACPI_STATUS         acpiStatus;
    ACPI_OBJECT_LIST    argList;
    ACPI_OBJECT         arg;

    acpiStatus = AE_OK;
    memzero(&argList, sizeof(ACPI_OBJECT_LIST));
    memzero(&arg, sizeof(ACPI_OBJECT));

    LOG_FUNC_START;

    // One argument, IntegerArgument; No return value expected
    argList.Count = 1;
    argList.Pointer = &arg;
    arg.Type = ACPI_TYPE_INTEGER;
    arg.Integer.Value = (UINT64)APIC_PIC_METHOD_APIC;       // place in APIC mode

    acpiStatus = AcpiEvaluateObject(NULL, "\\_PIC", &argList, NULL);
    if (ACPI_FAILURE(acpiStatus))
    {
        LOG_WARNING("AcpiEvaluateObject failed with status 0x%x - Couldn't locate ACPI \\\\_PIC method\n", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }

    acpiStatus = AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, _AcpiInterfaceDeviceWalkCallback, NULL, NULL, NULL);
    if (AE_OK != acpiStatus)
    {
        LOG_FUNC_ERROR("AcpiWalkNamespace", acpiStatus);
        return STATUS_UNSUCCESSFUL;
    }

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

static
ACPI_STATUS
_AcpiInterfaceDeviceWalkCallback(
    ACPI_HANDLE                     Object,
    UINT32                          NestingLevel,
    void                            *Context,
    void                            **ReturnValue
    )
{
    ACPI_STATUS acpiStatus;
    ACPI_HANDLE prtHandle;
    ACPI_BUFFER buffer;
    DWORD noOfPrtEntries;
    BYTE busNumber;
    WORD segNumber;
    STATUS status;
    BOOLEAN bBusSourceInParent;

    ASSERT( NULL != Object );
    ASSERT( NULL == Context );
    ASSERT( NULL == ReturnValue );

    acpiStatus = AE_OK;
    prtHandle = NULL;
    memzero(&buffer, sizeof(ACPI_BUFFER));
    noOfPrtEntries = 0;
    busNumber = 0;
    segNumber = 0;
    status = STATUS_SUCCESS;
    bBusSourceInParent = FALSE;

    LOG_FUNC_START;

    __try
    {
        LOG_TRACE_ACPI("Nesting level %u\n", NestingLevel);

        acpiStatus = AcpiGetHandle(Object, METHOD_NAME__PRT, &prtHandle);
        if (AE_OK != acpiStatus)
        {
            LOG_TRACE_ACPI("No PRT method for device!\n");
            acpiStatus = AE_OK;
            __leave;
        }
        ASSERT(NULL != prtHandle);

        LOG_TRACE_ACPI("Found PRT method for object at 0x%X\n", Object);

        status = _AcpiRetrieveBusAndSegmentNumber(Object, &busNumber, &segNumber, &bBusSourceInParent);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_AcpiRetrieveBusAndSegmentNumber", status);
            acpiStatus = AE_NOT_FOUND;
            __leave;
        }
        LOG_TRACE_ACPI("Object 0x%X is on segment:bus 0x%x:0x%x found in parent: 0x%x\n",
             Object, segNumber, busNumber, bBusSourceInParent);

        buffer.Length = ACPI_ALLOCATE_BUFFER;
        buffer.Pointer = NULL;
        acpiStatus = AcpiGetIrqRoutingTable(Object, &buffer);
        if (AE_OK != acpiStatus)
        {
            LOG_FUNC_ERROR("AcpiGetIrqRoutingTable", acpiStatus);
            __leave;
        }

        if (bBusSourceInParent)
        {
            // If the BUS BASE NUMBER was found in a parent device =>
            // this is a secondary bridge device => its PRT entries will
            // correspond to the devices connected on its secondary bus
            QWORD addr;
            PCI_DEVICE_LOCATION pciLocation;

            acpiStatus = AcpiUtEvaluateNumericObject(METHOD_NAME__ADR, Object, &addr);
            if (AE_OK != acpiStatus)
            {
                LOG_FUNC_ERROR("AcpiUtEvaluateNumericObject", acpiStatus);
                __leave;
            }

            memzero(&pciLocation, sizeof(PCI_DEVICE_LOCATION));
            pciLocation.Bus = busNumber;
            pciLocation.Device = WORD_LOW(DWORD_HIGH(addr));
            pciLocation.Function = WORD_LOW(addr);

            status = IoGetPciSecondaryBusForBridge(pciLocation, &busNumber);
            if (!SUCCEEDED(status))
            {
                if (STATUS_ELEMENT_NOT_FOUND != status)
                {
                    LOG_FUNC_ERROR("IoGetPciDevicesMatchingLocation", status);
                    acpiStatus = AE_BAD_DATA;
                }
                else
                {
                    acpiStatus = AE_OK;
                }

                __leave;
            }
        }

        for (ACPI_PCI_ROUTING_TABLE* prtEntry = ACPI_CAST_PTR(ACPI_PCI_ROUTING_TABLE, buffer.Pointer);
             prtEntry->Length != 0;
             prtEntry = ACPI_ADD_PTR(ACPI_PCI_ROUTING_TABLE, prtEntry, prtEntry->Length))
        {
            PACPI_PRT_ENTRY pEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(ACPI_PRT_ENTRY), HEAP_ACPIIF_TAG, 0);
            if (NULL == pEntry)
            {
                LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(ACPI_PRT_ENTRY));
                acpiStatus = AE_NO_MEMORY;
                __leave;
            }

            memcpy(&pEntry->Data, prtEntry, sizeof(ACPI_PCI_ROUTING_TABLE));

            pEntry->BusNumber = busNumber;
            pEntry->SegmentNumber = segNumber;
            InsertTailList(&m_acpiData.PrtList, &pEntry->ListEntry);

            LOG_TRACE_ACPI("\nPRT address is 0x%X\n", prtEntry->Address);
            LOG_TRACE_ACPI("Bus number is 0x%x\n", busNumber);
            LOG_TRACE_ACPI("PIN is [%c]\n", 'A' + prtEntry->Pin);
            LOG_TRACE_ACPI("Source index 0x%x\n", prtEntry->SourceIndex);

            noOfPrtEntries++;
        }

        LOG("Number of PRT entries is %u\n", noOfPrtEntries);
    }
    __finally
    {
        if(NULL != buffer.Pointer)
        {
            AcpiOsFree(buffer.Pointer);
            buffer.Pointer = NULL;
        }

        LOG_FUNC_END;
    }

    return acpiStatus;
}

static
STATUS
_AcpiRetrieveBusAndSegmentNumber(
    IN      ACPI_HANDLE         Object,
    OUT     BYTE*               BusNumber,
    OUT     WORD*               SegmentNumber,
    OUT     BOOLEAN*            FoundInParent
    )
{
    QWORD busValue;
    ACPI_STATUS acpiStatus;
    ACPI_HANDLE currentObj;
    BOOLEAN bFoundInParent;

    ASSERT( NULL != Object );
    ASSERT( NULL != BusNumber );
    ASSERT( NULL != FoundInParent );

    acpiStatus = AE_NOT_FOUND;
    currentObj = Object;
    bFoundInParent = FALSE;

#pragma warning(suppress:4127)
    while(TRUE)
    {
        acpiStatus = AcpiUtEvaluateNumericObject(METHOD_NAME__BBN, currentObj, &busValue);
        if(ACPI_SUCCESS(acpiStatus))
        {
            break;
        }

        bFoundInParent = TRUE;
        acpiStatus = AcpiGetParent(currentObj, &currentObj);
        if(!ACPI_SUCCESS(acpiStatus))
        {
            LOG_ERROR("Failed to retrieve parent!\n");
            break;
        }
    }

    if (ACPI_SUCCESS(acpiStatus))
    {
        QWORD segmentValue;

        // ACPICA 6.5.6
        // If _SEG does not exist, OSPM assumes that all PCI bus segments are in PCI Segment Group 0.
        acpiStatus = AcpiUtEvaluateNumericObject(METHOD_NAME__SEG, currentObj, &segmentValue);
        *SegmentNumber = ACPI_SUCCESS(acpiStatus) ? (segmentValue & MAX_WORD) : 0;

        ASSERT(busValue <= MAX_BYTE);
        *BusNumber = (BYTE)busValue;

        *FoundInParent = bFoundInParent;

        acpiStatus = AE_OK;
    }

    return ACPI_FAILURE(acpiStatus) ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}