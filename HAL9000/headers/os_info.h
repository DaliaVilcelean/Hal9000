#pragma once

#include "io.h"

void
OsInfoPreinit(
    void
    );

STATUS
OsInfoInit(
    void
    );

const
char*
OsInfoGetName(
    void
    );

const
char*
OsGetBuildDate(
    void
    );

const
char*
OsGetBuildType(
    void
    );

const
char*
OsGetVersion(
    void
    );

FUNC_InterruptFunction         OsInfoTimeUpdateIsr;