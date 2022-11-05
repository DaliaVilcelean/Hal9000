#pragma once

#define MdlAllocate(...)            MdlAllocateEx(__VA_ARGS__, __readcr3(), NULL)

typedef struct _PAGING_LOCK_DATA*   PPAGING_LOCK_DATA;

PTR_SUCCESS
PMDL
MdlAllocateEx(
    IN          PVOID               VirtualAddress,
    IN          DWORD               Length,
    IN_OPT      PHYSICAL_ADDRESS    Cr3,
    IN_OPT      PPAGING_LOCK_DATA   PagingData
    );

void
MdlFree(
    INOUT       PMDL            Mdl
    );

DWORD
MdlGetNumberOfPairs(
    IN          PMDL            Mdl
    );

PTR_SUCCESS
PMDL_TRANSLATION_PAIR
MdlGetTranslationPair(
    IN          PMDL            Mdl,
    IN          DWORD           Index
    );