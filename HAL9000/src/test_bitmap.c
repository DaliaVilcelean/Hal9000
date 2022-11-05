#include "test_common.h"
#include "test_bitmap.h"
#include "bitmap.h"

static const DWORD TST_BITMAP_NO_OF_ELEMENTS_TO_TEST_FOR[] = 
{ 
    BITS_FOR_STRUCTURE(BYTE), 
    BITS_FOR_STRUCTURE(BYTE) * 10, 
    BITS_FOR_STRUCTURE(BYTE) * 10 + 1, 
    BITS_FOR_STRUCTURE(BYTE) * 10 + 31, 
    212, 
    1, 
    491230 
};
static const DWORD NO_OF_ELEMENTS_TSTS = ARRAYSIZE(TST_BITMAP_NO_OF_ELEMENTS_TO_TEST_FOR);

static
void
_TestBitmapForElements(
    IN      DWORD       NumberOfElements
    );

void
TestBitmap(
    void
    )
{
    DWORD i;

    for (i = 0; i < NO_OF_ELEMENTS_TSTS; ++i)
    {
        LOGL("[%d]Will test bitmap for %d elements\n", i, TST_BITMAP_NO_OF_ELEMENTS_TO_TEST_FOR[i] );
        _TestBitmapForElements(TST_BITMAP_NO_OF_ELEMENTS_TO_TEST_FOR[i]);
    }
}

static
void
_TestBitmapForElements(
    IN      DWORD       NumberOfElements
    )
{
    BITMAP tstBitmap;
    DWORD sizeOfStruct;
    DWORD expectedSizeOfStruct;
    PBYTE pBitmapBuffer;
    STATUS status;
    DWORD bitmapIndex;

    LOG_FUNC_START;

    ASSERT( 0 != NumberOfElements );

    memzero(&tstBitmap, sizeof(BITMAP));
    sizeOfStruct = 0;
    expectedSizeOfStruct = ( AlignAddressUpper(NumberOfElements, BITS_FOR_STRUCTURE(BYTE)) / BITS_FOR_STRUCTURE(BYTE) ) * sizeof(BYTE);
    status = STATUS_SUCCESS;
    bitmapIndex = MAX_DWORD;

    sizeOfStruct = BitmapPreinit(&tstBitmap, NumberOfElements );
    ASSERT_INFO( sizeOfStruct == expectedSizeOfStruct,
                                  "Size of bitmap buffer should be %d bytes, but it is %d bytes", expectedSizeOfStruct, sizeOfStruct );
    ASSERT( 0 == sizeOfStruct % sizeof(BYTE));
    LOGL("Buffer required: %d bytes\n", sizeOfStruct );

    pBitmapBuffer = ExAllocatePoolWithTag(PoolAllocatePanicIfFail, sizeOfStruct, HEAP_TEST_TAG, 0 );

    BitmapInit(&tstBitmap, pBitmapBuffer );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO(MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex);

    bitmapIndex = BitmapScan(&tstBitmap, 1, FALSE);
    ASSERT_INFO(0 == bitmapIndex, "Index: %d", bitmapIndex);

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, FALSE );
    ASSERT_INFO( 0 == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements + 1, FALSE );
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScanAndFlip(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScanAndFlip(&tstBitmap, NumberOfElements, FALSE);
    ASSERT_INFO(0 == bitmapIndex, "Index: %d", bitmapIndex);

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, FALSE);
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO( 0 == bitmapIndex, "Index: %d", bitmapIndex );

    BitmapClearBits(&tstBitmap, 0, NumberOfElements );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, FALSE);
    ASSERT_INFO( 0 == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    BitmapSetBits(&tstBitmap, 0, NumberOfElements );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, FALSE);
    ASSERT_INFO( MAX_DWORD == bitmapIndex, "Index: %d", bitmapIndex );

    bitmapIndex = BitmapScan(&tstBitmap, NumberOfElements, TRUE);
    ASSERT_INFO( 0 == bitmapIndex, "Index: %d", bitmapIndex );

    ExFreePoolWithTag(pBitmapBuffer, HEAP_TEST_TAG);

    BitmapUninit(&tstBitmap);

    LOG_FUNC_END;
}