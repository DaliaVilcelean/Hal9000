#include "HAL9000.h"
#include "cmd_fs_helper.h"

#include "display.h"
#include "print.h"
#include "io.h"
#include "os_time.h"

#include "dmp_memory.h"
#include "strutils.h"
#include "iomu.h"

#pragma warning(push)

// warning C4212: nonstandard extension used: function declaration used ellipsis
#pragma warning(disable:4212)

#define CMD_READFILE_BYTES_TO_READ_AT_A_TIME            (32*KB_SIZE)

static
void
_DisplayDirectoryInformation(
    IN_Z    char*                           Directory,
    IN      PFILE_DIRECTORY_INFORMATION     FileInformation,
    IN      BOOLEAN                         Recursive,
    IN      DWORD                           CurrentRecursionLevel
    );

static
void
_ListDirectory(
    IN_Z    char*       File,
    IN      BOOLEAN     Recursive,
    IN      DWORD       CurrentRecursionLevel
    );

void
(__cdecl CmdStatFile)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;
    FILE_INFORMATION fileInformation;
    char tempBuffer[MAX_PATH];
    char* pFileType;

    ASSERT(NumberOfParameters == 1);

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;
    memzero(&fileInformation, sizeof(FILE_INFORMATION));
    pFileType = NULL;

    printf("Will open file %s\n", File);

    status = IoCreateFile(&pFileObject,
                          File,
                          FALSE,
                          FALSE,
                          FALSE
                          );
    if (!SUCCEEDED(status))
    {
        // try to open path as a directory
        status = IoCreateFile(&pFileObject,
                              File,
                              TRUE,
                              FALSE,
                              FALSE
                              );
        if (!SUCCEEDED(status))
        {
            perror("IoCreateFile failed with status: 0x%x\n", status);
            return;
        }
    }

    printf("IoCreateFile succeeded\n");

    __try
    {
        status = IoQueryInformationFile(pFileObject,
                                        &fileInformation
        );
        if (!SUCCEEDED(status))
        {
            perror("IoQueryInformationFile failed with status: 0x%x\n", status);
            __leave;
        }

        printf("IoQueryInformationFile succeeded\n");

        if (IsBooleanFlagOn(fileInformation.FileAttributes, FILE_ATTRIBUTE_VOLUME))
        {
            pFileType = "VOLUME";
        }
        else if (IsBooleanFlagOn(fileInformation.FileAttributes, FILE_ATTRIBUTE_DIRECTORY))
        {
            pFileType = "DIRECTORY";
        }
        else
        {
            pFileType = "FILE";
        }

        printf("File type is: %s\n", pFileType);
        printf("File size: %D KB\n", fileInformation.FileSize / KB_SIZE);
        OsTimeGetStringFormattedTime(&fileInformation.CreationTime, tempBuffer, MAX_PATH);
        printf("Creation time: %s\n", tempBuffer);

        OsTimeGetStringFormattedTime(&fileInformation.LastWriteTime, tempBuffer, MAX_PATH);
        printf("Last write time: %s\n", tempBuffer);
    }
    __finally
    {
        if (NULL != pFileObject)
        {
            status = IoCloseFile(pFileObject);
            if (!SUCCEEDED(status))
            {
                perror("IoCloseFIle failed with status: 0x%x\n", status);
            }
            else
            {
                printf("IoCloseFile succeeded\n");
            }

            pFileObject = NULL;
        }
    }
}

void
(__cdecl CmdMakeDirectory)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;

    ASSERT(NumberOfParameters == 1);

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;

    printf("Will create directory %s\n", File);

    status = IoCreateFile(&pFileObject,
                          File,
                          TRUE,
                          TRUE,
                          FALSE
                          );
    if (!SUCCEEDED(status))
    {
        perror("IoCreateFile failed with status: 0x%x\n", status);
        return;
    }

    printf("IoCreateFile succeeded\n");

    status = IoCloseFile(pFileObject);
    if (!SUCCEEDED(status))
    {
        perror("IoCloseFIle failed with status: 0x%x\n", status);
        return;
    }
    pFileObject = NULL;

    printf("IoCloseFile succeeded\n");
}

void
(__cdecl CmdMakeFile)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;

    ASSERT(NumberOfParameters == 1);

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;

    printf("Will create file %s\n", File);

    status = IoCreateFile(&pFileObject,
                          File,
                          FALSE,
                          TRUE,
                          FALSE
                          );
    if (!SUCCEEDED(status))
    {
        perror("IoCreateFile failed with status: 0x%x\n", status);
        return;
    }
    else
    {
        printf("IoCreateFile succeeded\n");
    }

    status = IoCloseFile(pFileObject);
    if (!SUCCEEDED(status))
    {
        perror("IoCloseFIle failed with status: 0x%x\n", status);
        return;
    }
    pFileObject = NULL;

    printf("IoCloseFile succeeded\n");
}

void
(__cdecl CmdListDirectory)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File,
    IN_Z    char*       Recursive
    )
{
    printf("Directory [%s] contents:\n", File );
    _ListDirectory(File,
                   NumberOfParameters >= 2 ? (stricmp(Recursive, "-R") == 0) : FALSE,
                   0);
}

void
(__cdecl CmdReadFile)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File,
    IN_Z    char*       Async
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;
    QWORD bytesRead;
    FILE_INFORMATION fileInformation;
    PBYTE pData;
    QWORD bytesRemaining;
    DWORD allocationSize;
    DWORD bytesToRead;
    QWORD fileOffset;
    BOOLEAN bAsync;

    ASSERT(1 <= NumberOfParameters && NumberOfParameters <= 2);

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;
    bytesRead = 0;
    memzero(&fileInformation, sizeof(FILE_INFORMATION));
    pData = NULL;
    bytesRemaining = 0;
    allocationSize = 0;
    bytesToRead = 0;
    fileOffset = 0;
    bAsync = (NumberOfParameters >= 2) ? (stricmp(Async, "async") == 0) : FALSE;

    printf("Will open file %s\n", File);

    status = IoCreateFile(&pFileObject,
                          File,
                          FALSE,
                          FALSE,
                          bAsync
                          );
    if (!SUCCEEDED(status))
    {
        perror("IoCreateFile failed with status: 0x%x\n", status);
        return;
    }

    printf("IoCreateFile succeeded\n");

    __try
    {
        status = IoQueryInformationFile(pFileObject,
                                        &fileInformation
        );
        if (!SUCCEEDED(status))
        {
            perror("IoQueryInformationFile failed with status: 0x%x\n", status);
            __leave;
        }

        printf("IoQueryInformationFile succeeded\n");

        bytesRemaining = fileInformation.FileSize;
        allocationSize = (DWORD)min(fileInformation.FileSize, CMD_READFILE_BYTES_TO_READ_AT_A_TIME);

        pData = ExAllocatePoolWithTag(0, allocationSize, HEAP_TEMP_TAG, 0);
        if (NULL == pData)
        {
            perror("HeapAllocatePoolWithTag failed for file size: %D KB\n", allocationSize / KB_SIZE);
            __leave;
        }

        printf("HeapAllocatePoolWithTag succeeded\n");

        while (0 != bytesRemaining)
        {
            bytesToRead = (DWORD)min(bytesRemaining, allocationSize);

            status = IoReadFile(pFileObject,
                                bytesToRead,
                                &fileOffset,
                                pData,
                                &bytesRead
            );
            if (!SUCCEEDED(status))
            {
                perror("IoReadFile failed with status: 0x%x\n", status);
                __leave;
            }
            ASSERT(bytesToRead == bytesRead);

            DumpMemory(pData, fileOffset, (DWORD)bytesRead, TRUE, TRUE);

            bytesRemaining = bytesRemaining - bytesRead;
            fileOffset = fileOffset + bytesRead;
        }
    }
    __finally
    {
        if (NULL != pFileObject)
        {
            status = IoCloseFile(pFileObject);
            if (!SUCCEEDED(status))
            {
                perror("IoCloseFIle failed with status: 0x%x\n", status);
            }
            else
            {
                printf("IoCloseFile succeeded\n");
            }

            pFileObject = NULL;
        }

        if (NULL != pData)
        {
            ExFreePoolWithTag(pData, HEAP_TEMP_TAG);
            pData = NULL;
        }
    }
}

void
(__cdecl CmdWriteFile)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       File,
    IN_Z    char*       CharToWrite,
    IN_Z    char*       Extend,
    IN_Z    char*       Async
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;
    QWORD bytesWritten;
    FILE_INFORMATION fileInformation;
    PBYTE pData;
    QWORD bytesRemaining;
    DWORD allocationSize;
    DWORD bytesToWrite;
    QWORD fileOffset;
    char  charToWrite;
    BOOLEAN bExtend;
    BOOLEAN bAsync;

    ASSERT(1 <= NumberOfParameters && NumberOfParameters <= 4);

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;
    bytesWritten = 0;
    memzero(&fileInformation, sizeof(FILE_INFORMATION));
    pData = NULL;
    bytesRemaining = 0;
    allocationSize = 0;
    bytesToWrite = 0;
    fileOffset = 0;
    charToWrite = (NumberOfParameters >= 2) ? CharToWrite[0] : 'x';
    bExtend = (NumberOfParameters >= 3) ? (stricmp(Extend, "ext") == 0) : FALSE;
    bAsync = (NumberOfParameters >= 4) ? (stricmp(Async, "async") == 0) : FALSE;

    printf("Will open file %s\n", File);

    status = IoCreateFile(&pFileObject,
        File,
        FALSE,
        FALSE,
        bAsync
    );
    if (!SUCCEEDED(status))
    {
        perror("IoCreateFile failed with status: 0x%x\n", status);
        return;
    }

    printf("IoCreateFile succeeded\n");

    __try
    {
        status = IoQueryInformationFile(pFileObject,
            &fileInformation
        );
        if (!SUCCEEDED(status))
        {
            perror("IoQueryInformationFile failed with status: 0x%x\n", status);
            __leave;
        }

        printf("IoQueryInformationFile succeeded\n");

        bytesRemaining = fileInformation.FileSize;
        bytesRemaining += bExtend ? 9 * 0x200 : 0;
        allocationSize = (DWORD)min(fileInformation.FileSize, CMD_READFILE_BYTES_TO_READ_AT_A_TIME);
        allocationSize = (DWORD)min(fileInformation.FileSize, 1 * 0x200);

        pData = ExAllocatePoolWithTag(0, allocationSize, HEAP_TEMP_TAG, 0);
        if (NULL == pData)
        {
            perror("HeapAllocatePoolWithTag failed for file size: %D KB\n", allocationSize / KB_SIZE);
            __leave;
        }

        printf("HeapAllocatePoolWithTag succeeded\n");

        memset(pData, charToWrite, allocationSize);
        DWORD i = 0;
        while (0 != bytesRemaining)
        {
            sprintf((char*)pData, "%02dsector", i++);
            bytesToWrite = (DWORD)min(bytesRemaining, allocationSize);

            status = IoWriteFile(pFileObject,
                bytesToWrite,
                &fileOffset,
                pData,
                &bytesWritten
            );
            if (!SUCCEEDED(status))
            {
                perror("IoWriteFile failed with status: 0x%x\n", status);
                __leave;
            }
            printf("bytesWritten = %X\n", bytesWritten);
            ASSERT(bytesToWrite == bytesWritten);

            //DumpMemory(pData, fileOffset, (DWORD)bytesWritten, TRUE, TRUE);

            bytesRemaining = bytesRemaining - bytesWritten;
            fileOffset = fileOffset + bytesWritten;
        }
    }
    __finally
    {
        if (NULL != pFileObject)
        {
            status = IoCloseFile(pFileObject);
            if (!SUCCEEDED(status))
            {
                perror("IoCloseFIle failed with status: 0x%x\n", status);
            }
            else
            {
                printf("IoCloseFile succeeded\n");
            }

            pFileObject = NULL;
        }

        if (NULL != pData)
        {
            ExFreePoolWithTag(pData, HEAP_TEMP_TAG);
            pData = NULL;
        }
    }
}

void
(__cdecl CmdSwap)(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       Operation,
    IN_Z    char*       OffsetString
    )
{
    BOOLEAN bPerformWrite;
    QWORD offset;
    PFILE_OBJECT pSwapFile;
    STATUS status;
    QWORD bytesTransmitted;
    PBYTE pData;
    QWORD swapFileSize;

    ASSERT(1 <= NumberOfParameters && NumberOfParameters <= 2);

    bPerformWrite = (strcmp(Operation, "W") == 0);
    offset = 0;
    pSwapFile = NULL;
    status = STATUS_SUCCESS;
    bytesTransmitted = 0;
    pData = NULL;

    if (NumberOfParameters == 2)
    {
        atoi64(&offset, OffsetString, BASE_HEXA);
    }

    pSwapFile = IomuGetSwapFile();
    if (pSwapFile == NULL)
    {
        LOG_ERROR("System has no swap file!\n");
        return;
    }

    status = IoGetFileSize(pSwapFile, &swapFileSize);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoGetFileSize", status);
        return;
    }

    LOG("System has a swap file of size %U KB [%U MB]\n",
        swapFileSize / KB_SIZE, swapFileSize / MB_SIZE);

    __try
    {
        pData = ExAllocatePoolWithTag(0, PAGE_SIZE, HEAP_TEMP_TAG, 0);
        if (pData == NULL)
        {
            LOG_FUNC_ERROR_ALLOC("ExAllocatedPoolWithTag", PAGE_SIZE);
            status = STATUS_INSUFFICIENT_MEMORY;
            __leave;
        }

        if (bPerformWrite)
        {
            PQWORD pOffsets = (PQWORD) pData;
            for (DWORD i = 0; i < PAGE_SIZE / sizeof(QWORD); ++i)
            {
                pOffsets[i] = offset + i * sizeof(QWORD);
            }

            status = IoWriteFile(pSwapFile,
                                 PAGE_SIZE,
                                 &offset,
                                 pData,
                                 &bytesTransmitted);
        }
        else
        {
            status = IoReadFile(pSwapFile,
                                PAGE_SIZE,
                                &offset,
                                pData,
                                &bytesTransmitted);
        }
        if (!SUCCEEDED(status))
        {
            LOG_ERROR("[%s] Operation failed with status 0x%x at offset 0x%X\n",
                      bPerformWrite ? "WRITE" : "READ",
                      status,
                      offset);
            __leave;
        }
        ASSERT(bytesTransmitted == PAGE_SIZE);

        if (!bPerformWrite)
        {
            DumpMemory(pData, 0, PAGE_SIZE, FALSE, TRUE);
        }
    }
    __finally
    {
        if (pData != NULL)
        {
            ExFreePoolWithTag(pData, HEAP_TEMP_TAG);
            pData = NULL;
        }
    }
}

static
void
_DisplayDirectoryInformation(
    IN_Z    char*                           Directory,
    IN      PFILE_DIRECTORY_INFORMATION     FileInformation,
    IN      BOOLEAN                         Recursive,
    IN      DWORD                           CurrentRecursionLevel
    )
{
    PFILE_DIRECTORY_INFORMATION pCurEntry;
    char fileName[MAX_PATH];
    DWORD i;
    char timeBuffer[32];
    BOOLEAN lastIteration;
    COLOR color;

    ASSERT(NULL != FileInformation);

    lastIteration = FALSE;
    color = WHITE_COLOR;

    for (pCurEntry = FileInformation;
    !lastIteration;
        pCurEntry = (PFILE_DIRECTORY_INFORMATION)((PBYTE)pCurEntry + pCurEntry->NextEntryOffset)
        )
    {
        ASSERT(pCurEntry->FilenameLength < MAX_PATH - 1);

        lastIteration = 0 == pCurEntry->NextEntryOffset;

        memcpy(fileName, pCurEntry->Filename, pCurEntry->FilenameLength);
        fileName[pCurEntry->FilenameLength] = '\0';

        for (i = 0; i < CurrentRecursionLevel; ++i)
        {
            printf("\t");
        }

        OsTimeGetStringFormattedTime(&pCurEntry->BasicFileInformation.LastWriteTime, timeBuffer, 32);
        if (IsBooleanFlagOn(pCurEntry->BasicFileInformation.FileAttributes, FILE_ATTRIBUTE_VOLUME))
        {
            color = MAGENTA_COLOR;
            printColor(color, "%8s", "<VOL>");
        }
        else if (IsBooleanFlagOn(pCurEntry->BasicFileInformation.FileAttributes, FILE_ATTRIBUTE_DIRECTORY))
        {
            color = BLUE_COLOR;
            printColor(color, "%8s", "<DIR>");
        }
        else
        {
            color = WHITE_COLOR;
            printColor(color, "%5d KB", pCurEntry->BasicFileInformation.FileSize / KB_SIZE);
        }

        printf("\t");


        printColor(color, "%s\t", timeBuffer);
        printColor(color, "%s\n", fileName);

        if (Recursive)
        {
            if (IsBooleanFlagOn(pCurEntry->BasicFileInformation.FileAttributes, FILE_ATTRIBUTE_DIRECTORY))
            {
                char temp[MAX_PATH];
                BOOLEAN skip = FALSE;

                if ((0 == strcmp(fileName, ".")) ||
                    (0 == strcmp(fileName, ".."))
                    )
                {
                    // we don't go into dot or dot dot
                    skip = TRUE;
                }

                if (!skip)
                {
                    if (Directory[strlen(Directory) - 1] == '\\')
                    {
                        snprintf(temp, MAX_PATH, "%s%s", Directory, fileName);
                    }
                    else
                    {
                        snprintf(temp, MAX_PATH, "%s\\%s", Directory, fileName);
                    }

                    _ListDirectory(temp, Recursive, CurrentRecursionLevel + 1);
                }
            }
        }
    }
}

static
void
_ListDirectory(
    IN_Z    char*       File,
    IN      BOOLEAN     Recursive,
    IN      DWORD       CurrentRecursionLevel
    )
{
    STATUS status;
    PFILE_OBJECT pFileObject;
    PFILE_DIRECTORY_INFORMATION pDirectoryInformation;
    DWORD sizeOfStructure;

    if (NULL == File)
    {
        return;
    }

    status = STATUS_SUCCESS;
    pFileObject = NULL;
    pDirectoryInformation = NULL;
    sizeOfStructure = 0;

    status = IoCreateFile(&pFileObject,
                          File,
                          TRUE,
                          FALSE,
                          FALSE
                          );
    if (!SUCCEEDED(status))
    {
        perror("IoCreateFile failed with status: 0x%x\n", status);
        return;
    }

    __try
    {
        do
        {
            if (0 != sizeOfStructure)
            {
                if (NULL != pDirectoryInformation)
                {
                    ExFreePoolWithTag(pDirectoryInformation, HEAP_TEMP_TAG);
                    pDirectoryInformation = NULL;
                }

                pDirectoryInformation = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeOfStructure, HEAP_TEMP_TAG, 0);
                if (NULL == pDirectoryInformation)
                {
                    __leave;
                }
            }

            status = IoQueryDirectoryFile(pFileObject,
                                          sizeOfStructure,
                                          pDirectoryInformation,
                                          &sizeOfStructure
            );
        } while (STATUS_BUFFER_TOO_SMALL == status);
        if (!SUCCEEDED(status))
        {
            perror("IoQueryDirectoryFile failed with status: 0x%x\n", status);
            __leave;
        }

        _Analysis_assume_(NULL != pDirectoryInformation);
        _DisplayDirectoryInformation(File, pDirectoryInformation, Recursive, CurrentRecursionLevel);
    }
    __finally
    {
        if (NULL != pDirectoryInformation)
        {
            ExFreePoolWithTag(pDirectoryInformation, HEAP_TEMP_TAG);
            pDirectoryInformation = NULL;
        }

        if (NULL != pFileObject)
        {
            status = IoCloseFile(pFileObject);
            if (!SUCCEEDED(status))
            {
                perror("IoCloseFIle failed with status: 0x%x\n", status);
            }

            pFileObject = NULL;
        }
    }
}

#pragma warning(pop)
