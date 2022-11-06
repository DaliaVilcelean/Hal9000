#include "HAL9000.h"
#include "cmd_interpreter.h"

#include "keyboard_utils.h"
#include "display.h"
#include "print.h"

#include "cmd_common.h"
#include "cmd_fs_helper.h"
#include "cmd_thread_helper.h"
#include "cmd_proc_helper.h"
#include "cmd_sys_helper.h"
#include "cmd_net_helper.h"
#include "cmd_basic.h"
#include "boot_module.h"

#pragma warning(push)

// warning C4029: declared formal parameter list different from definition
#pragma warning(disable:4029)

#define CMD_MAX_ARGS        10

#define CMD_EXIT            "exit"

static FUNC_GenericCommand  _CmdPrintHelp;

// warning C4212: nonstandard extension used: function declaration used ellipsis
#pragma warning(push)
#pragma warning(disable:4212)

typedef struct _COMMAND_DEFINITION
{
    char*                   CommandName;
    char*                   Description;

    PFUNC_GenericCommand    CommandFunction;

    QWORD                   MinParameters;
    QWORD                   MaxParameters;
} COMMAND_DEFINITION, *PCOMMAND_DEFINITION;

static const COMMAND_DEFINITION COMMANDS[] =
{
    { "reset", "Restarts the system", CmdResetSystem, 0, 0},
    { "shutdown", "Shuts the system down", CmdShutdownSystem, 0, 0},

    { "log", "[ON|OFF] - enables or disables logging", CmdLogSetState, 1, 1},
    { "loglevel", "$LOG_LEVEL - decimal value from enum", CmdSetLogLevel, 1, 1},
    { "logcomp", "$LOG_COMPONENT - hexadecimal value from enum", CmdSetLogComponents, 1, 1},

    { "cls", "Clears screen", CmdClearScreen, 0, 0},

    { "vol", "Displays volumes", CmdPrintVolumeInformation, 0, 0},
    { "less", "$FILENAME [async]\n\tdisplay $FILENAME contents\n\tasync - use DMA read instead of polling", CmdReadFile, 1, 2 },
    { "fwrite", "$FILENAME [char] [ext] [async]\n\twrite predefined buffer into $FILENAME\n\text - if 'ext' then extend file size\n\tasync - use DMA read instead of polling", CmdWriteFile, 1, 4},
    { "stat", "$FILENAME\n\tdisplays $FILENAME information", CmdStatFile, 1, 1},
    { "mkdir", "$DIRECTORY\n\tcreates a new directory", CmdMakeDirectory, 1, 1},
    { "touch", "$FILENAME\n\tcreates a new file", CmdMakeFile, 1, 1},
    { "ls", "$DIRECTORY [-R]\n\tlists directory contents\n\tif -R specified goes recursively", CmdListDirectory, 1, 2},

    { "swap", "R|W [0x$OFFSET]\n\t$OFFSET - offset inside swap where to perform operation", CmdSwap, 1, 2},

    { "cpu", "Displays CPU related information", CmdListCpus, 0, 0},
    { "int", "List interrupts received", CmdListCpuInterrupts, 0, 0},
    { "yield", "Yields processor", CmdYield, 0, 0},
    { "timer", "$MODE [$TIME_IN_US] [$TIMES]\n\tSee EX_TIMER_TYPE for timer types\n\t$TIME_IN_US time in uS until timer fires"
                "\n\t$TIMES - number of times to wait for timer, valid only if periodic", CmdTestTimer, 1, 3},

    { "threads", "Displays all threads", CmdListThreads, 0, 0},
    { "run", "$TEST [$NO_OF_THREADS]\n\tRuns the $TEST specified"
             "\n\t$NO_OF_THREADS the number of threads for running the test,"
             "if the number is not specified then it will run on 2 * NumberOfProcessors",
             CmdRunTest, 1, 2},

    { "processes", "Displays all processes", CmdListProcesses, 0, 0},
    { "procstat", "0x$PID - displays information about a process", CmdProcessDump, 1, 1},
    { "procstart", "$PATH_TO_EXE - starts a process", CmdStartProcess, 1, 1},
    { "proctest", "$TEST_NAME - runs a process test", CmdTestProcess, 1, 1},

    { "sysinfo", "Retrieves system information", CmdDisplaySysInfo, 0, 0},
    { "getidle", "Retrieves idle timeout", CmdGetIdle, 0, 0},
    { "setidle", "$PERIOD_IN_SECONDS - Sets idle timeout", CmdSetIdle, 1, 1},

    { "rdmsr", "0x$INDEX\n\t$INDEX is the MSR to read", CmdRdmsr, 1, 1},
    { "wrmsr", "0x$INDEX 0x$VALUE\n\t$INDEX is the MSR to write\n\t$VALUE is the value to place in the MSR", CmdWrmsr, 2, 2},
    { "chkad", "Check if paging accessed/dirty bits mechanism is working", CmdCheckAd, 0, 0},
    { "spawn", "$CPU_BOUND $IO_BOUND\n\tNumber of CPU bound threads to spawn\n\tNumber of IO bound threads to spawn", CmdSpawnThreads, 2, 2},
    { "cpuid", "[0x$INDEX] [0x$SUBINDEX]\n\tIf index is not specified lists all available CPUID values"
                "\n\tIf subindex is specified displays subleaf information", CmdCpuid, 0, 2},
    { "ipi", "$MODE [$DEST] {$WAIT]\n\tSee SMP_IPI_SEND_MODE for destination mode\n\t$DEST - processor IDs"
              "\n\tIf last parameter is specified will wait until all CPUs acknowledge IPI", CmdSendIpi, 1, 3},

    { "networks", "Displays network information", CmdListNetworks, 0, 0},
    { "netrecv", "[YES|NO] - receive network packets\n\tIf yes will resend the packets received, if no it will not", CmdNetRecv, 0, 1},
    { "netsend", "Send network packets", CmdNetSend, 0, 0},
    { "netstatus", "$DEV_ID $RX_EN $TX_EN - changes the state of a network device"
                   "\n\tDevice ID\n\tIf $RX_EN is 1 => will enable receive on device\n\tIf $TX_EN is 1 => will enable send on device",
                    CmdChangeDevStatus, 3, 3},

    { "tests", "Runs functional tests", CmdRunAllFunctionalTests, 0, 0},
    { "perf", "Runs performance tests", CmdRunAllPerformanceTests, 0, 0},

    { "recursion", "Generates an infinite recursion", CmdInfiniteRecursion, 0, 0},
    { "rtcfail", "Causes an RTC check stack to assert", CmdRtcFail, 0, 0},
    { "rangefail", "Causes a range check failure to assert", CmdRangeFail, 0, 0},
    { "bitecookie", "Causes a GS cookie corruption to assert", CmdBiteCookie, 0, 0},

    { "help", "Displays this help menu", _CmdPrintHelp, 0, 0}
};

#define NO_OF_COMMANDS      ARRAYSIZE(COMMANDS)


static
BOOLEAN
_CmdExecLine(
    _Inout_updates_z_(Length)
                    char*   CommandLine,
    IN              DWORD   Length
    );

static
BOOLEAN
_CmdExecuteModuleCommands(
    void
    );

// SAL simply doesn't want to let me tell him that each pointer in argv is NULL terminated
// _At_buffer_(argv, i, argc,
//             _Pre_satisfies_(argv[i] _Null_terminated_))
BOOLEAN
ExecCmd(
    IN      DWORD       argc,
    IN_READS(CMD_MAX_ARGS)
            char**      argv
    )
{
    char* pCommand;
    BOOLEAN bFoundCommand;
    DWORD noOfParameters;

    ASSERT(1 <= argc && argc <= CMD_MAX_ARGS);
    ASSERT(NULL != argv);

    pCommand = (char*) argv[0];
    bFoundCommand = FALSE;
    noOfParameters = argc - 1;

    ASSERT(NULL != pCommand);

    // check for exit command
    if (0 == stricmp(pCommand, CMD_EXIT))
    {
        return TRUE;
    }

    for (DWORD i = 0; i < NO_OF_COMMANDS; ++i)
    {
        if (stricmp(pCommand, COMMANDS[i].CommandName) == 0)
        {
            bFoundCommand = TRUE;

            if (COMMANDS[i].MinParameters <= noOfParameters && noOfParameters <= COMMANDS[i].MaxParameters)
            {
                COMMANDS[i].CommandFunction(noOfParameters,
                                            argv[1],
                                            argv[2],
                                            argv[3],
                                            argv[4],
                                            argv[5],
                                            argv[6],
                                            argv[7],
                                            argv[8],
                                            argv[CMD_MAX_ARGS - 1]);

                break;
            }
            else
            {
                LOG_ERROR("Tried to call command [%s] which requires between %u and %u parameters with %u parameters!\n",
                          COMMANDS[i].CommandName, COMMANDS[i].MinParameters, COMMANDS[i].MaxParameters, noOfParameters);
                LOG("%s\n", COMMANDS[i].Description);
            }
        }
    }

    if (!bFoundCommand)
    {
        _CmdPrintHelp(0);
    }

    return FALSE;
}

void
CmdRun(
    void
    )
{
    BOOLEAN exit;
    char buffer[CHARS_PER_LINE];
    DWORD bytesRead;

    bytesRead = 0;

    exit = _CmdExecuteModuleCommands();
    while (!exit)
    {
        gets_s(buffer, CHARS_PER_LINE, &bytesRead);

        exit = _CmdExecLine(buffer, bytesRead);
    }

    return;
}

static
BOOLEAN
_CmdExecLine(
    _Inout_updates_z_(Length)
                    char*   CommandLine,
    IN              DWORD   Length
    )
{
    BOOLEAN bExit;
    char* pCmdArgs[CMD_MAX_ARGS];

    bExit = FALSE;

    if (CommandLine[0] == '/')
    {
        char* context;
        char* pCurArg;
        DWORD argIndex;

        memzero(pCmdArgs, sizeof(char*) * CMD_MAX_ARGS);
        context = NULL;
        pCurArg = NULL;
        argIndex = 0;

        if (Length <= 1)
        {
            pwarn("A command must succeed the '/' character\n");
            return FALSE;
        }

        // warning C4127: conditional expression is constant
#pragma warning(suppress:4127)
        while (TRUE)
        {
            pCurArg = (char*)strtok_s(&CommandLine[1], " ", &context);
            if (pCurArg == NULL)
            {
                break;
            }

            ASSERT(argIndex < CMD_MAX_ARGS);

            pCmdArgs[argIndex] = pCurArg;
            argIndex = argIndex + 1;
        }

        // we have a command
        bExit = ExecCmd(argIndex, pCmdArgs);
    }
    else
    {
        // just plain old text
        printf("%s\n", CommandLine);
    }

    return bExit;
}

static
void
(__cdecl _CmdPrintHelp)(
    IN      QWORD           NumberOfParameters
    )
{
    ASSERT(NumberOfParameters == 0);

    LOG("All commands are prefixed with the '/' character and are case insensitive\n");
    LOG("Available commands:\n");
    LOG("exit - stops the OS\n");
    for (DWORD i = 0; i < NO_OF_COMMANDS; ++i)
    {
        LOG("%s - %s\n", COMMANDS[i].CommandName, COMMANDS[i].Description);
    }
}

static
BOOLEAN
_CmdExecuteModuleCommands(
    void
    )
{
    STATUS status;
    char* pBaseAddress;
    QWORD modLen;
    char* pCurrentLine;
    char* context;
    BOOLEAN bExit;

    context = NULL;
    bExit = FALSE;

    status = BootModuleGet("Tests", &pBaseAddress, &modLen);
    if (!SUCCEEDED(status))
    {
        LOG_WARNING("BootModuleGet failed with status 0x%x for Tests module\n", status);
        return FALSE;
    }

    do
    {
        pCurrentLine = (char*)strtok_s(pBaseAddress, "\n\r", &context);

        if (pCurrentLine != NULL && strlen(pCurrentLine) != 0)
        {
            LOG("Current line is [%s] of length 0x%x\n", pCurrentLine,
                strlen(pCurrentLine));
            bExit = _CmdExecLine(pCurrentLine, strlen(pCurrentLine));
        }

    } while (pCurrentLine != NULL && !bExit);

    return bExit;
}

#pragma warning(pop)
