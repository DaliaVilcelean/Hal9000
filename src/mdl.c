#include "HAL9000.h"
#include "io.h"
#include "mdl.h"
#include "mmu.h"

PTR_SUCCESS
PMDL
MdlAllocateEx(
    IN          PVOID               VirtualAddress,
    IN          DWORD               Length,
    IN_OPT      PHYSICAL_ADDRESS    Cr3,
    IN_OPT      PPAGING_LOCK_DATA   PagingData
    )
{
    STATUS status;
    DWORD offset;
    PBYTE pAlignedAddress;
    PMDL pMdl;
    PHYSICAL_ADDRESS pa;
    MDL_TRANSLATION_PAIR mdlCurPair;
    DWORD noOfPairs;
    DWORD mdlSize;
    DWORD indexInMdlArray;
    DWORD alignedSize;
    DWORD alignmentDifferences;
    BOOLEAN bKernelMemory;

    LOG_FUNC_START;

    ASSERT( NULL != VirtualAddress);
    ASSERT( 0 != Length );
    ASSERT((Cr3 != NULL) ^ (PagingData != NULL));

    LOG_TRACE_MMU("Will allocate MDL for VA: 0x%X of size %u\n", VirtualAddress, Length);

    status = STATUS_SUCCESS;
    pAlignedAddress = (PBYTE)AlignAddressLower(VirtualAddress, PAGE_SIZE);
    offset = 0;
    pMdl = NULL;
    pa = NULL;
    memzero(&mdlCurPair, sizeof(MDL_TRANSLATION_PAIR));
    noOfPairs = 0;
    mdlSize = 0;
    alignmentDifferences = AddressOffset(VirtualAddress, PAGE_SIZE );
    alignedSize = AlignAddressUpper(Length + alignmentDifferences, PAGE_SIZE);
    bKernelMemory = (PagingData == NULL || PagingData->Data.KernelSpace);

    LOG_TRACE_MMU("Aligned address: 0x%X\n", pAlignedAddress);
    LOG_TRACE_MMU("Aligned size: %u\n", alignedSize);

    if (bKernelMemory)
    {
        // We cannot probe memory user-mode memory (there is absolutely nothing we can do if it is not mapped)

        // we first need to 'probe' the VAs
        // before determining their physical address (they may not be mapped)
        MmuProbeMemory(VirtualAddress, Length);
    }

    mdlCurPair.Address = MmuGetPhysicalAddressEx(VirtualAddress, PagingData, Cr3);
    if (mdlCurPair.Address == NULL)
    {
        ASSERT_INFO(!bKernelMemory,
                    "Accesses to kernel memory cause that region to be probed, if we probed it, it's certainly mapped!");

        return NULL;
    }

    ASSERT(mdlCurPair.Address != NULL);

    mdlCurPair.NumberOfBytes = PAGE_SIZE - alignmentDifferences;
    noOfPairs = 1;

    for(offset = PAGE_SIZE;
        offset < alignedSize;
        offset = offset + PAGE_SIZE
        )
    {
        PBYTE pCurAddr = pAlignedAddress + offset;

        // if we're here => VA was mapped to PA
        pa = MmuGetPhysicalAddressEx(pCurAddr, PagingData, Cr3);
        ASSERT(NULL != pa);

        LOG_TRACE_MMU("VA 0x%X -> PA 0x%X\n", pCurAddr, pa);

        if ((PBYTE)mdlCurPair.Address + mdlCurPair.NumberOfBytes == pa)
        {
            mdlCurPair.NumberOfBytes = mdlCurPair.NumberOfBytes + PAGE_SIZE;
        }
        else
        {
            noOfPairs = noOfPairs + 1;

            mdlCurPair.Address = pa;
            mdlCurPair.NumberOfBytes = PAGE_SIZE;
        }
    }

    mdlSize = sizeof(MDL) + sizeof(MDL_TRANSLATION_PAIR) * noOfPairs;

    pMdl = ExAllocatePoolWithTag(PoolAllocateZeroMemory, mdlSize, HEAP_MDL_TAG, 0);
    if (NULL == pMdl)
    {
        LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", mdlSize);
        return NULL;
    }

    pMdl->ByteCount = Length;
    pMdl->StartVa = pAlignedAddress;
    pMdl->ByteOffset = alignmentDifferences;

    indexInMdlArray = 0;
    memzero(&mdlCurPair, sizeof(MDL_TRANSLATION_PAIR));

    mdlCurPair.Address = MmuGetPhysicalAddressEx(VirtualAddress, PagingData, Cr3);
    ASSERT(mdlCurPair.Address != NULL);
    mdlCurPair.NumberOfBytes = PAGE_SIZE - pMdl->ByteOffset;

    for(offset = PAGE_SIZE;
        offset < alignedSize;
        offset = offset + PAGE_SIZE
        )
    {
        PBYTE pCurAddr = pAlignedAddress + offset;

        // if we're here => VA was mapped to PA
        pa = MmuGetPhysicalAddressEx(pCurAddr, PagingData, Cr3);
        ASSERT(NULL != pa);

        LOG_TRACE_MMU("VA 0x%X -> PA 0x%X\n", pCurAddr, pa);

        if ((PBYTE)mdlCurPair.Address + mdlCurPair.NumberOfBytes == pa)
        {
            mdlCurPair.NumberOfBytes = mdlCurPair.NumberOfBytes + PAGE_SIZE;
        }
        else
        {
            pMdl->Translations[indexInMdlArray].Address = mdlCurPair.Address;
            pMdl->Translations[indexInMdlArray].NumberOfBytes = mdlCurPair.NumberOfBytes;

            mdlCurPair.Address = pa;
            mdlCurPair.NumberOfBytes = PAGE_SIZE;

            indexInMdlArray = indexInMdlArray + 1;
        }
    }

    pMdl->Translations[indexInMdlArray].Address = mdlCurPair.Address;
    pMdl->Translations[indexInMdlArray].NumberOfBytes = mdlCurPair.NumberOfBytes;
    indexInMdlArray = indexInMdlArray + 1;
    pMdl->NumberOfTranslationPairs = indexInMdlArray;

    ASSERT(indexInMdlArray == noOfPairs);

    LOG_FUNC_END;

    return pMdl;
}

void
MdlFree(
    INOUT       PMDL            Mdl
    )
{
    LOG_FUNC_START;

    ASSERT( NULL != Mdl );

    ExFreePoolWithTag(Mdl, HEAP_MDL_TAG );

    LOG_FUNC_END;
}

DWORD
MdlGetNumberOfPairs(
    IN          PMDL            Mdl
    )
{
    ASSERT( NULL != Mdl );

    return Mdl->NumberOfTranslationPairs;
}

PTR_SUCCESS
PMDL_TRANSLATION_PAIR
MdlGetTranslationPair(
    IN          PMDL            Mdl,
    IN          DWORD           Index
    )
{
    ASSERT( NULL != Mdl );

    return ( Index < Mdl->NumberOfTranslationPairs ) ? &Mdl->Translations[Index] : NULL;
}