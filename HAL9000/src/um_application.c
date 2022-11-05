#include "HAL9000.h"
#include "um_application.h"
#include "io.h"
#include "pe_parser.h"
#include "vmm.h"
#include "thread_internal.h"
#include "process_internal.h"

static
STATUS
_UmApplicationReadExecutableContents(
    IN_Z        char*       FullPath,
    _Outptr_result_buffer_(*BufferSize)
                PVOID*      Buffer,
    OUT         QWORD*      BufferSize
    );

static
void
_UmApplicationDiscardKernelExecutableMapping(
    IN          PVOID       ApplicationBuffer
    );

STATUS
UmApplicationRetrieveHeader(
    IN_Z        char*                   Path,
    OUT         PPE_NT_HEADER_INFO      NtHeaderInfo
    )
{
    STATUS status;
    QWORD peSize;
    PVOID pBuffer;

    if (Path == NULL)
    {
        return STATUS_INVALID_PARAMETER1;
    }

    if (NtHeaderInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER2;
    }

    LOG_FUNC_START;

    pBuffer = NULL;
    status = STATUS_SUCCESS;
    peSize = 0;
    memzero(NtHeaderInfo, sizeof(PE_NT_HEADER_INFO));

    __try
    {
        LOG_TRACE_USERMODE("Will open executable found at [%s]\n", Path);

        status = _UmApplicationReadExecutableContents(Path,
                                                      &pBuffer,
                                                      &peSize);
        if (!SUCCEEDED(status))
        {
            LOG_TRACE_USERMODE("[ERROR]_UmApplicationReadExecutableContents failed with status 0x%x", status);
            __leave;
        }

        LOG_TRACE_USERMODE("Will parse NT header!\n");

        ASSERT(peSize <= MAX_DWORD);
        status = PeRetrieveNtHeader(pBuffer,
                                    (DWORD)peSize,
                                    NtHeaderInfo);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("PeRetrieveNtHeader", status);
            __leave;
        }

        LOG_TRACE_USERMODE("Successfully parsed NT header!\n");
    }
    __finally
    {
        if (!SUCCEEDED(status))
        {
            if (pBuffer != NULL)
            {
                _UmApplicationDiscardKernelExecutableMapping(pBuffer);
                pBuffer = NULL;
            }
        }
    }

    LOG_FUNC_END;

    return status;
}

STATUS
UmApplicationRun(
    IN          PPROCESS                Process,
    IN          BOOLEAN                 WaitForExecution,
    OUT_OPT     STATUS*                 CompletionStatus
    )
{
    STATUS status;
    PTHREAD pThread;

    UNREFERENCED_PARAMETER(WaitForExecution);
    UNREFERENCED_PARAMETER(CompletionStatus);

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    pThread = NULL;

    __try
    {
        // Loads the kernel image at the preferred image dictated by the NT header
        status = MmuLoadPe(Process->HeaderInfo,
                           Process->PagingData);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("MmuLoadPe", status);
            __leave;
        }

        LOG_TRACE_USERMODE("Successfully loaded PE file!\n");

        // After we loaded the PE into the appropriate process there is no need to keep the kernel mapping
        _UmApplicationDiscardKernelExecutableMapping(Process->HeaderInfo->ImageBase);

        LOG_TRACE_USERMODE("Will create thread with entry point at 0x%X\n", Process->HeaderInfo->Preferred.AddressOfEntryPoint);

        status = ThreadCreateEx("Test",
                                ThreadPriorityDefault,
                                //  warning C4055: 'type cast': from data pointer 'PVOID' to function pointer 'PFUNC_ThreadStart'
#pragma warning(suppress:4055)
                                (PFUNC_ThreadStart)Process->HeaderInfo->Preferred.AddressOfEntryPoint,
                                NULL,
                                &pThread,
                                Process);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("ThreadCreate", status);
            __leave;
        }

        LOG_TRACE_USERMODE("Successfully created thread!\n");
    }
    __finally
    {
        if (pThread != NULL)
        {
            // This MUST be done on both success and failure
            // We are not returning the thread pointer anywhere => we need to close
            // it here so the reference count will be zero when it will terminate
            ThreadCloseHandle(pThread);
            pThread = NULL;
        }
    }

    LOG_FUNC_END;

    return status;
}

static
STATUS
_UmApplicationReadExecutableContents(
    IN_Z        char*       FullPath,
    _Outptr_result_buffer_(*BufferSize)
                PVOID*      Buffer,
    OUT         QWORD*      BufferSize
    )
{
    PFILE_OBJECT pExecutableFile;
    STATUS status;
    FILE_INFORMATION fileInfo;
    PVOID pBuffer;
    QWORD bytesRead;

    LOG_FUNC_START;

    pExecutableFile = NULL;
    status = STATUS_SUCCESS;
    pBuffer = NULL;
    memzero(&fileInfo, sizeof(FILE_INFORMATION));
    bytesRead = 0;

    __try
    {
        LOG_TRACE_USERMODE("Will open executable found at [%s]\n", FullPath);

        status = IoCreateFile(&pExecutableFile,
                              FullPath,
                              FALSE,
                              FALSE,
                              FALSE);
        if (!SUCCEEDED(status))
        {
            LOG_TRACE_USERMODE("[ERROR] IoCreateFile with status 0x%x\n", status);
            __leave;
        }

        status = IoQueryInformationFile(pExecutableFile,
                                        &fileInfo);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("IoQueryInformationFile", status);
            __leave;
        }

        LOG_TRACE_USERMODE("Executable has %U bytes length, file object at 0x%X!\n", fileInfo.FileSize, pExecutableFile);
        ASSERT(fileInfo.FileSize <= MAX_DWORD);

        // Allocate a memory region backed up by a file and bring it eagerly into memory
        pBuffer = VmmAllocRegionEx(NULL,
                                   fileInfo.FileSize,
                                   VMM_ALLOC_TYPE_RESERVE | VMM_ALLOC_TYPE_COMMIT | VMM_ALLOC_TYPE_NOT_LAZY,
                                   PAGE_RIGHTS_READWRITE,
                                   FALSE,
                                   pExecutableFile,
                                   NULL,
                                   NULL,
                                   NULL);
        if (pBuffer == NULL)
        {
            LOG_FUNC_ERROR_ALLOC("VmmAllocRegionEx", fileInfo.FileSize);
            __leave;
        }

        LOG_TRACE_USERMODE("Buffer allocated at 0x%X\n", pBuffer);
    }
    __finally
    {
        if (SUCCEEDED(status))
        {
            *BufferSize = fileInfo.FileSize;
            *Buffer = pBuffer;
        }
        else
        {
            if (pExecutableFile != NULL)
            {
                IoCloseFile(pExecutableFile);
                pExecutableFile = NULL;
            }
        }
    }

    LOG_FUNC_END;

    return status;
}

static
void
_UmApplicationDiscardKernelExecutableMapping(
    IN          PVOID       ApplicationBuffer
    )
{
    ASSERT(ApplicationBuffer != NULL);

    LOG_TRACE_USERMODE("Will free memory region at 0x%X\n", ApplicationBuffer);

    // We don't want to release the physical frames because they will still be used by
    // the user-mode application
    VmmFreeRegionEx(ApplicationBuffer, 0, VMM_FREE_TYPE_RELEASE, FALSE, NULL, NULL);
}