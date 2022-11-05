#include "HAL9000.h"
#include "dmp_nt.h"

void
DumpNtHeader(
    IN      PPE_NT_HEADER_INFO      NtHeader
    )
{
    ASSERT( NULL != NtHeader );

    LOG("Image Base: 0x%X\n", NtHeader->ImageBase);
    LOG("NT base: 0x%X\n", NtHeader->NtBase );
    LOG("Image Size: 0x%X\n", NtHeader->Size);
    LOG("Image machine: 0x%x\n", NtHeader->Machine);
    LOG("Image subsystem: 0x%x\n", NtHeader->Subsystem );
    LOG("Number of sections: %u\n", NtHeader->NumberOfSections );
}

void
DumpNtSection(
    IN      PPE_SECTION_INFO        SectionInfo
    )
{
    ASSERT( NULL != SectionInfo );
    
    LOG("Base address: 0x%X\n", SectionInfo->BaseAddress );
    LOG("Size: 0x%x\n", SectionInfo->Size );
    LOG("Characteristics: 0x%x\n", SectionInfo->Characteristics );
}