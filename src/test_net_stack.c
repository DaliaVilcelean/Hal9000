#include "test_common.h"
#include "test_net_stack.h"
#include "io.h"
#include "thread.h"
#include "dmp_memory.h"
#include "dmp_network.h"
#include "keyboard.h"
#include "keyboard_utils.h"
#include "cpu.h"

#define RECEIVE_THREAD_INITIAL_BUFFER_SIZE                  sizeof(NET_RECEIVE_FRAME_OUTPUT)//64*KB_SIZE
#define TRANSMIT_THREAD_BUFFER_SIZE                         1*KB_SIZE

#define BUFFER_TO_SEND                                      "This is the c00le$t buffer ev4r made!!!!!"

typedef struct _NET_TRAFFIC_THREAD_CONTEXT
{
    DEVICE_ID               NetworkDevice;
    volatile BOOLEAN*       StopRequests;

    // valid only for receive thread
    BOOLEAN                 ResendRequests;
} NET_TRAFFIC_THREAD_CONTEXT, *PNET_TRAFFIC_THREAD_CONTEXT;

static FUNC_ThreadStart _TestReceivePacketsForAdapter;

static FUNC_ThreadStart _TestTransmitPacketsForAdapter;

_No_competing_thread_
BOOLEAN
TestNetwork(
        IN      BOOLEAN         Transmit,
    _When_(Transmit, _Reserved_)
    _When_(!Transmit, IN)
        IN      BOOLEAN         ResendRequets
    )
{
    STATUS status;
    DWORD noOfDevices;
    DWORD i;
    char threadName[MAX_PATH];
    PTHREAD* pThreads;
    PNET_TRAFFIC_THREAD_CONTEXT pThreadContexts;
    volatile BOOLEAN bStopRequests;
    DWORD temp;
    PNETWORK_DEVICE_INFO pNetDevices;

    LOG_FUNC_START;

    status = STATUS_SUCCESS;
    noOfDevices = 0;
    pThreads = NULL;
    pThreadContexts = NULL;
    pNetDevices = NULL;
    // C28113: Accessing a local variable via an Interlocked function : This is an unusual usage which could be reconsidered
#pragma warning(suppress: 28113)
    _InterlockedExchange8(&bStopRequests, FALSE);

    status = NetGetNetworkDevices(NULL, &noOfDevices);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("NetGetNetworkDevices", status );
        return FALSE;
    }
    if (noOfDevices == 0)
    {
        LOG_WARNING("No network devices found!\n");
        return TRUE;
    }

    __try
    {
        pNetDevices = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(NETWORK_DEVICE_INFO) * noOfDevices, HEAP_TEST_TAG, 0);
        ASSERT(NULL != pNetDevices);

        temp = noOfDevices;
        status = NetGetNetworkDevices(pNetDevices, &temp);
        if (!SUCCEEDED(status))
        {
            LOG_FUNC_ERROR("NetGetNetworkDevices", status);
            __leave;
        }
        ASSERT(temp == noOfDevices);


        pThreads = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(PTHREAD) * noOfDevices, HEAP_TEST_TAG, 0);
        ASSERT(NULL != pThreads);

        pThreadContexts = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(NET_TRAFFIC_THREAD_CONTEXT) * noOfDevices, HEAP_TEST_TAG, 0);
        ASSERT(NULL != pThreadContexts);

        for (i = 0; i < noOfDevices; ++i)
        {
            pThreadContexts[i].NetworkDevice = pNetDevices[i].DeviceId;

            // A variable which is accessed via an Interlocked function must always be accessed via an Interlocked function
            /// there is no access to memory, SAL is just being crazy
#pragma warning(suppress: 28112)
            pThreadContexts[i].StopRequests = &bStopRequests;
            pThreadContexts[i].ResendRequests = ResendRequets;

            snprintf(threadName,
                     MAX_PATH,
                     Transmit ? "Network transmit-%02x" : "Network receive-%02x",
                     i
            );
            status = ThreadCreate(threadName,
                                  ThreadPriorityDefault,
                                  Transmit ? _TestTransmitPacketsForAdapter : _TestReceivePacketsForAdapter,
                                  &pThreadContexts[i],
                                  &pThreads[i]
            );
            ASSERT(SUCCEEDED(status));
        }

        // wait for a space key press
        while (KEY_SPACE != getch());


        LOG("Will stop receiver threads\n");

        // C28113: Accessing a local variable via an Interlocked function : This is an unusual usage which could be reconsidered
#pragma warning(suppress: 28113)
        _InterlockedExchange8(&bStopRequests, 1);

        for (i = 0; i < noOfDevices; ++i)
        {
            LOG("Waiting for thread 0x%x termination\n", ThreadGetId(pThreads[i]));
            ThreadWaitForTermination(pThreads[i], &status);
            LOG("Thread 0x%x terminated with status 0x%x\n", ThreadGetId(pThreads[i]), status);
            ASSERT(SUCCEEDED(status));
        }

        LOG("Receiver threads terminated\n");
    }
    __finally
    {
        if (NULL != pThreads)
        {
            for (i = 0; i < noOfDevices; ++i)
            {
                if (NULL != pThreads[i])
                {
                    ThreadCloseHandle(pThreads[i]);
                    pThreads[i] = NULL;
                }
            }

            ExFreePoolWithTag(pThreads, HEAP_TEST_TAG);
            pThreads = NULL;
        }

        if (NULL != pThreadContexts)
        {
            ExFreePoolWithTag(pThreadContexts, HEAP_TEST_TAG);
            pThreadContexts = NULL;
        }

        if (NULL != pNetDevices)
        {
            ExFreePoolWithTag(pNetDevices, HEAP_TEST_TAG);
            pNetDevices = NULL;
        }

        LOG_FUNC_END;
    }

    return SUCCEEDED(status);
}

STATUS
(__cdecl _TestReceivePacketsForAdapter)(
    IN_OPT      PVOID       Context
    )
{
    STATUS status;
    PNET_TRAFFIC_THREAD_CONTEXT pCtx;
    DWORD bufferSize;
    DWORD requiredBufferSize;
    PETHERNET_FRAME pFrame;

    ASSERT( NULL != Context );

    LOG_FUNC_START_THREAD;

    status = STATUS_SUCCESS;
    pCtx = (PNET_TRAFFIC_THREAD_CONTEXT) Context;
    pFrame = NULL;
    bufferSize = RECEIVE_THREAD_INITIAL_BUFFER_SIZE;
    requiredBufferSize = bufferSize;

    while (!*pCtx->StopRequests)
    {
        if (bufferSize < requiredBufferSize)
        {
            bufferSize = requiredBufferSize;
            ASSERT( NULL != pFrame );

            ExFreePoolWithTag(pFrame, HEAP_TEST_TAG);
            pFrame = NULL;
        }

        if (NULL == pFrame)
        {
            pFrame = ExAllocatePoolWithTag(PoolAllocateZeroMemory, bufferSize, HEAP_TEST_TAG, 0 );
            ASSERT( NULL != pFrame );
        }

        status = NetReceiveFrame(pCtx->NetworkDevice,
                                 pFrame,
                                 bufferSize,
                                 &requiredBufferSize
                                 );
        if (STATUS_BUFFER_TOO_SMALL == status)
        {
            status = STATUS_SUCCESS;
            LOG_WARNING("IoCallDriver failed before buffer of size %u was too small, %u bytes are required\n", bufferSize, requiredBufferSize);
            continue;
        }
        else if (STATUS_DEVICE_DISABLED == status)
        {
            status = STATUS_SUCCESS;
            LOG("Device RX has been disabled!\n");
            break;
        }
        else if (STATUS_DEVICE_NOT_CONNECTED == status)
        {
            status = STATUS_SUCCESS;
            LOG("Device link is down!\n");
            break;
        }

        ASSERT(SUCCEEDED(status));

        DumpEthernetFrame(pFrame, requiredBufferSize);

        if (pCtx->ResendRequests)
        {
            status = NetSendFrame(FALSE,
                                  pCtx->NetworkDevice,
                                  pFrame,
                                  requiredBufferSize,
                                  MAC_BROADCAST
                                  );
            if (STATUS_DEVICE_DISABLED == status)
            {
                LOG_WARNING("Could not send network frame because TX functionality is disabled! :(\n");
                status = STATUS_SUCCESS;
            }
            else if (STATUS_DEVICE_NOT_CONNECTED == status)
            {
                status = STATUS_SUCCESS;
                LOG("Device link is down!\n");
                break;
            }
            ASSERT(SUCCEEDED(status));
        }
    }

    LOGTPL("Exit status: 0x%x\n", status );
    LOG_FUNC_END_THREAD;

    return status;
}

STATUS
(__cdecl _TestTransmitPacketsForAdapter)(
    IN_OPT      PVOID       Context
    )
{
    STATUS status;
    PNET_TRAFFIC_THREAD_CONTEXT pCtx;
    PETHERNET_FRAME pFrame;
    DWORD bufferSize;
    QWORD packetIndex;

    ASSERT(NULL != Context);

    LOG_FUNC_START_THREAD;

    status = STATUS_SUCCESS;
    pCtx = (PNET_TRAFFIC_THREAD_CONTEXT)Context;
    pFrame = NULL;
    bufferSize = TRANSMIT_THREAD_BUFFER_SIZE;

    pFrame = ExAllocatePoolWithTag(PoolAllocateZeroMemory, bufferSize, HEAP_TEST_TAG, 0 );
    ASSERT( NULL != pFrame );

    pFrame->Type = htonw(ETHERNET_FRAME_TYPE_IP4);
    memcpy(pFrame->Data, BUFFER_TO_SEND, sizeof(BUFFER_TO_SEND));
    packetIndex = 0;

    while (!*pCtx->StopRequests)
    {
        memcpy(pFrame->Data + sizeof(BUFFER_TO_SEND), &packetIndex, sizeof(QWORD));
        status = NetSendFrame(FALSE,
                              pCtx->NetworkDevice,
                              pFrame,
                              bufferSize,
                              pFrame->Destination
                              );
        if (STATUS_DEVICE_DISABLED == status)
        {
            status = STATUS_SUCCESS;
            LOG("Device TX has been disabled!\n");
            break;
        }
        else if (STATUS_DEVICE_NOT_CONNECTED == status)
        {
            status = STATUS_SUCCESS;
            LOG("Device link is down!\n");
            break;
        }

        ASSERT(SUCCEEDED(status));
        packetIndex++;
    }

    if (NULL != pFrame)
    {
        ExFreePoolWithTag(pFrame, HEAP_TEST_TAG);
        pFrame = NULL;
    }

    LOGTPL("Exit status: 0x%x\n", status);
    LOG_FUNC_END_THREAD;

    return status;
}