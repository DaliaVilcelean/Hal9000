#include "HAL9000.h"
#include "dmp_int15.h"

void
DumpInt15Entry(
    IN  PINT15_MEMORY_MAP_ENTRY     MemoryMapEntry
    )
{
    if (NULL == MemoryMapEntry)
    {
        return;
    }

    LOG("BaseAddress: 0x%X\n", MemoryMapEntry->BaseAddress);
    LOG("Size: 0x%X\n", MemoryMapEntry->Length);
    LOG("Type: %d\n", MemoryMapEntry->Type);
    LOG("Extended attributed: 0x%x\n", MemoryMapEntry->ExtendedAttributes);
}