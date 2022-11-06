#include "test_common.h"
#include "test_bitmap.h"
#include "test_pmm.h"
#include "test_vmm.h"
#include "test_file_io.h"
#include "test_dma.h"
#include "test_thread.h"
#include "smp.h"

#define TEST_HEAP_ALLOCATION_SIZE           0x100

void
TestRunAllFunctional(
    void
    )
{
    TestBitmap();
    TestPmmReserveAndReleaseFunctions();
    TestVmmAllocAndFreeFunctions();
    TestFileRead();
    TestAllThreadFunctionalities(SmpGetNumberOfActiveCpus() * 2 );
}

void
TestRunAllPerformance(
    void
    )
{
    TestFileReadPerformance();
    TestDmaPerformance();
}