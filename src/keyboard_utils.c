#include "HAL9000.h"
#include "keyboard.h"
#include "keyboard_utils.h"
#include "display.h"

#define CMD_SHELL       ">>"
#define CMD_SHELL_SIZE  (sizeof(CMD_SHELL))

#define CMD_HISTORY_MAX_SIZE    16

static char m_CmdHistory[CMD_HISTORY_MAX_SIZE][CHARS_PER_LINE] = { 0 };

static DWORD m_CmdHistoryFirstIndex = 0;
static DWORD m_CmdHistorySize = 0;

KEYCODE
getch(
    void
)
{
    KEYCODE result = KEY_UNKNOWN;

    do
    {
        result = KeyboardWaitForKey();
        KeyboardDiscardLastKey();
    } while (KEY_UNKNOWN == result);



    return result;
}

void
gets_s(
    OUT_WRITES_Z(BufferSize)    char*       Buffer,
    IN                          DWORD       BufferSize,
    OUT                         DWORD*      UsedSize
)
{
    DWORD i;
    int j;
    DWORD maxBufferSize;
    KEYCODE key;
    char c;
    SCREEN_POSITION cursorPosition;
    DWORD cmdHistoryIndex;

    i = 0;
    j = 0;
    key = KEY_UNKNOWN;
    c = 0;
    cmdHistoryIndex = MAX_DWORD;

    cursorPosition.Line = LINES_PER_SCREEN - 1;
    cursorPosition.Column = CMD_SHELL_SIZE;

    ASSERT(NULL != UsedSize);
    ASSERT(NULL != Buffer);
    ASSERT(BufferSize > 1);

    // we cannot write more than a line or then the buffer we have
    // we add sizeof('\0') because we do not need to print the NULL terminator :)
    maxBufferSize = min(BufferSize, CHARS_PER_LINE - CMD_SHELL_SIZE + 1);

    // zero the buffer
    memzero(Buffer, BufferSize);

#pragma warning(suppress:4127)
    while (TRUE)
    {
        DispClearLine(LINES_PER_SCREEN - 1);
        DispSetCursor(cursorPosition, CYAN_COLOR);

        // display command shell
        DispPutBufferColor(CMD_SHELL, LINES_PER_SCREEN - 1, 0, BLUE_COLOR);

        // warning 6054: String 'Buffer' might not be zero-terminated
        // the memzero call outside the while loop zero-terminates the string
#pragma warning(suppress: 6054)
        DispPutBufferColor(Buffer, LINES_PER_SCREEN - 1, CMD_SHELL_SIZE, BRIGHT_CYAN_COLOR);

        key = getch();
        ASSERT_INFO(KEY_UNKNOWN != key, "getch can't return when there is no valid key\n");

        if (KEY_RETURN == key)
        {
            // enter was pressed

            if (0 == strlen_s(Buffer, maxBufferSize))
            {
                break;
            }

            if (CMD_HISTORY_MAX_SIZE == m_CmdHistorySize)
            {
                m_CmdHistoryFirstIndex = (m_CmdHistoryFirstIndex + 1) % CMD_HISTORY_MAX_SIZE;
            }
            else
            {
                m_CmdHistorySize++;
            }

            strtrim(Buffer);

            strncpy(m_CmdHistory[(m_CmdHistoryFirstIndex + m_CmdHistorySize - 1) % CMD_HISTORY_MAX_SIZE], Buffer, maxBufferSize - 1);

            break;
        }

        if (KEY_BACKSPACE == key)
        {
            // delete a key
            // if i is already 0 nothing to delete
            if (0 != i)
            {
                // shift left all the characters in the Buffer after the current position
                for (j = (int)i; j <= (int)strlen_s(Buffer, maxBufferSize - 1); j++)
                {
                    Buffer[j - 1] = Buffer[j];
                }

                cursorPosition.Column--;
                --i;
            }

            // go to the next iteration
            continue;
        }

        if (KEY_DELETE == key)
        {
            /// TODO: implement
            continue;
        }

        if (KEY_UP == key)
        {
            if (0 == m_CmdHistorySize)  // nothing in history
            {
                continue;
            }

            if (MAX_DWORD == cmdHistoryIndex)
            {
                cmdHistoryIndex = (m_CmdHistoryFirstIndex + m_CmdHistorySize - 1) % CMD_HISTORY_MAX_SIZE;
            }
            else if (cmdHistoryIndex != m_CmdHistoryFirstIndex) // not yet at first command
            {
                cmdHistoryIndex = (cmdHistoryIndex - 1) % CMD_HISTORY_MAX_SIZE;
            }

            strncpy(Buffer, m_CmdHistory[cmdHistoryIndex], maxBufferSize - 1);

            i = strlen_s(Buffer, maxBufferSize);
            cursorPosition.Column = CMD_SHELL_SIZE + (BYTE)i;

            // go to the next iteration
            continue;
        }

        if (KEY_DOWN == key)
        {
            if (0 == m_CmdHistorySize)  // nothing in history
            {
                continue;
            }

            if (MAX_DWORD == cmdHistoryIndex)
            {
                continue;
            }

            if (cmdHistoryIndex != (m_CmdHistoryFirstIndex + m_CmdHistorySize - 1) % CMD_HISTORY_MAX_SIZE) // not yet at last command
            {
                cmdHistoryIndex = (cmdHistoryIndex + 1) % CMD_HISTORY_MAX_SIZE;
            }

            strncpy(Buffer, m_CmdHistory[cmdHistoryIndex], maxBufferSize - 1);

            i = strlen_s(Buffer, maxBufferSize);
            cursorPosition.Column = CMD_SHELL_SIZE + (BYTE)i;

            // go to the next iteration
            continue;
        }

        if (KEY_LEFT == key)
        {
            // move cursor to the left
            if (0 != i)
            {
                // if i is already 0 we have nowhere to go
                cursorPosition.Column--;
                --i;
            }

            // go to the next iteration
            continue;
        }

        if (KEY_RIGHT == key)
        {
            // move cursor to the right
            if (i < maxBufferSize - 2)
            {
                // we use -2 because if we move the cursor we must
                // be able to write a character afterward

                // [BUFFER_SIZE - 2][CHAR AFTER CURSOR MOVEMENT][\0]

                // move cursor only if there are characters to the right
                if ('\0' != Buffer[i])
                {
                    cursorPosition.Column++;
                    ++i;
                }
            }

            // go to the next iteration
            continue;
        }

        if (KEY_HOME == key)
        {
            if (0 != i)
            {
                cursorPosition.Column -= (BYTE)i;
                i = 0;
            }

            continue;
        }

        if (KEY_END == key)
        {
            i = strlen_s(Buffer, maxBufferSize);
            cursorPosition.Column = CMD_SHELL_SIZE + (BYTE)i;

            continue;
        }

        if (KEY_TAB == key)
        {
            /// TODO: implement
            continue;
        }

        // if we're here we might have an ASCII character
        c = KeyboardKeyToAscii(key);
        if (0 != c)
        {
            // we have an ASCII character

            // shift right all the characters in the Buffer after the current position
            for (j = strlen_s(Buffer, maxBufferSize - 1); j >= (int)i; j--)
            {
                Buffer[j + 1] = Buffer[j];
            }

            Buffer[i] = c;
            ++i;
            cursorPosition.Column++;
        }

        // we use BufferSize - 1 because we append a NULL terminator
        if (i >= maxBufferSize - 1)
        {
            // we cannot write anymore in the buffer
            break;
        }
    }

    *UsedSize = i;
}
