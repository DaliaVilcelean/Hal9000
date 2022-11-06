#include "HAL9000.h"
#include "hal_assert.h"
#include "cpumu.h"
#include "smp.h"
#include "thread.h"

static volatile DWORD m_osAsserted = 0;

void
(__cdecl Hal9000Assert)(
    IN_Z            char*       Message
    )
{
    DWORD assertValue;

    LogSetState(TRUE);

    if (SmpGetNumberOfActiveCpus() > 1)
    {
        assertValue = _InterlockedExchange(&m_osAsserted, 1);
        if (0 == assertValue)
        {
            // we are the first to assert
            LOGP_ERROR("Sending IPI to other processors\n");
            SmpSendPanic();
        }
    }

    LOGP_ERROR( "Kernel panic!\n");

    LOGP_ERROR("Thread: [%s]\n", ThreadGetName(NULL));

    LOGP_ERROR( "%s\n", Message);

    AssertFreeLock();

    CpuIntrDisable();
    __halt();
}