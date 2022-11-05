#pragma once

#include "io.h"

void
_No_competing_thread_
IomuPreinitSystem(
    void
    );

_No_competing_thread_
STATUS
IomuInitSystem(
    IN      WORD            CodeSelector,
    IN      BYTE            NumberOfTssStacks
    );

STATUS
IomuInitSystemAfterApWakeup(
    void
    );

STATUS
IomuInitSystemDriver(
    void
    );

STATUS
IomuLateInit(
    void
    );

const char*
IomuGetSystemPartitionPath(
    void
    );

PFILE_OBJECT
IomuGetSwapFile(
    void
    );

void
IomuAckInterrupt(
    IN      BYTE            InterruptIndex
    );

PDRIVER_OBJECT
IomuGetDriverByName(
    IN_Z    char*           DriverName
    );

void
IomuDriverInstalled(
    IN      PDRIVER_OBJECT  Driver
    );

PLIST_ENTRY
IomuGetPciDeviceList(
    void
    );

STATUS
IomuGetDevicesByType(
    IN_RANGE_UPPER(DeviceTypeMax)
                                    DEVICE_TYPE         DeviceType,
    _When_(*NumberOfDevices > 0, OUT_PTR)
    _When_(*NumberOfDevices == 0, OUT_PTR_MAYBE_NULL)
                                    PDEVICE_OBJECT**    DeviceObjects,
    OUT                             DWORD*              NumberOfDevices
    );

void
IomuNewVpbCreated(
    INOUT       struct _VPB*        Vpb
    );

void
IomuExecuteForEachVpb(
    IN          PFUNC_ListFunction  Function,
    IN_OPT      PVOID               Context,
    IN          BOOLEAN             Exclusive
    );

PTR_SUCCESS
PVPB
IomuSearchForVpb(
    IN          char                DriveLetter
    );

STATUS
IomuRegisterInterrupt(
    IN          PIO_INTERRUPT           Interrupt,
    IN_OPT      PDEVICE_OBJECT          DeviceObject,
    OUT_OPT     PBYTE                   Vector
    );

QWORD
IomuGetSystemTicks(
    OUT_OPT     QWORD*                  TickFrequency
    );

QWORD
IomuGetSystemTimeUs(
    void
    );

QWORD
IomuTickCountToUs(
    IN          QWORD                   TickCount
    );

void
IomuCmosUpdateOccurred(
    void
    );

DWORD
IomuGetTimerInterrupTimeUs(
    void
    );

BOOLEAN
IomuIsInterruptSpurious(
    IN          BYTE                    Vector
    );