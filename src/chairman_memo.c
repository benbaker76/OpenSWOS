/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* chairman_memo.c — Chairman memos and club business menu functions.
 *
 * Chunk 80 (partial): 5 exported functions ported from SWOS2.s lines 85420-85675.
 */

#include "chairman_memo.h"
#include "menu_system.h"
#include "input_text.h"
#include "translations.h"   /* tr_ptr — localize the by-pointer memo body text */
#include "mod_api.h"        /* ModCareerEventKind */
#include "mod_hooks.h"      /* ModHooks_CareerEvent: the PC chairman's scenes */

/* ---- ASM externs (globals) ---- */
extern MenuEntry _menuEntries[];
extern uint16_t _fireResetFlag;
extern uint16_t _isNationalSelector;
extern volatile uint16_t _currentMessage;
extern uint8_t *_plNameForMessage;
extern volatile uint16_t _currentGameTick;
extern uint16_t _clubBusinessActive;
extern void WaitForVSync(void);

/* ---- ASM externs (data labels) ---- */
extern uint8_t _chairmanMemosMenu[];
extern uint8_t _clubBusinessMenu[];
extern uint8_t *_clubMessagesTable[];       /* array of string pointers, 2 per message */
extern uint8_t _managerSexBuffer[];
extern uint8_t _managerSurnameBuffer[];
extern uint8_t _managerNameBuffer[];
extern uint8_t _aDearAB_0[];               /* "DEAR %a %b" */
extern uint8_t _aMemoFromThePre[];         /* "MEMO FROM THE PRESIDENT" */
extern uint8_t _aThePresident[];           /* "THE PRESIDENT" */

/* ================================================================
 * ShowChairmanMessage  (SWOS2.s line 85422)
 *
 * Entry point for displaying chairman memos.
 * Loads the chairman memos menu and shows it.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowChairmanMessage(void)
{
    /* (where the PC game plays its chairman's scene first: ShowChairmanMessage calls
     * PlayChairmanScene, then ShowMenu -- a mod's, display only) */
    ModHooks_CareerEvent(MODCAREER_CHAIRMAN, 0, _currentMessage, 0);
    ShowMenu(_chairmanMemosMenu);
}

/* ================================================================
 * ChairmanMemosInit  (SWOS2.s line 85615)
 *
 * Menu init callback. Sets fire reset flag, changes title to
 * "MEMO FROM THE PRESIDENT" if national team, then falls through
 * to OnReturn to render the memo content.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChairmanMemosInit(void)
{
    _fireResetFlag = 1;

    if (_isNationalSelector) {
        /* Change menu title to "MEMO FROM THE PRESIDENT" */
        _menuEntries[0].fg = (uintptr_t)_aMemoFromThePre;
        /* Change sign-off to "THE PRESIDENT" (entry 5 = offset $118/56) */
        _menuEntries[5].fg = (uintptr_t)_aThePresident;
    }

    /* Fall through to OnReturn */
    ChairmanMemosOnReturn();
}

/* ================================================================
 * ChairmanMemosOnReturn  (SWOS2.s line 85626)
 *
 * Menu onReturn callback. Formats the "DEAR %a %b" greeting line
 * and the two message body lines from clubMessagesTable, draws the
 * menu, waits 100 VBlanks, then shows the "Continue" button.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChairmanMemosOnReturn(void)
{
    /* Format "DEAR %a %b" into entry 1's text buffer */
    PrintFormatted(tr_fmt(_aDearAB_0), (uint8_t *)_menuEntries[1].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerSexBuffer, _managerSurnameBuffer, (void *)0);

    /* Look up message pair from clubMessagesTable (1-based, 2 pointers per message).
     * Guard against currentMessage being 0 or out of range (38 messages max).
     * Can happen if the chairman memo menu is triggered with stale state. */
    if (_currentMessage == 0 || _currentMessage > 38)
        return;

    int16_t msgIdx = (int16_t)(_currentMessage - 1) * 2;

    /* Format message line 1 into entry 2's text buffer */
    PrintFormatted(tr_fmt(_clubMessagesTable[msgIdx]),
                    (uint8_t *)_menuEntries[2].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerNameBuffer, (const uint8_t *)_plNameForMessage, (void *)0);

    /* Format message line 2 into entry 3's text buffer */
    PrintFormatted(tr_fmt(_clubMessagesTable[msgIdx + 1]),
                    (uint8_t *)_menuEntries[3].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerNameBuffer, (const uint8_t *)_plNameForMessage, (void *)0);

    /* SDL build skips the original Amiga 100-VBlank (~2s) dwell before
     * the Continue button appears. With a series of chairman messages
     * during a season-end review, the cumulative wait was painful and
     * users have no way to short-circuit it on the original hardware
     * either — so we just show Continue immediately. */
    _menuEntries[6].isInvisible = 0;
    DrawMenu();
}

/* ================================================================
 * ChairmanMemosContinue  (SWOS2.s line 85666)
 *
 * "Continue" button callback. Clears the current message and
 * exits the menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChairmanMemosContinue(void)
{
    _currentMessage = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * ShowClubBussinesMenu  (SWOS2.s line 85672)
 *
 * Entry point for the club business/bank statement menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowClubBussinesMenu(void)
{
    _clubBusinessActive = 0;
    ShowMenu(_clubBusinessMenu);
}
