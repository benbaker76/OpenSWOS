/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/*
 * input_text.c — PrintFormatted + InputText family
 * Ported from SWOS2.s (Chunk 36)
 */

#include "input_text.h"
#include "menu_system.h"
#include "string_utils.h"
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>
#endif

/* The web page (wasm/shell.html) is told a text field is open, and puts up a phone's on-screen
 * keyboard (its hidden field; what is typed comes back through InputSDL_WebChar). */
static void web_text_field(int on)
{
#ifdef SWOS_WASM_BUILD
    EM_ASM({ if (Module.swosTextInput) Module.swosTextInput($0); }, on);
#else
    (void)on;
#endif
}

/* ---- External globals ---- */
extern volatile uint16_t _panicReasonCode;
extern uint8_t _inMenuKeyboardBuffer[100];
extern uint16_t _currentKey;
extern volatile uint16_t _shortFire;
extern uint16_t _deadVar02;
extern uint16_t _deadInputTextVar;
extern uint16_t _allowExtraCharsFlag;
extern uint8_t _inputAllowedControlKeys[];

/* ---- External functions ---- */
extern void PanicHaltLoop(void);
extern void MenuCheckControls(void);
extern void CallDrawMenuItem(MenuEntry *entry);
extern void DrawMenuItemBackgroundMasked(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Redraw the entry being edited. An entry with a background clears its own box
 * (DrawMenuItem fills it first), and every text input in SWOS itself has one. The
 * SWOS 2020 menus have text inputs WITHOUT one -- the log-in screen's NAME and
 * SERVER boxes -- because that game redraws the whole menu every frame; drawn over
 * the old text, a shortened string left the longer one showing through it. So put
 * the menu picture back under the box first, the way a transparent entry is
 * cleared. */
static void inputTextRedraw(MenuEntry *entry)
{
    if (entry->background == NO_BACKGROUND)
        DrawMenuItemBackgroundMasked(entry->x, entry->y, entry->dwWidth, entry->dwHeight);
    CallDrawMenuItem(entry);
}

/* ============================================================
 * PrintFormatted
 * Format string printer: %0..%6 = decimal numbers, %a/%b/%c = strings.
 * Uses goto-based fall-through to match 68k digit emission behavior
 * (once we enter at a digit level, all lower digits are always emitted).
 * ============================================================ */
__attribute__((used, externally_visible))
void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC)
{
    for (;;) {
        /* Copy bytes until '%' or NUL */
        while (*fmt != '%') {
            uint8_t c = *fmt++;
            *out++ = c;
            if (c == 0) {
                return;
            }
        }

        /* Skip '%' and read the specifier character */
        fmt++;
        uint8_t spec = *fmt++;

        /* String substitution: %a, %b, %c */
        if (spec == 'a' || spec == 'b' || spec == 'c') {
            const uint8_t *src;
            if (spec == 'a')
                src = strA;
            else if (spec == 'b')
                src = strB;
            else
                src = strC;

            while (*src != 0) {
                *out++ = *src++;
            }
            continue;
        }

        /* Numeric substitution: %0..%6 */
        uint16_t val;
        if (spec == '0')      val = n0;
        else if (spec == '1') val = n1;
        else if (spec == '2') val = n2;
        else if (spec == '3') val = n3;
        else if (spec == '4') val = n4;
        else if (spec == '5') val = n5;
        else if (spec == '6') val = n6;
        else {
            _panicReasonCode = 0x5916u;
            PanicHaltLoop();
            return;
        }

        /* Convert number to decimal digits.
         * Asm uses fall-through: once we enter at a digit level,
         * all lower digits are always emitted (including zeros).
         * e.g. val=50 → "50", val=1050 → "1050", val=100 → "100" */
        if (val < 10)    goto emit_units;
        if (val < 100)   goto emit_tens;
        if (val < 1000)  goto emit_hundreds;
        if (val < 10000) goto emit_thousands;

        /* 10000+ : andi.l #$FFFF,d7; divu.w #10000 */
        *out++ = (uint8_t)(val / 10000) + '0';
        val = val % 10000;

    emit_thousands:
        *out++ = (uint8_t)(val / 1000) + '0';
        val = val % 1000;

    emit_hundreds:
        *out++ = (uint8_t)(val / 100) + '0';
        val = val % 100;

    emit_tens:
        *out++ = (uint8_t)(val / 10) + '0';
        val = val % 10;

    emit_units:
        *out++ = (uint8_t)val + '0';
    }
}

/* ============================================================
 * InputTextFilterKey
 * Filters currentKey to allowed charset.
 * Returns: key value if allowed, 0 if not.
 * ============================================================ */
__attribute__((used, externally_visible))
uint16_t InputTextFilterKey(void)
{
    uint16_t key = _currentKey;

    /* A-Z allowed */
    if (key >= 'A' && key <= 'Z')
        return key;

    /* 0-9 allowed */
    if (key >= '0' && key <= '9')
        return key;

    /* When allowExtraCharsFlag is set, '.' and '/' are rejected
     * (intercepted before the table scan where they'd normally match) */
    if (_allowExtraCharsFlag != 0) {
        if ((uint8_t)key == '.' || (uint8_t)key == '/')
            return 0;
    }

    /* Scan allowed control keys table (terminated by 0xFF) */
    {
        uint8_t *p = _inputAllowedControlKeys;
        while (*p != 0xFF) {
            if (*p == (uint8_t)key)
                return key;
            p++;
        }
    }

    return 0;
}

/* ============================================================
 * InsertCharacter
 * Inserts ch at pos within inMenuKeyboardBuffer, shifting bytes right.
 * Note: GetStringLength stub returns a0 = buffer + len (at NUL),
 * so the shift starts from the NUL and moves everything right.
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InsertCharacter(uint8_t *pos, uint8_t ch)
{
    uint16_t len = GetStringLength(_inMenuKeyboardBuffer);
    uint8_t *p = _inMenuKeyboardBuffer + len; /* points to NUL (matching asm stub: adda.w d0,a0) */

    /* Shift bytes right from p down to pos (do-while matches asm: shift first, then check) */
    do {
        *(p + 1) = *p;
        p--;
    } while (p >= pos);

    /* Write ch at pos (p is now one below pos, so p+1 == pos) */
    *(p + 1) = ch;
}

/* ============================================================
 * InputTextDeleteCharAtCursor
 * Deletes char at pos, shifting remaining chars left.
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InputTextDeleteCharAtCursor(uint8_t *pos)
{
    if (*pos == 0)
        return;

    uint8_t *p = pos;
    do {
        *p = *(p + 1);
        p++;
    } while (*(p - 1) != 0);
}

/* A text field is being typed into: a redefined key is a letter here, not a control
 * (input_sdl.c). */
volatile uint16_t _gTextInputActive;

__attribute__((optimize("no-tree-loop-distribute-patterns")))
static uint16_t inputTextBody(uint16_t maxLenPlus1, uint8_t *entryRaw);

/* ============================================================
 * InputText
 * Interactive text editor using the menu system.
 * maxLenPlus1: max input length + 1 (for NUL), must be <= 99
 * entryRaw: pointer to MenuEntry (passed as uint8_t* from asm)
 * Returns: 0 = committed (enter/fire), 1 = cancelled (escape)
 * ============================================================ */
__attribute__((used, externally_visible))
uint16_t InputText(uint16_t maxLenPlus1, uint8_t *entryRaw)
{
    uint16_t r;
    _gTextInputActive = 1;
    web_text_field(1);
    r = inputTextBody(maxLenPlus1, entryRaw);
    web_text_field(0);
    _gTextInputActive = 0;
    return r;
}

static uint16_t inputTextBody(uint16_t maxLenPlus1, uint8_t *entryRaw)
{
    MenuEntry *entry = (MenuEntry *)entryRaw;

    _deadVar02 = 1;

    if (maxLenPlus1 > 99) {
        _panicReasonCode = 0x1197u;
        PanicHaltLoop();
        return 0;
    }

    uint16_t maxLen = maxLenPlus1 - 1;

    /* Save original string pointer, copy to edit buffer */
    uint8_t *origStr = (uint8_t *)entry->fg;
    StringCopy(origStr, _inMenuKeyboardBuffer);
    entry->fg = (uintptr_t)_inMenuKeyboardBuffer;

    /* Cursor starts at end of string (at the NUL terminator).
     * Asm: GetStringLength stub sets a0 = buffer + len (at NUL). */
    uint16_t initLen = GetStringLength(_inMenuKeyboardBuffer);
    uint8_t *pos = _inMenuKeyboardBuffer + initLen;

    _deadInputTextVar = 0;

    for (;;) {
        MenuCheckControls();

        if (_shortFire != 0)
            goto commit;

        uint16_t key = InputTextFilterKey();

        if (key == 0) {
            goto insert_cursor;
        }

        if (key == 13) { /* Enter — commit */
            goto commit;
        }

        if (key == 19) { /* Left shift — move cursor left */
            if (pos > _inMenuKeyboardBuffer) {
                pos--;
            }
            continue;
        }

        if (key == 18) { /* Right shift — move cursor right */
            if (*pos != 0) {
                pos++;
            }
            continue;
        }

        if (key == 27) { /* Escape — cancel */
            entry->fg = (uintptr_t)origStr;
            inputTextRedraw(entry);
            _deadVar02 = 0;
            return 1;
        }

        if (key == 127) { /* Backspace — delete char to the left */
            if (pos > _inMenuKeyboardBuffer) {
                pos--;
                InputTextDeleteCharAtCursor(pos);
            }
            goto insert_cursor;
        }

        if (key == 126) { /* 0x7E — delete at cursor (like Delete key) */
            InputTextDeleteCharAtCursor(pos);
            goto insert_cursor;
        }

        /* Printable character (0x20..0x7F) — insert if not full */
        if (key >= 0x20 && key <= 0x7F) {
            uint16_t curLen = GetStringLength(_inMenuKeyboardBuffer);
            if (curLen != maxLen) {
                InsertCharacter(pos, (uint8_t)key);
                pos++;
            }
        }

    insert_cursor:
        /* Show cursor: insert 0xFF at pos, draw, then remove it */
        InsertCharacter(pos, 0xFF);
        inputTextRedraw(entry);
        InputTextDeleteCharAtCursor(pos);
    }

commit:
    StringCopy(_inMenuKeyboardBuffer, origStr);
    entry->fg = (uintptr_t)origStr;
    inputTextRedraw(entry);
    _deadVar02 = 0;
    return 0;
}
