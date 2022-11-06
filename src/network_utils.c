#include "HAL9000.h"
#include "network_utils.h"

char*
NetUtilMacAddressToText(
    IN                                                          MAC_ADDRESS         Address,
    OUT_WRITES_BYTES_ALL(TEXT_MAC_ADDRESS_CHARS_REQUIRED)       char*               Buffer
    )
{
    ASSERT( NULL != Buffer );

    snprintf(Buffer,
             TEXT_MAC_ADDRESS_CHARS_REQUIRED,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             Address.Value[0],
             Address.Value[1],
             Address.Value[2],
             Address.Value[3],
             Address.Value[4],
             Address.Value[5]);

    return Buffer;
}

char*
NetUtilIp4AddressToText(
    IN                                                          IP4_ADDRESS         Address,
    OUT_WRITES_BYTES_ALL(TEXT_IP4_ADDRESS_CHARS_REQUIRED)       char*               Buffer
    )
{
    ASSERT(NULL != Buffer);

    snprintf(Buffer,
             TEXT_IP4_ADDRESS_CHARS_REQUIRED,
             "%u.%u.%u.%u",
             Address.ByteAddress[0],
             Address.ByteAddress[1],
             Address.ByteAddress[2],
             Address.ByteAddress[3]
             );

    return Buffer;
}