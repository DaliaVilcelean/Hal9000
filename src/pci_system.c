#include "HAL9000.h"
#include "list.h"
#include "pci_system.h"
#include "acpi_interface.h"
#include "io.h"
#include "pcie.h"

/// to remove
#include "dmp_pci.h"

typedef struct _PCI_ROOT_COMPLEX_ENTRY
{
    PCI_ROOT_COMPLEX    RootComplex;

    LIST_ENTRY          ListEntry;
} PCI_ROOT_COMPLEX_ENTRY, *PPCI_ROOT_COMPLEX_ENTRY;

typedef struct _PCI_SYSTEM_DATA
{
    BOOLEAN             PciExpressSupport;

    LIST_ENTRY          PciRootComplexList;
} PCI_SYSTEM_DATA, *PPCI_SYSTEM_DATA;

static PCI_SYSTEM_DATA  m_pciSystemData;

static
__forceinline
PTR_SUCCESS
PPCI_ROOT_COMPLEX
_PciSystemFindRootComplexForDevice(
    IN          PCI_DEVICE_LOCATION         DeviceLocation
    )
{
    PLIST_ENTRY pEntry;

    for( pEntry = m_pciSystemData.PciRootComplexList.Flink;
        pEntry != &m_pciSystemData.PciRootComplexList;
        pEntry = pEntry->Flink)
    {
        PPCI_ROOT_COMPLEX_ENTRY pRootComplex = CONTAINING_RECORD(pEntry, PCI_ROOT_COMPLEX_ENTRY, ListEntry);

        if ( ( pRootComplex->RootComplex.StartBusNumber <= DeviceLocation.Bus ) && ( DeviceLocation.Bus <= pRootComplex->RootComplex.EndBusNumber ) )
        {
            return &pRootComplex->RootComplex;
        }
    }

    return NULL;
}

static
STATUS
_PciSystemReadConfigurationSpace(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    OUT     DWORD*                  Value
    );

static
STATUS
_PciSystemWriteConfigurationSpace(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    IN      DWORD                   Value
    );

static
STATUS
_PciSystemInitRootComplexEntry(
    IN          ACPI_MCFG_ALLOCATION*       AcpiEntry,
    OUT_PTR     PPCI_ROOT_COMPLEX_ENTRY*    RootComplexEntry
    );

static
STATUS
_PciSystemRetrievePciExpressDevices(
    INOUT   PLIST_ENTRY     PciDeviceList,
    OUT     PDWORD          NumberOfDevices
    );

static
STATUS
_PciSystemRetrievePciDevices(
    INOUT   PLIST_ENTRY     PciDeviceList,
    OUT     PDWORD          NumberOfDevices
    );

static
STATUS
_PciSystemRetrieveParentForDevice(
    IN      PLIST_ENTRY                 BridgeList,
    IN      BYTE                        Bus,
    OUT     PPCI_DEVICE_DESCRIPTION*    ParentDevice
    );

void
PciSystemPreinit(
    void
    )
{
    memzero(&m_pciSystemData, sizeof(PCI_SYSTEM_DATA));

    InitializeListHead(&m_pciSystemData.PciRootComplexList);
}

STATUS
PciSystemInit(
    void
    )
{
    STATUS status;
    BOOLEAN bRestartSearch;
    ACPI_MCFG_ALLOCATION* pEntry;
    PCI_ROOT_COMPLEX_ENTRY* pMcfg;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    bRestartSearch = TRUE;
    pEntry = FALSE;
    pMcfg = NULL;

#pragma warning(suppress:4127)
    while (TRUE)
    {
        ASSERT( NULL == pMcfg);

        status = AcpiRetrieveNextMcfgEntry(bRestartSearch, &pEntry);
        if (STATUS_NO_MORE_OBJECTS == status)
        {
            LOGL("Reached end of MCFG list\n");
            status = STATUS_SUCCESS;
            break;
        }

        status = _PciSystemInitRootComplexEntry(pEntry, &pMcfg);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_PciSystemInitRootComplexEntry", status);
            return status;
        }

        InsertTailList(&m_pciSystemData.PciRootComplexList, &pMcfg->ListEntry);

        bRestartSearch = FALSE;
        pMcfg = NULL;
    }

    // we have at least one 1 MCFG structure => we have PCI express support
    m_pciSystemData.PciExpressSupport = !IsListEmpty(&m_pciSystemData.PciRootComplexList);

    LOG_FUNC_END;

    return status;
}

STATUS
PciSystemRetrieveDevices(
    INOUT   PLIST_ENTRY     PciDeviceList
    )
{
    STATUS status;
    DWORD noOfDevicesRetrieved;

    if (NULL == PciDeviceList)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    status = STATUS_SUCCESS;
    noOfDevicesRetrieved = 0;

    LOGL("PCI Express support is %s available\n", 
         !m_pciSystemData.PciExpressSupport ? "NOT" : "" );

    if (m_pciSystemData.PciExpressSupport)
    {
        status = _PciSystemRetrievePciExpressDevices(PciDeviceList, &noOfDevicesRetrieved);
    }
    else
    {
        status = _PciSystemRetrievePciDevices(PciDeviceList, &noOfDevicesRetrieved);
    }
    if (!SUCCEEDED(status))
    {
        LOG_ERROR("Failed to retrieved PCI devices with status: 0x%x\n", status);
        return status;
    }


    LOG("Retrieved %d PCI %s devices\n", 
        noOfDevicesRetrieved,
        m_pciSystemData.PciExpressSupport ? "Express" : "" );

    return status;
}

void
PciSystemEstablishHierarchy(
    IN      PLIST_ENTRY     PciDeviceList,
    INOUT   PLIST_ENTRY     PciBridgeList
    )
{
    ASSERT(NULL != PciDeviceList);
    ASSERT(NULL != PciBridgeList);

    for (PLIST_ENTRY pEntry = PciDeviceList->Flink;
         pEntry != PciDeviceList;
         pEntry = pEntry->Flink)
    {
        PPCI_DEVICE_LIST_ENTRY pDevice = CONTAINING_RECORD(pEntry, PCI_DEVICE_LIST_ENTRY, ListEntry);

        if(pDevice->PciDevice.DeviceData->Header.HeaderType.Layout == PCI_HEADER_LAYOUT_PCI_TO_PCI)
        {
            LOG_TRACE_PCI("Added bridge device (%u.%u.%u)\n",
                pDevice->PciDevice.DeviceLocation.Bus,
                pDevice->PciDevice.DeviceLocation.Device,
                pDevice->PciDevice.DeviceLocation.Function);
            InsertTailList(PciBridgeList, &pDevice->BridgeEntry);
        }

        if(pDevice->PciDevice.DeviceLocation.Bus != 0)
        {
            // find parent bus
            STATUS status = _PciSystemRetrieveParentForDevice(PciBridgeList, 
                                                              pDevice->PciDevice.DeviceLocation.Bus,
                                                              &pDevice->PciDevice.Parent);
            if(!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("_PciSystemRetrieveParentForDevice", status);
                NOT_REACHED;
            }

            LOG_TRACE_PCI("Found parent (%u.%u.%u) for device (%u.%u.%u)\n",
                pDevice->PciDevice.Parent->DeviceLocation.Bus,
                pDevice->PciDevice.Parent->DeviceLocation.Device,
                pDevice->PciDevice.Parent->DeviceLocation.Function,
                pDevice->PciDevice.DeviceLocation.Bus,
                pDevice->PciDevice.DeviceLocation.Device,
                pDevice->PciDevice.DeviceLocation.Function);
        }
    }
}

STATUS
PciSystemFindDevicesMatchingSpecification(
    IN      PLIST_ENTRY     PciDeviceList,
    IN      PCI_SPEC        Specification,
    OUT_WRITES_OPT(*NumberOfDevices) 
            PPCI_DEVICE_DESCRIPTION*    PciDevices,
    _When_(NULL == PciDevices, OUT)
    _When_(NULL != PciDevices, INOUT)
            DWORD*          NumberOfDevices
    )
{
    PLIST_ENTRY pCurEntry;
    DWORD noOfDevicesFound;

    if (NULL == PciDeviceList)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (NULL == NumberOfDevices)
    {
        return STATUS_INVALID_PARAMETER5;
    }

    noOfDevicesFound = 0;

    for (pCurEntry = PciDeviceList->Flink;
        pCurEntry != PciDeviceList;
        pCurEntry = pCurEntry->Flink)
    {
        PPCI_DEVICE_LIST_ENTRY pDevice = CONTAINING_RECORD(pCurEntry, PCI_DEVICE_LIST_ENTRY, ListEntry);

        if (!Specification.MatchClass || pDevice->PciDevice.DeviceData->Header.ClassCode == Specification.Description.ClassCode )
        {
            if (!Specification.MatchSubclass || pDevice->PciDevice.DeviceData->Header.Subclass ==  Specification.Description.Subclass)
            {
                if (!Specification.MatchVendor || pDevice->PciDevice.DeviceData->Header.VendorID == Specification.Description.VendorId)
                {
                    if (!Specification.MatchDevice || pDevice->PciDevice.DeviceData->Header.DeviceID ==  Specification.Description.DeviceId)
                    {
                        if (NULL != PciDevices)
                        {
                            if (noOfDevicesFound > *NumberOfDevices)
                            {
                                return STATUS_BUFFER_TOO_SMALL;
                            }

                            PciDevices[noOfDevicesFound] = &pDevice->PciDevice;
                        }
                        noOfDevicesFound = noOfDevicesFound + 1;
                    }
                }
            }
        }
    }

    *NumberOfDevices = noOfDevicesFound;

    return (0 != noOfDevicesFound) ? STATUS_SUCCESS : STATUS_ELEMENT_NOT_FOUND;
}

STATUS
PciSystemFindDevicesMatchingLocation(
    IN      PLIST_ENTRY                 PciDeviceList,
    IN      PCI_SPEC_LOCATION           Specification,
    OUT_WRITES_OPT(*NumberOfDevices) 
            PPCI_DEVICE_DESCRIPTION*    PciDevices,
    _When_(NULL == PciDevices, OUT)
    _When_(NULL != PciDevices, INOUT)
            DWORD*                      NumberOfDevices
    )
{
    PLIST_ENTRY pCurEntry;
    DWORD noOfDevicesFound;

    if (NULL == PciDeviceList)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (NULL == NumberOfDevices)
    {
        return STATUS_INVALID_PARAMETER5;
    }

    noOfDevicesFound = 0;

    for (pCurEntry = PciDeviceList->Flink;
         pCurEntry != PciDeviceList;
         pCurEntry = pCurEntry->Flink)
    {
        PPCI_DEVICE_LIST_ENTRY pDevice = CONTAINING_RECORD(pCurEntry, PCI_DEVICE_LIST_ENTRY, ListEntry);

        if (!Specification.MatchBus || pDevice->PciDevice.DeviceLocation.Bus == Specification.Location.Bus)
        {
            if (!Specification.MatchDevice || pDevice->PciDevice.DeviceLocation.Device == Specification.Location.Device)
            {
                if (!Specification.MatchFunction || pDevice->PciDevice.DeviceLocation.Function == Specification.Location.Function)
                {
                    if (NULL != PciDevices)
                    {
                        if (noOfDevicesFound > *NumberOfDevices)
                        {
                            return STATUS_BUFFER_TOO_SMALL;
                        }

                        PciDevices[noOfDevicesFound] = &pDevice->PciDevice;
                    }
                    noOfDevicesFound = noOfDevicesFound + 1;
                }
            }
        }
    }

    *NumberOfDevices = noOfDevicesFound;

    return (0 != noOfDevicesFound) ? STATUS_SUCCESS : STATUS_ELEMENT_NOT_FOUND;
}

STATUS
PciSystemReadConfigurationSpaceGeneric(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    IN      BYTE                    Width,
    OUT     QWORD*                  Value
    )
{
    DWORD value;
    STATUS status;

    if (!IsAddressAligned(Register, Width / BITS_PER_BYTE))
    {
        return STATUS_INVALID_PARAMETER3;
    }

    if (NULL == Value)
    {
        return STATUS_INVALID_PARAMETER4;
    }

    LOG_FUNC_START;

    LOG_TRACE_PCI("(%u.%u.%u) Register: 0x%x\n", DeviceLocation.Bus,
                   DeviceLocation.Device, DeviceLocation.Function, Register);

    status = _PciSystemReadConfigurationSpace(DeviceLocation,
                                             AlignAddressLower(Register, sizeof(DWORD)),
                                             &value
                                             );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemReadConfigurationSpace", status);
        return AE_ERROR;
    }

    LOG_TRACE_PCI("Before shift 0x%x\n", value);
    value = (value >> (AddressOffset(Register, sizeof(DWORD)) * 8));
    LOG_TRACE_PCI("After shift 0x%x\n", value);

    switch (Width)
    {
    case BITS_FOR_STRUCTURE(BYTE):
        *Value = value & MAX_BYTE;
        break;
    case BITS_FOR_STRUCTURE(WORD):
        *Value = value & MAX_WORD;
        break;
    case BITS_FOR_STRUCTURE(DWORD):
        *Value = value & MAX_DWORD;
        break;
    case BITS_FOR_STRUCTURE(QWORD):
        {
            DWORD valueHigh;

            status = _PciSystemReadConfigurationSpace(DeviceLocation,
                                                     (WORD)Register + 4,
                                                     &valueHigh
                                                     );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("PciSystemReadConfigurationSpace", status);
                return AE_ERROR;
            }
            *Value = DWORDS_TO_QWORD(valueHigh, value);
        }
        break;
    default:
        return AE_BAD_PARAMETER;
    }

    LOG_TRACE_PCI("Placed value: 0x%X [%u]\n", *Value, Width);

    LOG_FUNC_END;


    return AE_OK;
}

STATUS
PciSystemWriteConfigurationSpaceGeneric(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    IN      BYTE                    Width,
    IN      QWORD                   Value
    )
{
    STATUS status;
    DWORD initialValue;
    DWORD alignedRegister;
    BYTE offsetInRegisterBits;
    DWORD valueToWrite;

    if (!IsAddressAligned(Register, Width / BITS_PER_BYTE))
    {
        return STATUS_INVALID_PARAMETER3;
    }

    initialValue = 0;
    alignedRegister = AlignAddressLower(Register, sizeof(DWORD));
    ASSERT(alignedRegister <= MAX_WORD);

    offsetInRegisterBits = (BYTE)((Register - alignedRegister) * BITS_PER_BYTE);
    valueToWrite = 0;
    status = STATUS_SUCCESS;

    LOG_FUNC_START;

    LOG_TRACE_PCI("(%u.%u.%u) Register: 0x%x\n", DeviceLocation.Bus,
                 DeviceLocation.Device, DeviceLocation.Function, Register);
    LOG_TRACE_PCI("Value to write: 0x%X [%u]\n", Value, Width);

    // for DWORD and QWORD accesses there is no need to read previous value
    if (Width < BITS_FOR_STRUCTURE(DWORD))
    {
        status = _PciSystemReadConfigurationSpace(DeviceLocation,
                                                 (WORD)alignedRegister,
                                                 &initialValue
                                                 );
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("PciSystemReadConfigurationSpace", status);
            return AE_ERROR;
        }
    }



    switch (Width)
    {
    case BITS_FOR_STRUCTURE(BYTE):
        valueToWrite = (initialValue & ~(MAX_BYTE << offsetInRegisterBits)) | ((Value & MAX_BYTE) << offsetInRegisterBits);
        break;
    case BITS_FOR_STRUCTURE(WORD):
        valueToWrite = (initialValue & ~(MAX_WORD << offsetInRegisterBits)) | ((Value & MAX_WORD) << offsetInRegisterBits);
        break;
    case BITS_FOR_STRUCTURE(DWORD):
    case BITS_FOR_STRUCTURE(QWORD):
        valueToWrite = QWORD_LOW(Value);
        break;
    }

    LOG_TRACE_PCI("Initial value was: 0x%x, Value to write is: 0x%x\n", initialValue, valueToWrite);

    status = _PciSystemWriteConfigurationSpace(DeviceLocation,
                                              (WORD)alignedRegister,
                                              (DWORD)valueToWrite
                                              );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemWriteConfigurationSpace", status);
        return AE_ERROR;
    }

    if (Width == BITS_FOR_STRUCTURE(QWORD))
    {
        LOG_TRACE_PCI("Will write to upper part 0x%x!\n", QWORD_HIGH(Value));
        status = _PciSystemWriteConfigurationSpace(DeviceLocation,
                                                 (WORD)alignedRegister + sizeof(DWORD),
                                                  QWORD_HIGH(Value)
                                                 );
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("PciSystemWriteConfigurationSpace", status);
            return AE_ERROR;
        }
    }

    LOG_FUNC_END;

    return AE_OK;

}

STATUS
_PciSystemReadConfigurationSpace(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    OUT     DWORD*                  Value
    )
{
    if( NULL == Value )
    {
        return STATUS_INVALID_PARAMETER3;
    }

    ASSERT( IsAddressAligned(Register, sizeof(DWORD)));

    if( m_pciSystemData.PciExpressSupport )
    {
        if( Register >= PREDEFINED_PCI_EXPRESS_DEVICE_SPACE_SIZE )
        {
            LOG_ERROR("PCI express device configuration space is 4096 bytes!\n");
            return STATUS_DEVICE_SPACE_RANGE_EXCEEDED;
        }

        PPCI_ROOT_COMPLEX pPciRootComplex = _PciSystemFindRootComplexForDevice(DeviceLocation);
        if( NULL == pPciRootComplex )
        {
            LOG_ERROR("Cannot find PCI root complex for device (%u.%u.%u)\n",
                      DeviceLocation.Bus, DeviceLocation.Device, DeviceLocation.Function );
            return STATUS_DEVICE_NOT_CONNECTED;
        }
        LOG_TRACE_PCI("Pci device (%u.%u.%u) belongs to root complex [%u,%u]\n",
                     DeviceLocation.Bus, DeviceLocation.Device, DeviceLocation.Function,
                     pPciRootComplex->StartBusNumber, pPciRootComplex->EndBusNumber );

        *Value = PciExpressReadConfigurationSpace(pPciRootComplex,DeviceLocation,Register);
    }
    else
    {
        if( Register >= PREDEFINED_PCI_DEVICE_SPACE_SIZE )
        {
            LOG_ERROR("Legacy PCI device configuration space is 256 bytes!\n");
            return STATUS_DEVICE_SPACE_RANGE_EXCEEDED;
        }

        *Value = PciReadConfigurationSpace(DeviceLocation, (BYTE) Register);
    }

    return STATUS_SUCCESS;
}

STATUS
_PciSystemWriteConfigurationSpace(
    IN      PCI_DEVICE_LOCATION     DeviceLocation,
    IN      WORD                    Register,
    IN      DWORD                   Value
    )
{
    ASSERT(IsAddressAligned(Register, sizeof(DWORD)));

    if (m_pciSystemData.PciExpressSupport)
    {
        if (Register >= PREDEFINED_PCI_EXPRESS_DEVICE_SPACE_SIZE)
        {
            LOG_ERROR("PCI express device configuration space is 4096 bytes!\n");
            return STATUS_DEVICE_SPACE_RANGE_EXCEEDED;
        }

        PPCI_ROOT_COMPLEX pPciRootComplex = _PciSystemFindRootComplexForDevice(DeviceLocation);
        if (NULL == pPciRootComplex)
        {
            LOG_ERROR("Cannot find PCI root complex for device (%u.%u.%u)\n",
                      DeviceLocation.Bus, DeviceLocation.Device, DeviceLocation.Function);
            return STATUS_DEVICE_NOT_CONNECTED;
        }
        LOG_TRACE_PCI("Pci device (%u.%u.%u) belongs to root complex [%u,%u]\n",
                     DeviceLocation.Bus, DeviceLocation.Device, DeviceLocation.Function,
                     pPciRootComplex->StartBusNumber, pPciRootComplex->EndBusNumber);

        PciExpressWriteConfigurationSpace(pPciRootComplex, DeviceLocation, Register, Value);
    }
    else
    {
        if (Register >= PREDEFINED_PCI_DEVICE_SPACE_SIZE)
        {
            LOG_ERROR("Legacy PCI device configuration space is 256 bytes!\n");
            return STATUS_DEVICE_SPACE_RANGE_EXCEEDED;
        }

        PciWriteConfigurationSpace(DeviceLocation, (BYTE)Register, Value);
    }

    return STATUS_SUCCESS;
}

static
STATUS
_PciSystemInitRootComplexEntry(
    IN          ACPI_MCFG_ALLOCATION*       AcpiEntry,
    OUT_PTR     PPCI_ROOT_COMPLEX_ENTRY*    RootComplexEntry
    )
{
    PCI_ROOT_COMPLEX_ENTRY* pRootComplexEntry;
    DWORD bytesToMap;

    ASSERT(NULL != AcpiEntry);
    ASSERT(NULL != RootComplexEntry);
    ASSERT(AcpiEntry->PciSegment == 0);

    pRootComplexEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PCI_ROOT_COMPLEX_ENTRY), HEAP_PCI_TAG, 0 );
    if (NULL == pRootComplexEntry)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(PCI_ROOT_COMPLEX_ENTRY));
        return STATUS_HEAP_INSUFFICIENT_RESOURCES;
    }

    pRootComplexEntry->RootComplex.StartBusNumber = AcpiEntry->StartBusNumber;
    pRootComplexEntry->RootComplex.EndBusNumber = AcpiEntry->EndBusNumber;

    bytesToMap = ( pRootComplexEntry->RootComplex.EndBusNumber - pRootComplexEntry->RootComplex.StartBusNumber + 1 ) * MB_SIZE;

    LOGL("Will map %u bytes at PA 0x%X\n", bytesToMap, AcpiEntry->Address );
    pRootComplexEntry->RootComplex.BaseAddress = IoMapMemory((PHYSICAL_ADDRESS) AcpiEntry->Address,
                                                             bytesToMap,
                                                             PAGE_RIGHTS_READWRITE
                                                             );
    if (NULL == pRootComplexEntry->RootComplex.BaseAddress)
    {
        LOG_ERROR("IoMapMemory failed for mapping 0x%x bytes at address 0x%X\n", bytesToMap, AcpiEntry->Address );
        return STATUS_MEMORY_CANNOT_BE_MAPPED;
    }

    *RootComplexEntry = pRootComplexEntry;
                                            
    return STATUS_SUCCESS;
}

static
STATUS
_PciSystemRetrievePciExpressDevices(
    INOUT   PLIST_ENTRY     PciDeviceList,
    OUT     PDWORD          NumberOfDevices
    )
{
    STATUS status;
    PLIST_ENTRY pEntry;
    DWORD noOfDevicesRetrieved;

    ASSERT( NULL != PciDeviceList );
    ASSERT( NULL != NumberOfDevices);

    status = STATUS_SUCCESS;
    noOfDevicesRetrieved = 0;

    for (pEntry = m_pciSystemData.PciRootComplexList.Flink;
         pEntry != &m_pciSystemData.PciRootComplexList;
         pEntry = pEntry->Flink)
    {
        PCI_ROOT_COMPLEX_ENTRY* pRootComplex = CONTAINING_RECORD(pEntry, PCI_ROOT_COMPLEX_ENTRY, ListEntry);
        BOOLEAN bRestartSearch = TRUE;
        PCI_DEVICE_LIST_ENTRY* pPciDeviceEntry = NULL;

#pragma warning(suppress:4127)
        while (TRUE)
        {
            ASSERT(NULL == pPciDeviceEntry);

            pPciDeviceEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PCI_DEVICE_LIST_ENTRY), HEAP_PCI_TAG, 0);
            if (NULL == pPciDeviceEntry)
            {
                LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(PCI_DEVICE_LIST_ENTRY));
                return STATUS_HEAP_INSUFFICIENT_RESOURCES;
            }

                status = PciExpressRetrieveNextDevice(&pRootComplex->RootComplex, bRestartSearch, &pPciDeviceEntry->PciDevice);
                if (STATUS_DEVICE_NO_MORE_DEVICES == status)
                {
                    // exhausted PCI device space
                    status = STATUS_SUCCESS;
                    break;
                }

                if (!SUCCEEDED(status))
                {
                    // we failed, why?
                    LOG_FUNC_ERROR("PciExpressRetrieveNextDevice", status);
                    break;
                }


            // if we're here we succeeded => we can add entry to linked list
            InsertTailList(PciDeviceList, &pPciDeviceEntry->ListEntry);
            noOfDevicesRetrieved = noOfDevicesRetrieved + 1;
            if (LogIsComponentTraced(LogComponentPci))
            {
                DumpPciDevice(&pPciDeviceEntry->PciDevice);
            }

            // set to NULL so we won't free it at the end
            pPciDeviceEntry = NULL;

            bRestartSearch = FALSE;
        }

        if (NULL != pPciDeviceEntry)
        {
            ExFreePoolWithTag(pPciDeviceEntry, HEAP_PCI_TAG);
            pPciDeviceEntry = NULL;
        }
    }

    *NumberOfDevices = noOfDevicesRetrieved;

    return status;
}

static
STATUS
_PciSystemRetrievePciDevices(
    INOUT   PLIST_ENTRY     PciDeviceList,
    OUT     PDWORD          NumberOfDevices
    )
{
    STATUS status;
    DWORD noOfDevicesRetrieved;
    PPCI_DEVICE_LIST_ENTRY pPciDeviceEntry;
    BOOLEAN bRestartSearch;

    ASSERT(NULL != PciDeviceList);
    ASSERT(NULL != NumberOfDevices);

    noOfDevicesRetrieved = 0;
    pPciDeviceEntry = NULL;
    bRestartSearch = TRUE;
    status = STATUS_SUCCESS;

#pragma warning(suppress:4127)
    while (TRUE)
    {
        ASSERT(NULL == pPciDeviceEntry);

        pPciDeviceEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PCI_DEVICE_LIST_ENTRY) + PREDEFINED_PCI_DEVICE_SPACE_SIZE, HEAP_PCI_TAG, 0);
        if (NULL == pPciDeviceEntry)
        {
            LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(PCI_DEVICE_LIST_ENTRY));
            return STATUS_HEAP_INSUFFICIENT_RESOURCES;
        }

        pPciDeviceEntry->PciDevice.DeviceData = (PPCI_DEVICE) ( (PBYTE) pPciDeviceEntry + sizeof(PCI_DEVICE_LIST_ENTRY) );
        status = PciRetrieveNextDevice(bRestartSearch, PREDEFINED_PCI_DEVICE_SPACE_SIZE, &pPciDeviceEntry->PciDevice);
        if (STATUS_DEVICE_NO_MORE_DEVICES == status)
        {
            // exhausted PCI device space
            status = STATUS_SUCCESS;
            break;
        }

        if (!SUCCEEDED(status))
        {
            // we failed, why?
            LOG_FUNC_ERROR("PciRetrieveNextDevice", status);
            break;
        }

        // if we're here we succeeded => we can add entry to linked list
        InsertTailList(PciDeviceList, &pPciDeviceEntry->ListEntry);
        noOfDevicesRetrieved = noOfDevicesRetrieved + 1;
        if (LogIsComponentTraced(LogComponentPci))
        {
            DumpPciDevice(&pPciDeviceEntry->PciDevice);
        }

        // set to NULL so we won't free it at the end
        pPciDeviceEntry = NULL;

        bRestartSearch = FALSE;
    }



    if (NULL != pPciDeviceEntry)
    {
        ExFreePoolWithTag(pPciDeviceEntry, HEAP_PCI_TAG);
        pPciDeviceEntry = NULL;
    }

    *NumberOfDevices = noOfDevicesRetrieved;
    
    return status;
}

static
STATUS
_PciSystemRetrieveParentForDevice(
    IN      PLIST_ENTRY                 BridgeList,
    IN      BYTE                        Bus,
    OUT     PPCI_DEVICE_DESCRIPTION*    ParentDevice
    )
{
    ASSERT(NULL != BridgeList);
    ASSERT(0 != Bus);
    ASSERT(NULL != ParentDevice);

    for(PLIST_ENTRY pEntry = BridgeList->Flink;
        pEntry != BridgeList;
        pEntry = pEntry->Flink)
    {
        PPCI_DEVICE_LIST_ENTRY pDevice = CONTAINING_RECORD(pEntry, PCI_DEVICE_LIST_ENTRY, BridgeEntry);

        ASSERT(pDevice->PciDevice.DeviceData->Header.HeaderType.Layout == PCI_HEADER_LAYOUT_PCI_TO_PCI);

        if( pDevice->PciDevice.DeviceData->Header.Bridge.SecondaryBusNumber == Bus)
        {
            *ParentDevice = &pDevice->PciDevice;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_ELEMENT_NOT_FOUND;
}