#include "HAL9000.h"
#include "cmd_sys_helper.h"
#include "ex.h"
#include "print.h"
#include "core.h"
#include "strutils.h"
#include "keyboard.h"
#include "acpi_interface.h"

#pragma warning(push)

// warning C4212: nonstandard extension used: function declaration used ellipsis
#pragma warning(disable:4212)

// warning C4029: declared formal parameter list different from definition
#pragma warning(disable:4029)

void
(__cdecl CmdDisplaySysInfo)(
    IN          QWORD       NumberOfParameters
    )
{
    SYSTEM_INFORMATION sysInfo;
    QWORD sizeInKB;
    QWORD frequencyMhz;
    QWORD uptimeInMs;

    ASSERT(NumberOfParameters == 0);

    ExGetSystemInformation(&sysInfo);

    sizeInKB = sysInfo.TotalPhysicalMemory / KB_SIZE;
    frequencyMhz = sysInfo.CpuFrequency / SEC_IN_US;
    uptimeInMs = sysInfo.SystemUptimeUs / MS_IN_US;

    printf("System memory: %U KB (%U MB)\n", sizeInKB, sizeInKB / KB_SIZE );
    printf("Highest physical memory: 0x%X\n", sysInfo.HighestPhysicalAddress );
    printf("CPU frequency: %u.%02u GHz\n", frequencyMhz / 1000, frequencyMhz % 1000 );
    printf("Uptime: %u.%03u sec\n", uptimeInMs / 1000, uptimeInMs % 1000 );
}

void
(__cdecl CmdSetIdle)(
    IN          QWORD       NumberOfParameters,
    IN_Z        char*       SecondsString
    )
{
    DWORD noOfSeconds;

    ASSERT(NumberOfParameters == 1);

    atoi32(&noOfSeconds, SecondsString, BASE_TEN);

    if (0 == noOfSeconds)
    {
        pwarn("Idle period must differ from 0\n");
        return;
    }

    printf("Setting idle period to %u seconds\n", noOfSeconds);
    CoreSetIdlePeriod(noOfSeconds);
}

void
(__cdecl CmdGetIdle)(
    IN          QWORD       NumberOfParameters
    )
{
    DWORD idlePeriod;

    ASSERT(NumberOfParameters == 0);

    idlePeriod = CoreSetIdlePeriod(0);
    printf("Idle period: %u seconds\n", idlePeriod );
}

void
(__cdecl CmdResetSystem)(
    IN          QWORD       NumberOfParameters
    )
{
    ASSERT(NumberOfParameters == 0);

    KeyboardResetSystem();
}

void
(__cdecl CmdShutdownSystem)(
    IN          QWORD       NumberOfParameters
    )
{
    ASSERT(NumberOfParameters == 0);

    AcpiShutdown();
}

#pragma warning(pop)
