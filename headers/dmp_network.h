#pragma once

#include "network.h"

void
DumpEthernetFrame(
    IN      PETHERNET_FRAME     Frame,
    IN      DWORD               BufferSize
    );

void
DumpArpPacket(
    IN      PARP_PACKET         ArpPacket,
    IN      DWORD               BufferSize
    );

void
DumpIp4Packet(
    IN      PIP4_PACKET         Ip4Packet,
    IN      DWORD               BufferSize
    );