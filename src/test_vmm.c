#include "test_common.h"
#include "test_vmm.h"

#define TST_VMM_MAGIC_VALUE_TO_WRITE                0xAC
#define TST_VMM_VA_TO_REQUEST                       (PtrOffset(gVirtualToPhysicalOffset,32 * TB_SIZE))

static const DWORD TST_VMM_ALLOCATION_SIZES[] =
{
    PAGE_SIZE,
    1,
    1024,
    PAGE_SIZE * 10,
    1 * MB_SIZE
};
static const DWORD TST_VMM_NO_OF_SIZES = ARRAYSIZE(TST_VMM_ALLOCATION_SIZES);

static
STATUS
_TstVmmAllocationAndDeallocation(
    IN          DWORD       AllocationSize,
    IN          BOOLEAN     SpecifyBase
    );

void
TestVmmAllocAndFreeFunctions(
    void
    )
{
    DWORD i;
    BYTE specifyAddress;
    STATUS status;

    for (specifyAddress = 0; specifyAddress < 2; ++specifyAddress)
    {
        for (i = 0; i < TST_VMM_NO_OF_SIZES; ++i)
        {
            LOGL("Will call _TstVmmAllocationAndDeallocation for size: %u B, specify address: %d\n", TST_VMM_ALLOCATION_SIZES[i], specifyAddress );
            status = _TstVmmAllocationAndDeallocation(TST_VMM_ALLOCATION_SIZES[i], specifyAddress );
            LOGL("_TstVmmAllocationAndDeallocation finished with status: 0x%x\n", status );
        }
    }
}

static
STATUS
_TstVmmAllocationAndDeallocation(
    IN          DWORD       AllocationSize,
    IN          BOOLEAN     SpecifyBase
    )
{
    STATUS status;
    PBYTE pBaseAddress;
    PVOID pAddressToMap;

    status = STATUS_SUCCESS;
    pAddressToMap = SpecifyBase ? TST_VMM_VA_TO_REQUEST : NULL;

    LOGL("About to reserve region of %u bytes\n", AllocationSize );
    pBaseAddress = VmmAllocRegion(pAddressToMap,
                                  AllocationSize,
                                  VMM_ALLOC_TYPE_RESERVE,
                                  PAGE_RIGHTS_READWRITE
                                  );
    if (NULL == pBaseAddress)
    {
        if (0 != AllocationSize)
        {
            status = STATUS_MEMORY_CANNOT_BE_RESERVED;
            LOG_ERROR("VmmAllocRegion failed reserve: %u bytes of memory starting at address: 0x%X\n", AllocationSize, pAddressToMap);
            return status;
        }
        else
        {
            return STATUS_SUCCESS;
        }
    }

    LOGL("About to commit region of %u bytes\n", AllocationSize );
    pBaseAddress = VmmAllocRegion(pBaseAddress,
                                  AllocationSize,
                                  VMM_ALLOC_TYPE_COMMIT,
                                  PAGE_RIGHTS_READWRITE
                                  );
    if (NULL == pBaseAddress)
    {
        status = STATUS_MEMORY_CANNOT_BE_COMMITED;
        LOG_ERROR("VmmAllocRegion failed commit: %u bytes of memory starting at address: 0x%X\n", AllocationSize, pAddressToMap);
        return status;
    }

    LOGL("About to write to reserved region at address 0x%X\n", pBaseAddress );
    *pBaseAddress = TST_VMM_MAGIC_VALUE_TO_WRITE;
    if (TST_VMM_MAGIC_VALUE_TO_WRITE != *pBaseAddress)
    {
        LOG_ERROR("Value written does not correspond to value read\n");
        return STATUS_UNSUCCESSFUL;
    }

    LOGL("About to decommit region of %u bytes\n", AllocationSize );
    VmmFreeRegion(pBaseAddress,
                  AllocationSize,
                  VMM_FREE_TYPE_DECOMMIT
                  );

    LOGL("About to eagerly commit region of %u bytes\n", AllocationSize );
    pBaseAddress = VmmAllocRegion(pBaseAddress,
                                  AllocationSize,
                                  VMM_ALLOC_TYPE_COMMIT | VMM_ALLOC_TYPE_NOT_LAZY,
                                  PAGE_RIGHTS_READWRITE
                                  );
    if (NULL == pBaseAddress)
    {
        status = STATUS_MEMORY_CANNOT_BE_COMMITED;
        LOG_ERROR("VmmAllocRegion failed eager commit: %u bytes of memory starting at address: 0x%X\n", AllocationSize, pAddressToMap);
        return status;
    }

    LOGL("About to write to reserved region at address 0x%X\n", pBaseAddress );
    *pBaseAddress = TST_VMM_MAGIC_VALUE_TO_WRITE;
    if (TST_VMM_MAGIC_VALUE_TO_WRITE != *pBaseAddress)
    {
        LOG_ERROR("Value written does not correspond to value read\n");
        return STATUS_UNSUCCESSFUL;
    }

    LOGL("About to release region of %u bytes\n", AllocationSize );
    VmmFreeRegion(pBaseAddress,
                  0,
                  VMM_FREE_TYPE_RELEASE
                  );

    return status;
}