#include "HAL9000.h"
#include "pci_system.h"
#include "ioapic_system.h"
#include "acpi_interface.h"
#include "io.h"
#include "ioapic.h"
#include "bitmap.h"
#include "lapic_system.h"
#include "synch.h"

typedef struct _IO_APIC_INTERRUPT_OVERRIDE
{
    BYTE                SourceIrq;
    BYTE                DestinationIrq;

    APIC_TRIGGER_MODE   TriggerMode;
    APIC_PIN_POLARITY   PinPolarity;
} IO_APIC_INTERRUPT_OVERRIDE, *PIO_APIC_INTERRUPT_OVERRIDE;

typedef struct _IO_APIC_PRT_ENTRY
{
    PCI_DEVICE_LOCATION DeviceLocation;

    DWORD               IoApicLine;
    BYTE                InterruptPin;
} IO_APIC_PRT_ENTRY, *PIO_APIC_PRT_ENTRY;

typedef struct _IO_APIC_ENTRY
{
    // Pseudo RO - initialized once, should never be modified again
    APIC_ID             ApicId;
    BYTE                Version;
    BYTE                IrqBase;
    BYTE                MaximumRedirectionEntry;

    LOCK                IoApicLock;

    // Access to the IOAPIC memory and to the software managed interrupts written bitmap
    // must be protected by a lock, we can't use a shared one because even reads from the
    // IOAPIC imply writes to some mapped registers

    _Guarded_by_(IoApicLock)
    PVOID               MappedAddress;

    _Guarded_by_(IoApicLock)
    BITMAP              InterruptsWritten;
    DWORD               BitmapBuffer;

    LIST_ENTRY          ListEntry;
} IO_APIC_ENTRY, *PIO_APIC_ENTRY;

typedef struct _IO_APIC_SYSTEM_DATA
{
    LIST_ENTRY                      ApicList;

    BOOLEAN                         MaskInterrupts;

    BYTE                            Version;

    DWORD                           NoOfInterruptOverrides;
    PIO_APIC_INTERRUPT_OVERRIDE     InterruptOverrides;

    DWORD                           NoOfPrtEntries;
    PIO_APIC_PRT_ENTRY              PrtEntries;
} IO_APIC_SYSTEM_DATA, *PIO_APIC_SYSTEM_DATA;

static IO_APIC_SYSTEM_DATA m_ioApicData;

__forceinline
static
_Ret_maybenull_
PIO_APIC_INTERRUPT_OVERRIDE
_IoApicReturnInterruptOverrideForIrq(
    IN      BYTE        Irq
    )
{
    PIO_APIC_INTERRUPT_OVERRIDE pResult = NULL;
    DWORD i;

    for( i = 0; i < m_ioApicData.NoOfInterruptOverrides; ++i )
    {
        ASSERT( NULL != m_ioApicData.InterruptOverrides );

        if( m_ioApicData.InterruptOverrides[i].SourceIrq == Irq )
        {
            pResult = &m_ioApicData.InterruptOverrides[i];
        }
    }

    if( NULL != pResult )
    {
        LOG_TRACE_INTERRUPT("Found interrupt override IRQ 0x%x -> 0x%x, Trigger mode: [%s], Pin Polarity: [%s]\n",
                            Irq, pResult->DestinationIrq,
                            pResult->TriggerMode == ApicTriggerModeEdge ? "Edge" : "Level",
                            pResult->PinPolarity == ApicPinPolarityActiveHigh ? "High" : "Low"
                            );
    }
    else
    {
        LOG_TRACE_INTERRUPT("No interrupt override for IRQ 0x%x\n", Irq );
    }

    return pResult;
}

static
STATUS
_IoApicSystemRetrieveIoApics(
    void
    );

static
STATUS
_IoApicSystemRetrieveInterruptOverrides(
    void
    );

static
STATUS
_IoApicSystemRetrievePrtEntries(
    void
    );

static
STATUS
_IoApicSystemInitEntry(
    IN          ACPI_MADT_IO_APIC*     AcpiEntry,
    OUT_PTR     PIO_APIC_ENTRY*        IoApicEntry
    );

static
PTR_SUCCESS
PIO_APIC_ENTRY
_IoApicSystemGetIoApicForIrq(
    IN          BYTE                            Irq,
    OUT         PBYTE                           RelativeIrq,
    OUT_OPT_PTR_MAYBE_NULL
                PIO_APIC_INTERRUPT_OVERRIDE*    InterruptOverride
    );

_No_competing_thread_
void
IoApicSystemPreinit(
    void
    )
{
    memzero(&m_ioApicData, sizeof(IO_APIC_SYSTEM_DATA));

    InitializeListHead(&m_ioApicData.ApicList);
    m_ioApicData.MaskInterrupts = TRUE;
}

_No_competing_thread_
STATUS
IoApicSystemInit(
    void
    )
{
    STATUS status;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;

    status = _IoApicSystemRetrieveIoApics();
    if(!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IoApicSystemRetrieveIoApics", status);
        return status;
    }
    LOGL("IO APICs retrieved successfully\n");

    status = _IoApicSystemRetrieveInterruptOverrides();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IoApicSystemRetrieveInterruptOverrides", status);
        return status;
    }
    LOGL("IO APIC interrupt overrides retrieved successfully\n");

    LOG_FUNC_END;

    return status;
}

_No_competing_thread_
STATUS
IoApicLateSystemInit(
    void
    )
{
    STATUS status;

    LOG_FUNC_START;

    status = _IoApicSystemRetrievePrtEntries();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IoApicSystemRetrievePrtEntries", status);
        return status;
    }
    LOGL("PRT entries retrieved successfully\n");

    LOG_FUNC_END;

    return status;
}

_No_competing_thread_
void
IoApicSystemEnableRegisteredInterrupts(
    void
    )
{
    PLIST_ENTRY pEntry;
    PIO_APIC_ENTRY pIoApic;

    m_ioApicData.MaskInterrupts = FALSE;


    for (pEntry = m_ioApicData.ApicList.Flink;
        pEntry != &m_ioApicData.ApicList;
        pEntry = pEntry->Flink
        )
    {
        pIoApic = CONTAINING_RECORD(pEntry, IO_APIC_ENTRY, ListEntry);

        for (BYTE i = 0; i <= pIoApic->MaximumRedirectionEntry; ++i)
        {
            // if there is an interrupt registered => unmask it
            if (BitmapGetBitValue(&pIoApic->InterruptsWritten, i))
            {
                LOG_TRACE_INTERRUPT("Will unmask entry %u on IO APIC 0x%02x\n", i, pIoApic->ApicId );
                IoApicSetRedirectionTableEntryMask(pIoApic->MappedAddress, i, FALSE);
            }
        }
    }
}

STATUS
IoApicSystemSetInterrupt(
    IN      BYTE                    Irq,
    IN      BYTE                    Vector,
    IN _Strict_type_match_
            APIC_DESTINATION_MODE   DestinationMode,
    IN _Strict_type_match_
            APIC_DELIVERY_MODE      DeliveryMode,
    IN _Strict_type_match_
            APIC_PIN_POLARITY       PinPolarity,
    IN  _Strict_type_match_
            APIC_TRIGGER_MODE       TriggerMode,
    IN      APIC_ID                 Destination,
    IN      BOOLEAN                 Overwrite
    )
{
    PIO_APIC_ENTRY pIoApic;
    BYTE offsetInIoApic;
    PIO_APIC_INTERRUPT_OVERRIDE pInterruptOverride;
    APIC_PIN_POLARITY pinPolarity;
    APIC_TRIGGER_MODE triggerMode;
    STATUS status;
    INTR_STATE oldState;

    LOG_FUNC_START;

    pIoApic = NULL;
    offsetInIoApic = 0;
    pInterruptOverride = NULL;
    status = STATUS_SUCCESS;

    pIoApic = _IoApicSystemGetIoApicForIrq(Irq, &offsetInIoApic, &pInterruptOverride);
    if (NULL == pIoApic)
    {
        LOG_ERROR("Couldn't find IO Apic for IRQ 0x%x\n", Irq);
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }
    ASSERT(NULL != pIoApic);

    LockAcquire(&pIoApic->IoApicLock, &oldState);

    __try
    {
        pinPolarity = pInterruptOverride != NULL ? pInterruptOverride->PinPolarity : PinPolarity;
        triggerMode = pInterruptOverride != NULL ? pInterruptOverride->TriggerMode : TriggerMode;

        LOG_TRACE_INTERRUPT("Irq 0x%x belongs to IO APIC 0x%02x\n", Irq, pIoApic->ApicId);

        if (BitmapGetBitValue(&pIoApic->InterruptsWritten, offsetInIoApic) && !Overwrite)
        {
            LOG_ERROR("Interrupt 0x%x is already taken in IO Apic 0x%x\n", offsetInIoApic, pIoApic->ApicId);
            status = STATUS_DEVICE_INTERRUPT_NOT_AVAILABLE;
            __leave;
        }

        ASSERT(TRUE == LapicSystemGetState());
        IoApicSetRedirectionTableEntry(pIoApic->MappedAddress,
                                       offsetInIoApic,
                                       Vector,
                                       DestinationMode,
                                       Destination,
                                       DeliveryMode,
                                       pinPolarity,
                                       8 != Irq ? triggerMode : ApicTriggerModeLevel
        );
        BitmapSetBit(&pIoApic->InterruptsWritten, offsetInIoApic);

        if (!m_ioApicData.MaskInterrupts)
        {
            IoApicSetRedirectionTableEntryMask(pIoApic->MappedAddress, offsetInIoApic, FALSE);
        }
    }
    __finally
    {
        LockRelease(&pIoApic->IoApicLock, oldState);

        LOG_FUNC_END;
    }

    return status;
}

void
IoApicSystemMaskInterrupt(
    IN      BYTE                    Irq,
    IN      BOOLEAN                 Mask
    )
{
    PIO_APIC_ENTRY pIoApic;
    BYTE offsetInIoApic;
    INTR_STATE oldState;

    LOG_FUNC_START;

    pIoApic = NULL;
    offsetInIoApic = 0;

    pIoApic = _IoApicSystemGetIoApicForIrq(Irq, &offsetInIoApic, NULL);
    ASSERT(NULL != pIoApic);

    LockAcquire(&pIoApic->IoApicLock, &oldState);
    IoApicSetRedirectionTableEntryMask(pIoApic->MappedAddress, offsetInIoApic, Mask);
    LockRelease(&pIoApic->IoApicLock, oldState);

    LOG_FUNC_END;
}

STATUS
IoApicSystemGetVectorForIrq(
    IN      BYTE                    Irq,
    OUT     PBYTE                   Vector
    )
{
    PIO_APIC_ENTRY pIoApic;
    BYTE offsetInIoApic;
    INTR_STATE oldState;
    STATUS status;

    ASSERT(Vector != NULL);

    status = STATUS_SUCCESS;

    pIoApic = _IoApicSystemGetIoApicForIrq(Irq, &offsetInIoApic, NULL);
    if (NULL == pIoApic)
    {
        LOG_ERROR("Couldn't find IO Apic for IRQ 0x%x\n", Irq);
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }
    ASSERT(NULL != pIoApic);

    LockAcquire(&pIoApic->IoApicLock, &oldState);

    __try
    {
        if (0 == BitmapGetBitValue(&pIoApic->InterruptsWritten, offsetInIoApic))
        {
            LOG_WARNING("Interrupt 0x%x is not registered in IO Apic 0x%x\n", offsetInIoApic, pIoApic->ApicId);
            status = STATUS_DEVICE_INTERRUPT_NOT_CONFIGURED;
            __leave;
        }

        *Vector = IoApicGetRedirectionTableEntryVector(pIoApic->MappedAddress, offsetInIoApic);
    }
    __finally
    {
        LockRelease(&pIoApic->IoApicLock, oldState);
    }

    return status;
}

DWORD
IoApicGetInterruptLineForPciDevice(
    IN      struct _PCI_DEVICE_DESCRIPTION* PciDevice
    )
{
    BOOLEAN bFound;
    DWORD irqLine;

    ASSERT( NULL != PciDevice );

    LOG_FUNC_START;

    bFound = FALSE;
    irqLine = MAX_DWORD;

    for( DWORD i = 0; i < m_ioApicData.NoOfPrtEntries; ++i )
    {
        PIO_APIC_PRT_ENTRY pEntry = &m_ioApicData.PrtEntries[i];

        if( pEntry->DeviceLocation.Bus != PciDevice->DeviceLocation.Bus )
        {
            continue;
        }

        if( pEntry->DeviceLocation.Device != PciDevice->DeviceLocation.Device )
        {
            continue;
        }

        if( pEntry->DeviceLocation.Function != MAX_BYTE && pEntry->DeviceLocation.Function != PciDevice->DeviceLocation.Function )
        {
            continue;
        }

        if( pEntry->InterruptPin != PciDevice->DeviceData->Header.Device.InterruptPin )
        {
            continue;
        }

        bFound = TRUE;
        irqLine = pEntry->IoApicLine;
        break;
    }

    if(!bFound)
    {
        // If we didn't find an entry for the device we need to see if it is behind a secondary bridge
        // If it is => we need to translate the PIN index of the device connected to the bridge, i.e
        // on the secondary bus to the pin index of the bridge on the primary bus
        // See PCI Bridge Specification V 1.1 Section 9.1 Interrupt Routing
        BYTE accumulatedOffset = PciDevice->DeviceData->Header.Device.InterruptPin;

        for( PPCI_DEVICE_DESCRIPTION pParentDescription = PciDevice->Parent;
             pParentDescription != NULL;
             pParentDescription = pParentDescription->Parent )
        {
            if(pParentDescription->Parent != NULL)
            {
                /// TODO: Make sure translation for secondary bus to primary bus is actually correct
                accumulatedOffset = ( accumulatedOffset + pParentDescription->DeviceLocation.Function ) % 4;
            }
        }

        irqLine = 0x10 + accumulatedOffset;

        LOGL("Accumulated offset 0x%x, child line 0x%02x\n",
             accumulatedOffset, irqLine );
    }

    LOGL("PCI device at (%u.%u.%u) will use IO APIC line 0x%02x for PIN %c, %s entry in PRT\n",
         PciDevice->DeviceLocation.Bus, PciDevice->DeviceLocation.Device, PciDevice->DeviceLocation.Function,
         irqLine, 'A' + PciDevice->DeviceData->Header.Device.InterruptPin - 1,
         bFound ? "use" : "no");

    LOG_FUNC_END;

    return irqLine;
}

void
IoApicSystemSendEOI(
    IN      BYTE                Vector
    )
{
    UNREFERENCED_PARAMETER(Vector);

    if (m_ioApicData.Version >= IO_APIC_VERSION_PCI_22_COMPLIANT)
    {
        LOG_WARNING("This functionality is not implemented\n");
    }
}

static
STATUS
_IoApicSystemRetrieveIoApics(
    void
    )
{
    STATUS status;
    ACPI_MADT_IO_APIC* pEntry;
    BOOLEAN bRestartSearch;
    PIO_APIC_ENTRY pIoApic;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pEntry = NULL;
    bRestartSearch = TRUE;
    pIoApic = NULL;

#pragma warning(suppress:4127)
    while (TRUE)
    {
        ASSERT(NULL == pIoApic);

        status = AcpiRetrieveNextIoApic(bRestartSearch, &pEntry);
        if (STATUS_NO_MORE_OBJECTS == status)
        {
            LOGL("Reached end of IO Apic list\n");
            status = STATUS_SUCCESS;
            break;
        }

        status = _IoApicSystemInitEntry(pEntry, &pIoApic);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("_IoApicInitEntry", status );
            return status;
        }

        InsertTailList(&m_ioApicData.ApicList, &pIoApic->ListEntry);

        if (bRestartSearch)
        {
            // first iteration
            m_ioApicData.Version = pIoApic->Version;
        }

        // make sure we don't have 2+ IO Apics with different versions
        ASSERT(pIoApic->Version == m_ioApicData.Version);

        bRestartSearch = FALSE;
        pIoApic = NULL;
    }

    return status;
}

static
STATUS
_IoApicSystemRetrieveInterruptOverrides(
    void
    )
{
    STATUS status;
    ACPI_MADT_INTERRUPT_OVERRIDE* pEntry;
    BOOLEAN bRestartSearch;
    PIO_APIC_INTERRUPT_OVERRIDE pInterruptOverride;
    DWORD noOfInterruptOverrides;
    DWORD i;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pInterruptOverride = NULL;
    noOfInterruptOverrides = 0;
    pEntry = NULL;

    for (i = 0; i < 2; ++i)
    {
        bRestartSearch = TRUE;
        m_ioApicData.NoOfInterruptOverrides = noOfInterruptOverrides;

        if( noOfInterruptOverrides == 0 )
        {
            if (i == 1)
            {
                // there are no interrupt overrides
                break;
            }
        }

        if( noOfInterruptOverrides != 0 )
        {
            pInterruptOverride = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(IO_APIC_INTERRUPT_OVERRIDE) * noOfInterruptOverrides, HEAP_APIC_TAG, 0 );
            if( NULL == pInterruptOverride )
            {
                LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(IO_APIC_INTERRUPT_OVERRIDE) * noOfInterruptOverrides);
                return STATUS_HEAP_INSUFFICIENT_RESOURCES;
            }
        }

        noOfInterruptOverrides = 0;

#pragma warning(suppress:4127)
        while (TRUE)
        {
            status = AcpiRetrieveNextInterruptOverride(bRestartSearch, &pEntry);
            if (STATUS_NO_MORE_OBJECTS == status)
            {
                LOGL("Reached end of Interrupt override list\n");
                status = STATUS_SUCCESS;
                break;
            }

            if( NULL != pInterruptOverride )
            {
                // init structure
                pInterruptOverride[noOfInterruptOverrides].SourceIrq = pEntry->SourceIrq;

                ASSERT( pEntry->GlobalIrq <= MAX_BYTE );
                pInterruptOverride[noOfInterruptOverrides].DestinationIrq = (BYTE) pEntry->GlobalIrq;
                pInterruptOverride[noOfInterruptOverrides].PinPolarity = IsBooleanFlagOn(pEntry->IntiFlags, 0b10);
                pInterruptOverride[noOfInterruptOverrides].TriggerMode = IsBooleanFlagOn(pEntry->IntiFlags, 0b1000 );

                ASSERT_INFO( 0 == pEntry->Bus, "Only 0 (ISA Bus) is documented in the ACPI specfication\n" );
            }

            noOfInterruptOverrides++;

            bRestartSearch = FALSE;
        }

        if( i == 1 )
        {
            ASSERT( noOfInterruptOverrides == m_ioApicData.NoOfInterruptOverrides );
        }
    }

    m_ioApicData.NoOfInterruptOverrides = noOfInterruptOverrides;
    m_ioApicData.InterruptOverrides = pInterruptOverride;

    return status;
}


static
STATUS
_IoApicSystemRetrievePrtEntries(
    void
    )
{
    STATUS status;
    ACPI_PCI_ROUTING_TABLE* pEntry;
    BOOLEAN bRestartSearch;
    PIO_APIC_PRT_ENTRY pPrtEntires;
    DWORD noOfPrtEntries;
    DWORD i;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pPrtEntires = NULL;
    noOfPrtEntries = 0;
    pEntry = NULL;

    for (i = 0; i < 2; ++i)
    {
        bRestartSearch = TRUE;
        m_ioApicData.NoOfInterruptOverrides = noOfPrtEntries;

        if (noOfPrtEntries == 0)
        {
            if (i == 1)
            {
                // there are no PRT entries
                break;
            }
        }

        if (noOfPrtEntries != 0)
        {
            pPrtEntires = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(IO_APIC_PRT_ENTRY) * noOfPrtEntries, HEAP_APIC_TAG, 0);
            if (NULL == pPrtEntires)
            {
                LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(IO_APIC_PRT_ENTRY) * noOfPrtEntries);
                return STATUS_HEAP_INSUFFICIENT_RESOURCES;
            }
        }

        noOfPrtEntries = 0;

#pragma warning(suppress:4127)
        while (TRUE)
        {
            BYTE busNumber;
            WORD segmentNumber;

            status = AcpiRetrieveNextPrtEntry(bRestartSearch, &pEntry, &busNumber, &segmentNumber);
            if (STATUS_NO_MORE_OBJECTS == status)
            {
                LOGL("Reached end of PRT list\n");
                status = STATUS_SUCCESS;
                break;
            }

            ASSERT( 0 == segmentNumber );

            if (NULL != pPrtEntires)
            {
                // init structure
                pPrtEntires[noOfPrtEntries].DeviceLocation.Bus = busNumber;
                pPrtEntires[noOfPrtEntries].DeviceLocation.Device = (BYTE) (WORD_LOW(DWORD_HIGH(QWORD_LOW(pEntry->Address))));
                pPrtEntires[noOfPrtEntries].DeviceLocation.Function = (BYTE) (pEntry->Address & MAX_BYTE);

                // PCI specification 3.0 Section 6.2.4
                // A value of 1 corresponds to INTA#.A value of 2 corresponds to INTB#.A value of 3
                // corresponds to INTC#.A value of 4 corresponds to INTD#. Devices (or device functions)
                // that do not use an interrupt pin must put a 0 in this register.

                // ACPI specification 6.0 Section 6.2.13
                // The PCI pin number of the device (0–INTA, 1–INTB, 2–INTC, 3–INTD).

                // => we need to add 1 to the PIN number
                pPrtEntires[noOfPrtEntries].InterruptPin = (BYTE) pEntry->Pin + 1;
                pPrtEntires[noOfPrtEntries].IoApicLine = pEntry->SourceIndex;

                ASSERT( 0 == *((DWORD*)pEntry->Source));

                LOG_TRACE_PCI("[%u] PRT entry for device (%u.%u.%u) with interrupt pin %c at IO APIC line 0x%02x\n",
                    noOfPrtEntries,
                    pPrtEntires[noOfPrtEntries].DeviceLocation.Bus,
                    pPrtEntires[noOfPrtEntries].DeviceLocation.Device,
                    pPrtEntires[noOfPrtEntries].DeviceLocation.Function,
                    'A' + pPrtEntires[noOfPrtEntries].InterruptPin - 1,
                    pPrtEntires[noOfPrtEntries].IoApicLine);
            }

            noOfPrtEntries++;

            bRestartSearch = FALSE;
        }

        if (i == 1)
        {
            ASSERT(noOfPrtEntries == m_ioApicData.NoOfInterruptOverrides);
        }
    }

    m_ioApicData.NoOfPrtEntries = noOfPrtEntries;
    m_ioApicData.PrtEntries = pPrtEntires;

    return status;
}

static
STATUS
_IoApicSystemInitEntry(
    IN          ACPI_MADT_IO_APIC*     AcpiEntry,
    OUT_PTR     PIO_APIC_ENTRY*        IoApicEntry
    )
{
    PIO_APIC_ENTRY pIoApic;
    DWORD bitmapSize;
    BYTE i;

    ASSERT( NULL != AcpiEntry );
    ASSERT( NULL != IoApicEntry );

    pIoApic = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(IO_APIC_ENTRY), HEAP_APIC_TAG, 0);
    if (NULL == pIoApic)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(IO_APIC_ENTRY));
        return STATUS_HEAP_INSUFFICIENT_RESOURCES;
    }

    pIoApic->ApicId = AcpiEntry->Id;


    ASSERT(AcpiEntry->GlobalIrqBase <= MAX_BYTE);
    pIoApic->IrqBase = (BYTE) AcpiEntry->GlobalIrqBase;

    // warning C4312: 'type cast': conversion from 'const UINT32' to 'PHYSICAL_ADDRESS' of greater size
#pragma warning(suppress:4312)
    pIoApic->MappedAddress = IoMapMemory((PHYSICAL_ADDRESS)AcpiEntry->Address,
                                         PAGE_SIZE,
                                         PAGE_RIGHTS_READWRITE
                                         );
    if (NULL == pIoApic->MappedAddress)
    {
        LOG_FUNC_ERROR("IoMapMemory failed to map PA 0x%x\n", AcpiEntry->Address);
        return STATUS_MEMORY_CANNOT_BE_MAPPED;
    }
    ASSERT(IoApicGetId( pIoApic->MappedAddress) == pIoApic->ApicId);
    pIoApic->Version = IoApicGetVersion(pIoApic->MappedAddress);

    pIoApic->MaximumRedirectionEntry = IoApicGetMaximumRedirectionEntry(pIoApic->MappedAddress);

    bitmapSize = BitmapPreinit(&pIoApic->InterruptsWritten, pIoApic->MaximumRedirectionEntry + 1 );
    ASSERT( bitmapSize <= sizeof(DWORD));

    BitmapInit(&pIoApic->InterruptsWritten, (PBYTE) &pIoApic->BitmapBuffer );

    for (i = 0; i <= pIoApic->MaximumRedirectionEntry; ++i)
    {
        // mask all IO apic interrupts
        IoApicSetRedirectionTableEntryMask(pIoApic->MappedAddress, i, TRUE );
    }

    LockInit(&pIoApic->IoApicLock);

    *IoApicEntry = pIoApic;

    return STATUS_SUCCESS;
}

static
PTR_SUCCESS
PIO_APIC_ENTRY
_IoApicSystemGetIoApicForIrq(
    IN          BYTE                            Irq,
    OUT         PBYTE                           RelativeIrq,
    OUT_OPT_PTR_MAYBE_NULL
                PIO_APIC_INTERRUPT_OVERRIDE*    InterruptOverride
    )
{
    PLIST_ENTRY pEntry;
    PIO_APIC_ENTRY pIoApic;
    PIO_APIC_INTERRUPT_OVERRIDE pInterruptOverride;
    BYTE finalIrq;

    ASSERT( NULL != RelativeIrq );

    // before searching for the Irq we have to see if we don't have any interrupt
    // overrides for it
    pInterruptOverride = _IoApicReturnInterruptOverrideForIrq(Irq);
    finalIrq = pInterruptOverride != NULL ? pInterruptOverride->DestinationIrq : Irq;
    if( NULL != InterruptOverride )
    {
        *InterruptOverride = pInterruptOverride;
    }

    for (pEntry = m_ioApicData.ApicList.Flink;
    pEntry != &m_ioApicData.ApicList;
        pEntry = pEntry->Flink
        )
    {
        pIoApic = CONTAINING_RECORD(pEntry, IO_APIC_ENTRY, ListEntry);

        if (CHECK_BOUNDS(finalIrq, 1, pIoApic->IrqBase, pIoApic->MaximumRedirectionEntry + 1))
        {
            LOG("Irq 0x%x is handled by IO APIC 0x%x which handles [0x%x, 0x%x]\n",
                finalIrq, pIoApic->ApicId, pIoApic->IrqBase, pIoApic->MaximumRedirectionEntry);

            *RelativeIrq = finalIrq - pIoApic->IrqBase;
            return pIoApic;
        }
    }

    return NULL;
}