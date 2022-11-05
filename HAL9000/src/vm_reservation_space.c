#include "HAL9000.h"
#include "vm_reservation_space.h"
#include "cpumu.h"
#include "bitmap.h"
#include "lock_common.h"
#include "io.h"

typedef enum _VMM_RESERVATION_STATE
{
    VmmReservationStateFree     = 0x0,
    VmmReservationStateUsed     = 0x1,
    VmmReservationStateLast     = 0x2,
} VMM_RESERVATION_STATE;

// A reservation is allocated each time a process reserves an area of
// virtual memory. The commit bitmap is used to distinguish between the
// reserved memory and the committed memory.
typedef struct _VMM_RESERVATION
{
    // Starting addresses of the virtual memory allocation
    PVOID                   StartVa;

    // Size of the allocation
    QWORD                   Size;

    // The rights with which the memory was allocated
    PAGE_RIGHTS             PageRights;

    // The state of the this structure, this is used for finding the
    // next free slot
    VMM_RESERVATION_STATE   State;

    // If TRUE memory will be set as strong uncacheable (UC)
    // If FALSE the memory will be set as write back (WB)
    BOOLEAN                 Uncacheable;

    // Used for memory backed up by files
    // Indicates the file which holds the data
    PFILE_OBJECT            BackingFile;

    // Describes which pages of the virtual memory reserved are actually
    // committed, i.e. which are valid when a #PF occurs
    BITMAP                  CommitBitmap;
} VMM_RESERVATION, *PVMM_RESERVATION;

// 20% Will go for the list of reservations
// 80% Will go for the bitmaps describing the memory committed by those reservations
#define RESERVATION_LIST_PERCENTAGE_IN_HUNDREDS     (20 * 100)

//******************************************************************************
// Function:     _VmFindFirstFreeReservation
// Description:  Returns the first free reservation entry or NULL if the whole
//               region was exhausted.
// Returns:      PVMM_RESERVATION
// Parameter:    void
//******************************************************************************
REQUIRES_SHARED_LOCK(ReservationSpace->ReservationLock)
static
PTR_SUCCESS
PVMM_RESERVATION
_VmFindFirstFreeReservation(
    INOUT    PVMM_RESERVATION_SPACE  ReservationSpace
    );

//******************************************************************************
// Function:     VmFindReservation
// Description:  Checks if there is a reservation made for the address range
//               received as input. If Reservation is non-NULL the pointer which
//               describes the reservation is returned.
// Returns:      STATUS - STATUS_SUCCESS or STATUS_ELEMENT_NOT_FOUND in case of
//               failure
// Parameter:    IN PVOID Address
// Parameter:    IN QWORD Size
// Parameter:    OUT_OPT_PTR PVMM_RESERVATION* Reservation
//******************************************************************************
REQUIRES_SHARED_LOCK(ReservationSpace->ReservationLock)
static
STATUS
_VmFindReservation(
    INOUT           PVMM_RESERVATION_SPACE  ReservationSpace,
    IN              PVOID                   Address,
    IN              QWORD                   Size,
    OUT_OPT_PTR     PVMM_RESERVATION*       Reservation
    );

//******************************************************************************
// Function:     VmInitializeReservation
// Description:  Initializes a reservation entry to describe the range of
//               virtual addresses received as input.
// Returns:      STATUS
// Parameter:    IN PVOID Address
// Parameter:    IN QWORD Size
// Parameter:    IN PAGE_RIGHTS PageRights
// Parameter:    OUT PVMM_RESERVATION VmmReservation
//******************************************************************************
REQUIRES_EXCL_LOCK(ReservationSpace->ReservationLock)
static
void
_VmInitializeReservation(
    INOUT   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject,
    OUT     PVMM_RESERVATION        VmmReservation
    );

//******************************************************************************
// Function:     VmChangeVaReservationState
// Description:  Reserves and/or commits the virtual address range received as
//               input. These addresses must be PAGE_SIZE aligned.
// Returns:      STATUS
// Parameter:    IN PVOID Address
// Parameter:    IN QWORD Size
// Parameter:    IN VMM_ALLOC_TYPE AllocationType
// Parameter:    IN PAGE_RIGHTS PageRights
//******************************************************************************
REQUIRES_EXCL_LOCK(ReservationSpace->ReservationLock)
static
STATUS
_VmChangeVaReservationState(
    INOUT   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    IN      VMM_ALLOC_TYPE          AllocationType,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject
    );

// This function should be called only on a copy of the reservation to be uninitialized
// i.e. the reservation will not be itself uninitialized, but only the fields described
// and that's why it's ok to use a copy
// NOTE: this method is applied so that we won't have to hold the reservation lock when
// there is no good reason to do so
static
void
_VmUninitializeReservation(
    IN      PVMM_RESERVATION        VmmReservation
    );

//******************************************************************************
// Function:     _VmCommitReservation
// Description:  Commits a region of an already initialized reservation entry.
// Returns:      void
// Parameter:    IN PVOID Address
// Parameter:    IN QWORD Size
// Parameter:    INOUT PVMM_RESERVATION VmmReservation
//******************************************************************************
/// REQUIRES_EXCL_LOCK(m_vmmData.ReservationLock)
static
void
_VmCommitReservation(
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    INOUT   PVMM_RESERVATION        VmmReservation
    );

/// REQUIRES_EXCL_LOCK(m_vmmData.ReservationLock)
static
void
_VmDecommitReservation(
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    INOUT   PVMM_RESERVATION        VmmReservation
    );

//******************************************************************************
// Function:     _VmIsVaCommited
// Description:  Checks if a virtual address from within a reservation is
//               commited or not.
// Returns:      BOOLEAN
// Parameter:    IN PVMM_RESERVATION VmmReservation
// Parameter:    IN PVOID Address
//******************************************************************************
/// REQUIRES_SHARED_LOCK(m_vmmData.ReservationLock)
static
BOOLEAN
_VmIsVaCommited(
    IN      PVMM_RESERVATION        VmmReservation,
    IN      PVOID                   Address
    );


// We have the following virtual memory layout
// --------------------------------------------------------------------------------------------------------------
// |          ReservationMetadataBaseAddress  | + ReservationMetadataSize or at ReservationBaseAddress          |
// --------------------------------------------------------------------------------------------------------------
// | ReservationList | Commit Bitmap Buffers  | VmmAllocRegionEx allocates Virtual Addresses starting from here |
// --------------------------------------------------------------------------------------------------------------
// |       20%       |          80%           |                                                                 |
// --------------------------------------------------------------------------------------------------------------
_No_competing_thread_
void
VmReservationSpaceInit(
    IN          PVOID                   ReservationMetadataBaseAddress,
    IN_OPT      PVOID                   ReservationBaseAddress,
    IN          QWORD                   ReservationMetadataSize,
    OUT         PVMM_RESERVATION_SPACE  ReservationSpace
    )
{
    QWORD sizeForReservationList;

    ASSERT(NULL != ReservationMetadataBaseAddress );
    ASSERT(IsAddressAligned(ReservationMetadataBaseAddress, PAGE_SIZE));
    ASSERT(ReservationMetadataSize != 0);
    ASSERT(NULL != ReservationSpace);

    LOG_TRACE_VMM("VMM reservation space base address at 0x%X\n", ReservationMetadataBaseAddress);

    sizeForReservationList = CalculatePercentage(ReservationMetadataSize, RESERVATION_LIST_PERCENTAGE_IN_HUNDREDS);

    ReservationSpace->ReservationList = (PVMM_RESERVATION) ReservationMetadataBaseAddress;
    ReservationSpace->BitmapAddressStart = PtrOffset(ReservationMetadataBaseAddress, sizeForReservationList);
    ReservationSpace->FreeBitmapAddress = ReservationSpace->BitmapAddressStart;

    LOG_TRACE_VMM("Start of bitmap address: 0x%X\n", ReservationSpace->BitmapAddressStart);

    ReservationSpace->FreeVirtualAddressPointer = (ReservationBaseAddress == NULL) ?
                    (PVOID)PtrOffset(ReservationMetadataBaseAddress, ReservationMetadataSize) :
                    ReservationBaseAddress;
    ReservationSpace->StartOfVirtualAddressSpace = ReservationSpace->FreeVirtualAddressPointer;
    ReservationSpace->ReservedAreaSize = ReservationMetadataSize;

    LOG_TRACE_VMM("First virtual address is 0x%X\n", ReservationSpace->FreeVirtualAddressPointer);
    LOG_TRACE_VMM("Start of reserved VA: 0x%X\n", ReservationMetadataBaseAddress );
    LOG_TRACE_VMM("End of reserved VA: 0x%X\n", PtrOffset(ReservationMetadataBaseAddress, ReservationSpace->ReservedAreaSize));

    LOG_TRACE_VMM("Reserved area size: %U KB\n", ReservationMetadataSize / KB_SIZE );

    RwSpinlockInit(&ReservationSpace->ReservationLock);
}

_No_competing_thread_
void
VmReservationSpaceFinishInit(
    INOUT       PVMM_RESERVATION_SPACE  ReservationSpace
    )
{
    ASSERT(ReservationSpace != NULL);

    ReservationSpace->ReservationList[0].State = VmmReservationStateLast;
}

REQUIRES_SHARED_LOCK(ReservationSpace->ReservationLock)
static
STATUS
_VmFindReservation(
    INOUT           PVMM_RESERVATION_SPACE  ReservationSpace,
    IN              PVOID                   Address,
    IN              QWORD                   Size,
    OUT_OPT_PTR     PVMM_RESERVATION*       Reservation
    )
{
    STATUS status;
    PVMM_RESERVATION pCurrentReservation;
    BOOLEAN bFound;

    ASSERT(ReservationSpace != NULL);
    ASSERT(NULL != Address );
    ASSERT(0 != Size );
    ASSERT(Reservation != NULL);

    status = STATUS_SUCCESS;
    bFound = FALSE;

    for(pCurrentReservation = ReservationSpace->ReservationList;
            VmmReservationStateLast != pCurrentReservation->State &&
            (PVOID) pCurrentReservation < ReservationSpace->BitmapAddressStart;
        pCurrentReservation = pCurrentReservation + 1
        )
    {
        if (VmmReservationStateFree == pCurrentReservation->State)
        {
            // skip free entries
            continue;
        }

        if (CHECK_BOUNDS(Address, Size, pCurrentReservation->StartVa, pCurrentReservation->Size))
        {
            bFound = TRUE;
            break;
        }
    }

    if (!bFound)
    {
        LOG_TRACE_VMM("Could not find reservation for VA 0x%X of size 0x%X\n", Address, Size );
        status = STATUS_ELEMENT_NOT_FOUND;
    }
    else
    {
        if (NULL != Reservation)
        {
            *Reservation = pCurrentReservation;
        }
    }

    return status;
}

REQUIRES_EXCL_LOCK(ReservationSpace->ReservationLock)
static
void
_VmInitializeReservation(
    INOUT   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject,
    OUT     PVMM_RESERVATION        VmmReservation
    )
{
    QWORD bitmapSize;
    QWORD noOfPages;

    ASSERT(NULL != ReservationSpace);
    ASSERT( NULL != Address );
    ASSERT( 0 != Size );
    ASSERT(IsAddressAligned(Address, PAGE_SIZE));
    ASSERT(IsAddressAligned(Size, PAGE_SIZE));
    ASSERT( NULL != VmmReservation );

    noOfPages = Size / PAGE_SIZE;
    bitmapSize = 0;

    VmmReservation->State = VmmReservationStateUsed;
    VmmReservation->StartVa = Address;
    VmmReservation->Size = Size;
    VmmReservation->PageRights = PageRights;
    VmmReservation->Uncacheable = Uncacheable;
    VmmReservation->BackingFile = FileObject;

    LOG_TRACE_VMM("StartVa: 0x%X\n", Address );
    LOG_TRACE_VMM("Size: 0x%X\n", Size );

    ASSERT( noOfPages <= MAX_DWORD);

    bitmapSize = BitmapPreinit( &VmmReservation->CommitBitmap, (DWORD) noOfPages );
    ASSERT( 0 != bitmapSize );

    LOG_TRACE_VMM("BitmapSize: 0x%X\n", bitmapSize );
    LOG_TRACE_VMM("NoOfPages: 0x%X\n", noOfPages );
    LOG_TRACE_VMM("Bitmap->BitCount: 0x%x\n", BitmapGetMaxElementCount(&VmmReservation->CommitBitmap) );

    /// TODO: Check if there's a reason in which we advance the bitmap buffers from page to page - it seems rather
    /// wasteful (most reservations will probably be under 8 pages - this could be described by a BYTE, not a PAGE)
    /// One reason may be because at uninit the whole physical frame of memory will be released => there can't be two
    /// bitmap buffers in the same PAGE - we need to analyze what is more wasteful (never being able to free some physical
    /// frames or allocating a physical frame to described the buffer for each reservation)
    /// Another suggestion would be for each VMM_RESERVATION structure to also have a QWORD which can be used as the
    /// bitmap if the pages described fit in the buffer (i.e. 64 pages or 256KB which will cover 99,99% cases)
    ASSERT( IsAddressAligned(ReservationSpace->FreeBitmapAddress, PAGE_SIZE ));
    BitmapInit(&VmmReservation->CommitBitmap, ReservationSpace->FreeBitmapAddress );

    // Make sure we're not exceeding our bitmap buffers VA space
    ASSERT(CHECK_BOUNDS(ReservationSpace->FreeBitmapAddress,
                        AlignAddressUpper(bitmapSize, PAGE_SIZE),
                        ReservationSpace->ReservationList,
                        ReservationSpace->ReservedAreaSize));

    ReservationSpace->FreeBitmapAddress = ReservationSpace->FreeBitmapAddress + AlignAddressUpper( bitmapSize, PAGE_SIZE );
}

REQUIRES_EXCL_LOCK(ReservationSpace->ReservationLock)
static
STATUS
_VmChangeVaReservationState(
    INOUT   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    IN      VMM_ALLOC_TYPE          AllocationType,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject
    )
{
    PVMM_RESERVATION pReservation;
    STATUS status;

    ASSERT(ReservationSpace != NULL);
    ASSERT(NULL != Address);
    ASSERT(0 != Size);

    // We want only one of these 2 flags to be set
    ASSERT((AllocationType == VMM_ALLOC_TYPE_RESERVE) ^ (AllocationType == VMM_ALLOC_TYPE_COMMIT));

    ASSERT(IsAddressAligned(Address, PAGE_SIZE));
    ASSERT(IsAddressAligned(Size, PAGE_SIZE));

    status = STATUS_SUCCESS;
    pReservation = NULL;

    status = _VmFindReservation(ReservationSpace,
                                Address,
                                Size,
                                &pReservation
                                );
    if (STATUS_ELEMENT_NOT_FOUND == status)
    {
        if (VMM_ALLOC_TYPE_RESERVE != AllocationType)
        {
            LOG_ERROR("There is no reservation found for address 0x%X\n", Address);
            return STATUS_MEMORY_IS_NOT_RESERVED;
        }
        status = STATUS_SUCCESS;
    }
    else if (SUCCEEDED(status))
    {
        if (VMM_ALLOC_TYPE_RESERVE == AllocationType)
        {
            LOG_ERROR("Cannot reserve an already reserved virtual address 0x%X\n", Address);
            return STATUS_MEMORY_ALREADY_RESERVED;
        }
    }

    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("_VmFindReservation", status );
        return status;
    }

    switch (AllocationType)
    {
    case VMM_ALLOC_TYPE_RESERVE:
        pReservation = _VmFindFirstFreeReservation(ReservationSpace);
        ASSERT( NULL != pReservation );

        // _VmChangeVaReservationState is called with the lock taken exclusively and no function to release
        // the lock is called
        // warning C26110: Caller failing to hold lock 'm_vmmData.ReservationLock' before calling function
        // '_VmInitializeReservation'
#pragma warning(suppress: 26110)
        _VmInitializeReservation(ReservationSpace,
                                Address,
                                Size,
                                PageRights,
                                Uncacheable,
                                FileObject,
                                pReservation
                                );
        break;
    case VMM_ALLOC_TYPE_COMMIT:
        ASSERT( NULL != pReservation );

        if (pReservation->PageRights != PageRights)
        {
            LOG_ERROR("Reservation rights 0x%x differ from the ones currently requested: 0x%x\n", pReservation->PageRights, PageRights );
            return STATUS_MEMORY_CONFLICTING_ACCESS_RIGHTS;
        }

        if (pReservation->Uncacheable != Uncacheable)
        {
            LOG_ERROR("Caching 0x%x differs from the one currently requested: 0x%x\n", pReservation->Uncacheable, Uncacheable );
            return STATUS_MEMORY_CONFLICTING_CACHEABILITY;
        }

        // _VmChangeVaReservationState is called with the lock taken exclusively and no function to release
        // the lock is called
        // warning C26110: Caller failing to hold lock 'm_vmmData.ReservationLock' before calling function
        // '_VmCommitReservation'
#pragma warning(suppress: 26110)
        _VmCommitReservation(Address,
                             Size,
                             pReservation
                             );
        break;
    default:
        status = STATUS_UNSUPPORTED;
    }

    return status;
}

REQUIRES_SHARED_LOCK(ReservationSpace->ReservationLock)
static
PTR_SUCCESS
PVMM_RESERVATION
_VmFindFirstFreeReservation(
    INOUT       PVMM_RESERVATION_SPACE  ReservationSpace
    )
{
    PVMM_RESERVATION pResult;

    ASSERT(ReservationSpace != NULL);

    pResult = NULL;

    for (PVMM_RESERVATION pCurrentReservation = ReservationSpace->ReservationList;
        (PVOID)pCurrentReservation < ReservationSpace->BitmapAddressStart;
         pCurrentReservation = pCurrentReservation + 1
         )
    {
        if (VmmReservationStateFree == pCurrentReservation->State)
        {
            pResult = pCurrentReservation;

            break;
        }

        if (VmmReservationStateLast == pCurrentReservation->State)
        {
            pCurrentReservation->State = VmmReservationStateFree;

            // set the next reservation as the last one
            (pCurrentReservation + 1)->State = VmmReservationStateLast;

            pResult = pCurrentReservation;

            break;
        }
    }

    return pResult;
}

static
void
_VmUninitializeReservation(
    IN      PVMM_RESERVATION        VmmReservation
    )
{
    PVOID pBitmapBuffer;
    DWORD bitmapSize;

    ASSERT( NULL != VmmReservation );

    pBitmapBuffer = VmmReservation->CommitBitmap.BitmapBuffer;
    ASSERT(IsAddressAligned( pBitmapBuffer, PAGE_SIZE ));

    bitmapSize = VmmReservation->CommitBitmap.BufferSize;

    BitmapUninit(&VmmReservation->CommitBitmap);

    MmuUnmapMemoryEx(pBitmapBuffer, bitmapSize, TRUE, NULL );

    if (VmmReservation->BackingFile != NULL)
    {
        STATUS status;

        // this is a memory mapped file
        status = IoCloseFile(VmmReservation->BackingFile);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoCloseFile", status);
        }

        VmmReservation->BackingFile = NULL;
    }
}

/// REQUIRES_EXCL_LOCK(m_vmmData.ReservationLock)
static
void
_VmCommitReservation(
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    INOUT   PVMM_RESERVATION        VmmReservation
    )
{
    QWORD startIndex;
    QWORD noOfPages;

    ASSERT(NULL != Address);
    ASSERT(0 != Size);
    ASSERT(IsAddressAligned(Address, PAGE_SIZE));
    ASSERT(IsAddressAligned(Size, PAGE_SIZE));

    noOfPages = Size / PAGE_SIZE;
    startIndex = PtrDiff(Address, VmmReservation->StartVa) / PAGE_SIZE;

    ASSERT(noOfPages <= MAX_DWORD);
    ASSERT(startIndex <= MAX_DWORD);

    BitmapSetBits(&VmmReservation->CommitBitmap, (DWORD) startIndex, (DWORD) noOfPages );
}

/// REQUIRES_EXCL_LOCK(m_vmmData.ReservationLock)
static
void
_VmDecommitReservation(
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    INOUT   PVMM_RESERVATION        VmmReservation
    )
{
    QWORD startIndex;
    QWORD noOfPages;

    ASSERT(NULL != Address);
    ASSERT(0 != Size);
    ASSERT(IsAddressAligned(Address, PAGE_SIZE));
    ASSERT(IsAddressAligned(Size, PAGE_SIZE));

    noOfPages = Size / PAGE_SIZE;
    startIndex = PtrDiff(Address, VmmReservation->StartVa) / PAGE_SIZE;

    ASSERT(noOfPages <= MAX_DWORD);
    ASSERT(startIndex <= MAX_DWORD);

    BitmapClearBits(&VmmReservation->CommitBitmap, (DWORD)startIndex, (DWORD)noOfPages);
}

/// REQUIRES_SHARED_LOCK(m_vmmData.ReservationLock)
static
BOOLEAN
_VmIsVaCommited(
    IN      PVMM_RESERVATION        VmmReservation,
    IN      PVOID                   Address
    )
{
    QWORD pageNo;

    ASSERT(NULL != VmmReservation);
    ASSERT(NULL != Address);

    pageNo = PtrDiff(Address, VmmReservation->StartVa) / PAGE_SIZE;

    ASSERT(pageNo <= MAX_DWORD);

    return BitmapGetBitValue(&VmmReservation->CommitBitmap, (DWORD) pageNo );
}

BOOLEAN
VmReservationCanAddressBeAccessed(
    INOUT                   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN                      PVOID                   FaultingAddress,
    IN                      PAGE_RIGHTS             RightsRequested,
    OUT                     PAGE_RIGHTS*            MemoryRights,
    OUT                     BOOLEAN*                Uncacheable,
    OUT_PTR_MAYBE_NULL      PFILE_OBJECT*           BackingFile,
    OUT                     QWORD*                  FileOffset
    )
{
    BOOLEAN bSolvedPageFault;
    BOOLEAN reservationLockHeld;
    INTR_STATE dummyState;
    PAGE_RIGHTS pageRights;
    BOOLEAN uncacheable;
    PFILE_OBJECT pBackingFile;
    QWORD fileOffset;
    PCPU* pCpu;
    STATUS status;

    ASSERT(ReservationSpace != NULL);
    ASSERT(MemoryRights != NULL);
    ASSERT(Uncacheable != NULL);
    ASSERT(BackingFile != NULL);
    ASSERT(FileOffset != NULL);
    ASSERT(INTR_OFF == CpuIntrGetState());

    if (NULL == FaultingAddress)
    {
        return FALSE;
    }

    bSolvedPageFault = FALSE;
    reservationLockHeld = FALSE;
    pageRights = 0;
    uncacheable = FALSE;
    pBackingFile = NULL;
    fileOffset = 0;
    pCpu = GetCurrentPcpu();
    status = STATUS_SUCCESS;

    __try
    {
        // m_vmmData.ReservationList needs to be accessed with the lock taken only when actually
        // accessing the elements of the array
        // warning C26130: Missing annotation _Requires_lock_held_(m_vmmData.ReservationLock) or _No_competing_thread_
        // at function 'VmmSolvePageFault'.Otherwise it could be a race condition.Variable 'm_vmmData.ReservationList'
        // should be protected by lock 'm_vmmData.ReservationLock'
#pragma warning(suppress: 26130)
        if (CHECK_BOUNDS(FaultingAddress, 1, ReservationSpace->ReservationList, ReservationSpace->ReservedAreaSize))
        {
            LOG_TRACE_VMM("Faulting address 0x%X is in reservation area!\n", FaultingAddress);

            // Check that the CPU is knowing what it's doing when accessing the reservation metadata
            // We wouldn't want to receive a #PF in this area due to a corruption or due to a bad system architecture
            if (NULL == pCpu || pCpu->VmmMemoryAccess)
            {
                // Also, definitely we don't want to execute code from here, so just drop it
                bSolvedPageFault = !IsBooleanFlagOn(RightsRequested, PAGE_RIGHTS_EXECUTE);

                // the pageRights variable is not actually used in case we can't solve the page fault, but still, for
                // future compatibility it will be set to 0 in case we can't solve the #PF
                pageRights = bSolvedPageFault ? PAGE_RIGHTS_READWRITE : 0;
                __leave;
            }
        }
        else
        {
            PVMM_RESERVATION pReservation;
            BOOLEAN bIsVaCommited;

            // another memory access
            RwSpinlockAcquireShared(&ReservationSpace->ReservationLock, &dummyState);
            reservationLockHeld = TRUE;

            status = _VmFindReservation(ReservationSpace, FaultingAddress, 1, &pReservation);
            if (!SUCCEEDED(status))
            {
                LOG_TRACE_VMM("_VmFindReservation", status);
                __leave;
            }

            LOG_TRACE_VMM("Reservation VA: 0x%X\n", pReservation->StartVa);
            LOG_TRACE_VMM("Reservation size: 0x%X\n", pReservation->Size);

            bIsVaCommited = _VmIsVaCommited(pReservation, FaultingAddress);

            pageRights = pReservation->PageRights;
            uncacheable = pReservation->Uncacheable;

            pBackingFile = pReservation->BackingFile;
            if (pBackingFile != NULL)
            {
                fileOffset = AlignAddressLower(PtrDiff(FaultingAddress, pReservation->StartVa), PAGE_SIZE);
            }

            // to solve the page fault we must have the VA already committed
            // and the page rights which were requested must be included in the
            // reservation rights
            bSolvedPageFault = bIsVaCommited && (IsBooleanFlagOn(pageRights, RightsRequested));

            __leave;
        }
    }
    __finally
    {
        if (reservationLockHeld)
        {
            // This function checks at the beginning that it was called with the interrupts disabled,
            // when we release the lock they will remain that way
            RwSpinlockReleaseShared(&ReservationSpace->ReservationLock, INTR_OFF);
            reservationLockHeld = FALSE;
        }

        if (bSolvedPageFault)
        {
            *MemoryRights = pageRights;
            *Uncacheable = uncacheable;

            *BackingFile = pBackingFile;
            *FileOffset = fileOffset;
        }
    }

    return bSolvedPageFault;
}

STATUS
VmReservationSpaceAllocRegion(
    IN      PVMM_RESERVATION_SPACE  ReservationSpace,
    IN_OPT  PVOID                   BaseAddress,
    IN      QWORD                   Size,
    IN      VMM_ALLOC_TYPE          AllocType,
    IN      PAGE_RIGHTS             Rights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject,
    OUT     PVOID*                  MappedAddress,
    OUT     QWORD*                  MappedSize
    )
{
    PVOID pBaseAddress;
    INTR_STATE oldState;
    STATUS status;
    QWORD alignedSize;
    PPCPU pCpu;

    ASSERT(ReservationSpace != NULL);
    ASSERT(Size != 0);
    ASSERT(MappedAddress != NULL);
    ASSERT(MappedSize != NULL);
    ASSERT(IsFlagOn(AllocType, VMM_ALLOC_TYPE_COMMIT | VMM_ALLOC_TYPE_RESERVE));

    status = STATUS_SUCCESS;

    if (NULL != BaseAddress)
    {
        // if we received BaseAddress as input we need
        // to align the address
        pBaseAddress = (PVOID)AlignAddressLower(BaseAddress, PAGE_SIZE);
        alignedSize = AlignAddressUpper(Size + PtrDiff(BaseAddress, pBaseAddress), PAGE_SIZE);

        if (pBaseAddress < ReservationSpace->StartOfVirtualAddressSpace)
        {
            LOG_ERROR("Cannot alloc virtual memory at 0x%X. Allocations start from 0x%X\n",
                      pBaseAddress, ReservationSpace->StartOfVirtualAddressSpace);
            return STATUS_MEMORY_CANNOT_BE_RESERVED;
        }
    }
    else
    {
        // if we're generating the virtual address =>
        // we can make sure it is aligned and we only
        // need to align the size
        alignedSize = AlignAddressUpper(Size, PAGE_SIZE);
        pBaseAddress = VmReservationSpaceDetermineNextFreeVirtualAddress(ReservationSpace, alignedSize);
    }

    RwSpinlockAcquireExclusive(&ReservationSpace->ReservationLock, &oldState);
    pCpu = GetCurrentPcpu();

    if (NULL != pCpu)
    {
        pCpu->VmmMemoryAccess = TRUE;
    }

    __try
    {
        if (IsBooleanFlagOn(AllocType, VMM_ALLOC_TYPE_RESERVE))
        {
            // reserve area
            status = _VmChangeVaReservationState(ReservationSpace,
                                                pBaseAddress,
                                                alignedSize,
                                                VMM_ALLOC_TYPE_RESERVE,
                                                Rights,
                                                Uncacheable,
                                                FileObject
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("_VmChangeVaReservationState", status);
                __leave;
            }
        }
        // these 2 are independent of each other and can be both active
        // so no if else

        if (IsBooleanFlagOn(AllocType, VMM_ALLOC_TYPE_COMMIT))
        {
            // commit area
            status = _VmChangeVaReservationState(ReservationSpace,
                                                 pBaseAddress,
                                                 alignedSize,
                                                 VMM_ALLOC_TYPE_COMMIT,
                                                 Rights,
                                                 Uncacheable,
                                                 FileObject
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("_VmChangeVaReservationState", status);
                __leave;
            }
        }
    }
    __finally
    {
        if (NULL != pCpu)
        {
            pCpu->VmmMemoryAccess = FALSE;
        }
        RwSpinlockReleaseExclusive(&ReservationSpace->ReservationLock, oldState);

        if (SUCCEEDED(status))
        {
            *MappedAddress = pBaseAddress;
            *MappedSize = alignedSize;
        }
    }

    return status;
}

void
VmReservationSpaceFreeRegion(
    INOUT   PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    _When_(VMM_FREE_TYPE_RELEASE == FreeType, IN_OPT)
    _When_(VMM_FREE_TYPE_RELEASE != FreeType, IN)
            QWORD                   Size,
    IN      VMM_FREE_TYPE           FreeType,
    OUT     PVOID*                  AlignedAddress,
    OUT     QWORD*                  AlignedSize
    )
{
    INTR_STATE oldState;
    PPCPU pCpu;
    STATUS status;
    PVMM_RESERVATION pReservation;
    BOOLEAN lockHeld;
    PVOID alignedAddress;
    QWORD alignedSize;

    ASSERT(Address != NULL);
    ASSERT(   (IsBooleanFlagOn(FreeType,VMM_FREE_TYPE_RELEASE) && (Size == 0))
           || (IsBooleanFlagOn(FreeType,VMM_FREE_TYPE_DECOMMIT) && (Size != 0))
            );
    ASSERT(AlignedAddress != NULL);
    ASSERT(AlignedSize != NULL);

    status = STATUS_SUCCESS;
    lockHeld = FALSE;
    pReservation = NULL;
    alignedAddress = NULL;
    alignedSize = 0;

    // they cannot both be used at the same time
    ASSERT( IsBooleanFlagOn( FreeType, VMM_FREE_TYPE_RELEASE ) ^ IsBooleanFlagOn(FreeType, VMM_FREE_TYPE_DECOMMIT ));

    RwSpinlockAcquireExclusive(&ReservationSpace->ReservationLock, &oldState);
    pCpu = GetCurrentPcpu();
    lockHeld = TRUE;

    if (NULL != pCpu)
    {
        pCpu->VmmMemoryAccess = TRUE;
    }

    status = _VmFindReservation(ReservationSpace, Address, 1, &pReservation);
    ASSERT_INFO(SUCCEEDED(status), "Cannot free reservation at address 0x%X\n", Address);

    if (IsBooleanFlagOn(FreeType, VMM_FREE_TYPE_RELEASE))
    {
        VMM_RESERVATION reservationCopy;

        // remove reservation
        memcpy( &reservationCopy, pReservation, sizeof(VMM_RESERVATION));
        memzero( pReservation, sizeof(VMM_RESERVATION));
        pReservation->State = VmmReservationStateFree;

        _Analysis_assume_lock_held_(ReservationSpace->ReservationLock);
        RwSpinlockReleaseExclusive(&ReservationSpace->ReservationLock, oldState);
        lockHeld = FALSE;

        // we will want to ummap this memory
        alignedAddress = reservationCopy.StartVa;
        alignedSize = reservationCopy.Size;

        // _VmUninitializeReservation actually acts on a copy
        // of the reservation, this is so that we can call the function
        // without holding the reservation lock
        _VmUninitializeReservation(&reservationCopy);
    }
    else if (IsBooleanFlagOn(FreeType, VMM_FREE_TYPE_DECOMMIT))
    {
        // de-commit memory
        alignedAddress = (PVOID)AlignAddressLower(Address, PAGE_SIZE);
        alignedSize = AlignAddressUpper(Size + ((PBYTE)Address - (PBYTE)alignedAddress), PAGE_SIZE);

        _VmDecommitReservation(alignedAddress, alignedSize, pReservation );
    }

    if (NULL != pCpu)
    {
        pCpu->VmmMemoryAccess = FALSE;
    }
    if (lockHeld)
    {
        RwSpinlockReleaseExclusive(&ReservationSpace->ReservationLock, oldState);
    }

    *AlignedAddress = alignedAddress;
    *AlignedSize = alignedSize;
}

STATUS
VmReservationReturnRightsForAddress(
    IN      PVMM_RESERVATION_SPACE  ReservationSpace,
    IN      PVOID                   Address,
    IN      QWORD                   Size,
    OUT     PAGE_RIGHTS*            Rights
    )
{
    PVMM_RESERVATION pReservation;
    INTR_STATE oldState;
    STATUS status;
    BOOLEAN bFullyCommited;

    ASSERT(ReservationSpace != NULL);
    ASSERT(Address != NULL);
    ASSERT(Size != 0);
    ASSERT(Rights != NULL);

    status = STATUS_SUCCESS;
    pReservation = NULL;
    bFullyCommited = FALSE;

    RwSpinlockAcquireShared(&ReservationSpace->ReservationLock, &oldState);

    __try
    {
        status = _VmFindReservation(ReservationSpace, Address, Size, &pReservation);
        if (!SUCCEEDED(status))
        {
            LOG_TRACE_VMM("_VmFindReservation", status);
            __leave;
        }

        *Rights = pReservation->PageRights;

        // Start with the assumption that the reservation is fully committed
        bFullyCommited = TRUE;

        /// TODO: This could be greatly optimized by both improving the bitmap implementation
        /// or by checking the offset directly from here without calling the function
        /// the function call implies an avoidable pointer subtraction and integer division
        for (QWORD i = 0; i < Size; i += PAGE_SIZE)
        {
            if (!_VmIsVaCommited(pReservation, PtrOffset(Address,i)))
            {
                bFullyCommited = FALSE;
                __leave;
            }
        }
    }
    __finally
    {
        RwSpinlockReleaseShared(&ReservationSpace->ReservationLock, oldState);
    }

    return bFullyCommited ? STATUS_SUCCESS : STATUS_MEMORY_IS_NOT_COMMITED;
}
