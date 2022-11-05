#include "HAL9000.h"
#include "dmp_ata.h"
#include "dmp_common.h"

void
DumpAtaIdentifyCommand(
    IN      PATA_IDENTIFY_RESPONSE          Identify
    )
{
    QWORD totalSectors;
    WORD* wordArray;
    char serialNumber[ATA_SERIAL_NO_CHARS + 1];
    char modelNumber[ATA_MODEL_NO_CHARS + 1];
    DWORD i;
    INTR_STATE intrState;

    if (NULL == Identify)
    {
        return;
    }

    memzero(serialNumber, ATA_SERIAL_NO_CHARS + 1);
    memzero(modelNumber, ATA_MODEL_NO_CHARS + 1);

    totalSectors = Identify->Address48Bit;

    for (i = 0; i < ATA_SERIAL_NO_CHARS / 2; ++i)
    {
        serialNumber[2 * i] = Identify->SerialNumbers[2 * i + 1];
        serialNumber[2 * i + 1] = Identify->SerialNumbers[2 * i];
    }

    for (i = 0; i < ATA_MODEL_NO_CHARS / 2; ++i)
    {
        modelNumber[2 * i] = Identify->ModelNumber[2 * i + 1];
        modelNumber[2 * i + 1] = Identify->ModelNumber[2 * i];
    }

    intrState = DumpTakeLock();
    LOG("Serial number: %s\n", serialNumber);
    LOG("Model number: %s\n", modelNumber);
    LOG("totalSectors: %U\n", totalSectors);
    LOG("Current C/H/S: %u/%u/%u\n", Identify->LogicalCylindersCurrent, Identify->LogicalHeadsCurrent, Identify->LogicalSectorsCurrent);
    LOG("Default C/H/S: %u/%u/%u\n", Identify->LogicalCylinders, Identify->LogicalHeads, Identify->LogicalSectors);
    LOG("Total capacity: %U MB\n", (totalSectors * SECTOR_SIZE) >> 20);
    LOG("28 addressable: %U GB\n", ((QWORD)Identify->Address28Bit * SECTOR_SIZE) >> 30);
    LOG("48 addressable: %U GB\n", ((QWORD)Identify->Address48Bit * SECTOR_SIZE) >> 30);

    wordArray = (WORD*)Identify;
    LOG("Maximum sectors per interrupt: %d\n", wordArray[47] & MAX_BYTE);
    LOG("Current setting for sectors per interrupt: %d\n", wordArray[59] & MAX_BYTE);

    LOG("Support for DMA mode 2: %d\n", IsBooleanFlagOn(wordArray[63], (1 << 2)));
    LOG("Selected DMA mode: 2: %d\n", IsBooleanFlagOn(wordArray[64], (1<<10)));

    LOG("PIO mode support: 0b%b\n", WORD_LOW(wordArray[64]));

    LOG("Minimum cycle time PIO transfer: %d ns\n", wordArray[68]);

    LOG("ATA-8 compliant: %d\n", IsBooleanFlagOn(wordArray[80], (1 << 8)));
    LOG("ATA-7 compliant: %d\n", IsBooleanFlagOn(wordArray[80], (1 << 7)));
    LOG("ATA-6 compliant: %d\n", IsBooleanFlagOn(wordArray[80], (1 << 6)));
    LOG("ATA-5 compliant: %d\n", IsBooleanFlagOn(wordArray[80], (1 << 5)));
    LOG("ATA-4 compliant: %d\n", IsBooleanFlagOn(wordArray[80], (1 << 4)));

    LOG("48-bit LBA support: %d\n", Identify->Features.SupportLba48);

    LOG("Ultra-DMA 6 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 6)));
    LOG("Ultra-DMA 5 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 5)));
    LOG("Ultra-DMA 4 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 4)));
    LOG("Ultra-DMA 3 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 3)));
    LOG("Ultra-DMA 2 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 2)));
    LOG("Ultra-DMA 1 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 1)));
    LOG("Ultra-DMA 0 support: %d\n", IsBooleanFlagOn(wordArray[88], (1 << 0)));

    LOG("Ultra-DMA 6 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (6 + 8))));
    LOG("Ultra-DMA 5 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (5 + 8))));
    LOG("Ultra-DMA 4 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (4 + 8))));
    LOG("Ultra-DMA 3 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (3 + 8))));
    LOG("Ultra-DMA 2 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (2 + 8))));
    LOG("Ultra-DMA 1 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (1 + 8))));
    LOG("Ultra-DMA 0 selected: %d\n", IsBooleanFlagOn(wordArray[88], (1 << (0 + 8))));

    LOG("Packet support: %d\n", Identify->Features.PacketFeature);
    DumpReleaseLock(intrState);
}