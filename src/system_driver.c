#include "HAL9000.h"
#include "io.h"
#include "system_driver.h"

STATUS
(__cdecl SystemDriverEntry)(
    INOUT       PDRIVER_OBJECT      DriverObject
    )
{
    PDEVICE_OBJECT pDev;

    ASSERT( NULL != DriverObject );

    LOG_FUNC_START;

    pDev = IoCreateDevice(DriverObject,
                          0,
                          DeviceTypeSystem
                          );
    ASSERT( NULL != pDev );

    LOG_FUNC_END;

    return STATUS_SUCCESS;
}