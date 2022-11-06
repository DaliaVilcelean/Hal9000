#pragma once

#include "scan_codes.h"

KEYCODE
getch(
    void
    );

void
gets_s(
    OUT_WRITES_Z(BufferSize)    char*       Buffer,
    IN                          DWORD       BufferSize,
    OUT                         DWORD*      UsedSize
    );