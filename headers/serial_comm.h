#pragma once

STATUS
SerialCommunicationInitialize(
    IN_READS(NoOfPorts)     WORD*           Ports,
    IN                      DWORD           NoOfPorts
    );

STATUS
SerialCommunicationReinitialize(
    void
    );

void
SerialCommWriteBuffer(
    IN_Z char*  Buffer
    );