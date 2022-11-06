#pragma once

void
CorePreinit(
    void
    );

STATUS
CoreInit(
    void
    );

void
CoreUpdateIdleTime(
    IN  BOOLEAN             IdleScheduled
    );

DWORD
CoreSetIdlePeriod(
    IN  DWORD               SecondsForIdle
    );