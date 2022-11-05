#include "HAL9000.h"
#include "serial_comm.h"
#include "serial.h"

static WORD m_serialPortNumber = 0;

STATUS
SerialCommunicationInitialize(
    IN_READS(NoOfPorts)     WORD*           Ports,
    IN                      DWORD           NoOfPorts
    )
{
    WORD currentPortAddress;
    DWORD i;
    
    currentPortAddress = 0;
    
    if (0 != m_serialPortNumber)
    {
         // return an error warning
         return STATUS_COMM_SERIAL_ALREADY_INITIALIZED;
    }

    for (i = 0; i < NoOfPorts; ++i)
    {
        currentPortAddress = Ports[i];
        if (0 != currentPortAddress)
        {
            break;
        }
    }

    if (0 == currentPortAddress)
    {
        return STATUS_COMM_SERIAL_NO_PORTS_AVAILABLE;
    }
    
    SerialInitialize(currentPortAddress);
    m_serialPortNumber = currentPortAddress;

    return STATUS_SUCCESS;
}

STATUS
SerialCommunicationReinitialize(
    void
    )
{
    if (0 == m_serialPortNumber)
    {
        return STATUS_COMM_SERIAL_NOT_INITIALIZED;
    }

    SerialInitialize(m_serialPortNumber);

    return STATUS_SUCCESS;
}

void
SerialCommWriteBuffer(
    IN_Z char*  Buffer
    )
{
    if (0 == m_serialPortNumber)
    {
        return;
    }

    ASSERT( NULL != Buffer );

    SerialWriteBuffer(m_serialPortNumber, Buffer);
}