#include "HAL9000.h"
#include "print.h"
#include "log.h"
#include "serial_comm.h"
#include "synch.h"

#define INFO_LEVEL_MODIFIER         ""
#define WARNING_LEVEL_MODIFIER      "[WARNING]"
#define ERROR_LEVEL_MODIFIER        "[ERROR]"

#define LOG_BUF_MAX_SIZE            512

typedef struct _LOG_DATA
{
    LOCK                        Lock;

    _Interlocked_
    volatile BOOLEAN            Enabled;

    _Interlocked_
    volatile LOG_LEVEL          LoggingLevel;

    _Interlocked_
    volatile LOG_COMPONENT      LoggingComponents;
} LOG_DATA, *PLOG_DATA;

static LOG_DATA m_logData;

static
void
_LogBufferInternal(
    IN_Z        char*                   Buffer,
    IN          PFUNC_PrintFunction     PrintFunction
    );

_No_competing_thread_
void
LogSystemPreinit(
    void
    )
{
    memzero(&m_logData, sizeof(LOG_DATA));

    LockInit(&m_logData.Lock);
}

_No_competing_thread_
void
LogSystemInit(
    IN _Strict_type_match_
                LOG_LEVEL       LogLevel,
    IN
                LOG_COMPONENT   LogComponenets,
    IN          BOOLEAN         Enable
    )
{
    m_logData.Enabled = Enable;
    m_logData.LoggingComponents = LogComponenets;
    m_logData.LoggingLevel = LogLevel;
}

void
LogEx(
    IN _Strict_type_match_
                LOG_LEVEL       LogLevel,
    IN
                LOG_COMPONENT   LogComponent,
    IN_Z        char*           FormatBuffer,
    ...
    )
{
    char logBuffer[LOG_BUF_MAX_SIZE];
    char* pLevelModifier;
    DWORD modifierLength;
    PFUNC_PrintFunction printFunction;
    va_list va;

    if (!m_logData.Enabled)
    {
        return;
    }

    if (LogLevel < m_logData.LoggingLevel)
    {
        // logging is not activated for this level
        return;
    }


    if (LogLevel == LogLevelTrace &&
        !IsFlagOn(m_logData.LoggingComponents, LogComponent))
    {
        // logging is not activated for this component
        return;
    }

    pLevelModifier = NULL;
    printFunction = NULL;
    modifierLength = MAX_DWORD;

    switch (LogLevel)
    {
    case LogLevelTrace:
    case LogLevelInfo:
        pLevelModifier = INFO_LEVEL_MODIFIER;
        printFunction = printf;
        modifierLength = sizeof(INFO_LEVEL_MODIFIER);
        break;
    case LogLevelWarning:
        pLevelModifier = WARNING_LEVEL_MODIFIER;
        printFunction = pwarn;
        modifierLength = sizeof(WARNING_LEVEL_MODIFIER);
        break;
    case LogLevelError:
        pLevelModifier = ERROR_LEVEL_MODIFIER;
        printFunction = perror;
        modifierLength = sizeof(ERROR_LEVEL_MODIFIER);
        break;
    }
    ASSERT(pLevelModifier != NULL);
    ASSERT(printFunction != NULL);
    ASSERT(modifierLength != MAX_DWORD);

    // remove length of NULL terminator
    modifierLength = modifierLength - 1;

    // log buffer will start with level modifier
    strcpy(logBuffer, pLevelModifier);

    va_start(va, FormatBuffer);

    // resolve formatted buffer
    vsnprintf(logBuffer + modifierLength, 
              LOG_BUF_MAX_SIZE - modifierLength, FormatBuffer, va);

    _LogBufferInternal(logBuffer, printFunction);
}

BOOLEAN
LogSetState(
    IN          BOOLEAN         Enable
    )
{
    return _InterlockedExchange8(&m_logData.Enabled, Enable );
}

LOG_LEVEL
LogGetLevel(
    void
    )
{
    return m_logData.LoggingLevel;
}

LOG_LEVEL
LogSetLevel(
    IN          LOG_LEVEL   NewLogLevel
    )
{
    return _InterlockedExchange(&m_logData.LoggingLevel, (DWORD) NewLogLevel);
}

LOG_COMPONENT
LogGetTracedComponents(
    void
    )
{
    return m_logData.LoggingComponents;
}

LOG_COMPONENT
LogSetTracedComponents(
    IN          LOG_COMPONENT   Components
    )
{
    return _InterlockedExchange(&m_logData.LoggingComponents, (DWORD) Components);
}

static
void
_LogBufferInternal(
    IN_Z        char*                   Buffer,
    IN          PFUNC_PrintFunction     PrintFunction
    )
{
    INTR_STATE oldState;

    ASSERT(NULL != Buffer);

    LockAcquire(&m_logData.Lock, &oldState);

    PrintFunction(Buffer);

    // also write through the serial port
    SerialCommWriteBuffer(Buffer);

    LockRelease(&m_logData.Lock, oldState);
}
