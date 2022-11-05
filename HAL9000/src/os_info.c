#include "HAL9000.h"
#include "os_info.h"
#include "display.h"
#include "os_time.h"
#include "rtc.h"
#include "iomu.h"

#define TIME_BUFFER_SIZE                    100

#define OS_NAME                             "HAL9000"
#define OS_VERSION                          "1.8"

#define OS_VERSION_COLUMN                   0

static
void
_OsInfoUpdateTime(
    void
    );

void
OsInfoPreinit(
    void
    )
{
    DWORD osNameLength;
    BYTE middlePosition;

    osNameLength = 0;
    middlePosition = 0;

    // clear title line
    DispClearLine(0);

    osNameLength = strlen(OS_NAME);
    ASSERT(osNameLength <= CHARS_PER_LINE);

    // place OS version
    DispPutBufferColor(OS_VERSION, 0, OS_VERSION_COLUMN, CYAN_COLOR);

    // we want the OS name to be printed on the middle of the line
    middlePosition = (CHARS_PER_LINE - (BYTE) osNameLength ) / 2;

    DispPutBufferColor(OS_NAME, 0, middlePosition, BRIGHT_GREEN_COLOR);

    // update time
    _OsInfoUpdateTime();
}

STATUS
OsInfoInit(
    void
    )
{
    return STATUS_SUCCESS;
}

const
char*
OsInfoGetName(
    void
    )
{
    return OS_NAME;
}

const
char*
OsGetBuildDate(
    void
    )
{
    return __TIMESTAMP__;
}

const
char*
OsGetBuildType(
    void
    )
{
#ifdef DEBUG
    return "Debug";
#else
    return "Release";
#endif
}

const
char*
OsGetVersion(
    void
    )
{
    return OS_VERSION;
}


static
void
_OsInfoUpdateTime(
    void
    )
{
    char timeBuffer[TIME_BUFFER_SIZE];

    memzero(timeBuffer, TIME_BUFFER_SIZE);

    OsTimeGetStringFormattedTime(NULL, timeBuffer, TIME_BUFFER_SIZE);

    DispPutBufferColor(timeBuffer, 0, CHARS_PER_LINE - (BYTE)strlen(timeBuffer), CYAN_COLOR);
}

BOOLEAN
(__cdecl OsInfoTimeUpdateIsr)(
    IN      PDEVICE_OBJECT  Device
    )
{
    ASSERT(NULL != Device);

    RtcAcknowledgeTimerInterrupt();
    IomuCmosUpdateOccurred();
    _OsInfoUpdateTime();

    // we handled the interrupt
    return TRUE;
}