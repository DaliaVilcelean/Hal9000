#include "HAL9000.h"
#include "io.h"
#include "dmp_mdl.h"

void 
DumpMdl(
    IN PMDL Mdl
    )
{
    DWORD i;

    if (NULL == Mdl)
    {
        return;
    }

    LOG("Will dump MDL found at 0x%X\n", Mdl );
    LOG("Start VA: 0x%X\n", Mdl->StartVa );
    LOG("Byte offset: 0x%x\n", Mdl->ByteOffset );
    LOG("Byte count: 0x%x\n", Mdl->ByteCount );
    LOG("Number of translation pairs: %u\n", Mdl->NumberOfTranslationPairs );

    for (i = 0; i < Mdl->NumberOfTranslationPairs; ++i)
    {
        LOG("PA: 0x%X, #of bytes: %u\n", 
            Mdl->Translations[i].Address, Mdl->Translations[i].NumberOfBytes
            );
    }
}
