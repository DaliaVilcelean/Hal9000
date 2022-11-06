#pragma once

void
DumpPreinit(
    void
    );

REQUIRES_NOT_HELD_LOCK(m_dumpLock)
RELEASES_EXCL_AND_REENTRANT_LOCK(m_dumpLock)
INTR_STATE
// Warning C28285 For function 'DumpTakeLock' 'return' syntax error
// This is because SAL does not know who m_dumpLock is
#pragma warning(suppress: 28285)
DumpTakeLock(
    void
    );

REQUIRES_EXCL_LOCK(m_dumpLock)
RELEASES_EXCL_AND_REENTRANT_LOCK(m_dumpLock)
void
// Warning C28285 For function 'DumpTakeLock' 'return' syntax error
// This is because SAL does not know who m_dumpLock is
#pragma warning(suppress: 28285)
DumpReleaseLock(
    IN INTR_STATE   OldIntrState
    );