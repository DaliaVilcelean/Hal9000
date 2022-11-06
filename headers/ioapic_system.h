#pragma once

#include "apic_common.h"

_No_competing_thread_
void
IoApicSystemPreinit(
    void
    );

_No_competing_thread_
STATUS
IoApicSystemInit(
    void
    );

_No_competing_thread_
STATUS
IoApicLateSystemInit(
    void
    );

_No_competing_thread_
void
IoApicSystemEnableRegisteredInterrupts(
    void
    );

STATUS
IoApicSystemSetInterrupt(
    IN      BYTE                    Irq,
    IN      BYTE                    Vector,
    IN _Strict_type_match_
            APIC_DESTINATION_MODE   DestinationMode,
    IN _Strict_type_match_
            APIC_DELIVERY_MODE      DeliveryMode,
    IN _Strict_type_match_
            APIC_PIN_POLARITY       PinPolarity,
    IN  _Strict_type_match_
            APIC_TRIGGER_MODE       TriggerMode,
    IN      APIC_ID                 Destination,
    IN      BOOLEAN                 Overwrite
    );

void
IoApicSystemMaskInterrupt(
    IN      BYTE                    Irq,
    IN      BOOLEAN                 Mask
    );

STATUS
IoApicSystemGetVectorForIrq(
    IN      BYTE                    Irq,
    OUT     PBYTE                   Vector
    );

DWORD
IoApicGetInterruptLineForPciDevice(
    IN      struct _PCI_DEVICE_DESCRIPTION* PciDevice
    );

__declspec(deprecated)
void
IoApicSystemSendEOI(
    IN      BYTE                Vector
    );