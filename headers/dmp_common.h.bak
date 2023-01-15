#pragma once

void
DumpPreinit(
    void
    );

REQUIRES_NOT_HELD_LOCK(m_dumpLock)
RELEASES_EXCL_AND_REENTRANT_LOCK(m_dumpLock)
INTR_STATE
DumpTakeLock(
    void
    );

REQUIRES_EXCL_LOCK(m_dumpLock)
RELEASES_EXCL_AND_REENTRANT_LOCK(m_dumpLock)
void
DumpReleaseLock(
    IN INTR_STATE   OldIntrState
    );