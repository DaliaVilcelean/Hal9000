#include "HAL9000.h"
#include "pmm.h"
#include "int15.h"
#include "bitmap.h"
#include "synch.h"

typedef struct _MEMORY_REGION_LIST
{
    MEMORY_MAP_TYPE     Type;
    DWORD               NumberOfEntries;
} MEMORY_REGION_LIST, *PMEMORY_REGION_LIST;

typedef struct _PMM_DATA
{
    // Both of the highest physical address values are setup on initialization and
    // remain unchanged, i.e. if the highest available address becomes later reserved
    // HighestPhysicalAddressAvailable will not change.

    // This points to the end of the highest physical address available in the system
    // i.e. this is not reserved and can be used
    // E.g: if the last available memory region starts at 0xFFFE'0000 and occupies a
    // page HighestPhysicalAddressAvailable will be 0xFFFE'1000
    PHYSICAL_ADDRESS    HighestPhysicalAddressAvailable;

    // This includes the memory already reserved in the system, it is greater than or
    // equal to HighestPhysicalAddressAvailable, depending on the arrangement of
    // memory in the system.
    PHYSICAL_ADDRESS    HighestPhysicalAddressPresent;

    // Total size of available memory over 1MB
    QWORD               PhysicalMemorySize;

    MEMORY_REGION_LIST  MemoryRegionList[MemoryMapTypeMax];

    LOCK                AllocationLock;

    _Guarded_by_(AllocationLock)
    BITMAP              AllocationBitmap;
} PMM_DATA, *PPMM_DATA;

static PMM_DATA m_pmmData;

static
void
_PmmDetermineMemoryLimits(
    IN_READS(NoOfEntries)               INT15_MEMORY_MAP_ENTRY*     MemoryMap,
    IN_RANGE_LOWER(1)                   DWORD                       NoOfEntries,
    OUT                                 QWORD*                      AvailableSystemMemory,
    OUT                                 PHYSICAL_ADDRESS*           HighestPresentPhysicalAddress,
    OUT                                 PHYSICAL_ADDRESS*           HighestAvailablePhysicalAddress,
    INOUT_UPDATES_ALL(MemoryMapTypeMax) PMEMORY_REGION_LIST         MemoryRegions
    );

static
void
_PmmInitializeAllocationBitmap(
    IN                          PVOID                       CurrentVirtualAddress,
    IN                          QWORD                       HighestMemoryAddress,
    IN                          PINT15_MEMORY_MAP_ENTRY     MemoryEntries,
    IN                          DWORD                       NumberOfMemoryEntries,
    OUT                         PBITMAP                     Bitmap,
    OUT                         DWORD*                      SizeReserved
    );

_No_competing_thread_
void
PmmPreinitSystem(
    void
    )
{
    DWORD i;

    memzero(&m_pmmData, sizeof(PMM_DATA));

    for (i = MemoryMapTypeUsableRAM; i < MemoryMapTypeMax; ++i)
    {
        m_pmmData.MemoryRegionList[i].Type = i;
    }

    LockInit(&m_pmmData.AllocationLock);
}

_No_competing_thread_
STATUS
PmmInitSystem(
    IN          PVOID                   BaseAddress,
    IN          PINT15_MEMORY_MAP_ENTRY MemoryEntries,
    IN          DWORD                   NumberOfMemoryEntries,
    OUT         DWORD*                  SizeReserved
    )
{
    QWORD pagingStructuresSize;
    DWORD sizeReserved;

    if (NULL == BaseAddress)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (NULL == MemoryEntries)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    if (0 == NumberOfMemoryEntries)
    {
        return STATUS_INVALID_PARAMETER3;
    }

    if (NULL == SizeReserved)
    {
        return STATUS_INVALID_PARAMETER4;
    }

    pagingStructuresSize = 0;
    sizeReserved = 0;

    _PmmDetermineMemoryLimits(MemoryEntries,
                              NumberOfMemoryEntries,
                              &m_pmmData.PhysicalMemorySize,
                              &m_pmmData.HighestPhysicalAddressPresent,
                              &m_pmmData.HighestPhysicalAddressAvailable,
                              m_pmmData.MemoryRegionList
                              );

    LOG("Physical memory size: 0x%X\n", m_pmmData.PhysicalMemorySize );
    LOG("Highest Physical address present: 0x%X\n", m_pmmData.HighestPhysicalAddressPresent);
    LOG("Highest Physical address available: 0x%X\n", m_pmmData.HighestPhysicalAddressAvailable);

    _PmmInitializeAllocationBitmap(BaseAddress,
                                   (QWORD) m_pmmData.HighestPhysicalAddressPresent,
                                   MemoryEntries,
                                   NumberOfMemoryEntries,
                                   &m_pmmData.AllocationBitmap,
                                   &sizeReserved
                                   );

    LOG("_PmmInitializeAllocationBitmap completed successfully\n");

    *SizeReserved = AlignAddressUpper( sizeReserved, PAGE_SIZE );

    return STATUS_SUCCESS;
}

PTR_SUCCESS
PHYSICAL_ADDRESS
PmmReserveMemoryEx(
    IN          DWORD                   NoOfFrames,
    IN_OPT      PHYSICAL_ADDRESS        MinPhysAddr
    )
{
    DWORD idx;
    QWORD startIdx;

    INTR_STATE oldState;

    if( 0 == NoOfFrames )
    {
        return NULL;
    }

    if (!IsAddressAligned(MinPhysAddr, PAGE_SIZE))
    {
        return NULL;
    }

    startIdx = (QWORD) MinPhysAddr / PAGE_SIZE;
    if (startIdx > MAX_DWORD)
    {
        return NULL;
    }

    LockAcquire( &m_pmmData.AllocationLock, &oldState);
    idx = BitmapScanFromAndFlip(&m_pmmData.AllocationBitmap, (DWORD) startIdx, NoOfFrames, FALSE );
    if (MAX_DWORD == idx)
    {
        LockRelease( &m_pmmData.AllocationLock, oldState);
        return NULL;
    }

    LockRelease( &m_pmmData.AllocationLock, oldState);

    return (PHYSICAL_ADDRESS) ( (QWORD) idx * PAGE_SIZE );
}

void
PmmReleaseMemory(
    IN          PHYSICAL_ADDRESS        PhysicalAddr,
    IN          DWORD                   NoOfFrames
    )
{
    QWORD index;
    INTR_STATE oldState;

    ASSERT( IsAddressAligned(PhysicalAddr, PAGE_SIZE));

    index = (QWORD) PhysicalAddr / PAGE_SIZE;

    ASSERT( index <= MAX_DWORD);

    LockAcquire( &m_pmmData.AllocationLock, &oldState);
    BitmapClearBits(&m_pmmData.AllocationBitmap, (DWORD) index, NoOfFrames);
    LockRelease( &m_pmmData.AllocationLock, oldState);
}

QWORD
PmmGetTotalSystemMemory(
    void
    )
{
    return m_pmmData.PhysicalMemorySize;
}

PHYSICAL_ADDRESS
PmmGetHighestPhysicalMemoryAddressPresent(
    void
    )
{
    return m_pmmData.HighestPhysicalAddressPresent;
}

PHYSICAL_ADDRESS
PmmGetHighestPhysicalMemoryAddressAvailable(
    void
    )
{
    return m_pmmData.HighestPhysicalAddressAvailable;
}

static
void
_PmmDetermineMemoryLimits(
    IN_READS(NoOfEntries)               INT15_MEMORY_MAP_ENTRY*     MemoryMap,
    IN_RANGE_LOWER(1)                   DWORD                       NoOfEntries,
    OUT                                 QWORD*                      AvailableSystemMemory,
    OUT                                 PHYSICAL_ADDRESS*           HighestPresentPhysicalAddress,
    OUT                                 PHYSICAL_ADDRESS*           HighestAvailablePhysicalAddress,
    INOUT_UPDATES_ALL(MemoryMapTypeMax) PMEMORY_REGION_LIST         MemoryRegions
    )
{
    DWORD i;
    QWORD sizeOfAvailableMemory;
    QWORD highestMemoryAddressPresent;
    QWORD highestMemoryAddressAvailable;
    DWORD memoryType;

    ASSERT(NULL != MemoryMap);
    ASSERT(0 != NoOfEntries);
    ASSERT(NULL != AvailableSystemMemory);
    ASSERT(NULL != HighestPresentPhysicalAddress);
    ASSERT(NULL != HighestAvailablePhysicalAddress);
    ASSERT(NULL != MemoryRegions);

    sizeOfAvailableMemory = 0;
    highestMemoryAddressPresent = 0;
    highestMemoryAddressAvailable = 0;

    for (i = 0; i < NoOfEntries; ++i)
    {
        memoryType = MemoryMap[i].Type;

        if (MemoryMap[i].BaseAddress + MemoryMap[i].Length > highestMemoryAddressPresent)
        {
            highestMemoryAddressPresent = MemoryMap[i].BaseAddress + MemoryMap[i].Length;
        }

        if (!IsBooleanFlagOn(MemoryMap[i].ExtendedAttributes, MEMORY_MAP_ENTRY_EA_VALID_ENTRY))
        {
            // if this flag is clear => entry should be ignored
            continue;
        }

        MemoryRegions[memoryType].NumberOfEntries++;

        if (MemoryMapTypeUsableRAM != memoryType)
        {
            // we only care about usable RAM memory
            continue;
        }

        if (MemoryMap[i].BaseAddress + MemoryMap[i].Length > highestMemoryAddressAvailable)
        {
            highestMemoryAddressAvailable = MemoryMap[i].BaseAddress + MemoryMap[i].Length;
        }

        sizeOfAvailableMemory = sizeOfAvailableMemory + MemoryMap[i].Length;
    }

    *AvailableSystemMemory = sizeOfAvailableMemory;
    *HighestPresentPhysicalAddress = (PHYSICAL_ADDRESS) highestMemoryAddressPresent;
    *HighestAvailablePhysicalAddress = (PHYSICAL_ADDRESS) highestMemoryAddressAvailable;
}

static
void
_PmmInitializeAllocationBitmap(
    IN                          PVOID                       CurrentVirtualAddress,
    IN                          QWORD                       HighestMemoryAddress,
    IN                          PINT15_MEMORY_MAP_ENTRY     MemoryEntries,
    IN                          DWORD                       NumberOfMemoryEntries,
    OUT                         PBITMAP                     Bitmap,
    OUT                         DWORD*                      SizeReserved
    )
{
    DWORD bitmapSize;
    QWORD noOfPhysicalFrames;
    DWORD i;
    DWORD memoryType;

    LOG_FUNC_START;

    ASSERT(NULL != CurrentVirtualAddress);
    ASSERT( 0 != HighestMemoryAddress );
    ASSERT( NULL != Bitmap );
    ASSERT( NULL != SizeReserved );

    noOfPhysicalFrames = HighestMemoryAddress / PAGE_SIZE;
    ASSERT( noOfPhysicalFrames <= MAX_DWORD);

    bitmapSize = BitmapPreinit(Bitmap, (DWORD) noOfPhysicalFrames);
    BitmapInit(Bitmap, CurrentVirtualAddress );

    LOG("Bitmap size: %u B\n", bitmapSize );

    *SizeReserved = bitmapSize;

    // The idea here is to reserve all possible physical memory
    // PA 0 ----> HighestMemoryAddress
    // and then mark as free only only usable RAM memory over 1MB
    // This means in-existent and reserved system memory will never be used

    PmmReserveMemory(BitmapGetMaxElementCount(Bitmap));

    LOG("All memory is now reserved\n");

    for (i = 0; i < NumberOfMemoryEntries; ++i)
    {
        memoryType = MemoryEntries[i].Type;

        if (!IsBooleanFlagOn(MemoryEntries[i].ExtendedAttributes, MEMORY_MAP_ENTRY_EA_VALID_ENTRY))
        {
            // if this flag is clear => entry should be ignored
            continue;
        }

        if (MemoryMapTypeUsableRAM != memoryType)
        {
            // we only care about usable RAM memory
            continue;
        }

        if (MemoryEntries[i].BaseAddress < 1 * MB_SIZE)
        {
            // we won't be allocating any memory under 1MB
            continue;
        }

        PHYSICAL_ADDRESS physAddr = (PHYSICAL_ADDRESS) AlignAddressUpper(MemoryEntries[i].BaseAddress, PAGE_SIZE);
        QWORD noOfFrames = MemoryEntries[i].Length / PAGE_SIZE;

        ASSERT( noOfFrames <= MAX_DWORD);

        // here it is necessary to use PmmReleaseMemory
        PmmReleaseMemory(physAddr, (DWORD) noOfFrames );

        LOG("Releasing %d frames of memory starting from PA 0x%X\n", noOfFrames, physAddr );
    }

    LOG_FUNC_END;
}