#pragma once

#include "io.h"

FUNC_ListFunction           DumpVpb;

void
DumpDriverList(
    IN      PLIST_ENTRY     DriverList
    );

void
DumpDriver(
    IN      PDRIVER_OBJECT  Driver
    );

void
DumpDeviceList(
    IN      PLIST_ENTRY     DeviceList
    );

void
DumpDevice(
    IN      PDEVICE_OBJECT  Device
    );
