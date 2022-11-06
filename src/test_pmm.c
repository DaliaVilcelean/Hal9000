#include "test_common.h"
#include "test_pmm.h"

static const DWORD TST_PMM_ALLOCATION_FRAMES[] =
{
    1,
    3,
    16,
    0
};
static const DWORD TST_PMM_NO_OF_SIZES = ARRAYSIZE(TST_PMM_ALLOCATION_FRAMES);

static
STATUS
_TstPmmReservationAndRelease(
    IN          DWORD               NoOfFrames,
    IN_OPT      PHYSICAL_ADDRESS    MinimumAddress
    );

void
TestPmmReserveAndReleaseFunctions(
    void
    )
{
    DWORD i;
    BYTE specifyAddress;
    PHYSICAL_ADDRESS minPa;
    STATUS status;
    QWORD totalSystemMemory;

    totalSystemMemory = PmmGetTotalSystemMemory();

    LOGL("Total system memory: %U KB\n", totalSystemMemory / KB_SIZE );

    for (specifyAddress = 0; specifyAddress < 2; ++specifyAddress)
    {
        minPa = specifyAddress ? (PHYSICAL_ADDRESS) AlignAddressLower( ( totalSystemMemory / 2 ), PAGE_SIZE ) : NULL;

        for (i = 0; i < TST_PMM_NO_OF_SIZES; ++i)
        {
            LOGL("Will call _TstPmmReservationAndRelease for %u frames with minimum address: 0x%X\n", TST_PMM_ALLOCATION_FRAMES[i], minPa);
            status = _TstPmmReservationAndRelease(TST_PMM_ALLOCATION_FRAMES[i], minPa);
            LOGL("_TstPmmReservationAndRelease finished with status: 0x%x\n", status);
        }
    }
}

static
STATUS
_TstPmmReservationAndRelease(
    IN          DWORD               NoOfFrames,
    IN_OPT      PHYSICAL_ADDRESS    MinimumAddress
    )
{
    STATUS status;
    PHYSICAL_ADDRESS pa;
    PHYSICAL_ADDRESS initialPa;

    status = STATUS_SUCCESS;
    initialPa = NULL;

    LOGL("About to reserve physical memory\n");
    pa = PmmReserveMemoryEx(NoOfFrames, MinimumAddress );
    if (NULL == pa)
    {
        if (0 != NoOfFrames)
        {
            LOG_ERROR("PmmReserveMemoryEx failed for %u frames starting from PA: 0x%X\n", NoOfFrames, MinimumAddress );
            return STATUS_PHYSICAL_MEMORY_NOT_AVAILABLE;
        }
        else
        {
            return STATUS_SUCCESS;
        }
    }

    if (pa < MinimumAddress)
    {
        LOG_ERROR("Physical address returned 0x%X is lower than the minimum request 0x%X\n", pa, MinimumAddress );
        return STATUS_UNSUCCESSFUL;
    }
    initialPa = pa;

    LOGL("About to release previously reserved memory\n");
    PmmReleaseMemory(pa, NoOfFrames );

    LOGL("About to reserve physical memory\n");
    pa = PmmReserveMemoryEx(NoOfFrames, MinimumAddress );
    if (NULL == pa)
    {
        LOG_ERROR("PmmReserveMemoryEx failed for %u frames starting from PA: 0x%X\n", NoOfFrames, MinimumAddress );
        return STATUS_PHYSICAL_MEMORY_NOT_AVAILABLE;
    }

    if (pa < MinimumAddress)
    {
        LOG_ERROR("Physical address returned 0x%X is lower than the minimum request 0x%X\n", pa, MinimumAddress);
        return STATUS_UNSUCCESSFUL;
    }

    if (initialPa != pa)
    {
        LOG_ERROR("After request another frame of memory the initial physical address 0x%X was not returned again 0x%X\n", initialPa, pa );
        return STATUS_UNSUCCESSFUL;
    }

    LOGL("About to release previously reserved memory\n");
    PmmReleaseMemory(pa, NoOfFrames);

    return STATUS_SUCCESS;
}