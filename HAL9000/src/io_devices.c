#include "HAL9000.h"
#include "io.h"

#include "volume.h"
#include "iomu.h"
#include "filesystem.h"
#include "mdl.h"
#include "isr.h"
#include "mmu.h"
#include "vmm.h"
#include "os_time.h"

/// TODO: These function calls cross trust boundaries, validate parameters
/// and do not ASSERT
__forceinline
static
BOOLEAN
_IoIsValidDeviceType(
    IN      DEVICE_TYPE     DeviceType
    )
{
    return ((DeviceTypeMin <= DeviceType) && (DeviceType <= DeviceTypeMax));
}

static
STATUS
_IoReadWriteDevice(
    IN          PDEVICE_OBJECT          DeviceObject,
    _When_(Write,OUT_WRITES_BYTES(*Length))
    _When_(!Write,IN_READS_BYTES(*Length))
                PVOID                   Buffer,
    INOUT       QWORD*                  Length,
    IN          QWORD                   Offset,
    IN          BOOLEAN                 Write,
    IN          BOOLEAN                 Asynchronous
    );

static
void
_IoAllocateVpb(
    INOUT   PDEVICE_OBJECT  VolumeDevice
    );

PTR_SUCCESS
PDEVICE_OBJECT
IoCreateDevice(
    INOUT   PDRIVER_OBJECT  DriverObject,
    IN      DWORD           DeviceExtensionSize,
    IN      DEVICE_TYPE     DeviceType
    )
{
    PDEVICE_OBJECT pDevice;
    STATUS status;

    ASSERT(NULL != DriverObject);

    ASSERT(_IoIsValidDeviceType(DeviceType));

    pDevice = NULL;
    status = STATUS_SUCCESS;

    __try
    {
        pDevice = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(DEVICE_OBJECT), HEAP_DEVICE_TAG, 0);
        if (NULL == pDevice)
        {
            status = STATUS_HEAP_NO_MORE_MEMORY;
            __leave;
        }

        pDevice->DeviceExtensionSize = DeviceExtensionSize;
        if (0 != DeviceExtensionSize)
        {
            pDevice->DeviceExtension = ExAllocatePoolWithTag(PoolAllocateZeroMemory, DeviceExtensionSize, HEAP_DEVICE_EXT_TAG, 0);
            if (NULL == pDevice->DeviceExtension)
            {
                status = STATUS_HEAP_NO_MORE_MEMORY;
                __leave;
            }
        }
        pDevice->DeviceType = DeviceType;

        if (DeviceTypeVolume == pDevice->DeviceType)
        {
            // we need to allocate a volume parameter block
            _IoAllocateVpb(pDevice);
        }

        pDevice->StackSize = 1;

        MutexInit(&pDevice->DeviceLock, FALSE);

        // insert device into list
        /// TODO: need to lock
        /// Actually this is debatable: the IoCreateDevice should normally be called only in the DriverEntry portion
        /// of the driver which is single-threaded. Because we have no PNP support (ATM) I see no way in which we could
        /// have a problem by not synchronizing access to this list.

        InsertTailList(&DriverObject->DeviceList, &pDevice->NextDevice);
        DriverObject->NoOfDevices = DriverObject->NoOfDevices + 1;

        // set driver object
        pDevice->DriverObject = DriverObject;
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (NULL != pDevice)
            {
                if (0 != pDevice->DeviceExtension)
                {
                    ExFreePoolWithTag(pDevice->DeviceExtension, HEAP_DEVICE_EXT_TAG);
                    pDevice->DeviceExtension = NULL;
                }

                ExFreePoolWithTag(pDevice, HEAP_DEVICE_TAG);
                pDevice = NULL;
            }
        }
    }

    // if we failed => pDevice will be NULL,
    // else it will point to the new device object
    return pDevice;
}

PVOID
IoGetDeviceExtension(
    IN      PDEVICE_OBJECT      Device
    )
{
    ASSERT(NULL != Device);

    return Device->DeviceExtension;
}

void
IoDeleteDevice(
    INOUT   PDEVICE_OBJECT      Device
    )
{
    PDRIVER_OBJECT pDriver;

    ASSERT(NULL != Device);

    pDriver = Device->DriverObject;

    ASSERT(NULL != pDriver);

    RemoveEntryList(&Device->NextDevice);
    pDriver->NoOfDevices = pDriver->NoOfDevices - 1;

    if (0 != Device->DeviceExtensionSize)
    {
        ASSERT(NULL != Device->DeviceExtension);

        ExFreePoolWithTag(Device->DeviceExtension, HEAP_DEVICE_EXT_TAG);
        Device->DeviceExtension = NULL;
    }

    ExFreePoolWithTag(Device, HEAP_DEVICE_TAG);
    Device = NULL;
}

PTR_SUCCESS
PDRIVER_OBJECT
IoCreateDriver(
    IN_Z        char*                   DriverName,
    IN          PFUNC_DriverEntry       DriverEntry
    )
{
    STATUS status;
    PDRIVER_OBJECT pDriver;
    DWORD drvNameLength;

    ASSERT(NULL != DriverName);
    ASSERT(NULL != DriverEntry);

    pDriver = NULL;
    status = STATUS_SUCCESS;
    drvNameLength = INVALID_STRING_SIZE;

    __try
    {
        pDriver = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(DRIVER_OBJECT), HEAP_DRIVER_TAG, 0);
        if (NULL == pDriver)
        {
            status = STATUS_HEAP_NO_MORE_MEMORY;
            __leave;
        }

        drvNameLength = strlen(DriverName);
        ASSERT(INVALID_STRING_SIZE != drvNameLength);

        pDriver->DriverName = ExAllocatePoolWithTag(PoolAllocateZeroMemory, drvNameLength + 1, HEAP_DRIVER_TAG, 0);
        if (NULL == pDriver->DriverName)
        {
            status = STATUS_HEAP_NO_MORE_MEMORY;
            __leave;
        }
        strcpy(pDriver->DriverName, DriverName);

        InitializeListHead(&pDriver->DeviceList);

        // call driver entry
        status = DriverEntry(pDriver);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("DriverEntry", status);
            __leave;
        }

        IomuDriverInstalled(pDriver);
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (NULL != pDriver)
            {
                if (NULL != pDriver->DriverName)
                {
                    ExFreePoolWithTag(pDriver->DriverName, HEAP_DRIVER_TAG);
                    pDriver->DriverName = NULL;
                }

                ExFreePoolWithTag(pDriver, HEAP_DRIVER_TAG);
                pDriver = NULL;
            }
        }
    }

    return pDriver;
}

PVOID
IoGetDriverExtension(
    IN          PDEVICE_OBJECT  Device
    )
{
    ASSERT( NULL != Device );
    ASSERT( NULL != Device->DriverObject );

    return Device->DriverObject->DriverExtension;
}

void
IoAttachDevice(
    INOUT   PDEVICE_OBJECT  SourceDevice,
    IN      PDEVICE_OBJECT  TargetDevice
    )
{
    ASSERT(NULL != SourceDevice);
    ASSERT(NULL != TargetDevice);

    ASSERT(NULL == SourceDevice->AttachedDevice);
    SourceDevice->AttachedDevice = TargetDevice;

    SourceDevice->StackSize = TargetDevice->StackSize + 1;
}

PTR_SUCCESS
PIRP
IoAllocateIrp(
    IN      BYTE            StackSize
    )
{
    PIRP pIrp;
    DWORD irpSize;

    ASSERT(StackSize > 0);

    pIrp = NULL;
    irpSize = sizeof(IRP) + StackSize * sizeof(IO_STACK_LOCATION);

    LOG_TRACE_IO("Irp has %d stack locations\n", StackSize);

    pIrp = ExAllocatePoolWithTag(PoolAllocateZeroMemory, irpSize, HEAP_IRP_TAG, 0);
    if (NULL == pIrp)
    {
        LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", irpSize );
        return NULL;
    }

    // set current stack location
    // this is intentionally not set to StackSize - 1 because
    // at each call to IoCallDriver it is decremented => the
    // current stack location will properly point to StackSize - 1
    pIrp->CurrentStackLocation = StackSize;

    LOG_FUNC_END;

    return pIrp;
}

void
IoFreeIrp(
    IN      PIRP            Irp
    )
{
    ASSERT(NULL != Irp);

    if (NULL != Irp->Mdl)
    {
        IoFreeMdl(Irp->Mdl);
        Irp->Mdl = NULL;
    }

    ExFreePoolWithTag(Irp, HEAP_IRP_TAG);
}

PTR_SUCCESS
PIO_STACK_LOCATION
IoGetCurrentIrpStackLocation(
    IN      PIRP            Irp
    )
{
    ASSERT(NULL != Irp);

    ASSERT(MAX_BYTE != Irp->CurrentStackLocation);

    return &Irp->StackLocations[Irp->CurrentStackLocation];
}

PTR_SUCCESS
PIO_STACK_LOCATION
IoGetNextIrpStackLocation(
    IN      PIRP            Irp
    )
{
    ASSERT(NULL != Irp);

    ASSERT(0 != Irp->CurrentStackLocation);

    return &Irp->StackLocations[Irp->CurrentStackLocation - 1];
}


void
IoCopyCurrentStackLocationToNext(
    INOUT   PIRP            Irp
    )
{
    BYTE currentStackLocation;

    ASSERT(NULL != Irp);

    currentStackLocation = Irp->CurrentStackLocation;
    ASSERT(0 != currentStackLocation);

    LOG_TRACE_IO("Current stack location: %d\n", currentStackLocation);

    memcpy(&Irp->StackLocations[currentStackLocation - 1], &Irp->StackLocations[currentStackLocation], sizeof(IO_STACK_LOCATION));
}

STATUS
IoCallDriver(
    IN      PDEVICE_OBJECT  Device,
    INOUT   PIRP            Irp
    )
{
    STATUS status;
    PIO_STACK_LOCATION pStackLocation;
    PDRIVER_OBJECT pDriver;
    PFUNC_DriverDispatch pDispatchFunction;

    ASSERT(NULL != Device);
    ASSERT(NULL != Irp);

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pStackLocation = NULL;

    ASSERT(0 != Irp->CurrentStackLocation);
    Irp->CurrentStackLocation = Irp->CurrentStackLocation - 1;

    pStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if ((IRP_MJ_READ == pStackLocation->MajorFunction) || (IRP_MJ_WRITE == pStackLocation->MajorFunction))
    {
        if (!IsAddressAligned(pStackLocation->Parameters.ReadWrite.Length, Device->DeviceAlignment))
        {
            // read/write length not aligned to device requirement
            LOG_ERROR("ReadWrite length does not satisfy alignment requirement\nRequested: 0x%X, Required: 0x%x\n", pStackLocation->Parameters.ReadWrite.Length, Device->DeviceAlignment);
            return STATUS_DEVICE_ALIGNMENT_NO_SATISFIED;
        }

        if (!IsAddressAligned(pStackLocation->Parameters.ReadWrite.Offset, Device->DeviceAlignment))
        {
            // read/write offset not aligned to device requirement
            LOG_ERROR("ReadWrite offset does not satisfy alignment requirement\nRequested: 0x%X, Required: 0x%x\n", pStackLocation->Parameters.ReadWrite.Offset, Device->DeviceAlignment);
            return STATUS_DEVICE_ALIGNMENT_NO_SATISFIED;
        }
    }

    pDriver = Device->DriverObject;
    ASSERT(NULL != pDriver);

    pDispatchFunction = pDriver->DispatchFunctions[pStackLocation->MajorFunction];
    if (NULL == pDispatchFunction)
    {
        status = STATUS_DEVICE_INVALID_OPERATION;
    }
    else
    {
        MutexAcquire(&Device->DeviceLock);
        status = pDispatchFunction(Device, Irp);
        MutexRelease(&Device->DeviceLock);
    }
    if (!SUCCEEDED(status))
    {
        return status;
    }

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

void
IoCompleteIrp(
    INOUT   PIRP            Irp
    )
{
    ASSERT(NULL != Irp);
    ASSERT(FALSE == Irp->Flags.Completed);

    Irp->Flags.Completed = TRUE;
}

static
STATUS
_IoReadWriteDevice(
    IN          PDEVICE_OBJECT          DeviceObject,
    _When_(!Write,OUT_WRITES_BYTES(*Length))
    _When_(Write,IN_READS_BYTES(*Length))
                PVOID                   Buffer,
    INOUT       QWORD*                  Length,
    IN          QWORD                   Offset,
    IN          BOOLEAN                 Write,
    IN          BOOLEAN                 Asynchronous
    )
{
    STATUS status;
    PIRP pIrp;
    PIO_STACK_LOCATION pStackLocation;

    ASSERT(NULL != DeviceObject);
    ASSERT(NULL != Buffer);
    ASSERT(NULL != Length);

    status = STATUS_SUCCESS;
    pIrp = NULL;
    pStackLocation = NULL;

    pIrp = IoAllocateIrp(DeviceObject->StackSize);
    ASSERT(NULL != pIrp);

    pStackLocation = IoGetNextIrpStackLocation(pIrp);
    pStackLocation->MajorFunction = Write ? IRP_MJ_WRITE : IRP_MJ_READ;

    pIrp->Buffer = Buffer;
    pIrp->Flags.Asynchronous = Asynchronous;

    pStackLocation->Parameters.ReadWrite.Length = *Length;
    pStackLocation->Parameters.ReadWrite.Offset = Offset;

    __try
    {
        status = IoCallDriver(DeviceObject, pIrp);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoCallDriver", status);
            __leave;
        }

        *Length = pIrp->IoStatus.Information;
        status = pIrp->IoStatus.Status;
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoCallDriver", status);
            __leave;
        }
    }
    __finally
    {
        if (NULL != pIrp)
        {
            IoFreeIrp(pIrp);
            pIrp = NULL;
        }

        LOG_FUNC_END;
    }

    return status;
}

static
void
_IoAllocateVpb(
    INOUT   PDEVICE_OBJECT  VolumeDevice
    )
{
    PVPB pVpb;

    ASSERT(NULL != VolumeDevice);

    ASSERT(DeviceTypeVolume == VolumeDevice->DeviceType);

    pVpb = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(VPB), HEAP_VPB_TAG, 0);
    ASSERT(NULL != pVpb);

    pVpb->VolumeDevice = VolumeDevice;
    VolumeDevice->Vpb = pVpb;

    IomuNewVpbCreated(pVpb);
}

STATUS
IoGetPciDevicesMatchingSpecification(
    IN          PCI_SPEC        Specification,
    _When_(*NumberOfDevices > 0, OUT_PTR)
    _When_(*NumberOfDevices == 0, OUT_PTR_MAYBE_NULL)
                PPCI_DEVICE_DESCRIPTION**    PciDevices,
    OUT         DWORD*           NumberOfDevices
    )
{
    STATUS status;
    PLIST_ENTRY pDeviceList;
    DWORD noOfDevices;
    DWORD temp;
    PPCI_DEVICE_DESCRIPTION* pDevices;

    ASSERT(NULL != PciDevices);
    ASSERT(NULL != NumberOfDevices);

    status = STATUS_SUCCESS;
    pDeviceList = IomuGetPciDeviceList();
    noOfDevices = 0;
    pDevices = NULL;

    status = PciSystemFindDevicesMatchingSpecification(pDeviceList, Specification, NULL, &noOfDevices );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciDevFindDevicesByClassAndSubclass", status);
        return status;
    }

    __try
    {
        if (0 == noOfDevices)
        {
            status = STATUS_SUCCESS;
            __leave;
        }

        pDevices = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PPCI_DEVICE_DESCRIPTION) * noOfDevices, HEAP_TEMP_TAG, 0);
        if (NULL == pDevices)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(PPCI_DEVICE_DESCRIPTION) * noOfDevices);
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        temp = noOfDevices;
        status = PciSystemFindDevicesMatchingSpecification(pDeviceList, Specification, pDevices, &temp);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("PciDevFindDevicesByClassAndSubclass", status);
            __leave;
        }
        ASSERT(temp == noOfDevices);
    }
    __finally
    {
        if (SUCCEEDED(status))
        {
            *NumberOfDevices = noOfDevices;
            *PciDevices = pDevices;
        }
        else
        {
            if( NULL != pDevices )
            {
                ExFreePoolWithTag(pDevices, HEAP_TEMP_TAG );
                pDevices = NULL;
            }
        }
    }

    return status;
}

STATUS
IoGetPciDevicesMatchingLocation(
    IN          PCI_SPEC_LOCATION           Specification,
    _When_(*NumberOfDevices > 0, OUT_PTR)
    _When_(*NumberOfDevices == 0, OUT_PTR_MAYBE_NULL)
                PPCI_DEVICE_DESCRIPTION**   PciDevices,
    OUT         DWORD*                      NumberOfDevices
    )
{
    STATUS status;
    PLIST_ENTRY pDeviceList;
    DWORD noOfDevices;
    DWORD temp;
    PPCI_DEVICE_DESCRIPTION* pDevices;

    ASSERT(NULL != PciDevices);
    ASSERT(NULL != NumberOfDevices);

    status = STATUS_SUCCESS;
    pDeviceList = IomuGetPciDeviceList();
    noOfDevices = 0;
    pDevices = NULL;

    status = PciSystemFindDevicesMatchingLocation(pDeviceList, Specification, NULL, &noOfDevices );
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciDevFindDevicesByClassAndSubclass", status);
        return status;
    }

    __try
    {
        if (0 == noOfDevices)
        {
            status = STATUS_SUCCESS;
            __leave;
        }

        pDevices = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PPCI_DEVICE_DESCRIPTION) * noOfDevices, HEAP_TEMP_TAG, 0);
        if (NULL == pDevices)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatePoolWithTag", sizeof(PPCI_DEVICE_DESCRIPTION) * noOfDevices);
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            __leave;
        }

        temp = noOfDevices;
        status = PciSystemFindDevicesMatchingLocation(pDeviceList, Specification, pDevices, &temp);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("PciDevFindDevicesByClassAndSubclass", status);
            __leave;
        }
        ASSERT(temp == noOfDevices);
    }
    __finally
    {
        if (SUCCEEDED(status))
        {
            *NumberOfDevices = noOfDevices;
            *PciDevices = pDevices;
        }
        else
        {
            if( NULL != pDevices )
            {
                ExFreePoolWithTag(pDevices, HEAP_TEMP_TAG );
                pDevices = NULL;
            }
        }
    }

    return status;
}

STATUS
IoGetPciSecondaryBusForBridge(
    IN          PCI_DEVICE_LOCATION         DeviceLocation,
    OUT         BYTE*                       Bus
    )
{
    STATUS status;
    PLIST_ENTRY pDeviceList;
    DWORD noOfDevices;
    PPCI_DEVICE_DESCRIPTION device;
    PCI_SPEC_LOCATION pciLocation;

    ASSERT(NULL != Bus);

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pDeviceList = IomuGetPciDeviceList();
    noOfDevices = 0;
    memzero(&pciLocation, sizeof(PCI_SPEC_LOCATION));

    LOG_TRACE_IO("Will search for device at (%u.%u.%u)\n",
        DeviceLocation.Bus, DeviceLocation.Device, DeviceLocation.Function);

    memcpy(&pciLocation.Location, (const PVOID) &DeviceLocation, sizeof(PCI_DEVICE_LOCATION));
    pciLocation.MatchBus = pciLocation.MatchDevice = pciLocation.MatchFunction = TRUE;

    status = PciSystemFindDevicesMatchingLocation(pDeviceList, pciLocation, &device, &noOfDevices);
    if(!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("PciSystemFindDevicesMatchingLocation", status);
        return status;
    }
    ASSERT(noOfDevices == 1);

    if(device->DeviceData->Header.HeaderType.Layout != PCI_HEADER_LAYOUT_PCI_TO_PCI)
    {
        LOG_ERROR("Only bridges have devices attached to their secondary bus!\n");
        return STATUS_DEVICE_TYPE_INVALID;
    }

    *Bus = device->DeviceData->Header.Bridge.SecondaryBusNumber;

    LOG_FUNC_END;

    return status;
}

STATUS
IoGetDevicesByType(
    IN                      DEVICE_TYPE         DeviceType,
    _When_(*NumberOfDevices > 0, OUT_PTR)
    _When_(*NumberOfDevices == 0, OUT_PTR_MAYBE_NULL)
    PDEVICE_OBJECT**    DeviceObjects,
    OUT                     DWORD*              NumberOfDevices
    )
{
    ASSERT(_IoIsValidDeviceType(DeviceType));

    // warning C6001: Using uninitialized memory (this warning is a caused by a SAL bug)
    // NumberOfDevices is an OUT parameter to IomuGetDevicesByType as well
#pragma warning(suppress: 6001)
    return IomuGetDevicesByType(DeviceType, DeviceObjects, NumberOfDevices);
}

void
IoFreeTemporaryData(
    IN          PVOID               Data
    )
{
    ASSERT(NULL != Data);

    LOG_FUNC_START;

    ExFreePoolWithTag(Data, HEAP_TEMP_TAG);

    LOG_FUNC_END;
}

PTR_SUCCESS
PIRP
// Warning C6101 Returning uninitialized memory '*OutputBuffer'. A successful path through
// the function does not set the named _Out_ parameter.
// I'm really not proud of this, but there is no other way to tell SAL what's going on
// and Microsoft does the exact same hack... You learn from the best!
#pragma warning(suppress: 6101)
IoBuildDeviceIoControlRequest(
    IN          DWORD            IoControlCode,
    IN          PDEVICE_OBJECT   DeviceObject,
    IN_OPT      PVOID            InputBuffer,
    IN          DWORD            InputBufferLength,
    OUT_OPT     PVOID            OutputBuffer,
    IN          DWORD            OutputBufferLength
    )
{
    PIRP pIrp;
    PIO_STACK_LOCATION pStackLocation;

    ASSERT(NULL != DeviceObject);

    pIrp = IoAllocateIrp(DeviceObject->StackSize);
    if (NULL == pIrp)
    {
        LOG_ERROR("IoAllocateIrp failed!\n");
        return NULL;
    }

    pStackLocation = IoGetNextIrpStackLocation(pIrp);

    pStackLocation->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    pStackLocation->Parameters.DeviceControl.IoControlCode = IoControlCode;
    pStackLocation->Parameters.DeviceControl.InputBufferLength = InputBufferLength;
    pStackLocation->Parameters.DeviceControl.OutputBufferLength = OutputBufferLength;
    pStackLocation->Parameters.DeviceControl.OutputBuffer = OutputBuffer;

    pIrp->Buffer = InputBuffer;

    return pIrp;
}

STATUS
IoReadDeviceEx(
    IN                          PDEVICE_OBJECT          DeviceObject,
    OUT_WRITES_BYTES(*Length)   PVOID                   Buffer,
    INOUT                       QWORD*                  Length,
    IN                          QWORD                   Offset,
    IN                          BOOLEAN                 Asynchronous
    )
{
    LOG_FUNC_START;

    return _IoReadWriteDevice(DeviceObject, Buffer, Length, Offset, FALSE, Asynchronous);
}

STATUS
IoWriteDeviceEx(
    IN                          PDEVICE_OBJECT          DeviceObject,
    IN_READS_BYTES(*Length)     PVOID                   Buffer,
    INOUT                       QWORD*                  Length,
    IN                          QWORD                   Offset,
    IN                          BOOLEAN                 Asynchronous
    )
{
    LOG_FUNC_START;

    return _IoReadWriteDevice(DeviceObject, Buffer, Length, Offset, TRUE, Asynchronous);
}

STATUS
IoAllocateMdl(
    IN          PVOID           VirtualAddress,
    IN          DWORD           Length,
    IN_OPT      PIRP            Irp,
    OUT_PTR     PMDL*           Mdl
    )
{
    PMDL pMdl;

    LOG_FUNC_START;

    if (NULL == VirtualAddress)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (0 == Length)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (NULL == Mdl)
    {
        return STATUS_INVALID_PARAMETER4;
    }

    pMdl = MdlAllocate(VirtualAddress, Length );
    if (NULL == pMdl)
    {
        LOG_ERROR("MdlAllocate failed!\n");
        return STATUS_UNSUCCESSFUL;
    }

    if (NULL != Irp)
    {
        // set MDL
        Irp->Mdl = pMdl;
    }

    *Mdl = pMdl;

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}

void
IoFreeMdl(
    INOUT       PMDL            Mdl
    )
{
    LOG_FUNC_START;

    ASSERT( NULL != Mdl );

    MdlFree(Mdl);

    LOG_FUNC_END;
}

SIZE_SUCCESS
DWORD
IoMdlGetNumberOfPairs(
    IN          PMDL            Mdl
    )
{
    if (NULL == Mdl)
    {
        return INVALID_STRING_SIZE;
    }

    return MdlGetNumberOfPairs(Mdl);
}

PTR_SUCCESS
PMDL_TRANSLATION_PAIR
IoMdlGetTranslationPair(
    IN          PMDL            Mdl,
    IN          DWORD           Index
    )
{
    if (NULL == Mdl)
    {
        return NULL;
    }

    return MdlGetTranslationPair(Mdl, Index);
}

STATUS
IoRegisterInterruptEx(
    IN          PIO_INTERRUPT           Interrupt,
    IN_OPT      PDEVICE_OBJECT          DeviceObject,
    OUT_OPT     PBYTE                   Vector
    )
{
    STATUS status;

    if (NULL == Interrupt)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    status = IomuRegisterInterrupt(Interrupt,DeviceObject, Vector);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IomuRegisterInterrupt", status );
        return status;
    }

    return status;
}

PTR_SUCCESS
PVOID
IoMapMemory(
    IN      PHYSICAL_ADDRESS        PhysicalAddress,
    IN      DWORD                   Size,
    IN      PAGE_RIGHTS             PageRights
    )
{
    if ( (NULL == PhysicalAddress) || ( 0 == Size ) )
    {
        return NULL;
    }

    return MmuMapMemoryEx(PhysicalAddress, Size, PageRights, TRUE, TRUE, NULL );
}

void
IoUnmapMemory(
    IN      PVOID                   VirtualAddress,
    IN      DWORD                   Size
    )
{
    ASSERT( NULL != VirtualAddress );
    ASSERT( 0 != Size );

    MmuUnmapSystemMemory(VirtualAddress, Size );
}

PTR_SUCCESS
PHYSICAL_ADDRESS
IoGetPhysicalAddress(
    IN      PVOID                   VirtualAddress
    )
{
    if (NULL == VirtualAddress)
    {
        return NULL;
    }

    return MmuGetPhysicalAddress(VirtualAddress);
}

PTR_SUCCESS
PVOID
IoAllocateContinuousMemoryEx(
    IN      DWORD                   AllocationSize,
    IN      BOOLEAN                 Uncacheable
    )
{
    if (0 == AllocationSize)
    {
        return NULL;
    }

    return VmmAllocRegionEx(NULL,
                            AllocationSize,
                            VMM_ALLOC_TYPE_RESERVE | VMM_ALLOC_TYPE_COMMIT | VMM_ALLOC_TYPE_NOT_LAZY,
                            PAGE_RIGHTS_READWRITE,
                            Uncacheable,
                            NULL,
                            NULL,
                            NULL,
                            NULL
                            );
}

void
IoFreeContinuousMemory(
    IN      PVOID                   VirtualAddress
    )
{
    ASSERT( NULL != VirtualAddress );

    VmmFreeRegion(VirtualAddress, 0, VMM_FREE_TYPE_RELEASE );
}


DATETIME
IoGetCurrentDateTime(
    void
    )
{
    return OsTimeGetCurrentDateTime();
}
