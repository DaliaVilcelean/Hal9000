#pragma once

#pragma pack(push,1)

#define MULTIBOOT_FLAG_BOOT_MODULES_PRESENT         (1<<3)
#define MULTIBOOT_FLAG_LOADER_NAME_PRESENT          (1<<9)

/*
+-------------------+
0       | flags             |    (required)
+-------------------+
4       | mem_lower         |    (present if flags[0] is set)
8       | mem_upper         |    (present if flags[0] is set)
+-------------------+
12      | boot_device       |    (present if flags[1] is set)
+-------------------+
16      | cmdline           |    (present if flags[2] is set)
+-------------------+
20      | mods_count        |    (present if flags[3] is set)
24      | mods_addr         |    (present if flags[3] is set)
+-------------------+
28 - 40 | syms              |    (present if flags[4] or
|                   |                flags[5] is set)
+-------------------+
44      | mmap_length       |    (present if flags[6] is set)
48      | mmap_addr         |    (present if flags[6] is set)
+-------------------+
52      | drives_length     |    (present if flags[7] is set)
56      | drives_addr       |    (present if flags[7] is set)
+-------------------+
60      | config_table      |    (present if flags[8] is set)
+-------------------+
64      | boot_loader_name  |    (present if flags[9] is set)
+-------------------+
68      | apm_table         |    (present if flags[10] is set)
+-------------------+
72      | vbe_control_info  |    (present if flags[11] is set)
76      | vbe_mode_info     |
80      | vbe_mode          |
82      | vbe_interface_seg |
84      | vbe_interface_off |
86      | vbe_interface_len |
+-------------------+
*/
typedef struct _MULTIBOOT_MODULE_INFORMATION
{
    DWORD       ModuleStartPhysAddr;
    DWORD       ModuleEndPhysAddr;

    DWORD       StringPhysAddr;
    DWORD       __Reserved;
} MULTIBOOT_MODULE_INFORMATION, *PMULTIBOOT_MODULE_INFORMATION;

typedef struct _MULTIBOOT_INFORMATION
{
    // indicates the presence of the other fields
    DWORD       Flags;                          // 0x0

                                                // number of KB's of lower/higher memory size

                                                // starts at address 0
    DWORD       LowerMemorySize;                // 0x4

                                                // starts at address 1MB
    DWORD       HigherMemorySize;               // 0x8


                                                /// should be modified to a struct in the future

                                                // information about the boot device
    DWORD       BootDevice;                     // 0xC

                                                // C-style zero-terminated string
    DWORD       CommandLine;                    // 0x10

                                                // number of modules loaded
    DWORD       ModuleCount;                    // 0x14

                                                // pointer to array of module structure
    DWORD       ModuleAddress;                  // 0x18

                                                /// currently not interested in this field
    BYTE        Reserved[16];                   // 0x1C

                                                // Memory map address
    DWORD       MemoryMapSize;                  // 0x30

                                                // Memory map - size of buffer
    DWORD       MemoryMapAddress;               // 0x3C

                                                // Drive Structures start address
    DWORD       DriveStructuresAddress;         // 0x34

                                                // Drive Structures size
    DWORD       DriveStructuresSize;            // 0x38

                                                // address of ROM configuration table
    DWORD       ConfigTableAddress;             // 0x3C

                                                // pointer to C-style zero terminated string
    DWORD       BootLoaderName;                 // 0x40

                                                // pointer to APM table
    DWORD       ApmTable;                       // 0x44

                                                // we don't care about no graphics
    BYTE        GraphicsData[20];               // 0x48
} MULTIBOOT_INFORMATION, *PMULTIBOOT_INFORMATION;

// it is not physically possible to have more than 4
// in the BDA
#define     BIOS_MAX_NO_OF_SERIAL_PORTS     4

// parameter structure
typedef struct _ASM_PARAMETERS
{
    MULTIBOOT_INFORMATION*      MultibootInformation;
    PVOID                       KernelBaseAddress;
    QWORD                       KernelSize;
    QWORD                       VirtualToPhysicalOffset;

    PHYSICAL_ADDRESS            MemoryMapAddress;
    DWORD                       MemoryMapEntries;

    WORD                        BiosSerialPorts[BIOS_MAX_NO_OF_SERIAL_PORTS];
} ASM_PARAMETERS, *PASM_PARAMETERS;
#pragma pack(pop)