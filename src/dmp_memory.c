#include "HAL9000.h"
#include "dmp_memory.h"
#include "dmp_common.h"
#include "strutils.h"

void
DumpMemory(
    IN      PVOID           LogicalAddress,
    IN      QWORD           Offset,
    IN      DWORD           Size,
    IN      BOOLEAN         DisplayAddress,
    IN      BOOLEAN         DisplayAscii
    )
{
    DWORD index;
    DWORD charPosition;
    PBYTE pCurAddress;
    char currentLine[3 * DUMP_LINE_WIDTH + 1];
    char currentAddress[LONG_ADDRESS_DIGITS + 1];
    char tempStr[3];
    char asciiLine[DUMP_LINE_WIDTH + 1];
    DWORD curValue;
    PVOID pointerToCurAddress;
    PBYTE pCurrentOffset;
    BOOLEAN bLogState;
    INTR_STATE oldState;

    ASSERT(NULL != LogicalAddress);
    ASSERT(0 != Size );

    bLogState = LogSetState(TRUE);

    pCurAddress = (PBYTE)LogicalAddress;
    charPosition = 0;
    pCurrentOffset = (PBYTE) Offset;

    oldState = DumpTakeLock();

    for (index = 0; index < Size; ++index)
    {
        curValue = pCurAddress[index];
        snprintf(tempStr, 3, "%02x", curValue);

        currentLine[3 * charPosition] = tempStr[0];
        currentLine[3 * charPosition + 1] = tempStr[1];
        currentLine[3 * charPosition + 2] = ' ';
        
        snprintf(tempStr, 3, "%c", curValue);
        asciiLine[charPosition] = isascii(tempStr[0]) ? tempStr[0] : ' ';

        charPosition++;

        if (0 == ((index + 1) % DUMP_LINE_WIDTH))
        {
            currentLine[3 * charPosition] = '\0';
            asciiLine[charPosition] = '\0';
            charPosition = 0;

            pointerToCurAddress = pCurrentOffset + (index / DUMP_LINE_WIDTH) * DUMP_LINE_WIDTH;
            snprintf(currentAddress, 17, "%012X", pointerToCurAddress);

            if (DisplayAddress)
            {
                LOG("%s| ", currentAddress);
            }
            LOG("%s", currentLine);
            if (DisplayAscii)
            {
                LOG("|%s", asciiLine);
            }
            LOG("\n");
        }
    }

    /// TODO: properly log last line (address + ascii representation)
    // log the last line
    if (0 != charPosition)
    {
        currentLine[3 * charPosition] = '\0';
        LOG("%s\n", currentLine);
    }

     LogSetState(bLogState);

     DumpReleaseLock(oldState);
}