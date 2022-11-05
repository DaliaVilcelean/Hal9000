#include "HAL9000.h"
#include "dmp_network.h"
#include "network_utils.h"
#include "dmp_memory.h"
#include "dmp_common.h"

static
const
char*
_DumpEthTypeToString(
    IN      ETHERNET_FRAME_TYPE FrameType
    )
{
    switch (FrameType)
    {
    case ETHERNET_FRAME_TYPE_IP4:
        return "IP4";
    case ETHERNET_FRAME_TYPE_ARP:
        return "ARP";
    case ETHERNET_FRAME_TYPE_RARP:
        return "RARP";
    case ETHERNET_FRAME_TYPE_VLAN:
        return "VLAN";
    case ETHERNET_FRAME_TYPE_IP6:
        return "IP6";
    case ETHERNET_FRAME_TYPE_LLDP:
        return "LLDP";
    default:
        LOGL("Frame type: [0x%x]\n", FrameType);
        NOT_REACHED;
        return "UNKNOWN";
    }
}

static
const
char*
_DumpProtocolTypeToString(
    IN      IP_PROTOCOL     Protocol
    )
{
    switch (Protocol)
    {
    case IP_PROTOCOL_ICMP:
        return "ICMP";
    case IP_PROTOCOL_IGMP:
        return "IGMP";
    case IP_PROTOCOL_TCP:
        return "TCP";
    case IP_PROTOCOL_UDP:
        return "UDP";
    default:
        LOGL("Protocol: [0x%x]\n", Protocol);
        NOT_REACHED;
        return "UNKNOWN";
    }
}

void
DumpEthernetFrame(
    IN      PETHERNET_FRAME     Frame,
    IN      DWORD               BufferSize
    )
{
    ETHERNET_FRAME_TYPE frameType;
    char srcAddress[TEXT_MAC_ADDRESS_CHARS_REQUIRED];
    char dstAddress[TEXT_MAC_ADDRESS_CHARS_REQUIRED];
    DWORD remainingSize;
    INTR_STATE oldState;

    ASSERT( NULL != Frame );
    ASSERT( BufferSize >= sizeof(ETHERNET_FRAME) );

    NetUtilMacAddressToText(Frame->Source, srcAddress);
    NetUtilMacAddressToText(Frame->Destination, dstAddress);
    frameType = ntohw(Frame->Type);

    oldState = DumpTakeLock();
    LOG("Ethernet frame of type: 0x%x [%s]\nSource: [%s]\nDestination: [%s]\n\n", 
        frameType,
        _DumpEthTypeToString(frameType),
        srcAddress,
        dstAddress
        );

    remainingSize = BufferSize - sizeof(ETHERNET_FRAME);

    switch (frameType)
    {
    case ETHERNET_FRAME_TYPE_ARP:
        DumpArpPacket((PARP_PACKET)Frame->Data,
                      remainingSize
                      );
        break;
    case ETHERNET_FRAME_TYPE_IP4:
        DumpIp4Packet((PIP4_PACKET)Frame->Data,
                      remainingSize
                      );
        break;
    default:
        DumpMemory(Frame->Data,
                   0,
                   remainingSize,
                   FALSE,
                   TRUE
                   );
        break;
    }
    DumpReleaseLock(oldState);
}

void
DumpArpPacket(
    IN      PARP_PACKET         ArpPacket,
    IN      DWORD               BufferSize
    )
{
    char senderHwAddress[TEXT_MAC_ADDRESS_CHARS_REQUIRED];
    char senderPrtAddress[TEXT_IP4_ADDRESS_CHARS_REQUIRED];
    char targetHwAddress[TEXT_MAC_ADDRESS_CHARS_REQUIRED];
    char targetPrtAddress[TEXT_IP4_ADDRESS_CHARS_REQUIRED];
    ETHERNET_FRAME_TYPE protocolType;
    ARP_OPERATION arpOperation;
    HARDWARE_TYPE hwType;

    ASSERT( NULL != ArpPacket );
    ASSERT( BufferSize >= sizeof(ARP_PACKET) );

    hwType = ntohw(ArpPacket->HardwareType);
    protocolType = ntohw(ArpPacket->ProtocolType);
    arpOperation = ntohw(ArpPacket->Operation);

    if (HARDWARE_TYPE_ETHERNET != hwType)
    {
        LOG_WARNING("Unknown hardware type: 0x%x\n", ArpPacket->HardwareType );
        return;
    }

    if (MAC_ADDRESS_SIZE != ArpPacket->HardwareAddressLength)
    {
        LOG_WARNING("Unknown hardware address length: %u\n", ArpPacket->HardwareAddressLength );
        return;
    }

    if (ETHERNET_FRAME_TYPE_IP4 != protocolType)
    {
        LOG_WARNING("Unknown protocol type: 0x%x\n", protocolType );
        return;
    }

    if (IP4_ADDRESS_SIZE != ArpPacket->ProtocolAddressLength)
    {
        LOG_WARNING("Unknown protocol address length: %u\n", ArpPacket->ProtocolAddressLength);
        return;
    }

    LOG("ARP packet of type [%s]\n"
        "Sender HW address: [%s]\n"
        "Sender protocol address: [%s]\n"
        "Target HW address: [%s]\n"
        "Target protocol address: [%s]\n\n",
        ( ARP_OPERATION_REQUEST == arpOperation ) ? "Request" : "Reply",
        NetUtilMacAddressToText(ArpPacket->SenderHardwareAddress, senderHwAddress),
        NetUtilIp4AddressToText(ArpPacket->SenderProtocolAddress, senderPrtAddress),
        NetUtilMacAddressToText(ArpPacket->TargetHardwareAddress, targetHwAddress),
        NetUtilIp4AddressToText(ArpPacket->TargetProtocolAddress, targetPrtAddress)
        )
        ;
}

void
DumpIp4Packet(
    IN      PIP4_PACKET         Ip4Packet,
    IN      DWORD               BufferSize
    )
{
    char sourceAddress[TEXT_IP4_ADDRESS_CHARS_REQUIRED];
    char destinationAddress[TEXT_IP4_ADDRESS_CHARS_REQUIRED];
    DWORD dataLength;
    DWORD remainingBufferLength;

    ASSERT( NULL != Ip4Packet );
    ASSERT( BufferSize >= sizeof(IP4_PACKET) );

    dataLength = ntohw(Ip4Packet->Length) - (Ip4Packet->InternetHeaderLength * sizeof(DWORD));
    remainingBufferLength = BufferSize - sizeof(IP4_PACKET);

    ASSERT(4 == Ip4Packet->Version);

    if (5 != Ip4Packet->InternetHeaderLength)
    {
        LOG_WARNING("Unhandled IHL: %u\n", Ip4Packet->InternetHeaderLength);
        return;
    }
    ASSERT_INFO(dataLength <= remainingBufferLength,
                "Calculated remaining size: %u\nActual size from header: %u\n", 
                remainingBufferLength,
                dataLength
                );

    LOG("IP Packet\n"
        "Source: [%s]\n"
        "Destination: [%s]\n"
        "Length: %u bytes\n"
        "TTL: %u\n"
        "Protocol: 0x%x [%s]\n\n",
        NetUtilIp4AddressToText(Ip4Packet->Source, sourceAddress),
        NetUtilIp4AddressToText(Ip4Packet->Destination, destinationAddress),
        dataLength,
        Ip4Packet->TimeToLive,
        Ip4Packet->Protocol, _DumpProtocolTypeToString(Ip4Packet->Protocol)
        )
        ;
}