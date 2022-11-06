#include "HAL9000.h"
#include "dmp_mbr.h"
#include "dmp_memory.h"

static
void
_DumpMbrPartitionEntry(
    IN      PMBR_PARTITION_ENTRY        PartitionEntry
    )
{
    if (NULL == PartitionEntry)
    {
        return;
    }

    LOG("Status: 0x%x\n", PartitionEntry->Status);
    LOG("Partition type: 0x%x\n", PartitionEntry->PartitionType);
    LOG("First sector (LBA): 0x%x\n", PartitionEntry->FirstSectorLBA);
    LOG("Number of sectors: 0x%x\n", PartitionEntry->NumberOfSectors);
}

void
DumpMbr(
    IN      PMBR                        Mbr
    )
{
    DWORD i;

    if (NULL == Mbr)
    {
        return;
    }

    for (i = 0; i < MBR_NO_OF_PARTITIONS; ++i)
    {
        LOG("Partition %d\n", i);
        _DumpMbrPartitionEntry(&Mbr->Partitions[i]);
    }

    LOG("MBR signature: 0x%x\n", Mbr->BootSignature);

    DumpMemory(Mbr, 0, sizeof(MBR), TRUE, TRUE );
}