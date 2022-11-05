// ACPICA OSL implementation

#include "HAL9000.h"

#pragma warning(push)
#include "acpi.h"
#pragma warning(pop)
#include "mmu.h"
#include "synch.h"
#include "thread.h"
#include "pci_system.h"
#include "pit.h"
#include "iomu.h"

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsInitialize
ACPI_STATUS
AcpiOsInitialize (
    void)
{
    LOG_FUNC_START;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsTerminate
ACPI_STATUS
AcpiOsTerminate (
    void)
{
    LOG_FUNC_START;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetRootPointer
ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer (
    void)
{
    ACPI_SIZE Ret;

    LOG_FUNC_START;

    AcpiFindRootPointer(&Ret);

    LOG_FUNC_END;

    return Ret;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsPredefinedOverride
ACPI_STATUS
AcpiOsPredefinedOverride (
    const ACPI_PREDEFINED_NAMES *InitVal,
    ACPI_STRING                 *NewVal)
{
    UNREFERENCED_PARAMETER(InitVal);

    if (NULL == NewVal)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    *NewVal = NULL;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsTableOverride
ACPI_STATUS
AcpiOsTableOverride (
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_TABLE_HEADER       **NewTable)
{
    UNREFERENCED_PARAMETER(ExistingTable);

    if (NULL == NewTable)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    *NewTable = NULL;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsPhysicalTableOverride
ACPI_STATUS
AcpiOsPhysicalTableOverride (
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_PHYSICAL_ADDRESS   *NewAddress,
    UINT32                  *NewTableLength)
{
    UNREFERENCED_PARAMETER(ExistingTable);

    if (NULL == NewAddress)
    {
        return AE_BAD_PARAMETER;
    }

    if (NULL == NewTableLength)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    *NewAddress = 0;
    *NewTableLength = 0;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCreateLock
ACPI_STATUS
AcpiOsCreateLock (
    ACPI_SPINLOCK           *OutHandle)
{
    PLOCK lock;

    if (NULL == OutHandle)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    lock = ExAllocatePoolWithTag(PoolAllocateZeroMemory,
                                 sizeof(LOCK),
                                 HEAP_ACPICA_TAG,
                                 0
                                 );
    if (NULL == lock)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(LOCK));
        return AE_NO_MEMORY;
    }

    LockInit(lock);

    LOG_TRACE_ACPI("Lock at: 0x%X\n", lock );

    *OutHandle = lock;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsDeleteLock
void
AcpiOsDeleteLock (
    ACPI_SPINLOCK           Handle)
{
    ASSERT(NULL != Handle);

    LOG_FUNC_START;

    ExFreePoolWithTag(Handle, HEAP_ACPICA_TAG);

    LOG_FUNC_END;

    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsAcquireLock
ACPI_CPU_FLAGS
AcpiOsAcquireLock (
    ACPI_SPINLOCK           Handle)
{
    INTR_STATE intrState;

    ASSERT(NULL != Handle);

    LockAcquire((PLOCK) Handle, &intrState);

    // Warning C26165 Possibly failing to release lock
    // Because we can't modify the ACPICA headers we have nothing to do but suppress the warning
#pragma warning(suppress: 26165)
    return intrState;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReleaseLock
void
AcpiOsReleaseLock (
    ACPI_SPINLOCK           Handle,
    ACPI_CPU_FLAGS          Flags)
{
    ASSERT( NULL != Handle );

    // Warning C26110 Caller failing to hold lock before calling function
    // Because we can't modify the ACPICA headers we have nothing to do but suppress the warning
#pragma warning(suppress: 26110)
    LockRelease(Handle, (INTR_STATE) Flags);

    // Warning C26167 Possibly releasing unheld lock
    // Because we can't modify the ACPICA headers we have nothing to do but suppress the warning
#pragma warning(suppress: 26167)
    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCreateSemaphore
ACPI_STATUS
AcpiOsCreateSemaphore (
    UINT32                  MaxUnits,
    UINT32                  InitialUnits,
    ACPI_SEMAPHORE          *OutHandle)
{
    PEVENT event;
    STATUS status;

    if (NULL == OutHandle)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    LOG_TRACE_ACPI("Initial/max units: %u/%u\n", InitialUnits, MaxUnits );

    ASSERT(MaxUnits == 1);
    ASSERT(InitialUnits <= 1);

    event = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(EVENT), HEAP_ACPICA_TAG, 0 );
    if (NULL == event)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(EVENT));
        return AE_NO_MEMORY;
    }

    status = EvtInitialize(event, EventTypeSynchronization, (BOOLEAN) InitialUnits);
    if (!SUCCEEDED(status))
    {
        LOG_ERROR("EvtInitialize", status );
        return AE_NO_HANDLER;
    }

    LOG_TRACE_ACPI("Semaphore at: 0x%X\n", event );

    *OutHandle = event;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsDeleteSemaphore
ACPI_STATUS
AcpiOsDeleteSemaphore (
    ACPI_SEMAPHORE          Handle)
{
    if (NULL == Handle)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    ExFreePoolWithTag(Handle, HEAP_ACPICA_TAG);

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWaitSemaphore
ACPI_STATUS
AcpiOsWaitSemaphore (
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units,
    UINT16                  Timeout)
{
    ACPI_STATUS acpiStatus;

    if (NULL == Handle)
    {
        return AE_BAD_PARAMETER;
    }

    acpiStatus = AE_OK;

    ASSERT(1 == Units);

    acpiStatus = EvtIsSignaled(Handle) ? AE_OK : AE_TIME;

    if (AE_TIME == acpiStatus && Timeout != 0)
    {
        if (MAX_WORD != Timeout)
        {
            LOG_TRACE_ACPI("Will sleep %u ms\n", Timeout);
            ASSERT(Timeout <= MAX_DWORD / MS_IN_US);

            // Because we don't have a mechanism to wait for an event
            // for a period of time (Timeout) we will first wait for the
            // timeout interval to pass and after we'll check th event again
            PitSleep((DWORD)Timeout * MS_IN_US);

            // check after sleep if event is still not signaled
            acpiStatus = EvtIsSignaled(Handle) ? AE_OK : AE_TIME;
        }
        else
        {
            // MAX_WORD

            // This is very dangerous because we may never return
            // if something is not configured properly
            LOG_TRACE_ACPI("Will wait indefinetely!\n");
            EvtWaitForSignal(Handle);
            acpiStatus = AE_OK;
        }
    }

    return acpiStatus;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsSignalSemaphore
ACPI_STATUS
AcpiOsSignalSemaphore (
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units)
{
    if (NULL == Handle)
    {
        return AE_BAD_PARAMETER;
    }

    ASSERT( 1 == Units );
    EvtSignal(Handle);

    return AE_OK;
}
#endif

#if (ACPI_MUTEX_TYPE != ACPI_BINARY_SEMAPHORE)

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCreateMutex
ACPI_STATUS
AcpiOsCreateMutex (
    ACPI_MUTEX              *OutHandle)
{
    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsDeleteMutex
void
AcpiOsDeleteMutex (
    ACPI_MUTEX              Handle)
{
    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsAcquireMutex
ACPI_STATUS
AcpiOsAcquireMutex (
    ACPI_MUTEX              Handle,
    UINT16                  Timeout)
{
    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReleaseMutex
void
AcpiOsReleaseMutex (
    ACPI_MUTEX              Handle)
{
    return;
}
#endif

#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsAllocate
void *
AcpiOsAllocate (
    ACPI_SIZE               Size)
{
    PVOID pResult;

    if (0 == Size)
    {
        return NULL;
    }

    ASSERT( Size <= MAX_DWORD );

    // Mid July: ACPICA is stupid, it states in the documentation that the AcpiOsAllocate does not expect
    // the memory to be initialized, when in fact there seem to be problems when not zero-initializing the memory
    /// TODO: Mid September: investigate if this still happens - I'm not able to reproduce the problem
    pResult = ExAllocatePoolWithTag(PoolAllocateZeroMemory, (DWORD)Size, HEAP_ACPICA_TAG, 0);

    LOG_TRACE_ACPI("Allocated %U bytes of memory at 0x%X\n", Size, pResult );

    return pResult;
}
#endif


#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsFree
void
AcpiOsFree (
    void*                   Memory)
{
    if (NULL == Memory)
    {
        return;
    }

    //LOG_TRACE_ACPI( "AcpiOsFree about to free memory at address 0x%X\n", Memory );

    ExFreePoolWithTag( Memory, HEAP_ACPICA_TAG );
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsMapMemory
void *
AcpiOsMapMemory (
    ACPI_PHYSICAL_ADDRESS   Where,
    ACPI_SIZE               Length)
{
    PVOID pMappedAddress;

    LOG_FUNC_START;

    //LOG_TRACE_ACPI( "AcpiOsMapMemory about to map 0x%X bytes starting at PA 0x%X\n", Length, Where );

    pMappedAddress = MmuMapSystemMemory( (PHYSICAL_ADDRESS) Where, Length );

    LOG_FUNC_END;

    return pMappedAddress;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsUnmapMemory
void
AcpiOsUnmapMemory (
    void                    *LogicalAddress,
    ACPI_SIZE               Size)
{
    LOG_FUNC_START;

    LOG_TRACE_ACPI( "AcpiOsUnmapMemory about to unmap 0x%X bytes starting at VA 0x%X PA 0x%X\n",
        Size, LogicalAddress, MmuGetPhysicalAddress(LogicalAddress) );

    MmuUnmapSystemMemory( LogicalAddress, ( DWORD ) Size );

    LOG_FUNC_END;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetPhysicalAddress
ACPI_STATUS
AcpiOsGetPhysicalAddress (
    void                    *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS   *PhysicalAddress)
{
    if (PhysicalAddress == NULL)
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    *PhysicalAddress = (ACPI_PHYSICAL_ADDRESS) MmuGetPhysicalAddress(LogicalAddress);

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Memory/Object Cache
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCreateCache
ACPI_STATUS
AcpiOsCreateCache (
    char                    *CacheName,
    UINT16                  ObjectSize,
    UINT16                  MaxDepth,
    ACPI_CACHE_T            **ReturnCache)
{
    WORD* pCache;

    UNREFERENCED_PARAMETER(CacheName);
    UNREFERENCED_PARAMETER(MaxDepth);

    if ((0 == ObjectSize) || ( NULL == ReturnCache ))
    {
        return AE_BAD_PARAMETER;
    }

    LOG_TRACE_ACPI("Object size: 0x%x\n", ObjectSize);

    pCache = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(WORD), HEAP_ACPICA_TAG, 0);
    if (NULL == pCache)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(WORD));
        return AE_NO_MEMORY;
    }

    *pCache = ObjectSize;

    *ReturnCache = (ACPI_CACHE_T*) pCache;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsDeleteCache
ACPI_STATUS
AcpiOsDeleteCache (
    ACPI_CACHE_T            *Cache)
{
    LOG_FUNC_START;

    ExFreePoolWithTag(Cache, HEAP_ACPICA_TAG);

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsPurgeCache
ACPI_STATUS
AcpiOsPurgeCache (
    ACPI_CACHE_T            *Cache)
{
    UNREFERENCED_PARAMETER(Cache);

    LOG_FUNC_START;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsAcquireObject
void *
AcpiOsAcquireObject (
    ACPI_CACHE_T            *Cache)
{
    PVOID pResult;

    pResult = ExAllocatePoolWithTag(PoolAllocateZeroMemory, * (WORD*)Cache, HEAP_ACPICA_TAG, 0 );
    if (NULL == pResult)
    {
        LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", *(WORD*)Cache);
        return NULL;
    }

    return pResult;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReleaseObject
ACPI_STATUS
AcpiOsReleaseObject (
    ACPI_CACHE_T            *Cache,
    void                    *Object)
{
    if ((NULL == Cache) || (NULL == Object))
    {
        return AE_BAD_PATHNAME;
    }

    ExFreePoolWithTag(Object, HEAP_ACPICA_TAG);

    return AE_OK;
}
#endif


/*
* Interrupt handlers
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsInstallInterruptHandler
ACPI_STATUS
AcpiOsInstallInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
    UNREFERENCED_PARAMETER(InterruptNumber);
    UNREFERENCED_PARAMETER(ServiceRoutine);
    UNREFERENCED_PARAMETER(Context);

    LOG_FUNC_START;

    LOG_TRACE_ACPI("Interrupt 0x%x\n", InterruptNumber );

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsRemoveInterruptHandler
ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
    UNREFERENCED_PARAMETER(InterruptNumber);
    UNREFERENCED_PARAMETER(ServiceRoutine);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Threads and Scheduling
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetThreadId
ACPI_THREAD_ID
AcpiOsGetThreadId (
    void)
{
    ACPI_THREAD_ID id;

    id = ThreadGetId(NULL) + 4;

    return id;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsExecute
ACPI_STATUS
AcpiOsExecute (
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Context);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWaitEventsComplete
void
AcpiOsWaitEventsComplete (
    void)
{
    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsSleep
void
AcpiOsSleep (
    UINT64                  Milliseconds)
{
    UNREFERENCED_PARAMETER(Milliseconds);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsStall
void
AcpiOsStall (
    UINT32                  Microseconds)
{
    UNREFERENCED_PARAMETER(Microseconds);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return;
}
#endif


/*
* Platform and hardware-independent I/O interfaces
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReadPort
ACPI_STATUS
AcpiOsReadPort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
    WORD port;

    LOG_FUNC_START;

    ASSERT(Address <= MAX_WORD);
    port = (WORD)Address;

    LOG("Will read port: 0x%X\n", Address );

    switch (Width)
    {
    case 8:
        *Value = __inbyte(port);
        break;
    case 16:
        *Value = __inword(port);
        break;
    case 32:
        *Value = __indword(port);
        break;
    }

    LOG("Read value: 0x%x\n", *Value);

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWritePort
ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
    WORD port;

    LOG_FUNC_START;

    ASSERT(Address <= MAX_WORD);
    port = (WORD)Address;

    LOG("Will write to port 0x%X value 0x%x\n", Address, Value);

    switch (Width)
    {
    case 8:
        __outbyte(port, Value);
        break;
    case 16:
        __outword(port, Value);
        break;
    case 32:
        __outdword(port, Value);
        break;
    }

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Platform and hardware-independent physical memory interfaces
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReadMemory
ACPI_STATUS
AcpiOsReadMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(Width);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWriteMemory
ACPI_STATUS
AcpiOsWriteMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(Width);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Platform and hardware-independent PCI configuration space access
* Note: Can't use "Register" as a parameter, changed to "Reg" --
* certain compilers complain.
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReadPciConfiguration
ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width)
{
    PCI_DEVICE_LOCATION pciDevice;
    STATUS status;

    if ((NULL == PciId) || (Reg > MAX_WORD) || (NULL == Value) || (Width > BITS_FOR_STRUCTURE(QWORD)))
    {
        return AE_BAD_PARAMETER;
    }

    LOG_FUNC_START;

    ASSERT( 0 == PciId->Segment );
    ASSERT(IsAddressAligned(Reg, Width/BITS_PER_BYTE));

    LOG_TRACE_ACPI("(%u.%u.%u) Register: 0x%x\n", PciId->Bus,
                   PciId->Device, PciId->Function, Reg );

    pciDevice.Bus = (BYTE)PciId->Bus;
    pciDevice.Device = (BYTE)PciId->Device;
    pciDevice.Function = (BYTE)PciId->Function;

    status = PciSystemReadConfigurationSpaceGeneric(pciDevice,
                                                    (WORD)Reg,
                                                    (BYTE)Width,
                                                    Value);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemReadConfigurationSpaceGeneric", status);
        return AE_ERROR;
    }

    LOG_TRACE_ACPI("Placed value: 0x%X [%u]\n", *Value, Width );

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWritePciConfiguration
ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  Value,
    UINT32                  Width)
{
    PCI_DEVICE_LOCATION pciDevice;
    STATUS status;

    if ((NULL == PciId) || (Reg > MAX_WORD) || (Width > BITS_FOR_STRUCTURE(QWORD)))
    {
        return AE_BAD_PARAMETER;
    }

    ASSERT(0 == PciId->Segment);
    ASSERT(IsAddressAligned(Reg, Width / BITS_PER_BYTE));

    memzero(&pciDevice, sizeof(PCI_DEVICE_LOCATION));
    status = STATUS_SUCCESS;

    LOG_FUNC_START;

    LOG_TRACE_ACPI("(%u.%u.%u) Register: 0x%x\n", PciId->Bus,
                   PciId->Device, PciId->Function, Reg);
    LOG_TRACE_ACPI("Value to write: 0x%X [%u]\n", Value, Width );

    pciDevice.Bus       = (BYTE) PciId->Bus;
    pciDevice.Device    = (BYTE) PciId->Device;
    pciDevice.Function  = (BYTE) PciId->Function;

    status = PciSystemWriteConfigurationSpaceGeneric(pciDevice,
                                                     (WORD)Reg,
                                                     (BYTE)Width,
                                                     Value);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemWriteConfigurationSpaceGeneric", status);
        return AE_ERROR;
    }

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Miscellaneous
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReadable
BOOLEAN
AcpiOsReadable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    UNREFERENCED_PARAMETER(Pointer);
    UNREFERENCED_PARAMETER(Length);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;
    return TRUE;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWritable
BOOLEAN
AcpiOsWritable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    UNREFERENCED_PARAMETER(Pointer);
    UNREFERENCED_PARAMETER(Length);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return TRUE;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetTimer
UINT64
AcpiOsGetTimer (
    void)
{
    UINT64 time;

    // RETURN VALUE
    // The current value of the system timer in 100-nanosecond
    // units.
    time = IomuGetSystemTimeUs() * ( US_IN_NS / 100);
    LOGL("Get time 0x%X\n", time );

    return time;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsSignal
ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Info);

    NOT_REACHED;

    return AE_OK;
}
#endif


/*
* Debug print routines
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsPrintf
void ACPI_INTERNAL_VAR_XFACE
AcpiOsPrintf (
    const char              *Format,
    ...)
{
    va_list Args;
    char buffer[MAX_PATH];

    va_start(Args,Format);
    vsnprintf(buffer, MAX_PATH, Format, Args );
    LOG_TRACE_ACPI("%s\n", buffer);

    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsVprintf
void
AcpiOsVprintf (
    const char              *Format,
    va_list                 Args)
{
    char buffer[MAX_PATH];

    vsnprintf(buffer, MAX_PATH, Format, Args );
    LOG_TRACE_ACPI("%s\n", buffer);

    return;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsRedirectOutput
void
AcpiOsRedirectOutput (
    void                    *Destination)
{
    UNREFERENCED_PARAMETER(Destination);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return;
}
#endif


/*
* Debug input
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetLine
ACPI_STATUS
AcpiOsGetLine (
    char                    *Buffer,
    UINT32                  BufferLength,
    UINT32                  *BytesRead)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(BytesRead);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Obtain ACPI table(s)
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetTableByName
ACPI_STATUS
AcpiOsGetTableByName (
    char                    *Signature,
    UINT32                  Instance,
    ACPI_TABLE_HEADER       **Table,
    ACPI_PHYSICAL_ADDRESS   *Address)
{
    UNREFERENCED_PARAMETER(Signature);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Address);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetTableByIndex
ACPI_STATUS
AcpiOsGetTableByIndex (
    UINT32                  Index,
    ACPI_TABLE_HEADER       **Table,
    UINT32                  *Instance,
    ACPI_PHYSICAL_ADDRESS   *Address)
{
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Address);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetTableByAddress
ACPI_STATUS
AcpiOsGetTableByAddress (
    ACPI_PHYSICAL_ADDRESS   Address,
    ACPI_TABLE_HEADER       **Table)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Table);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif


/*
* Directory manipulation
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsOpenDirectory
void *
AcpiOsOpenDirectory (
    char                    *Pathname,
    char                    *WildcardSpec,
    char                    RequestedFileType)
{
    UNREFERENCED_PARAMETER(Pathname);
    UNREFERENCED_PARAMETER(WildcardSpec);
    UNREFERENCED_PARAMETER(RequestedFileType);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return NULL;
}
#endif


#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetNextFilename
char *
AcpiOsGetNextFilename (
    void                    *DirHandle)
{
    UNREFERENCED_PARAMETER(DirHandle);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return NULL;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCloseDirectory
void
AcpiOsCloseDirectory (
    void                    *DirHandle)
{
    UNREFERENCED_PARAMETER(DirHandle);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return;
}
#endif


/*
* File I/O and related support
*/
#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsOpenFile
ACPI_FILE
AcpiOsOpenFile (
    const char              *Path,
    UINT8                   Modes)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Modes);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return NULL;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsCloseFile
void
AcpiOsCloseFile (
    ACPI_FILE               File);
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsReadFile
int
AcpiOsReadFile (
    ACPI_FILE               File,
    void                    *Buffer,
    ACPI_SIZE               Size,
    ACPI_SIZE               Count)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Count);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return 0;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsWriteFile
int
AcpiOsWriteFile (
    ACPI_FILE               File,
    void                    *Buffer,
    ACPI_SIZE               Size,
    ACPI_SIZE               Count)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Count);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return 0;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsGetFileOffset
long
AcpiOsGetFileOffset (
    ACPI_FILE               File)
{
    UNREFERENCED_PARAMETER(File);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return 0;
}
#endif

#ifndef ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsSetFileOffset
ACPI_STATUS
AcpiOsSetFileOffset (
    ACPI_FILE               File,
    long                    Offset,
    UINT8                   From)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(From);

    LOG_FUNC_START;

    NOT_REACHED;

    LOG_FUNC_END;

    return AE_OK;
}
#endif

#undef memset
#ifdef NDEBUG
#pragma function(memset)
#endif
_At_buffer_(address, i, size, _Post_satisfies_(((PBYTE)address)[i] == value))
void*
memset(
    OUT_WRITES_BYTES_ALL(size)  PVOID address,
    IN                          BYTE value,
    IN                          DWORD size
)
{
    cl_memset(address, value, size);

    return address;
}
