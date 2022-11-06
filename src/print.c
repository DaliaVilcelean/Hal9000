#include "HAL9000.h"
#include "print.h"
#include "display.h"
#include "va_list.h"
#include "synch.h"

#define SCREEN_BUFFER_SIZE          MAX_PATH

static char m_printBuffer[SCREEN_BUFFER_SIZE] = { 0 };
static LOCK m_printLock;

void
printSystemPreinit(
    IN_OPT  PVOID   DisplayAddress
    )
{
    PVOID pAddr;

    pAddr = NULL == DisplayAddress ? (PVOID) PA2VA(BASE_VIDEO_ADDRESS) : DisplayAddress;

    DispPreinitScreen( pAddr, 1, LINES_PER_SCREEN - 1);
    LockInit(&m_printLock);
}

static
void
_vprintColor(
    IN_Z    char*   Format,
    INOUT   va_list Args,
    IN      BYTE    Color
    )
{
    COLOR prevColor;
    INTR_STATE oldState;

    LockAcquire(&m_printLock, &oldState);

    vsnprintf(m_printBuffer, SCREEN_BUFFER_SIZE, Format, Args);
    
    prevColor = DispGetColor();
    DispSetColor(Color);
    
    DispPrintString(m_printBuffer);

    DispSetColor(prevColor);

    LockRelease(&m_printLock, oldState);
}

void
perror(
    IN_Z    char*   Format,
    ...
    )
{
    va_list argPtr;

    va_start(argPtr, Format);

    _vprintColor(Format, argPtr, RED_COLOR);
}

void
pwarn(
    IN_Z    char*   Format,
    ...
    )
{
    va_list argPtr;

    va_start(argPtr, Format);

    _vprintColor(Format, argPtr, YELLOW_COLOR);
}

void
printf(
    IN_Z    char*   Format,
    ...
    )
{
    va_list argPtr;

    va_start(argPtr, Format);

    _vprintColor(Format, argPtr, WHITE_COLOR);
}

void
printColor(
    IN      BYTE    Color,
    IN_Z    char*   Format,
    ...
    )
{
    va_list argPtr;

    va_start(argPtr, Format);

    _vprintColor(Format, argPtr, Color);
}