#include "HAL9000.h"
#include "tss.h"
#include "gdtmu.h"
#include "gdt.h"

static volatile WORD m_selectorIndex = 0;

#define FIRST_SELECTOR_INDEX                    0x8

static
__forceinline
WORD
_GdtMuRetrieveNextSelectorIndex(
    IN      BOOLEAN         SystemDescriptor
    )
{
    return _InterlockedExchangeAdd16(&m_selectorIndex,
                                     SystemDescriptor ? sizeof(TSS_DESCRIPTOR) : sizeof(SEGMENT_DESCRIPTOR));
}

STATUS
GdtMuInit(
    void
)
{
    STATUS status;
    SEGMENT_DESCRIPTOR_FLAGS flags;
    WORD selIdx;

    status = STATUS_SUCCESS;

    // NULL descriptor is guaranteed by HAL no need to place it

    for (SEL_PRIV selPriv = SelPrivillegeSupervisor; selPriv < SelPrivillegeReserved; ++selPriv)
    {
        for (SEL_OP_MODE opMode = SelOpMode32; opMode < SelOpModeReserved; ++opMode)
        {
            for (SEL_TYPE selType = SelTypeCode; selType < SelTypeReserved; ++selType)
            {
                flags = ((selPriv == SelPrivillegeSupervisor) ? (1 << SegmentDescriptorFlagRing0) : 0) |
                    ((opMode == SelOpMode64 && selType == SelTypeCode) ? (1 << SegmentDescriptorLongMode) :
                    ((opMode == SelOpMode16) ? 0 : (1 << SegmentDescriptor32bitOperation)));

                selIdx = GdtMuRetrieveSelectorIndex(selPriv, opMode, selType);
                status = GdtInstallDescriptor(selIdx,
                                              0,
                                              (selType == SelTypeData) ? NonSystemSegmentDataReadWriteAccessed : NonSystemSegmentCodeExecuteOnlyAccessed,
                                              flags);
            }
        }
    }

    GdtMuReload(
        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode64, SelTypeCode),
        GdtMuRetrieveSelectorIndex(SelPrivillegeSupervisor, SelOpMode64, SelTypeData),
        TRUE,
        TRUE);

    m_selectorIndex = selIdx + sizeof(SEGMENT_DESCRIPTOR);

    return status;
}

STATUS
GdtMuInstallTssDescriptor(
    OUT     PTSS            Tss,
    IN_RANGE(1,NO_OF_IST)
            BYTE            NumberOfStacks,
    IN_READS(NumberOfStacks)
            PVOID*          Stacks,
    OUT_OPT WORD*           Selector
    )
{
    STATUS status;
    WORD selector;
    
    selector = _GdtMuRetrieveNextSelectorIndex(TRUE);

    status = TssInstall(Tss,
                        selector,
                        NumberOfStacks,
                        Stacks);
    if(!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("TssInstall", status );
        return status;
    }

    if (NULL != Selector)
    {
        *Selector = selector;
    }

    return status;
}

WORD
GdtMuRetrieveSelectorIndex(
    IN      SEL_PRIV        Privillege,
    IN      SEL_OP_MODE     Mode,
    IN      SEL_TYPE        Type
    )
{
    ASSERT(Privillege < SelPrivillegeReserved);
    ASSERT(Mode < SelOpModeReserved);
    ASSERT(Type < SelTypeReserved);

    return (WORD) (
            FIRST_SELECTOR_INDEX +
                (Privillege * SelOpModeReserved * SelTypeReserved + 
                Mode * SelTypeReserved +
                Type
                ) * sizeof(SEGMENT_DESCRIPTOR));
}

void
GdtMuReload(
    IN          WORD            CodeSelector,
    IN          WORD            DataSelector,
    IN          BOOLEAN         PreserveGsBase,
    IN          BOOLEAN         PreserveFsBase
    )
{
    // The initialization is done just because the compiler is retarded :)
    // It thinks we might write the oldGs/FsBase values without reading them
    QWORD oldGsBase = 0;
    QWORD oldFsBase = 0;

    if (PreserveGsBase) oldGsBase = __readmsr(IA32_GS_BASE_MSR);
    if (PreserveFsBase) oldFsBase = __readmsr(IA32_FS_BASE_MSR);

    GdtReload(CodeSelector, DataSelector);

    if (PreserveGsBase) __writemsr(IA32_GS_BASE_MSR, oldGsBase);
    if (PreserveFsBase) __writemsr(IA32_FS_BASE_MSR, oldFsBase);
}
