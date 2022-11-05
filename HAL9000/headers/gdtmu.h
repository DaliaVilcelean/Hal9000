#pragma once

typedef struct _TSS*        PTSS;

STATUS
GdtMuInit(
    void
    );

STATUS
GdtMuInstallTssDescriptor(
    OUT     PTSS            Tss,
    IN_RANGE(1,NO_OF_IST)
            BYTE            NumberOfStacks,
    IN_READS(NumberOfStacks)
            PVOID*          Stacks,
    OUT_OPT WORD*           Selector
    );

typedef enum _SEL_OP_MODE
{
    SelOpMode32,
    SelOpMode64,
    SelOpMode16,
    SelOpModeReserved = SelOpMode16 + 1
} SEL_OP_MODE;

typedef enum _SEL_PRIV
{
    SelPrivillegeSupervisor,
    SelPrivillegeUsermode,
    SelPrivillegeReserved = SelPrivillegeUsermode + 1
} SEL_PRIV;

typedef enum _SEL_TYPE
{
    SelTypeCode,
    SelTypeData,
    SelTypeReserved = SelTypeData + 1
} SEL_TYPE;

WORD
GdtMuRetrieveSelectorIndex(
    IN      SEL_PRIV        Privillege,
    IN      SEL_OP_MODE     Mode,
    IN      SEL_TYPE        Type
    );

void
GdtMuReload(
    IN          WORD            CodeSelector,
    IN          WORD            DataSelector,
    IN          BOOLEAN         PreserveGsBase,
    IN          BOOLEAN         PreserveFsBase
    );

#define GdtMuGetCS64Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode64, SelTypeCode)
#define GdtMuGetDS64Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode64, SelTypeData)
#define GdtMuGetCS32Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode32, SelTypeCode)
#define GdtMuGetDS32Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode32, SelTypeData)
#define GdtMuGetCS16Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode16, SelTypeCode)
#define GdtMuGetDS16Supervisor()        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode16, SelTypeData)
#define GdtMuGetCS64Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode64, SelTypeCode)
#define GdtMuGetDS64Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode64, SelTypeData)
#define GdtMuGetCS32Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode32, SelTypeCode)
#define GdtMuGetDS32Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode32, SelTypeData)
#define GdtMuGetCS16Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode16, SelTypeCode)
#define GdtMuGetDS16Usermode()          GdtMuRetrieveSelectorIndex(SelPrivillegeUsermode, SelOpMode16, SelTypeData)