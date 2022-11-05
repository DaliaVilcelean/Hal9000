#pragma once

typedef struct _PROCESS* PPROCESS;
typedef struct _PE_NT_HEADER_INFO* PPE_NT_HEADER_INFO;

STATUS
UmApplicationRetrieveHeader(
    IN_Z        char*                   Path,
    OUT         PPE_NT_HEADER_INFO      NtHeaderInfo
    );

STATUS
UmApplicationRun(
    IN          PPROCESS                Process,
    IN          BOOLEAN                 WaitForExecution,
    OUT_OPT     STATUS*                 CompletionStatus
    );
