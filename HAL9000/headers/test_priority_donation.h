#pragma once

typedef enum _TEST_PRIORITY_DONATION_MULTIPLE
{
    TestPriorityDonationMultipleOneThreadPerLock                    = 0b00,
    TestPriorityDonationMultipleOneThreadPerLockInverseRelease      = 0b01,
    TestPriorityDonationMultipleTwoThreadsPerLock                   = 0b10,
    TestPriorityDonationMultipleTwoThreadsPerLockInverseRelease     = 0b11,
} TEST_PRIORITY_DONATION_MULTIPLE;

FUNC_ThreadStart            TestThreadPriorityDonationBasic;
FUNC_ThreadStart            TestThreadPriorityDonationMultiple;
FUNC_ThreadStart            TestThreadPriorityDonationChain;