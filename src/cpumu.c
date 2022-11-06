#include "HAL9000.h"
#include "thread_internal.h"
#include "cpumu.h"
#include "mmu.h"
#include "gdtmu.h"
#include "smp.h"
#include "vmm.h"
#include "gs_utils.h"
#include "syscall.h"

#define STACK_MINIMUM_SIZE          PAGE_SIZE
#define STACK_MAXIMUM_SIZE          (16*PAGE_SIZE)

#define TSS_STACK_DEFAULT_SIZE      (3*PAGE_SIZE)

#define IA32_EXPECTED_PAT_VALUES    0x0007'0406'0007'0406ULL

#define HAL9000_USED_XCR0_FEATURES           (XCR0_SAVED_STATE_x87_MMX | XCR0_SAVED_STATE_SSE)

typedef struct _CPUMU_DATA
{
    CPUID_BASIC_INFORMATION                         BasicInformation;
    CPUID_FEATURE_INFORMATION                       FeatureInformation;
    CPUID_MONITOR_LEAF                              MonitorLeaf;
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_LEAF    StructuredExtendedFeatures;
    CPUID_EXTENDED_CPUID_INFORMATION                ExtendedCpuidInformation;
    CPUID_EXTENDED_FEATURE_INFORMATION              ExtendedFeatureInformation;
    CPUID_EXTENDED_STATE_ENUMERATION_MAIN_LEAF      ExtendedStateMainLeaf;
} CPUMU_DATA, *PCPMU_DATA;

static CPUMU_DATA m_cpuMuData;

static
void
_CpuValidateCurrentCpu(
    void
    );

static
__forceinline
void
_CpuActivateAvailableFeatures(
    void
    )
{
    QWORD cr4FlagsToActivate;
    QWORD eferFlagsToActivate;

    cr4FlagsToActivate = 0;
    eferFlagsToActivate = 0;

    // CR4
    cr4FlagsToActivate |= ((m_cpuMuData.StructuredExtendedFeatures.ebx.SMEP) ? CR4_SMEP : 0);

    __writecr4(__readcr4() | cr4FlagsToActivate);

    LOGL("CR4 is 0x%X\n", __readcr4());

    // EFER
    eferFlagsToActivate |= ((m_cpuMuData.ExtendedFeatureInformation.edx.Syscall) ? IA32_EFER_SCE : 0);

    __writemsr(IA32_EFER, __readmsr(IA32_EFER) | eferFlagsToActivate);

    LOG("EFER is 0x%X\n", __readmsr(IA32_EFER));
}

__forceinline
static
void
_CpuMuCollectBasicInformation(
    void
    )
{
    __cpuid((int*) &m_cpuMuData.BasicInformation, CpuidIdxBasicInformation);

    if (m_cpuMuData.BasicInformation.MaxValueForBasicInfo >= CpuidIdxFeatureInformation)
    {
        __cpuid((int*)&m_cpuMuData.FeatureInformation, CpuidIdxFeatureInformation);
    }

    if (m_cpuMuData.BasicInformation.MaxValueForBasicInfo >= CpuidIdxMonitorLeaf)
    {
        __cpuid((int*)&m_cpuMuData.MonitorLeaf, CpuidIdxMonitorLeaf);
    }

    if (m_cpuMuData.BasicInformation.MaxValueForBasicInfo >= CpuidIdxStructuredExtendedFeaturesLeaf)
    {
        __cpuid((int*)&m_cpuMuData.StructuredExtendedFeatures, CpuidIdxStructuredExtendedFeaturesLeaf);
    }

    if (m_cpuMuData.BasicInformation.MaxValueForBasicInfo >= CpuidIdxExtendedStateEnumerationMainLeaf)
    {
        __cpuidex((int*)&m_cpuMuData.ExtendedStateMainLeaf, CpuidIdxExtendedStateEnumerationMainLeaf, 0x0);
    }
}

__forceinline
static
void
_CpuMuCollectExtendedInformation(
    void
    )
{
    __cpuid((int*) &m_cpuMuData.ExtendedCpuidInformation, CpuidIdxExtendedMaxFunction);

    if (m_cpuMuData.ExtendedCpuidInformation.MaxValueForExtendedInfo >= CpuidIdxExtendedFeatureInformation)
    {
        __cpuid((int*) &m_cpuMuData.ExtendedFeatureInformation, CpuidIdxExtendedFeatureInformation);
    }
}

// this has nothing to do with the thread system, it is a `hack` used to
// have the same consistency through GetCurrentThread() calls since
// `CpuMuPreInit` was called until the first thread is setup on the CPU in
// `ThreadSystemInitMainForCurrentCPU`
// This DUMMY_THREAD will be the thread of both the DUMMY_CPU and the real CPU until
// the threading system is set up

// PS: next year we might want to create a real `THREAD_HEADER` structure in `thread_internal.h` to have a cleaner solution
// However, as not to confuse people by updating the `THREAD` structure after the semester has started this is the current solution
typedef struct _DUMMY_THREAD
{
    REF_COUNT               RefCnt;

    struct _THREAD* Self;
} DUMMY_THREAD;
static_assert(sizeof(DUMMY_THREAD) == FIELD_OFFSET(THREAD, Id) && FIELD_OFFSET(DUMMY_THREAD, Self) == FIELD_OFFSET(THREAD, Self),
    "Safety measure, if someone modified the THREAD structure we may need to modify this DUMMY_THREAD as well");

// mark .Self as NULL such that GetCurrentThread will return always NULL until we setup the first real thread later in the boot
// set up both for dummy CPU and for the real CPU structure until threading system is initialized
static DUMMY_THREAD __dummySelfThread = { .Self = NULL };

void
CpuMuPreinit(
    void
    )
{
    static PCPU* __dummySelfCpu = NULL;

    memzero(&m_cpuMuData, sizeof(CPUMU_DATA));

    // Basic information
    _CpuMuCollectBasicInformation();

    // Extended information
    _CpuMuCollectExtendedInformation();

    // We cannot have the current CPU NULL because we always dereference the first field
    //  => use static dummy PCPU which has the first field NULL
    SetCurrentPcpu(&__dummySelfCpu);

    // we're not using the SetCurrentThread macro because it will
    // try to dereference the PCPU pointer
    __writemsr(IA32_FS_BASE_MSR, &__dummySelfThread);
}

void
CpuMuValidateConfiguration(
    void
    )
{
    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.APIC, "We cannot wake up APs without APIC!");

    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.MSR, "We cannot do anything without MSRs!");

    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.TSC, "We need a way to measure time!");

    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.PAT, "We need PAT!");

    ASSERT( IA32_EXPECTED_PAT_VALUES == __readmsr(IA32_PAT) );

    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.SSE, "We need SFENCE support!");

    ASSERT_INFO( m_cpuMuData.FeatureInformation.edx.SSE2, "We need LFENCE/MFENCE support");

#if INCLUDE_FP_SUPPORT
    ASSERT_INFO( m_cpuMuData.FeatureInformation.ecx.XSAVE, "We need XSAVE/XRSTOR support");

    ASSERT_INFO(IsBooleanFlagOn(
        DWORDS_TO_QWORD(m_cpuMuData.ExtendedStateMainLeaf.Xcr0FeatureSupportHigh, m_cpuMuData.ExtendedStateMainLeaf.Xcr0FeatureSupportLow),
        HAL9000_USED_XCR0_FEATURES),
        "We need to support 0x%X, however we only have support for 0x%X\n",
        HAL9000_USED_XCR0_FEATURES,
        DWORDS_TO_QWORD(m_cpuMuData.ExtendedStateMainLeaf.Xcr0FeatureSupportHigh, m_cpuMuData.ExtendedStateMainLeaf.Xcr0FeatureSupportLow));
#endif

    ASSERT_INFO( m_cpuMuData.ExtendedFeatureInformation.edx.Syscall, "We need SYSCALL/SYSRET support");
}

STATUS
CpuMuSetMonitorFilterSize(
    IN          WORD        FilterSize
    )
{
    if (!m_cpuMuData.FeatureInformation.ecx.MONITOR)
    {
        // no monitor available
        return STATUS_CPU_MONITOR_NOT_SUPPORTED;
    }

    if (FilterSize < m_cpuMuData.MonitorLeaf.eax.SmallestMonitorLineSize)
    {
        return STATUS_CPU_MONITOR_FILTER_SIZE_TOO_SMALL;
    }

    if (FilterSize > m_cpuMuData.MonitorLeaf.ebx.LargestMonitorLineSize)
    {
        return STATUS_CPU_MONITOR_FILTER_SIZE_TOO_LARGE;
    }

    __writemsr( IA32_MONITOR_FILTER_SIZE_MSR, FilterSize);

    return STATUS_SUCCESS;
}

STATUS
CpuMuAllocAndInitCpu(
    OUT_PTR     PPCPU*      PhysicalCpu,
    IN _Strict_type_match_
                APIC_ID     ApicId,
    IN          DWORD       StackSize,
    IN          BYTE        NoOfTssStacks
    )
{
    STATUS status;
    PPCPU pPcpu;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pPcpu = NULL;

    status = CpuMuAllocCpu(&pPcpu, ApicId, StackSize, NoOfTssStacks);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("CpuMuAllocCpu", status);
        return status;
    }

    status = CpuMuInitCpu(pPcpu, TRUE);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("CpuMuInitCpu", status );
        return status;
    }

    *PhysicalCpu = pPcpu;

    LOG_FUNC_END;

    return status;
}

STATUS
CpuMuAllocCpu(
    OUT_PTR     PPCPU*      PhysicalCpu,
    IN _Strict_type_match_
                APIC_ID     ApicId,
    IN          DWORD       StackSize,
    IN          BYTE        NumberOfTssStacks
    )
{
    STATUS status;
    PCPU* pPcpu;

    LOG_FUNC_START;

    ASSERT(NULL != PhysicalCpu);
    ASSERT(0 != StackSize);
    ASSERT( NumberOfTssStacks <= NO_OF_IST );

    status = STATUS_SUCCESS;
    pPcpu = NULL;

    pPcpu = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PCPU), HEAP_CPU_TAG, 0);
    if (NULL == pPcpu)
    {
        LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", sizeof(PCPU));
        return STATUS_MEMORY_CANNOT_BE_MAPPED;
    }

    pPcpu->Self = pPcpu;
    pPcpu->ApicId = ApicId;
    pPcpu->LogicalApicId = ( 1U << ApicId );

    LOG("APIC ID: 0x%02x, logical ID: 0x%02x\n", pPcpu->ApicId, pPcpu->LogicalApicId );

    pPcpu->StackTop = MmuAllocStack(StackSize, TRUE, FALSE, NULL);
    if (NULL == pPcpu->StackTop)
    {
        LOG_FUNC_ERROR_ALLOC("MmuAllocStack", StackSize);
        return STATUS_MEMORY_CANNOT_BE_COMMITED;
    }
    pPcpu->StackSize = StackSize;

    for (DWORD i = 0; i < NumberOfTssStacks; ++i)
    {
        pPcpu->TssStacks[i] = MmuAllocStack(TSS_STACK_DEFAULT_SIZE, TRUE, FALSE, NULL);
        if (NULL == pPcpu->TssStacks[i])
        {
            LOG_FUNC_ERROR_ALLOC("MmuAllocStack", TSS_STACK_DEFAULT_SIZE);
            return STATUS_MEMORY_CANNOT_BE_COMMITED;
        }
    }
    pPcpu->NumberOfTssStacks = NumberOfTssStacks;

    status = GdtMuInstallTssDescriptor(&pPcpu->Tss, NumberOfTssStacks, pPcpu->TssStacks, &pPcpu->TrSelector);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("GdtMuInstallTssDescriptor", status);
        return status;
    }

    InitializeListHead(&pPcpu->EventList);
    LockInit(&pPcpu->EventListLock);
    pPcpu->NoOfEventsInList = 0;

    *PhysicalCpu = pPcpu;

    LOG_FUNC_END;

    return status;
}

STATUS
CpuMuInitCpu(
    IN          PPCPU       PhysicalCpu,
    IN          BOOLEAN     ChangeStack
    )
{
    STATUS status;

    ASSERT( NULL != PhysicalCpu );
    _CpuValidateCurrentCpu();

    LOG_FUNC_START;

    status = STATUS_SUCCESS;

    // load task register
    __ltr(PhysicalCpu->TrSelector);

    // write CPU structure to GS
    SetCurrentPcpu(PhysicalCpu);
    SetCurrentThread((PTHREAD)&__dummySelfThread);

    // we assume we haven't used more than 1 PAGE of our stack
    if (ChangeStack)
    {
        LOGPL("Will change to stack: 0x%X\n", PhysicalCpu->StackTop );
        CpuMuChangeStack(PhysicalCpu->StackTop);
    }

    _CpuActivateAvailableFeatures();

    status = SmpCpuInit();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("SmpCpuInit", status);
        return status;
    }

    SyscallCpuInit();

    // create thread
    status = ThreadSystemInitMainForCurrentCPU();
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("ThreadSystemInitForCurrentCPU", status);
        return status;
    }

    // notify SMP module that the CPU has woken up
    SmpNotifyCpuWakeup();

    LOG_FUNC_END;

    return status;

}

void
CpuMuChangeStack(
    IN          PVOID       NewStack
    )
{
    PVOID oldStackBase = (PVOID)AlignAddressUpper(_AddressOfReturnAddress(), PAGE_SIZE);

    ASSERT( NULL != NewStack );

    // function needs to be written in assembly, else we cannot know how much
    // we need to copy for the local variables
    __changeStack(oldStackBase, NewStack);

    // if GS is enabled we need to update the cookies on the stack because the value placed
    // on the stack is the result __security_cookie ^ RSP at the moment the cookie is placed
    // because we changed our RSP when we will check the value on the stack
    // __security_cookie ^ OldRSP ^ NewRSP != __security_cookie => we will have a spurious
    // cookie corruption
    GSNotifyStackChange(oldStackBase, NewStack, PAGE_SIZE);
}

BOOLEAN
CpuMuIsPcidFeaturePresent(
    void
    )
{
    return (m_cpuMuData.FeatureInformation.ecx.PCID == 1);
}

STATUS
CpuMuActivateFpuFeatures(
    void
    )
{
    return HalSetActiveFpuFeatures(HAL9000_USED_XCR0_FEATURES);
}

static
void
_CpuValidateCurrentCpu(
    void
    )
{
    QWORD cr0;
    QWORD cr4;
    QWORD cr8;
    QWORD eferMsr;
    QWORD ia32PatValues;

    // read CRs
    cr0 = __readcr0();
    cr4 = __readcr4();
    cr8 = __readcr8();

    // read MSRs
    eferMsr = __readmsr(IA32_EFER);
    ia32PatValues = __readmsr(IA32_PAT);

    // Step 1. check control registers

    // check for protection and paging
    ASSERT(IsBooleanFlagOn(cr0, CR0_PG | CR0_PE));

    // check for write protect
    ASSERT(IsBooleanFlagOn(cr0, CR0_WP));

    // make sure caching is enabled
    ASSERT(!IsFlagOn(cr0, CR0_CD | CR0_NW));

    // how did we get into IA32-e without enabling PAE?
    ASSERT(IsBooleanFlagOn(cr4, CR4_PAE));

    // we are not using hardware priorities
    ASSERT(0 == cr8);

    // Step 2. check MSRs

    // make sure we're in 64 bit
    ASSERT(IsBooleanFlagOn(eferMsr, IA32_EFER_LME | IA32_EFER_LMA));

    // make sure we're using the NX feature
    ASSERT(IsBooleanFlagOn(eferMsr, IA32_EFER_NXE));

    ASSERT(IA32_EXPECTED_PAT_VALUES == ia32PatValues);
}