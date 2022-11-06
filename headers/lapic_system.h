#pragma once

#include "lapic.h"

STATUS
LapicSystemInit(
    void
    );

STATUS
LapicSystemInitializeCpu(
    IN      BYTE                            TimerInterruptVector
    );

// Disables or enables the LAPIC in SW
void
LapicSystemSetState(
    IN      BOOLEAN                         Enable
    );

BOOLEAN
LapicSystemGetState(
    void
    );

void
LapicSystemSendEOI(
    IN      BYTE                            Vector
    );

//******************************************************************************
// Function:     LapicSystemSetTimer
// Description:  Enables the LAPIC timer on the current CPU to trigger every
//               Microseconds ms. If the argument is 0 the timer is stopped.
// Parameter:    IN DWORD Microseconds - Trigger period in microseconds.
// Parameter:    OUT_PTR PTHREAD * Thread
// NOTE:         This only programs the LAPIC timer on the current CPU.
//******************************************************************************
void
LapicSystemSetTimer(
    IN      DWORD                           Microseconds
    );

void
LapicSystemSendIpi(
    _When_(ApicDestinationShorthandNone == DeliveryMode, IN)
    _When_(ApicDestinationShorthandNone != DeliveryMode, _Reserved_)
            APIC_ID                         ApicId,
    IN      _Strict_type_match_
            APIC_DELIVERY_MODE              DeliveryMode,
    IN      _Strict_type_match_
            APIC_DESTINATION_SHORTHAND      DestinationShorthand,
    IN      _Strict_type_match_
            APIC_DESTINATION_MODE           DestinationMode,
    IN_OPT  BYTE*                           Vector
    );

BYTE
LapicSystemGetPpr(
    void
    );

QWORD
LapicSystemGetTimerElapsedUs(
    void
    );

BOOLEAN
LapicSystemIsInterruptServiced(
    IN      BYTE                            Vector
    );