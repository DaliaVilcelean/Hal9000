#include "HAL9000.h"
#include "idt_handlers.h"
#include "iomu.h"
#include "pic.h"
#include "rtc.h"
#include "keyboard.h"
#include "pci_system.h"
#include "strutils.h"
#include "disk.h"
#include "volume.h"
#include "ata.h"
#include "filesystem.h"
#include "fat32.h"
#include "swapfs.h"
#include "lapic_system.h"
#include "dmp_io.h"
#include "isr.h"
#include "os_info.h"
#include "eth_82574L.h"
#include "system_driver.h"
#include "ioapic_system.h"
#include "bitmap.h"
#include "pit.h"
#include "smp.h"
#include "ex_system.h"
#include "lock_common.h"

#define PIC_MASTER_OFFSET                   0x20
#define PIC_SLAVE_OFFSET                    0x28

#define SCHEDULER_TIMER_INTERRUPT_TIME_US   (40*MS_IN_US)

#define HAL9000_SYSTEM_FILE_NAME            "HAL9000.ini"

#pragma warning(push)

// warning C4201: nonstandard extension used: nameless struct/union
#pragma warning(disable:4201)
typedef union __declspec(align(8)) _UPTIME
{
    struct
    {
        volatile    DWORD   UptimeSeconds;
        volatile    DWORD   UptimeMicroseconds;
    };
    volatile        QWORD   Raw;
} UPTIME, *PUPTIME;
#pragma warning(pop)

typedef struct _REGISTERED_INTERRUPT_ENTRY
{
    LIST_ENTRY                  ListEntry;

    PFUNC_InterruptFunction     Function;
    PDEVICE_OBJECT              Device;
} REGISTERED_INTERRUPT_ENTRY, *PREGISTERED_INTERRUPT_ENTRY;

typedef struct _REGISTERED_INTERRUPT_LIST
{
    RW_SPINLOCK                 Lock;

    _Guarded_by_(Lock)
    LIST_ENTRY                  List;
} REGISTERED_INTERRUPT_LIST, *PREGISTERED_INTERRUPT_LIST;

typedef struct _IOMU_DATA
{
    QWORD                       TscFrequency;

    LIST_ENTRY                  PciDeviceList;

    LIST_ENTRY                  PciBridgeList;

    LIST_ENTRY                  DriverList;

    LIST_ENTRY                  VpbList;

    UPTIME                      SystemUptime;

    PDEVICE_OBJECT              SystemDevice;

    PFILE_OBJECT                SwapFile;

    DWORD                       TimerInterruptTimeUs;
    DWORD                       TimeUpdatePerCpuUs;
    WORD                        PitInitialTickCount;

    char                        SystemDrive[4];

    LOCK                        GlobalInterruptLock;

    REGISTERED_INTERRUPT_LIST   RegisteredInterrupts[NO_OF_USABLE_INTERRUPTS];

    _Guarded_by_(GlobalInterruptLock)
    BITMAP                      InterruptBitmap;
    BYTE                        BitmapBuffer[NO_OF_TOTAL_INTERRUPTS / BITS_FOR_STRUCTURE(BYTE)];
} IOMU_DATA, *PIOMU_DATA;
STATIC_ASSERT(FIELD_OFFSET(IOMU_DATA, SystemUptime) % sizeof(QWORD) == 0 );

static IOMU_DATA m_iomuData;

#define DRIVER_MAX_NAME         16

typedef struct _DRIVER_DECLARATION
{
    char*                   DriverName;
    PFUNC_DriverEntry       DriverEntry;
    BOOLEAN                 Mandatory;
} DRIVER_DECLARATION, *PDRIVER_DECLARATION;

#define DECLARE_DRIVER(name,entry,mand)    { name ## ".sys" , (entry), (mand) }

static const DRIVER_DECLARATION SYSTEM_DRIVER = DECLARE_DRIVER( "system", SystemDriverEntry, TRUE );

static const DRIVER_DECLARATION DRIVER_NAMES[] = {
    DECLARE_DRIVER("ata", AtaDriverEntry, FALSE),
    DECLARE_DRIVER("disk", DiskDriverEntry, FALSE),
    DECLARE_DRIVER("vol", VolDriverEntry, FALSE),
    DECLARE_DRIVER("fat", FatDriverEntry, FALSE),
    DECLARE_DRIVER("swapfs", SwapFsDriverEntry, FALSE),
    DECLARE_DRIVER("eth82574L", Eth82574LDriverEntry, FALSE)
};

static FUNC_CompareFunction     _VpbCompareFunction;

static FUNC_IsrRoutine          _IomuGenericInterrupt;
static FUNC_InterruptFunction   _IomuSystemTickInterrupt;

__forceinline
char
static
_IomuGetNextVolumeLetter(
    void
    )
{
    static char m_currentVolumeLetter = 'B';

    m_currentVolumeLetter = m_currentVolumeLetter + 1;
    ASSERT(m_currentVolumeLetter <= 'Z');


    return m_currentVolumeLetter;
}

static
__forceinline
void
_IomuUpdateSystemTime(
    void
    )
{
    _InterlockedExchangeAdd( &m_iomuData.SystemUptime.UptimeMicroseconds, m_iomuData.TimeUpdatePerCpuUs );
}

static
STATUS
_IomuSetupRtc(
    OUT_OPT     QWORD*          TscFrequency
    );

static
STATUS
_IomuSetupPit(
    IN          DWORD           TimerPeriodUs,
    OUT         WORD*           InitialPitCount
    );

STATUS
_IomuRetrievePciDevicesAndEstablishHierarchy(
    void
    );

static
STATUS
_IomuInitDrivers(
    void
    );

static
STATUS
_IomuDetermineSystemPartition(
    void
    );

static
STATUS
_IomuInitializeSwapFile(
    void
    );

static
BOOLEAN
_IomuIsSystemPartitionOnDrive(
    IN      char        DriveLetter
    );

static
BOOLEAN
_IomuIsDeviceMsiCapable(
    IN          PPCI_DEVICE_DESCRIPTION     PciDevice
    );

static
STATUS
_IomuProgramPciInterrupt(
    IN          PPCI_DEVICE_DESCRIPTION     PciDevice,
    IN          BYTE                        Vector,
    IN _Strict_type_match_
                APIC_DELIVERY_MODE          DeliveryMode
    );

void
_No_competing_thread_
IomuPreinitSystem(
    void
    )
{
    DWORD i;
    DWORD bitmapSize;

    memzero(&m_iomuData, sizeof(IOMU_DATA));

    m_iomuData.TimerInterruptTimeUs = SCHEDULER_TIMER_INTERRUPT_TIME_US;
    m_iomuData.TimeUpdatePerCpuUs = SCHEDULER_TIMER_INTERRUPT_TIME_US;

    InitializeListHead(&m_iomuData.PciDeviceList);
    InitializeListHead(&m_iomuData.PciBridgeList);
    InitializeListHead(&m_iomuData.DriverList);
    InitializeListHead(&m_iomuData.VpbList);

    for (i = 0; i < NO_OF_USABLE_INTERRUPTS; ++i)
    {
        InitializeListHead(&m_iomuData.RegisteredInterrupts[i].List);
        RwSpinlockInit(&m_iomuData.RegisteredInterrupts[i].Lock);
    }

    bitmapSize = BitmapPreinit(&m_iomuData.InterruptBitmap, NO_OF_TOTAL_INTERRUPTS);
    ASSERT( bitmapSize == sizeof(m_iomuData.BitmapBuffer));

    BitmapInit(&m_iomuData.InterruptBitmap, m_iomuData.BitmapBuffer);

    // reserve bits for exceptions
    BitmapSetBits(&m_iomuData.InterruptBitmap, 0, NO_OF_RESERVED_EXCEPTIONS );

    LockInit(&m_iomuData.GlobalInterruptLock);

    IoApicSystemPreinit();
}

_No_competing_thread_
STATUS
IomuInitSystem(
    IN      WORD        CodeSelector,
    IN      BYTE        NumberOfTssStacks
    )
{
    STATUS status;

    status = STATUS_SUCCESS;

    status = PciSystemInit();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemInit", status);
        return status;
    }
    LOGL("PciSystemInit suceeded\n");

    // we must initialize the PIC before we enable the IO APIC
    PicInitialize(PIC_MASTER_OFFSET, PIC_SLAVE_OFFSET);
    LOGL("Legacy PIC inititialized\n");

    status = IoApicSystemInit();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoApicSystemInit", status);
        return status;
    }
    LOGL("IoApicSystemInit suceeded\n");

    // We re-initialize the IDT handlers after we created the TSS stacks for the
    // current CPU. We first initialized them with 0 TSS stacks to be able to
    // satisfy page faults and to be able to detect an early exception when we
    // modify early boot code.
    status = InitIdtHandlers(CodeSelector, NumberOfTssStacks);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("InitIdtHandlers", status);
        return status;
    }

    LOGL("InitIdtHandlers succeeded\n");

    // retrieve PCI devices and hierarchy
    status = _IomuRetrievePciDevicesAndEstablishHierarchy();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuRetrievePciDevicesAndEstablishHierarchy", status);
        return status;
    }

    LOGL("_IomuRetrievePciDevicesAndEstablishHierarchy succeeded\n");

    // setup RTC
    status = _IomuSetupRtc(&m_iomuData.TscFrequency);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuSetupRtc", status);
        return status;
    }
    LOGL("_IomuSetupRtc succeeded\n");

    LOGL("TSC frequency: 0x%X\n", m_iomuData.TscFrequency );

    status = _IomuSetupPit(m_iomuData.TimerInterruptTimeUs,
                           &m_iomuData.PitInitialTickCount);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuSetupPit", status);
        return status;
    }
    LOGL("_IomuSetupPit succeeded\n");

    // setup KBD
    status = KeyboardInitialize(IrqKeyboard);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("KeyboardInitialize", status);
        return status;
    }

    LOGL("KeyboardInitialize succeeded\n");

    return status;
}

STATUS
IomuInitSystemAfterApWakeup(
    void
    )
{
    DWORD noOfActiveCpus;
    STATUS status;

    noOfActiveCpus = SmpGetNumberOfActiveCpus();
    status = STATUS_SUCCESS;

    m_iomuData.TimeUpdatePerCpuUs = m_iomuData.TimerInterruptTimeUs / noOfActiveCpus;
    LOGL("CPU interrupt time %u/%u us\n", m_iomuData.TimeUpdatePerCpuUs, m_iomuData.TimerInterruptTimeUs);

    status = IoApicLateSystemInit();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoApicLateSystemInit", status);
        return status;
    }
    LOGL("IoApicLateSystemInit succeeded\n");

    // unmask IO Apic interrupts
    IoApicSystemEnableRegisteredInterrupts();

    return status;
}

void
IomuAckInterrupt(
    IN      BYTE        InterruptIndex
    )
{
    LapicSystemSendEOI(InterruptIndex);

    // 10.8.5 - Vol 3 US 057
    // System software desiring to perform directed EOIs for level - triggered interrupts should set bit 12 of the Spurious
    // Interrupt Vector Register and follow each the EOI to the local xAPIC for a level triggered interrupt with a directed
    // EOI to the I / O APIC generating the interrupt(this is done by writing to the I / O APIC’s EOI register).
}

static
STATUS
_IomuSetupRtc(
    OUT_OPT     QWORD*          TscFrequency
    )
{
    STATUS status;
    IO_INTERRUPT ioInterrupt;

    status = STATUS_SUCCESS;
    memzero(&ioInterrupt, sizeof(IO_INTERRUPT));

    RtcInit(TscFrequency);
    LOGL("RtcInit finished\n");

    // install ISR
    ioInterrupt.Type = IoInterruptTypeLegacy;
    ioInterrupt.Irql = IrqlClockLevel;
    ioInterrupt.ServiceRoutine = OsInfoTimeUpdateIsr;
    ioInterrupt.Exclusive = TRUE;
    ioInterrupt.Legacy.Irq = IrqRtc;

    status = IoRegisterInterrupt( &ioInterrupt, NULL);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoRegisterInterrupt", status);
        return status;
    }
    LOGL("RTC Isr successfully installed!\n");

    return status;
}

static
STATUS
_IomuSetupPit(
    IN          DWORD           TimerPeriodUs,
    OUT         WORD*           InitialPitCount
    )
{
    STATUS status;
    IO_INTERRUPT ioInterrupt;
    WORD initialPitCount;

    ASSERT(NULL != InitialPitCount);

    status = STATUS_SUCCESS;
    memzero(&ioInterrupt, sizeof(IO_INTERRUPT));

    // setup PIT timer
    initialPitCount = PitSetTimer(TimerPeriodUs, TRUE);
    LOGL("Initial PIT timer count is 0x%x\n", initialPitCount );

    // install ISR
    ioInterrupt.Type = IoInterruptTypeLegacy;
    ioInterrupt.Irql = IrqlClockLevel;
    ioInterrupt.ServiceRoutine = _IomuSystemTickInterrupt;
    ioInterrupt.Exclusive = TRUE;
    ioInterrupt.BroadcastInterrupt = TRUE;
    ioInterrupt.Legacy.Irq = IrqPitTimer;

    status = IoRegisterInterrupt(&ioInterrupt, NULL);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoRegisterInterrupt", status);
        return status;
    }
    LOGL("PIT Isr successfully installed!\n");

    *InitialPitCount = initialPitCount;

    return status;
}

STATUS
_IomuRetrievePciDevicesAndEstablishHierarchy(
    void
    )
{
    STATUS status;

    status = STATUS_SUCCESS;

    status = PciSystemRetrieveDevices(&m_iomuData.PciDeviceList);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemRetrieveDevices", status);
        return status;
    }

    LOGL("PciDevRetrieveDevices succeeded\n");

    PciSystemEstablishHierarchy(&m_iomuData.PciDeviceList, &m_iomuData.PciBridgeList);
    LOGL("PciSystemEstablishHierarchy finished\n");

    return status;
}

STATUS
IomuInitSystemDriver(
    void
    )
{
    PDRIVER_OBJECT pDriver;
    PLIST_ENTRY pNextDevice;

    LOG_FUNC_START;

    pDriver = IoCreateDriver( SYSTEM_DRIVER.DriverName, SYSTEM_DRIVER.DriverEntry );
    if (NULL == pDriver)
    {
        LOG_ERROR("IoCreateDriver failed for system driver!\n");
        return STATUS_DEVICE_DRIVER_COULD_NOT_BE_CREATED;
    }

    ASSERT(1 == pDriver->NoOfDevices);

    pNextDevice = pDriver->DeviceList.Flink;
    ASSERT(pNextDevice != &pDriver->DeviceList);

    m_iomuData.SystemDevice = CONTAINING_RECORD(pNextDevice, DEVICE_OBJECT, NextDevice);

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

STATUS
IomuLateInit(
    void
    )
{
    STATUS status;

    status = _IomuInitDrivers();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuInitDrivers", status);
        return status;
    }

    LOGL("Drivers successfully initialized!\n");

    status = _IomuDetermineSystemPartition();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuDetermineSystemPartition", status);
    }
    else
    {
        LOGL("Successfully determined system partition!\n");
    }

    status = _IomuInitializeSwapFile();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_IomuInitializeSwapFile", status);
    }
    else
    {
        LOGL("Successfully determined swap partition!\n");
    }

    return STATUS_SUCCESS;
}

const char*
IomuGetSystemPartitionPath(
    void
    )
{
    return m_iomuData.SystemDrive;
}

PFILE_OBJECT
IomuGetSwapFile(
    void
    )
{
    return m_iomuData.SwapFile;
}

PDRIVER_OBJECT
IomuGetDriverByName(
    IN_Z    char*           DriverName
    )
{
    PDRIVER_OBJECT pDriver;
    PLIST_ENTRY pCurEntry;

    ASSERT(NULL != DriverName);

    pDriver = NULL;
    for(pCurEntry = m_iomuData.DriverList.Flink;
        pCurEntry != &m_iomuData.DriverList;
        pCurEntry = pCurEntry->Flink)
    {
        pDriver = CONTAINING_RECORD(pCurEntry, DRIVER_OBJECT, NextDriver);

        if (0 == strcmp(pDriver->DriverName, DriverName))
        {
            // found driver
            return pDriver;
        }
    }

    return NULL;
}

void
IomuDriverInstalled(
    IN      PDRIVER_OBJECT  Driver
    )
{
    ASSERT(NULL != Driver);

    InsertTailList(&m_iomuData.DriverList, &Driver->NextDriver);
}

PLIST_ENTRY
IomuGetPciDeviceList(
    void
    )
{
    return &m_iomuData.PciDeviceList;
}

STATUS
IomuGetDevicesByType(
    IN_RANGE_UPPER(DeviceTypeMax)
                                    DEVICE_TYPE         DeviceType,
    _When_(*NumberOfDevices > 0, OUT_PTR)
    _When_(*NumberOfDevices == 0, OUT_PTR_MAYBE_NULL)
                                    PDEVICE_OBJECT**    DeviceObjects,
    OUT                             DWORD*              NumberOfDevices
    )
{
    STATUS status;
    PLIST_ENTRY pCurDriverEntry;
    PLIST_ENTRY pCurDeviceEntry;
    DWORD count;
    DWORD i;
    PDEVICE_OBJECT* pFoundDevices;
    DWORD indexInArray;

    ASSERT(NULL != DeviceObjects);
    ASSERT(NULL != NumberOfDevices);

    status = STATUS_SUCCESS;
    count = 0;
    pFoundDevices = NULL;
    indexInArray = 0;

    for (i = 0; i < 2; ++i)
    {
        if (1 == i)
        {
            // second iteration
            if (0 == count)
            {
                // nothing to do, no devices by type found
                break;
            }

            pFoundDevices = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PDEVICE_OBJECT) * count, HEAP_TEMP_TAG, 0);
            if (NULL == pFoundDevices)
            {
                status = STATUS_HEAP_NO_MORE_MEMORY;
                break;
            }
        }

        for(pCurDriverEntry = m_iomuData.DriverList.Flink;
            pCurDriverEntry != &m_iomuData.DriverList;
            pCurDriverEntry = pCurDriverEntry->Flink)
        {
            PDRIVER_OBJECT pDriver = CONTAINING_RECORD(pCurDriverEntry, DRIVER_OBJECT, NextDriver);

            for (pCurDeviceEntry = pDriver->DeviceList.Flink;
            pCurDeviceEntry != &pDriver->DeviceList;
                pCurDeviceEntry = pCurDeviceEntry->Flink)
            {
                PDEVICE_OBJECT pDeviceObject = CONTAINING_RECORD(pCurDeviceEntry, DEVICE_OBJECT, NextDevice);

                if (DeviceType == pDeviceObject->DeviceType)
                {
                    // found device type we're looking for
                    if (0 == i)
                    {
                        // on the first iteration we count
                        count = count + 1;
                    }
                    else
                    {
                        // on the second iteration we add to our allocated array
                        pFoundDevices[indexInArray] = pDeviceObject;
                        indexInArray = indexInArray + 1;
                    }
                }
            }
        }
    }

    if (SUCCEEDED(status))
    {
        ASSERT(indexInArray == count);

        *DeviceObjects = pFoundDevices;
        *NumberOfDevices = count;
    }

    return status;
}

void
IomuNewVpbCreated(
    INOUT       struct _VPB*        Vpb
    )
{
    ASSERT(NULL != Vpb);

    Vpb->VolumeLetter = _IomuGetNextVolumeLetter();

    InsertOrderedList(&m_iomuData.VpbList, &Vpb->NextVpb, _VpbCompareFunction, NULL);
}

void
IomuExecuteForEachVpb(
    IN          PFUNC_ListFunction  Function,
    IN_OPT      PVOID               Context,
    IN          BOOLEAN             Exclusive
    )
{
    ASSERT(NULL != Function);

    UNREFERENCED_PARAMETER(Exclusive);

    // take lock
    ForEachElementExecute(&m_iomuData.VpbList, Function, Context, FALSE);
    // release lock
}

PTR_SUCCESS
PVPB
IomuSearchForVpb(
    IN          char                DriveLetter
    )
{
    VPB vpbToSearchFor;
    PLIST_ENTRY pCorrespondingVpb;

    memzero(&vpbToSearchFor, sizeof(VPB));

    // take volume letter
    vpbToSearchFor.VolumeLetter = DriveLetter;

    pCorrespondingVpb = ListSearchForElement(&m_iomuData.VpbList, &vpbToSearchFor.NextVpb, TRUE, _VpbCompareFunction, NULL);
    if (NULL == pCorrespondingVpb)
    {
        return NULL;
    }

    return CONTAINING_RECORD(pCorrespondingVpb, VPB, NextVpb);
}

STATUS
IomuRegisterInterrupt(
    IN          PIO_INTERRUPT           Interrupt,
    IN_OPT      PDEVICE_OBJECT          DeviceObject,
    OUT_OPT     PBYTE                   Vector
    )
{
    STATUS status;
    BOOLEAN bListEmpty;
    PREGISTERED_INTERRUPT_ENTRY pNewEntry;
    BOOLEAN bSetupIoApicRedirEntry;
    PDEVICE_OBJECT pDevObj;
    BYTE interruptIndex;
    BYTE interruptVector;
    DWORD bitmapResult;
    BOOLEAN bIoApicEntryRegistered;
    APIC_DELIVERY_MODE apicDeliveryMode;
    BOOLEAN bMsiCapable;
    BOOLEAN bIoApicInterrupt;
    BYTE interruptLine;
    INTR_STATE intrState;
    BOOLEAN bAcquiredListLock;
    INTR_STATE dummyState;

    ASSERT( NULL != Interrupt );
    ASSERT( NULL != Interrupt->ServiceRoutine );


    status = STATUS_SUCCESS;
    bSetupIoApicRedirEntry = FALSE;
    pNewEntry = NULL;
    pDevObj = ( NULL != DeviceObject ) ? DeviceObject : m_iomuData.SystemDevice;
    ASSERT( NULL != pDevObj );
    bIoApicEntryRegistered = FALSE;
    interruptVector = 0;
    apicDeliveryMode = Interrupt->BroadcastInterrupt ? ApicDeliveryModeFixed : ApicDeliveryModeLowest;
    bMsiCapable = FALSE;
    bAcquiredListLock = FALSE;
    interruptIndex = MAX_BYTE;

    LOG_TRACE_INTERRUPT("Searching for free vector for IRQL 0x%x\n", Interrupt->Irql );

    // Need to take lock here because we must protect the whole flow of configuring an interrupt
    // IoApicSystemGetVectorForIrq and BitmapScanFromToAndFlip will cause problems if they are called concurrently
    LockAcquire(&m_iomuData.GlobalInterruptLock, &intrState);

    __try
    {

        interruptLine = MAX_BYTE;
        if (Interrupt->Type == IoInterruptTypePci)
        {
            bMsiCapable = _IomuIsDeviceMsiCapable(Interrupt->Pci.PciDevice);
            if (!bMsiCapable)
            {
                DWORD temp = IoApicGetInterruptLineForPciDevice(Interrupt->Pci.PciDevice);

                ASSERT(temp <= MAX_BYTE);
                interruptLine = (BYTE)temp;
                LOGL("IRQ line is 0x%x\n", interruptLine);
            }
        }
        else if (Interrupt->Type == IoInterruptTypeLegacy)
        {
            interruptLine = Interrupt->Legacy.Irq;
        }

        bIoApicInterrupt = (Interrupt->Type == IoInterruptTypeLegacy) || (Interrupt->Type == IoInterruptTypePci && !bMsiCapable);

        if (bIoApicInterrupt)
        {
            ASSERT((Interrupt->Type == IoInterruptTypeLegacy) || (Interrupt->Type == IoInterruptTypePci && !bMsiCapable));

            // check to see if we already have an IO APIC entry for this IRQ
            status = IoApicSystemGetVectorForIrq(interruptLine,
                                                 &interruptVector);
            if (STATUS_SUCCESS == status)
            {
                // it means we already have a vector mapped for the IRQ
                if (VECTOR_TO_IRQL(interruptVector) != Interrupt->Irql)
                {
                    LOG_ERROR("Cannot register legacy interrupt for IRQL 0x%x because there is already a registered vector 0x%02x for IRQ 0x%x which differs in priority\n",
                              Interrupt->Irql, interruptVector, interruptLine);
                    status = STATUS_DEVICE_INTERRUPT_PRIORITY_NOT_AVAILABLE;
                    __leave;
                }

                // we can share the vector from the IRQL point of view
                bIoApicEntryRegistered = TRUE;
            }
            if (!SUCCEEDED(status) && status != STATUS_DEVICE_INTERRUPT_NOT_CONFIGURED)
            {
                LOG_FUNC_ERROR("IoApicSystemGetVectorForIrq", status);
                __leave;
            }

            // if the IoApicSystemGetVectorForIrq failed because these is no vector registered for the
            // specified IRQ => it is all ok
            status = STATUS_SUCCESS;
        }

        if (!bIoApicEntryRegistered)
        {
            bitmapResult = BitmapScanFromToAndFlip(&m_iomuData.InterruptBitmap,
                                                   IRQL_TO_VECTOR(Interrupt->Irql),
                                                   IRQL_TO_VECTOR(Interrupt->Irql) + VECTORS_PER_IRQL,
                                                   1,
                                                   FALSE);
        }
        else
        {
            ASSERT((Interrupt->Type == IoInterruptTypeLegacy) || (Interrupt->Type == IoInterruptTypePci && !bMsiCapable));

            // simulate the fact that the interrupt needs to be shared
            bitmapResult = MAX_DWORD;
        }

        if (MAX_DWORD == bitmapResult)
        {
            // we don't have any vectors left for the required IRQL
            if (Interrupt->Exclusive)
            {
                LOG_ERROR("All the vectors left for the required IRQL 0x%x are already registered and exclusive rights were requested!\n", Interrupt->Irql);
                status = STATUS_DEVICE_INTERRUPT_NOT_AVAILABLE;
                __leave;
            }
            else
            {
                // can share interrupts
                // if bIoApicEntryRegistered is set => the interrupt vector is already set
                if (!bIoApicEntryRegistered)
                {
                    interruptVector = IRQL_TO_VECTOR(Interrupt->Irql);
                }
            }
        }
        else
        {
            interruptVector = (BYTE)bitmapResult;
        }

        LOG_TRACE_INTERRUPT("Will register vector 0x%02x for IRQL 0x%x\n", interruptVector, Interrupt->Irql);

        ASSERT(interruptVector >= NO_OF_RESERVED_EXCEPTIONS && interruptVector < NO_OF_TOTAL_INTERRUPTS);
        interruptIndex = interruptVector - NO_OF_RESERVED_EXCEPTIONS;

        RwSpinlockAcquireExclusive(&m_iomuData.RegisteredInterrupts[interruptIndex].Lock, &dummyState);
        bAcquiredListLock = TRUE;

        bListEmpty = IsListEmpty(&m_iomuData.RegisteredInterrupts[interruptIndex].List);
        ASSERT_INFO(bListEmpty || !Interrupt->Exclusive, "We shouldn't get here because the check is already done and the function should have returned\n");

        if (bListEmpty)
        {
            // try to install handler

            // warning C4306: 'type cast': conversion from 'const BYTE' to 'PVOID' of greater size
#pragma warning(suppress:4306)
            status = IsrInstallEx(interruptVector, _IomuGenericInterrupt, (PVOID)interruptIndex);
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("IsrInstallEx", status);
                __leave;
            }

            bSetupIoApicRedirEntry = ((IoInterruptTypeLegacy == Interrupt->Type) || (Interrupt->Type == IoInterruptTypePci && !bMsiCapable)) && !bIoApicEntryRegistered;
        }

        pNewEntry = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(REGISTERED_INTERRUPT_ENTRY), HEAP_IOMU_TAG, 0);
        if (NULL == pNewEntry)
        {
            LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(REGISTERED_INTERRUPT_ENTRY));
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        pNewEntry->Function = Interrupt->ServiceRoutine;
        pNewEntry->Device = pDevObj;

        // there is no reason to maintain information about device exclusivity
        // because new interrupts are added to the tail of the list =>
        // if we have an exclusive ISR at the beginning it thinks it is the
        // only one being executed
        InsertTailList(&m_iomuData.RegisteredInterrupts[interruptIndex].List, &pNewEntry->ListEntry);

        RwSpinlockReleaseExclusive(&m_iomuData.RegisteredInterrupts[interruptIndex].Lock, INTR_OFF);
        bAcquiredListLock = FALSE;

        if (Interrupt->Type == IoInterruptTypePci && bMsiCapable)
        {
            LOG_TRACE_INTERRUPT("Will setup PCI interrupt for device at (%u.%u.%u)\n",
                                Interrupt->Pci.PciDevice->DeviceLocation.Bus,
                                Interrupt->Pci.PciDevice->DeviceLocation.Device,
                                Interrupt->Pci.PciDevice->DeviceLocation.Function);

            status = _IomuProgramPciInterrupt(Interrupt->Pci.PciDevice,
                                              interruptVector,
                                              apicDeliveryMode);
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("_IomuRegisterPciInterrupt", status);
                __leave;
            }
        }

        if (bSetupIoApicRedirEntry)
        {
            ASSERT((Interrupt->Type == IoInterruptTypeLegacy) || (Interrupt->Type == IoInterruptTypePci && !bMsiCapable));


            // PCI specification 3.0 Section 2.2.6 Interrupt Pins
            // Interrupts on PCI are optional and defined as "level sensitive," asserted low (negative true)
            LOG_TRACE_INTERRUPT("Will setup IOAPIC redirection entry for IRQ 0x%x\n", interruptLine);
            status = IoApicSystemSetInterrupt(interruptLine,
                                              interruptVector,
                                              ApicDestinationModeLogical,
                                              apicDeliveryMode,
                                              (Interrupt->Type == IoInterruptTypePci) ? ApicPinPolarityActiveLow : ApicPinPolarityActiveHigh,
                                              (Interrupt->Type == IoInterruptTypePci) ? ApicTriggerModeLevel : ApicTriggerModeEdge,
                                              MAX_BYTE,
                                              FALSE);
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("IoApicSystemSetInterrupt", status);
                __leave;
            }
        }
    }
    __finally
    {
        if (bAcquiredListLock)
        {
            ASSERT(interruptIndex != MAX_BYTE);

            RwSpinlockReleaseExclusive(&m_iomuData.RegisteredInterrupts[interruptIndex].Lock, INTR_OFF);
        }

        LockRelease(&m_iomuData.GlobalInterruptLock, intrState);

        if (SUCCEEDED(status))
        {
            if (NULL != Vector)
            {
                *Vector = interruptVector;
            }

            LOG_TRACE_INTERRUPT("Succesfully registered interrupt [0x%02x] for device 0x%X\n", interruptVector, pDevObj);
        }
    }

    return status;
}

QWORD
IomuGetSystemTicks(
    OUT_OPT     QWORD*                  TickFrequency
    )
{
    if (NULL != TickFrequency)
    {
        *TickFrequency = m_iomuData.TscFrequency;
    }

    return RtcGetTickCount();
}

QWORD
IomuGetSystemTimeUs(
    void
    )
{
    UPTIME uptime;
    QWORD systemTime;

    uptime.Raw = m_iomuData.SystemUptime.Raw;

    systemTime = (QWORD) uptime.UptimeSeconds * SEC_IN_US +
                 ( uptime.UptimeMicroseconds >= SEC_IN_US ? SEC_IN_US : uptime.UptimeMicroseconds );

    return systemTime;
}

QWORD
IomuTickCountToUs(
    IN          QWORD                   TickCount
    )
{
    return ( TickCount * 1000 ) / ( m_iomuData.TscFrequency / 1000 );
}

void
IomuCmosUpdateOccurred(
    void
    )
{
    QWORD newSecCount;

    newSecCount = (QWORD) m_iomuData.SystemUptime.UptimeSeconds + 1;
    ASSERT( newSecCount <= MAX_DWORD );

    // this will also set UptimeMicroseconds to zero
    _InterlockedExchange64(&m_iomuData.SystemUptime.Raw, newSecCount );
}

DWORD
IomuGetTimerInterrupTimeUs(
    void
    )
{
    return m_iomuData.TimerInterruptTimeUs;
}

BOOLEAN
IomuIsInterruptSpurious(
    IN          BYTE                    Vector
    )
{
    return ((Vector == PIC_MASTER_OFFSET + IrqSpurious) || (Vector == PIC_SLAVE_OFFSET + IrqSpurious))
        && !LapicSystemIsInterruptServiced(Vector);
}

static
INT64
(__cdecl _VpbCompareFunction) (
    IN      PLIST_ENTRY     FirstElem,
    IN      PLIST_ENTRY     SecondElem,
    IN_OPT  PVOID           Context
    )
{
    PVPB pFirstVpb;
    PVPB pSecondVpb;

    ASSERT(NULL != FirstElem);
    ASSERT(NULL != SecondElem);
    ASSERT(Context == NULL);

    pFirstVpb = CONTAINING_RECORD(FirstElem, VPB, NextVpb);
    pSecondVpb = CONTAINING_RECORD(SecondElem, VPB, NextVpb);

    return ( tolower(pFirstVpb->VolumeLetter) - tolower(pSecondVpb->VolumeLetter));
}

static
BOOLEAN
(__cdecl _IomuGenericInterrupt)(
    IN_OPT      PVOID           Context
    )
{
    BYTE interrupt;
    BOOLEAN bHandledInterrupt;
    INTR_STATE dummyState;
    BOOLEAN bFoundEntry;

    ASSERT( NULL != Context );
    ASSERT(CpuIntrGetState() == INTR_OFF);

    // warning C4305: 'type cast': truncation from 'const PVOID' to 'BYTE'
#pragma warning(suppress:4305)
    interrupt = (BYTE) Context;

    bFoundEntry = FALSE;
    bHandledInterrupt = FALSE;

    ASSERT( interrupt < NO_OF_USABLE_INTERRUPTS );

    RwSpinlockAcquireShared(&m_iomuData.RegisteredInterrupts[interrupt].Lock, &dummyState);
    for (PLIST_ENTRY pListEntry = m_iomuData.RegisteredInterrupts[interrupt].List.Flink;
         pListEntry != &m_iomuData.RegisteredInterrupts[interrupt].List;
         pListEntry = pListEntry->Flink
        )
    {
        PREGISTERED_INTERRUPT_ENTRY pEntry = CONTAINING_RECORD(pListEntry, REGISTERED_INTERRUPT_ENTRY, ListEntry);

        bHandledInterrupt = pEntry->Function( pEntry->Device );
        if (bHandledInterrupt)
        {
            break;
        }
    }
    RwSpinlockReleaseShared(&m_iomuData.RegisteredInterrupts[interrupt].Lock, dummyState);

    return bHandledInterrupt;
}

static
BOOLEAN
(__cdecl _IomuSystemTickInterrupt)(
    IN        PDEVICE_OBJECT           Device
    )
{
    ASSERT( NULL != Device );

    _IomuUpdateSystemTime();
    //LOGP("%U us\n", IomuGetSystemTimeUs());

    ExSystemTimerTick();

    return TRUE;
}

static
STATUS
_IomuInitDrivers(
    void
    )
{
    STATUS status;
    PDRIVER_OBJECT pDriver;
    DWORD i;

    status = STATUS_SUCCESS;
    pDriver = NULL;

    __try
    {
        for (i = 0; i < ARRAYSIZE(DRIVER_NAMES); ++i)
        {
            pDriver = IoCreateDriver(DRIVER_NAMES[i].DriverName, DRIVER_NAMES[i].DriverEntry);
            if (NULL == pDriver)
            {
                if (DRIVER_NAMES[i].Mandatory)
                {
                    LOG_ERROR("Mandatory driver %s could not be loaded\n", DRIVER_NAMES[i].DriverName);
                    status = STATUS_DEVICE_DRIVER_COULD_NOT_BE_CREATED;
                    __leave;
                }
                else
                {
                    LOG_WARNING("Secondary driver %s could not be loaded\n", DRIVER_NAMES[i].DriverName);
                }
            }
        }
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            // call IomuUnitDrivers
        }
    }

    // dump driver list
    DumpDriverList(&m_iomuData.DriverList);

    return status;
}

static
STATUS
_IomuDetermineSystemPartition(
    void
    )
{
    STATUS status;
    BOOLEAN bFoundSystemPartition;

    status = STATUS_SUCCESS;
    bFoundSystemPartition = FALSE;

    for (PLIST_ENTRY pListEntry = m_iomuData.VpbList.Flink;
         pListEntry != &m_iomuData.VpbList;
         pListEntry = pListEntry->Flink)
    {
        PVPB pVpb = CONTAINING_RECORD(pListEntry, VPB, NextVpb);

        if (!pVpb->Flags.Mounted)
        {
            continue;
        }

        bFoundSystemPartition = _IomuIsSystemPartitionOnDrive(pVpb->VolumeLetter);
        if (bFoundSystemPartition)
        {
            snprintf(m_iomuData.SystemDrive, sizeof(m_iomuData.SystemDrive),
                     "%c:\\", pVpb->VolumeLetter);

            LOG("Found system partition at [%s]\n", m_iomuData.SystemDrive);

            break;
        }
    }

    return bFoundSystemPartition ? STATUS_SUCCESS : STATUS_FILE_NOT_FOUND;
}

static
STATUS
_IomuInitializeSwapFile(
    void
    )
{
    STATUS status;
    BOOLEAN bOpenedSwapFile;

    status = STATUS_SUCCESS;
    bOpenedSwapFile = FALSE;

    for (PLIST_ENTRY pListEntry = m_iomuData.VpbList.Flink;
         pListEntry != &m_iomuData.VpbList;
         pListEntry = pListEntry->Flink)
    {
        PVPB pVpb = CONTAINING_RECORD(pListEntry, VPB, NextVpb);
        char swapFilePath[4];

        // if the FS is not mounted => we do not recognize it
        if (!pVpb->Flags.Mounted)
        {
            continue;
        }

        // we only care about swap partitions
        if (!pVpb->Flags.SwapSpace)
        {
            continue;
        }

        status = snprintf(swapFilePath, sizeof(swapFilePath), "%c:\\", pVpb->VolumeLetter);
        ASSERT(SUCCEEDED(status));

        status = IoCreateFile(&m_iomuData.SwapFile,
                              swapFilePath,
                              FALSE,
                              FALSE,
                              FALSE);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoCreateFile", status);
            continue;
        }
        bOpenedSwapFile = TRUE;
    }

    return bOpenedSwapFile ? STATUS_SUCCESS : STATUS_FILE_NOT_FOUND;
}

static
BOOLEAN
_IomuIsSystemPartitionOnDrive(
    IN      char        DriveLetter
    )
{
    PFILE_OBJECT pFileObject;
    STATUS status;
    char iniFilePath[MAX_PATH];

    snprintf(iniFilePath, MAX_PATH, "%c:\\%s",
             DriveLetter, HAL9000_SYSTEM_FILE_NAME);

    status = IoCreateFile(&pFileObject,
                          iniFilePath,
                          FALSE,
                          FALSE,
                          FALSE);
    if (!SUCCEEDED(status))
    {
        LOG_TRACE_IO("Cannot find [%s] ini file on %c\n",
                     iniFilePath, DriveLetter);
        return FALSE;
    }

    IoCloseFile(pFileObject);
    pFileObject = NULL;

    return TRUE;
}

static
BOOLEAN
_IomuIsDeviceMsiCapable(
    IN          PPCI_DEVICE_DESCRIPTION     PciDevice
    )
{
    STATUS status;
    PPCI_CAPABILITY_HEADER pciCap;

    ASSERT( NULL != PciDevice );

    status = PciDevRetrieveCapabilityById(PciDevice->DeviceData,
                                          PCI_CAPABILITY_ID_MSI,
                                          &pciCap
                                          );

    return SUCCEEDED(status);
}

static
STATUS
_IomuProgramPciInterrupt(
    IN          PPCI_DEVICE_DESCRIPTION     PciDevice,
    IN          BYTE                        Vector,
    IN _Strict_type_match_
                APIC_DELIVERY_MODE          DeliveryMode
    )
{
    STATUS status;

    ASSERT( NULL != PciDevice );
    ASSERT(_IomuIsDeviceMsiCapable(PciDevice));

    LOG_FUNC_START;

    status = PciDevDisableLegacyInterrupts(PciDevice);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciDevDisableLegacyInterrupts", status);
        return status;
    }
    LOG_TRACE_INTERRUPT("Successfully disabled legacy interrupts for PCI device at (%u.%u.%u)\n",
                        PciDevice->DeviceLocation.Bus, PciDevice->DeviceLocation.Device, PciDevice->DeviceLocation.Function );

    status = PciDevProgramMsiInterrupt(PciDevice,
                                       Vector,
                                       ApicDestinationModeLogical,
                                       MAX_BYTE,
                                       DeliveryMode,
                                       ApicPinPolarityActiveHigh,
                                       ApicTriggerModeLevel
                                       );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciDevProgramMsiInterrupt", status );
        return status;
    }

    LOG_FUNC_END;

    return status;
}