#include "HAL9000.h"
#include "test_dma.h"
#include "perf_framework.h"
#include "io.h"
#include "mmu.h"
#include "cpumu.h"

#define DMA_TEST_ITERATION_COUNT            10

typedef struct _RAW_TEST_CTX
{
    QWORD           BytesToRead;
    BOOLEAN         Asynchronous;
    PDEVICE_OBJECT  Device;
    PVOID           Buffer;
} RAW_TEST_CTX, *PRAW_TEST_CTX;

static FUNC_TestPerformance     _TestRawReadPerformance;

static const DWORD BYTES_TO_READ[] = { SECTOR_SIZE, PAGE_SIZE, 4 * PAGE_SIZE, 8 * PAGE_SIZE, 15 * PAGE_SIZE };
static const DWORD NO_OF_BYTES_VALUES = ARRAYSIZE(BYTES_TO_READ);
static const char* STAT_NAMES[2] = { "SYNCHRONOUS", "ASYNCHRONOUS" };

void
TestDmaPerformance(
    void
    )
{
    RAW_TEST_CTX ctx;
    PERFORMANCE_STATS perfStats[2];
    DWORD i;
    DWORD async;
    DWORD bytesToRead;
    PVOID pBuffer;
    PDEVICE_OBJECT pVolumeDevice;
    STATUS status;
    PDEVICE_OBJECT* pDeviceObjects;
    DWORD noOfDevices;

    memzero(&ctx, sizeof(RAW_TEST_CTX));
    bytesToRead = 0;
    pBuffer = NULL;
    pVolumeDevice = NULL;

    status = IoGetDevicesByType(DeviceTypeVolume,
                                &pDeviceObjects,
                                &noOfDevices
                                );
    ASSERT(SUCCEEDED(status));
    ASSERT( NULL != pDeviceObjects );
    ASSERT(noOfDevices > 0 );

    pVolumeDevice = pDeviceObjects[0];
    ctx.Device = pVolumeDevice;

    IoFreeTemporaryData(pDeviceObjects);
    pDeviceObjects = NULL;

    for (i = 0; i < NO_OF_BYTES_VALUES; ++i)
    {
        memzero(&perfStats, sizeof(perfStats));

        bytesToRead = BYTES_TO_READ[i];

        if (NULL != pBuffer)
        {
            ExFreePoolWithTag(pBuffer, HEAP_TEST_TAG);
            pBuffer = NULL;
        }

        pBuffer = ExAllocatePoolWithTag(PoolAllocateZeroMemory, bytesToRead, HEAP_TEST_TAG, 0 );
        ASSERT( NULL != pBuffer );

        MmuProbeMemory(pBuffer, bytesToRead );

        ctx.BytesToRead = bytesToRead;
        ctx.Buffer = pBuffer;
        for (async = 0; async < 2; ++async)
        {
            ctx.Asynchronous = (BOOLEAN) async;

            RunPerformanceFunction(_TestRawReadPerformance,
                                   &ctx,
                                   DMA_TEST_ITERATION_COUNT,
                                   FALSE,
                                   &perfStats[async]
                                   );
        }

        LOGL("Volume read with chunk size 0x%x bytes\n", bytesToRead);
        DisplayPerformanceStats(perfStats, 2, STAT_NAMES);
    }

    if (NULL != pBuffer)
    {
        ExFreePoolWithTag(pBuffer, HEAP_TEST_TAG);
        pBuffer = NULL;
    }
}


void
(__cdecl _TestRawReadPerformance)(
    IN_OPT  PVOID       Context
    )
{
    PRAW_TEST_CTX pCtx;
    QWORD bytesToRead;
    STATUS status;

    LOG_FUNC_START_CPU;

    ASSERT( NULL != Context );

    pCtx = (PRAW_TEST_CTX) Context;
    bytesToRead = pCtx->BytesToRead;

    status = IoReadDeviceEx(pCtx->Device,
                            pCtx->Buffer,
                            &bytesToRead,
                            0,
                            pCtx->Asynchronous
                            );
    ASSERT(SUCCEEDED(status));
    ASSERT( bytesToRead == pCtx->BytesToRead );

    LOG_FUNC_END_CPU;
}
