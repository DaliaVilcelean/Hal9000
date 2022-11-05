#pragma once

#include "io.h"
#include "../../Disk/headers/disk_structures.h"

void
DumpDisk(
    IN      PDISK_OBJECT        Disk
    );

void
DumpPartition(
    IN      PPARTITION_INFORMATION  Partition
    );