#include "HAL9000.h"
#include "os_time.h"
#include "cmos.h"

DATETIME
OsTimeGetCurrentDateTime(
    void
    )
{
    DATETIME result;
    CMOS_DATA cmosData;

    memzero(&cmosData, sizeof(CMOS_DATA));

    CmosReadData(&cmosData);

    // set date
    result.Date.Year = cmosData.Year;
    result.Date.Month = cmosData.Month;
    result.Date.Day = cmosData.Day;

    // set time
    result.Time.Hour = cmosData.Hour;
    result.Time.Minute = cmosData.Minute;
    result.Time.Second = cmosData.Second;

    return result;
}

STATUS
OsTimeGetStringFormattedTime(
    IN_OPT                      PDATETIME       DateTime,
    OUT_WRITES_Z(BufferSize)    char*           Buffer,
    IN                          DWORD           BufferSize
    )
{
    PDATETIME pDateTimeToFormat;
    DATETIME dateTime;

    if (NULL == DateTime)
    {
        dateTime = OsTimeGetCurrentDateTime();

        pDateTimeToFormat = &dateTime;
    }
    else
    {
        pDateTimeToFormat = DateTime;
    }

    return TimeGetStringFormattedBuffer(*pDateTimeToFormat,
                                        Buffer,
                                        BufferSize
                                        );
}