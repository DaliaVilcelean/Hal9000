#include "HAL9000.h"
#include "network.h"
#include "cmd_net_helper.h"
#include "print.h"
#include "dmp_net_device.h"
#include "test_net_stack.h"
#include "strutils.h"

#pragma warning(push)

// warning C4212: nonstandard extension used: function declaration used ellipsis
#pragma warning(disable:4212)

// warning C4029: declared formal parameter list different from definition
#pragma warning(disable:4029)

void
CmdListNetworks(
    IN          QWORD       NumberOfParameters
    )
{
    PNETWORK_DEVICE_INFO pNetDevices;
    DWORD noOfDevices;
    STATUS status;
    DWORD temp;

    ASSERT(NumberOfParameters == 0);

    pNetDevices = NULL;
    noOfDevices = 0;
    status = STATUS_SUCCESS;

    status = NetGetNetworkDevices(NULL, &noOfDevices);
    if (!SUCCEEDED(status))
    {
        perror("NetGetNetworkDevices failed with status: 0x%x\n", status);
        return;
    }

    if( 0 == noOfDevices )
    {
        pwarn("There are no network devices\n");
        return;
    }

    __try
    {
        pNetDevices = ExAllocatePoolWithTag(PoolAllocateZeroMemory, sizeof(NETWORK_DEVICE_INFO) * noOfDevices, HEAP_TEMP_TAG, 0);
        if (NULL == pNetDevices)
        {
            perror("ExAllocatePoolWithTag failed for size: 0x%x\n", sizeof(NETWORK_DEVICE_INFO) * noOfDevices);
            __leave;
        }

        temp = noOfDevices;

        status = NetGetNetworkDevices(pNetDevices, &temp);
        if (!SUCCEEDED(status))
        {
            perror("NetGetNetworkDevices failed with status: 0x%x\n", status);
            __leave;
        }
        ASSERT(temp == noOfDevices);

        for (DWORD i = 0; i < noOfDevices; ++i)
        {
            DumpNetworkDevice(&pNetDevices[i]);
            LOG("\n");
        }
    }
    __finally
    {
        if (NULL != pNetDevices)
        {
            ExFreePoolWithTag(pNetDevices, HEAP_TEMP_TAG);
            pNetDevices = NULL;
        }
    }
}

void
CmdNetRecv(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       ResendString
    )
{
    BOOLEAN bResend;

    ASSERT(0 <= NumberOfParameters && NumberOfParameters <= 1);

    if (1 == NumberOfParameters)
    {
        bResend = (0 == stricmp(ResendString, "YES"));
    }
    else
    {
        bResend = FALSE;
    }

    TestNetwork(FALSE, bResend );
}

void
CmdNetSend(
    IN          QWORD       NumberOfParameters
    )
{
    ASSERT(NumberOfParameters == 0);

    TestNetwork(TRUE, FALSE);
}

void
CmdChangeDevStatus(
    IN      QWORD       NumberOfParameters,
    IN_Z    char*       DeviceString,
    IN_Z    char*       RxEnableString,
    IN_Z    char*       TxEnableString
    )
{
    STATUS status;
    NETWORK_DEVICE_STATUS devStatus;
    DEVICE_ID devId;
    DWORD tmp;
    BOOLEAN rxEnable;
    BOOLEAN txEnable;

    ASSERT(NumberOfParameters == 3);

    atoi32(&devId, DeviceString, BASE_HEXA);

    atoi32(&tmp, RxEnableString, BASE_TEN);
    rxEnable = (BOOLEAN)tmp;

    atoi32(&tmp, TxEnableString, BASE_TEN);
    txEnable = (BOOLEAN)tmp;

    printf("Device ID: 0x%x, enable RX: %u, enable TX: %u\n",
           devId, rxEnable, txEnable);

    status = STATUS_SUCCESS;
    memzero(&devStatus, sizeof(NETWORK_DEVICE_STATUS));

    devStatus.RxEnabled = rxEnable;
    devStatus.TxEnabled = txEnable;

    status = NetChangeDeviceStatus(devId, &devStatus);
    if (!SUCCEEDED(status))
    {
        perror("NetChangeDeviceStatus failed with status: 0x%x\n", status);
        return;
    }
}

#pragma warning(pop)
