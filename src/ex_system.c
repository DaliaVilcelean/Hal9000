#include "HAL9000.h"
#include "ex_system.h"
#include "thread_internal.h"
#include "ex_timer.h"

void
ExSystemTimerTick(
    void
)
{
    ThreadTick();
    ExTimerCheckAll();
}