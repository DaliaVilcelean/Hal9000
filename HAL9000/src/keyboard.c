#include "HAL9000.h"
#include "keyboard.h"
#include "io.h"
#include "strutils.h"
#include "ex_event.h"

#define KBD_ENCODER_INPUT_PORT         0x60
#define KBD_ENCODER_COMMAND_PORT       0x60

#define KBD_CONTROLLER_SREG_PORT       0x64
#define KBD_CONTROLLER_COMMAND_PORT    0x64

#define KBD_SREG_OUTPUT_BUFFER_FULL     (1<<0)
#define KBD_SREG_INPUT_BUFFER_FULL      (1<<1)

// keyboard controller commands
typedef enum _KEYBOARD_CTRL_COMMAND
{
    KeyboardCtrlCommandReadCommand      = 0x20,
    KeyboardCtrlCommandWriteCommand     = 0x60,
    KeyboardCtrlCommandSelfTest         = 0xAA,
    KeyboardCtrlCommandInterfaceTest    = 0xAB,
    KeyboardCtrlCommandDisableKbd       = 0xAD,
    KeyboardCtrlCommandEnableKbd        = 0xAE,
    KeyboardCtrlCommandSystemReset      = 0xFE
} KEYBOARD_CTRL_COMMAND;

// keyboard encoder commands
typedef enum _KEYBOARD_ENC_COMMAND
{
    KeyboardEncCommandSetLEDs           = 0xED
} KEYBOARD_ENC_COMMAND;

#define KBD_ENC_LED_SCROLL_LOCK_BIT     0
#define KBD_ENC_LED_NUM_LOCK_BIT        1
#define KBD_ENC_LED_CAPS_LOCK_BIT       2

//! original xt scan code set. Array index==make code
static WORD _kkybrd_scancode_std[] = {

    //! key             scancode
    KEY_UNKNOWN,        //0
    KEY_ESCAPE,         //1
    KEY_1,              //2
    KEY_2,              //3
    KEY_3,              //4
    KEY_4,              //5
    KEY_5,              //6
    KEY_6,              //7
    KEY_7,              //8
    KEY_8,              //9
    KEY_9,              //0xa
    KEY_0,              //0xb
    KEY_MINUS,          //0xc
    KEY_EQUAL,          //0xd
    KEY_BACKSPACE,      //0xe
    KEY_TAB,            //0xf
    KEY_Q,              //0x10
    KEY_W,              //0x11
    KEY_E,              //0x12
    KEY_R,              //0x13
    KEY_T,              //0x14
    KEY_Y,              //0x15
    KEY_U,              //0x16
    KEY_I,              //0x17
    KEY_O,              //0x18
    KEY_P,              //0x19
    KEY_LEFTBRACKET,    //0x1a
    KEY_RIGHTBRACKET,   //0x1b
    KEY_RETURN,         //0x1c
    KEY_LCTRL,          //0x1d
    KEY_A,              //0x1e
    KEY_S,              //0x1f
    KEY_D,              //0x20
    KEY_F,              //0x21
    KEY_G,              //0x22
    KEY_H,              //0x23
    KEY_J,              //0x24
    KEY_K,              //0x25
    KEY_L,              //0x26
    KEY_SEMICOLON,      //0x27
    KEY_QUOTE,          //0x28
    KEY_GRAVE,          //0x29
    KEY_LSHIFT,         //0x2a
    KEY_BACKSLASH,      //0x2b
    KEY_Z,              //0x2c
    KEY_X,              //0x2d
    KEY_C,              //0x2e
    KEY_V,              //0x2f
    KEY_B,              //0x30
    KEY_N,              //0x31
    KEY_M,              //0x32
    KEY_COMMA,          //0x33
    KEY_DOT,            //0x34
    KEY_SLASH,          //0x35
    KEY_RSHIFT,         //0x36
    KEY_KP_ASTERISK,    //0x37
    KEY_RALT,           //0x38
    KEY_SPACE,          //0x39
    KEY_CAPSLOCK,       //0x3a
    KEY_F1,             //0x3b
    KEY_F2,             //0x3c
    KEY_F3,             //0x3d
    KEY_F4,             //0x3e
    KEY_F5,             //0x3f
    KEY_F6,             //0x40
    KEY_F7,             //0x41
    KEY_F8,             //0x42
    KEY_F9,             //0x43
    KEY_F10,            //0x44
    KEY_KP_NUMLOCK,     //0x45
    KEY_SCROLLLOCK,     //0x46
    KEY_KP_7,           //0x47
    KEY_KP_8,           //0x48
    KEY_KP_9,           //0x49
    KEY_KP_MINUS,       //0x4a
    KEY_KP_4,           //0x4b
    KEY_KP_5,           //0x4c
    KEY_KP_6,           //0x4d
    KEY_KP_PLUS,        //0x4e
    KEY_KP_1,           //0x4f
    KEY_KP_2,           //0x50    //keypad down arrow
    KEY_KP_3,           //0x51    //keypad page down
    KEY_KP_0,           //0x52    //keypad insert key
    KEY_KP_DECIMAL,     //0x53    //keypad delete key
    KEY_UNKNOWN,        //0x54
    KEY_UNKNOWN,        //0x55
    KEY_UNKNOWN,        //0x56
    KEY_F11,            //0x57
    KEY_F12             //0x58
};

static WORD _kkybrd_scancode_ext[] = {

    //! key            scancode
    KEY_UNKNOWN,    //0
    KEY_UNKNOWN,        //1
    KEY_UNKNOWN,            //2
    KEY_UNKNOWN,            //3
    KEY_UNKNOWN,            //4
    KEY_UNKNOWN,            //5
    KEY_UNKNOWN,            //6
    KEY_UNKNOWN,            //7
    KEY_UNKNOWN,            //8
    KEY_UNKNOWN,            //9
    KEY_UNKNOWN,            //0xa
    KEY_UNKNOWN,            //0xb
    KEY_UNKNOWN,        //0xc
    KEY_UNKNOWN,        //0xd
    KEY_UNKNOWN,    //0xe
    KEY_UNKNOWN,        //0xf
    KEY_UNKNOWN,            //0x10
    KEY_UNKNOWN,            //0x11
    KEY_UNKNOWN,            //0x12
    KEY_UNKNOWN,            //0x13
    KEY_UNKNOWN,            //0x14
    KEY_UNKNOWN,            //0x15
    KEY_UNKNOWN,            //0x16
    KEY_UNKNOWN,            //0x17
    KEY_UNKNOWN,            //0x18
    KEY_UNKNOWN,            //0x19
    KEY_UNKNOWN,//0x1a
    KEY_UNKNOWN,//0x1b
    KEY_KP_ENTER,        //0x1c
    KEY_UNKNOWN,        //0x1d
    KEY_UNKNOWN,            //0x1e
    KEY_UNKNOWN,            //0x1f
    KEY_UNKNOWN,            //0x20
    KEY_UNKNOWN,            //0x21
    KEY_UNKNOWN,            //0x22
    KEY_UNKNOWN,            //0x23
    KEY_UNKNOWN,            //0x24
    KEY_UNKNOWN,            //0x25
    KEY_UNKNOWN,            //0x26
    KEY_UNKNOWN,    //0x27
    KEY_UNKNOWN,        //0x28
    KEY_UNKNOWN,        //0x29
    KEY_UNKNOWN,        //0x2a
    KEY_UNKNOWN,    //0x2b
    KEY_UNKNOWN,            //0x2c
    KEY_UNKNOWN,            //0x2d
    KEY_UNKNOWN,            //0x2e
    KEY_UNKNOWN,            //0x2f
    KEY_UNKNOWN,            //0x30
    KEY_UNKNOWN,            //0x31
    KEY_UNKNOWN,            //0x32
    KEY_UNKNOWN,        //0x33
    KEY_UNKNOWN,        //0x34
    KEY_UNKNOWN,        //0x35
    KEY_UNKNOWN,        //0x36
    KEY_UNKNOWN,//0x37
    KEY_UNKNOWN,        //0x38
    KEY_UNKNOWN,        //0x39
    KEY_UNKNOWN,    //0x3a
    KEY_UNKNOWN,            //0x3b
    KEY_UNKNOWN,            //0x3c
    KEY_UNKNOWN,            //0x3d
    KEY_UNKNOWN,            //0x3e
    KEY_UNKNOWN,            //0x3f
    KEY_UNKNOWN,            //0x40
    KEY_UNKNOWN,            //0x41
    KEY_UNKNOWN,            //0x42
    KEY_UNKNOWN,            //0x43
    KEY_UNKNOWN,        //0x44
    KEY_UNKNOWN,    //0x45
    KEY_UNKNOWN,    //0x46
    KEY_HOME,       //0x47
    KEY_UP,       //0x48
    KEY_UNKNOWN,       //0x49
    KEY_UNKNOWN,   //0x4a
    KEY_LEFT,       //0x4b
    KEY_UNKNOWN,       //0x4c
    KEY_RIGHT,       //0x4d
    KEY_UNKNOWN,    //0x4e
    KEY_END,       //0x4f
    KEY_DOWN,        //0x50    //keypad down arrow
    KEY_UNKNOWN,        //0x51    //keypad page down
    KEY_UNKNOWN,        //0x52    //keypad insert key
    KEY_DELETE,    //0x53    //keypad delete key
    KEY_UNKNOWN,    //0x54
    KEY_UNKNOWN,    //0x55
    KEY_UNKNOWN,    //0x56
    KEY_UNKNOWN,        //0x57
    KEY_UNKNOWN//0x58
};

#define KEY_BREAK               (1<<7)

#define KEY_INVALID_SCANCODE    0x0

typedef struct _KEYBOARD_DATA
{
    KEYCODE             Keycode;

    // LEDs
    BOOLEAN             NumLock;
    BOOLEAN             ScrollLock;
    BOOLEAN             CapsLock;

    // Modifiers
    BOOLEAN             Shift;
    BOOLEAN             Alt;
    BOOLEAN             Ctrl;

    // Basic Assurance Test
    BOOLEAN             BatSucceeded;
    BOOLEAN             KeyboardInitialized;

    // if signaled => key was pressed
    // and it wasn't discarded yet
    EX_EVENT            KeyPressedEvt;
} KEYBOARD_DATA, *PKEYBOARD_DATA;

static KEYBOARD_DATA m_keyboardData = { 0 };

static FUNC_InterruptFunction  _KeyboardIsr;

static
void
_KeyboardCtrlSendCommand(
    IN      BYTE        Command
);

static
void
_KeyboardEncSendCommand(
    IN      BYTE        Command
);

static
void
_KeyboardUpdateLEDs(
    void
);

static
__forceinline
BYTE
_KeyboardCtrlReadStatus(
    void
)
{
    return __inbyte(KBD_CONTROLLER_SREG_PORT);
}

static
__forceinline
BYTE
_KeyboardEncReadBuffer(
    void
)
{
    return __inbyte(KBD_ENCODER_INPUT_PORT);
}

static
__forceinline
void
_KeyboardEnableKbd(
    void
)
{
    _KeyboardCtrlSendCommand(KeyboardCtrlCommandEnableKbd);
}

STATUS
KeyboardInitialize(
    IN      BYTE        InterruptIrq
)
{
    STATUS status;
    IO_INTERRUPT ioInterrupt;

    status = STATUS_SUCCESS;
    memzero(&ioInterrupt, sizeof(IO_INTERRUPT));

    // check to see if keyboard wasn't already initialized
    if (m_keyboardData.KeyboardInitialized)
    {
        // return already a HINT, not an error
        return STATUS_ALREADY_INITIALIZED_HINT;
    }

    m_keyboardData.Keycode = KEY_UNKNOWN;

    // make sure keyboard is enabled
    _KeyboardEnableKbd();

    /// TODO: do a BAT test (basic assurance test)

    // set LEDs status
    m_keyboardData.CapsLock = m_keyboardData.ScrollLock = m_keyboardData.NumLock = FALSE;
    _KeyboardUpdateLEDs();

    // set shift, ctrl and alt keys status
    m_keyboardData.Ctrl = m_keyboardData.Shift = m_keyboardData.Alt = FALSE;

    // create event
    status = ExEventInit(&m_keyboardData.KeyPressedEvt, ExEventTypeNotification, FALSE);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("ExEventInit", status);
        return status;
    }

    // install ISR
    ioInterrupt.Type = IoInterruptTypeLegacy;
    ioInterrupt.Irql = IrqlUserInputLevel;
    ioInterrupt.ServiceRoutine = _KeyboardIsr;
    ioInterrupt.Exclusive = TRUE;
    ioInterrupt.Legacy.Irq = InterruptIrq;

    status = IoRegisterInterrupt(&ioInterrupt, NULL);
    if (!SUCCEEDED(status))
    {
        LOG_FUNC_ERROR("IoRegisterInterrupt", status);
        return status;
    }

    // keyboard initialized
    m_keyboardData.KeyboardInitialized = TRUE;

    return status;
}

_Success_(KEY_UNKNOWN != return)
KEYCODE
KeyboardGetLastKey(
    void
)
{
    return m_keyboardData.Keycode;
}

_Success_(KEY_UNKNOWN != return)
KEYCODE
KeyboardWaitForKey(
    void
)
{
    ExEventWaitForSignal(&m_keyboardData.KeyPressedEvt);
    return m_keyboardData.Keycode;
}

void
KeyboardDiscardLastKey(
    void
)
{
    ExEventClearSignal(&m_keyboardData.KeyPressedEvt);
    m_keyboardData.Keycode = KEY_UNKNOWN;
}

char
KeyboardKeyToAscii(
    IN      KEYCODE     KeyCode
)
{
    char result = KeyCode;

    if (KEY_UNKNOWN == KeyCode || !isascii(KeyCode))
    {
        // we have nothing to return, this is not a valid ASCII
        // character
        return 0;
    }

    // if shift key is down xor caps lock is on, make the key uppercase
    // we certainly have small letter
    // if SHIFT on => uppercase
    // if CAPS on => uppercase
    // if BOTH shift and CAPS => it remains lowercase
    if (m_keyboardData.Shift ^ m_keyboardData.CapsLock)
    {
        if (result >= 'a' && result <= 'z')
        {
            result = result - ('a' - 'A');
        }
    }

    if (m_keyboardData.Shift)
    {
        if (result >= '0' && result <= '9')
        {
            switch (result)
            {

            case '0':
                result = KEY_RIGHTPARENTHESIS;
                break;
            case '1':
                result = KEY_EXCLAMATION;
                break;
            case '2':
                result = KEY_AT;
                break;
            case '3':
                result = KEY_EXCLAMATION;
                break;
            case '4':
                result = KEY_HASH;
                break;
            case '5':
                result = KEY_PERCENT;
                break;
            case '6':
                result = KEY_CARRET;
                break;
            case '7':
                result = KEY_AMPERSAND;
                break;
            case '8':
                result = KEY_ASTERISK;
                break;
            case '9':
                result = KEY_LEFTPARENTHESIS;
                break;
            }
        }
        else
        {

            switch (result)
            {
            case KEY_COMMA:
                result = KEY_LESS;
                break;

            case KEY_DOT:
                result = KEY_GREATER;
                break;

            case KEY_SLASH:
                result = KEY_QUESTION;
                break;

            case KEY_SEMICOLON:
                result = KEY_COLON;
                break;

            case KEY_QUOTE:
                result = KEY_QUOTEDOUBLE;
                break;

            case KEY_LEFTBRACKET:
                result = KEY_LEFTCURL;
                break;

            case KEY_RIGHTBRACKET:
                result = KEY_RIGHTCURL;
                break;

            case KEY_GRAVE:
                result = KEY_TILDE;
                break;

            case KEY_MINUS:
                result = KEY_UNDERSCORE;
                break;

            case KEY_PLUS:
                result = KEY_EQUAL;
                break;

            case KEY_BACKSLASH:
                result = KEY_BAR;
                break;
            }
        }
    }

    return result;
}

void
KeyboardResetSystem(
    void
)
{
    _KeyboardCtrlSendCommand(KeyboardCtrlCommandSystemReset);
}

static
void
_KeyboardCtrlSendCommand(
    IN      BYTE        Command
)
{
    // we can't send any commands while the keyboard buffer is full
    while (IsBooleanFlagOn(_KeyboardCtrlReadStatus(), KBD_SREG_INPUT_BUFFER_FULL));

    __outbyte(KBD_CONTROLLER_COMMAND_PORT, Command);
}

static
void
_KeyboardEncSendCommand(
    IN      BYTE        Command
)
{
    // because the commands sent to the encoder go through the controller
    // we can't send any commands while the keyboard buffer is full
    while (IsBooleanFlagOn(_KeyboardCtrlReadStatus(), KBD_SREG_INPUT_BUFFER_FULL));

    __outbyte(KBD_ENCODER_COMMAND_PORT, Command);
}

static
void
_KeyboardUpdateLEDs(
    void
)
{
    BYTE data = 0;

    // set appriate bits
    data = data | (m_keyboardData.ScrollLock << KBD_ENC_LED_SCROLL_LOCK_BIT);
    data = data | (m_keyboardData.NumLock << KBD_ENC_LED_NUM_LOCK_BIT);
    data = data | (m_keyboardData.CapsLock << KBD_ENC_LED_CAPS_LOCK_BIT);

    LOG("Will update LEDs with data: 0x%x\n", data);

    // send the command -- update (LEDs)
    _KeyboardEncSendCommand(KeyboardEncCommandSetLEDs);
    _KeyboardEncSendCommand(data);
}

static
BOOLEAN
(__cdecl _KeyboardIsr)(
    IN      PDEVICE_OBJECT           Device
    )
{
    BYTE code;
    WORD key;
    BOOLEAN updateLEDs;
    BOOLEAN keyBreak;
    BYTE kbdStatus;

    static BOOLEAN _extendedCode = FALSE;

    ASSERT(NULL != Device);

    kbdStatus = _KeyboardCtrlReadStatus();

    ASSERT_INFO(IsBooleanFlagOn(kbdStatus, KBD_SREG_OUTPUT_BUFFER_FULL),
        "How did we get interrupt if there is nothing in the buffer??");

    code = _KeyboardEncReadBuffer();
    key = 0;
    updateLEDs = FALSE;
    keyBreak = IsBooleanFlagOn(code, KEY_BREAK);

    __try
    {
        if (0xE0 == code || 0xE1 == code)
        {
            _extendedCode = TRUE;
            __leave;
        }

        if (keyBreak)
        {
            // this is a key break
            // covert the break code into its make code equivalent
            code -= 0x80;
        }

        if (_extendedCode)
        {
            key = _kkybrd_scancode_ext[code];
        }
        else
        {
            // grab the key
            key = _kkybrd_scancode_std[code];
        }

        if (keyBreak)
        {
            // test if a special key has been released & set it
            switch (key)
            {
            case KEY_LCTRL:
            case KEY_RCTRL:
                m_keyboardData.Ctrl = FALSE;
                break;

            case KEY_LSHIFT:
            case KEY_RSHIFT:
                m_keyboardData.Shift = FALSE;
                break;
            case KEY_LALT:
            case KEY_RALT:
                m_keyboardData.Alt = FALSE;
                break;
            }
        }
        else
        {
            // this is a make - update current scan code
            m_keyboardData.Keycode = key;

            // test if user is holding down any special keys & set it
            switch (key)
            {

            case KEY_LCTRL:
            case KEY_RCTRL:
                m_keyboardData.Ctrl = TRUE;
                break;

            case KEY_LSHIFT:
            case KEY_RSHIFT:
                m_keyboardData.Shift = TRUE;
                break;

            case KEY_LALT:
            case KEY_RALT:
                m_keyboardData.Alt = TRUE;
                break;
            case KEY_CAPSLOCK:
                m_keyboardData.CapsLock = !m_keyboardData.CapsLock;
                updateLEDs = TRUE;
                break;
            case KEY_KP_NUMLOCK:
                m_keyboardData.NumLock = !m_keyboardData.NumLock;
                updateLEDs = TRUE;
                break;
            case KEY_SCROLLLOCK:
                m_keyboardData.ScrollLock = !m_keyboardData.ScrollLock;
                updateLEDs = TRUE;
                break;
            }

            if (updateLEDs)
            {
                _KeyboardUpdateLEDs();
            }
        }

        // if we get here it's not an extended code
        _extendedCode = FALSE;

        // if we're here => we had a valid key press
        ExEventSignal(&m_keyboardData.KeyPressedEvt);
    }
    __finally
    {

    }

    return TRUE;
}
