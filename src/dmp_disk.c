#include "HAL9000.h"
#include "pci.h"
#include "list.h"
#include "dmp_disk.h"
#include "dmp_volume.h"
#include "dmp_common.h"

static FUNC_ListFunction        _DumpDiskVolume;

void
DumpDisk(
    IN      PDISK_OBJECT        Disk
    )
{
    DWORD i;
    INTR_STATE intrState;

    ASSERT( NULL != Disk );

    intrState = DumpTakeLock();
    LOG("Number of sectors: 0x%X\n", Disk->NumberOfSectors);
    LOG("Number of partitions: %d\n", Disk->DiskLayout->NumberOfPartitions);

    for (i = 0; i < Disk->DiskLayout->NumberOfPartitions; ++i)
    {
        DumpPartition(&Disk->DiskLayout->Partitions[i]);
    }
    DumpReleaseLock(intrState);
}

void
DumpPartition(
    IN      PPARTITION_INFORMATION  Partition
    )
{
    ASSERT( NULL != Partition );

    LOG("Partition offset: 0x%X\n", Partition->OffsetInDisk);
    LOG("Partition size: 0x%X\n", Partition->PartitionSize);
    LOG("Partition type: 0x%x\n", Partition->PartitionType);
    LOG("Partition bootable: 0x%x\n", Partition->Bootable);
}

STATUS
(__cdecl _DumpDiskVolume) (
    IN      PLIST_ENTRY ListEntry,
    IN_OPT  PVOID       FunctionContext
    )
{
    PVOLUME_LIST_ENTRY pVolumeEntry;

    ASSERT(NULL != ListEntry);
    ASSERT(NULL == FunctionContext);

    pVolumeEntry = CONTAINING_RECORD(ListEntry, VOLUME_LIST_ENTRY, ListEntry);
    DumpVolume(IoGetDeviceExtension(pVolumeEntry->Volume));


    return STATUS_SUCCESS;
}