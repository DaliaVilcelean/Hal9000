#include "HAL9000.h"
#include "test_file_io.h"
#include "io.h"
#include "perf_framework.h"
#include "mmu.h"
#include "rtc.h"

static const char FILES_TO_READ[][MAX_PATH] = { "C:\\WINLOA~1.RAR",
                                                "D:\\cacheset.exe",
                                                "D:\\procexp.exe"
                                                };
static const DWORD NO_OF_FILES = ARRAYSIZE(FILES_TO_READ);

static const DWORD READ_CHUNK_SIZES[] = { PAGE_SIZE, 4 * PAGE_SIZE, 8 * PAGE_SIZE, 32 * PAGE_SIZE, 63 * PAGE_SIZE };
static const DWORD NO_OF_CHUNK_SIZES = ARRAYSIZE(READ_CHUNK_SIZES);
static const char* STAT_NAMES[2] = { "SYNCHRONOUS", "ASYNCHRONOUS" };

#define FILE_TEST_PERFORMANCE_NO_OF_ITERATIONS          5

static
STATUS
_TestSingleFileRead(
    IN      PFILE_OBJECT        File,
    IN      QWORD               FileSize,
    IN      PVOID               Buffer,
    IN      DWORD               BufferSize
    );

static
STATUS
_TestFileRead(
    IN_Z    char*               Filename,
    IN      PVOID               Buffer,
    IN      DWORD               BufferSize,
    IN      BOOLEAN             Asynchronous,
    IN      DWORD               Iterations,
    OUT     PPERFORMANCE_STATS  PerfStats
    );

BOOLEAN
TestFileRead(
    void
    )
{
    PFILE_OBJECT syncFile;
    PFILE_OBJECT asyncFile;
    STATUS status;
    STATUS statusSup;
    DWORD i;
    DWORD j;
    PVOID pSyncBuffer;
    PVOID pAsyncBuffer;
    QWORD fileOffset;
    QWORD bytesRemaining;
    FILE_INFORMATION fileInformation;

    syncFile = asyncFile = NULL;
    pSyncBuffer = pAsyncBuffer = NULL;
    status = STATUS_SUCCESS;
    statusSup = STATUS_SUCCESS;
    memzero(&fileInformation, sizeof(FILE_INFORMATION));

    __try
    {
        for (i = 0; i < NO_OF_FILES; ++i)
        {
            if (NULL != syncFile)
            {
                status = IoCloseFile(syncFile);
                if (!SUCCEEDED(status))
                {
                    LOG_FUNC_ERROR("IoCloseFile", status);
                    __leave;
                }

                syncFile = NULL;
            }

            if (NULL != asyncFile)
            {
                status = IoCloseFile(asyncFile);
                if (!SUCCEEDED(status))
                {
                    LOG_FUNC_ERROR("IoCloseFile", status);
                    __leave;
                }

                asyncFile = NULL;
            }

            status = IoCreateFile(&syncFile,
                                  FILES_TO_READ[i],
                                  FALSE,
                                  FALSE,
                                  FALSE
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("IoCreateFile", status);
                continue;
            }

            status = IoCreateFile(&asyncFile,
                                  FILES_TO_READ[i],
                                  FALSE,
                                  FALSE,
                                  TRUE
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("IoCreateFile", status);
                continue;
            }

            status = IoQueryInformationFile(syncFile,
                                            &fileInformation
            );
            if (!SUCCEEDED(status))
            {
                LOG_FUNC_ERROR("IoQueryInformationFile", status);
                __leave;
            }

            fileOffset = 0;
            bytesRemaining = fileInformation.FileSize;

            for (j = 0; j < NO_OF_CHUNK_SIZES; ++j)
            {
                DWORD allocationSize = READ_CHUNK_SIZES[j];

                LOGL("Running on file [%s] with chunk size 0x%x bytes\n", FILES_TO_READ[i], allocationSize);

                if (NULL != pSyncBuffer)
                {
                    ExFreePoolWithTag(pSyncBuffer, HEAP_TEST_TAG);
                    pSyncBuffer = NULL;
                }

                if (NULL != pAsyncBuffer)
                {
                    ExFreePoolWithTag(pAsyncBuffer, HEAP_TEST_TAG);
                    pAsyncBuffer = NULL;
                }

                pSyncBuffer = ExAllocatePoolWithTag(PoolAllocateZeroMemory, allocationSize, HEAP_TEST_TAG, 0);
                if (NULL == pSyncBuffer)
                {
                    LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", allocationSize);
                    status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
                    __leave;
                }

                pAsyncBuffer = ExAllocatePoolWithTag(PoolAllocateZeroMemory, allocationSize, HEAP_TEST_TAG, 0);
                if (NULL == pAsyncBuffer)
                {
                    LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", allocationSize);
                    status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
                    __leave;
                }

                while (0 != bytesRemaining)
                {
                    QWORD bytesRead;
                    QWORD bytesToRead = (DWORD)min(bytesRemaining, allocationSize);

                    LogSetState(FALSE);

                    status = IoReadFile(syncFile,
                                        bytesToRead,
                                        &fileOffset,
                                        pSyncBuffer,
                                        &bytesRead
                    );
                    if (!SUCCEEDED(status))
                    {
                        LOG_FUNC_ERROR("IoReadFile", status);
                        __leave;
                    }
                    ASSERT(bytesToRead == bytesRead);

                    status = IoReadFile(asyncFile,
                                        bytesToRead,
                                        &fileOffset,
                                        pAsyncBuffer,
                                        &bytesRead
                    );
                    if (!SUCCEEDED(status))
                    {
                        LOG_FUNC_ERROR("IoReadFile", status);
                        __leave;
                    }
                    ASSERT(bytesToRead == bytesRead);

                    LogSetState(TRUE);

                    ASSERT(bytesRead <= MAX_DWORD);
                    if (0 != memcmp(pAsyncBuffer, pSyncBuffer, (DWORD)bytesRead))
                    {
                        LOG_ERROR("Async buffers differs from sync buffer at file offset 0x%X\n", fileOffset);
                    }

                    bytesRemaining = bytesRemaining - bytesRead;
                    fileOffset = fileOffset + bytesRead;
                }
            }

        }
    }
    __finally
    {
        if (NULL != syncFile)
        {
            statusSup = IoCloseFile(syncFile);
            if (!SUCCEEDED(statusSup))
            {
                LOG_FUNC_ERROR("IoCloseFile", statusSup);
            }

            syncFile = NULL;
        }

        if (NULL != asyncFile)
        {
            statusSup = IoCloseFile(asyncFile);
            if (!SUCCEEDED(statusSup))
            {
                LOG_FUNC_ERROR("IoCloseFile", statusSup);
            }

            asyncFile = NULL;
        }

        if (NULL != pSyncBuffer)
        {
            ExFreePoolWithTag(pSyncBuffer, HEAP_TEST_TAG);
            pSyncBuffer = NULL;
        }

        if (NULL != pAsyncBuffer)
        {
            ExFreePoolWithTag(pAsyncBuffer, HEAP_TEST_TAG);
            pAsyncBuffer = NULL;
        }
    }

    return SUCCEEDED(status);
}

void
TestFileReadPerformance(
    void
    )
{
    STATUS status;
    DWORD i;
    DWORD j;
    BOOLEAN async;
    PVOID pBuffer;
    PERFORMANCE_STATS perfStats[2];

    pBuffer = NULL;
    status = STATUS_SUCCESS;

    __try
    {
        for (i = 0; i < NO_OF_FILES; ++i)
        {
            for (j = 0; j < NO_OF_CHUNK_SIZES; ++j)
            {
                DWORD allocationSize = READ_CHUNK_SIZES[j];

                LOGL("Running on file [%s] with chunk size 0x%x bytes\n", FILES_TO_READ[i], allocationSize);

                if (NULL != pBuffer)
                {
                    ExFreePoolWithTag(pBuffer, HEAP_TEST_TAG);
                    pBuffer = NULL;
                }

                pBuffer = ExAllocatePoolWithTag(PoolAllocateZeroMemory, allocationSize, HEAP_TEST_TAG, 0);
                if (NULL == pBuffer)
                {
                    LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", allocationSize);
                    status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
                    __leave;
                }

                // this is done so we won't have page faults
                // when reading the buffers
                MmuProbeMemory(pBuffer, allocationSize);

                memzero(&perfStats, sizeof(perfStats));
                LogSetState(FALSE);
                for (async = 0; async < 2; ++async)
                {
                    status = _TestFileRead(FILES_TO_READ[i],
                                           pBuffer,
                                           allocationSize,
                                           async,
                                           FILE_TEST_PERFORMANCE_NO_OF_ITERATIONS,
                                           &perfStats[async]
                                           );
                    if (!SUCCEEDED(status))
                    {
                        LogSetState(TRUE);
                        LOG_FUNC_ERROR("_TestFileRead", status);
                        continue;
                    }
                }
                LogSetState(TRUE);

                LOGL("File [%s] with chunk size 0x%x bytes\n", FILES_TO_READ[i], allocationSize);
                DisplayPerformanceStats(perfStats, 2, STAT_NAMES);
            }

        }
    }
    __finally
    {
        if (NULL != pBuffer)
        {
            ExFreePoolWithTag(pBuffer, HEAP_TEST_TAG);
            pBuffer = NULL;
        }
    }
}

static
STATUS
_TestSingleFileRead(
    IN      PFILE_OBJECT        File,
    IN      QWORD               FileSize,
    IN      PVOID               Buffer,
    IN      DWORD               BufferSize
    )
{
    STATUS status;
    QWORD bytesRemaining;
    QWORD fileOffset;

    ASSERT( NULL != File );
    ASSERT( 0 != FileSize );
    ASSERT( NULL != Buffer );
    ASSERT( 0 != BufferSize );

    bytesRemaining = FileSize;
    fileOffset = 0;

    while (0 != bytesRemaining)
    {
        QWORD bytesRead;
        QWORD bytesToRead = min(bytesRemaining, BufferSize);

        status = IoReadFile(File,
                            bytesToRead,
                            &fileOffset,
                            Buffer,
                            &bytesRead
                            );
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoReadFile", status);
            return status;
        }
        ASSERT(bytesToRead == bytesRead);

        bytesRemaining = bytesRemaining - bytesRead;
        fileOffset = fileOffset + bytesRead;
    }

    return STATUS_SUCCESS;
}

static
STATUS
_TestFileRead(
    IN_Z    char*               Filename,
    IN      PVOID               Buffer,
    IN      DWORD               BufferSize,
    IN      BOOLEAN             Asynchronous,
    IN      DWORD               Iterations,
    OUT     PPERFORMANCE_STATS  PerfStats
    )
{
    STATUS status;
    STATUS statusSup;
    PFILE_OBJECT pFile;
    DWORD i;
    FILE_INFORMATION fileInfo;
    QWORD* allTimes;
    QWORD startTime;
    QWORD endTime;

    ASSERT( NULL != Filename );
    ASSERT( NULL != Buffer );
    ASSERT( 0 != BufferSize );
    ASSERT( NULL != PerfStats );

    status = STATUS_SUCCESS;
    statusSup = STATUS_SUCCESS;
    pFile = NULL;
    memzero(&fileInfo, sizeof(FILE_INFORMATION));
    memzero( PerfStats, sizeof(PERFORMANCE_STATS));
    allTimes = NULL;

    __try
    {
        status = IoCreateFile(&pFile,
                              Filename,
                              FALSE,
                              FALSE,
                              Asynchronous
        );
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoCreateFile", status);
            __leave;
        }

        status = IoQueryInformationFile(pFile, &fileInfo);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoQueryInformationFile", status);
            __leave;
        }

        allTimes = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(QWORD) * Iterations, HEAP_TEST_TAG, 0);
        if (NULL == allTimes)
        {
            status = STATUS_HEAP_INSUFFICIENT_RESOURCES;
            LOG_FUNC_ERROR_ALLOC("HeapAllocatePoolWithTag", status);
            __leave;
        }

        for (i = 0; i < Iterations; ++i)
        {
            startTime = RtcGetTickCount();
            status = _TestSingleFileRead(pFile,
                                         fileInfo.FileSize,
                                         Buffer,
                                         BufferSize
            );
            endTime = RtcGetTickCount();
            LOGL("Finished iteration %d\n", i);

            allTimes[i] = endTime - startTime;
        }
    }
    __finally
    {
        if (SUCCEEDED(status))
        {
            QWORD totalTime;
            QWORD minTime;
            QWORD maxTime;

            ASSERT(NULL != allTimes);

            totalTime = minTime = maxTime = allTimes[0];

            for (i = 1; i < Iterations; ++i)
            {
                if (allTimes[i] < minTime)
                {
                    minTime = allTimes[i];
                }
                else if (allTimes[i] > maxTime)
                {
                    maxTime = allTimes[i];
                }

                if (MAX_QWORD - allTimes[i] < totalTime)
                {
                    // we will overflow
                    NOT_REACHED;
                }

                totalTime = totalTime + allTimes[i];
            }

            PerfStats->Min = minTime;
            PerfStats->Max = maxTime;
            PerfStats->Mean = totalTime / Iterations;
        }

        if (NULL != pFile)
        {
            statusSup = IoCloseFile(pFile);
            if (!SUCCEEDED(statusSup))
            {
                LOG_FUNC_ERROR("IoCloseFile", statusSup);
            }
        }

        if (NULL != allTimes)
        {
            ExFreePoolWithTag(allTimes, HEAP_TEST_TAG);
            allTimes = NULL;
        }
    }

    return status;
}