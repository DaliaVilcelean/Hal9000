#include "HAL9000.h"
#include "dmp_cmos.h"
#include "dmp_common.h"

void
DumpCmos(
    IN      PCMOS_DATA      CmosData
    )
{
    INTR_STATE intrState;

    if (NULL == CmosData)
    {
        return;
    }

    intrState = DumpTakeLock();

    LOG("Second: %d\n", CmosData->Second);
    LOG("Minute: %d\n", CmosData->Minute);
    LOG("Hour: %d\n", CmosData->Hour);
    LOG("Day: %d\n", CmosData->Day);
    LOG("Month: %d\n", CmosData->Month);
    LOG("Year: %d\n", CmosData->Year);

    DumpReleaseLock(intrState);
}