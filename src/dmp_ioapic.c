#include "HAL9000.h"
#include "ioapic.h"
#include "dmp_common.h"
#include "../../HAL/headers/ioapic_registers.h"

__forceinline
static
const
char*
_DeliveryModeToString(
    IN      APIC_DELIVERY_MODE      DeliveryMode
    )
{
    switch (DeliveryMode)
    {
    case ApicDeliveryModeFixed:
        return "FIXED";
    case ApicDeliveryModeLowest:
        return "LOWEST";
    case ApicDeliveryModeSMI:
        return "SMI";
    case ApicDeliveryModeNMI:
        return "NMI";
    case ApicDeliveryModeINIT:
        return "INIT";
    case ApicDeliveryModeSIPI:
        return "SIPI";
    case ApicDeliveryModeExtINT:
        return "EXTINT";
    default:
        return "UNKNOWN";
    }
}

extern IO_APIC_REDIR_TABLE_ENTRY
_IoApicReadRedirectionEntry(
    IN      PIO_APIC        IoApic,
    IN      BYTE            Index
    );

void
DumpIoApic(
    IN      PVOID       IoApicBaseAddress
    )
{
    INTR_STATE oldState;

    ASSERT( NULL != IoApicBaseAddress );

    oldState = DumpTakeLock();

    LOG("ELCR 1: 0b%08b\n", __inbyte(0x4d0));
    LOG("ELCR 2: 0b%08b\n", __inbyte(0x4d1));

    LOG("IO Apic ID: 0x%x\n", IoApicGetId(IoApicBaseAddress));
    LOG("Apic version: 0x%x\n", IoApicGetVersion(IoApicBaseAddress));
    LOG("Max redirection entry: 0x%x\n", IoApicGetMaximumRedirectionEntry(IoApicBaseAddress));
    LOG("%6s", "Index|");
    LOG("%7s", "Vector|");
    LOG("%7s", "Masked|");
    LOG("%14s", "Delivery Mode|");
    LOG("%17s", "Destination Mode|");
    LOG("%8s", "Trigger|");
    LOG("%9s", "Polarity|");
    LOG("%8s", "APIC ID|");
    LOG("\n");
    for (BYTE i = 0; i <= IoApicGetMaximumRedirectionEntry(IoApicBaseAddress); ++i)
    {
        IO_APIC_REDIR_TABLE_ENTRY entry = _IoApicReadRedirectionEntry(IoApicBaseAddress,i);

        LOG("%3c%02x|", ' ', i );
        LOG("%4c%02x|", ' ', entry.Vector);
        LOG("%6s|", entry.Masked ? "YES" : "NO");
        LOG("%13s|", _DeliveryModeToString(entry.DeliveryMode));
        LOG("%16s|", entry.DestinationMode == ApicDestinationModePhysical ? "PHYSICAL" : "LOGICAL" );
        LOG("%7s|", entry.TriggerMode == ApicTriggerModeEdge ? "EDGE" : "LEVEL" );
        LOG("%8s|", entry.PinPolarity == ApicPinPolarityActiveHigh ? "HIGH" : "LOW" );
        LOG("%5c%02x|", ' ', entry.Destination );

        LOG("\n");
    }

    DumpReleaseLock(oldState);
}