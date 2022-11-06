#include "HAL9000.h"
#include "dmp_multiboot.h"

void
DumpMultiBootInformation(
    IN MULTIBOOT_INFORMATION* MultibootInformation
    )
{

    if (NULL == MultibootInformation)
    {
        return;
    }

    LOG("\n");
    LOG("-----------------------------\n");
    LOG("Multiboot structure at: 0x%X\n", MultibootInformation);
    LOG("Flags: 0b%b\n", MultibootInformation->Flags);
    LOG("Lower Memory Size: %D bytes\n", MultibootInformation->LowerMemorySize * KB_SIZE);
    LOG("Higher Memory SIze: %D bytes\n", MultibootInformation->HigherMemorySize * KB_SIZE);
    LOG("Boot device: 0x%x\n", MultibootInformation->BootDevice);
    // command line
    // module information
    LOG("Memory map address: 0x%X\n", MultibootInformation->MemoryMapAddress);
    LOG("Size of memory map: %d bytes\n", MultibootInformation->MemoryMapSize);

    if (IsBooleanFlagOn(MultibootInformation->Flags, MULTIBOOT_FLAG_LOADER_NAME_PRESENT))
    {
        // warning C4312: 'type cast': conversion from 'const DWORD' to 'char *' of greater size
#pragma warning(suppress:4312)
        LOG("Loader name: %s\n", (char*)MultibootInformation->BootLoaderName);
    }
    LOG("-----------------------------");
    LOG("\n");

    if (IsBooleanFlagOn(MultibootInformation->Flags, MULTIBOOT_FLAG_BOOT_MODULES_PRESENT))
    {
        LOG("%u boot modules are present!\n", MultibootInformation->ModuleCount);
        LOG("Boot modules are at 0x%X\n", MultibootInformation->ModuleAddress);

        for (DWORD i = 0; i < MultibootInformation->ModuleCount; ++i)
        {
            PMULTIBOOT_MODULE_INFORMATION pModuleInformation = &((PMULTIBOOT_MODULE_INFORMATION)(QWORD)MultibootInformation->ModuleAddress)[i];

            LOG("Module between 0x%X -> 0x%X\n",
                pModuleInformation->ModuleStartPhysAddr, pModuleInformation->ModuleEndPhysAddr);

            LOG("Module name is [%s]\n",
                (char*)(QWORD)pModuleInformation->StringPhysAddr);
        }
    }
}

void
DumpParameters(
    IN ASM_PARAMETERS* Parameters
    )
{
    if (NULL == Parameters)
    {
        return;
    }

    LOG("Kernel base address: 0x%X\n", Parameters->KernelBaseAddress);
    LOG("Kernel code size: 0x%X\n", Parameters->KernelSize);
    LOG("VA2PA offset: 0x%X\n", Parameters->VirtualToPhysicalOffset);

    LOG("Number of entries: %d\n", Parameters->MemoryMapEntries );
    LOG("PA of memory map: 0x%X\n", Parameters->MemoryMapAddress );

    DumpMultiBootInformation(Parameters->MultibootInformation);
}