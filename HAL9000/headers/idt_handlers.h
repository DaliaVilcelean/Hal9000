#pragma once

typedef enum _EXCEPTION
{
    ExceptionDivideError                = 0,
    ExceptionDebugException,
    ExceptionNMI,
    ExceptionBreakpoint,
    ExceptionOverflow,
    ExceptionBoundRange,
    ExceptionInvalidOpcode,
    ExceptionDeviceNotAvailable,
    ExceptionDoubleFault,
    ExceptionCoprocOverrun,
    ExceptionInvalidTSS,
    ExceptionSegmentNotPresent,
    ExceptionStackFault,
    ExceptionGeneralProtection,
    ExceptionPageFault                  = 14,
    ExceptionX87FpuException            = 16,
    ExceptionAlignmentCheck,
    ExceptionMachineCheck,
    ExceptionSIMDFpuException,
    ExceptionVirtualizationException
} EXCEPTION;

typedef enum _INTERRUPT
{
    InterruptTimer              = 32,
    InterruptKeyboard,
    InterruptCascade,
    InterruptCOM2,
    InterruptCOM1,
    InterruptDiskette           = 38,
    InterruptLPT1,
    InterruptCmosRTC,
    InterruptCGA,
    InterruptFPU                = 45,
    InterruptHdController
} INTERRUPT;

#define     NO_OF_IDT_ENTRIES               MAX_BYTE

#define     IVT_ENTRY_SIZE                  4
#define     IVT_LIMIT                       0x3FF

STATUS
InitIdtHandlers(
    IN      WORD                CodeSelector,
    IN      BYTE                NumberOfTssStacks
    );