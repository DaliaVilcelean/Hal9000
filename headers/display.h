#pragma once

#define BASE_VIDEO_ADDRESS          0xB8000UL
#define SCREEN_SIZE                 (CHARS_PER_LINE*LINES_PER_SCREEN*BYTES_PER_CHAR)

// colors for printf
#define BRIGHT_WHITE_COLOR          0xF
#define YELLOW_COLOR                0xE
#define BRIGHT_MAGENTA_COLOR        0xD
#define BRIGHT_RED_COLOR            0xC
#define BRIGHT_CYAN_COLOR           0xB
#define BRIGHT_GREEN_COLOR          0xA        
#define BRIGHT_BLUE_COLOR           0x9
#define GRAY_COLOR                  0x8
#define WHITE_COLOR                 0x7
#define BROWN_COLOR                 0x6
#define MAGENTA_COLOR               0x5
#define RED_COLOR                   0x4
#define CYAN_COLOR                  0x3
#define GREEN_COLOR                 0x2
#define BLUE_COLOR                  0x1
#define BLACK_COLOR                 0x0

// screen size
#define CHARS_PER_LINE              80
#define BYTES_PER_LINE              160
#define LINES_PER_SCREEN            25

#define BYTES_PER_CHAR              2

typedef struct _SCREEN_POSITION
{
    BYTE        Line;
    BYTE        Column;
} SCREEN_POSITION, *PSCREEN_POSITION;

typedef BYTE COLOR;

void
DispPreinitScreen(
    IN      PVOID   VideoAddress,
    IN      BYTE    IndexOfFirstUsableRow,
    IN      BYTE    IndexOfFirstUnusableRow
    );

void
DispClearScreen(
    void
    );

void
DispSetColor(
    IN      COLOR           Color
    );

COLOR
DispGetColor(
    void
    );


//******************************************************************************
// Function:      DispPrintString
// Description: Prints a buffer to the screen
// Returns:       void
// Parameter:     IN char* Buffer - NULL terminated buffer
// Parameter:     IN BYTE Color   - color to use
// NOTE:        Not MP safe.
//******************************************************************************
void
DispPrintString(
    IN_Z    char*       Buffer
    );

void
DispPutBufferColor(
    IN_Z    char*       Buffer,
    IN      BYTE        Line,
    IN      BYTE        Column,
    IN      COLOR       Color
    );

void
DispClearLine(
    IN      BYTE        Line
    );

void
DispSetCursor(
    IN      SCREEN_POSITION     CursorPosition,
    IN      COLOR               Color
    );

STATUS
DispStoreBuffer(
    OUT_WRITES_BYTES(Size)  PVOID               Buffer,
    IN                      DWORD               Size
    );

STATUS
DispRestoreBuffer(
    IN_READS_BYTES(Size)    PVOID               Buffer,
    IN                      DWORD               Size
    );