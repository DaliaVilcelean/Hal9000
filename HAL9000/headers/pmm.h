#pragma once

#include "mmu.h"

#define PmmReserveMemory(Frames)        PmmReserveMemoryEx((Frames), NULL )

_No_competing_thread_
void
PmmPreinitSystem(
    void
    );

_No_competing_thread_
STATUS
PmmInitSystem(
    IN          PVOID                   BaseAddress,
    IN          PHYSICAL_ADDRESS        MemoryEntries,
    IN          DWORD                   NumberOfMemoryEntries,
    OUT         DWORD*                  SizeReserved
    );

//******************************************************************************
// Function:     PmmRequestMemoryEx
// Description:  Reserves the first free frames available after MinPhysAddr.
// Returns:      PHYSICAL_ADDRESS - start address of physical address reserved
// Parameter:    IN DWORD NoOfFrames - frames to reserved.
// Parameter:    IN_OPT PHYSICAL_ADDRESS MinPhysAddr - physical address from
//               which to start searching for free frames.
//******************************************************************************
PTR_SUCCESS
PHYSICAL_ADDRESS
PmmReserveMemoryEx(
    IN          DWORD                   NoOfFrames,
    IN_OPT      PHYSICAL_ADDRESS        MinPhysAddr
    );

//******************************************************************************
// Function:     PmmReleaseMemory
// Description:  Releases previously reserved memory
// Returns:      void
// Parameter:    IN PHYSICAL_ADDRESS PhysicalAddr
// Parameter:    IN DWORD NoOfFrames
//******************************************************************************
void
PmmReleaseMemory(
    IN          PHYSICAL_ADDRESS        PhysicalAddr,
    IN          DWORD                   NoOfFrames
    );

//******************************************************************************
// Function:     PmmGetTotalSystemMemory
// Description:
// Returns:      QWORD - Returns the number of bytes of physical memory
//               available in the system.
// Parameter:    void
//******************************************************************************
QWORD
PmmGetTotalSystemMemory(
    void
    );

// Note: This address may reserved by the firmware or some other device.
// If you want to retrieve the highest available physical address for software
// usage use PmmGetHighestPhysicalMemoryAddressAvailable
PHYSICAL_ADDRESS
PmmGetHighestPhysicalMemoryAddressPresent(
    void
    );

PHYSICAL_ADDRESS
PmmGetHighestPhysicalMemoryAddressAvailable(
    void
    );
