#pragma once

#define DUMP_LINE_WIDTH         0x10

#define LONG_ADDRESS_DIGITS     16

void
DumpMemory(
    IN      PVOID           LogicalAddress,
    IN      QWORD           Offset,
    IN      DWORD           Size,
    IN      BOOLEAN         DisplayAddress,
    IN      BOOLEAN         DisplayAscii
    );