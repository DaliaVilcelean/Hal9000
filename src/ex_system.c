#include "HAL9000.h"
#include "ex_system.h"
#include "thread_internal.h"

void
ExSystemTimerTick(
    void
    )
{
    ThreadTick();
}