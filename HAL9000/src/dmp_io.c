#include "HAL9000.h"
#include "dmp_io.h"

#include "../../Volume/headers/volume_structures.h"
#include "filesystem.h"

#define DEVICE_TYPE_MAX_NAME_LEN            25

static const char DEVICE_TYPE_NAMES[][DEVICE_TYPE_MAX_NAME_LEN] = {
    "System",
    "Harddisk Controller",
    "Disk",
    "Volume",
    "File System",
    "Physical Netcard",
};

__forceinline
static
const
char*
_PartitionTypeToString(
    IN      BYTE            PartitionType
    )
{
    switch (PartitionType)
    {
    case PARTITION_TYPE_NTFS:
        return "NTFS";
    case PARTITION_TYPE_FAT_CHS:
        return "FAT-CHS";
    case PARTITION_TYPE_FAT_LBA:
        return "FAT-LBA";
    case PARTITION_TYPE_FAT16_LBA:
        return "FAT16-LBA";
    case PARTITION_TYPE_LINUX_SWAP:
        return "LINUX-SWP";
    case PARTITION_TYPE_LINUX_NATIVE:
        return "LINUX-NAT";
    case PARTITION_TYPE_MICROSOFT_PROTECTIVE_MBR:
        return "MS PRT MBR";
    case PARTITION_TYPE_INTEL_PROTECTIVE_MBR:
        return "IT PRT MBR";
    default:
        return "UNKNOWN";
    }
}

__forceinline
static
const
char*
_DeviceTypeToString(
    IN      DEVICE_TYPE     DeviceType
    )
{
    return DEVICE_TYPE_NAMES[DeviceType];
}

STATUS
(__cdecl DumpVpb) (
    IN      PLIST_ENTRY ListEntry,
    IN_OPT  PVOID       FunctionContext
    )
{
    PVPB pVpb;
    PVOLUME pVolume;

    ASSERT(NULL != ListEntry);
    ASSERT(NULL == FunctionContext);

    pVpb = CONTAINING_RECORD(ListEntry, VPB, NextVpb);

    pVolume = IoGetDeviceExtension(pVpb->VolumeDevice);
    ASSERT(NULL != pVolume);

    LOG("%c:\\%4c", pVpb->VolumeLetter, '|');
    LOG("%10s(0x%02x)%c", _PartitionTypeToString(pVolume->PartitionInformation.PartitionType), pVolume->PartitionInformation.PartitionType, '|');
    LOG("%9s%c", pVpb->Flags.Mounted ? "YES" : "NO", '|');
    LOG("%9s%c", pVolume->PartitionInformation.Bootable ? "YES" : "NO", '|');
    LOG("%16X%c", pVolume->PartitionInformation.OffsetInDisk * SECTOR_SIZE, '|');
    LOG("%16X%c", pVolume->PartitionInformation.PartitionSize * SECTOR_SIZE, '|');

    LOG("\n");

    return STATUS_SUCCESS;
}

void
DumpDriverList(
    IN      PLIST_ENTRY     DriverList
    )
{
    PDRIVER_OBJECT pDriverObject;
    PLIST_ENTRY pCurEntry;

    if (NULL == DriverList)
    {
        return;
    }


    for(pCurEntry = DriverList->Flink;
        pCurEntry != DriverList;
        pCurEntry = pCurEntry->Flink)
    {
        pDriverObject = CONTAINING_RECORD(pCurEntry, DRIVER_OBJECT, NextDriver);

        LOG("\n");
        DumpDriver(pDriverObject);
    }
}

void
DumpDriver(
    IN      PDRIVER_OBJECT  Driver
    )
{
    DWORD i;

    if (NULL == Driver)
    {
        return;
    }
    
    LOG("Driver at 0x%X\n", Driver);
    LOG("Name: %s\n", Driver->DriverName);
    LOG("Dispatch functions:\n");
    for (i = 0; i < IRP_MJ_MAX; ++i)
    {
        LOG("DispatchFunction[%u] = 0x%X\n", i, Driver->DispatchFunctions[i]);
    }

    LOG("Device number: %d\n", Driver->NoOfDevices);
    if (0 != Driver->NoOfDevices)
    {
        LOG("Device list:\n");
        DumpDeviceList(&Driver->DeviceList);
    }
}

void
DumpDeviceList(
    IN      PLIST_ENTRY     DeviceList
    )
{
    PDEVICE_OBJECT pDeviceObject;
    PLIST_ENTRY pCurEntry;

    if (NULL == DeviceList)
    {
        return;
    }

    for(pCurEntry = DeviceList->Flink;
        pCurEntry != DeviceList;
        pCurEntry = pCurEntry->Flink)
    {
        pDeviceObject = CONTAINING_RECORD(pCurEntry, DEVICE_OBJECT, NextDevice);

        DumpDevice(pDeviceObject);
        LOG("\n");
    }
}

void
DumpDevice(
    IN      PDEVICE_OBJECT  Device
    )
{
    if (NULL == Device)
    {
        return;
    }

    LOG("Device at 0x%X\n", Device);
    LOG("Device type: %s\n", _DeviceTypeToString(Device->DeviceType));
    LOG("Stack size: %d\n", Device->StackSize);
    LOG("Device extension size: 0x%x\n", Device->DeviceExtensionSize);
    LOG("Attached to device: 0x%X\n", Device->AttachedDevice);
}