/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* highlights_menu.c — Highlights menu functions.
 *
 * Chunk 83: HighlightsMenuInit, HighlightsMenuOnReturn,
 *           HighlightsMenuCommon, ExitHighlights
 * Chunk 89: LoadHighlightsMenu, SaveHighlightsMenu
 */

#include "game_types.h"
#include "menu_system.h"
#include "string_utils.h"
#include "file_io.h"
#include "menus.h"
#include "highlights_menu.h"
#include "endian_helpers.h"

/* ---- Menu entries (defined in asm) ---- */
extern MenuEntry _menuEntries[];

/* ---- Globals (defined in asm) ---- */
extern uint16_t _hilNumGoals;
extern uint16_t _hilTeam1Goals;
extern uint16_t _hilTeam2Goals;
extern uint16_t _hilPitchNumber;
extern uint16_t _hilMaxSubstitutes;
extern uintptr_t _extension;
extern uint8_t _selectedFilename[];
extern uint8_t _hilFileBuffer[];
extern uint32_t _kHilFileSig;
extern uint8_t _hilFilename[];

/* ---- String literals (defined in asm) ---- */
extern uint8_t _aLoadHighlights[];     /* "LOAD HIGHLIGHTS" */
extern uint8_t _aSaveHighlights[];     /* "SAVE HIGHLIGHTS" */
extern uint8_t _aNotAHighlights[];     /* "NOT A HIGHLIGHTS FILE" */
extern uint8_t _aCannotPlayOldV[];     /* "CANNOT PLAY OLD VERSION..." */

/* ---- Color constants ---- */
#define PINK_TO_BROWN_9         9
#define YELLOW_GREEN_BLACK_14   14

/* ================================================================
 * HighlightsMenuCommon  (SWOS2.s line 91039)
 *
 * Enables or disables the highlights menu entries based on whether
 * there are any goals recorded (hilNumGoals). If no goals, entries
 * 1 and 3 are disabled and colored gray. If goals exist, they are
 * enabled and colored normally. Also sets the current entry to 2
 * when disabled (so cursor lands on "EXIT" instead of greyed item).
 * ================================================================ */
__attribute__((used, externally_visible))
void HighlightsMenuCommon(void)
{
    if (_hilNumGoals == 0) {
        /* No goals — disable entries 1 and 3 */
        _menuEntries[1].isDisabled = 1;
        uint16_t color1 = _menuEntries[1].bgColor;
        color1 = (color1 & 0xFFF0) + PINK_TO_BROWN_9;
        _menuEntries[1].bgColor = color1;

        _menuEntries[3].isDisabled = 1;
        uint16_t color3 = _menuEntries[3].bgColor;
        color3 = (color3 & 0xFFF0) + PINK_TO_BROWN_9;
        _menuEntries[3].bgColor = color3;

        SetCurrentEntry(2);
    } else {
        /* Goals exist — enable entries 1 and 3 */
        _menuEntries[1].isDisabled = 0;
        uint16_t color1 = _menuEntries[1].bgColor;
        color1 = (color1 & 0xFFF0) + YELLOW_GREEN_BLACK_14;
        _menuEntries[1].bgColor = color1;

        _menuEntries[3].isDisabled = 0;
        uint16_t color3 = _menuEntries[3].bgColor;
        color3 = (color3 & 0xFFF0) + YELLOW_GREEN_BLACK_14;
        _menuEntries[3].bgColor = color3;
    }
}

/* ================================================================
 * HighlightsMenuInit  (SWOS2.s line 91031)
 *
 * Initializes the highlights menu. Falls through to OnReturn
 * (which calls HighlightsMenuCommon).
 * ================================================================ */
__attribute__((used, externally_visible))
void HighlightsMenuInit(void)
{
    HighlightsMenuCommon();
}

/* ================================================================
 * HighlightsMenuOnReturn  (SWOS2.s line 91033)
 *
 * Called when returning to the highlights menu. Calls Common
 * to update enable/disable state.
 * ================================================================ */
__attribute__((used, externally_visible))
void HighlightsMenuOnReturn(void)
{
    HighlightsMenuCommon();
}

/* ================================================================
 * ExitHighlights  (SWOS2.s line 91142)
 *
 * Exits the highlights menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitHighlights(void)
{
    SetExitMenuFlag();
}

/* ================================================================
 * LoadHighlightsMenu  (SWOS2.s line 89894)
 *
 * Menu callback: prompts for save disk, lets user select a .HIL
 * file, loads it, verifies signature, and copies filename to
 * hilFilename. Shows error for wrong extension or old version.
 * Then calls HighlightsMenuCommon to update menu state.
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadHighlightsMenu(void)
{
    if (Asset_PromptInsertSaveDiskDf0(_aLoadHighlights) != 0)
        return;

    if (GetFilenameAndExtension(0, _aLoadHighlights, _aLoadHighlights) != 0)
        goto cleanup;

#ifdef SWOS_SDL_BUILD
    /* PC: also accept .RPL — a whole-match replay in the same DOS HIL format.
     * The Highlights menu plays both; clear any prior RPL state first so a .HIL
     * uses the normal scene buffer. */
    {
        extern void DosReplay_ClearRpl(void);
        extern int  DosReplay_LoadRpl(uint8_t *filename);
        extern void    ReplayV2_Unload(void);
        extern int32_t ReplayV2_Load(const uint8_t *name);
        DosReplay_ClearRpl();
        ReplayV2_Unload();
        if (_extension == 0x2E535250u) {   /* '.SRP' -- OpenSWOS's own whole-match replay */
            if (ReplayV2_Load(_selectedFilename) != 0) {
                ShowErrorMenu(_aNotAHighlights);
                goto cleanup;
            }
            StringCopy(_selectedFilename, _hilFilename);
            goto cleanup;
        }
        if (_extension == 0x2E52504Cu) {   /* '.RPL' */
            if (DosReplay_LoadRpl(_selectedFilename) != 0) {
                ShowErrorMenu(_aNotAHighlights);
                goto cleanup;
            }
            StringCopy(_selectedFilename, _hilFilename);
            goto cleanup;   /* loaded; skip the .HIL sig check below */
        }
    }
#endif
    if (_extension != 0x2E48494Cu) { /* '.HIL' */
        ShowErrorMenu(_aNotAHighlights);
        goto cleanup;
    }

    if (LoadHilFile(_selectedFilename) != 0)
        goto cleanup;

    /* HIL file header is big-endian (Amiga m68k); BE_READ_U32 is a no-op
     * on the Amiga build and a byteswap on x86. */
    if (BE_READ_U32(_hilFileBuffer) != _kHilFileSig) {
        ShowErrorMenu(_aCannotPlayOldV);
        _hilNumGoals = 0;
        goto cleanup;
    }

    /* BE→host swap for the 5 u16 header fields now lives inside
     * LoadHilFile's legacy load branch so the JSON load path can
     * skip it. Both source formats converge on host-native here. */

    StringCopy(_selectedFilename, _hilFilename);

cleanup:
    ShowInsertDisk2Dialog(_aLoadHighlights);
    HighlightsMenuCommon();
}

/* ================================================================
 * SaveHighlightsMenu  (SWOS2.s line 89930)
 *
 * Menu callback: prompts for save disk, lets user select filename
 * with .HIL extension, saves highlights, and copies filename to
 * hilFilename on success.
 * ================================================================ */
__attribute__((used, externally_visible))
void SaveHighlightsMenu(void)
{
    if (Asset_PromptInsertSaveDiskDf0(_aSaveHighlights) != 0)
        return;

#ifdef SWOS_SDL_BUILD
    {   /* A whole-match replay (.SRP: LATEST and the dated copies) is loaded: it has no highlight
         * scenes, so what is kept is the recording itself, under the name chosen. */
        extern int32_t ReplayV2_Loaded(void);
        extern int32_t ReplayV2_SaveCopy(const uint8_t *name);
        if (ReplayV2_Loaded()) {
            if (SelectFileToSaveDialog(0x2E535250u /* '.SRP' */, _hilFilename, _aSaveHighlights) == 0 &&
                ReplayV2_SaveCopy(_selectedFilename) != 0)
                ShowErrorMenu(_aNotAHighlights);
            goto cleanup;
        }
    }
#endif

    if (SelectFileToSaveDialog(0x2E48494Cu /* '.HIL' */, _hilFilename, _aSaveHighlights) != 0)
        goto cleanup;

    if (DoSaveHighlights(_selectedFilename) != 0)
        goto cleanup;

    StringCopy(_selectedFilename, _hilFilename);

cleanup:
    ShowInsertDisk2Dialog(_aSaveHighlights);
}

/* ================================================================
 * Chunk 161: ShowHighlights, ShowMatchSequence,
 *            ViewHighlightsSaveCurrentMenu, JmpShowHighlights
 * ================================================================ */
extern uint16_t _hilStarted;
extern uint16_t _hilMaxSubstitutes;
extern uint16_t _gameMaxSubstitutes;
extern uint16_t _teamsLoaded;
extern uint16_t _poolplyrLoaded;
extern uint16_t _menuMusic;
extern uint16_t _deadVarAlways0;
extern volatile uint16_t _menuStatus;
extern volatile uint16_t _exitMenu;
extern uintptr_t _currentMenuPtr;
extern uint8_t  _currentMenu[];

extern void CopyTeamsAndHeader(void);
extern void StopMusic(void);
extern void InitMenuMusic(void);
extern void TryPlayMusic(void);
extern void PrepareMenuForFadeIn(void);
extern void ViewMatchSequence(void);
extern void ViewHighlights(void);

/* These are menu structs defined in assembly as labels */
extern uint8_t _stadiumMenu[];

/* ================================================================
 * viewAndRestoreMenu — helper for ShowHighlights/ShowMatchSequence
 *
 * Saves current menu state, calls a view function, then restores
 * the previous menu. Pattern used by both ShowHighlights and
 * ShowMatchSequence in the original asm.
 * ================================================================ */
static void viewAndRestoreMenu(void (*viewFunc)(void))
{
    Menu *menu = (Menu *)_currentMenu;
    MenuEntry *savedSelectedEntry = menu->selectedEntry;
    uintptr_t savedMenuPtr = _currentMenuPtr;

    viewFunc();

#ifdef SWOS_SDL_BUILD
    /* Back to the menu view. Every full-screen viewer routed through here may
     * have switched to the DOS 320x200 (game_loop.c MatchViewEnter), and the
     * menus are 272 lines of Amiga artwork. Safe unconditionally: MatchViewLeave
     * is a no-op when the view was never entered, and the match path's own call
     * restores the same saved values. Doing it HERE rather than inside the
     * viewer covers its early-return paths too. */
    { extern void MatchViewLeave(void); MatchViewLeave(); }
    { extern void ReplayV2_RestoreView(void); ReplayV2_RestoreView(); }   /* a .SRP's view */
#endif

    _menuStatus = 1;
    _exitMenu = 0;
    RestorePreviousMenu(savedSelectedEntry, (uint8_t *)savedMenuPtr);
}

/* ================================================================
 * ShowHighlights  (SWOS2.s line 29873)
 *
 * Starts highlight replay: loads teams, shows stadium menu,
 * plays highlights, and restores menu music.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowHighlights(void)
{
    _hilStarted = 1;
    _gameMaxSubstitutes = _hilMaxSubstitutes;
    _teamsLoaded = 0;
    _poolplyrLoaded = 0;
    CopyTeamsAndHeader();

    if (_menuMusic != 0)
        StopMusic();

    ShowMenu(_stadiumMenu);
    _deadVarAlways0 = 0;

#ifdef SWOS_SDL_BUILD
    /* Show the stadium "VS" line-up for a brief beat, then auto-advance. The
     * Amiga held it on screen while the selection fire was held, proceeding the
     * moment it was released (SWOS2.s wait_for_both_joy_fire_release → FadeOut) —
     * never a separate press. On SDL the menu-click fire is already released, so
     * a short fixed view matches that brief-view intent without flashing past.
     * WaitForVSync pumps input/audio and holds the frame, as the real-match
     * stadium hold does. */
    {
        extern void WaitForVSync(void);
        for (int16_t i = 75; i > 0; i--)   /* ~1.5s at 50Hz */
            WaitForVSync();
    }
#endif

    viewAndRestoreMenu(ViewHighlights);

    InitMenuMusic();
    if (_menuMusic != 0)
        TryPlayMusic();

    PrepareMenuForFadeIn();
}

/* ================================================================
 * ShowMatchSequence  (SWOS2.s line 29905)
 *
 * Saves current menu, plays match sequence, restores menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowMatchSequence(void)
{
    viewAndRestoreMenu(ViewMatchSequence);
}

/* ================================================================
 * ViewHighlightsSaveCurrentMenu  (SWOS2.s line 29921)
 *
 * Saves current menu, views all highlights, restores menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewHighlightsSaveCurrentMenu(void)
{
    viewAndRestoreMenu(ViewHighlights);
}

/* ================================================================
 * JmpShowHighlights  (SWOS2.s line 79492)
 *
 * Trivial forwarder to ShowHighlights.
 * ================================================================ */
__attribute__((used, externally_visible))
void JmpShowHighlights(void)
{
    ShowHighlights();
}
