#include "HAL9000.h"
#include "dmp_net_device.h"
#include "network_utils.h"
#include "dmp_common.h"

void
DumpNetworkDevice(
    IN      PNETWORK_DEVICE_INFO        NetworkDevice
    )
{
    char macAddress[TEXT_MAC_ADDRESS_CHARS_REQUIRED];
    INTR_STATE intrState;

    ASSERT( NULL != NetworkDevice );

    intrState = DumpTakeLock();
    LOG("Device ID: 0x%x\n", NetworkDevice->DeviceId );

    NetUtilMacAddressToText(NetworkDevice->PhysicalAddress, macAddress);

    LOG("Mac address [%s]\n", macAddress );
    LOG("Link status is [%s]\n", NetworkDevice->LinkStatus ? "UP" : "DOWN" );
    LOG("Device RX is [%s]\n", 
        NetworkDevice->DeviceStatus.RxEnabled ? "ENABLED" : "DISABLED"
        );
    LOG("Device TX is [%s]\n",
        NetworkDevice->DeviceStatus.TxEnabled ? "ENABLED" : "DISABLED"
        );
    DumpReleaseLock(intrState);
}