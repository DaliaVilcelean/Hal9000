#pragma once

#include "scan_codes.h"

// A Make Code is sent when a key is pressed or held down
// A Break code is sent when a key is released

STATUS
KeyboardInitialize(
    IN      BYTE        InterruptIrq
    );

_Success_(KEY_UNKNOWN!=return)
KEYCODE
KeyboardGetLastKey(
    void
    );

_Success_(KEY_UNKNOWN != return)
KEYCODE
KeyboardWaitForKey(
    void
    );

void
KeyboardDiscardLastKey(
    void
    );

char
KeyboardKeyToAscii(
    IN      KEYCODE     KeyCode
    );

void
KeyboardResetSystem(
    void
    );