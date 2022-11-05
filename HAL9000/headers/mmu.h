#pragma once

#include "mem_structures.h"
#include "lock_common.h"

typedef struct _PROCESS* PPROCESS;
typedef struct _PE_NT_HEADER_INFO *PPE_NT_HEADER_INFO;

/// TODO: Move BasePhysicalAddress and KernelSpace outside protected region
typedef struct _PAGING_DATA
{
    PHYSICAL_ADDRESS        BasePhysicalAddress;

    // because we don't have any functions to unmap
    // paging structures (and we probably won't need to)
    // there is no use in using a bitmap
    DWORD                   NumberOfFrames;
    DWORD                   CurrentIndex;

    BOOLEAN                 KernelSpace;
} PAGING_DATA, *PPAGING_DATA;

typedef struct _PAGING_LOCK_DATA
{
    /// TODO: investigate if we really need a recursive lock here
    REC_RW_SPINLOCK                 Lock;

    _Guarded_by_(Lock)
    PAGING_DATA                     Data;
} PAGING_LOCK_DATA, *PPAGING_LOCK_DATA;

// These map/unmap memory only in the context of the system process
#define MmuMapSystemMemory(Pa,Sz)   MmuMapMemoryEx((Pa),(Sz),PAGE_RIGHTS_READWRITE, FALSE, FALSE, NULL)
#define MmuUnmapSystemMemory(Va,Sz) MmuUnmapMemoryEx((Va),(Sz),FALSE, NULL)

_No_competing_thread_
void
MmuPreinitSystem(
    void
    );

//******************************************************************************
// Function:     MmuInitSystem
// Description:  Initializes the PMM, VMM, creates a new set of paging tables
//               and performs a CR3 switch to them. Initializes the heaps and
//               switches to a new stack.
// Returns:      STATUS
// Parameter:    IN PVOID KernelBaseAddress
// Parameter:    IN DWORD KernelSize
// Parameter:    IN PHYSICAL_ADDRESS MemoryEntries
// Parameter:    IN DWORD NumberOfMemoryEntries
//******************************************************************************
_No_competing_thread_
STATUS
MmuInitSystem(
    IN          PVOID                   KernelBaseAddress,
    IN          DWORD                   KernelSize,
    IN          PHYSICAL_ADDRESS        MemoryEntries,
    IN          DWORD                   NumberOfMemoryEntries
    );

//******************************************************************************
// Function:     MmuDiscardIdentityMappings
// Description:  Discards the identitiy memmory mapped regions required for AP
//               initialization.
// Returns:      void
//******************************************************************************
_No_competing_thread_
void
MmuDiscardIdentityMappings(
    void
    );


//******************************************************************************
// Function:     MmuInitThreadingSystem
// Description:  Creates the worker thread responsible for zero-ing physical
//               frames after they have been released.
// Returns:      STATUS
//******************************************************************************
STATUS
MmuInitThreadingSystem(
    void
    );

//******************************************************************************
// Function:     MmuGetTotalSystemMemory
// Description:  Returns the number of bytes of physical memory available in the
//               system.
// Returns:      QWORD
// NOTE:         This is the initial value on system boot. It is not updated as
//               more memory is reserved by the OS.
//******************************************************************************
QWORD
MmuGetTotalSystemMemory(
    void
    );

//******************************************************************************
// Function:     MmuGetHighestPhysicalMemoryAddressPresent
// Description:  Returns the largest physical address present on the system.
// Returns:      PHYSICAL_ADDRESS
// NOTE:         This also includes already reserved device memory.
//******************************************************************************
PHYSICAL_ADDRESS
MmuGetHighestPhysicalMemoryAddressPresent(
    void
    );

//******************************************************************************
// Function:     MmuMapMemoryEx
// Description:  Maps a physical address range into virtual address space.
// Returns:      PVOID - Resulting mapping for PhysicalAddress
// Parameter:    IN PHYSICAL_ADDRESS PhysicalAddress
// Parameter:    IN DWORD Size
// Parameter:    IN PAGE_RIGHTS PageRights
// Parameter:    IN BOOLEAN Invalidate - If TRUE modifies paging structure even
//               if it was already mapped.
// Parameter:    IN BOOLEAN Uncacheable
// Parameter:    IN_OPT PPAGING_LOCK_DATA PagingData - Paging structures to use,
//               if NULL maps only to kernel space.
//******************************************************************************
PTR_SUCCESS
PVOID
MmuMapMemoryEx(
    IN      PHYSICAL_ADDRESS        PhysicalAddress,
    IN      QWORD                   Size,
    IN      PAGE_RIGHTS             PageRights,
    IN      BOOLEAN                 Invalidate,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     MmuMapMemoryInternal
// Description:  Maps a physical address range into the specified virtual
//               address space.
// Returns:      void
// Parameter:    IN PHYSICAL_ADDRESS PhysicalAddress
// Parameter:    IN DWORD Size
// Parameter:    IN PAGE_RIGHTS PageRights
// Parameter:    IN PVOID VirtualAddress
// Parameter:    IN BOOLEAN Invalidate
// Parameter:    IN BOOLEAN Uncacheable
/// NOTE:        This should only be used by ap_tramp, vmm and no other modules.
//******************************************************************************
void
MmuMapMemoryInternal(
    IN      PHYSICAL_ADDRESS        PhysicalAddress,
    IN      QWORD                   Size,
    IN      PAGE_RIGHTS             PageRights,
    IN      PVOID                   VirtualAddress,
    IN      BOOLEAN                 Invalidate,
    IN      BOOLEAN                 Uncacheable,
    IN_OPT  PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     MmuUnmapMemory
// Description:  Unmaps a previously mapped memory region.
// Returns:      void
// Parameter:    IN PVOID VirtualAddress
// Parameter:    IN DWORD Size
//******************************************************************************
void
MmuUnmapMemoryEx(
    IN      PVOID                   VirtualAddress,
    IN      QWORD                   Size,
    IN      BOOLEAN                 ReleaseMemory,
    IN_OPT  PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     MmuReleaseMemory
// Description:  Schedules NoOfFrames frames of physical memory to be released
//               by the zero worker thread after being zeroed.
// Returns:      void
// Parameter:    IN PHYSICAL_ADDRESS PhysicalAddr
// Parameter:    IN DWORD NoOfFrames
//******************************************************************************
void
MmuReleaseMemory(
    IN          PHYSICAL_ADDRESS        PhysicalAddr,
    IN          DWORD                   NoOfFrames
    );

//******************************************************************************
// Function:     MmuGetPhysicalAddress
// Description:  Returns the physical address mapping for VirtualAddress using
//               the current CR3 paging structures.
// Returns:      PHYSICAL_ADDRESS
// Parameter:    IN PVOID VirtualAddress
//******************************************************************************
PTR_SUCCESS
PHYSICAL_ADDRESS
MmuGetPhysicalAddress(
    IN      PVOID                   VirtualAddress
    );

//******************************************************************************
// Function:     MmuGetPhysicalAddressEx
// Description:  Returns the physical address mapping for VirtualAddress using
//               either the PagingData parameter or the Cr3Base parameter.
// Returns:      PTR_SUCCESS
// Parameter:    IN PVOID VirtualAddress
// Parameter:    IN_OPT PPAGING_LOCK_DATA PagingData
// Parameter:    IN_OPT PHYSICAL_ADDRESS Cr3Base
// NOTE:         PagingData and Cr3Base cannot both be non-NULL.
//******************************************************************************
PTR_SUCCESS
PHYSICAL_ADDRESS
MmuGetPhysicalAddressEx(
    IN      PVOID                   VirtualAddress,
    IN_OPT  PPAGING_LOCK_DATA       PagingData,
    IN_OPT  PHYSICAL_ADDRESS        Cr3Base
    );

//******************************************************************************
// Function:     MmuAllocatePoolWithTag
// Description:  Allocates AllocationSize bytes of memory aligned at
//               AllocationAlignment bytes.
// Returns:      PVOID
// Parameter:    IN DWORD Flags - if PoolAllocateZeroMemory is specified then
//               memory will be initialized to zero before returning from this
//               function.
// Parameter:    IN DWORD AllocationSize
// Parameter:    IN DWORD Tag - Used to validate memory de-allocation does not
//               free random, unwanted memory addresses. Also, useful for
//               debugging, when tracing leaks.
// Parameter:    IN DWORD AllocationAlignment - If zero => the address will
//               be use the NATURAL_ALIGNMENT constant for determining the
//               alignment.
//******************************************************************************
_Always_(_When_(IsBooleanFlagOn(Flags, PoolAllocatePanicIfFail), RET_NOT_NULL))
PTR_SUCCESS
PVOID
MmuAllocatePoolWithTag(
    IN      DWORD                   Flags,
    IN      DWORD                   AllocationSize,
    IN      DWORD                   Tag,
    IN      DWORD                   AllocationAlignment
    );

//******************************************************************************
// Function:     MmuFreePoolWithTag
// Description:  Frees a previously allocated memory region.
// Returns:      void
// Parameter:    PVOID MemoryAddress
// Parameter:    IN DWORD Tag - Must match the tag used for allocating the
//               memory region.
//******************************************************************************
void
MmuFreePoolWithTag(
    _Pre_notnull_ _Post_ptr_invalid_
            PVOID                   MemoryAddress,
    IN      DWORD                   Tag
    );

//******************************************************************************
// Function:     MmuProbeMemory
// Description:  Ensures the virtual memory described by the Buffer is mapped
//               to physical addresses.
// Returns:      void
// Parameter:    IN PVOID Buffer
// Parameter:    IN DWORD NumberOfBytes
//******************************************************************************
void
MmuProbeMemory(
    IN      PVOID                   Buffer,
    IN      DWORD                   NumberOfBytes
    );

//******************************************************************************
// Function:     MmuSolvePageFault
// Description:  Attempts to handle the page fault ocurrred when accessing
//               FaultingAddress.
// Returns:      BOOLEAN
// Parameter:    IN PVOID FaultingAddress
// Parameter:    IN DWORD ErrorCode
//******************************************************************************
BOOLEAN
MmuSolvePageFault(
    IN      PVOID                   FaultingAddress,
    IN      DWORD                   ErrorCode
    );

//******************************************************************************
// Function:     MmuLoadPe
// Description:  Maps a PE eagerly to a VA using the paging structures specified
//               as a parameter. Currently the file alignment and the section
//               alignment need to be equal for this to be possible.
// Returns:      STATUS
// Parameter:    IN PPE_NT_HEADER_INFO NtHeader - The parsed PE header
// Parameter:    IN PPAGING_LOCK_DATA PagingData - The paging data of the process
//               in which to map.
//******************************************************************************
STATUS
MmuLoadPe(
    IN      PPE_NT_HEADER_INFO      NtHeader,
    IN      PPAGING_LOCK_DATA       PagingData
    );

//******************************************************************************
// Function:     MmuCreateAddressSpaceForProcess
// Description:  Creates both the physical paging structures and the VAS for a
//               process.
// Returns:      STATUS
// Parameter:    INOUT PPROCESS Process
//******************************************************************************
STATUS
MmuCreateAddressSpaceForProcess(
    INOUT   PPROCESS                Process
    );

//******************************************************************************
// Function:     MmuDestroyAddressSpaceForProcess
// Description:  Destroys a previously created address space.
// Returns:      void
// Parameter:    INOUT PPROCESS Process
//******************************************************************************
void
MmuDestroyAddressSpaceForProcess(
    INOUT   PPROCESS                Process
    );

//******************************************************************************
// Function:     MmuInitVirtualSpaceForSystemProcess
// Description:  Initializes the address space for the system process.
// Returns:      void
//******************************************************************************
_No_competing_thread_
void
MmuInitAddressSpaceForSystemProcess(
    void
    );

//******************************************************************************
// Function:     MmuActivateProcessIds
// Description:  Activates PCIDs for the current CPU and reloads the CR3 with
//               the running processes's ID.
// Returns:      void
// Parameter:    void
//******************************************************************************
void
MmuActivateProcessIds(
    void
    );

//******************************************************************************
// Function:     MmuActivateProcessIds
// Description:  Switches to the address space of a different process.
// Returns:      void
// Parameter:    IN PPROCESS Process
//******************************************************************************
void
MmuChangeProcessSpace(
    IN          PPROCESS            Process
    );

//******************************************************************************
// Function:     MmuAllocStack
// Description:  Allocates a stack of StackSize bytes.
// Returns:      PVOID
// Parameter:    IN DWORD StackSize
// Parameter:    IN BOOLEAN ProtectStack - if TRUE STACK_GUARD_SIZE of additional
//               bytes will be reserved in the virtual space at the end of the
//               stack to detect stack overflows.
// Parameter:    IN BOOLEAN LazyMap
// Parameter:    IN_OPT PPROCESS Process - if non-NULL a user-mode stack will
//               be allocated.
//******************************************************************************
PTR_SUCCESS
PVOID
MmuAllocStack(
    IN          DWORD               StackSize,
    IN          BOOLEAN             ProtectStack,
    IN          BOOLEAN             LazyMap,
    IN_OPT      PPROCESS            Process
    );

//******************************************************************************
// Function:     MmuFreeStack
// Description:  Frees a previously allocated stack with MmuAllocStack.
// Returns:      void
// Parameter:    IN PVOID Stack
// Parameter:    IN_OPT PPROCESS Process
//******************************************************************************
void
MmuFreeStack(
    IN          PVOID               Stack,
    IN_OPT      PPROCESS            Process
    );

//******************************************************************************
// Function:     MmuIsBufferValid
// Description:  Verifies if Buffer of size BufferSize can be accessed with
//               RightsRequested access rights by the Process process.
// Returns:      STATUS
// Parameter:    IN PVOID Buffer
// Parameter:    IN QWORD BufferSize
// Parameter:    IN PAGE_RIGHTS RightsRequested
// Parameter:    IN PPROCESS Process
//******************************************************************************
STATUS
MmuIsBufferValid(
    IN          PVOID               Buffer,
    IN          QWORD               BufferSize,
    IN          PAGE_RIGHTS         RightsRequested,
    IN          PPROCESS            Process
    );

//******************************************************************************
// Function:     MmuGetSystemVirtualAddressForUserBuffer
// Description:  Maps the physical memory which backs UserAddress from the
//               Process process into kernel space with PageRights rights
// Returns:      STATUS
// Parameter:    IN PVOID UserAddress
// Parameter:    IN QWORD Size
// Parameter:    IN PAGE_RIGHTS PageRights
// Parameter:    IN PPROCESS Process
// Parameter:    OUT PVOID * KernelAddress
//******************************************************************************
STATUS
MmuGetSystemVirtualAddressForUserBuffer(
    IN          PVOID               UserAddress,
    IN          QWORD               Size,
    IN          PAGE_RIGHTS         PageRights,
    IN          PPROCESS            Process,
    OUT         PVOID*              KernelAddress
    );

//******************************************************************************
// Function:     MmuFreeSystemVirtualAddressForUserBuffer
// Description:  Frees previously a mapped user address with
//               MmuGetSystemVirtualAddressForUserBuffer.
// Returns:      void
// Parameter:    IN PVOID KernelAddress
//******************************************************************************
void
MmuFreeSystemVirtualAddressForUserBuffer(
    IN          PVOID               KernelAddress
    );
