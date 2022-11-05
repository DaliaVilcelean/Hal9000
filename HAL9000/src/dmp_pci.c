#include "HAL9000.h"
#include "dmp_pci.h"

static
__forceinline
char*
_PciCapabilityIdToString(
    IN PCI_CAPABILITY_ID    Id
    )
{
    switch (Id)
    {
    case PCI_CAPABILITY_ID_RESERVED:
        return "RESERVED";
    case PCI_CAPABILITY_ID_POWER_MGMT:
        return "Power Management";
    case PCI_CAPABILITY_ID_MSI:
        return "MSI";
    case PCI_CAPABILITY_ID_VENDOR:
        return "Vendor specific";
    case PCI_CAPABILITY_ID_HOT_PLUG:
        return "PCI Hot Plug";
    case PCI_CAPABILITY_ID_BRIDGE_SUB_VENDOR:
        return "PCI Bridge Subsystem Vendor";
    case PCI_CAPABILITY_ID_SECURE_DEVICE:
        return "PCI Secure Device";
    case PCI_CAPABILITY_ID_PCI_EXPRESS:
        return "PCI Express";
    case PCI_CAPABILITY_ID_MSIX:
        return "MSI-X";
    default:
        return "UNKNOWN";
    }
}

static
void
_DumpPciHeader(
    IN PPCI_COMMON_HEADER Header
    );

static
void
_DumpPciCapabilities(
    IN PPCI_DEVICE          Device
    );

static
void
_DumpPciCapability(
    IN PPCI_CAPABILITY_HEADER   Capability
    );

void
DumpPciDevice(
    IN  PPCI_DEVICE_DESCRIPTION     Device
    )
{
    if (NULL == Device)
    {
        return;
    }

    LOG("PCI device at (%d.%d.%d)\n",
        Device->DeviceLocation.Bus,
        Device->DeviceLocation.Device,
        Device->DeviceLocation.Function
        );

    _DumpPciHeader(&Device->DeviceData->Header);
    _DumpPciCapabilities(Device->DeviceData);

    LOG("\n");
}

static
void
_DumpPciHeader(
    IN PPCI_COMMON_HEADER Header
    )
{
    BOOLEAN bBridge;

    ASSERT( NULL != Header );

    LOG("Vendor ID: 0x%04x\n", Header->VendorID);
    LOG("Device ID: 0x%04x\n", Header->DeviceID);
    LOG("Class Code: 0x%x\n", Header->ClassCode);
    LOG("Subclass: 0x%x\n", Header->Subclass);
    LOG("Header type: 0x%x\n", Header->HeaderType );
    LOG("Is multifunction: 0x%x\n", Header->HeaderType.Multifunction );

    bBridge = Header->HeaderType.Layout == PCI_HEADER_LAYOUT_PCI_TO_PCI;

    for (DWORD i = 0;
         i < (bBridge ? PCI_BRIDGE_NO_OF_BARS : PCI_DEVICE_NO_OF_BARS);
         ++i)
    {
        LOG("Bar[%d] = 0x%x\n", i, Header->Device.Bar[i]);
    }

    if(bBridge)
    {
        LOG("Primary bus number: 0x%x\n", Header->Bridge.PrimaryBusNumber );
        LOG("Secondary bus number: 0x%x\n", Header->Bridge.SecondaryBusNumber );
        LOG("Subordinate bus number: 0x%x\n", Header->Bridge.SubordinateBusNumber );
    }

    LOG("Interrupt PIN: %d\n", Header->Device.InterruptPin );
    LOG("Interrupt line: %d\n", Header->Device.InterruptLine );

    LOG("Status: 0x%x\n", Header->Status.Value );
    LOG("Device control: 0x%x\n", Header->Command );
    LOG("Program IF: 0x%x\n", Header->ProgIF );
}

static
void
_DumpPciCapabilities(
    IN PPCI_DEVICE          Device
    )
{
    STATUS status;
    PPCI_CAPABILITY_HEADER pciCap;

    ASSERT( NULL != Device );

    status = STATUS_SUCCESS;
    pciCap = NULL;

    LOG("Device capabilities list:\n");

    // warning C4127: conditional expression is constant
#pragma warning(suppress:4127)
    while(TRUE)
    {
        status = PciDevRetrieveNextCapability(Device, pciCap, &pciCap );
        if (!SUCCEEDED(status))
        {
            if (STATUS_DEVICE_CAPABILITIES_NOT_SUPPORTED == status)
            {
                LOG("Device does not have capabilities\n");
            }
            break;
        }

        _DumpPciCapability(pciCap);


    }

}

static
void
_DumpPciCapability(
    IN PPCI_CAPABILITY_HEADER   Capability
    )
{
    ASSERT( NULL != Capability );

    LOG("Next capability pointer: 0x%x\n", Capability->NextPointer);
    LOG("Capability ID: 0x%x [%s]\n", Capability->CapabilityId, _PciCapabilityIdToString(Capability->CapabilityId));

    switch (Capability->CapabilityId)
    {
    case PCI_CAPABILITY_ID_MSI:
        {
            PPCI_CAPABILITY_MSI capMsi = (PPCI_CAPABILITY_MSI) Capability;

            LOG("Per-vector masking capable: [%s]\n", capMsi->MessageControl.PerVectorMasking ? "YES" : "NO" );
            LOG("64 bit capable: [%s]\n", capMsi->MessageControl.Is64BitCapable ? "YES" : "NO" );
            LOG("Multiple message capable: 0x%x\n", 1UL << capMsi->MessageControl.MultipleMessageCapable );
            LOG("MSI enabled: [%s]\n", capMsi->MessageControl.MsiEnable ? "YES" : "NO" );

            if (capMsi->MessageControl.Is64BitCapable)
            {
                LOG("Message address: 0x%X\n", DWORDS_TO_QWORD(capMsi->Capability64Bit.MessageAddressHigher, capMsi->MessageAddressLower.Raw));
                LOG("Message data: 0x%x\n", capMsi->Capability64Bit.MessageData);
            }
            else
            {
                LOG("Message address: 0x%x\n", capMsi->MessageAddressLower.Raw);
                LOG("Message data: 0x%x\n", capMsi->Capability32Bit.MessageData);
            }
        }
    }
}