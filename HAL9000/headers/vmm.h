#pragma once

#include "mmu.h"
#include "pte.h"

typedef struct _FILE_OBJECT* PFILE_OBJECT;

typedef struct _VMM_RESERVATION_SPACE* PVMM_RESERVATION_SPACE;

typedef struct _MDL *PMDL;

_No_competing_thread_
void
VmmPreinit(
    void
    );

_No_competing_thread_
STATUS
VmmInit(
    IN      PVOID                   BaseAddress
    );

//******************************************************************************
// Function:     VmmMapMemoryEx
// Description:  Maps a PA using the received paging data into virtual space.
// Returns:      PVOID - Virtual Address to which PhysicalAddress was mapped
// Parameter:    IN PPAGING_DATA PagingData - Paging tables to use
// Parameter:    IN PHYSICAL_ADDRESS PhysicalAddress - address to map
// Parameter:    IN DWORD Size - PAGE_SIZE aligned number of bytes to map
// Parameter:    IN PAGE_RIGHTS PageRights
// Parameter:    IN BOOLEAN Invalidate
// Parameter:    IN BOOLEAN Uncacheable
//******************************************************************************
PTR_SUCCESS
PVOID
VmmMapMemoryEx(
    IN      PPAGING_DATA            PagingData,
    IN      PHYSICAL_ADDRESS        PhysicalAddress,
    IN      QWORD                   Size,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Invalidate,
    IN      BOOLEAN                 Uncacheable
    );

//******************************************************************************
// Function:     VmmMapMemoryInternal
// Description:  Same as VmmMapMemoryEx except it maps the address to an
//               explicit virtual address.
/// NOTE:        This should be used used only in the vmm and mmu files
//******************************************************************************
void
VmmMapMemoryInternal(
    IN      PPAGING_DATA            PagingData,
    IN      PHYSICAL_ADDRESS        PhysicalAddress,
    IN      QWORD                   Size,
    IN      PVOID                   BaseAddress,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Invalidate,
    IN      BOOLEAN                 Uncacheable
    );

//******************************************************************************
// Function:     VmmUnmapMemoryEx
// Description:  Unmaps a previously mapped VA with VmmMapMemoryEx or
//               VmmMapMemoryInternal
// Returns:      void
// Parameter:    IN PML4 Cr3 - paging tables
// Parameter:    IN PVOID VirtualAddress
// Parameter:    IN DWORD Size - PAGE_SIZE aligned number of bytes to unmap
//******************************************************************************
void
VmmUnmapMemoryEx(
    IN      PML4                    Cr3,
    IN      PVOID                   VirtualAddress,
    IN      QWORD                   Size,
    IN      BOOLEAN                 ReleaseMemory
    );

#define VmmGetPhysicalAddress(Cr3,Va)   VmmGetPhysicalAddressEx((Cr3),(Va),NULL,NULL)

//******************************************************************************
// Function:     VmmGetPhysicalAddressEx
// Description:  Retrieves the physical address corresponding to VirtualAddress
//               given Cr3 and optionally retrieves the accessed and dirty
//               bits for the address.
// Returns:      PHYSICAL_ADDRESS - If NULL the virtual addressed is not mapped
// Parameter:    IN PML4 Cr3
// Parameter:    IN PVOID VirtualAddress
// Parameter:    OUT_OPT BOOLEAN* Accessed
// Parameter:    OUT_OPT BOOLEAN* Dirty
// NOTE:         If the Accessed or Dirty parameter is non-NULL the
//               corresponding bit will be cleared after the value is saved.
//******************************************************************************
PTR_SUCCESS
PHYSICAL_ADDRESS
VmmGetPhysicalAddressEx(
    IN      PML4                    Cr3,
    IN      PVOID                   VirtualAddress,
    OUT_OPT BOOLEAN*                Accessed,
    OUT_OPT BOOLEAN*                Dirty
    );

//******************************************************************************
// Function:     VmmPreparePagingData
// Description:  Retrieves the PAT indices required for mapping uncacheable and
//               writeback memory.
// Returns:      STATUS
// Parameter:    void
//******************************************************************************
_No_competing_thread_
STATUS
VmmPreparePagingData(
    void
    );

//******************************************************************************
// Function:     VmmSetupPageTables
// Description:  Setups the structures required for managing the paging tables
//               with CR3 BasePhysicalAddress and FramesReserved in PagingData.
//               This data will be available from the PagingDataWhereToMap space.
// Returns:      STATUS
// Parameter:    INOUT PPAGING_DATA PagingDataWhereToMap - Structures which will
//               described the PagingData created.
// Parameter:    OUT PPAGING_DATA PagingData - Structure describing the paging
//               structures.
// Parameter:    IN PHYSICAL_ADDRESS BasePhysicalAddress - CR3 physical address
// Parameter:    IN DWORD FramesReserved - Number of frames to use
//******************************************************************************
STATUS
VmmSetupPageTables(
    INOUT   PPAGING_DATA            PagingDataWhereToMap,
    OUT     PPAGING_DATA            PagingData,
    IN      PHYSICAL_ADDRESS        BasePhysicalAddress,
    IN      DWORD                   FramesReserved,
    IN      BOOLEAN                 KernelStructures
    );

//******************************************************************************
// Function:     VmmChangeCr3
// Description:  Performs a CR3 switch to Pml4Base using PCID Pcid.
// Returns:      void
// Parameter:    IN PHYSICAL_ADDRESS Pml4Base
// Parameter:    IN PCID Pcid
// Parameter:    IN BOOLEAN Invalidate - if TRUE invalidates CPU caching
//               translations made with Pcid.
//******************************************************************************
void
VmmChangeCr3(
    IN      PHYSICAL_ADDRESS        Pml4Base,
    IN_RANGE(PCID_FIRST_VALID_VALUE, PCID_TOTAL_NO_OF_VALUES - 1)
            PCID                    Pcid,
    IN      BOOLEAN                 Invalidate
    );

_No_competing_thread_
void
VmmInitReservationSystem(
    void
    );

#define VmmAllocRegion(Addr,Size,Type,Rights)       VmmAllocRegionEx((Addr),(Size),(Type),(Rights),FALSE, NULL, NULL, NULL, NULL)

//******************************************************************************
// Function:     VmmAllocRegion
// Description:  Allocates a region of virtual memory.
// Returns:      PVOID - Virtual address of the memory allocated
// Parameter:    IN_OPT PVOID BaseAddress
// Parameter:    IN QWORD Size
// Parameter:    IN VMM_ALLOC_TYPE AllocType - a mask of the following values:
//               VMM_ALLOC_TYPE_RESERVED - reserves the virtual address space
//               VMM_ALLOC_TYPE_COMMIT - commits the virtual address
//               VMM_ALLOC_TYPE_NOT_LAZY - if set the mapping will be valid
//               after the function returns (without #PF occurrence) and the
//               mapping will be to CONTINUOUS physical frames.
// Parameter:    IN PAGE_RIGHTS Rights
// Parameter:    IN BOOLEAN Uncacheable
// Parameter:    IN_OPT PFILE_OBJECT FileObject -if non-NULL, represents the
//               file which backs up the newly allocated memory.
// Parameter:    IN_OPT PVMM_RESERVATION_SPACE VaSpace
// Parameter:    IN_OPT PPAGING_LOCK_DATA PagingData
// Parameter:    IN_OPT PMDL Mdl - if non-NULL, describes the physical memory
//               to which the newly allocated virtual addresses will map to.
/// NOTE:        When an address is committed it is not mapped to physical
///              memory, it will be mapped on the first #PF
//******************************************************************************
PTR_SUCCESS
PVOID
VmmAllocRegionEx(
    IN_OPT  PVOID                   BaseAddress,
    IN      QWORD                   Size,
    IN      VMM_ALLOC_TYPE          AllocType,
    IN      PAGE_RIGHTS             Rights,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PFILE_OBJECT            FileObject,
    IN_OPT  PVMM_RESERVATION_SPACE  VaSpace,
    IN_OPT  PPAGING_LOCK_DATA       PagingData,
    IN_OPT  PMDL                    Mdl
    );

#define VmmFreeRegion(...)          VmmFreeRegionEx(__VA_ARGS__,TRUE, NULL, NULL)

//******************************************************************************
// Function:     VmmFreeRegionEx
// Description:  Frees a region of memory previously allocated.
// Returns:      void
// Parameter:    IN PVOID Address
// Parameter:    IN QWORD Size
// Parameter:    IN VMM_FREE_TYPE FreeType - can have one of two values (xor):
//               VMM_FREE_TYPE_DECOMMIT - de-commits the virtual address
//               VMM_FREE_TYPE_RELEASE - releases the whole reservation
// Parameter:    IN BOOLEAN ReleaseMemory - if TRUE => the physical frames are
//               released - else they will still be reserved. NOTE: You should
//               really know what you're doing if you're setting this parameter
//               to FALSE.
// Parameter:    IN_OPT PVMM_RESERVATION_SPACE VaSpace
// Parameter:    IN_OPT PPAGING_LOCK_DATA PagingData
//******************************************************************************
void
VmmFreeRegionEx(
    IN      PVOID                   Address,
    _When_(VMM_FREE_TYPE_RELEASE == FreeType, _Reserved_)
    _When_(VMM_FREE_TYPE_RELEASE != FreeType, IN)
            QWORD                   Size,
    IN      VMM_FREE_TYPE           FreeType,
    IN      BOOLEAN                 Release,
    IN_OPT  PVMM_RESERVATION_SPACE  VaSpace,
    IN_OPT  PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     VmmSolvePageFault
// Description:
// Returns:      BOOLEAN - TRUE => The current process has RightsRequested for
//               the FaultingAddress and the address was successfully mapped.
//                       - FALSE => The address is not commited or the process
//               doesn't have RightsRequested for the FaultingAddress.
// Parameter:    IN PVOID FaultingAddress - Address which incurred the #PF (CR2)
// Parameter:    IN PAGE_RIGHTS RightsRequested - The rights requested by the
//               access (taken from the Error Code pushed on the stack)
// Parameter     IN PPAGING_LOCK_DATA PagingData - The paging structures
//               used when the page-fault was generated.
//******************************************************************************
BOOLEAN
VmmSolvePageFault(
    IN      PVOID                   FaultingAddress,
    IN      PAGE_RIGHTS             RightsRequested,
    IN      PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     VmmRetrieveReservationSpaceForSystemProcess
// Description:  Retrieves a pointer to the system's reservation space.
// Returns:      PVMM_RESERVATION_SPACE
//******************************************************************************
PVMM_RESERVATION_SPACE
VmmRetrieveReservationSpaceForSystemProcess(
    void
    );

//******************************************************************************
// Function:     VmmCreateVirtualAddressSpace
// Description:  Creates a virtual address space beginning at
//               StartOfVirtualAddressSpace with metadata describing the space
//               of ReservationMetadataSize bytes.
// Returns:      STATUS
// Parameter:    OUT_PTR PVMM_RESERVATION_SPACE * ReservationSpace
// Parameter:    IN QWORD ReservationMetadataSize
// Parameter:    IN PVOID StartOfVirtualAddressSpace
//******************************************************************************
STATUS
VmmCreateVirtualAddressSpace(
    OUT_PTR PVMM_RESERVATION_SPACE*         ReservationSpace,
    IN      QWORD                           ReservationMetadataSize,
    IN      PVOID                           StartOfVirtualAddressSpace
    );

//******************************************************************************
// Function:     VmmDestroyVirtualAddressSpace
// Description:  Destroys a previously created VAS by VmmCreateVirtualAddressSpace
// Returns:      void
// Parameter:    PVMM_RESERVATION_SPACE ReservationSpace
//******************************************************************************
void
VmmDestroyVirtualAddressSpace(
    _Pre_valid_ _Post_ptr_invalid_
            PVMM_RESERVATION_SPACE          ReservationSpace
    );

//******************************************************************************
// Function:     VmmIsBufferValid
// Description:  Checks if the buffer described by Buffer and BufferSize is
//               committed in ReservationSpace and if the RightsRequested
//               access rights are available.
// Returns:      STATUS
// Parameter:    IN PVOID Buffer
// Parameter:    IN QWORD BufferSize
// Parameter:    IN PAGE_RIGHTS RightsRequested
// Parameter:    IN PVMM_RESERVATION_SPACE ReservationSpace
// Parameter:    IN BOOLEAN KernelAccess
//******************************************************************************
STATUS
VmmIsBufferValid(
    IN          PVOID                               Buffer,
    IN          QWORD                               BufferSize,
    IN          PAGE_RIGHTS                         RightsRequested,
    IN          PVMM_RESERVATION_SPACE              ReservationSpace,
    IN          BOOLEAN                             KernelAccess
    );
