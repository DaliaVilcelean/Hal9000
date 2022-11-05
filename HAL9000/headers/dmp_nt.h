#pragma once

#include "pe_parser.h"

void
DumpNtHeader(
    IN      PPE_NT_HEADER_INFO      NtHeader
    );

void
DumpNtSection(
    IN      PPE_SECTION_INFO        SectionInfo
    );