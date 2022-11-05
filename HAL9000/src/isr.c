#include "HAL9000.h"
#include "isr.h"
#include "idt_handlers.h"
#include "iomu.h"
#include "pic.h"
#include "thread_internal.h"
#include "mmu.h"
#include "cpumu.h"
#include "dmp_cpu.h"
#include "process.h"

#define UNDEFINED_INTERRUPT_TEXT                "UNKNOWN INTERRUPT"
#define STACK_BYTES_TO_DUMP_ON_EXCEPTION        0x100

const char EXCEPTION_NAME[ExceptionVirtualizationException+1][MAX_PATH] = { "#DE - Divide Error", "#DB - Debug Exception", "NMI Interrupt",
                                                                            "#BP - Breakpoint Exception", "#OF - Overflow Exception", "#BR - BOUND Range Exceeded Exception",
                                                                            "#UD - Invalid Opcode Exception", "#NM - Device Not Available Exception", "#DF - Double Fault Exception",
                                                                            "Coprocessor Segment Overrun", "#TS - Invalid TSS Exception", "#NP - Segment Not Present",
                                                                            "#SS - Stack Fault Exception", "#GP - General Protection Exception", "#PF - Page-Fault Exception",
                                                                            UNDEFINED_INTERRUPT_TEXT, "#MF - x87 FPU Floating-Point Error", "#AC - Alignment Check",
                                                                            "#MC - Machine-Check Exception", "#XM - SIMD Floating-Point Exception", "#VE - Virtualization Exception"
                                                                        };

const char INTERRUPT_NAME[NO_OF_IRQS][MAX_PATH] = {     "Timer", "Keyboard", "Cascade",
                                                        "COM2", "COM1", UNDEFINED_INTERRUPT_TEXT,
                                                        "Diskette", "LPT1", "CMOS RTC",
                                                        "CGA", UNDEFINED_INTERRUPT_TEXT,UNDEFINED_INTERRUPT_TEXT
                                                        UNDEFINED_INTERRUPT_TEXT, "FPU","Hard Disk",UNDEFINED_INTERRUPT_TEXT,
                                                        UNDEFINED_INTERRUPT_TEXT
                                                        };

static PFUNC_IsrRoutine m_isrRoutines[NO_OF_USABLE_INTERRUPTS] = { NULL };
static PVOID m_isrContexts[NO_OF_USABLE_INTERRUPTS] = { NULL };

static
void
_IsrExceptionHandler(
    IN BYTE                         InterruptIndex,
    IN PINTERRUPT_STACK_COMPLETE    StackPointer,
    IN BOOLEAN                      ErrorCodeAvailable,
    IN COMPLETE_PROCESSOR_STATE*    ProcessorState
    );

static
void
_IsrInterruptHandler(
    IN BYTE             InterruptIndex
    );


void
IsrCommonHandler(
    IN BYTE                                 InterruptIndex,
    IN PINTERRUPT_STACK_COMPLETE            StackPointer,
    IN BOOLEAN                              ErrorCodeAvailable,
    IN COMPLETE_PROCESSOR_STATE*            ProcessorState
    )
{
    PPCPU pPcpu;

    CHECK_STACK_ALIGNMENT;

    ASSERT(CpuIntrGetState() == INTR_OFF);

    pPcpu = GetCurrentPcpu();
    if (NULL != pPcpu)
    {
        // in the early stages the PCPU may have not been yet set
        pPcpu->InterruptsTriggered[InterruptIndex] += 1;
    }

    if (InterruptIndex < NO_OF_RESERVED_EXCEPTIONS)
    {
        _IsrExceptionHandler(InterruptIndex, StackPointer, ErrorCodeAvailable, ProcessorState);
    }
    else
    {
        _IsrInterruptHandler(InterruptIndex);
    }
}

static
void
_IsrExceptionHandler(
    IN BYTE                         InterruptIndex,
    IN PINTERRUPT_STACK_COMPLETE    StackPointer,
    IN BOOLEAN                      ErrorCodeAvailable,
    IN COMPLETE_PROCESSOR_STATE*             ProcessorState
    )
{
    DWORD errorCode;
    BOOLEAN exceptionHandled;

    errorCode = 0;
    exceptionHandled = FALSE;

    LOG_TRACE_EXCEPTION("Exception: 0x%x [%s]\n", InterruptIndex, EXCEPTION_NAME[InterruptIndex]);

    // now even if we don't have an error code
    // our ISRs push a zero on the stack
    ASSERT(NULL != StackPointer);

    if (ErrorCodeAvailable)
    {
        errorCode = (DWORD)StackPointer->ErrorCode;
    }

    if (ExceptionPageFault == InterruptIndex)
    {
        PVOID pfAddr;

        ASSERT(ErrorCodeAvailable);

        pfAddr = __readcr2();
        LOG_TRACE_EXCEPTION("#PF address: 0x%X\n", pfAddr);
        exceptionHandled = MmuSolvePageFault(pfAddr, errorCode );
        if (!exceptionHandled)
        {
            PPCPU pCpu;

            pCpu = GetCurrentPcpu();
            if (NULL != pCpu)
            {
                BYTE* pStackBottom;

                pStackBottom = (BYTE*)pCpu->StackTop - pCpu->StackSize;

                if (CHECK_BOUNDS(pfAddr, 1, pStackBottom - STACK_GUARD_SIZE, STACK_GUARD_SIZE))
                {
                    // memory accessed is directly below the stack (in the unmapped stack guard area)
                    LOG_ERROR("Stack overflow\n"
                              "Stack in range [0x%X, 0x%X]\n"
                              "#PF is at 0x%X\n",
                              pStackBottom, pCpu->StackTop,
                              pfAddr
                              );
                }
            }
        }
    }
    else if (ExceptionGeneralProtection == InterruptIndex)
    {
        LOG_TRACE_EXCEPTION("RSP[0]: 0x%X\n", *((QWORD*)StackPointer->Registers.Rsp));
    }

    // no use in logging if we solved the problem
    if (!exceptionHandled)
    {
        PVOID* pCurrentStackItem;
        DWORD noOfStackElementsToDump;
        PPCPU pCpu;

        LOG_ERROR("Could not handle exception 0x%x [%s]\n", InterruptIndex, EXCEPTION_NAME[InterruptIndex]);

        DumpInterruptStack(StackPointer, ErrorCodeAvailable );
        DumpControlRegisters();
        DumpProcessorState(ProcessorState);

        LOG("Faulting stack data:\n");

        pCpu = GetCurrentPcpu();

        pCurrentStackItem = (PVOID*) max(StackPointer->Registers.Rsp,
                                         pCpu != NULL ? (QWORD) PtrDiff(pCpu->StackTop, (QWORD) pCpu->StackSize)
                                                      : StackPointer->Registers.Rsp);
        noOfStackElementsToDump = (DWORD) (min(STACK_BYTES_TO_DUMP_ON_EXCEPTION,
                                               pCpu != NULL ? PtrDiff(pCpu->StackTop,pCurrentStackItem) : STACK_BYTES_TO_DUMP_ON_EXCEPTION)
                                           / sizeof(PVOID));
        for (DWORD i = 0; i < noOfStackElementsToDump; ++i)
        {
            LOG("[0x%X]: 0x%X\n", &pCurrentStackItem[i], pCurrentStackItem[i]);
        }
    }

    ASSERT_INFO(exceptionHandled, "Exception 0x%x was not handled\n", InterruptIndex);
}

static
void
_IsrInterruptHandler(
    IN BYTE             InterruptIndex
    )
{
    BOOLEAN interruptHandled;
    BOOLEAN bSpuriousInterrupt;
    BYTE indexInHandlers;
    IRQL prevIrql;

    interruptHandled = FALSE;
    indexInHandlers = InterruptIndex - NO_OF_RESERVED_EXCEPTIONS;
    bSpuriousInterrupt = FALSE;

    // In operating systems that use the lowest priority delivery mode but do not update the TPR, the TPR information
    // saved in the chipset will potentially cause the interrupt to be always delivered to the same processor from the
    // logical set. This behavior is functionally backward compatible with the P6 family processor but may result in
    // unexpected performance implications.
    prevIrql = CpuMuRaiseIrql(VECTOR_TO_IRQL(InterruptIndex));

    // call registered interrupt
    if (NULL != m_isrRoutines[indexInHandlers])
    {
        interruptHandled = m_isrRoutines[indexInHandlers](m_isrContexts[indexInHandlers]);
    }
    else
    {
        LOG_ERROR("No interrupt registered for interrupt 0x%x\n", InterruptIndex);
    }

    if (!interruptHandled)
    {
        bSpuriousInterrupt = IomuIsInterruptSpurious(InterruptIndex);
        if (bSpuriousInterrupt)
        {
            LOGP_WARNING("Received spurious vector 0x%02x\n", InterruptIndex);
        }
    }

    ASSERT_INFO(interruptHandled || bSpuriousInterrupt, "Interrupt 0x%x was not handled\n", InterruptIndex);

    if (!bSpuriousInterrupt)
    {
        // send EOI
        IomuAckInterrupt(InterruptIndex);
    }

    // must be called before ThreadYield, else we may lower the IRQL too late or never
    // if the thread terminates
    CpuMuLowerIrql(prevIrql);

    if (ThreadYieldOnInterrupt())
    {
        ThreadYield();
    }
}

STATUS
IsrInstallEx(
    IN      BYTE                Vector,
    IN      PFUNC_IsrRoutine    IsrRoutine,
    IN_OPT  PVOID               Context
    )
{
    BYTE indexInRoutines;

    ASSERT( Vector > NO_OF_RESERVED_EXCEPTIONS);

    indexInRoutines = Vector - NO_OF_RESERVED_EXCEPTIONS;

    if (NULL != m_isrRoutines[indexInRoutines])
    {
        LOG_WARNING("There is already a routine installed at vector 0x%x\n", indexInRoutines);
        return STATUS_ALREADY_INITIALIZED;
    }

    LOG_TRACE_INTERRUPT("Registering ISR for vector: 0x%x\n", Vector);
    m_isrRoutines[indexInRoutines] = IsrRoutine;
    m_isrContexts[indexInRoutines] = Context;

    return STATUS_SUCCESS;
}
