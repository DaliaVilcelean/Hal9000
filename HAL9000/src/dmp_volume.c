#include "HAL9000.h"
#include "dmp_volume.h"

void
DumpVolume(
    IN      PVOLUME     Volume
    )
{
    if (NULL == Volume)
    {
        return;
    }


    LOG("Disk device: 0x%X\n", Volume->DiskDevice);
    LOG("Partition type: 0x%x\n", Volume->PartitionInformation.PartitionType);
    LOG("Offset in disk: 0x%x\n", Volume->PartitionInformation.OffsetInDisk);
    LOG("Size in sectors: 0x%x\n", Volume->PartitionInformation.PartitionSize);
}