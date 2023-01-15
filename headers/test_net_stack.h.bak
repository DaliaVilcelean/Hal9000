#pragma once

#include "network.h"

_No_competing_thread_
BOOLEAN
TestNetwork(
        IN      BOOLEAN         Transmit,
    _When_(Transmit, _Reserved_)
    _When_(!Transmit, IN)
        IN      BOOLEAN         ResendRequets
    );