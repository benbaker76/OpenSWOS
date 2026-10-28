/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "menus.h"
#include "challenge.h"
#ifdef SWOS_SDL_BUILD
/* PlatformInitOptions + Settings_Save, for the OPTIONS -> GAMEPLAY row. Guarded
 * because this header reaches platform_sdl.h and therefore SDL.h, which the
 * m68k build has no idea about — and there is no SWOS.ini on the Amiga. */
#include "swos_settings.h"
#endif
#include "translations.h"
#include "comp_identity.h"
#include "endian_helpers.h"
#include "auto_skip_debug.h"
#ifdef SWOS_DEBUG_AUTOSKIP
#endif
#include "game_utils.h"
#include "game_types.h"
#include "champions36.h"
#include "frontend.h"
#include "test_rules.h"
#include "display.h"            /* AGA: which Amiga display this build is */
#include "menu_system.h"
#include "online_menus.h"   /* MULTIPLAYER opens SWOS 2020's online flow */
#include "credits_menu.h"   /* CREDITS (SDL) */
#include "shooting_practice.h"  /* SHOOTING PRACTICE runs the friendly flow below */
#include "penalty_shootout.h"   /* PENALTY SHOOTOUT does too (the PRACTICE submenu) */
#include "deterministic.h"
#include "string_utils.h"
#include "text_render.h"
#include "edit_tactics.h"
#include "teams.h"
#include "audio.h"
#include "career_status.h"
#include "chairman_memo.h"
#include "play_match_menu.h"

/* ---- Constants ---- */
#define GAME_TYPE_NO_GAME 0
#define CN_ENGLAND        8
#define SEASONAL          0xFFFE
#define PLAYERPOSITIONS_SIZE         35

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint8_t *_menuImageStreamBaseEnd;
extern uint16_t _menuBGImageCmdStream[];
extern uint8_t _aGrafsMenubgRaw[];
extern uint16_t _twoPlayers;
extern uint16_t _player1ClearFlag;
extern uint16_t _player2ClearFlag;
extern uint16_t _deadVar01;
extern uint16_t _deadVar02;
extern uint16_t _deadVar03;
extern uint16_t _flipOnOff;
extern uint16_t _inFriendlyMenu;
extern uint16_t _isNationalTeam;
extern volatile uint16_t _menuStatus;
extern uint16_t _menuFade;
extern volatile uint16_t _exitMenu;
extern uint16_t _fireResetFlag;
extern volatile uint16_t _seasonUpdateInProgress;
extern uint16_t _internationalAutoAdvanceFlag;
extern uint16_t _highlightedLeagueTeamId;
extern uint16_t _writeOnlyVar01;
extern uint16_t _coachOrPlayer;
extern uint8_t _plNationality;
extern CompetitionSlotBuffer _primaryCompetitionBuf;          /* slot-0 untangle (S6): typed buffer (was competitionOver/diyPresetNameOfCompetition/competitionFileBuffer/primaryCompSquadViewParam/numDiyTeams) */
extern uint16_t _numSelectedTeams;
extern uint16_t _teamsLoaded;
extern uint16_t _poolplyrLoaded;
extern uint8_t _importTacticsFilename;
extern TeamFileEx *_chooseTacticsTeamPtr;
extern uint8_t _swosMainMenu[];

/* ZeroOutStars globals */
extern uint8_t _aResigned[];
extern uint8_t _stringDataEnd[]; /* memory boundary marker — end of strings region */

/* SetDefaultOptions globals */
extern uint16_t _gameLength;
extern uint16_t _autoReplays;
extern uint16_t _menuMusic;
extern uint16_t _autoSaveHighlights;
extern uint16_t _pitchType;
extern uint16_t _allPlayerTeamsEqual;

/* SetDefaultNameAndSurname globals */
extern uint16_t _mrOrMs;
extern volatile uint16_t _currentGameTick;
extern void *_defaultNamesTable[6];
extern void *_defaultSurnamesTable[6];
extern uint8_t _managerNameBuffer[9];
extern uint8_t _managerSurnameBuffer[13];

/* InitCareerVariables globals */
extern volatile uint16_t _currentTick;
extern uint16_t _randomSeed;
extern uint8_t _newRandomSeedCopy;
extern uint16_t _gameType;
extern uint16_t _contestStarted;
extern uint16_t _careerAutoAdvanceInProgress;
extern uint16_t _careerLoadedFlag;
extern uint16_t _selTeamNumber;
extern uint16_t _currentTeamNumber;
extern uint16_t _currentSeasonShowing;
extern uint8_t _seasonPlaying;
extern uint8_t _competitionFilename[14];
extern uint8_t _hilFilename[14];
extern uint16_t _careerFileBuffer;

/* InitUserTactics globals */
extern TeamTactics _teamTactics532;
extern TeamTactics _teamTactics442;
extern TeamTactics _teamTacticsSweep;
extern TeamTactics _teamTacticsUSERA;
extern TeamTactics _teamTacticsUSERB;
extern TeamTactics _teamTacticsUSERC;
extern TeamTactics _teamTacticsUSERD;
extern TeamTactics _teamTacticsUSERE;
extern TeamTactics _teamTacticsUSERF;
extern uint8_t _aUSER_A[];
extern uint8_t _aUSER_B[];
extern uint8_t _aUSER_C[];
extern uint8_t _aUSER_D[];
extern uint8_t _aUSER_E[];
extern uint8_t _aUSER_F[];

/* InitializeTacticsPositions globals */
extern void *_playersPositionsByTactics[18];
extern void *_teamTacticsTable[19];
extern uint16_t _tacticsPositionDefensiveness[11];

/* SaveOptions / RestoreOptions globals */
extern uint8_t _saveGameLength[];

/* ShowErrorMenu globals */
extern uint16_t _errorPromptsEnabled;
extern uintptr_t _errorText;
extern uint8_t _continueMenu[];

/* DoContinueAbortMenu globals */
extern uintptr_t _promptHeader;
extern uintptr_t _whatIsChanging;
extern uintptr_t _continueText;
extern uintptr_t _abortText;
extern uint16_t _contAbortDefault;
extern uint16_t _contAbortResult;
extern uint8_t _continueAbortMenu[];
extern uint8_t _aAbort[];
extern MenuEntry _menuEntries[];

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
extern void ProcessImageCommandStream(const uint16_t *cmdBuf, uint8_t *workBuf);
extern void LoadOverlayToFrontbuffer(const uint8_t *path);

/* ---- Local helpers ---- */

/* ================================================================
 * ZeroOutStars  (SWOS2.s)
 *
 * Scans through strings from aResigned up to stringDataEnd,
 * replacing '*' characters with a null terminator.
 *
 * In the original Amiga binary the strings between those two labels
 * were one contiguous block of `dc.b` data with no '*' chars, so the
 * routine was a runtime no-op. In our C builds (both SDL and the
 * back-ported Amiga ELF), `aResigned` and `stringDataEnd` are
 * unrelated C symbols at linker-determined addresses with arbitrary
 * other globals between them — the walk corrupts every byte equal
 * to 0x2A inside that range.
 *
 * SDL was already gated. On Amiga the corruption manifested as
 * `playerPositionsStringTable[0..7]` losing its high 16 bits
 * (entries 0..7's runtime addresses had byte-1 = 0x2A, which is
 * exactly where the walk hit), so position/skill text never rendered
 * on the play-match squad screen. Same fix: no-op for any C build.
 * ================================================================ */
__attribute__((used, externally_visible))
void ZeroOutStars(void)
{
    /* No-op for all C builds — see comment above. */
    return;
}

/* ================================================================
 * SetDefaultOptions  (SWOS2.s)
 *
 * Initializes game options to default values.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetDefaultOptions(void)
{
    _gameLength = 0;
    _autoReplays = 1;
    _menuMusic = 1;
    _autoSaveHighlights = 1;
    _pitchType = SEASONAL;
    _allPlayerTeamsEqual = 0;
}

/* ================================================================
 * SetDefaultNameAndSurname  (SWOS2.s)
 *
 * Selects a pseudo-random default name/surname pair from lookup
 * tables using currentGameTick as seed. Clears mrOrMs.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetDefaultNameAndSurname(void)
{
    uint16_t idx;

    _mrOrMs = 0;

    /* Original asm computed (tick & 0xFF) * 6 / 256 — yielding 0..5 — then
     * scaled by 4 to index a 32-bit pointer table by byte offset. On 64-bit
     * the pointer slots are 8 bytes, so the byte-offset arithmetic was
     * pulling values from mid-pointer. Drop the byte math and index the
     * `void *[6]` arrays directly. */
    idx = _currentGameTick & 0xFF;
    idx = (uint16_t)((idx * 6) >> 8);

    StringCopy((uint8_t *)_defaultNamesTable[idx], _managerNameBuffer);
    StringCopy((uint8_t *)_defaultSurnamesTable[idx], _managerSurnameBuffer);
}

/* ================================================================
 * InitializeTacticsPositions  (SWOS2.s)
 *
 * For each of 18 tactics formations, computes a defensiveness score
 * for each of the 10 outfield players, then sorts players by score
 * (descending — most defensive first) using bubble sort.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitializeTacticsPositions(void)
{
    int16_t formation;

    for (formation = 0; formation < 18; formation++) {
        TeamTactics *tactics = (TeamTactics *)_teamTacticsTable[formation];
        uint8_t *posArr = (uint8_t *)_playersPositionsByTactics[formation];

        int16_t player;

        /* Initialize: byte 0 = 0, bytes 1-10 = player numbers */
        posArr[0] = 0;

        for (player = 1; player <= 10; player++) {
            int16_t pos;
            posArr[player] = (uint8_t)player;
            _tacticsPositionDefensiveness[player] = 0;

            /* Accumulate defensiveness over positions 15-34 (20 half-field positions) */
            for (pos = 34; pos >= 15; pos--) {
                int16_t offset = (player - 1) * PLAYERPOSITIONS_SIZE + pos;
                uint8_t posData = ((uint8_t *)tactics->positions)[offset];
                int16_t xCoord = (posData >> 4) & 0xF;
                int16_t yCoord = posData & 0xF;
                int16_t xDist = xCoord - 7;
                int16_t yDist;
                int16_t contrib;

                if (xDist < 0) xDist = -xDist;  /* |x - 7| */
                yDist = 15 - yCoord;

                /* contribution = max(xDist, yDist) */
                contrib = xDist;
                if ((uint16_t)yDist > (uint16_t)contrib)
                    contrib = yDist;

                _tacticsPositionDefensiveness[player] += (uint16_t)contrib;
            }
        }

        /* Bubble sort players 1-10 by defensiveness (descending) */
        {
            int16_t swapped;
            do {
                int16_t j;
                swapped = 0;
                for (j = 0; j < 9; j++) {
                    if (_tacticsPositionDefensiveness[j + 1] < _tacticsPositionDefensiveness[j + 2]) {
                        /* Swap scores */
                        uint16_t tmpScore = _tacticsPositionDefensiveness[j + 1];
                        _tacticsPositionDefensiveness[j + 1] = _tacticsPositionDefensiveness[j + 2];
                        _tacticsPositionDefensiveness[j + 2] = tmpScore;
                        /* Swap player numbers in position array */
                        {
                            uint8_t tmpPlayer = posArr[j + 1];
                            posArr[j + 1] = posArr[j + 2];
                            posArr[j + 2] = tmpPlayer;
                        }
                        swapped = 1;
                    }
                }
            } while (swapped);
        }
    }

}

/* ================================================================
 * InitUserTactics  (SWOS2.s)
 *
 * Initializes 6 user-defined tactics by:
 * 1) Copying 70 bytes from 532 formation positions to 442 and Sweep
 * 2) Copying full 442 tactics (370 bytes) to each user slot (A-F)
 * 3) Overwriting user slot names with "USER A"-"USER F"
 * 4) Calling InitializeTacticsPositions to sort player positions
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InitUserTactics(void)
{
    int16_t i;

    /* Copy positions[8..9] (70 bytes) from 532 formation to both 442 and Sweep.
     *
     * Through _teamTacticsTable, NOT the bare _teamTactics442 / Sweep symbols:
     * SWOS_ORIGINAL_TACTICS and SWOS_DOS_TACTICS point the table at variant
     * copies, and writing to the unsuffixed symbols left those variants holding
     * whatever their JSON baked — which for the DOS 4-4-2 and Sweep is 60 bytes
     * of DEAD dseg data that the real DOS build overwrites here too. The asm
     * writes to the tables the game plays with; so does this. */
    {
        TeamTactics *t442   = (TeamTactics *)_teamTacticsTable[0];
        TeamTactics *tSweep = (TeamTactics *)_teamTacticsTable[8];
        uint8_t *src = (uint8_t *)&_teamTactics532.positions[8];
        uint8_t *dst442 = (uint8_t *)&t442->positions[8];
        uint8_t *dstSweep = (uint8_t *)&tSweep->positions[8];
        for (i = 0; i < 70; i++) {
            dst442[i] = src[i];
            dstSweep[i] = src[i];
        }
    }

    /* Copy full 370-byte 442 tactics to each user slot */
    {
        TeamTactics *userSlots[6];
        int16_t slot;
        userSlots[0] = &_teamTacticsUSERA;
        userSlots[1] = &_teamTacticsUSERB;
        userSlots[2] = &_teamTacticsUSERC;
        userSlots[3] = &_teamTacticsUSERD;
        userSlots[4] = &_teamTacticsUSERE;
        userSlots[5] = &_teamTacticsUSERF;

        for (slot = 0; slot < 6; slot++) {
            uint8_t *dst = (uint8_t *)userSlots[slot];
            uint8_t *src = (uint8_t *)_teamTacticsTable[0];   /* the 4-4-2 in play */
            for (i = 0; i < (int16_t)sizeof(TeamTactics); i++) {
                dst[i] = src[i];
            }
        }
    }

    /* Overwrite each user slot's name field */
    StringCopy(_aUSER_A, (uint8_t *)_teamTacticsUSERA.name);
    StringCopy(_aUSER_B, (uint8_t *)_teamTacticsUSERB.name);
    StringCopy(_aUSER_C, (uint8_t *)_teamTacticsUSERC.name);
    StringCopy(_aUSER_D, (uint8_t *)_teamTacticsUSERD.name);
    StringCopy(_aUSER_E, (uint8_t *)_teamTacticsUSERE.name);
    StringCopy(_aUSER_F, (uint8_t *)_teamTacticsUSERF.name);

    InitializeTacticsPositions();
}

/* ================================================================
 * InitCareerVariables  (SWOS2.s)
 *
 * Initializes career mode variables. Seeds random generator from
 * currentTick, sets game type, clears flags, then calls Randomize2
 * and InitUserTactics.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitCareerVariables(uint16_t gameTypeParam)
{
#ifdef DETERMINISTIC_MODE
    InitDeterministicMode();
#else
    _randomSeed = _currentTick;
    _newRandomSeedCopy = (uint8_t)(_currentTick & 0xFF);
#endif
    _gameType = gameTypeParam;
    _contestStarted = 0;
    _teamsLoaded = 0;
    _careerAutoAdvanceInProgress = 0;
    _careerLoadedFlag = 0;
    _selTeamNumber = 0xFFFF;
    _currentTeamNumber = 0xFFFF;
    _currentSeasonShowing = 0;
    _seasonPlaying = 96;
    _competitionFilename[0] = 0;
    _hilFilename[0] = 0;
    _careerFileBuffer = 0;

    Randomize2(0);
    InitUserTactics();
}

/* ================================================================
 * PrepareMenuBackground  (SWOS2.s line 42468)
 *
 * Processes the menu background image command stream, then loads
 * the menu background raw image into the frontbuffer overlay.
 * ================================================================ */
__attribute__((used, externally_visible))
void PrepareMenuBackground(void)
{
    ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    LoadOverlayToFrontbuffer(_aGrafsMenubgRaw);
}

/* ================================================================
 * InitMainMenuStuff  (SWOS2.s line 42280)
 *
 * Initializes the main menu state: clears all game/menu flags,
 * sets defaults, initializes user tactics, then activates the
 * main SWOS menu.
 * ================================================================ */
__attribute__((used, externally_visible, noinline))
void InitMainMenuStuff(void)
{
    ZeroOutStars();

    _twoPlayers = 0;
    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    _deadVar01 = 0;
    _deadVar02 = 0;

    SetDefaultOptions();

    _flipOnOff = 1;
    _inFriendlyMenu = 0;
    _isNationalTeam = 0;

    InitCareerVariables(GAME_TYPE_NO_GAME);

    _menuStatus = 0;
    _menuFade = 0;
    _deadVar03 = 0;
    _exitMenu = 0;
    _fireResetFlag = 0;
    _seasonUpdateInProgress = 0;
    _internationalAutoAdvanceFlag = 0;
    _highlightedLeagueTeamId = 0xFFFF;
    _writeOnlyVar01 = 0;
    _coachOrPlayer = 1;

    SetDefaultNameAndSurname();

    _plNationality = CN_ENGLAND;

    /* Original asm clears competitionOver 3 times (likely a bug or
     * intended to clear 3 consecutive words, but label is the same) */
    _primaryCompetitionBuf.tourn.header.competitionOver = 0;
    _primaryCompetitionBuf.tourn.header.competitionOver = 0;
    _primaryCompetitionBuf.tourn.header.competitionOver = 0;

    _numSelectedTeams = 0;
    _teamsLoaded = 0;
    _poolplyrLoaded = 0;
    _importTacticsFilename = 0;
    _chooseTacticsTeamPtr = (TeamFileEx *)0;

    InitUserTactics();

    /* Original: lea (swosMainMenu),a6; bra.w ActivateMenu */
    ActivateMenu(_swosMainMenu);
}

/* ================================================================
 * SaveOptions  (SWOS2.s line 69187)
 *
 * Copies 6 consecutive uint16_t values starting at gameLength
 * into a save area (saveGameLength) for later restore.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void SaveOptions(void)
{
    uint16_t *src = &_gameLength;
    uint16_t *dst = (uint16_t *)_saveGameLength;
    int16_t i;
    for (i = 5; i >= 0; i--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * RestoreOptions  (SWOS2.s line 69198)
 *
 * Restores 6 consecutive uint16_t values from saveGameLength
 * back into gameLength area.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void RestoreOptions(void)
{
    uint16_t *src = (uint16_t *)_saveGameLength;
    uint16_t *dst = &_gameLength;
    int16_t i;
    for (i = 5; i >= 0; i--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * ShowErrorMenu  (SWOS2.s line 90693)
 *
 * Sets error text and shows the continue menu for user acknowledgement.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowErrorMenu(uint8_t *errorTextStr)
{
    _errorPromptsEnabled = 1;
    _errorText = (uintptr_t)errorTextStr;
    ShowMenu(_continueMenu);
}

/* ================================================================
 * DoContinueAbortMenu  (SWOS2.s line 90685)
 *
 * Shows a continue/abort prompt with customizable text.
 *
 * Asm register convention:
 *   in:  a0 = prompt header, a1 = "what is changing" text,
 *        a2 = continue text, a3 = abort text, d0 = default entry
 *   out: d0 = 0 (continue) or 1 (abort), Z flag reflects d0
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing,
                              uint8_t *contText, uint8_t *abortTextStr,
                              uint16_t defaultEntry)
{
    _promptHeader = (uintptr_t)header;
    _whatIsChanging = (uintptr_t)changing;
    _continueText = (uintptr_t)contText;
    _abortText = (uintptr_t)abortTextStr;
    _contAbortDefault = defaultEntry;

    ShowMenu(_continueAbortMenu);

    return _contAbortResult;
}

/* ================================================================
 * ContinueInit  (SWOS2.s line 90741)
 *
 * Menu init callback for the Continue (error) menu.
 * Sets the error text string, and if prompts are disabled,
 * draws immediately and exits the menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ContinueInit(void)
{
    _menuEntries[0].fg = _errorText;

    if (_errorPromptsEnabled == 0) {
        /* Gray out and auto-dismiss */
        /* move.w #7 writes to upper 16 bits of bg (big-endian) */
        _menuEntries[0].bg = (7u << 16) | (_menuEntries[0].bg & 0xFFFFu);
        _menuEntries[1].isInvisible = 1;  /* hide continue button */
        DrawMenu();
        SetExitMenuFlag();
    }
}

/* ================================================================
 * ContinueMenuContinueSelected  (SWOS2.s line 90757)
 *
 * Menu callback for the Continue button in the continue menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ContinueMenuContinueSelected(void)
{
    SetExitMenuFlag();
}

/* ================================================================
 * ContinueAbortInit  (SWOS2.s line 90853)
 *
 * Menu init callback for the Continue/Abort menu.
 * Sets up the four entries: header, what's changing, continue, abort.
 * Adjusts header width if needed, colors abort button red, and
 * optionally sets default entry.
 * ================================================================ */
__attribute__((used, externally_visible))
void ContinueAbortInit(void)
{
    extern uint8_t _smallCharsTable[];

    if (_promptHeader == 0) {
        /* No header text: hide entry 0 */
        _menuEntries[0].isInvisible = 1;
    } else {
        /* Compute text width and adjust entry 0 width */
        uint16_t width = GetTextWidth((uint8_t *)_promptHeader, _smallCharsTable);
        width += 20;
        if (width <= 120)
            width = 120;

        _menuEntries[0].dwWidth = width;
        _menuEntries[0].x -= width >> 1;
        _menuEntries[0].fg = _promptHeader;
    }

    /* Set text pointers for entries 1-3 */
    _menuEntries[1].fg = _whatIsChanging;
    _menuEntries[2].fg = _continueText;
    _menuEntries[3].fg = _abortText;

    if (_abortText == 0) {
        /* No abort text: hide abort button */
        _menuEntries[3].isInvisible = 1;
        return;
    }

    /* If abort text is the standard "ABORT" string, color it red */
    if (_abortText == (uintptr_t)_aAbort) {
        /* move.w reads/writes upper 16 bits of bg (big-endian) */
        uint16_t bgColor = (uint16_t)(_menuEntries[3].bg >> 16);
        bgColor = (bgColor & 0xFFF0) + 10; /* RED_10 */
        _menuEntries[3].bg = ((uint32_t)bgColor << 16) | (_menuEntries[3].bg & 0xFFFFu);
    }

    /* Set default entry if requested */
    if (_contAbortDefault != 0) {
        SetCurrentEntry(3);
    }
}

/* ================================================================
 * ContinueSelected  (SWOS2.s line 90900)
 *
 * Menu callback: continue was selected.
 * ================================================================ */
__attribute__((used, externally_visible))
void ContinueSelected(void)
{
    _contAbortResult = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * AbortSelected  (SWOS2.s line 90906)
 *
 * Menu callback: abort was selected.
 * ================================================================ */
__attribute__((used, externally_visible))
void AbortSelected(void)
{
    _contAbortResult = 1;
    SetExitMenuFlag();
}

/* ---- Stadium Menu externs (Chunk 89) ---- */
extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern uint8_t _gameName[];
extern uint8_t _gameRound[];
extern uint16_t _hilStarted;
extern uint16_t _topTeamCoachNo;
extern uint16_t _bottomTeamCoachNo;
extern uint16_t _topTeamPlayerNo;
extern uint16_t _bottomTeamPlayerNo;
extern uint16_t _trainingGame;
extern uint16_t _gameMaxSubstitutes;
extern uint8_t _aGrafsMenubg2Ra[];     /* "grafs/menubg2.raw" */
extern EditTeamsSpriteCopy _stadiumMenuLittlePlayerSpriteCopy;
extern uint16_t _littlePlayersSpriteTable[];

/* Color constants */
#define BRIGHT_BLUE_TO_BLUE_13  13
#define BLUE_TO_PURPLE_11       11
#define RED_10                  10

#define SPR_LITTLE_PLAYER_GOALIE  213
#define SHIRT_VERTICAL_STRIPES    2

/* ================================================================
 * ShowStadiumInit  (SWOS2.s line 89572)
 *
 * Initializes the stadium menu: loads background, sets team names
 * with appropriate colors, fills player name pointers for both
 * teams, hides unused bench slots, then draws and exits.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowStadiumInit(void)
{
    LoadOverlayToFrontbuffer(_aGrafsMenubg2Ra);

    /* Entry 0: game name */
    _menuEntries[0].fg = (uintptr_t)_gameName;

    if (_gameRound[0] == 0) {
        /* No round info: hide entry 1, nudge entry 0 down */
        _menuEntries[1].isInvisible = 1;
        _menuEntries[0].y += 5;
    } else {
        /* Show round info in entry 1 */
        _menuEntries[1].fg = (uintptr_t)_gameRound;
    }

    /* Entry 2: top team name + trailing space */
    {
        uint8_t *dst = (uint8_t *)_menuEntries[2].fg;
        uint8_t *end = StringCopy((uint8_t *)_topTeamInGame.teamName, dst);
        *end++ = ' ';
        *end = 0;
    }

    /* Color top team name if not in highlights mode */
    if (_hilStarted == 0) {
        uint16_t color = BRIGHT_BLUE_TO_BLUE_13;
        if (_topTeamCoachNo != 0) {
            /* coached */
        } else if (_topTeamPlayerNo != 0) {
            color = BLUE_TO_PURPLE_11;
        } else if (_trainingGame != 0) {
            color = BRIGHT_BLUE_TO_BLUE_13;
        } else {
            color = RED_10;
        }
        _menuEntries[2].bgColor = color;
    }

    /* Entry 4: leading space + bottom team name */
    {
        uint8_t *dst = (uint8_t *)_menuEntries[4].fg;
        *dst++ = ' ';
        StringCopy((uint8_t *)_bottomTeamInGame.teamName, dst);
    }

    /* Color bottom team name if not in highlights mode */
    if (_hilStarted == 0) {
        uint16_t color = BRIGHT_BLUE_TO_BLUE_13;
        if (_bottomTeamCoachNo != 0) {
            /* coached */
        } else if (_bottomTeamPlayerNo != 0) {
            color = BLUE_TO_PURPLE_11;
        } else if (_trainingGame != 0) {
            color = BRIGHT_BLUE_TO_BLUE_13;
        } else {
            color = RED_10;
        }
        _menuEntries[4].bgColor = color;
    }

    /* Entries 16-31: top team player names */
    {
        int16_t i;
        for (i = 0; i < 16; i++) {
            _menuEntries[16 + i].fg = (uintptr_t)_topTeamInGame.players[i].fullName;
        }
    }

    /* Entries 43-58: bottom team player names */
    {
        int16_t i;
        for (i = 0; i < 16; i++) {
            _menuEntries[43 + i].fg = (uintptr_t)_bottomTeamInGame.players[i].fullName;
        }
    }

    /* Hide excess bench entries based on gameMaxSubstitutes */
    {
        int16_t entriesToHide = 5 - (int16_t)_gameMaxSubstitutes;
        if (entriesToHide > 0) {
            int16_t j;
            for (j = 0; j < entriesToHide; j++) {
                _menuEntries[31 - j].isInvisible = 1;
                _menuEntries[58 - j].isInvisible = 1;
            }
        }
    }

    DrawMenu();
    SetExitMenuFlag();
}

/* ================================================================
 * StadiumPlayerSpritesBeforeDraw  (SWOS2.s line 89693)
 *
 * Sets up the little player sprite and color table for a given
 * player in the stadium menu. Handles goalie vs outfield differences
 * in sprite selection and shirt color assignment. Fills skin color.
 *
 * in: team = TeamGame pointer, playerIndex = 0-based player index
 * ================================================================ */
__attribute__((used, externally_visible))
void StadiumPlayerSpritesBeforeDraw(TeamGame *team, uint16_t playerIndex)
{
    if (playerIndex == 0) {
        /* Goalie */
        _stadiumMenuLittlePlayerSpriteCopy.srcSprite =SPR_LITTLE_PLAYER_GOALIE;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[11] = 11;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[10] = 10;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[14] = (uint16_t)team->prShortsCol;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[15] = (uint16_t)team->prSocksCol;
    } else {
        /* Outfield player */
        _stadiumMenuLittlePlayerSpriteCopy.srcSprite =_littlePlayersSpriteTable[team->prShirtType];
        uint16_t col1 = (uint16_t)team->prShirtCol;
        uint16_t col2 = (uint16_t)team->prStripesCol;
        if (team->prShirtType == SHIRT_VERTICAL_STRIPES) {
            uint16_t tmp = col1;
            col1 = col2;
            col2 = tmp;
        }
        _stadiumMenuLittlePlayerSpriteCopy.colTable[11] = col1;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[10] = col2;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[14] = (uint16_t)team->prShortsCol;
        _stadiumMenuLittlePlayerSpriteCopy.colTable[15] = (uint16_t)team->prSocksCol;
    }

    /* Set skin color from player's face2 field */
    int16_t skinColor = (int8_t)team->players[playerIndex].face2;
    FillSkinColorConversionTable((uint16_t)skinColor, _stadiumMenuLittlePlayerSpriteCopy.colTable);
}

/* ================================================================
 * StadiumBeforeDrawLeftTeam  (SWOS2.s line 89674)
 *
 * Menu beforeDraw callback for left (top) team player entries.
 * Extracts player index from entry ordinal and delegates to
 * StadiumPlayerSpritesBeforeDraw with topTeamInGame.
 *
 * in: entry = MenuEntry* (a5 in asm)
 * ================================================================ */
__attribute__((used, externally_visible))
void StadiumBeforeDrawLeftTeam(void *entryPtr)
{
    MenuEntry *entry = (MenuEntry *)entryPtr;
    uint16_t playerIndex = entry->ordinal - 5;
    StadiumPlayerSpritesBeforeDraw(&_topTeamInGame, playerIndex);
}

/* ================================================================
 * StadiumBeforeDrawRightTeam  (SWOS2.s line 89682)
 *
 * Menu beforeDraw callback for right (bottom) team player entries.
 * Extracts player index from entry ordinal and delegates to
 * StadiumPlayerSpritesBeforeDraw with bottomTeamInGame.
 *
 * in: entry = MenuEntry* (a5 in asm)
 * ================================================================ */
__attribute__((used, externally_visible))
void StadiumBeforeDrawRightTeam(void *entryPtr)
{
    MenuEntry *entry = (MenuEntry *)entryPtr;
    uint16_t playerIndex = entry->ordinal - 32;
    StadiumPlayerSpritesBeforeDraw(&_bottomTeamInGame, playerIndex);
}

/* ================================================================
 * Squad menu callbacks (Chunk 111, SWOS2.s lines 56661-56711)
 * ================================================================ */

/* ---- External asm functions (not yet ported) ---- */
extern void PlayTrainingGame(void);

/* ---- External asm data ---- */
extern uint8_t _squadMenu[];
extern uint16_t _squadPrevSelectedPlayerIndex;
extern uint16_t _isSquadViewMode;
extern uintptr_t _squadTeamFilePtr;

/* ================================================================
 * SelectTraining  (SWOS2.s line 56661)
 *
 * Starts training match, then re-activates squad menu at entry 9.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectTraining(void)
{
    PlayTrainingGame();
    ActivateMenu(_squadMenu);
    SetCurrentEntry(9);
}

/* ================================================================
 * SelectGoals  (SWOS2.s line 56671)
 *
 * Switches squad view to goals/clubs mode. Saves positions,
 * sets view mode to 1, updates menu state, selects entry 11.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectGoals(void)
{
    _squadPrevSelectedPlayerIndex = 0xFFFF;
    SquadMenuSavePlayerPositions();
    _isSquadViewMode = 1;
    SquadUpdateMenuState();
    SetCurrentEntry(11);
    DrawMenu();
}

/* ================================================================
 * SquadOnSelect11  (SWOS2.s line 56682)
 *
 * Returns from goals/clubs view to normal squad view.
 * Clears view mode, updates menu state, selects entry 8.
 * ================================================================ */
__attribute__((used, externally_visible))
void SquadOnSelect11(void)
{
    _isSquadViewMode = 0;
    SquadUpdateMenuState();
    SetCurrentEntry(8);
    DrawMenu();
}

/* ================================================================
 * SquadOnSelect45  (SWOS2.s line 56696)
 *
 * Rotates players left by 5 positions and redraws items 23, 44.
 * ================================================================ */
__attribute__((used, externally_visible))
void SquadOnSelect45(void)
{
    static const int16_t squad45Items[] = { 23, 44, -1 };
    RotatePlayersLeft((TeamFileEx *)_squadTeamFilePtr, 5);
    DrawMenuItemsWaitForVSync(squad45Items);
}

/* ================================================================
 * Squad45AfterDraw  (SWOS2.s line 56709)
 *
 * Draws menu entry 44 (0x9A0 / 56 = 44).
 * ================================================================ */
__attribute__((used, externally_visible))
void Squad45AfterDraw(void)
{
    DrawMenuItem(&_menuEntries[44]);
}

/* ================================================================
 * Squad operations (Chunk 112, SWOS2.s lines 57494-57562)
 * ================================================================ */

/* SPR_CHECK_BIG / SPR_CROSS_BIG come from play_match_menu.h (included above). */

extern uint16_t _teamSkillCurrent;
extern uint16_t _teamSkillSnapshot;
extern uint8_t _playerPositionsArray[];
extern uint8_t _oldPlayerPositionsArray[];
extern uint16_t _teamSkillDiffAfterChange;

/* ================================================================
 * UpdateBigCheckMark  (SWOS2.s line 57537)
 *
 * Updates the big check mark sprite on menu entry 1 (offset 0x38)
 * based on skill difference after formation change.
 * - If teamSkillDiffAfterChange >= 0: hide the check mark
 * - If current > snapshot (team STRONGER): show green tick (SPR_CHECK_BIG)
 * - If current < snapshot (team WEAKER):   show red cross (SPR_CROSS_BIG)
 * - If equal: return (no change)
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateBigCheckMark(void)
{
    MenuEntry *entry = &_menuEntries[1]; /* menuEntries + 0x38 */

    entry->isInvisible = 1;

    if ((int16_t)_teamSkillDiffAfterChange >= 0)
        return;

    uint16_t flags = entry->stringFlags;
    flags &= ~(1 << 13); /* bclr #13 */
    entry->stringFlags = flags;

    if (_teamSkillCurrent > _teamSkillSnapshot) {
        /* Team got stronger: show green tick */
        entry->fgWord = SPR_CHECK_BIG;
    } else if (_teamSkillCurrent < _teamSkillSnapshot) {
        /* Team got weaker: show red cross */
        entry->fgWord = SPR_CROSS_BIG;
    } else {
        /* Equal: no mark shown */
        return;
    }

    flags = entry->stringFlags;
    flags |= (1 << 13); /* bset #13 */
    entry->stringFlags = flags;
    entry->isInvisible = 0;
}

/* ================================================================
 * SquadRedrawPlayerListAndCheckMark  (SWOS2.s line 57496)
 *
 * Redraws entries 28..38 (0x620/56=28, count=11) and entry 1
 * (0x38/56=1).
 * ================================================================ */
__attribute__((used, externally_visible))
void SquadRedrawPlayerListAndCheckMark(void)
{
    CallDrawMultipleItems(11, &_menuEntries[28]);
    CallDrawMenuItem(&_menuEntries[1]);
}

/* ================================================================
 * SquadMenuSavePlayerPositions  (SWOS2.s line 57506)
 *
 * Snapshots the team total (_teamSkillSnapshot = _teamSkillCurrent),
 * copies 32 bytes from playerPositionsArray → oldPlayerPositionsArray,
 * then calls UpdateBigCheckMark.
 * ================================================================ */
__attribute__((used, externally_visible))
void SquadMenuSavePlayerPositions(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)_oldPlayerPositionsArray;
    volatile uint8_t *src = (volatile uint8_t *)_playerPositionsArray;
    int16_t i;
    _teamSkillSnapshot = _teamSkillCurrent;
    for (i = 0; i < 32; i++)
        dst[i] = src[i];
    UpdateBigCheckMark();
}

/* ================================================================
 * SquadCommitPositionsAndUpdateSkill  (SWOS2.s line 57521)
 *
 * Same copy as SquadMenuSavePlayerPositions, then marks team file
 * byte 0x3B as 1, calls ComputeOldPlayerSkillForViewResult(team, 5),
 * and falls through to UpdateBigCheckMark.
 * ================================================================ */
extern uint16_t ComputeOldPlayerSkillForViewResult(TeamFileEx *team, uint16_t goalsCount);

__attribute__((used, externally_visible))
void SquadCommitPositionsAndUpdateSkill(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)_oldPlayerPositionsArray;
    volatile uint8_t *src = (volatile uint8_t *)_playerPositionsArray;
    int16_t i;
    _teamSkillSnapshot = _teamSkillCurrent;
    for (i = 0; i < 32; i++)
        dst[i] = src[i];

    TeamFile *teamFile = (TeamFile *)_squadTeamFilePtr;
    teamFile->playerValuesDirty = TEAM_VALUES_DIRTY;

    ComputeOldPlayerSkillForViewResult((TeamFileEx *)teamFile, 5);
    UpdateBigCheckMark();
}

/* ================================================================
 * Chunk 208: SquadUpdateGoalsMenuText + SquadCommon +
 *            SquadSwapPlayersAtIndices (3 → menus.c)
 * ================================================================ */

extern uint16_t _removedPlayerStatAccum;
extern uint16_t _squadPrevSelectedBreakPos;
extern MenuEntry *_squadPrevHighlightedMenuEntryPtr;
extern uint8_t _aGoals[];
extern uint8_t  _currentMatchPlayers[];
extern uint16_t _careerNumPlayers;
extern void CallDrawMenuItem(MenuEntry *entry);
extern void SetBenchPlayersNumbers(TeamFileEx *team);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern uint16_t _cGetPlayerAtIndexFileIdx;

/* SquadUpdateGoalsMenuText (SWOS2.s line 48686)
 * Computes total goals for all players in current squad and
 * writes "GOALS (N)" string into menu entry 16's fg buffer. */
__attribute__((used, externally_visible))
void SquadUpdateGoalsMenuText(void)
{
    TeamFile *tfBase = (TeamFile *)_squadTeamFilePtr;
    uint8_t *playerList = tfBase->playerNumbers;
    uint16_t count = 16;
    uint16_t totalGoals = 0;

    if ((int16_t)_selTeamNumber != -1 &&
        _selTeamNumber == *(uint16_t *)tfBase) {
        playerList = _currentMatchPlayers;
        count = _careerNumPlayers;
        totalGoals = _removedPlayerStatAccum;
    }

    /* Add own goals count from TeamFile.writesZeroHere */
    totalGoals += (uint16_t)tfBase->writesZeroHere;

    count--;   /* subq.w #1,d7 for dbf loop */
    uint16_t i;
    for (i = count + 1; i > 0; i--) {
        int16_t idx = (int16_t)(int8_t)*playerList++;
        TeamFileEx *tfx = (TeamFileEx *)tfBase;
        PlayerFile *pf = &tfx->players[(uint16_t)idx];
        totalGoals += (uint16_t)pf->goalStats[0];
        totalGoals += (uint16_t)pf->goalStats[1];
        totalGoals += (uint16_t)pf->goalStats[2];
        totalGoals += (uint16_t)pf->goalStats[3];
    }

    /* Build "GOALS (N)" string in entry 16's fg buffer */
    uint8_t *buf = (uint8_t *)_menuEntries[16].fg;
    buf = StringCopy((const uint8_t *)_aGoals, buf);
    *buf++ = ' ';
    *buf++ = '(';
    Int2Ascii(totalGoals, buf);
    while (*buf) buf++;
    *buf++ = ')';
    *buf = 0;
}

/* SquadSwapPlayersAtIndices (SWOS2.s line 49055)
 * Swaps two players at given break positions.  If the swap crosses
 * the starting 11 / substitutes boundary (one < 11, one >= 11, or
 * either >= 16), also swaps playerPositionsArray and shirt numbers.
 * Always swaps the player number array entry. */
static void squadSwapPlayersAtIndices(uint16_t prevFileIdx, uint16_t currFileIdx,
                                       uint16_t prevBreakPos, uint16_t currBreakPos)
{
    /* Check if full swap needed (cross-boundary or either >= 16) */
    int needFullSwap = 0;
    if (prevBreakPos >= 16 || currBreakPos >= 16) {
        needFullSwap = 1;
    } else if (prevBreakPos >= 11 && currBreakPos < 11) {
        needFullSwap = 1;
    } else if (currBreakPos >= 11 && prevBreakPos < 11) {
        needFullSwap = 1;
    }

    if (needFullSwap) {
        /* Full swap: get player pointers via GetPlayerAtIndex.
         * teamNum must be the BE-packed (country<<8)|ordinal that
         * GetPlayerAtIndex compares against selTeamNumber. The first
         * two bytes of TeamFile are the {countryNumber, teamOrdinal}
         * byte pair — reading them as a host-native uint16 returns
         * (ordinal<<8)|country on LE, which never matches selTeamNumber.
         * The lookup then falls through to the non-user-team branch
         * and panics 0x1146 whenever prevBreakPos or currBreakPos
         * is >= 16 — i.e. any swap involving the bench/trialist
         * slots, including subbing an injured player back in. */
        TeamFileEx *squadTf = (TeamFileEx *)_squadTeamFilePtr;
        uint16_t teamNum = PACK_BE_U16(squadTf->team.countryNumber,
                                       squadTf->team.teamOrdinal);
        uint8_t *prevBlockPtr = GetPlayerAtIndex(teamNum, prevBreakPos);
        uint16_t prevFIdx = _cGetPlayerAtIndexFileIdx;

        uint8_t *currBlockPtr = GetPlayerAtIndex(teamNum, currBreakPos);
        uint16_t currFIdx = _cGetPlayerAtIndexFileIdx;

        /* Swap playerPositionsArray entries */
        uint8_t tmp = _playerPositionsArray[currFIdx];
        _playerPositionsArray[currFIdx] = _playerPositionsArray[prevFIdx];
        _playerPositionsArray[prevFIdx] = tmp;

        /* Swap shirt numbers (block_ptr + 0x4E = PlayerFile.shirtNumber) */
        PlayerFile *prevPf = (PlayerFile *)(prevBlockPtr + 76);
        PlayerFile *currPf = (PlayerFile *)(currBlockPtr + 76);
        int8_t tmpShirt = currPf->shirtNumber;
        currPf->shirtNumber = prevPf->shirtNumber;
        prevPf->shirtNumber = tmpShirt;
    }

    /* Always swap player number array entries at break positions */
    uint8_t *numArray;
    if (_gameType == 4) {   /* GAME_TYPE_CAREER */
        numArray = _currentMatchPlayers;
    } else {
        numArray = ((TeamFile *)_squadTeamFilePtr)->playerNumbers;
    }
    uint8_t tmpNum = numArray[prevBreakPos];
    numArray[prevBreakPos] = numArray[currBreakPos];
    numArray[currBreakPos] = tmpNum;
}

/* SquadCommon (SWOS2.s line 49009)
 * Menu callback for squad entry selection.  Computes the selected
 * slot from entry y-coordinate delta, resolves via
 * GetPlayerInfoBreakPosition, handles swap or select/deselect. */
__attribute__((used, externally_visible))
void SquadCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    uint16_t d6 = a5->ordinal - _menuEntries[28].ordinal;
    uint16_t d1 = d6;

    TeamFileEx *teamPtr = (TeamFileEx *)_squadTeamFilePtr;
    uint16_t outSlot, outPlayerIndex;
    GetPlayerInfoBreakPosition(teamPtr, d1, &outSlot, &outPlayerIndex);
    d6 = outSlot;
    uint16_t d7 = outPlayerIndex;

    if ((int16_t)_squadPrevSelectedPlayerIndex >= 0) {
        if (d7 != _squadPrevSelectedPlayerIndex) {
            /* Swap the two players */
            squadSwapPlayersAtIndices(
                _squadPrevSelectedPlayerIndex, d7,
                _squadPrevSelectedBreakPos, d6);
            SetBenchPlayersNumbers((TeamFileEx *)_squadTeamFilePtr);
            ((TeamFile *)_squadTeamFilePtr)->playerValuesDirty = TEAM_VALUES_DIRTY;
            SquadCommitPositionsAndUpdateSkill();
            _squadPrevSelectedPlayerIndex = 0xFFFF;
            DrawMenu();
            return;
        }
        /* Same player selected twice → deselect */
        d7 = 0xFFFF;
    }

    _squadPrevSelectedPlayerIndex = d7;
    _squadPrevSelectedBreakPos = d6;

    MenuEntry *oldEntry = _squadPrevHighlightedMenuEntryPtr;
    _squadPrevHighlightedMenuEntryPtr = a5;
    if (oldEntry != (MenuEntry *)0) {
        CallDrawMenuItem(oldEntry);
    }
    CallDrawMenuItem(a5);
    SquadMenuSavePlayerPositions();
    SquadRedrawPlayerListAndCheckMark();
}

/* ================================================================
 * Chunk 209: SquadInit + SquadFinish (2 → menus.c)
 * ================================================================ */

extern uint16_t _isNationalSquad;
extern uint16_t _squadDisplayCount;
extern TeamFileEx _careerTeam;
extern uint8_t *_currentCompetitionPtr;
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_quaternaryCompetitionPtr;
extern void *_countriesTable[];
extern void *_playerPositionsStringTable[];
extern uint8_t _aClubs[];
extern uint16_t _isNationalSelector;
extern void SetCurrentEntry(uint16_t entry);
extern uint8_t *GetCompetitionAbbrevStr(uint8_t *competition);
extern void GetBankBalance(uint8_t *dst);
extern uint8_t _shortCountryNames[];

/* Helper: set the low nibble of the bg field's high word (68k big-endian color index) */
static void setBgColorIndex(MenuEntry *e, uint16_t colorIdx) {
    e->bgColor = (e->bgColor & 0xFFF0) | colorIdx;
}

/* SquadUpdateMenuState (SWOS2.s line 48491)
 * Updates squad menu entry visibility, colors, and strings based on
 * current mode (view vs edit), competition state, and team status. */
__attribute__((used, externally_visible))
void SquadUpdateMenuState(void)
{
    UpdateBigCheckMark();
    TeamFile *tf = (TeamFile *)_squadTeamFilePtr;

    /* Entry 2: bg color based on team type byte (offset 4) */
    setBgColorIndex(&_menuEntries[2], 12); /* ORANGE_TO_BROWN */
    if (tf->teamControls != 1) {
        setBgColorIndex(&_menuEntries[2], 11); /* BLUE_TO_PURPLE */
        if (tf->teamControls != 2) {
            setBgColorIndex(&_menuEntries[2], 13); /* BRIGHT_BLUE_TO_BLUE */
        }
    }

    /* Entry 3: set fg string to "(COUNTRY)" */
    {
        uint8_t *dst = (uint8_t *)_menuEntries[3].fg;
        uint8_t countryIdx = tf->countryNumber;
        uint8_t *countryStr = (uint8_t *)_countriesTable[countryIdx];
        *dst++ = '(';
        dst = StringCopy(countryStr + 1, dst);
        *dst++ = ')';
        *dst = 0;
    }

    /* Entry 25: fg = team writesZeroHere field (ENTRY_NUMBER: write high word) */
    _menuEntries[25].fgNumber = (uint16_t)(uint8_t)tf->writesZeroHere;

    /* Entries 15, 13 initially invisible */
    _menuEntries[15].isInvisible = 1;
    _menuEntries[13].isInvisible = 1;
    if (_isSquadViewMode == 0) {
        _menuEntries[13].isInvisible = 0;
    }

    /* Entry 14 initially invisible */
    _menuEntries[14].isInvisible = 1;
    {
        if (tf->countryNumber >= 0x50 && tf->countryNumber <= 0x55) {
            /* do nothing — keep invisible */
        } else if (_isSquadViewMode == 0) {
            _menuEntries[14].isInvisible = 0;
        }
    }

    if (_isSquadViewMode != 0) {
        /* ============ SQUAD VIEW MODE ============ */
        _menuEntries[6].isInvisible = 1;

        /* Asm: cmp.w (a6),d0 — compares selTeamNumber with packed team number at offset 0 */
        if ((int16_t)_selTeamNumber != -1 &&
            _selTeamNumber == PACK_BE_U16(tf->countryNumber, tf->teamOrdinal)) {
            _menuEntries[26].isInvisible = 0;
            _menuEntries[27].isInvisible = 0;
            _menuEntries[27].fg = (uintptr_t)_removedPlayerStatAccum;
        } else {
            _menuEntries[26].isInvisible = 1;
            _menuEntries[27].isInvisible = 1;
        }

        _menuEntries[24].isInvisible = 0;
        _menuEntries[25].isInvisible = 0;

        /* Entries 16-22: visible by default */
        {
            int16_t i;
            for (i = 16; i <= 22; i++)
                _menuEntries[i].isInvisible = 0;
        }

        _menuEntries[8].isInvisible = 1;
        _menuEntries[9].isInvisible = 1;

        if (_isNationalSquad != 0) {
            /* National squad view: hide competition entries 16-22, stats entries */
            int16_t i;
            for (i = 16; i <= 22; i++)
                _menuEntries[i].isInvisible = 1;
            _menuEntries[26].isInvisible = 1;
            _menuEntries[27].isInvisible = 1;
            _menuEntries[24].isInvisible = 1;
            _menuEntries[25].isInvisible = 1;
            goto afterEDE;
        }

        /* Check competition pointers for view mode */
        if (_quaternaryCompetitionPtr == 0 && _secondaryCompetitionPtr == 0 && _tertiaryCompetitionPtr == 0) {
            _menuEntries[18].isInvisible = 1;
            _menuEntries[17].isInvisible = 1;
            _menuEntries[19].isInvisible = 1;
            _menuEntries[21].isInvisible = 1;
            goto afterEDE;
        }

        if (_quaternaryCompetitionPtr == 0) {
            _menuEntries[21].isInvisible = 1;
        }

        if (_secondaryCompetitionPtr == 0) {
            _menuEntries[17].isInvisible = 1;
            _menuEntries[19].isInvisible = 1;
            goto afterEDE;
        }

        if (_tertiaryCompetitionPtr == 0) {
            _menuEntries[17].isInvisible = 1;
            goto afterEDE;
        }

        /* All three non-null: check national selector */
        if (_isNationalSelector != 0 &&
            _secondaryCompetitionPtr != 0 &&
            _tertiaryCompetitionPtr != 0) {
            _menuEntries[17].isInvisible = 1;
            _menuEntries[20].isInvisible = 0;
            _menuEntries[20].x = _menuEntries[17].x;
            _menuEntries[20].x += 11;
            goto afterEDE;
        }

        _menuEntries[19].isInvisible = 1;
        goto afterEDE;

    } else {
        /* ============ SQUAD EDIT MODE ============ */
        _menuEntries[6].isInvisible = 1;

        if (_gameType == 4 &&
            _squadTeamFilePtr == (uintptr_t)&_careerTeam &&
            _isNationalSelector == 0) {
            _menuEntries[6].isInvisible = 0;
        } else {
            /* teamSkillDiff < 0: adjust entry 8 and 9 positions */
            if ((int16_t)_teamSkillDiffAfterChange < 0) {
                _menuEntries[8].x = 0xEE;
                _menuEntries[8].dwWidth = 0x3C;
                _menuEntries[9].x = 0xEE;
                _menuEntries[9].dwWidth = 0x3C;

                if (_isNationalSquad != 0) {
                    _menuEntries[8].x -= 14;
                    _menuEntries[8].y = 0xE5;
                    _menuEntries[9].x -= 14;
                    _menuEntries[9].y = 0xF5;
                }
            }
        }

        /* Entry 6: bg color and disabled state */
        setBgColorIndex(&_menuEntries[6], 14); /* YELLOW_GREEN_BLACK */
        _menuEntries[6].isDisabled = 0;

        if (tf->andWith0xFE & 1) {
            setBgColorIndex(&_menuEntries[6], 9); /* PINK_TO_BROWN */
            _menuEntries[6].isDisabled = 1;
        }

        if ((int16_t)_teamSkillDiffAfterChange >= 0) {
            _menuEntries[6].x = 0xEE;
            _menuEntries[6].dwWidth = 0x3C;
            _menuEntries[9].x = 0xEE;
            _menuEntries[9].dwWidth = 0x3C;
            _menuEntries[9].y = 0xDD;

            {
                if (tf->teamControls != 1 &&
                    tf->countryNumber >= 0x50 && tf->countryNumber <= 0x55) {
                    _menuEntries[9].x -= 14;
                }
            }
        }

        /* Entries 26,27 invisible */
        _menuEntries[26].isInvisible = 1;
        _menuEntries[27].isInvisible = 1;

        /* Entries 16-22 invisible */
        {
            int16_t i;
            for (i = 16; i <= 22; i++)
                _menuEntries[i].isInvisible = 1;
        }

        /* Entries 8,9 visible */
        _menuEntries[8].isInvisible = 0;
        _menuEntries[9].isInvisible = 0;

        if ((int16_t)_teamSkillDiffAfterChange < 0) {
            _menuEntries[24].isInvisible = 1;
            _menuEntries[25].isInvisible = 1;

            if (_isNationalSquad != 0) {
                _menuEntries[24].isInvisible = 0;
                _menuEntries[25].isInvisible = 0;
                /* Make entries 16-22 visible again */
                {
                    int16_t i;
                    for (i = 16; i <= 22; i++)
                        _menuEntries[i].isInvisible = 0;
                }
                _menuEntries[13].isInvisible = 1;
                goto viewModeCompChecks;
            }
        } else {
            _menuEntries[24].isInvisible = 0;
            _menuEntries[25].isInvisible = 0;
        }
    }

afterEDE:
    /* Bank balance (entry 12) */
    if (_isSquadViewMode == 0 &&
        _gameType == 4 &&
        _isNationalSelector == 0) {
        TeamFileEx *teamPtr = (TeamFileEx *)_squadTeamFilePtr;
        if (PACK_BE_U16(teamPtr->team.countryNumber,
                        teamPtr->team.teamOrdinal) == _selTeamNumber) {
            _menuEntries[12].isInvisible = 0;
            GetBankBalance((uint8_t *)_menuEntries[12].fg);
            goto afterBankBalance;
        }
    }
    _menuEntries[12].isInvisible = 1;

afterBankBalance:
    /* Entry 2: fg = team name, Entry 5: fg = coach name */
    _menuEntries[2].fg = (uintptr_t)tf->teamName;
    _menuEntries[5].fg = (uintptr_t)tf->coachName;
    return;

viewModeCompChecks:
    /* Jumped to from edit mode national squad path — runs view mode competition checks */
    if (_quaternaryCompetitionPtr == 0 && _secondaryCompetitionPtr == 0 && _tertiaryCompetitionPtr == 0) {
        _menuEntries[18].isInvisible = 1;
        _menuEntries[17].isInvisible = 1;
        _menuEntries[19].isInvisible = 1;
        _menuEntries[21].isInvisible = 1;
        goto afterEDE;
    }
    if (_quaternaryCompetitionPtr == 0) {
        _menuEntries[21].isInvisible = 1;
    }
    if (_secondaryCompetitionPtr == 0) {
        _menuEntries[17].isInvisible = 1;
        _menuEntries[19].isInvisible = 1;
        goto afterEDE;
    }
    if (_tertiaryCompetitionPtr == 0) {
        _menuEntries[17].isInvisible = 1;
        goto afterEDE;
    }
    if (_isNationalSelector != 0 &&
        _secondaryCompetitionPtr != 0 &&
        _tertiaryCompetitionPtr != 0) {
        _menuEntries[17].isInvisible = 1;
        _menuEntries[20].isInvisible = 0;
        _menuEntries[20].x = _menuEntries[17].x;
        _menuEntries[20].x += 11;
        goto afterEDE;
    }
    _menuEntries[19].isInvisible = 1;
    goto afterEDE;
}

/* ================================================================
 * Chunk 211: SquadAfterDrawCommon (1 → menus.c)
 * ================================================================ */

extern uint8_t _playerPositionsArray[];
extern uint8_t _oldPlayerPositionsArray[];
extern uint16_t _squadRowMode;
extern uint16_t _squadSelectedPlayerIndex;
extern uintptr_t _squadSelectedPlayerPtr;
extern uint8_t _fundsBuffer[];
extern uint8_t _careerUnavailablePlayerNames[];

/* Helper: set color index on both entries 46 and 48 */
static void setColorPair46_48(uint16_t colorIdx) {
    setBgColorIndex(&_menuEntries[46], colorIdx);
    setBgColorIndex(&_menuEntries[48], colorIdx);
}

/* SquadAfterDrawCommon (SWOS2.s line 48777)
 * AfterDraw callback for squad menu player rows. Sets row colors,
 * fills in player data for entries 46-63, then draws them. */
__attribute__((used, externally_visible))
void SquadAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    uint16_t d7 = a5->ordinal - 0x1C;
    TeamFileEx *teamBase = (TeamFileEx *)_squadTeamFilePtr;
    TeamFile *tf = &teamBase->team;
    uint16_t d5;

    /* Set row background colors based on row index and team type */
    if (d7 == 0x10) {
        d5 = 1;
        setColorPair46_48(8);
    } else {
        d5 = 0;
        if (d7 >= 11) {
            /* Substitutes */
            if (tf->teamControls == 1) {
                setColorPair46_48(12);
            } else {
                setColorPair46_48(11);
            }
        } else {
            /* Starting 11 */
            if (tf->teamControls == 1) {
                setColorPair46_48(10);
            } else if (d7 == 0) {
                setColorPair46_48(14); /* goalkeeper */
            } else {
                setColorPair46_48(13);
            }
        }
    }

    /* Get player info at this row.
     * GetPlayerInfoBreakPosition returns a base pointer;
     * PlayerFile data starts sizeof(TeamFile) bytes in. */
    uint16_t d6_breakPos, d4_fileIdx;
    uint8_t *player = GetPlayerInfoBreakPosition(teamBase, d7, &d6_breakPos, &d4_fileIdx);
    PlayerFile *pf = (PlayerFile *)(player + sizeof(TeamFile));

    /* Check if injured — DISPLAY decision (row goes red), so use the honest
     * status rather than IsPlayerInjured, whose testers'-rule (test_rules.h)
     * beta override is a GAMEPLAY-availability override only. See the same
     * split in play_match_menu.c DrawPlayerEntries / edit_tactics.c. */
    uint16_t injured = IsPlayerInjuredForDisplay((PlayerFile *)player, (int16_t)_squadRowMode);
    if (injured != 0) {
        setColorPair46_48(10);
    }

    /* Check if this is the previously selected player */
    if (d4_fileIdx == _squadPrevSelectedPlayerIndex) {
        setColorPair46_48(6);
    }

    _squadSelectedPlayerIndex = d4_fileIdx;
    _squadSelectedPlayerPtr = (uintptr_t)player;

    uint16_t yPos = a5->y;

    /* Entry 46: status sprite */
    MenuEntry *e = &_menuEntries[46];
    e->y = yPos;
    SetSquadMenuEntryStatusSprite(e, (PlayerFile *)player, (uint16_t)_squadRowMode);

    /* Check position arrow (up/down indicator) */
    if (d6_breakPos < 11) {
        uint8_t curPos = _playerPositionsArray[d4_fileIdx];
        uint8_t oldPos = _oldPlayerPositionsArray[d4_fileIdx];
        if (curPos < oldPos) {
            e->fgWord = 0xD1; /* down arrow — ENTRY_SPRITE2: write high word */
        } else if (curPos > oldPos) {
            e->fgWord = 0xD0; /* up arrow — ENTRY_SPRITE2: write high word */
        }
    }

    /* Entry 47: shirt number (hidden if d5==1 i.e. row 16) */
    e = &_menuEntries[47];
    e->isInvisible = 1;
    if (d5 == 0) {
        /* ENTRY_NUMBER renderer reads e->fgNumber (the first 2 bytes of
         * the fg union slot). Direct named-field write replaces the
         * Amiga-shorthand `entry + 38` byte arithmetic, which on 64-bit
         * landed on `content` instead of `fg`. */
        e->fgNumber = (uint16_t)(uint8_t)pf->shirtNumber;
        e->isInvisible = 0;
    }
    e->y = yPos;

    /* Entry 48: player name */
    e = &_menuEntries[48];
    e->fg = (uintptr_t)(uint8_t *)pf->playerName;
    e->y = yPos;

    /* Entry 49: position string */
    e = &_menuEntries[49];
    {
        uint8_t posAndFace = pf->positionAndFace;
        uint16_t posIdx = (posAndFace & PF_POSITION_MASK) >> 3;
        e->fg = (uintptr_t)_playerPositionsStringTable[posIdx / 4];
    }
    e->y = yPos;

    /* Entry 50: top skills string */
    e = &_menuEntries[50];
    FormTopSkillsString((uint8_t *)e->fg, (PlayerFile *)player);
    e->y = yPos;

    /* Entries 51-54: invisible */
    _menuEntries[51].isInvisible = 1; _menuEntries[51].y = yPos;
    _menuEntries[52].isInvisible = 1; _menuEntries[52].y = yPos;
    _menuEntries[53].isInvisible = 1; _menuEntries[53].y = yPos;
    _menuEntries[54].isInvisible = 1; _menuEntries[54].y = yPos;

    /* Entry 54: nationality — show if team NOT in [0x50..0x55] and not view mode */
    {
        if (tf->countryNumber >= 0x50 && tf->countryNumber <= 0x55) {
            /* team in range — skip nationality */
        } else if (_isSquadViewMode == 0) {
            uint8_t *dst = (uint8_t *)_menuEntries[54].fg;
            uint8_t nat = (uint8_t)pf->nationality;
            uint16_t natIdx = (uint16_t)nat * 3;
            *dst++ = '(';
            dst[0] = _shortCountryNames[natIdx];
            dst[1] = _shortCountryNames[natIdx + 1];
            dst[2] = _shortCountryNames[natIdx + 2];
            dst += 3;
            *dst++ = ')';
            *dst = 0;
            _menuEntries[54].isInvisible = 0;
        }
    }
    _menuEntries[54].y = yPos;

    /* Entry 55: goals/stats */
    e = &_menuEntries[55];
    e->isInvisible = 1;
    {
        int16_t skillDiff = (int16_t)_teamSkillDiffAfterChange;
        if (skillDiff < 0) {
            if (_isNationalSquad == 0 && _isSquadViewMode == 0) {
                uint16_t sum = (uint16_t)pf->goalStats[0]
                             + (uint16_t)pf->goalStats[1]
                             + (uint16_t)pf->goalStats[2]
                             + (uint16_t)pf->goalStats[3];
                /* ENTRY_NUMBER: renderer reads high word of fg */
                e->fgNumber = sum;
                e->isInvisible = 0;
            }
        } else {
            e->fgNumber = (uint16_t)pf->goalStats[skillDiff];
            e->isInvisible = 0;
        }
    }
    e->y = yPos;

    /* Entry 56: invisible */
    _menuEntries[56].isInvisible = 1;
    _menuEntries[56].y = yPos;

    /* Entry 57: invisible by default */
    _menuEntries[57].isInvisible = 1;

    if (_isSquadViewMode == 0) {
        TeamFileEx *teamGeneral = (TeamFileEx *)_squadTeamFilePtr;
        uint16_t isNat = IsTeamNational(teamGeneral);
        if (isNat != 0) {
            /* dst must be the menu entry ptr (entry 57), NOT the team file.
             * Asm: a1 = current entry (57), a0 = player data. Passing teamGeneral
             * here would write to TeamFile+0x24, zeroing coachName[0..1]. */
            InitPlayerValueDisplayStruct(player, (uint8_t *)&_menuEntries[57]);
        } else {
            FormatPlayerValueOrStatusText(player, _fundsBuffer);
            _menuEntries[57].fg = (uintptr_t)_fundsBuffer;
        }
        _menuEntries[57].isInvisible = 0;

        /* Color based on PlayerFile.playerValuesDirty and positionAndFace */
        uint16_t colorIdx = 2;
        if ((uint8_t)pf->form == PLAYER_FORM_TRIALIST) {
            colorIdx = 13;
        } else if ((uint8_t)pf->form == PLAYER_FORM_RESERVE) {
            colorIdx = 1;
        } else if (pf->positionAndFace & PF_LOAN) {
            colorIdx = 15;
        }
        {
            uint16_t sf = _menuEntries[57].stringFlags;
            sf = (sf & 0xFFF0) | colorIdx;
            _menuEntries[57].stringFlags = sf;
        }
    }
    _menuEntries[57].y = yPos;

    /* Entry 58: invisible by default */
    _menuEntries[58].isInvisible = 1;
    _menuEntries[58].y = yPos;

    /* National squad view: show club names in entry 58 */
    if (_isNationalSquad != 0 && _isSquadViewMode != 0) {
        uint8_t *dst = (uint8_t *)_menuEntries[58].fg;
        uint8_t *clubName = _careerUnavailablePlayerNames + (uint16_t)(d4_fileIdx * 17);
        *dst++ = '(';
        dst = StringCopy(clubName, dst);
        *dst++ = ')';
        *dst = 0;
        _menuEntries[58].isInvisible = 0;
    }

    /* Entries 59-63: show competition goals when exactly one of
     * isNationalSquad/isSquadViewMode is set (XOR logic) */
    if ((_isNationalSquad != 0) == (_isSquadViewMode != 0)) {
        /* Both set or both clear: hide entries 59-63 */
        _menuEntries[59].isInvisible = 1; _menuEntries[59].y = yPos;
        _menuEntries[60].isInvisible = 1; _menuEntries[60].y = yPos;
        _menuEntries[61].isInvisible = 1; _menuEntries[61].y = yPos;
        _menuEntries[62].isInvisible = 1; _menuEntries[62].y = yPos;
        _menuEntries[63].isInvisible = 1; _menuEntries[63].y = yPos;
    } else {
        /* Show competition goal counts */
        /* Entry 59: first competition goals (if any secondary competitions exist) */
        e = &_menuEntries[59];
        e->isInvisible = 1;
        if (_quaternaryCompetitionPtr != 0 || _secondaryCompetitionPtr != 0 || _tertiaryCompetitionPtr != 0) {
            e->fgNumber = (uint16_t)pf->goalStats[0];
            e->isInvisible = 0;
        }
        e->y = yPos;

        /* Entry 60: secondary competition goals */
        e = &_menuEntries[60];
        e->isInvisible = 1;
        if (_secondaryCompetitionPtr != 0) {
            e->fgNumber = (uint16_t)pf->goalStats[1];
            e->isInvisible = 0;
        }
        e->y = yPos;

        /* Entry 61: tertiary competition goals */
        e = &_menuEntries[61];
        e->isInvisible = 1;
        if (_tertiaryCompetitionPtr != 0) {
            e->fgNumber = (uint16_t)pf->goalStats[2];
            e->isInvisible = 0;
        }
        e->y = yPos;

        /* Entry 62: quaternary competition goals */
        e = &_menuEntries[62];
        e->isInvisible = 1;
        if (_quaternaryCompetitionPtr != 0) {
            e->fgWord = (uint16_t)pf->goalStats[3];
            e->isInvisible = 0;
        }
        e->y = yPos;

        /* Entry 63: total goals (sum of all 4) */
        e = &_menuEntries[63];
        {
            uint16_t total = (uint16_t)pf->goalStats[0]
                           + (uint16_t)pf->goalStats[1]
                           + (uint16_t)pf->goalStats[2]
                           + (uint16_t)pf->goalStats[3];
            e->fgWord = total;
            e->isInvisible = 0;
        }
        e->y = yPos;
    }

    /* Draw 18 entries starting from entry 46 */
    DrawMultipleItems(18, &_menuEntries[46]);
}

/* SquadFinish (SWOS2.s line 48543)
 * Called as the squad menu's onReturn/setup callback.
 * Adjusts menu entry positions, sets display count, sets up
 * competition abbreviation strings, etc. */
__attribute__((used, externally_visible))
void SquadFinish(void)
{
    TeamFile *teamPtr = (TeamFile *)_squadTeamFilePtr;

    /* If not national squad and team countryNumber in [0x50..0x55],
     * shift entries 13, 55, 24, 25 left by 13 */
    if (_isNationalSquad == 0) {
        if (teamPtr->countryNumber >= 0x50 && teamPtr->countryNumber <= 0x55) {
            _menuEntries[13].x -= 13;
            _menuEntries[55].x -= 13;
            _menuEntries[24].x -= 13;
            _menuEntries[25].x -= 13;
        }
    }

    CheckIsTeamNational();
    SetBenchPlayersNumbers((TeamFileEx *)_squadTeamFilePtr);

    /* Determine squad display count */
    uint16_t displayCount;
    if ((int16_t)_selTeamNumber == -1 ||
        _selTeamNumber != BE_READ_U16(teamPtr)) {
        displayCount = 16;
    } else {
        displayCount = _careerNumPlayers;
    }
    _squadDisplayCount = displayCount;

    /* Reset selection state */
    _squadPrevHighlightedMenuEntryPtr = (MenuEntry *)0;
    _squadPrevSelectedPlayerIndex = 0xFFFF;

    uint8_t *a6 = (uint8_t *)_squadTeamFilePtr;

    /* Entries 44, 45 visibility: hidden by default, shown for extra players */
    _menuEntries[44].isInvisible = 1;
    _menuEntries[45].isInvisible = 1;
    int16_t extraPlayers = (int16_t)_squadDisplayCount - 16;
    if (extraPlayers != 0) {
        _menuEntries[44].isInvisible = 0;
        if (extraPlayers != 1) {
            _menuEntries[45].isInvisible = 0;
        }
    }

    /* Entry 21: set fg to country-specific position string.
     * Original asm reads selTeamNumber's high byte as countryIdx, but that
     * global is stale when reaching this menu via StatsViewSquads (the
     * team-pick menu doesn't update it). Use teamPtr->countryNumber, which
     * is the correct authoritative source for the team being displayed. */
    {
        uint8_t countryIdx = teamPtr->countryNumber;
        uint8_t *countryEntry = (uint8_t *)_countriesTable[countryIdx];
        int8_t langCode = (int8_t)countryEntry[0];
        int16_t langIdx = (int16_t)langCode;
        /* playerPositionsStringTable + $20 = index 8, then + langIdx */
        _menuEntries[21].fg = (uintptr_t)_playerPositionsStringTable[8 + langIdx];
    }

    if (_isNationalSquad != 0) {
        /* Set competition abbreviation strings */
        uint8_t *abbrev = GetCompetitionAbbrevStr((uint8_t *)_currentCompetitionPtr);
        MenuEntry *target = &_menuEntries[22]; /* default target if no secondary */

        if (_secondaryCompetitionPtr != 0) {
            _menuEntries[18].fg = (uintptr_t)abbrev;
            abbrev = GetCompetitionAbbrevStr((uint8_t *)_secondaryCompetitionPtr);
            target = &_menuEntries[19];

            if (_tertiaryCompetitionPtr != 0) {
                _menuEntries[19].fg = (uintptr_t)abbrev;
                abbrev = GetCompetitionAbbrevStr((uint8_t *)_tertiaryCompetitionPtr);
                _menuEntries[20].fg = (uintptr_t)abbrev;
                _menuEntries[20].isInvisible = 0;
                goto skipFgStore;
            }
        }
        target->fg = (uintptr_t)abbrev;
    skipFgStore:;
    }

    _isSquadViewMode = 0;
    SquadUpdateMenuState();

    /* Entry 8: GOALS/CLUBS label visibility */
    _menuEntries[8].isInvisible = 1;
    if ((int16_t)_teamSkillDiffAfterChange < 0) {
        _menuEntries[8].isInvisible = 0;
    }

    _menuEntries[8].fg = (uintptr_t)_aGoals;
    if (_isNationalSquad != 0) {
        _menuEntries[8].fg = (uintptr_t)_aClubs;
    }

    /* If team type byte (offset 4) == 1 */
    a6 = (uint8_t *)_squadTeamFilePtr;
    if (a6[4] == 1) {
        _menuEntries[9].isInvisible = 1;
        _menuEntries[10].isInvisible = 0;
        SetCurrentEntry(10);
        if (a6[0] >= 0x50 && a6[0] <= 0x55) {
            _menuEntries[10].x = 0xDF;
        }
    }

    SquadUpdateGoalsMenuText();
    SquadCommitPositionsAndUpdateSkill();
    SquadMenuSavePlayerPositions();
}

/* SquadInit (SWOS2.s line 48425)
 * Called as the squad menu's onInit callback.
 * Sets isNationalSquad flag, adjusts menu entry x positions
 * based on which competition pointers are active and
 * teamSkillDiffAfterChange, then falls through to SquadFinish. */
__attribute__((used, externally_visible))
void SquadInit(void)
{
    /* Set national squad flag */
    _isNationalSquad = 0;
    if (_squadTeamFilePtr == (uintptr_t)&_careerTeam && _isNationalSelector != 0) {
        _isNationalSquad = 1;
    }

    /* If all three competition pointers are zero, shift entries right */
    if (_quaternaryCompetitionPtr == 0 && _secondaryCompetitionPtr == 0 && _tertiaryCompetitionPtr == 0) {
        _menuEntries[59].x += 20;
        _menuEntries[60].x += 20;
        _menuEntries[61].x += 20;
        _menuEntries[62].x += 20;
        _menuEntries[18].x += 20;
        _menuEntries[19].x += 20;
        _menuEntries[17].x += 20;
        _menuEntries[21].x += 20;
    }

    /* If quaternary is null, shift more entries */
    if (_quaternaryCompetitionPtr == 0) {
        _menuEntries[59].x += 20;
        _menuEntries[60].x += 20;
        _menuEntries[61].x += 20;
        _menuEntries[18].x += 20;
        _menuEntries[19].x += 20;
        _menuEntries[17].x += 20;
    }

    /* If tertiary is null */
    if (_tertiaryCompetitionPtr == 0) {
        _menuEntries[59].x += 20;
        _menuEntries[60].x += 20;
        _menuEntries[18].x += 20;
        _menuEntries[19].x += 20;
    }

    /* If secondary is null */
    if (_secondaryCompetitionPtr == 0) {
        _menuEntries[59].x += 20;
        _menuEntries[18].x += 20;
    }

    /* Negative teamSkillDiff: compute shift and apply to many entries */
    if ((int16_t)_teamSkillDiffAfterChange < 0) {
        int16_t d1 = (_menuEntries[59].x - 0xC7) >> 1;

        if (_isNationalSquad != 0) {
            d1 += 8;
            _menuEntries[16].x -= 8;
        }

        _menuEntries[59].x -= d1;
        _menuEntries[60].x -= d1;
        _menuEntries[61].x -= d1;
        _menuEntries[62].x -= d1;
        _menuEntries[63].x -= d1;
        _menuEntries[17].x -= d1;
        _menuEntries[18].x -= d1;
        _menuEntries[19].x -= d1;
        _menuEntries[21].x -= d1;
        _menuEntries[22].x -= d1;
        _menuEntries[24].x -= d1;
        _menuEntries[25].x -= d1;
        _menuEntries[26].x -= d1;
        _menuEntries[27].x -= d1;
    }

    /* Non-negative teamSkillDiff: adjust entries 24, 25, 9 */
    if ((int16_t)_teamSkillDiffAfterChange >= 0) {
        _menuEntries[24].x -= 0x32;
        _menuEntries[24].y -= 0x0A;
        _menuEntries[25].x -= 0x32;
        _menuEntries[25].y -= 0x0A;
        _menuEntries[9].x -= 0x1F;
    }

    /* Fall through to SquadFinish */
    SquadFinish();
}

/* ================================================================
 * Options menu callbacks (Chunk 113, SWOS2.s lines 47471-47518)
 * ================================================================ */

extern uint16_t _showPrereleaseCounter;

/* OptionsMenuBeforeDraw: clears prerelease counter on menu entry */
__attribute__((used, externally_visible))
void OptionsMenuBeforeDraw(void)
{
    extern int _gCpuTeamsGetCards;
    extern uint16_t _cpuCardsOption;

    _showPrereleaseCounter = 0;
    /* The CPU CARDS row (21/22, every target) draws from the uint16_t mirror:
     * re-sync it here so the row always tells the truth about the game flag. */
    _cpuCardsOption = (uint16_t)(_gCpuTeamsGetCards != 0);
    /* One row, two meanings: CONTROLS (entries 15/16) opens a JSON menu about the
     * keyboard's keys, which only SDL runs; PITCHES (17/18) picks the AGA Amiga's
     * pitch set. Each target shows its own and steps GAMEPLAY (14) and CPU CARDS
     * (22) to it; the OCS Amiga has neither, and steps over the row. EXIT (19)
     * links up to 22 in the JSON, which is right on every target. */
#ifdef SWOS_SDL_BUILD
    _menuEntries[17].isInvisible = 1;
    _menuEntries[18].isInvisible = 1;
#else
    _menuEntries[15].isInvisible = 1;
    _menuEntries[16].isInvisible = 1;
#ifdef AGA
    _menuEntries[14].downEntry = 18;
    _menuEntries[22].upEntry = 18;
#else
    _menuEntries[17].isInvisible = 1;
    _menuEntries[18].isInvisible = 1;
    _menuEntries[14].downEntry = 22;
    _menuEntries[22].upEntry = 14;
#endif
#endif
}

/* OPTIONS -> PITCHES: DOS / 2020 (pitch.c, _pitchStyleOption). The next match loads
 * the set; nothing else depends on it. */
__attribute__((used, externally_visible))
void ChangePitchStyle(MenuEntry *entry)
{
    extern uint16_t _pitchStyleOption;
    _pitchStyleOption ^= 1;
    CallDrawMenuItem(entry);
}

/* OPTIONS -> CONTROLS: the player's own keys and the press latch (src/controls_menu.c). */
__attribute__((used, externally_visible))
void ShowControlsMenu(void)
{
#ifdef SWOS_SDL_BUILD
    extern void ControlsMenu_Show(void);
    ControlsMenu_Show();
#endif
}

/* ChangePitchType: cycle pitchType 0→1→2→3→4→5→6→0xFFFE→0→... */
__attribute__((used, externally_visible))
void ChangePitchType(MenuEntry *entry)
{
    _pitchType++;
    if (_pitchType == 7)
        _pitchType = 0xFFFE; /* RANDOM */
    CallDrawMenuItem(entry);
}

/* ChangeGameLength: cycle gameLength 0→1→2→3→0→... */
__attribute__((used, externally_visible))
void ChangeGameLength(MenuEntry *entry)
{
    _gameLength++;
    if (_gameLength == 4)
        _gameLength = 0;
    CallDrawMenuItem(entry);
}

/* ChangeAutoReplays: toggle autoReplays */
__attribute__((used, externally_visible))
void ChangeAutoReplays(MenuEntry *entry)
{
    _autoReplays ^= 1;
    CallDrawMenuItem(entry);
}

/* ChangeAllPlTeamsEqual: toggle allPlayerTeamsEqual */
__attribute__((used, externally_visible))
void ChangeAllPlTeamsEqual(MenuEntry *entry)
{
    _allPlayerTeamsEqual ^= 1;
    CallDrawMenuItem(entry);
}

/* ChangeAutoSaveHighlights: toggle autoSaveHighlights */
__attribute__((used, externally_visible))
void ChangeAutoSaveHighlights(MenuEntry *entry)
{
    _autoSaveHighlights ^= 1;
    CallDrawMenuItem(entry);
}

/* ChangeMenuMusic: toggle menuMusic, restart music, redraw */
__attribute__((used, externally_visible))
void ChangeMenuMusic(MenuEntry *entry)
{
    _menuMusic ^= 1;
    TryRestartingMusic();
    CallDrawMenuItem(entry);
}

/* ================================================================
 * ChangeGameplayStyle — the OPTIONS menu's "GAMEPLAY" row
 *
 * Toggles AMIGA <-> DOS, which is the same switch as `--dos-gameplay` and
 * [dos] gameplay in SWOS.ini: the DOS release's 70fps physics, its timer
 * reloads and its formation set, against the Amiga's.
 *
 * Three things have to happen together, and the order matters:
 *
 *   1. _gameplayStyleOption is what the menu DRAWS from (a uint16_t, because
 *      that is what the string-table header reads); _gDosGameplay is what the
 *      GAME reads. Keep them in step or the row lies about the setting.
 *   2. ApplyGameStyle() republishes every derived constant — the speed factor,
 *      the twelve timer reloads, the formation set. Without it the flag moves
 *      and nothing else does. It is a pure write of constants, so calling it
 *      here is free; a match already in progress is not affected because
 *      InitGameRestoreTeams calls it again at kick-off.
 *   3. Persist, so the choice survives a restart the way every other row on
 *      this menu does. g_arcadeOpts/g_arcadeIniPath are the live options and
 *      the resolved ini path, published by main.c.
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* The switch itself, for this menu row and for the online lobby (GameplayStyle_Borrow).
 * `persist` writes it to SWOS.ini, as the OPTIONS row does; a borrowed style does not. */
void GameplayStyle_Set(int dos, int persist)
{
    extern int _gDosGameplay;
    extern uint16_t _gameplayStyleOption;
    extern void ApplyGameStyle(void);
    extern int _gDosView;
    extern PlatformInitOptions *g_arcadeOpts;
    extern const char *g_arcadeIniPath;

    _gameplayStyleOption = (uint16_t)(dos ? 1 : 0);
    _gDosGameplay = dos ? 1 : 0;

    ApplyGameStyle();

    /* 4. The VIEW follows the style -- one setting, [gameplay] style, since
     *    2026-09-29 -- unless a --view on the command line pinned it, which is the
     *    same rule main.c applies at startup:
     *        dosView = (view == DOS) || (view == AUTO && dos_gameplay)
     *    This is what carries the DOS release's 320x200 screen -- and with it the
     *    camera's vertical range, since SIM_CAMERA_MAX_Y is derived from
     *    _sim_view_height (display.h).
     *
     *    Only the FLAG moves here. The view itself is switched per scene, at
     *    kick-off, by game_loop.c's MatchViewEnter: the menus are 272 lines of
     *    Amiga artwork, so resizing to 200 while one is on screen cuts its
     *    bottom rows off -- which is exactly what applying it globally did
     *    (09547c4). The next match picks it up.
     *
     *    Everything else the style covers already tracks the flag live and
     *    needs nothing here: the frame rate through DosGameplay_TargetFps()
     *    (platform_sdl.c re-reads it per frame and re-arms the tick timer),
     *    the camera aim through SIM_CAM_AIM_HEIGHT, and the ball physics,
     *    goalkeeper reach, formations and stoppage timers through
     *    ApplyGameStyle above. */
    if (g_arcadeOpts) {
        _gDosView = (g_arcadeOpts->view_style == VIEW_STYLE_DOS) ||
                    (g_arcadeOpts->view_style == VIEW_STYLE_AUTO && _gDosGameplay);
        if (persist) {
            g_arcadeOpts->dos_gameplay = _gDosGameplay;
            if (g_arcadeIniPath)
                Settings_Save(g_arcadeOpts, g_arcadeIniPath);
        }
    }
}

/* An online game plays its HOST's style (p2p_link.c): the player who joins takes it
 * for as long as they are with that player, and GiveBack returns their own when they
 * leave -- never written to SWOS.ini, so joining a DOS game does not change anyone's
 * OPTIONS. Borrowing twice keeps the first style to give back. */
static int s_ownStyle = -1;

void GameplayStyle_Borrow(int dos)
{
    extern int _gDosGameplay;
    if (s_ownStyle < 0)
        s_ownStyle = _gDosGameplay;
    GameplayStyle_Set(dos, 0);
}

void GameplayStyle_GiveBack(void)
{
    if (s_ownStyle < 0)
        return;
    GameplayStyle_Set(s_ownStyle, 0);
    fprintf(stderr, "[p2p] back to this player's own style (%s)\n", s_ownStyle ? "DOS" : "AMIGA");
    s_ownStyle = -1;
}
#endif

void ChangeGameplayStyle(MenuEntry *entry)
{
#ifdef SWOS_SDL_BUILD
    /* The switch, the view and SWOS.ini: GameplayStyle_Set. */
    extern int _gDosGameplay;
    GameplayStyle_Set(!_gDosGameplay, 1);
#else
    extern int _gDosGameplay;
    extern uint16_t _gameplayStyleOption;
    extern void ApplyGameStyle(void);

    _gameplayStyleOption ^= 1;
    _gDosGameplay = (int)_gameplayStyleOption;

    ApplyGameStyle();
#endif

    CallDrawMenuItem(entry);
}

/* ================================================================
 * ChangeCpuCards — the OPTIONS menu's "CPU CARDS" row
 *
 * ON (default): a computer side is booked and sent off by the same rolls as
 * a human one. OFF: the ORIGINAL's rule, a team with no human controller is
 * never carded. The same switch as [gameplay] cpu_cards in SWOS.ini and
 * --cpu-cards; _gCpuTeamsGetCards (tackle_foul.c) is what the match reads,
 * _cpuCardsOption the uint16_t mirror the row draws from. Offline only by
 * nature -- online both sides are human, so the gate never fires there.
 * On SDL the choice persists to SWOS.ini like the GAMEPLAY row's; the Amiga
 * has no SWOS.ini, so there it lasts the session.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChangeCpuCards(MenuEntry *entry)
{
    extern int _gCpuTeamsGetCards;
    extern uint16_t _cpuCardsOption;

    _cpuCardsOption ^= 1;
    _gCpuTeamsGetCards = (int)_cpuCardsOption;

#ifdef SWOS_SDL_BUILD
    {
        extern PlatformInitOptions *g_arcadeOpts;
        extern const char *g_arcadeIniPath;
        if (g_arcadeOpts) {
            g_arcadeOpts->cpu_cards = _gCpuTeamsGetCards;
            if (g_arcadeIniPath)
                Settings_Save(g_arcadeOpts, g_arcadeIniPath);
        }
    }
#endif

    CallDrawMenuItem(entry);
}

/* ================================================================
 * Simple menu exit/draw callbacks (Chunk 114)
 * ================================================================ */

extern volatile uint16_t _viewSquadSelectedPlayer;
extern uint16_t _selectNationalTeamMenuResult;
extern uint16_t _teamSelected;
extern uint8_t _selectedFilename[];

/* CompetitionTopGoalScorersExit (line 54745): trivial exit */
__attribute__((used, externally_visible))
void CompetitionTopGoalScorersExit(void)
{
    SetExitMenuFlag();
}

/* TopGoalScorersExit (line 55243): trivial exit */
__attribute__((used, externally_visible))
void TopGoalScorersExit(void)
{
    SetExitMenuFlag();
}

/* BuyPlayerExit (line 75812): trivial exit */
__attribute__((used, externally_visible))
void BuyPlayerExit(void)
{
    SetExitMenuFlag();
}

/* ViewSquadExitContinue (line 73991): set no-selection, exit */
__attribute__((used, externally_visible))
void ViewSquadExitContinue(void)
{
    _viewSquadSelectedPlayer = 0xFFFF;
    SetExitMenuFlag();
}

/* ViewSquad21AfterDraw (line 74007): draw entry 20 (0x460/56=20) */
__attribute__((used, externally_visible))
void ViewSquad21AfterDraw(void)
{
    DrawMenuItem(&_menuEntries[20]);
}

/* SelectNationalTeamExit (line 78585): set no-selection, exit */
__attribute__((used, externally_visible))
void SelectNationalTeamExit(void)
{
    _selectNationalTeamMenuResult = 0xFFFF;
    SetExitMenuFlag();
}

/* SelectNationalTeam25AfterDraw (line 78612): draw entry 24 (0x540/56=24) */
__attribute__((used, externally_visible))
void SelectNationalTeam25AfterDraw(void)
{
    DrawMenuItem(&_menuEntries[24]);
}

/* SelTeamForTacticsExit (line 63673): set no-team, exit */
__attribute__((used, externally_visible))
void SelTeamForTacticsExit(void)
{
    _teamSelected = 0xFFFF;
    SetExitMenuFlag();
}

/* AbortSaveFile (line 68003): clear filename, exit */
__attribute__((used, externally_visible))
void AbortSaveFile(void)
{
    _selectedFilename[0] = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * CareerOver + ViewSquad + National team callbacks (Chunk 115)
 * ================================================================ */

extern uint16_t _currentSeasonShowing;
extern uint16_t _numNewJobOffers;
extern uint16_t _numJobOffers;
extern uint16_t _youreSacked;
extern TeamFile *_squadTeamPtr;
extern TeamFileEx _careerTeam;

/* CareerOverInit (line 58154): set default menu entry based on career state.
 *
 * The Amiga ends every branch (including the empty fall-through case) with
 * `bra.w *+4` which lands on the immediately-following CareerOverFinish
 * label — the function deliberately falls through into its onReturn
 * counterpart so both run on init. Without this, entries 0/1/2 (manager
 * name / team name / season-year slots at the top of the careerOver menu)
 * never get populated and stay as the STDMENUTEXT placeholder allocated
 * by UnpackMenu. */
extern void CareerOverFinish(void);

__attribute__((used, externally_visible))
void CareerOverInit(void)
{
    if (_currentSeasonShowing == 0x13) {
        SetCurrentEntry(6);
    } else if (_numNewJobOffers != 0 || _numJobOffers != 0) {
        SetCurrentEntry(5);
    } else if (_youreSacked != 0) {
        SetCurrentEntry(8);
    }
    /* fall through to CareerOverFinish (Amiga: bra.w *+4) */
    CareerOverFinish();
}

/* ViewSquadOnSelect21 (line 73995): rotate players and redraw entry 20 */
__attribute__((used, externally_visible))
void ViewSquadOnSelect21(void)
{
    RotatePlayersLeft((TeamFileEx *)_squadTeamPtr, 5);
    CallDrawMenuItem(&_menuEntries[20]);
}

/* SelectNationalTeamOnSelect25 (line 78600): rotate career team and redraw entry 24 */
__attribute__((used, externally_visible))
void SelectNationalTeamOnSelect25(void)
{
    RotatePlayersLeft(&_careerTeam, 5);
    CallDrawMenuItem(&_menuEntries[24]);
}

/* ================================================================
 * Chunk 116: Menu Navigation Callbacks
 * ================================================================ */

/* CommonMenuExit is now ported as CommonMenuExit below */
extern void Career_FillCurrentSeasonInfo(void);
extern uint8_t _managementRecordMenu[];
extern uint8_t _viewCompetitionsMenu[];

/* SWOSMainMenuInit — calls CommonMenuExit on main menu init (SWOS2.s line 68835) */
__attribute__((used, externally_visible))
void SWOSMainMenuInit(void)
{
    CommonMenuExit();
}

/* MultiplayerMenuSelected — OpenSWOS's MULTIPLAYER button: SWOS 2020's online flow,
 * log in to a matchmaking server and be introduced to an opponent (src/online_menus.c,
 * src/mm_client.c, docs/NETPLAY.md section 7). The server is normally a local one —
 * scripts/run_matchmaker.sh; SWOS_MM_HOST / SWOS_MM_PORT point it elsewhere.
 *
 * The older direct-IP menu (src/p2p_menu.c, docs/NETPLAY.md section 6) is still here and
 * still works — it is the way to test a link across a real router with no server in the
 * middle — but it is off the main menu now: `--menu=p2p/p2pMenu` opens it. */
__attribute__((used, externally_visible))
void MultiplayerMenuSelected(void)
{
#ifdef SWOS_NETPLAY
    OnlineMenus_Init();
    OnlineMenus_Show("swos2020/logInMenu");
    /* Coming back re-unpacks the main menu from its packed bytes, where the CONTINUE and
     * SAVE rows (entries 11/12) are visible and LOAD OLD COMPETITION sits below them --
     * what SWOSMainMenuInit did to them is gone. Every main-menu handler that opens a
     * submenu puts it back the same way (OptionsMenuSelected, SaveDiskFilingMenu, ...);
     * without it the two rows return as empty coloured bars. */
    CommonMenuExit();
#endif
}

#ifndef SWOS_SDL_BUILD   /* compiled for both Amigas, reached on AGA only (CommonMenuExit) */
/* ---- The Amiga AGA CREDITS screen (menu_data.c _creditsAgaMenu) ----
 * The SDL builds draw the photos into their chunky frame buffer every frame. Here they
 * are part of the backdrop: grafs_aga/CREDITS.RAW is the menu backdrop with the four
 * photos set into it, an 8-plane picture like any other (scripts/make_credits_aga.py,
 * packed by SwosGfx), shown by the display hardware. The photos use palette entries
 * 128..255, which the menus leave alone, so those are lent to them while the screen is
 * up -- the same arrangement as on SDL. The words are include/credits_text.h, the same
 * as the SDL screen's (src/credits_menu.c). */
#include "credits_aga_palette.h"
#include "credits_text.h"
extern uint8_t  _creditsAgaMenu[];
extern uint16_t _menuPalette[256];
extern void     WriteCopperPalette(const uint16_t *palette);
extern void    *_charTables[];
extern uint16_t DrawMenuTextCentered(uint16_t x, uint16_t y, uint16_t color,
                                     uint8_t *text, uint8_t *charsTable);

/* Text is drawn 24 px left of and 36 px above where an ENTRY with the same coordinates
 * lands (the menu system centres entries on the screen): the header entry, centred on
 * x 152 at y 0, is at text x 176, y 36. Everything below is in text coordinates. */
enum {
    kCreditsAgaCentreX = 152 + 24,
    kCreditsAgaTextY   = 56,        /* the first line, under the header */
};

/* In the order make_credits_aga.py set the photos into the backdrop (its PHOTOS). */
static const char *const k_creditsAgaNames[CREDITS_AGA_PHOTOS] = {
    "BENBAKER76", "ZKZ", "PLAYAVELI", "THD79",
};

/* onInit: the screen's own backdrop, as the stadium screen loads its own. */
__attribute__((used, externally_visible))
void CreditsAgaInit(void)
{
    LoadOverlayToFrontbuffer((const uint8_t *)"grafs/credits.raw");
}

/* onDraw: after the backdrop, before the two entries. The lines before the photos run
 * down from the header; the photos are where the backdrop has them (CREDITS_AGA_Y), and
 * the lines after them start under their names. */
__attribute__((used, externally_visible))
void CreditsAgaDraw(void)
{
    uint16_t i, stagger = 0, y = kCreditsAgaTextY;

    /* When two neighbouring names would run into each other, every other one drops a
     * line (as src/credits_menu.c). */
    for (i = 1; i < CREDITS_AGA_PHOTOS; i++)
        if ((GetTextWidth((uint8_t *)k_creditsAgaNames[i - 1], (uint8_t *)_charTables[0]) +
             GetTextWidth((uint8_t *)k_creditsAgaNames[i], (uint8_t *)_charTables[0])) / 2 > CREDITS_AGA_STEP - 4)
            stagger = 1;
    for (i = 0; i < sizeof k_creditsLines / sizeof k_creditsLines[0]; i++) {
        const char *line = k_creditsLines[i];
        if (line == CREDITS_PHOTOS) {
            uint16_t p;
            for (p = 0; p < CREDITS_AGA_PHOTOS; p++)
                DrawMenuTextCentered((uint16_t)(CREDITS_AGA_X + p * CREDITS_AGA_STEP + CREDITS_AGA_PHOTO / 2),
                                     (uint16_t)(CREDITS_AGA_Y + CREDITS_AGA_PHOTO + CREDITS_NAME_DROP +
                                                (stagger && (p & 1) ? CREDITS_NAME_PITCH : 0)),
                                     2, (uint8_t *)k_creditsAgaNames[p], (uint8_t *)_charTables[0]);
            y = (uint16_t)(CREDITS_AGA_Y + CREDITS_AGA_PHOTO + CREDITS_NAME_DROP +
                           CREDITS_NAME_PITCH * (1 + stagger));
        } else if (!line[0])
            y += CREDITS_GAP_PITCH;
        else {
            DrawMenuTextCentered(kCreditsAgaCentreX, y, 2, (uint8_t *)line, (uint8_t *)_charTables[0]);
            y += CREDITS_LINE_PITCH;
        }
    }
}
#endif

/* CreditsMenuSelected -- OpenSWOS's CREDITS button (entry 15). SDL: a JSON-only menu
 * (src/credits_menu.c). Amiga AGA: the native screen above. On the OCS Amiga (16
 * colours: no room for the photos) the button is hidden (CommonMenuExit) and this does
 * nothing. Returning re-unpacks the main menu, hence CommonMenuExit, as above. */
__attribute__((used, externally_visible))
void CreditsMenuSelected(void)
{
#ifdef SWOS_SDL_BUILD
    CreditsMenu_Show();
    CommonMenuExit();
#elif defined(AGA)
    uint16_t saved[256 - CREDITS_AGA_PAL_BASE];
    uint16_t i;

    for (i = 0; i < 256 - CREDITS_AGA_PAL_BASE; i++) {
        saved[i] = _menuPalette[CREDITS_AGA_PAL_BASE + i];
        _menuPalette[CREDITS_AGA_PAL_BASE + i] = k_creditsAgaPalette[i];
    }
    WriteCopperPalette(_menuPalette);
    ShowMenu(_creditsAgaMenu);
    for (i = 0; i < 256 - CREDITS_AGA_PAL_BASE; i++)
        _menuPalette[CREDITS_AGA_PAL_BASE + i] = saved[i];
    WriteCopperPalette(_menuPalette);
    LoadOverlayToFrontbuffer(_aGrafsMenubgRaw);     /* the menus' own backdrop again */
    CommonMenuExit();
#endif
}

/* ManagementRecordMenu — show management record menu (SWOS2.s line 59220) */
__attribute__((used, externally_visible))
void ManagementRecordMenu(void)
{
    if (_youreSacked == 0) {
        Career_FillCurrentSeasonInfo();
    }
    ShowMenu(_managementRecordMenu);
}

/* ViewCompetitionsMenuRetired — show view competitions menu (SWOS2.s line 58291) */
__attribute__((used, externally_visible))
void ViewCompetitionsMenuRetired(void)
{
    ShowMenu(_viewCompetitionsMenu);
}

/* ================================================================
 * Chunk 118: CommonMenuExit
 * ================================================================ */

#define GAME_TYPE_DIY_COMPETITION    1
#define GAME_TYPE_PRESET_COMPETITION 2
#define GAME_TYPE_SEASON             3
#define GAME_TYPE_CAREER             4

extern uint8_t _aContinueA[];  /* "CONTINUE %a" */
extern uint8_t _aReplayA[];    /* "REPLAY %a" */
extern uint8_t _aSaveA[];      /* "SAVE %a" */
extern uint8_t _aSeason_1[];   /* "SEASON" */
extern uint8_t _aCareer_1[];   /* "CAREER" */
extern uint8_t _saveFileTitle[];

extern void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC);

/* CommonMenuExit — set CONTINUE/REPLAY/SAVE entries on main menu return (SWOS2.s line 68793) */
__attribute__((used, externally_visible))
void CommonMenuExit(void)
{
    uint8_t *nameStr;
    uint8_t *fmtStr;

    /* Back at the main menu: whatever was online is over (include/test_rules.h). */
    TestRules_Online(0);

    /* Here, not in SWOSMainMenuInit: coming back from a sub-menu shows the main menu
     * again without its onInit, and the two buttons came back with it (seen on the
     * Amiga). Every way into the main menu ends in this function. */
#if !defined(SWOS_SDL_BUILD) && !defined(AGA)
    /* CREDITS (entry 15): a JSON menu on SDL, a 256-colour picture on the AGA Amiga.
     * The OCS Amiga has neither. */
    _menuEntries[15].isInvisible = 1;
#endif
#ifndef SWOS_NETPLAY
    /* OpenSWOS's MULTIPLAYER button (entry 14) plays over the network, so it is only
     * in a build that has the netplay library (make NETPLAY=1, docs/NETPLAY.md): never
     * on the Amiga, and never on the web, where a browser has no UDP to play over. */
    _menuEntries[14].isInvisible = 1;
#endif

    if (_gameType == GAME_TYPE_DIY_COMPETITION) {
        nameStr = (uint8_t *)_primaryCompetitionBuf.tourn.header.competitionName;
        if (_primaryCompetitionBuf.tourn.header.competitionOver == 1)
            fmtStr = _aContinueA;
        else
            fmtStr = _aReplayA;
    } else if (_gameType == GAME_TYPE_PRESET_COMPETITION) {
        nameStr = (uint8_t *)_primaryCompetitionBuf.tourn.header.competitionName;
        if (_primaryCompetitionBuf.tourn.header.competitionOver == 1)
            fmtStr = _aContinueA;
        else
            fmtStr = _aReplayA;
    } else if (_gameType == GAME_TYPE_SEASON) {
        nameStr = _aSeason_1;
        fmtStr = _aContinueA;
    } else if (_gameType == GAME_TYPE_CAREER) {
        nameStr = _aCareer_1;
        fmtStr = _aContinueA;
    } else {
        /* No active competition: hide the START and SAVE slots and
         * shift LOAD OLD up to where START would have been. Matches
         * the Amiga behaviour (SWOS2.s CommonMenuExit gameType==0). */
        _menuEntries[11].isInvisible = 1;
        _menuEntries[12].isInvisible = 1;
        _menuEntries[13].y = _menuEntries[11].y;
        return;
    }

    /* Format "CONTINUE %a" or "REPLAY %a" into entry 11's string */
    uint8_t *dst = (uint8_t *)_menuEntries[11].fg;
    PrintFormatted(fmtStr, dst, 0, 0, 0, 0, 0, 0, 0, nameStr, 0, 0);

    /* Format "SAVE %a" into saveFileTitle */
    PrintFormatted(_aSaveA, _saveFileTitle, 0, 0, 0, 0, 0, 0, 0, nameStr, 0, 0);

    /* Show both entries */
    _menuEntries[12].isInvisible = 0;
    _menuEntries[11].isInvisible = 0;
}

/* ================================================================
 * Chunk 119: Menu callbacks + ShowFriendlyMenu + ShowSelectRepresentationMenu
 * ================================================================ */

/* RANDOM/SEASONAL constants for ShowFriendlyMenu */
#define RANDOM_PITCH     0xFFFF
#define SEASON_SEP       7
extern uint8_t _seed;              /* primary PRNG seed/counter */
extern uint8_t _randXorIndex;      /* primary XOR key index */
extern uint16_t _diySelected;
extern uint8_t _diyCompetitionMenu[];
extern uint8_t _editMenu[];        /* OpenSWOS: the EDIT submenu (menu_data.c) */
extern uint8_t _practiceMenu[];    /* OpenSWOS: the PRACTICE submenu (menu_data.c) */
extern uint8_t _highlightsMenu[];
extern uint8_t _optionsMenu[];
extern uint8_t _diskMenu[];
extern uint8_t _friendlyGameMenu[];
extern uint16_t _pitchTypeOrSeason;
extern uint16_t _friendlyPitchType;
extern uint16_t _friendlySeason;
extern uint16_t _minSubstitutes;
extern uint16_t _maxSubstitutes;
extern uint16_t _friendlyOk;
extern uint16_t _selectNationalTeamMenuResult;
extern uint8_t _selectNationalTeamMenu[];

extern void Friendly(void);
extern void EditTactics(void);

/* FriendlyFlow — FriendlyMenu without the main-menu fixup, for the handlers
 * that run the friendly flow from INSIDE the PRACTICE submenu
 * (ShootingPracticeMenu, PenaltyShootoutMenu). Under a 4-entry submenu,
 * CommonMenuExit writes through main-menu entries 11..15 that are not there
 * — the same stale-pointer trap EditMenuSelected documents. The submenu's
 * opener, PracticeMenuSelected, makes that call once, after it closes. */
__attribute__((used, externally_visible))
void FriendlyFlow(void)
{
#ifndef DETERMINISTIC_MODE
    /* move.w (currentTick),(seed) — seeds both seed (high byte) + randXorIndex (low byte) */
    _seed = (uint8_t)(_currentTick >> 8);
    _randXorIndex = (uint8_t)(_currentTick & 0xFF);
#endif
    Friendly();
}

/* FriendlyMenu — seed RNG, run Friendly, then update main menu (SWOS2.s line 68796) */
__attribute__((used, externally_visible))
void FriendlyMenu(void)
{
    FriendlyFlow();
    CommonMenuExit();
}

/* DIYCompetitionMenu — seed RNG, prompt, show DIY menu, dispatch design (SWOS2.s line 68803)
 * Note: CheckContinueAbortPrompt returns result via Z flag — need asm bridge */

/* EditMenuSelected — OpenSWOS's EDIT button: the submenu holding the two editors
 * that used to have a main-menu row each (menu_data.c _editMenu, which is where
 * the words are; docs/MENUS.md section 12). Grouping them freed the row the
 * PRACTICE submenu now has.
 *
 * CommonMenuExit belongs HERE and not in the two handlers below: it is the main
 * menu's fixup (its CONTINUE/SAVE rows, _menuEntries[11..15]), and the main menu
 * is what we come back to when the submenu closes. */
__attribute__((used, externally_visible))
void EditMenuSelected(void)
{
    ShowMenu(_editMenu);
    CommonMenuExit();
}

/* PracticeMenuSelected — OpenSWOS's PRACTICE button: the submenu holding the two
 * training modes (menu_data.c _practiceMenu): SHOOTING PRACTICE, which had this
 * main-menu row to itself until 2026-10-26, and PENALTY SHOOTOUT
 * (src/penalty_shootout.c). As with EditMenuSelected, CommonMenuExit is made
 * HERE, once, after the submenu closes — the mode handlers run the friendly
 * flow through FriendlyFlow, which deliberately omits it. */
__attribute__((used, externally_visible))
void PracticeMenuSelected(void)
{
    ShowMenu(_practiceMenu);
    CommonMenuExit();
}

/* EditCustomTeamMenu — edit custom teams (SWOS2.s line 69279). Reached from the EDIT
 * submenu, so NO CommonMenuExit: the current menu is _editMenu, whose four entries
 * do not reach _menuEntries[11..15]. Calling it here formatted a competition name
 * through entry 11's STALE fg pointer -- see EditMenuSelected. */
__attribute__((used, externally_visible))
void EditCustomTeamMenu(void)
{
    EditCustomTeam();
}

/* EditTacticsMenu — edit tactics (SWOS2.s line 69285). From the EDIT submenu too;
 * see EditCustomTeamMenu for why CommonMenuExit is not called here. */
__attribute__((used, externally_visible))
void EditTacticsMenu(void)
{
    EditTactics();
}

/* HighlightsMenu — show highlights menu, then update main menu (SWOS2.s line 69291) */
__attribute__((used, externally_visible))
void HighlightsMenu(void)
{
    ShowMenu(_highlightsMenu);
    CommonMenuExit();
}

/* OptionsMenuSelected — show options menu, then update main menu (SWOS2.s line 69298) */
__attribute__((used, externally_visible))
void OptionsMenuSelected(void)
{
    ShowMenu(_optionsMenu);
    CommonMenuExit();
}

/* SaveDiskFilingMenu — show disk filing menu, then update main menu (SWOS2.s line 69305) */
__attribute__((used, externally_visible))
void SaveDiskFilingMenu(void)
{
    ShowMenu(_diskMenu);
    /* No CommonMenuExit: this is a button of the EDIT submenu now, not of the main
     * menu, and that fixup writes through the MAIN menu's entries 11..15 -- from a
     * submenu it formats a competition name through a stale `fg` pointer.
     * EditMenuSelected makes the call once, after the submenu closes. */
}

/* CHALLENGES -- the main menu row SAVE DISK FILING used to have.
 *
 * Opens the collection / saved-positions hub. Its controller restores the main
 * menu once each modal screen or match has returned. */
__attribute__((used, externally_visible))
void ChallengesMenu(void)
{
    extern void ChallengeCollection_Show(void);
    ChallengeCollection_Show();
}

/* ShowFriendlyMenu — init friendly defaults, show menu (SWOS2.s line 50946) */
__attribute__((used, externally_visible))
void ShowFriendlyMenu(void)
{
    _inFriendlyMenu = 1;
    /* Invalidate the team-file cache so the friendly's country team list reloads
     * PRISTINE. LoadTeamFile() mutates every record's .league byte in place via
     * setLeagueNumbers() (adding the promotion/relegation delta from
     * _diyLeagueSeasonStateTable) whenever gameType==CAREER. A friendly played
     * after a career would otherwise cache-hit that career-mutated buffer
     * (teamsLoaded=1, lastLoadedExtNumber==<country>) and, filtering
     * .league==_leagueNumber, show the career's current division instead of the
     * default one — e.g. German promotion/relegation swapping BAYERN/1860 MUNICH
     * out for FSV ZWICKAU/GÜTERSLOH etc. Friendly runs with gameType==NO_GAME
     * (Friendly() clears it before calling us), so the forced reload skips
     * setLeagueNumbers and restores the pristine leagues. The career is
     * unaffected — its next InitializeNewSeason resets the cache too. NOTE:
     * SWOS2.s has this same latent leak; this is a deliberate fix (back-ported). */
    _teamsLoaded = 0;
    _pitchTypeOrSeason = 0;
    _friendlyPitchType = RANDOM_PITCH;
    _friendlySeason = SEASON_SEP;
    _minSubstitutes = 2;
    _maxSubstitutes = 5;
    _friendlyOk = 0;
    ShowMenu(_friendlyGameMenu);
    _inFriendlyMenu = 0;
}

/* ShowSelectRepresentationMenu — show national team selection, return result (SWOS2.s line 77663) */
__attribute__((used, externally_visible))
uint16_t ShowSelectRepresentationMenu(void)
{
    ShowMenu(_selectNationalTeamMenu);
    return _selectNationalTeamMenuResult;
}

/* SetSelectTwoTeamsText — format "SELECT 2 TEAMS" for friendly menu (SWOS2.s line 51415) */
extern uint8_t _aSelect0Teams[];   /* "SELECT %0 TEAMS" */

__attribute__((used, externally_visible))
void SetSelectTwoTeamsText(void)
{
    uint8_t *str = (uint8_t *)_menuEntries[10].fg;
    PrintFormatted(tr_fmt(_aSelect0Teams), str, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* ================================================================
 * Chunk 121: FriendlyOK, ViewSelectedTeams, ShowDomesticPlayerMarket,
 *            ContestMenuScrollUp, ContestMenuScrollDown
 * ================================================================ */

extern uint16_t _viewSelectedTeams;
extern uint16_t _chooseTeamsResult;
extern uint16_t _changeTeams;
extern uint16_t _abortSelectTeams;
extern uint16_t _exitSelectTeams;

extern void CallDrawMultipleItemsRect(uint16_t count, MenuEntry *firstEntry);

/* FriendlyOK — set friendly ok flag, activate friendly menu (SWOS2.s line 51240) */
__attribute__((used, externally_visible))
void FriendlyOK(void)
{
    _friendlyOk = 1;
    ActivateMenu(_friendlyGameMenu);
}

/* ViewSelectedTeams — set view flag, clear results, exit menu (SWOS2.s line 66094) */
__attribute__((used, externally_visible))
void ViewSelectedTeams(void)
{
    _viewSelectedTeams = 1;
    _chooseTeamsResult = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ContestMenuScrollUp — scroll up, rebuild, redraw 15 entries (SWOS2.s line 60082) */
__attribute__((used, externally_visible))
void ContestMenuScrollUp(void)
{
    ScrollUpInGameList();
    BuildGameListArrayFromTeamSelectionKeyCache();
    CallDrawMultipleItemsRect(15, &_menuEntries[18]);
}

/* ContestMenuScrollDown — scroll down, rebuild, redraw 15 entries (SWOS2.s line 60114) */
__attribute__((used, externally_visible))
void ContestMenuScrollDown(void)
{
    ScrollDownInGameList();
    BuildGameListArrayFromTeamSelectionKeyCache();
    CallDrawMultipleItemsRect(15, &_menuEntries[18]);
}

/* ================================================================
 * Chunk 122: Contest Menu Helpers
 * ================================================================ */

/* Color constants for menu entry background manipulation */
#define COLOR_ORANGE_TO_BROWN_4    4
#define COLOR_PINK_TO_BROWN_7      7
#define COLOR_BLUE_TO_BLACK_8      8
#define COLOR_PINK_TO_BROWN_9      9
#define COLOR_BLUE_TO_PURPLE_11   11

/* Chairman message IDs */
#define NO_MORE_TIME_TO_NEGOTIATE  3

/* ---- External globals (contest/career state) ---- */
extern uint16_t _teamSelectionKeyCache_ListIndex;
extern uint16_t _currentGame;
extern uint16_t _teamSelectionKeyCache_ListCount;
extern uint16_t _teamSelectionKeyCacheCount;
extern uint16_t _teamSelectionKeyCacheIndex;
extern uint16_t _teamSelectionKeyCacheEnd;
extern int16_t  _savedContestListIndex;
extern int16_t  _savedContestGameOffset;
extern uintptr_t _savedContestEntryPtr;
extern uintptr_t _savedContestRoundPtr;
extern uint16_t _gameListY;
extern uint16_t _contestEntryHeight;
extern void *_gameListArray[];          /* 14 pairs of 2 pointers (28 entries) */
extern uint16_t _columnMarker;
extern uint16_t _cglmVar3;
extern uint16_t _seasonFinished;
extern uint16_t _careerFinished;
extern uint16_t _retired;
extern uint16_t _clubBusinessActive;
extern uint16_t _timeToNegotiate;
extern uint16_t _teamNoOfferAccepted;
extern uint16_t _jobOffersFromRetired;
extern uint16_t _seasonNumberOfSquads;
extern uint16_t _isNationalSelector;
extern uint16_t _careerNumPlayers;
extern uint32_t _irqCookieObfuscated;
extern uint8_t  _managerNameBuffer[];
extern uint8_t  _managerSurnameBuffer[];
extern uint8_t  _seasonPlaying;
extern uint16_t _selTeamNumber;
extern uint8_t  _teamSelectionKeyCache[];
extern uint16_t _numOffers;
extern volatile uint16_t _currentMessage;
extern uint16_t _seasonSquadTeamNumbers[];
extern uintptr_t _selectedContest;
extern uint16_t _contestLeagueNumber;

/* String constants */
extern uint8_t _aPlayMatch[];        /* "PLAY MATCH" */
extern uint8_t _aFinishSeason[];     /* "FINISH SEASON" */
extern uint8_t _aNextMatch[];        /* "NEXT MATCH" */
extern uint8_t _aSquads[];           /* "SQUADS" */
extern uint8_t _aNoOffers[];         /* "NO OFFERS" */
extern uint8_t _aNewJob[];           /* "NEW JOB" */
extern uint8_t _aNoJobOffers[];      /* "NO JOB OFFERS" */
extern uint8_t _aSquadSelected[];    /* "SQUAD SELECTED" */
extern uint8_t _aChangeSquad[];      /* "CHANGE SQUAD" */
extern uint8_t _aSelectSquad[];      /* "SELECT SQUAD" */
extern uint8_t _a01Season[];         /* "%0/%1 SEASON" */
extern uint8_t _a001Season[];        /* "%0/0%1 SEASON" */
extern uint8_t _aSquad[];            /* "SQUAD" */

/* Menu packed data (still in asm) */
extern uint8_t _contestMenu[];
extern uint8_t _careerOverMenu[];
extern uint8_t _jobOffersMenu[];
extern uint8_t _gotJobOfferMenu[];
extern uint8_t _transferMarketMenu[];

/* Functions (asm or already ported) */
extern uint8_t *TeamSelectionKeyCache_GetListPtr(uint16_t listIndex);
extern uint8_t *TeamSelectionKeyCache_GetListElementPtr(uint16_t listIndex, uint16_t elementIndex);
extern uint8_t *TeamSelectionKeyCache_GetCurrentListPtr(void);
extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);
extern uint16_t GetStringLength(const uint8_t *str);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern int16_t  CheckForTransferOffers(void);
extern uint16_t IsTournamentAllowingSquadChangesIfNotCareer(void);
extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
extern void Career_PostMatchUpdate(void);        /* still in asm */
extern void Career_ViewOrEditCompetitionSlot(uint8_t *comp, uint16_t sel); /* C bridge for asm */
extern void SeasonFinished_ComputeSeasonReview(void); /* still in asm */
extern TeamFileEx *ChooseTeamForEditTactics(uint8_t *menuTitle, uint16_t *teamNumbers, uint16_t numTeams);
extern uint8_t _aSelectSquadToV[];   /* "SELECT SQUAD TO VIEW" */
extern uint16_t _diyNumberOfTeams;
extern uint8_t *_diyTeamsBufferPtr;
extern void ChooseCompetitionMenu(void);         /* asm stub → ChooseCompetitionMenu */

/* ScrollUpInGameList — decrement currentGame/listIndex with bounds (SWOS2.s line 60083) */
__attribute__((used, externally_visible))
void ScrollUpInGameList(void)
{
    int16_t game = (int16_t)_currentGame - 1;
    if (game >= 0) {
        _currentGame = (uint16_t)game;
        return;
    }
    /* currentGame underflowed, try moving to previous list entry */
    int16_t idx = (int16_t)_teamSelectionKeyCache_ListIndex - 1;
    if (idx < 0) {
        _teamSelectionKeyCache_ListIndex = 0;
        _currentGame = 0;
        return;
    }
    _teamSelectionKeyCache_ListIndex = (uint16_t)idx;
    uint8_t *entry = TeamSelectionKeyCache_GetListPtr((uint16_t)idx);
    int8_t numGames = *(int8_t *)(entry + 5);
    int16_t newGame;
    if (numGames == 0)
        newGame = 0;
    else
        newGame = numGames - 1;
    _currentGame = (uint16_t)newGame;
}

/* ScrollDownInGameList — increment currentGame/listIndex with bounds (SWOS2.s line 60109) */
__attribute__((used, externally_visible))
void ScrollDownInGameList(void)
{
    uint8_t *entry = TeamSelectionKeyCache_GetListPtr(_teamSelectionKeyCache_ListIndex);
    uint8_t numGames = *(uint8_t *)(entry + 5);
    if (numGames != 0) {
        uint16_t next = _currentGame + 1;
        if ((uint8_t)next != numGames) {
            _currentGame = next;
            return;
        }
    }
    /* Move to next list entry */
    uint16_t nextIdx = _teamSelectionKeyCache_ListIndex + 1;
    if (nextIdx == _teamSelectionKeyCache_ListCount)
        return;
    _teamSelectionKeyCache_ListIndex = nextIdx;
    _currentGame = 0;
}

/* RestoreContestScrollPosition — restore saved scroll pos (SWOS2.s line 60132) */
__attribute__((used, externally_visible))
void RestoreContestScrollPosition(void)
{
    _teamSelectionKeyCache_ListIndex = (uint16_t)_savedContestListIndex;
    if (_savedContestListIndex >= 0) {
        _currentGame = (uint16_t)_savedContestGameOffset;
    } else {
        _teamSelectionKeyCache_ListIndex = _teamSelectionKeyCacheIndex;
        _currentGame = 0;
        ScrollUpInGameList();
    }
    /* Scroll up 3 extra times to show context */
    ScrollUpInGameList();
    ScrollUpInGameList();
    ScrollUpInGameList();
}

/* SelectActiveContestAndRound — find current active contest entry (SWOS2.s line 60149) */
__attribute__((used, externally_visible))
void SelectActiveContestAndRound(void)
{
    if (_gameType != GAME_TYPE_CAREER) {
        /* DIY/preset/season without squads */
        if (_seasonNumberOfSquads == 0) {
            if (_seasonFinished != 0)
                goto setInvalid;
            uint8_t *a0 = TeamSelectionKeyCache_GetCurrentListPtr();
            _savedContestListIndex = (int16_t)_teamSelectionKeyCacheIndex;
            _savedContestEntryPtr = (uintptr_t)a0;
            _savedContestGameOffset = 0;
            _savedContestRoundPtr = (uintptr_t)a0;
            return;
        }
    }
    /* Career mode (or season with squads) */
    if (_careerFinished != 0)
        goto setInvalid;
    {
        TeamSelectionKeyCacheEntry *entry =
            (TeamSelectionKeyCacheEntry *)TeamSelectionKeyCache_GetCurrentListPtr();
        uint16_t d0 = _teamSelectionKeyCacheIndex;
        for (;;) {
            uint8_t numGames = (uint8_t)entry->totalElements;
            uint8_t remaining = (uint8_t)entry->remainingElements;
            if (numGames != 0 && remaining != 0) {
                _savedContestListIndex = (int16_t)d0;
                _savedContestEntryPtr = (uintptr_t)entry;
                int16_t gameOff = (int16_t)numGames - (int16_t)remaining;
                _savedContestGameOffset = gameOff;
                /* asm formula: a0 + gameOff*12 (the byte pointer at +gameOff*12
                 * from entry start — same shape as GetCurrentSelectedElementPtr).
                 * Writes will land at +0x0B etc. of that pointer = match slot
                 * gameOff's matchFlags/packedGoals/aggregate/penaltyScore. */
                uint32_t byteOff = (uint16_t)gameOff * 12u;
                _savedContestRoundPtr = (uintptr_t)((uint8_t *)entry + byteOff);
                return;
            }
            d0++;
            entry = (TeamSelectionKeyCacheEntry *)((uint8_t *)entry + entry->entryStride);
            if (d0 == _teamSelectionKeyCacheCount)
                break;
        }
    }
setInvalid:
    _savedContestListIndex = -1;
    _savedContestEntryPtr = 0xFFFFFFFF;
}

/* CheckClubBusiness — toggle blinking on CLUB BUSINESS entry (SWOS2.s line 60196)
 * Note: asm used absolute address ($297E).w which was entry 6 in original binary.
 * We use menuEntries[6] instead. */
__attribute__((used, externally_visible))
void CheckClubBusiness(void)
{
    if (_gameType != GAME_TYPE_CAREER)
        return;
    MenuEntry *entry = &_menuEntries[6];
    uint16_t flags = entry->stringFlags;
    if (_clubBusinessActive != 0)
        flags |= (1 << 13);
    else
        flags &= ~(1 << 13);
    entry->stringFlags = flags;
}

/* CheckJobOffers — update JOB OFFERS entry state (SWOS2.s line 60214) */
__attribute__((used, externally_visible))
void CheckJobOffers(void)
{
    if (_gameType != GAME_TYPE_CAREER)
        return;
    MenuEntry *entry = &_menuEntries[5]; /* JOB OFFERS entry */
    /* Reset background color */
    entry->bgColor = (entry->bgColor & 0xFFF0) | COLOR_BLUE_TO_PURPLE_11;
    entry->isDisabled = 0;
    uint16_t flags = entry->stringFlags;
    flags &= ~(1 << 13);

    if (_teamNoOfferAccepted != 0xFFFF) {
        /* Got an accepted offer — show "NEW JOB" */
        entry->fg = (uintptr_t)_aNewJob;
        entry->stringFlags = flags;
        return;
    }
    if (_numNewJobOffers != 0) {
        /* New offers available — set blinking */
        flags |= (1 << 13);
        entry->stringFlags = flags;
        return;
    }
    if (_numJobOffers != 0) {
        /* Old offers still available */
        entry->stringFlags = flags;
        return;
    }
    /* No offers at all */
    entry->fg = (uintptr_t)_aNoJobOffers;
    entry->isDisabled = 1;
    entry->bgColor = (entry->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_7;
    entry->stringFlags = flags;
}

/* ContestMenuOnReturn_SetButtonsAndDefaultSelection (SWOS2.s line 60254) */
__attribute__((used, externally_visible))
void ContestMenuOnReturn_SetButtonsAndDefaultSelection(MenuEntry *a5)
{
    ContestSetButtons();
    uint16_t ord = *(uint16_t *)((uint8_t *)a5 + 2); /* a5->ordinal at offset 2 */
    if (ord == 0x0D || ord == 0x0A) {
        if (_seasonFinished != 0) {
            SetCurrentEntry(7);
        }
    }
}

/* SeasonLowerButtons — reposition season lower buttons (SWOS2.s line 60346) */
__attribute__((used, externally_visible))
void SeasonLowerButtons(void)
{
    MenuEntry *entry13 = &_menuEntries[13];
    MenuEntry *entry14 = &_menuEntries[14];
    MenuEntry *entry15 = &_menuEntries[15];
    entry14->y = entry13->y;
    entry15->y = entry13->y + 13;
    _menuEntries[13].isInvisible = 1;
}

/* CheckTransfers — update TRANSFERS entry state (SWOS2.s line 60358) */
__attribute__((used, externally_visible))
void CheckTransfers(void)
{
    if (_gameType != GAME_TYPE_CAREER)
        return;

    if (_isNationalSelector != 0) {
        if (IsTournamentAllowingSquadChangesIfNotCareer() != 0)
            goto disableEntry;
    } else {
        if (_teamSelectionKeyCacheEnd != 0)
            goto disableEntry;
    }

    /* Enable transfers entry */
    {
        MenuEntry *entry = &_menuEntries[4];
        entry->bgColor = (entry->bgColor & 0xFFF0) | COLOR_BLUE_TO_BLACK_8;
        entry->isDisabled = 0;
        if (_timeToNegotiate != 0) {
            if (CheckForTransferOffers() == 0) {
                /* Has pending offers — set blinking */
                uint16_t flags = entry->stringFlags;
                flags |= (1 << 13);
                entry->stringFlags = flags;
            }
        }
    }
    /* Done — RETURN here. The Amiga (SWOS2.s:87142 locret_13C1C8 rts)
     * falls out of the function after enabling/setting blink. A pre-fix
     * `goto clearBlink` immediately cleared the bit we just set, so
     * the TRANSFERS button never blinked when offers arrived. */
    return;

disableEntry:
    {
        MenuEntry *entry = &_menuEntries[4];
        entry->bgColor = (entry->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
        entry->isDisabled = 1;
    }
    /* Disabled path: clear blink so a stale bit from a prior enable
     * doesn't keep the button flashing. */
    {
        MenuEntry *entry = &_menuEntries[4];
        uint16_t flags = entry->stringFlags;
        flags &= ~(1 << 13);
        entry->stringFlags = flags;
    }
}

/* BuildGameListArrayFromTeamSelectionKeyCache (SWOS2.s line 60406) */
__attribute__((used, externally_visible))
void BuildGameListArrayFromTeamSelectionKeyCache(void)
{
    uint8_t *elemPtr;
    /* Get list element pointer: a0=list entry, a1=element within it */
    /* The asm calls TeamSelectionKeyCache_GetListElementPtr which returns a0 (list) via a0, a1 via a1.
       In C, the function returns a1 but we also need a0. Use the list ptr function. */
    uint16_t listIdx = _teamSelectionKeyCache_ListIndex;
    uint16_t gameIdx = _currentGame;
    uint8_t *listEntry = TeamSelectionKeyCache_GetListPtr(listIdx);
    uint8_t *gameEntry = listEntry + gameIdx * 12;

    void **out = _gameListArray;
    uint16_t remaining = 14; /* 0xD + 1 = 14 entries (dbf counts from 13) */

    for (;;) {
        if (listIdx == _teamSelectionKeyCache_ListCount) {
            /* Fill remaining slots with NULL */
            while (remaining > 0) {
                *out++ = 0;
                *out++ = 0;
                remaining--;
            }
            return;
        }
        for (;;) {
            *out++ = listEntry;
            *out++ = gameEntry;
            uint8_t numGames = *(uint8_t *)(listEntry + 5);
            if (numGames == 0)
                break;
            gameIdx++;
            if ((uint8_t)gameIdx == numGames)
                break;
            gameEntry += 12;
            remaining--;
            if (remaining == 0)
                return;
        }
        /* Move to next list entry.
         *
         * The asm reads the slot's stride at +2..+3 and advances by it. On
         * SDL this can be corrupted: the in-match league-fixture writer
         * (DIY_RecordLeagueFixtureResultAndAdvanceProgress) writes multiple
         * 12-byte fixtures into a single 22-byte career-mode slot, and
         * fixture 2's team-ID bytes land on slot[N+1]'s stride field at
         * +2..+3. On Amiga the corrupted stride happens to jump to a zeroed
         * area of the overlaid careerForeignMarketPlayers region; on SDL
         * the same jump lands on populated foreign-player records, the
         * walker reads them as fake cache entries, and the reader panics
         * loading "team 0x0200" (= the first 2 bytes of a real
         * foreign-player record interpreted as a team id).
         *
         * Clamp the stride to a sane range. Slots are 22 bytes (career) up
         * to 118 bytes (DIY 18-team) — anything outside [10, 256] is the
         * sign of a stomped stride field. Treat it as the end of the cache
         * so the walker behaves the same as Amiga's "fell off into zeros"
         * accidental-safety-net behaviour. */
        uint16_t stride = ((TeamSelectionKeyCacheEntry *)listEntry)->entryStride;
        if (stride < KEYCACHE_STRIDE_MIN || stride > KEYCACHE_STRIDE_MAX) {
            while (remaining > 0) {
                *out++ = 0;
                *out++ = 0;
                remaining--;
            }
            return;
        }
        listIdx++;
        gameIdx = 0;
        listEntry += stride;
        gameEntry = listEntry;
        remaining--;
        if (remaining == 0)
            return;
    }
}

/* PrintNameAndSeason — show manager name, team, and season year (SWOS2.s line 60445) */
__attribute__((used, externally_visible))
void PrintNameAndSeason(void)
{
    if (_gameType != GAME_TYPE_CAREER) {
        _menuEntries[0].isInvisible = 1;
        /* 60/56 = entry offset 60 relative to entry 0 = entry1.isInvisible (offset 4 of entry 1 = byte 60 from start) */
        /* Actually: offset 60 from menuEntries[0] = menuEntries[1].isInvisible (entry 1, byte 56+4=60) */
        _menuEntries[1].isInvisible = 1;
        /* offset 116 from menuEntries[0] = menuEntries[2].isInvisible (entry 2, byte 112+4=116) */
        _menuEntries[2].isInvisible = 1;
        return;
    }
    _menuEntries[0].isInvisible = 0;
    _menuEntries[1].isInvisible = 0;
    _menuEntries[2].isInvisible = 0;
    _menuEntries[3].isInvisible = 1;  /* $A8 / 56 = 3, offset from base $A8 = 168 = 3*56=168, field offset 4 → isInvisible */

    /* Build manager name string into entry 0's string buffer */
    uint8_t *dst = (uint8_t *)_menuEntries[0].fg;
    uint16_t nameLen = GetStringLength(_managerNameBuffer);
    uint16_t surnameLen = GetStringLength(_managerSurnameBuffer);
    uint16_t totalLen = nameLen + surnameLen;
    uint8_t *src = _managerNameBuffer;

    if (totalLen > 12) {
        /* Name too long — abbreviate first name to initial */
        if (src[0] != 0) {
            *dst++ = src[0];
            *dst++ = ' ';
        }
    } else {
        dst = StringCopy(src, dst);
        *dst++ = ' ';
    }
    StringCopy(_managerSurnameBuffer, dst);

    /* Set team name in entry 1 */
    TeamFileEx *teamData = VerifyTeamLoaded(_selTeamNumber);
    uint8_t *teamName = (uint8_t *)teamData->team.teamName;
    uint8_t *entry1Str = (uint8_t *)_menuEntries[1].fg;
    StringCopy(teamName, entry1Str);

    /* Set season year in entry 2 */
    uint8_t *entry2Str = (uint8_t *)_menuEntries[2].fg;
    uint16_t year = (uint16_t)(_seasonPlaying & 0xFF);
    uint16_t d1 = year;
    year += 1900;
    if (year < 1996)
        year += 100;
    d1 += 1;
    if (d1 >= 100)
        d1 -= 100;
    uint8_t *fmt;
    if (d1 >= 10)
        fmt = _a01Season;    /* "%0/%1 SEASON" */
    else
        fmt = _a001Season;   /* "%0/0%1 SEASON" */
    PrintFormatted(fmt, entry2Str, year, d1, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* SetNationalSelectorButtons — configure national team selector buttons (SWOS2.s line 60517) */
__attribute__((used, externally_visible))
void SetNationalSelectorButtons(void)
{
    if (_gameType != GAME_TYPE_CAREER)
        return;
    if (_isNationalSelector == 0)
        return;

    /* Set transfers/squad entry text */
    MenuEntry *transfersEntry = &_menuEntries[4]; /* TRANSFERS */
    transfersEntry->fg = (uintptr_t)_aSquadSelected;

    if (IsTournamentAllowingSquadChangesIfNotCareer() == 0) {
        transfersEntry->fg = (uintptr_t)_aChangeSquad;
        if (_careerNumPlayers != 20)
            transfersEntry->fg = (uintptr_t)_aSelectSquad;
    }

    /* Entry 12 (national selector actions) */
    MenuEntry *entry12 = &_menuEntries[12];
    entry12->bgColor = (entry12->bgColor & 0xFFF0) | COLOR_ORANGE_TO_BROWN_4;
    entry12->isDisabled = 0;

    if (IsTournamentAllowingSquadChangesIfNotCareer() != 0) {
        entry12->bgColor = (entry12->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
        entry12->isDisabled = 1;
    }

    /* Hide entry 6 (CLUB BUSINESS) for national selector */
    _menuEntries[6].isInvisible = 1;

    if (_careerNumPlayers == 20)
        return;

    /* Not enough players — disable most entries */
    MenuEntry *entry8 = &_menuEntries[8];  /* SQUAD */
    entry8->bgColor = (entry8->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
    entry8->isDisabled = 1;

    MenuEntry *entry7 = &_menuEntries[7];  /* PLAY MATCH */
    entry7->bgColor = (entry7->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
    entry7->isDisabled = 1;

    MenuEntry *entry10 = &_menuEntries[10]; /* NEXT MATCH */
    entry10->bgColor = (entry10->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
    entry10->isDisabled = 1;

    MenuEntry *entry11 = &_menuEntries[11]; /* VIEW COMPETITIONS */
    entry11->bgColor = (entry11->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
    entry11->isDisabled = 1;

    entry12->bgColor = (entry12->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
    entry12->isDisabled = 1;
}

/* ContestSetButtons — set play/finish/next match button states (SWOS2.s line 60270) */
__attribute__((used, externally_visible))
void ContestSetButtons(void)
{
    MenuEntry *entry7 = &_menuEntries[7]; /* PLAY MATCH */
    entry7->fg = (uintptr_t)_aPlayMatch;
    entry7->stringFlags &= ~(1 << 13); /* clear blinking */

    if (_gameType == GAME_TYPE_SEASON) {
        /* Season mode */
        MenuEntry *entry13 = &_menuEntries[13];
        entry13->bgColor = (entry13->bgColor & 0xFFF0) | COLOR_ORANGE_TO_BROWN_4;
        entry13->isDisabled = 0;
        if (_seasonNumberOfSquads == 0) {
            entry7->fg = (uintptr_t)_aNextMatch;
            SeasonLowerButtons();
            if (_seasonFinished != 0)
                goto setFinishSeason;
            return;
        }
        if (_seasonFinished != 0)
            goto lowerAndFinish;
        if (_careerFinished != 0)
            goto setFinishSeason;
        return;
    }

    /* Career mode */
    if (_seasonFinished != 0)
        goto careerFinished_label;

    /* Season not finished */
    _menuEntries[10].isInvisible = 0;
    {
        MenuEntry *entry10 = &_menuEntries[10];
        entry10->bgColor = (entry10->bgColor & 0xFFF0) | COLOR_ORANGE_TO_BROWN_4;
    }
    if (_careerFinished != 0) {
        entry7->fg = (uintptr_t)_aFinishSeason;
        return;
    }
    /* Check if match available — set blinking if so */
    if (_isNationalSelector != 0)
        return;
    if (_teamSelectionKeyCacheEnd == 0)
        return;
    entry7->stringFlags |= (1 << 13);
    return;

careerFinished_label:
    entry7->fg = (uintptr_t)_aFinishSeason;
    {
        MenuEntry *entry10 = &_menuEntries[10];
        entry10->bgColor = (entry10->bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN_9;
        entry10->isDisabled = 1;
    }
    return;

lowerAndFinish:
    SeasonLowerButtons();
setFinishSeason:
    entry7->fg = (uintptr_t)_aFinishSeason;
}

/* ContestMenuInit — init callback for contest menu (SWOS2.s line 59786) */
__attribute__((used, externally_visible))
void ContestMenuInit(void)
{
    /* Tamper check: irqCookieObfuscated XOR 0xCFD1F861 must equal 0x1786 */
    uint32_t cookie = _irqCookieObfuscated ^ 0xCFD1F861;
    if (cookie != 0x1786)
        return; /* tamper detected — skip init (menu will be broken) */

    if (_retired != 0) {
        ActivateMenu(_careerOverMenu);
        return;
    }

    ContestSetButtons();
    ContestMenuOnReturn();

    if (_gameType != GAME_TYPE_CAREER)
        return;
    if (_isNationalSelector == 0)
        return;
    if (_careerNumPlayers == 0x14)
        return;
    SetCurrentEntry(4);
}

/* ContestMenuOnReturn — after-draw callback for contest menu (SWOS2.s line 59811) */
__attribute__((used, externally_visible))
void ContestMenuOnReturn(void)
{
    if (_gameType == GAME_TYPE_SEASON) {
        /* Season mode: hide career-only entries, show season-only entries */
        _menuEntries[10].isInvisible = 1;  /* NEXT MATCH (career) */
        _menuEntries[13].isInvisible = 0;  /* VIEW COMPETITIONS (season) */
        _menuEntries[11].isInvisible = 1;  /* VIEW COMPETITIONS (career) */
        _menuEntries[14].isInvisible = 0;  /* EXIT (season) */
        _menuEntries[9].isInvisible = 1;   /* EXIT (career) */
        _menuEntries[15].isInvisible = 0;  /* SCROLL arrows (season) */

        /* Adjust entry heights */
        _menuEntries[7].dwHeight -= 1;
        _menuEntries[8].dwHeight -= 1;

        /* If only 1 squad, change "SQUAD" text to "SQUADS" */
        if (_seasonNumberOfSquads == 1) {
            _menuEntries[8].fg = (uintptr_t)_aSquads;
        }

        /* Hide career-specific entries */
        _menuEntries[4].isInvisible = 1;   /* TRANSFERS */
        _menuEntries[12].isInvisible = 1;  /* national selector */
        _menuEntries[6].isInvisible = 1;   /* CLUB BUSINESS */
        _menuEntries[5].isInvisible = 1;   /* JOB OFFERS */
        /* fall through to career setup (shared code for game list) */
    } else if (_gameType != GAME_TYPE_CAREER) {
        return;
    }

    /* Career/Season common: set up game list area */
    MenuEntry *entry18 = &_menuEntries[18];
    _gameListY = entry18->y + 2;
    _contestEntryHeight = entry18->dwHeight - 4;

    PrintNameAndSeason();
    /* ContestMenuOnReturn_NoOp() — no-op, skip */

    _teamSelectionKeyCache_ListIndex = 0;
    _currentGame = 0;
    _teamSelectionKeyCache_ListCount = _teamSelectionKeyCacheCount;

    SelectActiveContestAndRound();
    RestoreContestScrollPosition();
    BuildGameListArrayFromTeamSelectionKeyCache();
    ContestSetButtons();
    CheckTransfers();
    CheckClubBusiness();
    CheckJobOffers();
    SetNationalSelectorButtons();
}

/* ==========================================================================
 * Chunk 123 — Contest Menu Action Handlers
 * (PlayMatchMenu, NextMatch, ViewCompetitionsMenu, ClubBusinessMenu,
 *  JobOffersMenu, SeasonFinished, TransfersMenu, ViewWorldMenu,
 *  jmp_SetExitMenuFlag)
 * ========================================================================== */

extern void CareerPlayMatch(void);
extern void Career_PlayMatchMenuLoop(void);
extern void UpdateCareerAfterMatch(void);
extern void Career_NextMatch(void);
extern void MaybeSackManagerAfterSeasonReview(void);
extern void Career_HandleRetirement(void);
extern void ShowClubBussinesMenu(void);

/* Forward declarations for functions in this chunk */
void NextMatch(MenuEntry *callerEntry);
void SeasonFinished(void);

/* PlayMatchMenu — contest PLAY MATCH action (SWOS2.s line 59796)
 * Called as menu onSelect callback; a5 = caller's entry.
 * Asm bridge passes a5 as parameter. */
__attribute__((used, externally_visible))
void PlayMatchMenu(MenuEntry *callerEntry)
{
    if (_seasonFinished != 0) {
        SeasonFinished();
        return;
    }

    if (_gameType != GAME_TYPE_CAREER) {
        if (_seasonNumberOfSquads == 0) {
            NextMatch(callerEntry);
            return;
        }
    }

    if (_careerFinished != 0) {
        CareerPlayMatch();
        SeasonFinished();
        return;
    }

    Career_PlayMatchMenuLoop();

    if (_gameType != GAME_TYPE_SEASON) {
        UpdateCareerAfterMatch();
        if (_youreSacked != 0) {
            Career_PostMatchUpdate();
            ActivateMenu(_contestMenu);
            return;
        }
        CheckClubBusiness();
    }

    /* loc_13BB64 */
    TeamSelectionKeyCache_GetCurrentListPtr();
    SelectActiveContestAndRound();
    ContestMenuOnReturn_SetButtonsAndDefaultSelection(callerEntry);
    RestoreContestScrollPosition();
    BuildGameListArrayFromTeamSelectionKeyCache();
    CheckTransfers();
    /* ContestMenuOnReturn_NoOp() — no-op, skip */

    if (_careerFinished != 0 || _seasonFinished != 0) {
        _fireResetFlag = 1;
    }
}

/* NextMatch — NEXT MATCH action for season mode (SWOS2.s line 59845)
 * Asm bridge passes a5 as parameter. */
__attribute__((used, externally_visible))
void NextMatch(MenuEntry *callerEntry)
{
    Career_NextMatch();

    if (_gameType != GAME_TYPE_SEASON) {
        UpdateCareerAfterMatch();
        if (_youreSacked != 0) {
            Career_PostMatchUpdate();
            ActivateMenu(_contestMenu);
            return;
        }
        CheckClubBusiness();
    }

    /* loc_13BBE6 */
    TeamSelectionKeyCache_GetCurrentListPtr();
    ContestMenuOnReturn_SetButtonsAndDefaultSelection(callerEntry);
    SelectActiveContestAndRound();
    RestoreContestScrollPosition();
    BuildGameListArrayFromTeamSelectionKeyCache();
    CheckTransfers();
    /* ContestMenuOnReturn_NoOp() — no-op, skip */

    if (_careerFinished != 0 || _seasonFinished != 0) {
        _fireResetFlag = 1;
    }
}

/* ViewCompetitionsMenu — show competitions viewer (SWOS2.s line 59880) */
__attribute__((used, externally_visible))
void ViewCompetitionsMenu(void)
{
    ShowMenu(_viewCompetitionsMenu);
    TeamSelectionKeyCache_GetCurrentListPtr();
    ContestSetButtons();
    CheckTransfers();
    /* ContestMenuOnReturn_NoOp — no-op, skip */
}

/* ClubBusinessMenu — show club business (SWOS2.s line 59925) */
__attribute__((used, externally_visible))
void ClubBusinessMenu(void)
{
    ShowClubBussinesMenu();
}

/* JobOffersMenu — show job offers (SWOS2.s line 59930) */
__attribute__((used, externally_visible))
void JobOffersMenu(void)
{
    if (_teamNoOfferAccepted == 0xFFFF) {
        /* No offer accepted yet */
        if (_numNewJobOffers == 0 && _numJobOffers == 0)
            return;

        _jobOffersFromRetired = 0;
        ShowMenu(_jobOffersMenu);
    } else {
        /* Already have an accepted offer */
        ShowMenu(_gotJobOfferMenu);
    }

    /* After either menu, check if entry 5 is disabled */
    MenuEntry *entry5 = &_menuEntries[5];
    if (entry5->isDisabled != 0) {
        SetCurrentEntry(7);
    }
}

/* SeasonFinished — handle end of season (SWOS2.s line 59956) */
__attribute__((used, externally_visible))
void SeasonFinished(void)
{
    if (_gameType != GAME_TYPE_CAREER) {
        InitCareerVariables(0);
        SetExitMenuFlag();
        return;
    }

    /* Career mode */
    if (_isNationalSelector != 0) {
        SeasonFinished_ComputeSeasonReview();
        MaybeSackManagerAfterSeasonReview();
        if (_youreSacked != 0) {
            Career_PostMatchUpdate();
            ActivateMenu(_contestMenu);
            return;
        }
    }

    /* loc_13BD4C */
    Career_HandleRetirement();
    ActivateMenu(_contestMenu);
}

/* TransfersMenu — show transfers or national team selection (SWOS2.s line 59981) */
__attribute__((used, externally_visible))
void TransfersMenu(void)
{
    if (_isNationalSelector != 0) {
        ShowSelectRepresentationMenu();
        return;
    }

    if (_timeToNegotiate == 0) {
        _currentMessage = NO_MORE_TIME_TO_NEGOTIATE;
        ShowChairmanMessage();
        return;
    }

    ShowMenu(_transferMarketMenu);
}

/* ViewWorldMenu — view world competitions (SWOS2.s line 59996) */
__attribute__((used, externally_visible))
void ViewWorldMenu(void)
{
    ChooseCompetitionMenu();
    if (_chooseTeamsResult == 0)
        return;

    Career_ViewOrEditCompetitionSlot((uint8_t *)_selectedContest, _contestLeagueNumber);
    ContestMenuOnReturn();
}


/* ==========================================================================
 * Chunk 124 — Competition Menu Functions
 * (DIYCompetitionMenu, PresetCompetitionMenu, SeasonMenu, CareerMenu,
 *  StartContest, SaveContest, LoadOldCompetitionMenu)
 * ========================================================================== */

extern int16_t CheckContinueAbortPrompt(uint8_t *header);
extern void ShowDesignDIYLeagueMenu(void);
extern void ShowDesignDIYCupMenu(void);
extern void ShowDesignDIYTournamentMenu(void);
extern void SelectTeamsForPresetCompetition(void);   /* asm stub → SelectTeamsForPresetCompetition */
extern void CopySelectedTeamNumbers(uint16_t count, uint8_t *dest);
extern void InitDIYCompetitionFromTableEntry(uint16_t idx, uint8_t *table);
extern void InitDIYCupFromRecord(uint8_t *cupRec);
extern void InitDIYLeagueFromRecord(uint8_t *leagueRec);
extern void InitTeams(void);
extern void ShowDiyLeaguesTournamentMenu(uint16_t type, uint16_t viewOnly, uint16_t focusOverride);
extern void ShowDIYCupMenuWithParams(uint16_t arg0, uint16_t arg1, uint16_t arg2);
extern void DIY_EditCompetitionSlot(uint16_t paramA, uint16_t paramB, uint16_t paramC);
extern void DIY_RestoreCompetitionFileBufferFromDIYCompetition(void);
extern void DIY_CopyCompetitionStartToCompetitionFileBuffer_Alt(void);
extern void DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer(void);
extern void ChooseSeasonMenu(void);                    /* asm stub → ChooseSeasonMenu */
extern void InitNewSeason(uint8_t *src, uint16_t divisionIndex);
extern int16_t Asset_PromptInsertSaveDiskDf0(uint8_t *title);
extern int16_t SelectFileToSaveDialog(uint32_t filterExt, uint8_t *defaultFilename, uint8_t *menuTitleStr);
extern int16_t GetFilenameAndExtension(uint32_t preferredExt, uint8_t *defaultFilename, uint8_t *menuTitleStr);
extern void ShowInsertDisk2Dialog(uint8_t *header);
extern int16_t SaveDIYFile(uint8_t *filename);
extern int16_t SavePresetCompetitionFile(uint8_t *filename);
extern int16_t SaveCareerFile(uint8_t *filename);
extern int16_t LoadDIYFile(uint8_t *filename);
extern int16_t LoadPresetFile(uint8_t *filename);
extern int16_t LoadCareerFile(uint8_t *filename);
extern void SaveDIYTournament(void);
extern void RestoreDIYTournament(void);
extern void SaveDIYLeague(void);
extern void RestoreDIYLeague(void);
extern void DIY_CopyCompetitionFileBufferToCompetitionStart(void);
extern void DIY_CopyCompetitionStartToCompetitionFileBuffer(void);
extern void DIY_SaveCompetitionFileBufferToDIYCompetition(void);
extern void DIY_CopyCompetitionFileBufferToDiyFileBufferCopy(void);
extern void DIY_CopyCompetitionFileBufferToCompetitionStart_Alt(void);
extern void DIY_ResetAndLoadCurrentCompetitionSlot(void);
extern void DIY_LoadCurrentCompetitionSlot(void);
extern void DIY_InitCupFromTeamGridAndValidateTeams(void);
extern void DIY_ResetCompetitionProgressAndShuffle(void);

extern uint16_t _diySelected;
extern uint8_t  _diyCompetitionMenu[];
extern uint8_t  _careerMenu[];
extern uint8_t  _aDiyCompetition_0[];
extern uint8_t  _aPresetCompetit_0[];
extern uint8_t  _aSeason_1[];
extern uint8_t  _aCareer_1[];
extern uint8_t  _aLoadOldCompeti[];
extern uint8_t  _aNotACompetitio[];
extern uint8_t  _competitionFilename[];
extern uint8_t  _selectedFilename[];
extern uintptr_t _extension;
extern DIYTournamentState _diyFileBufferCopy;  /* teamIds field was the diyTournamentTeamIds symbol */
/* diyLeagueTeamIds/diyCupTeamIds aliases retired -> typed diyCompetitionStart
 * field access (other extern is at ~6240, after these uses). */
extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t  _saveFileTitle[];

/* DIYCompetitionMenu — DIY competition type selection (SWOS2.s line 68119) */
__attribute__((used, externally_visible))
void DIYCompetitionMenu(void)
{
#ifndef DETERMINISTIC_MODE
    /* move.w (currentTick),(seed) — the m68k word write puts currentTick's HIGH
     * byte in seed and its LOW byte in randXorIndex. Done as two explicit byte
     * writes: a native uint16 store lands the bytes the other way round on a
     * little-endian host, and this also drops the seed/randXorIndex adjacency
     * assumption. Same form as FriendlyMenu. */
    _seed         = (uint8_t)(_currentTick >> 8);
    _randXorIndex = (uint8_t)(_currentTick & 0xFF);
#endif
    if (CheckContinueAbortPrompt(_aDiyCompetition_0) != 0)
        goto exit;

    ShowMenu(_diyCompetitionMenu);
    if ((int16_t)_diySelected < 0)
        goto exit;

    if (_diySelected == 0) {
        ShowDesignDIYLeagueMenu();
    } else if (_diySelected == 1) {
        ShowDesignDIYCupMenu();
    } else {
        ShowDesignDIYTournamentMenu();
    }

    if (_gameType != GAME_TYPE_NO_GAME) {
        SetCurrentEntry(11);
    }

exit:
    CommonMenuExit();
}

/* PresetCompetitionMenu — preset competition setup (SWOS2.s line 68149) */
__attribute__((used, externally_visible))
void PresetCompetitionMenu(void)
{
#ifndef DETERMINISTIC_MODE
    /* move.w (currentTick),(seed) — the m68k word write puts currentTick's HIGH
     * byte in seed and its LOW byte in randXorIndex. Done as two explicit byte
     * writes: a native uint16 store lands the bytes the other way round on a
     * little-endian host, and this also drops the seed/randXorIndex adjacency
     * assumption. Same form as FriendlyMenu. */
    _seed         = (uint8_t)(_currentTick >> 8);
    _randXorIndex = (uint8_t)(_currentTick & 0xFF);
#endif
    if (CheckContinueAbortPrompt(_aPresetCompetit_0) != 0) {
        CommonMenuExit();
        return;
    }

    SelectTeamsForPresetCompetition();
    if (_chooseTeamsResult == 0) {
        CommonMenuExit();
        return;
    }

    InitCareerVariables(2);
    /* A preset euro cup plays its 36-team tournament substitute (type 2), so
     * the substitution must come BEFORE the type dispatch: Europa/Conference
     * League replace type-1 cup records. */
    uint8_t *contest = Champions36_PresetRecord((uint8_t *)_selectedContest);
    uint8_t compType = contest[1];

    if (compType == 0) {
        /* League */
        CopySelectedTeamNumbers(_numSelectedTeams, (uint8_t *)_diyCompetitionStart.matchData.league.leagueTeamIds);
        InitDIYCompetitionFromTableEntry(_contestLeagueNumber, (uint8_t *)_selectedContest);
        InitTeams();
        ShowDiyLeaguesTournamentMenu(0, 0, 0);
        DIY_RestoreCompetitionFileBufferFromDIYCompetition();
        CommonMenuExit();
    } else if (compType == 1) {
        /* Cup */
        CopySelectedTeamNumbers(_numSelectedTeams, (uint8_t *)_diyCompetitionStart.cupTeamIds);
        InitDIYCupFromRecord((uint8_t *)_selectedContest);
        InitTeams();
        ShowDIYCupMenuWithParams(0, 0, 0);
        DIY_CopyCompetitionStartToCompetitionFileBuffer_Alt();
        CommonMenuExit();
    } else {
        /* Tournament */
        CopySelectedTeamNumbers(_numSelectedTeams, (uint8_t *)_diyFileBufferCopy.teamIds);
        InitDIYLeagueFromRecord(contest);
        InitTeams();
        DIY_EditCompetitionSlot(0, 0, 0);
        DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer();
    }

    CommonMenuExit();
    if (_gameType != GAME_TYPE_NO_GAME) {
        SetCurrentEntry(11);
    }
}

/* SeasonMenu — season mode setup (SWOS2.s line 68221) */
__attribute__((used, externally_visible))
void SeasonMenu(void)
{
#ifndef DETERMINISTIC_MODE
    /* move.w (currentTick),(seed) — the m68k word write puts currentTick's HIGH
     * byte in seed and its LOW byte in randXorIndex. Done as two explicit byte
     * writes: a native uint16 store lands the bytes the other way round on a
     * little-endian host, and this also drops the seed/randXorIndex adjacency
     * assumption. Same form as FriendlyMenu. */
    _seed         = (uint8_t)(_currentTick >> 8);
    _randXorIndex = (uint8_t)(_currentTick & 0xFF);
#endif
    if (CheckContinueAbortPrompt(_aSeason_1) != 0) {
        CommonMenuExit();
        return;
    }

    ChooseSeasonMenu();
    if (_chooseTeamsResult == 0) {
        CommonMenuExit();
        return;
    }

    InitCareerVariables(3);
    InitNewSeason((uint8_t *)_selectedContest, _contestLeagueNumber);
    ShowMenu(_contestMenu);
    CommonMenuExit();
    if (_gameType != GAME_TYPE_NO_GAME) {
        SetCurrentEntry(11);
    }
}

/* CareerMenu — career mode menu (SWOS2.s line 68250) */
__attribute__((used, externally_visible))
void CareerMenu(void)
{
#ifndef DETERMINISTIC_MODE
    /* move.w (currentTick),(seed) — the m68k word write puts currentTick's HIGH
     * byte in seed and its LOW byte in randXorIndex. Done as two explicit byte
     * writes: a native uint16 store lands the bytes the other way round on a
     * little-endian host, and this also drops the seed/randXorIndex adjacency
     * assumption. Same form as FriendlyMenu. */
    _seed         = (uint8_t)(_currentTick >> 8);
    _randXorIndex = (uint8_t)(_currentTick & 0xFF);
#endif
    if (CheckContinueAbortPrompt(_aCareer_1) != 0) {
        CommonMenuExit();
        return;
    }

    ShowMenu(_careerMenu);
    CommonMenuExit();
    if (_gameType != GAME_TYPE_NO_GAME) {
        SetCurrentEntry(11);
    }
    CommonMenuExit();
}

/* StartContest — start or resume contest based on game type (SWOS2.s line 68268)
 * Large dispatcher with branches for DIY, preset, season, career modes,
 * and sub-branches for league/cup/tournament and new/in-progress states. */
__attribute__((used, externally_visible))
void StartContest(void)
{
    _contestStarted = 0;
    uint16_t gt = _gameType;
#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("StartContest: ENTER gameType=%u competitionOver=%u cfb[0]=%u (compType)",
                      (unsigned)gt, (unsigned)_primaryCompetitionBuf.tourn.header.competitionOver,
                      (unsigned)_primaryCompetitionBuf.tourn.header.competitionType);
#endif

    if (gt == GAME_TYPE_DIY_COMPETITION) {
        if (_primaryCompetitionBuf.tourn.header.competitionOver == 1)
            goto diy_over;      /* loc_140FDC */
        goto diy_not_over;      /* loc_1410E4 */
    } else if (gt == GAME_TYPE_PRESET_COMPETITION) {
        if (_primaryCompetitionBuf.tourn.header.competitionOver == 1)
            goto preset_over;   /* loc_141050 */
        goto preset_not_over;   /* loc_14117C */
    } else if (gt == GAME_TYPE_SEASON) {
        /* loc_1410C0 */
        ShowMenu(_contestMenu);
        CommonMenuExit();
        goto final_check;
    } else {
        /* loc_1410D2 — career */
        ShowMenu(_contestMenu);
        CommonMenuExit();
        goto final_check;
    }

diy_over: /* loc_140FDC — DIY competition over */
    {
        uint16_t ct = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
        if (ct == 0) {
            /* League: loc_141030 */
            SaveDIYLeague();
            ShowDiyLeaguesTournamentMenu(0, 0, 0);
            RestoreDIYLeague();
            CommonMenuExit();
            goto final_check;
        } else if (ct == 1) {
            /* Cup: loc_141010 */
            DIY_CopyCompetitionFileBufferToCompetitionStart();
            ShowDIYCupMenuWithParams(0, 0, 0);
            DIY_CopyCompetitionStartToCompetitionFileBuffer();
            CommonMenuExit();
            goto final_check;
        } else {
            /* Tournament — match the asm at loc_140FDC: no LoadCurrentCompetitionSlot.
             * The cfb→diyFileBufferCopy copy in SaveDIYTournament also
             * populates diyCompetitionStart's struct region because
             * diyCompetitionStartBuf is aliased onto diyFileBufferCopyBuf+0x420
             * (Amiga BSS adjacency mirrored in src/swos_globals.c) — so the
             * 1450-word copy restores both. Calling LoadCurrentCompetitionSlot
             * here would re-run InitDIYCup / DIY_InitCupBracketState and
             * wipe the just-restored cup match results, which is the
             * "first match score erased to 0" bug on .DIY save round-trips. */
            SaveDIYTournament();
            DIY_EditCompetitionSlot(0, 0, 0);
            RestoreDIYTournament();
            CommonMenuExit();
            goto final_check;
        }
    }

preset_over: /* loc_141050 — preset competition over */
    {
        uint16_t ct = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
        if (ct == 0) {
            /* League: loc_1410A0 */
            DIY_SaveCompetitionFileBufferToDIYCompetition();
            ShowDiyLeaguesTournamentMenu(0, 0, 0);
            DIY_RestoreCompetitionFileBufferFromDIYCompetition();
            CommonMenuExit();
            goto final_check;
        } else if (ct == 1) {
            /* Cup: loc_141080 */
            DIY_CopyCompetitionFileBufferToCompetitionStart_Alt();
            ShowDIYCupMenuWithParams(0, 0, 0);
            DIY_CopyCompetitionStartToCompetitionFileBuffer_Alt();
            CommonMenuExit();
            goto final_check;
        } else {
            /* Tournament — match the asm at loc_141050: no LoadCurrentCompetitionSlot.
             * Same reasoning as the diy_over tournament path above —
             * diyCompetitionStartBuf is aliased onto diyFileBufferCopyBuf+0x420,
             * so the 1450-word cfb→diyFileBufferCopy copy also populates
             * diyCompetitionStart's struct region. Calling LoadCurrentCompetitionSlot
             * here re-runs InitDIYCup and wipes cup match results, which is
             * the "first match score erased to 0" bug on .PRE save round-trips. */
            DIY_CopyCompetitionFileBufferToDiyFileBufferCopy();
            DIY_EditCompetitionSlot(0, 0, 0);
            DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer();
            CommonMenuExit();
            goto final_check;
        }
    }

diy_not_over: /* loc_1410E4 — DIY not over yet */
    {
        uint16_t ct = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
        if (ct == 0) {
            /* League: loc_141150 */
            SaveDIYLeague();
            InitTeams();
            DIY_ResetCompetitionProgressAndShuffle();
            ShowDiyLeaguesTournamentMenu(0, 0, 0);
            RestoreDIYLeague();
            CommonMenuExit();
            goto final_check;
        } else if (ct == 1) {
            /* Cup: loc_141124 */
            DIY_CopyCompetitionFileBufferToCompetitionStart();
            InitTeams();
            DIY_InitCupFromTeamGridAndValidateTeams();
            ShowDIYCupMenuWithParams(0, 0, 0);
            DIY_CopyCompetitionStartToCompetitionFileBuffer();
            CommonMenuExit();
            goto final_check;
        } else {
            /* Tournament */
            SaveDIYTournament();
            InitTeams();
            DIY_ResetAndLoadCurrentCompetitionSlot();
            DIY_EditCompetitionSlot(0, 0, 0);
            RestoreDIYTournament();
            CommonMenuExit();
            goto final_check;
        }
    }

preset_not_over: /* loc_14117C — preset not over yet */
    {
        uint16_t ct = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
        if (ct == 0) {
            /* League: loc_1411E4 */
            DIY_SaveCompetitionFileBufferToDIYCompetition();
            InitTeams();
            DIY_ResetCompetitionProgressAndShuffle();
            ShowDiyLeaguesTournamentMenu(0, 0, 0);
            DIY_RestoreCompetitionFileBufferFromDIYCompetition();
            CommonMenuExit();
            goto final_check;
        } else if (ct == 1) {
            /* Cup: loc_1411B8 */
            DIY_CopyCompetitionFileBufferToCompetitionStart_Alt();
            InitTeams();
            DIY_InitCupFromTeamGridAndValidateTeams();
            ShowDIYCupMenuWithParams(0, 0, 0);
            DIY_CopyCompetitionStartToCompetitionFileBuffer_Alt();
            CommonMenuExit();
            goto final_check;
        } else {
            /* Tournament */
            DIY_CopyCompetitionFileBufferToDiyFileBufferCopy();
            InitTeams();
            DIY_ResetAndLoadCurrentCompetitionSlot();
            DIY_EditCompetitionSlot(0, 0, 0);
            DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer();
            CommonMenuExit();
            goto final_check;
        }
    }

final_check: /* loc_141210 */
    CommonMenuExit();
    if (_gameType == GAME_TYPE_NO_GAME) {
        SetCurrentEntry(13);
    }
}

/* SaveContest — save competition to disk (SWOS2.s line 68443) */
__attribute__((used, externally_visible))
void SaveContest(void)
{
    if (Asset_PromptInsertSaveDiskDf0(_saveFileTitle) != 0)
        goto cleanup;

    {
        uint16_t gt = _gameType;
        uint32_t ext;
        int16_t result;

        if (gt == GAME_TYPE_DIY_COMPETITION) {
            ext = 0x2E444959; /* '.DIY' */
        } else if (gt == GAME_TYPE_PRESET_COMPETITION) {
            ext = 0x2E505245; /* '.PRE' */
        } else if (gt == GAME_TYPE_SEASON) {
            ext = 0x2E534541; /* '.SEA' */
        } else {
            ext = 0x2E434152; /* '.CAR' */
        }

        if (SelectFileToSaveDialog(ext, _competitionFilename, _saveFileTitle) != 0)
            goto done;

        /* Sync the live working state into competitionFileBuffer
         * before writing it to disk. SAVE is invoked from inside the
         * tournament/league/cup menu — at this point the menu hasn't
         * exited yet, so the end-of-menu Copy that normally mirrors
         * the working buffer back hasn't run. Without this sync the
         * .PRE / .DIY contains the bytes that were in
         * competitionFileBuffer at menu *entry*, missing every match
         * result the user just played in the same session.
         *
         * Pick the right copy based on the competition format word
         * at competitionFileBuffer[0]:
         *   0 = league  → diyCompetitionStart → competitionFileBuffer
         *   1 = cup     → ditto via the cup-style copy
         *   2 = tournament → diyFileBufferCopy → competitionFileBuffer
         *
         * The tournament case is what surfaced the bug (mid-session
         * .PRE round-trip wiping a played match). The league + cup
         * cases get the same fix for free since they're the same
         * "live state lives elsewhere" pattern. */
        if (gt == GAME_TYPE_DIY_COMPETITION || gt == GAME_TYPE_PRESET_COMPETITION) {
            uint16_t ct = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
            if (ct == 0)
                DIY_RestoreCompetitionFileBufferFromDIYCompetition();
            else if (ct == 1)
                DIY_CopyCompetitionStartToCompetitionFileBuffer();
            else
                DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer();
        }

        if (gt == GAME_TYPE_DIY_COMPETITION) {
            result = SaveDIYFile(_selectedFilename);
        } else if (gt == GAME_TYPE_PRESET_COMPETITION) {
            result = SavePresetCompetitionFile(_selectedFilename);
        } else if (gt == GAME_TYPE_SEASON) {
            result = SaveCareerFile(_selectedFilename);   /* j_SaveCareerFile -> SaveCareerFile */
        } else {
            result = SaveCareerFile(_selectedFilename);
        }

        if (result != 0)
            goto done;

        _contestStarted = 1;
        StringCopy(_selectedFilename, _competitionFilename);
    }

done:
    ShowInsertDisk2Dialog(_saveFileTitle);
cleanup:
    CommonMenuExit();
}

/* LoadOldCompetitionMenu — load competition from disk (SWOS2.s line 68524) */
__attribute__((used, externally_visible))
void LoadOldCompetitionMenu(void)
{
    if (CheckContinueAbortPrompt(_aLoadOldCompeti) != 0) {
        CommonMenuExit();
        return;
    }

    if (Asset_PromptInsertSaveDiskDf0(_aLoadOldCompeti) != 0) {
        CommonMenuExit();
        return;
    }

    if (GetFilenameAndExtension(0, _aLoadOldCompeti, _aLoadOldCompeti) != 0)
        goto fail;

    {
        int16_t result;
        if (_extension == 0x2E444959) {          /* '.DIY' */
            result = LoadDIYFile(_selectedFilename);
        } else if (_extension == 0x2E505245) {   /* '.PRE' */
            result = LoadPresetFile(_selectedFilename);
        } else if (_extension == 0x2E534541) {   /* '.SEA' */
            result = LoadCareerFile(_selectedFilename);  /* j_LoadCareerFile */
        } else if (_extension == 0x2E434152) {   /* '.CAR' */
            result = LoadCareerFile(_selectedFilename);
        } else {
            ShowErrorMenu(_aNotACompetitio);
            goto fail;
        }

        if (result != 0) {
            InitCareerVariables(0);
            goto fail;
        }

        StringCopy(_selectedFilename, _competitionFilename);
    }

    /* Success path: loc_14147C */
    ShowInsertDisk2Dialog(_aLoadOldCompeti);
    CommonMenuExit();
    StartContest();
    return;

fail: /* loc_14149A */
    ShowInsertDisk2Dialog(_aLoadOldCompeti);
    CommonMenuExit();
}

/* ==========================================================================
 * Chunk 125 — Choose/Select Team Menu Functions
 * (ChooseCompetitionMenu, SelectTeamsForPresetCompetition, ChooseSeasonMenu,
 *  GoGetTeamsForPlay)
 * ========================================================================== */

extern void SelectTeamsFinalMenu(uint16_t mode); /* bridge: loads d0 from param, calls SelectTeamsFinalMenu */

extern uint16_t _teamsType;
extern uintptr_t _gameTypeStr;
extern uintptr_t _chooseTeamsHeader1stPart;
extern uintptr_t _chooseTeamsFunc;
extern uint16_t _careerSquadScreenDidSelectPlayerFlag;
extern uint16_t _selectTeamsNoLimit;
extern uint16_t _loadTeam;
extern uint16_t _showCupsAndOther;
extern uint16_t _playSeason;
extern uint16_t _chooseCompetitionOnly;
extern uint16_t _choosingPreset;
extern uint16_t _showClubTeams;
extern uint16_t _showNationalTeams;
extern uint16_t _showOrigCustomTeams;
extern uintptr_t _playWhatStr;
extern uint16_t _numNeededTeams;
extern uint8_t  _aChooseCompetit[];
extern uint8_t  _aChoosePresetCo[];
extern uint8_t  _aChooseSeason[];
extern uint8_t  _chooseTeamsMenu[];

/* ChooseCompetitionMenu — choose competition setup (SWOS2.s line 63765) */
__attribute__((used, externally_visible))
void ChooseCompetitionMenu(void)
{
    _gameTypeStr = 0;
    _chooseTeamsHeader1stPart = (uintptr_t)_aChooseCompetit;
    _chooseTeamsFunc = 0;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    _selectTeamsNoLimit = 1;
    _teamsType = 0xFFFF;
    _loadTeam = 0;
    _showCupsAndOther = 1;
    _playSeason = 0;
    _chooseCompetitionOnly = 1;
    SelectTeamsFinalMenu(255);
}

/* SelectTeamsForPresetCompetition — choose preset competition (SWOS2.s line 63782) */
__attribute__((used, externally_visible))
void SelectTeamsForPresetCompetition(void)
{
    _gameTypeStr = 0;
    _chooseTeamsHeader1stPart = (uintptr_t)_aChoosePresetCo;
    _chooseTeamsFunc = 0;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    _selectTeamsNoLimit = 1;
    _teamsType = 0xFFFF;
    _loadTeam = 0;
    _showCupsAndOther = 1;
    _playSeason = 0;
    _chooseCompetitionOnly = 0;
    _choosingPreset = 1;
    SelectTeamsFinalMenu(255);
    _choosingPreset = 0;
}

/* ChooseSeasonMenu — choose season setup (SWOS2.s line 63804) */
__attribute__((used, externally_visible))
void ChooseSeasonMenu(void)
{
    _gameTypeStr = 0;
    _chooseTeamsHeader1stPart = (uintptr_t)_aChooseSeason;
    _chooseTeamsFunc = 0;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    _selectTeamsNoLimit = 1;
    _teamsType = 0xFFFF;
    _loadTeam = 0;
    _showCupsAndOther = 0;
    _playSeason = 1;
    _chooseCompetitionOnly = 0;
    SelectTeamsFinalMenu(255);
}

/* GoGetTeamsForPlay — set up team selection globals and show menu (SWOS2.s line 63830)
 * Called from asm via register bridge (d0=numTeams, d2=showClub, d3=showNational,
 * d4=showOrigCustom, a0=gameTypeStrParam, a1=playWhatStrParam). */
__attribute__((used, externally_visible))
void GoGetTeamsForPlay(uint8_t *gameTypeStrParam, uint8_t *playWhatStrParam,
                        uint16_t numTeams, uint16_t showClub,
                        uint16_t showNational, uint16_t showOrigCustom)
{
    _showClubTeams = showClub;
    _showNationalTeams = showNational;
    _showOrigCustomTeams = showOrigCustom;
    _gameTypeStr = (uintptr_t)gameTypeStrParam;
    _playWhatStr = (uintptr_t)playWhatStrParam;
    _chooseTeamsHeader1stPart = 0;
    _numSelectedTeams = 0;
    _numNeededTeams = numTeams;
    _chooseTeamsFunc = 0;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    _selectTeamsNoLimit = 0;
    ShowMenu(_chooseTeamsMenu);
    if ((int16_t)_teamsType < 0)
        return;
    _showCupsAndOther = 0;
    _loadTeam = 1;
    _playSeason = 0;
    _chooseCompetitionOnly = 0;
    SelectTeamsFinalMenu(255);
    ShowInsertDisk2Dialog((uint8_t *)0);
}


/* ================================================================
 * Chunk 135 — Career Over callbacks, Stats dispatchers,
 *             Select Teams callbacks
 * ================================================================ */

/* Additional externs for Chunk 135 */
extern uint8_t  _gotJobOfferMenu[];
extern uint8_t  _jobOffersMenu[];
extern uint8_t  _a0EndSeason[];          /* "%0 END SEASON" */
extern uint8_t  _aRetire[];              /* "RETIRE" */
extern uint8_t  _aNewJob[];              /* "NEW JOB" */
extern uint8_t  _aNoJobOffers[];         /* "NO JOB OFFERS" */
extern uint16_t _selectedTeamIndex;
extern uint16_t _selectedLeagueIndex;
extern uintptr_t _selectedTeamText;
extern uint16_t _lastSelectedTeam;
extern uint16_t _startingTeamIndex;
extern uint16_t _numTeamsColumn1;
extern uint16_t _numTeamsColumn2;
extern uint16_t _numTeamsColumn3;
extern uint16_t _teamDepthLevel;
extern uint16_t _topOfStackMark;
extern uint16_t _diyCompetitionType;

extern void ShowLeagueInfo(void);
extern void ShowCupInfo(void);
extern void ShowTournamentInfo(void);

/* Already declared elsewhere in menus.c: */
/* extern uint16_t teamNoOfferAccepted, jobOffersFromRetired; */
/* extern uint16_t numNewJobOffers, numJobOffers, youreSacked; */
/* extern uint16_t currentSeasonShowing, contestStarted; */
/* extern uint8_t managerNameBuffer[], managerSurnameBuffer[], seasonPlaying; */
/* extern uint16_t viewSelectedTeams, chooseTeamsResult, changeTeams, abortSelectTeams, exitSelectTeams; */


/* ================================================================
 * CareerOverUpdateJobOffersEntry  (SWOS2.s line 55578)
 *
 * Updates the job offers menu entry (#5 at offset $118) visibility,
 * color, and text based on career state.
 * ================================================================ */
static void CareerOverUpdateJobOffersEntry(void)
{
    MenuEntry *e;

    /* If season 19 (max), hide the entry entirely */
    if (_currentSeasonShowing == 19)
    {
        _menuEntries[0x118 / 56].isInvisible = 1;
        return;
    }

    /* Set entry to yellow-green-black color scheme */
    e = &_menuEntries[0x118 / 56];
    {
        uint16_t bg = e->bgColor;  /* BG+BACKANDFRAMECOLOR at offset 30 */
        bg = (bg & 0xFFF0) + 14;  /* YELLOW_GREEN_BLACK_14 */
        e->bgColor = bg;
    }
    e->isDisabled = 0;

    /* Check stringFlags bit 13 (flashing/highlight) */
    {
        uint16_t flags = e->stringFlags;
        flags &= ~(1 << 13);  /* clear bit 13 */

        if (_teamNoOfferAccepted == 0xFFFF)
        {
            /* No offer accepted — check if there are any offers */
            if (_numNewJobOffers != 0 || _numJobOffers != 0)
            {
                flags |= (1 << 13);  /* set bit 13 (flash) */
                e->stringFlags = flags;
                return;
            }
            /* No offers at all */
            e->fg = (uintptr_t)_aNoJobOffers;
            e->isDisabled = 1;
            {
                uint16_t bg = e->bgColor;
                bg = (bg & 0xFFF0) + 9;  /* PINK_TO_BROWN_9 */
                e->bgColor = bg;
            }
            e->stringFlags = flags;
            return;
        }
        else
        {
            /* Offer accepted — show "NEW JOB" */
            e->fg = (uintptr_t)_aNewJob;
            e->stringFlags = flags;
            return;
        }
    }
}


/* ================================================================
 * CareerOverFinish  (SWOS2.s line 55487)
 *
 * Menu finish (onReturn) callback for career over menu.
 * Sets up manager name, team name, season year, and entry states.
 * ================================================================ */
__attribute__((used, externally_visible))
void CareerOverFinish(void)
{
    MenuEntry *titleEntry = &_menuEntries[0];
    uint8_t *dest = (uint8_t *)titleEntry->fg;
    uint8_t *src;
    uint16_t nameLen, surnameLen, totalLen;

    /* Build "FIRSTNAME SURNAME" or "F SURNAME" into title entry */
    src = _managerNameBuffer;
    nameLen = GetStringLength(src);
    src = _managerSurnameBuffer;
    surnameLen = GetStringLength(src);
    totalLen = nameLen + surnameLen;

    src = _managerNameBuffer;
    if (totalLen > 12)
    {
        /* Name too long — abbreviate first name to initial */
        if (*src != 0)
        {
            *dest++ = *src++;
            *dest++ = ' ';
        }
    }
    else
    {
        /* Full first name fits */
        dest = StringCopy(src, dest);
        *dest++ = ' ';
    }

    /* Append surname */
    src = _managerSurnameBuffer;
    dest = StringCopy(src, dest);

    /* Set team name in entry 1 (offset $38/56 = 1) */
    {
        uint16_t teamNum = _selTeamNumber;
        TeamFileEx *teamPtr = VerifyTeamLoaded(teamNum);
        uint8_t *teamName = (uint8_t *)teamPtr->team.teamName;
        MenuEntry *e1 = &_menuEntries[1];
        dest = (uint8_t *)e1->fg;
        StringCopy(teamName, dest);
    }

    /* Set season year in entry 2 (offset $70/56 = 2) */
    {
        MenuEntry *e2 = &_menuEntries[2];
        dest = (uint8_t *)e2->fg;
        uint16_t year = (uint16_t)_seasonPlaying + 1900;
        if (year < 1996)
            year += 100;
        year += 1;
        src = _a0EndSeason;
        PrintFormatted(src, dest, year, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    CareerOverUpdateJobOffersEntry();

    /* Season 19: final season special handling */
    if (_currentSeasonShowing == 19)
    {
        _menuEntries[0x188 / 56].isInvisible = 1;
        {
            MenuEntry *e150 = &_menuEntries[0x150 / 56];
            e150->fg = (uintptr_t)_aRetire;
        }
        _menuEntries[0xA8 / 56].isInvisible = 0;
        return;
    }

    /* Check if sacked with no offers → disable continue/save */
    if (_teamNoOfferAccepted == 0xFFFF && _youreSacked != 0
        && _numNewJobOffers == 0 && _numJobOffers == 0)
    {
        /* Disable "CONTINUE CAREER" entry (at offset $E0) */
        {
            MenuEntry *eE0 = &_menuEntries[0xE0 / 56];
            eE0->isDisabled = 1;
            uint16_t bg = eE0->bgColor;
            bg = (bg & 0xFFF0) + 9;  /* PINK_TO_BROWN_9 */
            eE0->bgColor = bg;
        }
        /* Disable "SAVE CAREER" entry (at offset $150) */
        {
            MenuEntry *e150 = &_menuEntries[0x150 / 56];
            e150->isDisabled = 1;
            uint16_t bg = e150->bgColor;
            bg = (bg & 0xFFF0) + 9;  /* PINK_TO_BROWN_9 */
            e150->bgColor = bg;
        }
        _menuEntries[0x188 / 56].isInvisible = 1;
        _menuEntries[0xA8 / 56].isInvisible = 0;
    }

    /* If sacked with no accepted offer → also disable save */
    if (_youreSacked != 0 && _teamNoOfferAccepted == 0xFFFF)
    {
        MenuEntry *e150 = &_menuEntries[0x150 / 56];
        e150->isDisabled = 1;
        uint16_t bg = e150->bgColor;
        bg = (bg & 0xFFF0) + 9;  /* PINK_TO_BROWN_9 */
        e150->bgColor = bg;
    }
}


/* ================================================================
 * JobOffersMenuRetired  (SWOS2.s line 55623)
 *
 * Shows job offers from the career over menu.
 * Handles different states: new/existing offers, no offers, etc.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffersMenuRetired(void)
{
    if (_teamNoOfferAccepted != 0xFFFF)
    {
        /* Already accepted an offer — show the "got job" menu */
        ShowMenu(_gotJobOfferMenu);
        goto update_entry;
    }

    /* Check if there are any offers to show */
    if (_numNewJobOffers == 0 && _numJobOffers == 0)
        return;

    /* Show job offers */
    _jobOffersFromRetired = 1;
    ShowMenu(_jobOffersMenu);

    if (_teamNoOfferAccepted == 0xFFFF)
    {
        /* No offer was accepted */
        if (_youreSacked != 0)
        {
            /* Sacked and no remaining offers → exit career */
            uint16_t totalOffers = _numNewJobOffers + _numJobOffers;
            if (totalOffers == 0)
            {
                InitCareerVariables(0);
                SetExitMenuFlag();
                return;
            }
        }
    }

update_entry:
    /* Check if job offers entry is disabled → set current entry to 8 */
    {
        MenuEntry *e = &_menuEntries[0x118 / 56];
        if (e->isDisabled != 0)
            SetCurrentEntry(8);
    }
}


/* ================================================================
 * ExitCareerMenu  (SWOS2.s line 55700)
 *
 * Exit handler for the career over menu.
 * If sacked with no offers, clears career state first.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitCareerMenu(void)
{
    if (_teamNoOfferAccepted == 0xFFFF && _youreSacked != 0
        && _numNewJobOffers == 0 && _numJobOffers == 0)
    {
        InitCareerVariables(0);
    }
    SetExitMenuFlag();
}


/* ================================================================
 * CompetitionInfo  (SWOS2.s line 55254)
 *
 * Dispatches to the correct info display based on competition type.
 * ================================================================ */
__attribute__((used, externally_visible))
void CompetitionInfo(void)
{
    if (_diyCompetitionType == 2)
        ShowTournamentInfo();
    else if (_diyCompetitionType != 0)
        ShowCupInfo();
    else
        ShowLeagueInfo();
}


/* ================================================================
 * StatsExit  (SWOS2.s line 55275)
 *
 * Trivial exit handler — just calls SetExitMenuFlag.
 * ================================================================ */
__attribute__((used, externally_visible))
void StatsExit(void)
{
    SetExitMenuFlag();
}


/* ================================================================
 * ChooseTeamsPlayGame  (SWOS2.s line 62910)
 *
 * "Play Game" button handler in choose teams menu.
 * Sets result flags and exits menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChooseTeamsPlayGame(void)
{
    _selectedTeamIndex = 0xFFFF;
    _selectedLeagueIndex = 0xFFFF;
    _chooseTeamsResult = 1;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * ExitSelectTeams  (SWOS2.s line 62922)
 *
 * Exit/abort handler for the select teams menu.
 * Behavior depends on teamDepthLevel vs topOfStackMark.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitSelectTeams(void)
{
    _selectedTeamIndex = 0xFFFF;
    _selectedLeagueIndex = 0xFFFF;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;

    {
        uint16_t depth = _teamDepthLevel;
        if (depth == 0 || depth == _topOfStackMark)
        {
            _abortSelectTeams = 1;
            _exitSelectTeams = 0;
        }
        else
        {
            _exitSelectTeams = 1;
            _abortSelectTeams = 0;
        }
    }
    SetExitMenuFlag();
}


/* ================================================================
 * SelectTeamsCommon  (SWOS2.s line 62942)
 *
 * Handler for selecting a team from the 3-column list.
 * Converts menu ordinal to team index accounting for column layout.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectTeamsCommon(MenuEntry *entry)
{
    uint8_t *namePtr = (uint8_t *)entry->fg;

    /* Ignore empty (space-prefixed) entries */
    if (*namePtr == ' ')
        return;

    {
        uint16_t ord = entry->ordinal;
        uint16_t idx;

        _lastSelectedTeam = ord;
        idx = ord - 21 + _startingTeamIndex;

        /* Convert column ordinal to flat index */
        if (idx >= 22)
        {
            if (idx >= 44)
            {
                idx -= 22;
                idx += _numTeamsColumn2;
            }
            idx -= 22;
            idx += _numTeamsColumn1;
        }

        _selectedTeamIndex = idx;
        _selectedLeagueIndex = 0xFFFF;
        _selectedTeamText = entry->fg;
        _chooseTeamsResult = 0;
        _viewSelectedTeams = 0;
        _changeTeams = 0;
        _abortSelectTeams = 0;
        _exitSelectTeams = 0;
        SetExitMenuFlag();
    }
}


/* ================================================================
 * SelectLeaguesCommon  (SWOS2.s line 62974)
 *
 * Handler for selecting a league from the list.
 * Converts ordinal (1-based) to league index.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectLeaguesCommon(MenuEntry *entry)
{
    uint8_t *namePtr = (uint8_t *)entry->fg;

    if (*namePtr == ' ')
        return;

    {
        uint16_t ord = entry->ordinal;
        uint16_t idx;

        _lastSelectedTeam = ord;
        idx = ord - 1;
        if (idx >= 10)
            idx -= 10;

        _selectedLeagueIndex = idx;
        _selectedTeamIndex = 0xFFFF;
        _selectedTeamText = entry->fg;
        _chooseTeamsResult = 0;
        _viewSelectedTeams = 0;
        _changeTeams = 0;
        _abortSelectTeams = 0;
        _exitSelectTeams = 0;
        SetExitMenuFlag();
    }
}


/* ================================================================
 * Chunk 136 — ViewCompetitions Init/OnReturn, Career Menu Init/Finish,
 *             Nationality handlers, SelTeamForTactics
 * ================================================================ */

/* Additional externs for Chunk 136 */
extern uint8_t *_currentCompetitionPtr;
extern uint8_t  _tourCopy[];
extern uint8_t  _friendlyCopy[];
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_quaternaryCompetitionPtr;
extern CompetitionSlotBuffer _competitionContextBuf;          /* slot-1 untangle (S4): typed buffer */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;   /* slot-2 untangle (S3): typed buffer */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf;   /* slot-3 untangle (S5): typed buffer */
extern uint8_t  _plNationality;
extern uint8_t  _nationalityOrdinal;
extern uint8_t  _nationalityOrdinalsArray[];
extern uint8_t  _shortCountryNames[];
extern uint8_t  _aOther[];               /* "OTHER" */
extern uint16_t _controlMask;
extern uint16_t _selTeamsColumn1;
extern uint16_t _selTeamsColumn2;
extern uint16_t _teamSelected;

extern void SetDefaultNameAndSurname(void);
extern void CheckSex(void);
extern void SetPlayerCoachFieldColor(void);


/* ================================================================
 * SetNationalityField  (SWOS2.s line 55866)
 *
 * Sets the nationality 3-char code in the career menu entry.
 * Entry 7 (offset $188/56) fg string pointer is the destination.
 * ================================================================ */
static void SetNationalityField(void)
{
    MenuEntry *e = &_menuEntries[0x188 / 56];
    uint8_t *dest = (uint8_t *)e->fg;
    uint8_t nat = _plNationality;

    if (nat == 0xFF)
    {
        /* Unknown nationality — write "OTHER" */
        StringCopy(_aOther, dest);
        return;
    }

    /* Copy 3-char country code from table */
    {
        uint16_t idx = (uint16_t)nat;
        uint16_t off = idx * 3;
        uint8_t *src = _shortCountryNames + off;
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[2];
        dest[3] = 0;
    }
}


/* ================================================================
 * ViewCompetitionsOnReturn  (SWOS2.s line 56227)
 *
 * Called when the ViewCompetitions menu is restored.
 * Sets up competition name text pointers and shuffles Y coordinates
 * when hiding unavailable competition slots.
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewCompetitionsOnReturn(void)
{
    /* Set text pointers for competition name entries */
    _menuEntries[1].fg = (uintptr_t)_primaryCompetitionBuf.tourn.header.competitionName;
    _menuEntries[2].fg = (uintptr_t)_competitionContextBuf.tourn.header.competitionName;
    _menuEntries[3].fg = (uintptr_t)_tertiaryCompetitionContextBuf.tourn.header.competitionName;
    _menuEntries[4].fg = (uintptr_t)_euroCupsCompetitionContextBuf.tourn.header.competitionName;

    /* If primary competition is tour/friendly, hide entry 1 and shift Y coords up */
    if (comp_is_tour_or_friendly(_currentCompetitionPtr
            ? comp_id((void *)_currentCompetitionPtr) : 0xFFu))
    {
        _menuEntries[1].isInvisible = 1;
        /* Shift Y coordinates: 5←4, 4←3, 3←2, 2←1 */
        _menuEntries[5].y = _menuEntries[4].y;
        _menuEntries[4].y = _menuEntries[3].y;
        _menuEntries[3].y = _menuEntries[2].y;
        _menuEntries[2].y = _menuEntries[1].y;
    }

    /* If secondary competition is null/tour/friendly, hide entry 2 and shift */
    if (_secondaryCompetitionPtr == 0
        || comp_is_tour_or_friendly(comp_id((void *)_secondaryCompetitionPtr)))
    {
        _menuEntries[2].isInvisible = 1;
        /* Shift Y coordinates: 5←4, 4←3, 3←2 */
        _menuEntries[5].y = _menuEntries[4].y;
        _menuEntries[4].y = _menuEntries[3].y;
        _menuEntries[3].y = _menuEntries[2].y;
    }

    /* If tertiary competition is null/tour/friendly, hide entry 3 and shift */
    if (_tertiaryCompetitionPtr == 0
        || comp_is_tour_or_friendly(comp_id((void *)_tertiaryCompetitionPtr)))
    {
        _menuEntries[3].isInvisible = 1;
        /* Shift Y coordinates: 5←4, 4←3 */
        _menuEntries[5].y = _menuEntries[4].y;
        _menuEntries[4].y = _menuEntries[3].y;
    }

    /* If quaternary competition is null, hide entry 4 and shift */
    if (_quaternaryCompetitionPtr == 0)
    {
        _menuEntries[4].isInvisible = 1;
        _menuEntries[5].y = _menuEntries[4].y;
    }

    /* Hide "Top Goal Scorers" entry (5) if national selector or no team selection cache */
    if (_isNationalSelector != 0 || _teamSelectionKeyCacheIndex == 0)
    {
        _menuEntries[5].isInvisible = 1;
    }

    /* Hide "Squad" entry (6) if not SEASON game type (4) */
    if (_gameType != 4)
    {
        _menuEntries[6].isInvisible = 1;
    }
}


/* ================================================================
 * ViewCompetitionsInit  (SWOS2.s line 56209)
 *
 * Initializes the ViewCompetitions menu — sets starting entry based
 * on whether current competition is tour/friendly.
 * Falls through to ViewCompetitionsOnReturn.
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewCompetitionsInit(void)
{
    if (comp_is_tour_or_friendly(_currentCompetitionPtr
            ? comp_id((void *)_currentCompetitionPtr) : 0xFFu))
    {
        SetCurrentEntry(5);

        if (_isNationalSelector != 0 || _teamSelectionKeyCacheIndex != 0)
        {
            SetCurrentEntry(7);
        }
    }

    ViewCompetitionsOnReturn();
}


/* ================================================================
 * CareerMenuFinish  (SWOS2.s line 55847)
 *
 * OnReturn callback for the career menu. Updates sex, player-coach
 * field color, and nationality display.
 * ================================================================ */
__attribute__((used, externally_visible))
void CareerMenuFinish(void)
{
    uint8_t nat;
    uint8_t *table;
    uint8_t idx;

    CheckSex();
    SetPlayerCoachFieldColor();

    /* Find nationalityOrdinal for the current plNationality */
    nat = _plNationality;
    table = _nationalityOrdinalsArray;
    idx = 0;
    while (*table != nat)
    {
        table++;
        idx++;
    }
    _nationalityOrdinal = idx;

    SetNationalityField();
}


/* ================================================================
 * CareerMenuInit  (SWOS2.s line 55842)
 *
 * OnInit callback for the career menu.
 * Calls SetDefaultNameAndSurname, then falls through to CareerMenuFinish.
 * ================================================================ */
__attribute__((used, externally_visible))
void CareerMenuInit(void)
{
    SetDefaultNameAndSurname();
    CareerMenuFinish();
}


/* ================================================================
 * ChangeNationality  (SWOS2.s line 55889)
 *
 * Menu callback for left/right on the nationality field.
 * Cycles through nationalities and redraws.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChangeNationality(MenuEntry *entry)
{
    uint16_t ctrl = _controlMask;
    int8_t ord;

    /* Right or Down → increment */
    if (ctrl == 0x81 || ctrl == 0x101)
    {
        ord = (int8_t)_nationalityOrdinal;
        ord++;
        if (ord == 64)
            ord = 0;
    }
    /* Left or Up → decrement */
    else if (ctrl == 0x41 || ctrl == 0x201)
    {
        ord = (int8_t)_nationalityOrdinal;
        ord--;
        if (ord < 0)
            ord = 63;
    }
    else
    {
        return;
    }

    _nationalityOrdinal = (uint8_t)ord;
    {
        int16_t idx = (int16_t)(int8_t)ord;  /* ext.w d0 */
        _plNationality = _nationalityOrdinalsArray[idx];
    }
    SetNationalityField();
    CallDrawMenuItem(entry);
}


/* ================================================================
 * SelTeamForTacticsCommon  (SWOS2.s line 60202)
 *
 * Handler for selecting a team from the tactics team selection list.
 * Converts menu ordinal to flat team index via column layout.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelTeamForTacticsCommon(MenuEntry *entry)
{
    uint16_t idx = entry->ordinal;
    idx -= 1;

    if (idx >= 22)
    {
        if (idx >= 44)
        {
            idx -= 22;
            idx += _selTeamsColumn2;
        }
        idx -= 22;
        idx += _selTeamsColumn1;
    }

    _teamSelected = idx;
    SetExitMenuFlag();
}


/* ================================================================
 * Chunk 137 — SelTeamForTactics_ReflowTeamListLayout
 * ================================================================ */

/* Additional externs for Chunk 137 */
extern uint16_t _selTeamsColumn3;
extern uint16_t _numTeamsCountryBuffer;
extern uint16_t _selTeamsListTopY;
extern uint16_t _selTeamsListBottomY;


/* ================================================================
 * SelTeamForTactics_ReflowTeamListLayout  (SWOS2.s line 60065)
 *
 * Calculates the Y positions for the 3-column team selection list
 * in the edit tactics team picker. Hides title if >18 teams,
 * adjusts bottom entry if >20 teams.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelTeamForTactics_ReflowTeamListLayout(void)
{
    uint16_t maxCol;
    uint16_t topY, bottomY, totalHeight, listHeight, startY;
    MenuEntry *e;
    int16_t col, row;

    /* Find the maximum column height */
    maxCol = _selTeamsColumn1;
    if (_selTeamsColumn2 > maxCol)
        maxCol = _selTeamsColumn2;
    if (_selTeamsColumn3 > maxCol)
        maxCol = _selTeamsColumn3;

    /* If more than 20 teams, adjust bottom entry */
    if (maxCol > 18)
    {
        if (maxCol > 20)
        {
            /* Entry at offset $EA8 = entry 67 */
            e = &_menuEntries[0xEA8 / 56];
            e->y += 7;
            e->dwHeight -= 7;
        }
        /* Hide title entry (entry 0) */
        _menuEntries[0].isInvisible = 1;
    }

    /* Calculate list top Y */
    e = &_menuEntries[0];
    topY = e->y;
    if (e->isInvisible == 0)
        topY += e->dwHeight;
    _selTeamsListTopY = topY;

    /* Calculate list bottom Y */
    e = &_menuEntries[0xEA8 / 56];
    _selTeamsListBottomY = e->y;
    if (_numTeamsCountryBuffer == 64)
    {
        _selTeamsListBottomY += e->dwHeight;
    }

    /* Center the list vertically */
    listHeight = (uint16_t)(maxCol * 9);
    totalHeight = _selTeamsListBottomY - _selTeamsListTopY;
    startY = ((totalHeight - listHeight) >> 1) + _selTeamsListTopY;

    /* Set Y positions for all 3 columns × 22 rows (entries starting at offset $38 = entry 1) */
    e = &_menuEntries[1];
    for (col = 2; col >= 0; col--)
    {
        uint16_t y = startY;
        for (row = 0x15; row >= 0; row--)
        {
            e->y = y;
            y += 9;
            e++;
        }
    }
}


/* ================================================================
 * Chunk 139 — Team Selection Type Handlers + League/Country Names
 * ================================================================ */

/* Externs for Chunk 139 */
extern uint16_t _teamsType;
extern uint16_t _chooseTeamsResult;
extern uint16_t _viewSelectedTeams;
extern uint16_t _changeTeams;
extern uint16_t _abortSelectTeams;
extern uint16_t _exitSelectTeams;
extern uint16_t _numLeaguesInCountry;
extern void *_teamNames[];          /* array of string pointers */
extern uint16_t _chooseTeamsNumTeams;
extern void *_countryNames[];       /* array of string pointers */
extern uint16_t _teamsLowerBound;

extern void SetExitMenuFlag(void);

/* Constants */
#define CLUB_TEAMS             1
#define ORIGINAL_CUSTOM_TEAMS  2
#define EDITED_CUSTOM_TEAMS    3
#define PINK_TO_BROWN_7        7


/* ================================================================
 * ClubTeams  (SWOS2.s line 62995)
 * ================================================================ */
__attribute__((used, externally_visible))
void ClubTeams(void)
{
    _teamsType = CLUB_TEAMS;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * NationalTeams  (SWOS2.s line 63006)
 * ================================================================ */
__attribute__((used, externally_visible))
void NationalTeams(void)
{
    _teamsType = 0;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * OriginalCustomTeams  (SWOS2.s line 63018)
 * ================================================================ */
__attribute__((used, externally_visible))
void OriginalCustomTeams(void)
{
    _teamsType = ORIGINAL_CUSTOM_TEAMS;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * EditedCustomTeams  (SWOS2.s line 63029)
 * ================================================================ */
__attribute__((used, externally_visible))
void EditedCustomTeams(void)
{
    _teamsType = EDITED_CUSTOM_TEAMS;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * ExitChooseTeams  (SWOS2.s line 63040)
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitChooseTeams(void)
{
    _teamsType = 0xFFFF;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 1;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}


/* ================================================================
 * SetLeagueNames  (SWOS2.s line 62387)
 *
 * Populates menu entries for league names in team selection.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetLeagueNames(void)
{
    MenuEntry *e;
    void **names;
    int16_t count;

    if (_numLeaguesInCountry == 0)
        return;

    names = _teamNames;
    /* Entry at $268/56 = 11 (the first lea at $38 is dead code in asm) */
    e = &_menuEntries[11];

    count = (int16_t)_numLeaguesInCountry - 1;

    do {
        e->isInvisible = 0;
        e->fg = (uintptr_t)*names++;

        /* If name starts with '.', set special background color */
        {
            const uint8_t *str = (const uint8_t *)e->fg;
            if (*str == '.')
            {
                /* Write PINK_TO_BROWN_7 to high word of bg (68k move.w to bg offset) */
                e->bgColor = PINK_TO_BROWN_7;
            }
        }

        e++;
    } while (--count >= 0);
}


/* ================================================================
 * SetCountryNames  (SWOS2.s line 62412)
 *
 * Populates menu entries for country team names in team selection.
 * Handles column-based layout and makes non-showing entries invisible.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetCountryNames(void)
{
    void **names = _countryNames;
    MenuEntry *e = &_menuEntries[21]; /* $498/56 = 21 */
    uint16_t d6 = _startingTeamIndex;

    for (;;)
    {
        e->isInvisible = 1;

        if (d6 >= _chooseTeamsNumTeams)
            goto next_entry;

        {
            uint16_t lower = _teamsLowerBound;

            for (;;)
            {
                uint16_t d1 = lower;
                uint16_t ord = e->ordinal;
                uint16_t d0_q, d0_r;

                /* (ordinal - 21) / 22 → quotient in low word, remainder in high word after swap */
                {
                    uint32_t divResult = (uint32_t)(ord - 21) / 22;
                    uint32_t modResult = (uint32_t)(ord - 21) % 22;
                    d0_q = (uint16_t)divResult;
                    d0_r = (uint16_t)modResult;
                }

                if (d0_q == 0)
                    d1 += _numTeamsColumn1;
                else if (d0_q == 1)
                    d1 += _numTeamsColumn2;
                else
                {
                    d1 = lower;
                    d1 += _numTeamsColumn3;
                }

                /* Check if remainder (row) is within [lower, d1) */
                if (d0_r >= lower && d0_r < d1)
                {
                    /* Show this entry */
                    e->isInvisible = 0;
                    e->fg = (uintptr_t)*names++;

                    /* If name starts with '.', set special background color */
                    {
                        const uint8_t *str = (const uint8_t *)e->fg;
                        if (*str == '.')
                        {
                            e->bgColor = PINK_TO_BROWN_7;
                        }
                    }
                    break; /* goto next_entry */
                }

                /* Skip entry — make next entry invisible and continue */
                e++;
                if (e->ordinal == 87)
                    return;
                e->isInvisible = 1;
                /* Continue inner loop with same lower bound */
            }
        }

next_entry:
        e++;
        if (e->ordinal == 87)
            return;
        d6++;
    }
}

/* ---- Chunk 141: SetMenuEntryPlayerFace ---- */
#define SPR_PL_FACE_WHITE_GUY     186

__attribute__((used, externally_visible))
void SetMenuEntryPlayerFace(MenuEntry *entry, PlayerFile *playerFile) {
    uint16_t d0 = playerFile[2].positionAndFace;
    d0 &= 0x18;
    d0 >>= 3;
    d0 += SPR_PL_FACE_WHITE_GUY;
    entry->fgWord = d0;
}

/* ---- Chunk 142: SaveCareer ---- */
/* SWOS2.s line 55472 — save career to disk */
__attribute__((used, externally_visible))
void SaveCareer(void)
{
    uint8_t *title = (uint8_t *)"SAVE CAREER";

    if (Asset_PromptInsertSaveDiskDf0(title) != 0)
        return;

    if (SelectFileToSaveDialog(0x2E434152, _competitionFilename, title) != 0)
        goto show_insert;

    if (SaveCareerFile(_selectedFilename) != 0)
        goto show_insert;

    _contestStarted = 1;
    StringCopy(_selectedFilename, _competitionFilename);

show_insert:
    ShowInsertDisk2Dialog(title);
}

/* ---- Chunk 143: ExitOptions ---- */
/* SWOS2.s line 46546 — options menu exit callback with prerelease easter egg */
/* showPrereleaseCounter declared extern at line 1027 (asm variable at SWOS2.s:46562) */

__attribute__((used, externally_visible))
void ExitOptions(void)
{
    if (_controlMask == 0x20) {
        SetExitMenuFlag();
        return;
    }
    if (_controlMask == 0x30) {
        _showPrereleaseCounter++;
        if (_showPrereleaseCounter == 15) {
            _menuEntries[14].isInvisible = 0;
            CallDrawMenuItem(&_menuEntries[14]);
        }
    }
}

/* ---- Chunk 143: SetSquadMenuEntryStatusSprite ---- */
/* SWOS2.s line 54885 — set player status sprite on squad menu entry */
/* Writes sprite ID to high word of entry->fg based on player injury/status bits.
   flag==0: during-match status bits (mask 0x1C), flag!=0: after-match bits (mask 0x03) */
extern uint16_t ShouldApplyRandomPlayerStatusAfterSim(void);

__attribute__((used, externally_visible))
void SetSquadMenuEntryStatusSprite(MenuEntry *entry, PlayerFile *playerFile, uint16_t flag)
{
    PlayerFile *pf = &playerFile[2];
    uint16_t d1 = pf->positionAndFace & PF_FACE_MASK;
    d1 >>= 3; /* face index 0-3 */
    uint16_t d2, d3;
    uint16_t sprite;

    if (flag == 0) {
        /* Path 0: during-match status */
        d2 = pf->cardsInjuries & 0x1C;
        d3 = pf->cardsInjuries & 0xE0;

        if (d2 == 0) {
            if (d3 == 0) {
                entry->fgWord = d1 + 0xBA;
                return;
            }
            if (d3 == 0x20) {
                entry->fgWord = d1 + 0x99;
                return;
            }
            goto injury_0;
        }
        /* d2 != 0 */
        if (d3 == 0 || d3 == 0x20) {
            sprite = (d2 - 4) / 4 + 0x91;
            goto write_fg_bg_0;
        }
        if (d2 == 4) {
            goto injury_0;
        }
        {
            uint16_t c0 = (d2 - 8) >> 2;
            uint16_t c1 = (d3 - 0x40) >> 5;
            if (c0 >= c1) {
                sprite = (d2 - 4) / 4 + 0x91;
                goto write_fg_bg_0;
            }
        }
    injury_0:
        if ((uint8_t *)playerFile >= (uint8_t *)&_careerTeam && (uint8_t *)playerFile < (uint8_t *)&_careerTeam + CAREER_TEAM_BUF_SIZE) {
            if (ShouldApplyRandomPlayerStatusAfterSim() == 0) {
                sprite = 0x98;
                goto write_fg_bg_0;
            }
        }
        sprite = (d3 - 0x40) / 32 + 0x9C;
    write_fg_bg_0:
        entry->fgWord = sprite;
        entry->bgColor &= 0xFFF0;
        return;
    }

    /* Path 1: after-match status (flag != 0) */
    d2 = pf->cardsInjuries & 0x03;
    d3 = pf->cardsInjuries & 0xE0;

    if (d2 == 0) {
        if (d3 == 0) {
            entry->fgWord = d1 + 0xBA;
            return;
        }
        if (d3 == 0x20) {
            entry->fgWord = d1 + 0x99;
            return;
        }
        goto injury_1;
    }
    /* d2 != 0 */
    if (d3 == 0 || d3 == 0x20) {
        sprite = d2 - 1 + 0x92;
        goto write_fg_bg_1;
    }
    {
        uint16_t c0 = d2 - 1;
        uint16_t c1 = (d3 - 0x40) >> 5;
        if (c0 >= c1) {
            sprite = d2 - 1 + 0x92;
            goto write_fg_bg_1;
        }
    }
injury_1:
    if ((uint8_t *)playerFile >= (uint8_t *)&_careerTeam && (uint8_t *)playerFile < (uint8_t *)&_careerTeam + CAREER_TEAM_BUF_SIZE) {
        if (ShouldApplyRandomPlayerStatusAfterSim() == 0) {
            sprite = 0x98;
            goto write_fg_bg_1;
        }
    }
    sprite = (d3 - 0x40) / 32 + 0x9C;
write_fg_bg_1:
    entry->fgWord = sprite;
    entry->bgColor &= 0xFFF0;
}

/* ---- Chunk 143: SetSquadMenuEntryStatusSprite_AfterSim ---- */
/* SWOS2.s line 55022 — after-sim variant: check random status first */
__attribute__((used, externally_visible))
void SetSquadMenuEntryStatusSprite_AfterSim(MenuEntry *entry, PlayerFile *playerFile)
{
    if (ShouldApplyRandomPlayerStatusAfterSim() == 0) {
        /* SetSquadMenuEntryStatusSprite expects a RAW player pointer
         * (team-relative, from GetPlayer); it adds sizeof(TeamFile)=76
         * internally to reach the PlayerFile data. But our `playerFile`
         * param is already +76-offset (the AfterSim convention used by
         * the transfer-market caller). Undo the offset before
         * delegating so the inner function doesn't double-count and
         * read fields from two PlayerFile slots ahead — that was
         * causing ADD INTERNATIONAL PLAYER's status-sprite column to
         * render with the wrong player's positionAndFace/cardsInjuries,
         * picking card-icon sprites instead of skin/hair faces. */
        SetSquadMenuEntryStatusSprite(entry,
            (PlayerFile *)((uint8_t *)playerFile - sizeof(TeamFile)), 0);
        return;
    }
    /* After sim path — extract face and status for sprite.
     * ASM reads PLAYERFILE_POSITIONANDFACE+$4C(a6) where a6 is the raw
     * player pointer. Since playerFile is already player+PF_OFF (76),
     * we access playerFile directly (offset 26+76=102 from raw player). */
    uint16_t d1 = playerFile->positionAndFace & PF_FACE_MASK;
    d1 >>= 3;
    uint16_t d3 = playerFile->cardsInjuries & 0xE0;

    if (d3 == 0) {
        entry->fgWord = d1 + 0xBA;
        return;
    }
    if (d3 == 0x20) {
        entry->fgWord = d1 + 0x99;
        return;
    }
    /* Injured */
    entry->fgWord = (d3 - 0x40) / 32 + 0x9C;
}

/* ---- Chunk 147: SetFriendlyPitchOrSeason ---- */
/* SWOS2.s line 49546 — set menu entry 4's fg to pitch type or season string table pointer */
extern uint8_t _friendlyPitchTypeTable[];
extern uint8_t _friendly_season_table[];

__attribute__((used, externally_visible))
void SetFriendlyPitchOrSeason(void)
{
    uint8_t *table = _friendlyPitchTypeTable;
    if (_pitchTypeOrSeason != 0)
        table = _friendly_season_table;
    _menuEntries[4].fg = (uintptr_t)table;  /* 0xE0 / 56 = 4 */
}

/* ---- Chunk 147: PitchTypeOrSeasonProc ---- */
/* SWOS2.s line 49537 — toggle pitch/season mode, update display, redraw entries */
__attribute__((used, externally_visible))
void PitchTypeOrSeasonProc(void)
{
    _pitchTypeOrSeason ^= 1;
    SetFriendlyPitchOrSeason();
    CallDrawMultipleItems(2, &_menuEntries[3]); /* 0xA8/56=3, count=2 */
}

/* ---- Chunk 147: FriendlyPitchTypeProc ---- */
/* SWOS2.s line 49558 — cycle pitch type (0-6, wrap to RANDOM=0xFFFF) or season (0-11).
 * Called as menu entry callback with a5=entry; redraws current entry after update. */
__attribute__((used, externally_visible))
void FriendlyPitchTypeProc(MenuEntry *entry)
{
    if (_pitchTypeOrSeason == 0) {
        _friendlyPitchType++;
        if (_friendlyPitchType == 7)
            _friendlyPitchType = 0xFFFF; /* RANDOM */
    } else {
        _friendlySeason++;
        if (_friendlySeason >= 12)
            _friendlySeason = 0;
    }
    CallDrawMenuItem(entry);
}

/* ================================================================
 * Chunk 149: Friendly menu cluster
 * ================================================================ */

#define GAME_TYPE_NO_GAME  0
#define SEASONAL           0xFFFE
#define PINK_TO_BROWN_9    9
#define ENTRY_FRAME_AND_BACK_COLOR 2

extern uint16_t _gameType;
extern void SaveSelectedTeams(void);
extern void RestoreSelectedTeams(void);
extern void ShowFriendlyMenu(void);
extern void SetSelectTwoTeamsText(void);
extern void Int2Ascii(uint16_t value, uint8_t *outBuf);
extern void GoGetTeamsForPlay(uint8_t *_gameTypeStr, uint8_t *_playWhatStr,
                               uint16_t numTeams, uint16_t showClub,
                               uint16_t showNational, uint16_t showCustom);
extern void InitTeams(void);
extern void PlayFriendly(void);
extern uint16_t Rand2(void);
extern void SetCurrentEntry(uint16_t entry);

extern uint8_t _aFriendly_1[];       /* "FRIENDLY" */
extern uint8_t _aPlayFriendly[];     /* "PLAY FRIENDLY" */
extern uint8_t _aShootingPractice[]; /* "SHOOTING PRACTICE" */
extern uint8_t _aPlayPractice[];     /* "PLAY PRACTICE" */
extern uint8_t _aPenaltyShootout[];  /* "PENALTY SHOOTOUT" */
extern uint8_t _aPlayShootout[];     /* "PLAY SHOOTOUT" */
extern uint16_t _friendlyOk;
extern uint16_t _minSubstitutes;
extern uint16_t _maxSubstitutes;

/* Friendly: if no game in progress → show friendly menu, else ContestInProgress. */
__attribute__((used, externally_visible))
void Friendly(void)
{
    if (_gameType == GAME_TYPE_NO_GAME) {
        ShowFriendlyMenu();
        return;
    }
    /* ContestInProgress: save state, show friendly, restore */
    SaveSelectedTeams();
    uint16_t savedGameType = _gameType;
    uint16_t savedSelTeam = _selTeamNumber;
    _selTeamNumber = 0xFFFF;
    _gameType = GAME_TYPE_NO_GAME;
    ShowFriendlyMenu();
    _selTeamNumber = savedSelTeam;
    _gameType = savedGameType;
    RestoreSelectedTeams();
}

/* DrawNumbersOfSubstitutes: write min/max sub counts to menu entries. */
__attribute__((used, externally_visible))
void DrawNumbersOfSubstitutes(void)
{
    Int2Ascii(_minSubstitutes, (uint8_t *)_menuEntries[5].fg);   /* 0x118/56=5 */
    Int2Ascii(_maxSubstitutes, (uint8_t *)_menuEntries[6].fg);   /* 0x150/56=6 */
    SetFriendlyPitchOrSeason();
}

/* DisableEntries: disable entries 1-6 that have frame+back background. */
__attribute__((used, externally_visible))
void DisableEntries(void)
{
    int16_t i;
    for (i = 0; i < 6; i++) {
        MenuEntry *e = &_menuEntries[1 + i];  /* 0x38/56=1, loop 6 */
        if (e->background == ENTRY_FRAME_AND_BACK_COLOR) {
            uint16_t bgVal = e->bgColor;
            if ((bgVal & 0x0F) != 0) {
                bgVal = (bgVal & 0xFFF0) + PINK_TO_BROWN_9;
                e->bgColor = bgVal;
                e->isDisabled = 1;
            }
        }
    }
}

/* FriendlyMenuInit: init friendly menu — set header text if OK, fall through to Reinit. */
__attribute__((used, externally_visible))
void FriendlyMenuInit(void)
{
    /* Entry 0 is the header box. SHOOTING PRACTICE and PENALTY SHOOTOUT run
     * this screen as their own (src/shooting_practice.c, src/penalty_shootout.c),
     * and it is the one place the word FRIENDLY would otherwise still be on
     * screen during either mode. */
    _menuEntries[0].fg = (uintptr_t)(g_shootingPracticeMode ? _aShootingPractice
                                   : g_penaltyShootoutMode  ? _aPenaltyShootout
                                                            : _aFriendly_1);
    if (_friendlyOk != 0)
        SetSelectTwoTeamsText();
    FriendlyMenuReinit();
}

/* FriendlyMenuReinit: update pitch/season display state and menu entry visibility. */
__attribute__((used, externally_visible))
void FriendlyMenuReinit(void)
{
    if (_pitchType != SEASONAL) {
        _pitchTypeOrSeason = 0;
        _friendlyPitchType = _pitchType;
        /* Disable entry 3 (pitch/season toggle) */
        MenuEntry *e3 = &_menuEntries[3];  /* 0xA8/56=3 */
        uint16_t bg3 = e3->bgColor;
        bg3 = (bg3 & 0xFFF0) + PINK_TO_BROWN_9;
        e3->bgColor = bg3;
        e3->isDisabled = 1;
        /* Disable entry 4 */
        MenuEntry *e4 = &_menuEntries[4];  /* 0xE0/56=4 */
        uint16_t bg4 = e4->bgColor;
        bg4 = (bg4 & 0xFFF0) + PINK_TO_BROWN_9;
        e4->bgColor = bg4;
        e4->isDisabled = 1;
    }

    DrawNumbersOfSubstitutes();

    if (_friendlyOk == 0) {
        _menuEntries[7].isInvisible = 1;   /* 0x188/56=7 */
        _menuEntries[8].isInvisible = 0;   /* 0x1C0/56=8 */
        _menuEntries[10].isInvisible = 1;  /* 0x230/56=10 */
        return;
    }

    DisableEntries();
    _menuEntries[8].isInvisible = 1;       /* 0x1C0/56=8 */
    SetCurrentEntry(7);
}

/* ChooseTeamsMenu: set up friendly team selection. */
__attribute__((used, externally_visible))
void ChooseTeamsMenu(void)
{
    /* SHOOTING PRACTICE and PENALTY SHOOTOUT borrow this whole flow, so the
     * only thing that marks the selection screens as a mode's is the two words
     * they are given: gameTypeStr heads them, playWhatStr is the button that
     * starts the match. */
    if (g_shootingPracticeMode)
        GoGetTeamsForPlay(_aShootingPractice, _aPlayPractice, 2, 1, 1, 1);
    else if (g_penaltyShootoutMode)
        GoGetTeamsForPlay(_aPenaltyShootout, _aPlayShootout, 2, 1, 1, 1);
    else
        GoGetTeamsForPlay(_aFriendly_1, _aPlayFriendly, 2, 1, 1, 1);
    if (_chooseTeamsResult == 0) {
        SetExitMenuFlag();
        return;
    }
    RandomlySwapTeams();
    InitTeams();
    PlayFriendly();
    SetExitMenuFlag();
}

/* MinSubstitutes: increment minSubstitutes with wraparound to 1. */
__attribute__((used, externally_visible))
void MinSubstitutes(void)
{
    if (_maxSubstitutes == 0) return;
    _minSubstitutes++;
    if (_minSubstitutes > _maxSubstitutes)
        _minSubstitutes = 1;
    DrawNumbersOfSubstitutes();
    CallDrawMenuItem(&_menuEntries[5]);   /* 0x118/56=5 */
}

/* MaxSubstitutes: increment _maxSubstitutes (0-5), enforce minSubstitutes >= 1. */
__attribute__((used, externally_visible))
void MaxSubstitutes(void)
{
    _maxSubstitutes++;
    if (_maxSubstitutes > 5) {
        _maxSubstitutes = 0;
        _minSubstitutes = 0;
    }
    if (_maxSubstitutes == 1)
        _minSubstitutes = 1;
    DrawNumbersOfSubstitutes();
    CallDrawMenuItem(&_menuEntries[6]);   /* 0x150/56=6 */
    CallDrawMenuItem(&_menuEntries[5]);   /* 0x118/56=5 */
}

/* RandomlySwapTeams: 50% chance to swap team 1 and team 2 in selTeamsPtr. */
__attribute__((used, externally_visible))
void RandomlySwapTeams(void)
{
    uint16_t r = Rand2();
    if (!(r & 1)) return;
    /* Swap two TeamFileEx records word-by-word (684 / 2 = 342 words). */
    TeamFileEx *base = _selectedTeams;
    uint16_t *a = (uint16_t *)&base[0];
    uint16_t *b = (uint16_t *)&base[1];
    int16_t i;
    for (i = 0; i < 342; i++) {
        uint16_t tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

/* ================================================================
 * Chunk 163: ViewSquad Init/Common/AfterDraw
 * ================================================================ */

extern uintptr_t _viewSquadTitle;
extern uint32_t _viewSquadFlags;
extern uint32_t _viewSquadMaskLo;
extern uint32_t _viewSquadMaskHi;
extern uint16_t _viewSquadArg2;
extern uint16_t _viewSquadArg3;
extern volatile uint16_t _viewSquadSelectedTeam;

extern uint8_t *GetPlayerInfoBreakPosition(TeamFileEx *teamPtr, uint16_t requestedSlot,
                                              uint16_t *outSlot, uint16_t *outPlayerIndex);
extern void BuildTransferPlayerMenuEntry(void *item, uint8_t *player,
                                           uint16_t y, int16_t idx,
                                           uint16_t style, uint8_t *extra);

/* ViewSquadInit (Chunk 163)
 * Initializes view squad menu: shows/hides entries based on roster size,
 * sets title/team name, handles pre-selection. */
__attribute__((used, externally_visible))
void ViewSquadInit(void)
{
    uint16_t teamNum = _selTeamNumber;
    if (teamNum != 0xFFFF) {
        if (teamNum == PACK_BE_U16(_squadTeamPtr->countryNumber, _squadTeamPtr->teamOrdinal)) {
            if (_careerNumPlayers == 16) {
                goto skipEntryShow;
            }
            _menuEntries[20].isInvisible = 0; /* 0x460/56 = 20 */
            if (_careerNumPlayers > 17) {
                _menuEntries[21].isInvisible = 0; /* 0x498/56 = 21 */
            }
        }
    }
skipEntryShow:

    _menuEntries[0].fg = _viewSquadTitle;
    _menuEntries[1].fg = (uintptr_t)_squadTeamPtr->teamName;

    if (_viewSquadArg2) {
        _menuEntries[2].isInvisible = 1;  /* 0x70/56 = 2 */
        _menuEntries[3].isInvisible = 0;  /* 0xA8/56 = 3 */
        SetCurrentEntry(3);
    }

    int16_t arg3 = (int16_t)_viewSquadArg3;
    if (arg3 < 0)
        return;

    if ((uint16_t)arg3 < 16) {
        SetCurrentEntry((uint16_t)arg3 + 4);
    } else {
        SetCurrentEntry(20);
    }
}

/* ViewSquadCommon (Chunk 163)
 * a5 = current menu entry. Computes selected player/team, exits menu. */
__attribute__((used, externally_visible))
void ViewSquadCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    MenuEntry *entry4 = &_menuEntries[4]; /* 0xE0/56 = 4 */
    uint16_t relIdx = a5->ordinal - entry4->ordinal;
    _viewSquadSelectedTeam = relIdx;

    uint16_t outSlot, outPlayerIndex;
    GetPlayerInfoBreakPosition((TeamFileEx *)_squadTeamPtr, relIdx,
                                 &outSlot, &outPlayerIndex);
    _viewSquadSelectedPlayer = outPlayerIndex;
    SetExitMenuFlag();
}

/* ViewSquadAfterDrawCommon (Chunk 163)
 * a5 = current menu entry. Draws transfer-style player entry with
 * selection/flag masks overlaid. */
__attribute__((used, externally_visible))
void ViewSquadAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    MenuEntry *entry4 = &_menuEntries[4]; /* 0xE0/56 = 4 */
    uint16_t d6 = a5->ordinal - entry4->ordinal;
    uint16_t d1 = d6;
    d6++;

    /* Special case: slot 16 (entry index 16 relative) → set d6 = 0xFFFF */
    if (d1 == 0x10)
        d6 = 0xFFFF;

    uint16_t outSlot, outPlayerIndex;
    uint8_t *player = GetPlayerInfoBreakPosition(
        (TeamFileEx *)_squadTeamPtr, d1, &outSlot, &outPlayerIndex);

    /* ASM: GetPlayerInfoBreakPosition overwrites d1 with the file index.
     * The mask checks below must use the file index, not the display row.
     * Mask to 0-31 to match ASM btst behavior (btst uses d1 mod 32). */
    d1 = outPlayerIndex & 0x1F;

    uint16_t style = 0;
    a5->isDisabled = 0;

    /* Check viewSquadMaskLo */
    if (_viewSquadMaskLo & (1u << d1)) {
        a5->isDisabled = 1;
        style = 9;
    }
    /* Check viewSquadFlags — overrides maskLo */
    if (_viewSquadFlags & (1u << d1)) {
        a5->isDisabled = 1;
        style = 1;
    }
    /* Check viewSquadMaskHi */
    if (_viewSquadMaskHi & (1u << d1)) {
        style = 0xD;
    }
    /* viewSquadArg2 forces disabled */
    if (_viewSquadArg2) {
        a5->isDisabled = 1;
    }

    uint16_t yCoord = a5->y;
    BuildTransferPlayerMenuEntry(&_menuEntries[23], player, yCoord,
                                   (int16_t)d6, style, 0);
    DrawMultipleItems(12, &_menuEntries[23]);
}

/* ================================================================
 * Chunk 164: ShowSquad / ShowSquadWithFlags
 * ================================================================ */

extern uint8_t _viewSquadMenu[];

/* ShowSquad (Chunk 164)
 * Core squad viewing function: stores params to globals, shows viewSquadMenu,
 * returns selected player ordinal. viewSquadFlags is NOT cleared here —
 * the asm bridge for ShowSquad clears it, ShowSquadWithFlags sets it from d4.
 * Asm: a0=teamPtr, a1=title, d0=maskLo, d1=maskHi, d2=arg2, d3=arg3
 * Returns: d0 = viewSquadSelectedPlayer, d3 = viewSquadSelectedTeam */
__attribute__((used, externally_visible))
int16_t ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo,
                    uint32_t maskHi, uint16_t arg2, uint16_t arg3)
{
    _squadTeamPtr = teamPtr;
    _viewSquadTitle = (uintptr_t)title;
    _viewSquadMaskLo = maskLo;
    _viewSquadMaskHi = maskHi;
    _viewSquadArg2 = arg2;
    _viewSquadArg3 = arg3;
    ShowMenu(_viewSquadMenu);
    return (int16_t)_viewSquadSelectedPlayer;
}

/* CheckNumberOfTeams — update "select N more/less teams" menu entries (SWOS2.s line 60199) */
extern uint8_t _aSelect1MoreTea[];  /* "SELECT 1 MORE TEAM" */
extern uint8_t _aSelect0MoreTea[];  /* "SELECT %0 MORE TEAMS" */
extern uint8_t _aSelect1LessTea[];  /* "SELECT 1 LESS TEAM" */
extern uint8_t _aSelect0LessTea[];  /* "SELECT %0 LESS TEAMS" */
extern uint8_t _aError[];           /* "ERROR" */
extern uint8_t _aSelect1CpuTeam[];  /* "SELECT 1 COMPUTER TEAM" */

__attribute__((used, externally_visible))
void CheckNumberOfTeams(void)
{
    /* Entry indices (from asm offsets):
     * 87 = "change teams" button (offset 0x1308)
     * 89 = "play" button (offset 0x1378)
     * 90 = "select N more/less" text (offset 0x13B0)
     */
    if (_selectTeamsNoLimit) {
        _menuEntries[89].isInvisible = 1;
        _menuEntries[90].isInvisible = 1;
        _menuEntries[87].isInvisible = 1;
        return;
    }

    int16_t diff = (int16_t)(_numSelectedTeams - _numNeededTeams);
    if (diff == 0 && g_shootingPracticeMode && !ShootingPractice_SelectionOk()) {
        /* Both teams picked, but not as one human side and one computer side --
         * the drill has nothing to strip, or nobody to shoot. Say so where the
         * count message goes instead of starting a match the mode can't apply
         * to (shooting_practice.h). */
        _menuEntries[89].isInvisible = 1;
        _menuEntries[90].isInvisible = 0;
        _menuEntries[87].isInvisible = (_numSelectedTeams == 0) ? 1 : 0;
        StringCopy(_aSelect1CpuTeam, (uint8_t *)_menuEntries[90].fg);
        return;
    }
    if (diff == 0) {
        /* Exact count: show "play" button, hide "select more/less" */
        _menuEntries[89].isInvisible = 0;
        _menuEntries[90].isInvisible = 1;
        _menuEntries[89].content = 2;
        _menuEntries[89].fg = _playWhatStr;
    } else {
        /* Wrong count: hide "play", show "select more/less" */
        _menuEntries[89].isInvisible = 1;
        _menuEntries[90].isInvisible = 0;
    }

    /* Entry 87 ("change teams") visible only if at least 1 team selected */
    _menuEntries[87].isInvisible = (_numSelectedTeams == 0) ? 1 : 0;

    /* Build status text in entry 90's string buffer */
    uint8_t *outBuf = (uint8_t *)_menuEntries[90].fg;
    uint8_t *fmt;
    uint16_t absDiff;

    if (diff == 0) {
        /* Should not reach here (handled above), but assembly has "ERROR" path */
        StringCopy(_aError, outBuf);
        return;
    } else if (diff < 0) {
        absDiff = (uint16_t)(-diff);
        if (absDiff == 1) {
            StringCopy(_aSelect1MoreTea, outBuf);
            return;
        }
        fmt = _aSelect0MoreTea;
    } else {
        absDiff = (uint16_t)diff;
        if (absDiff == 1) {
            StringCopy(_aSelect1LessTea, outBuf);
            return;
        }
        fmt = _aSelect0LessTea;
    }
    PrintFormatted(fmt, outBuf, absDiff, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}


/* ==========================================================================
 * Chunk 201 — ChooseTeamsDialog + ChooseTeamsDialogWithNationality
 * (SWOS2.s lines 54900-54963)
 * ========================================================================== */

extern uint16_t _selectedTeamCountryNumber;
extern TeamFileEx *_selectedTeamPtr;
extern uint8_t  _aStatistics[];

/* ChooseTeamsDialog — set up team selection globals and show choose-teams menu.
 * If teamsType >= 0 after menu, calls SelectTeamsFinalMenu(255).
 * Assembly: d2=showClub, d3=showNational, d4=showOrigCustom, a0=title, a1=onSelect.
 * Returns via globals: selectedTeamCountryNumber, selectedTeamPtr. */
__attribute__((used, externally_visible))
void ChooseTeamsDialog(uint16_t showClub, uint16_t showNational, uint16_t showOrigCustom,
                        uint8_t *title, void *onSelect)
{
    _showClubTeams = showClub;
    _showNationalTeams = showNational;
    _showOrigCustomTeams = showOrigCustom;
    _gameTypeStr = 0;
    _chooseTeamsHeader1stPart = (uintptr_t)title;
    _chooseTeamsFunc = (uintptr_t)onSelect;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    chooseTeamsKeepListOpen = 0;     /* never inherit it from an earlier dialog */
    /* seq instruction: sets high byte of word to 0xFF when a1==0 (big-endian) */
    if (onSelect == (void *)0)
        _careerSquadScreenDidSelectPlayerFlag = 0xFF00;
    _playWhatStr = (uintptr_t)_aStatistics;
    _numNeededTeams = 1;
    _selectTeamsNoLimit = 1;
    _selectedTeamCountryNumber = 0xFFFF;
    _selectedTeamPtr = (TeamFileEx *)0;
    ShowMenu(_chooseTeamsMenu);
    if ((int16_t)_teamsType >= 0) {
        _loadTeam = 1;
        _showCupsAndOther = 0;
        _playSeason = 0;
        SelectTeamsFinalMenu(255);
    }
}

/* ChooseTeamsDialogWithNationality — same as ChooseTeamsDialog but passes
 * nationality to SelectTeamsFinalMenu instead of 255.
 * Assembly: d0=nationality, d2=showClub, d3=showNational, d4=showOrigCustom,
 * a0=title, a1=onSelect.
 * Returns via globals: selectedTeamCountryNumber, selectedTeamPtr. */
__attribute__((used, externally_visible))
void ChooseTeamsDialogWithNationality(uint16_t nationality, uint16_t showClub,
                                       uint16_t showNational, uint16_t showOrigCustom,
                                       uint8_t *title, void *onSelect)
{
    _showClubTeams = showClub;
    _showNationalTeams = showNational;
    _showOrigCustomTeams = showOrigCustom;
    _gameTypeStr = 0;
    _chooseTeamsHeader1stPart = (uintptr_t)title;
    _chooseTeamsFunc = (uintptr_t)onSelect;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    chooseTeamsKeepListOpen = 0;     /* never inherit it from an earlier dialog */
    /* seq instruction: sets high byte of word to 0xFF when a1==0 (big-endian) */
    if (onSelect == (void *)0)
        _careerSquadScreenDidSelectPlayerFlag = 0xFF00;
    _playWhatStr = (uintptr_t)_aStatistics;
    _numNeededTeams = 1;
    _selectTeamsNoLimit = 1;
    _selectedTeamCountryNumber = 0xFFFF;
    _selectedTeamPtr = (TeamFileEx *)0;
    ShowMenu(_chooseTeamsMenu);
    if ((int16_t)_teamsType >= 0) {
        _loadTeam = 1;
        _showCupsAndOther = 0;
        _playSeason = 0;
        SelectTeamsFinalMenu(nationality);
    }
}


/* ==========================================================================
 * Chunk 202 — SelectTeamToManage
 * (SWOS2.s lines 50038-50050)
 * ========================================================================== */

extern uint8_t _aSelectTeamToMa[];

/* SelectTeamToManage — show club team selection dialog for career mode.
 * Wraps ChooseTeamsDialog with showClub=1, showNational=0, showOrigCustom=0.
 * Results available in globals: selectedTeamCountryNumber, selectedTeamPtr. */
__attribute__((used, externally_visible))
void SelectTeamToManage(void)
{
    ChooseTeamsDialog(1, 0, 0, _aSelectTeamToMa, (void *)0);
}


/* ==========================================================================
 * Chunk 204 — Career_ShowAddInternationalPlayerDialog
 * (SWOS2.s lines 64815-64835)
 * ========================================================================== */

uint16_t chooseTeamsKeepListOpen;    /* include/menus.h says what this is for */

extern uint16_t _careerAddIntlPlayerResult;
extern uintptr_t _careerAddIntlPlayerSelectedSrcPlayerPtr;
extern uintptr_t _careerAddIntlPlayerSelectedSrcPlayerNamePtr;
extern uint8_t  _nationalTeamFileId;
extern uint8_t  _aAddInternation[];
extern uint8_t  _aAddInternation_0[];
extern void AddInternationalPlayerHome(uint8_t *selectedTeam);
extern void AddInternationalPlayerAbroad(uint8_t *selectedTeam);

/* Additional externs for Chunk 205 */
extern uint16_t _careerAddIntlPlayerSelectedTeamNumber;
extern uint16_t _careerAddIntlPlayerSelectedPlayerOrdinal;
extern uint8_t  _nationalTeamNationality;
extern uint16_t _careerUnavailablePlayerList[];
extern uint16_t _careerUnavailablePlayerLookupTable[];
extern uint8_t  _currentMatchPlayers[];
extern TeamFileEx *GetTeamPointer(uint16_t teamId);
extern int16_t  ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo,
                            uint32_t maskHi, uint16_t arg2, uint16_t arg3);
extern void Career_CopyUnavailablePlayerToCareerTeamSlot(uint8_t *srcPlayer,
    uint8_t *dstTeam, uint8_t *srcName, uint16_t srcTeamNumber,
    uint16_t srcPlayerIndex, uint16_t dstSlotIndex);

/* Career_ShowAddInternationalPlayerDialog — show dialog to add international player.
 * If homeOrAbroadFlag==0: use nationality filter (home), else show all (abroad).
 * Results stored in globals: careerAddIntlPlayerResult,
 * careerAddIntlPlayerSelectedSrcPlayerPtr, careerAddIntlPlayerSelectedSrcPlayerNamePtr. */
__attribute__((used, externally_visible))
void Career_ShowAddInternationalPlayerDialog(uint16_t homeOrAbroadFlag)
{
    _careerAddIntlPlayerResult = 0xFFFF;
    if (homeOrAbroadFlag == 0) {
        /* Home: filter by nationality */
        ChooseTeamsDialogWithNationality((uint16_t)_nationalTeamFileId, 1, 0, 0,
                                          _aAddInternation, (void *)AddInternationalPlayerHome);
    } else {
        /* Abroad: show all club teams */
        ChooseTeamsDialog(1, 0, 0, _aAddInternation_0, (void *)AddInternationalPlayerAbroad);
    }
}


/* ==========================================================================
 * Chunk 205 — Career_AddInternationalPlayerCommon,
 *             AddInternationalPlayerHome, AddInternationalPlayerAbroad
 * (SWOS2.s lines 64829-64965)
 *
 * Called as chooseTeamsFunc callback with a0 = selected team entry pointer.
 * Displays a squad view filtered by nationality/unavailability, lets the
 * user pick a player, then copies that player into the career team.
 * ========================================================================== */

/* Career_AddInternationalPlayerCommon
 * selectedTeam: pointer to team selection data (first word = team number)
 * title: menu title string ("ADD INTERNATIONAL PLAYER (HOME/ABROAD)") */
__attribute__((used, externally_visible))
void Career_AddInternationalPlayerCommon(uint8_t *selectedTeam, uint8_t *title)
{
    uint16_t teamNumber = BE_READ_U16(selectedTeam);
    _careerAddIntlPlayerSelectedTeamNumber = teamNumber;

    TeamFileEx *teamEx = GetTeamPointer(teamNumber);
    uint8_t nat = _nationalTeamNationality;
    uint16_t teamNum = teamEx->team.globalTeamNumber;

    /* Loop 1: Mark unavailable players with injury flag in cardsInjuries.
     * For each player matching the national team nationality, check if
     * they appear in careerUnavailablePlayerList (4 entries). If found,
     * set injury bits in cardsInjuries. */
    for (uint16_t d2 = 0; d2 < 16; d2++) {
        if (teamEx->players[d2].nationality == (int8_t)nat) {
            uint16_t *entry = _careerUnavailablePlayerList;
            for (int16_t d4 = 3; d4 >= 0; d4--) {
                if (entry[0] == teamNum && entry[1] == d2) {
                    uint8_t ci = teamEx->players[d2].cardsInjuries;
                    ci = (ci & 0xFC) | 0x08;
                    teamEx->players[d2].cardsInjuries = ci;
                    break;
                }
                entry += 2;  /* next 4-byte entry */
            }
        }
    }

    /* Loop 2: Build nationality mask — bit set means player does NOT
     * match the national team nationality (greyed out in squad view). */
    uint32_t natMask = 0;
    for (uint16_t d2 = 0; d2 < 16; d2++) {
        if (teamEx->players[d2].nationality != (int8_t)nat) {
            natMask |= (1UL << d2);
        }
    }

    /* Loop 3: Build unavailable mask from careerUnavailablePlayerLookupTable
     * (20 entries). If the entry's team number matches, mark that player
     * index bit in both masks. */
    uint32_t unavailMask = 0;
    uint16_t *lookup = _careerUnavailablePlayerLookupTable;
    for (int16_t i = 19; i >= 0; i--) {
        if (lookup[0] == _careerAddIntlPlayerSelectedTeamNumber) {
            uint16_t pidx = lookup[1];
            natMask |= (1UL << pidx);
            unavailMask |= (1UL << pidx);
        }
        lookup += 2;  /* next 4-byte entry */
    }

    /* Show squad selection screen */
    _careerAddIntlPlayerResult = 0xFFFF;
    _careerSquadScreenDidSelectPlayerFlag = 0;
    _viewSquadFlags = unavailMask;
    int16_t selected = ShowSquad((TeamFile *)teamEx, title, natMask, 0, 0, 0xFFFF);

    if (selected < 0)
        return;

    _careerSquadScreenDidSelectPlayerFlag = 1;
    _careerAddIntlPlayerSelectedPlayerOrdinal = (uint16_t)selected;

    /* Calculate source player pointers using block pointer convention:
     * block_ptr = teamBase + ordinal * sizeof(PlayerFile)
     * PlayerFile fields at block_ptr + sizeof(TeamFile) */
    uint8_t *srcBlock = (uint8_t *)teamEx + (uint16_t)((uint16_t)selected * sizeof(PlayerFile));
    _careerAddIntlPlayerSelectedSrcPlayerPtr = (uintptr_t)srcBlock;
    _careerAddIntlPlayerSelectedSrcPlayerNamePtr = (uintptr_t)teamEx->team.teamName;

    /* Get position type of selected source player */
    uint16_t srcPos = (teamEx->players[selected].positionAndFace & PF_POSITION_MASK) >> 5;

    /* Check if career team is full (max 20 players) */
    if (_careerNumPlayers >= 20) {
        _careerAddIntlPlayerResult = 1;
        return;
    }

    /* Search for an empty, position-compatible slot in the career team */
    uint16_t dstSlot, dstFileIdx;
    for (int16_t d7 = 0; d7 < 20; d7++) {
        uint8_t *careerPlayer = GetPlayerInfoBreakPosition(
            &_careerTeam, (uint16_t)d7, &dstSlot, &dstFileIdx);
        PlayerFile *pf = (PlayerFile *)(careerPlayer + sizeof(TeamFile));

        /* Check if slot is empty (name starts with 0xFF) */
        if ((uint8_t)pf->playerName[0] != 0xFF)
            continue;

        /* Position compatibility:
         * goalkeeper (0) → only goalkeeper slots
         * outfield (>0) → only outfield slots */
        uint16_t dstPos = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
        if (srcPos == 0) {
            if (dstPos != 0)
                continue;
        } else {
            if (dstPos == 0)
                continue;
        }

        /* Found a compatible slot. If it is out in the overflow region, rotate
         * the new player DOWN into the squad list so he is actually visible.
         *
         * The asm rotates to a hardcoded slot 16 (`move.b d2,$10(a1)`,
         * SWOS2.s:105383), which is right only when slots 0..15 are full — the
         * case it was written for, a full squad. Remove a few players and it is
         * wrong: the reserved second-keeper FILE (index 11) gets rotated out to
         * the overflow region by the remove path, so adding a keeper lands him
         * at slot 16 — the unnumbered 17th row — while slots 11..15 sit empty.
         * The tester's words: "now the 2nd GK spot is not on No. 12".
         *
         * Rotate to the LOWEST FREE display slot instead. The rotate permutes
         * currentMatchPlayers, so each file's position type travels with it: the
         * keeper file lands on slot 11 and row 12 becomes the keeper spot again,
         * exactly where the original puts it on a freshly built squad. Folds to
         * the asm whenever 0..15 are occupied, because then the lowest free slot
         * IS 16. Use volatile to prevent GCC turning this into a memmove (no
         * libc). */
        if (dstSlot >= 16) {
            volatile uint8_t *cmp = _currentMatchPlayers;
            uint16_t home = dstSlot;
            for (uint16_t k = 0; k < dstSlot; k++) {
                uint8_t *occ = GetPlayerAtIndex(_selTeamNumber, k);
                PlayerFile *opf = (PlayerFile *)(occ + sizeof(TeamFile));
                if ((uint8_t)opf->playerName[0] == 0xFF) { home = k; break; }
            }
            if (home < dstSlot) {
                uint8_t saved = cmp[dstSlot];
                uint16_t idx = dstSlot;
                while (idx != home) {
                    cmp[idx] = cmp[idx - 1];
                    idx--;
                }
                cmp[home] = saved;
            }
        }

        /* Copy the international player into the career team slot */
        Career_CopyUnavailablePlayerToCareerTeamSlot(
            srcBlock, careerPlayer,
            (uint8_t *)teamEx + 5,  /* srcName = team name pointer */
            _careerAddIntlPlayerSelectedTeamNumber,
            _careerAddIntlPlayerSelectedPlayerOrdinal,
            dstFileIdx);

        _careerNumPlayers++;
        _careerAddIntlPlayerResult = 0;
        /* He is IN the squad, so there is nothing to come back to the squad
         * screen for: keep the club list open and let the manager walk to the
         * next club. Only on this path -- a pending add (result 1 below) has to
         * return, because the player is not placed until a squad slot is picked
         * to replace. */
        chooseTeamsKeepListOpen = 1;
        return;
    }

    /* No compatible empty slot found */
    _careerAddIntlPlayerResult = 1;
}

/* AddInternationalPlayerHome — callback for home nationality team selection.
 * selectedTeam passed in a0 from menu callback (chooseTeamsFunc). */
__attribute__((used, externally_visible))
void AddInternationalPlayerHome(uint8_t *selectedTeam)
{
    Career_AddInternationalPlayerCommon(selectedTeam, _aAddInternation);
}

/* AddInternationalPlayerAbroad — callback for abroad team selection.
 * selectedTeam passed in a0 from menu callback (chooseTeamsFunc). */
__attribute__((used, externally_visible))
void AddInternationalPlayerAbroad(uint8_t *selectedTeam)
{
    Career_AddInternationalPlayerCommon(selectedTeam, _aAddInternation_0);
}


/* ==========================================================================
 * Chunk 206 — National Team Selection family
 * (SWOS2.s lines 65499-65745)
 *
 * SelectNationalTeamInit, SelectNationalTeamFinish,
 * Career_RebuildInternationalPlayerSelectionMasks,
 * NationalTeamAddPlayer, NationalTeamRemovePlayers,
 * SelectNationalTeamAbort, SelectNationalTeamCommon,
 * SelectNationalTeamAfterDrawCommon
 * ========================================================================== */

extern uint32_t _careerIntlPlayersRemoveMask;
extern uint32_t _careerIntlPlayersWrongTypeMask;
extern uint32_t _careerIntlPlayersSelectableMask;
extern uint16_t _careerNationalTeamHasPendingAddPlayer;
extern uint16_t _selectNationalTeamMenuResult;
extern uint16_t _careerNationalTeamAddPlayerType;
extern uint8_t  _careerIntlPlayerSelectedPlayerDataCopy[];
extern uint8_t  _careerIntlPlayerSelectedTeamName[];
extern uint8_t  _nationalTeamAddPlayerMenu[];
extern uint8_t  _careerUnavailablePlayerNames[];
extern uint16_t _selTeamNumber;

extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern void Career_UpdateNationalTeamMenuEntries(void);
extern void Career_UpdateRemovePlayersMenuEntry(void);
extern void Career_RemoveInternationalPlayersByMask(uint32_t removeMask);

/* SelectNationalTeamInit — onInit for the national team selection menu.
 * Clears remove mask and pending add flag. If squad not full, enables
 * "add player" button (entry 3).
 *
 * Asm SelectNationalTeamInit ends with `bra.w *+4` — a literal
 * branch-to-PC+4 that on Amiga's sequential code layout falls through
 * into the SelectNationalTeamFinish function laid out immediately
 * after. So Init implicitly chains into Finish, which is what
 * populates the menu title ("GERMANY SQUAD") and the
 * "SELECT N MORE INTERNATIONAL SQUAD PLAYERS" subtitle via
 * Career_UpdateNationalTeamMenuEntries. Our C translation initially
 * treated Init and Finish as two separate callbacks, so on first show
 * the title stayed at the "STDMENUTEXT" placeholder and the red
 * subtitle bar said "TO REPLACE" (Playaveli's GERMANY.CAR SELECT
 * SQUAD repro). Mirror the fall-through explicitly. */
__attribute__((used, externally_visible))
void SelectNationalTeamInit(void)
{
    _careerIntlPlayersRemoveMask = 0;
    _careerNationalTeamHasPendingAddPlayer = 0;
    if (_careerNumPlayers != 20) {
        SetCurrentEntry(3);
    }
    SelectNationalTeamFinish();
}

/* SelectNationalTeamFinish — onReturn for the national team selection menu.
 * Adjusts entry layout, disables play button, sets string flag,
 * then updates menu state. */
__attribute__((used, externally_visible))
void SelectNationalTeamFinish(void)
{
    _menuEntries[38].x -= 25;
    _menuEntries[7].isDisabled = 1;
    uint16_t sf = _menuEntries[1].stringFlags;
    sf |= (1 << 13);
    _menuEntries[1].stringFlags = sf;
    Career_UpdateNationalTeamMenuEntries();
    Career_RebuildInternationalPlayerSelectionMasks();
    Career_UpdateRemovePlayersMenuEntry();
}

/* Career_RebuildInternationalPlayerSelectionMasks
 * Scans career team slots (0-19). For each occupied slot, checks if the
 * player's position type is compatible with the pending add player's type.
 * Builds two masks:
 *   careerIntlPlayersSelectableMask: bit CLEAR = selectable
 *   careerIntlPlayersWrongTypeMask: bit SET = wrong position type */
__attribute__((used, externally_visible))
void Career_RebuildInternationalPlayerSelectionMasks(void)
{
    /* loc_147376 is a block pointer for the pending player data copy.
     * In asm, loc_147376 = careerIntlPlayerSelectedPlayerDataCopy - 76.
     * The block pointer convention: adding 0x66 reaches positionAndFace. */
    PlayerFile *pendingPf = (PlayerFile *)_careerIntlPlayerSelectedPlayerDataCopy;
    uint16_t pendingPos = (pendingPf->positionAndFace & PF_POSITION_MASK) >> 5;

    uint32_t wrongMask = 0;
    uint32_t selectMask = 0xFFFFFFFF;  /* all bits set = all selectable */

    for (int16_t d1 = 19; d1 >= 0; d1--) {
        uint8_t *player = GetPlayer(_selTeamNumber, (uint16_t)d1);
        PlayerFile *pf = (PlayerFile *)(player + sizeof(TeamFile));

        /* Skip empty slots */
        if ((uint8_t)pf->playerName[0] == 0xFF)
            continue;

        /* Check position compatibility if pending add is active */
        if (_careerNationalTeamHasPendingAddPlayer) {
            uint16_t slotPos = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
            if (pendingPos == 0) {
                /* Pending is GK: only GK slots are compatible */
                if (slotPos != 0) {
                    wrongMask |= (1UL << d1);
                    continue;
                }
            } else {
                /* Pending is outfield: only non-GK slots */
                if (slotPos == 0) {
                    wrongMask |= (1UL << d1);
                    continue;
                }
            }
        }

        /* Slot is selectable: clear its bit */
        selectMask &= ~(1UL << d1);
    }

    _careerIntlPlayersSelectableMask = selectMask;
    _careerIntlPlayersWrongTypeMask = wrongMask;
}

/* NationalTeamAddPlayer — shows the home/abroad sub-menu, then lets user
 * select a player. If player selected but no room in career team (result==1),
 * copies player data for pending addition. */
__attribute__((used, externally_visible))
void NationalTeamAddPlayer(void)
{
    _careerIntlPlayersRemoveMask = 0;
    ShowMenu(_nationalTeamAddPlayerMenu);
    int16_t addType = (int16_t)_careerNationalTeamAddPlayerType;

    if (addType >= 0) {
        Career_ShowAddInternationalPlayerDialog((uint16_t)addType);

        if (_careerAddIntlPlayerResult == 1) {
            /* No room right now — store as pending add */
            StringCopy((uint8_t *)_careerAddIntlPlayerSelectedSrcPlayerNamePtr,
                         _careerIntlPlayerSelectedTeamName);

            /* Copy 38 bytes of PlayerFile data from source player */
            uint8_t *src = (uint8_t *)_careerAddIntlPlayerSelectedSrcPlayerPtr + sizeof(TeamFile);
            volatile uint8_t *dst = (volatile uint8_t *)_careerIntlPlayerSelectedPlayerDataCopy;
            for (int16_t i = 37; i >= 0; i--) {
                *dst++ = *src++;
            }

            _careerNationalTeamHasPendingAddPlayer = 1;
            SetCurrentEntry(5);
            Career_UpdateRemovePlayersMenuEntry();
        }
    }

    Career_UpdateNationalTeamMenuEntries();
    Career_RebuildInternationalPlayerSelectionMasks();
    DrawMenu();
}

/* NationalTeamRemovePlayers — removes marked international players and
 * updates the UI. */
__attribute__((used, externally_visible))
void NationalTeamRemovePlayers(void)
{
    Career_RemoveInternationalPlayersByMask(_careerIntlPlayersRemoveMask);
    _careerIntlPlayersRemoveMask = 0;
    Career_UpdateNationalTeamMenuEntries();
    Career_RebuildInternationalPlayerSelectionMasks();
    Career_UpdateRemovePlayersMenuEntry();
    SetCurrentEntry(4);
    if (_careerNumPlayers != 20) {
        SetCurrentEntry(3);
    }
    DrawMenu();
}

/* SelectNationalTeamAbort — cancels pending add and resets UI. */
__attribute__((used, externally_visible))
void SelectNationalTeamAbort(void)
{
    _careerNationalTeamHasPendingAddPlayer = 0;
    Career_UpdateNationalTeamMenuEntries();
    Career_RebuildInternationalPlayerSelectionMasks();
    Career_UpdateRemovePlayersMenuEntry();
    SetCurrentEntry(3);
    DrawMenu();
}

/* SelectNationalTeamCommon — handles clicking a player slot in the
 * national team menu. If a pending add is in progress, places the
 * pending player into the selected slot. Otherwise toggles the
 * remove-mark on the selected player.
 * entry = a5 from asm (current menu entry). */
__attribute__((used, externally_visible))
void SelectNationalTeamCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    /* Get player index from current entry's ordinal relative to entry 8 */
    uint16_t d1 = a5->ordinal - _menuEntries[8].ordinal;

    uint16_t slot, fileIdx;
    uint8_t *careerPlayer = GetPlayerInfoBreakPosition(
        &_careerTeam, d1, &slot, &fileIdx);

    if (_careerNationalTeamHasPendingAddPlayer) {
        /* Place pending player into selected slot.
         * loc_147376 = careerIntlPlayerSelectedPlayerDataCopy - 76 is the
         * "block pointer" for the pending player data copy. */
        uint8_t *pendingBlock = _careerIntlPlayerSelectedPlayerDataCopy - sizeof(TeamFile);

        Career_CopyUnavailablePlayerToCareerTeamSlot(
            pendingBlock, careerPlayer,
            _careerIntlPlayerSelectedTeamName,
            _careerAddIntlPlayerSelectedTeamNumber,
            _careerAddIntlPlayerSelectedPlayerOrdinal,
            fileIdx);

        _careerNationalTeamHasPendingAddPlayer = 0;
        SetCurrentEntry(3);
        Career_UpdateNationalTeamMenuEntries();
        Career_UpdateRemovePlayersMenuEntry();
        Career_RebuildInternationalPlayerSelectionMasks();
        DrawMenu();
    } else {
        /* Toggle remove mark on this player.
         *
         * The bit is fileIdx, NOT the visual row d1. In the asm both this and
         * selectNationalTeamMenuResult come from the SAME register:
         *
         *     move.w 2(a5),d1                    ; d1 = visual row
         *     jsr    GetPlayerInfoBreakPosition  ; RETURNS d1 = file index
         *     ...
         *   loc_1472B8:
         *     move.w d1,(selectNationalTeamMenuResult)
         *     bchg   d1,d0                       ; toggle bit d1  (SWOS2.s:106351)
         *
         * GetPlayerInfoBreakPosition overwrites d1 with the file index, so by
         * the toggle d1 is no longer the row. We kept a separate C variable
         * holding the visual row and used it for the bit, while correctly using
         * fileIdx for the menu result -- the two cannot both be right.
         *
         * It matters because visual != storage: for rows 0..10 the row is
         * remapped through the formation's position table
         * (GetPlayerInfoBreakPosition, edit_tactics.c:2063), and
         * Career_RemoveInternationalPlayersByMask walks STORAGE slots. So
         * selecting a player marked a different one for removal -- reported as
         * "select a player, hit REMOVE PLAYER, that player is not removed...
         * you do this three times and all of a sudden empty slots appear". */
        _selectNationalTeamMenuResult = fileIdx;
        _careerIntlPlayersRemoveMask ^= (1UL << fileIdx);
        Career_UpdateRemovePlayersMenuEntry();
        DrawMenu();
    }
}

/* SelectNationalTeamAfterDrawCommon — after-draw callback for each player
 * slot in the national team menu. Draws player info row with appropriate
 * coloring based on position compatibility, selection state, and remove marks.
 *
 * Uses the "block pointer" convention for BuildTransferPlayerMenuEntry:
 * block_ptr + 0x4C = PlayerFile.nationality, etc.
 * entry = a5 from asm (current menu entry). */
__attribute__((used, externally_visible))
void SelectNationalTeamAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    int16_t d6;
    uint16_t d5;
    uint8_t *a6_player;  /* player block pointer for BuildTransferPlayerMenuEntry */
    uint8_t *a4_extra;   /* extra name pointer */

    if (a5->ordinal == 7) {
        /* Entry 7 = pending add player display slot */
        if (!_careerNationalTeamHasPendingAddPlayer)
            return;

        /* Use the pending player data copy as block pointer.
         * loc_147376 = careerIntlPlayerSelectedPlayerDataCopy - 76. */
        a6_player = _careerIntlPlayerSelectedPlayerDataCopy - sizeof(TeamFile);
        d6 = -1;  /* 0xFFFF */
        d5 = 0x0D;
        a4_extra = _careerIntlPlayerSelectedTeamName;
    } else {
        /* Normal player slot */
        uint16_t relIdx = a5->ordinal - _menuEntries[8].ordinal;
        d6 = (int16_t)(relIdx + 1);
        uint16_t d1 = relIdx;

        /* Entry for last slot (ordinal delta == 16) gets d6 = -1 */
        if (d1 == 16)
            d6 = -1;

        uint16_t slot, fileIdx;
        uint8_t *careerPlayer = GetPlayerInfoBreakPosition(
            &_careerTeam, d1, &slot, &fileIdx);
        a6_player = careerPlayer;

        /* Default color based on position type */
        d5 = 0;
        PlayerFile *pf = (PlayerFile *)(careerPlayer + sizeof(TeamFile));
        uint16_t posType = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
        if (posType == 0)
            d5 = 0x0E;  /* goalkeeper color */

        /* Disable entry if wrong position type.
         *
         * Indexed by fileIdx, NOT the visual row — the same domain as the
         * remove mask below and for the same reason. Both masks are BUILT by
         * Career_RebuildInternationalPlayerSelectionMasks looping
         * `GetPlayer(_selTeamNumber, i)` and setting bit i, and GetPlayer's
         * second parameter is the FILE index (GetPlayerAtIndex is the one that
         * takes an ordinal, and it resolves it to a file index before calling
         * GetPlayer). The asm agrees on both halves: the builder is
         * `move.w #$13,d1 / bsr GetPlayer / bset d1,d5 / bclr d1,d2`
         * (SWOS2.s:106188), and the reader loads the visual row into d1, lets
         * GetPlayerInfoBreakPosition OVERWRITE d1 with the file index, then
         * `btst d1` — SWOS2.s:106349 and :106357, immediately above the
         * remove-mask btst that shares the very same register. */
        a5->isDisabled = 0;
        if (_careerIntlPlayersWrongTypeMask & (1UL << fileIdx)) {
            a5->isDisabled = 1;
            d5 = 9;
        }

        /* Disable entry if not selectable — same domain, same asm register. */
        if (_careerIntlPlayersSelectableMask & (1UL << fileIdx)) {
            a5->isDisabled = 1;
        }

        /* If not in pending add mode, check remove mask.
         *
         * Indexed by fileIdx, matching the toggle in SelectNationalTeamCommon
         * (which follows the asm: GetPlayerInfoBreakPosition returns the file
         * index in d1, and `bchg d1` toggles that bit -- SWOS2.s:106351). The
         * row's visual index is NOT the same thing, and testing the mask with
         * it highlighted a different row than the one that would actually be
         * removed: "I hit fire on Southgate and Ian Wright got highlighted...
         * The actual removal works fine". fileIdx is already to hand here --
         * this after-draw makes the very same GetPlayerInfoBreakPosition call.
         *
         * The starting eleven looked right because for those rows the file
         * index and the row happen to coincide; the substitutes are where they
         * diverge, which is exactly where the tester saw it. */
        if (!_careerNationalTeamHasPendingAddPlayer) {
            if (_careerIntlPlayersRemoveMask & (1UL << fileIdx)) {
                d5 = 0x0D;
            }
        }

        /* Set extra name pointer from careerUnavailablePlayerNames */
        /* Indexed by fileIdx, like every other access to this array.
         * Career_CopyUnavailablePlayerToCareerTeamSlot WRITES the club name at
         * dstSlotIndex * 17 and SelectNationalTeamCommon passes fileIdx as that
         * argument; menus.c:1726 reads it back the same way. This site used the
         * visual row instead, so once the two diverge the row picks up a
         * different slot's string -- usually an empty one, which is why an added
         * player showed "()" where his club should be. Same mix-up as the remove
         * highlight above; fileIdx is already to hand from the shared
         * GetPlayerInfoBreakPosition call. */
        a4_extra = _careerUnavailablePlayerNames + fileIdx * 17;
    }

    /* Call BuildTransferPlayerMenuEntry and draw */
    uint16_t y = a5->y;
    BuildTransferPlayerMenuEntry(
        (void *)&_menuEntries[27],
        a6_player,
        y,
        d6,
        d5,
        a4_extra);

    /* Hide entry 37 */
    _menuEntries[37].isInvisible = 1;

    DrawMultipleItems(12, &_menuEntries[27]);
}

/* ---- Chunk 217: SelectAndShowSquadToView + StatsViewSquads + SquadMenu ---- */
extern uint16_t _inEditTactics;
extern DIYCompetitionEx _diyCompetitionStart;

/* ================================================================
 * ShowSquadMenuForTeam  (SWOS2.s loc_1391C4)
 *
 * Common helper: sets up squad menu state and shows it.
 * teamPtr = pointer to team file, rowMode = squad display mode.
 * ================================================================ */
static void ShowSquadMenuForTeam(uint8_t *teamPtr, uint16_t rowMode)
{
    _squadTeamFilePtr = (uintptr_t)teamPtr;
    _squadRowMode = rowMode;
    _inEditTactics = rowMode;

    /* Read skill diff param from diyCompetitionStart offset 0x2C (word) */
    uint16_t d0 = *(uint16_t *)&_diyCompetitionStart.base.compSlot;
    if (d0 == 4)
        d0 = 0;
    _teamSkillDiffAfterChange = d0;

    /* For game types other than DIY(1) or preset(2), and team isn't computer,
     * set skill diff to -1 (disabled) */
    uint16_t gt = _gameType;
    if (gt != 1 && gt != 2) {
        if (((TeamFileEx *)teamPtr)->team.teamControls != 1) {
            _teamSkillDiffAfterChange = 0xFFFF;
        }
    }

    ShowMenu(_squadMenu);
}

/* ================================================================
 * SelectAndShowSquadToView  (SWOS2.s line 47290)
 *
 * Shows "SELECT SQUAD TO VIEW" dialog, then opens squad menu.
 * Called with teamNumbers in a1 and numTeams in d0 from caller.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectAndShowSquadToView(uint16_t *teamNumbers, uint16_t numTeams, uint16_t rowMode)
{
    TeamFileEx *teamPtr = ChooseTeamForEditTactics(_aSelectSquadToV, teamNumbers, numTeams);
    if (teamPtr == 0)
        return;
    ShowSquadMenuForTeam((uint8_t *)teamPtr, rowMode);
}

/* ================================================================
 * SelectAndShowSquadToView_Direct  (SWOS2.s loc_1391AC)
 *
 * Loads team from selTeamNumber, then opens squad menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectAndShowSquadToView_Direct(uint16_t rowMode)
{
    TeamFileEx *teamPtr = VerifyTeamLoaded(_selTeamNumber);
    ShowSquadMenuForTeam((uint8_t *)teamPtr, rowMode);
}

/* ================================================================
 * StatsViewSquads  (SWOS2.s line 49177)
 *
 * Opens squad view from stats menu using DIY teams buffer.
 *
 * diyTeamsBufferPtr points into diyCompetitionStart where team IDs
 * are stored as Amiga BE byte pairs ([country, ordinal]) — same
 * encoding as the other cup/league team lists.
 * SelectAndShowSquadToView's downstream VerifyTeamLoaded expects
 * a host-native (country<<8)|ord value, so we BE-decode each entry
 * into a stack buffer first. The other ChooseTeamForEditTactics
 * caller (edit_tactics.c:3165) already does this via PACK_BE_U16;
 * this is the same conversion, just applied to a different source. */
__attribute__((used, externally_visible))
void StatsViewSquads(void)
{
    uint8_t *src = (uint8_t *)(uintptr_t)_diyTeamsBufferPtr;
    uint16_t numTeams = _diyNumberOfTeams;
    uint16_t rowMode = (uint16_t)_diyCompetitionStart.base.substitutesCount;
    uint16_t decoded[64];
    if (numTeams > 64) numTeams = 64;
    for (uint16_t i = 0; i < numTeams; i++)
        decoded[i] = BE_READ_U16(src + (uint32_t)i * 2);
    SelectAndShowSquadToView(decoded, numTeams, rowMode);
}

/* ================================================================
 * SquadMenu  (SWOS2.s line 50835)
 *
 * Opens squad menu from management/season context.
 * ================================================================ */
__attribute__((used, externally_visible))
void SquadMenu(void)
{
    uint16_t rowMode = _primaryCompetitionBuf.tourn.header.squadViewParam;

    if (_gameType != GAME_TYPE_SEASON) {
        /* Non-season: load team directly */
        SelectAndShowSquadToView_Direct(rowMode);
        return;
    }

    /* Season mode */
    if (_seasonNumberOfSquads != 0) {
        /* Season with squads: check if exactly 1 squad */
        if (_seasonNumberOfSquads == 1) {
            /* Load that team and go to direct squad view */
            TeamFileEx *teamPtr = VerifyTeamLoaded(_seasonSquadTeamNumbers[0]);
            ShowSquadMenuForTeam((uint8_t *)teamPtr, rowMode);
            return;
        }
        /* Multiple squads: show selection from squad team numbers */
        SelectAndShowSquadToView(_seasonSquadTeamNumbers, _seasonNumberOfSquads, rowMode);
        return;
    }

    /* No squads — use competition file buffer teams. Read via the typed
     * struct view: `competitionFileBuffer` mirrors diyCompetitionStart's
     * bytes (written by Career_StoreCompetitionSlotStateToBuffer's
     * word-by-word copy), so it has the same 64-bit struct layout where
     * post-namePtr fields shift by +4 vs Amiga. The asm-style raw
     * offsets (+50 numTeams, +302/+590/+154 team lists) were reading
     * stale field bytes on 64-bit. Use typed fields instead.
     * See project_raw_offset_64bit_shift_trap.md. */
    DIYCompetitionEx *cfbEx = (DIYCompetitionEx *)&_primaryCompetitionBuf;
    DIYCompetition   *cfb   = &cfbEx->base;
    uint16_t compType = (uint16_t)cfb->competitionType;
    uint16_t numT = (uint16_t)cfb->numTeams;
    uint16_t *teamList;

    if (compType == 0) {
        teamList = cfbEx->matchData.league.leagueTeamIds;  /* parent +0x12E */
    } else if (compType == 1) {
        teamList = cfbEx->cupTeamIds;                       /* parent +0x24E */
    } else {
        /* Tournament: team-id list is DIYTournamentState.teamIds (parent +0x9A).
         * competitionFileBuffer's tournament view keeps teamIds at byte 0x9A on
         * both 32- and 64-bit (the DIYTournamentState header pad self-adjusts),
         * so the typed access is correct cross-target. */
        teamList = ((DIYTournamentState *)cfb)->teamIds;
    }

    if (numT > 64) numT = 0;  /* safety clamp — should never trigger now */

    /* Team IDs in `competitionFileBuffer`'s leagueTeamIds/cupTeamIds/
     * tournamentTeamIds region are BE-packed `{country, ordinal}` byte
     * pairs (same convention as elsewhere in the SDL build). The
     * downstream `VerifyTeamLoaded` expects a host-native
     * (country<<8)|ordinal value, so a raw `*(uint16_t *)` read on LE
     * would byte-swap and panic on bogus IDs like 0x000E.
     * Mirror the BE-decode helper from StatsViewSquads (see below). */
    {
        uint16_t decoded[64];
        for (uint16_t i = 0; i < numT; i++)
            decoded[i] = BE_READ_U16((uint8_t *)&teamList[i]);
        SelectAndShowSquadToView(decoded, numT, rowMode);
    }
}

/* ===== CareerGameListCommon (SWOS2.s line 49351) =====
 * beforeDraw callback for career game list menu entries.
 * Fills menu entries 33-43 with competition/match info for a single game slot.
 * Parameter: item (MenuEntry *) — the current menu entry being drawn (a5 in asm).
 */

extern uint8_t *BuildMatchResultString(uint16_t control, uint16_t leg2, uint16_t leg1,
                                         uint16_t pens, uint8_t *teamA, uint8_t *teamB,
                                         uint16_t flags);
extern char *_careerGameTopTeamName;
extern char *_careerGameBottomTeamName;
extern uint16_t _careerGameListTeamControl;
extern const char *_divStringTable[];
extern const char *_roundStringTable[];
extern const char *_rndStringTable[];
extern uint16_t _currentDivisionIndex;
extern uint8_t  _aError[];       /* "ERROR" */
extern uint8_t  _aMinus[];       /* "-" */
extern uint8_t  _aV[];           /* "V" */
extern uint8_t  _aH[];           /* "(H)" */
extern uint8_t  _aA_2[];         /* "(A)" */
extern uint8_t  _aLeg1[];        /* "LEG 1" */
extern uint8_t  _aLeg2[];        /* "LEG 2" */
extern uint8_t  _aReplay[];      /* "REPLAY" */
extern uint8_t  _aPlayoffs[];    /* "PLAYOFFS" */
extern uint8_t  _aCup_1[];       /* "CUP" */
extern uint8_t  _aCup_2[];       /* "CUP" */
extern uint8_t *_careerCompetitionSlotPtrTable[];

__attribute__((used, externally_visible))
void CareerGameListCommon(MenuEntry *item)
{
    MenuEntry *a5 = item;
    /* d0 = ordinal difference between current item and entry 19 */
    MenuEntry *entry19 = &_menuEntries[19]; /* menuEntries + 0x428/0x38 */
    uint16_t d0 = a5->ordinal - entry19->ordinal;

    if (d0 == 0) {
        _columnMarker = _gameListY;
        _cglmVar3 = 0;
    }

    uint16_t d2 = _columnMarker;

    if (_cglmVar3 != 0)
        goto hideAll;

    uint16_t d4 = 0;
    uint16_t d3 = 0;

    /* gameListArray is now a typed void *[28] = 14 pairs of
     * (contestEntry, roundOrGame). Index by pair via d0 * 2 instead
     * of byte arithmetic — the old `d0 << 3 / 4` form assumed 4-byte
     * slots and would land mid-pointer on 64-bit. */
    uint8_t *a6 = (uint8_t *)_gameListArray[d0 * 2];     /* contest entry ptr */
    if (a6 == 0)
        goto hideAll;
    uint8_t *a4 = (uint8_t *)_gameListArray[d0 * 2 + 1]; /* round/game ptr */

    /* a5 = entry 33 (menuEntries + 0x738) */
    a5 = &_menuEntries[33];

    /* Hide entry 33 (first slot) */
    a5->isInvisible = 1;
    a5++; /* entry 34 */

    /* Initialize team name pointers to "ERROR" */
    _careerGameTopTeamName = (char *)_aError;
    _careerGameBottomTeamName = (char *)_aError;

    uint8_t *a2 = 0; /* result string pointer, NULL if no result */

    /* [cglm-dump] One-shot dump of cache + gameListArray state on first
     * draw of the career main screen. Diagnostic for the 64-bit
     * bring-up: stale fixtures + 22-byte cache slot overflow have been
     * causing VerifyTeamLoaded panics. Dumps the whole cache and the
     * gameListArray pairs so we can see which entries are well-formed
     * vs. corrupted. Always-on for now (one-shot, small output) — gate
     * with SWOS_DEBUG_AUTOSKIP and remove once the 22-byte slot
     * overflow is properly fixed and the new-career menu is stable. */
    {
        extern uint8_t _teamSelectionKeyCache[];
        extern uint16_t _teamSelectionKeyCacheCount;
        static int s_cglmDumpDone = 0;
        if (!s_cglmDumpDone) {
            s_cglmDumpDone = 1;
            int cacheCountS = (int)(int16_t)_teamSelectionKeyCacheCount;
            SWOS_DBG("[cglm-dump] d0=%u cacheCount=%d cacheBase=%p\n",
                    (unsigned)d0, cacheCountS,
                    (void *)_teamSelectionKeyCache);
            uint8_t *p = _teamSelectionKeyCache;
            for (int s = 0; s < cacheCountS && s < 40; s++) {
                uint16_t stride = ((TeamSelectionKeyCacheEntry *)p)->entryStride;
                SWOS_DBG("[cglm-dump]   slot[%d] @%+lld stride=%u "
                                "hdr=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X"
                                "  fx0=%02X %02X %02X %02X %02X %02X\n",
                        (int)s, (long long)(p - _teamSelectionKeyCache),
                        (unsigned)stride,
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                        p[10], p[11], p[12], p[13], p[14], p[15]);
                if (stride < 12 || stride > KEYCACHE_STRIDE_MAX) {
                    SWOS_DBG("[cglm-dump]   stride out of range, halting dump\n");
                    break;
                }
                p += stride;
            }
            for (uint16_t i = 0; i < 14; i++) {
                void *ce = _gameListArray[i * 2];
                void *ge = _gameListArray[i * 2 + 1];
                if (ce == 0 && ge == 0) continue;
                long long ceOff = ce ? (long long)((uint8_t *)ce - _teamSelectionKeyCache) : -1;
                long long geOff = ge ? (long long)((uint8_t *)ge - _teamSelectionKeyCache) : -1;
                SWOS_DBG("[cglm-dump]   gameListArray[%u] contest=+%lld game=+%lld",
                        (unsigned)i, ceOff, geOff);
                if (ge) {
                    uint8_t *g = (uint8_t *)ge;
                    SWOS_DBG("  bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                            g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11]);
                }
                SWOS_DBG("\n");
            }
            SWOS_DBG_FLUSH();
        }
    }

    if (a6[5] != 0) {
        /* Has teams — load them. Team IDs in the fixture record are
         * { countryByte, ordinalByte } pairs that the original asm
         * reads as a big-endian uint16 via `move.w`. Use BE_READ_U16
         * so the LE host produces the `(country<<8)|ordinal` value
         * findTeamInCache / VerifyTeamLoaded compare against. */
        uint16_t topTeamIdx = BE_READ_U16(a4 + 0x0C);
        /* [career-row] Per-call trace. Always-on diagnostic during
         * bring-up — log every team verify with d0, listIndex,
         * game offset, and resolved team ids. Lets us see which row
         * triggers a verify panic without having to guess. */
        {
            extern uint8_t _teamSelectionKeyCache[];
            extern uint16_t _teamSelectionKeyCache_ListIndex;
            uint16_t botTeamIdxPreview = BE_READ_U16(a4 + 0x0E);
            long long gameOff = (long long)((uint8_t *)a4 - _teamSelectionKeyCache);
            long long contestOff = (long long)((uint8_t *)a6 - _teamSelectionKeyCache);
            SWOS_DBG("[career-row] d0=%u listIdx=%u contest=+%lld game=+%lld "
                            "numGames=%u top=0x%04X bot=0x%04X\n",
                    (unsigned)d0, (unsigned)_teamSelectionKeyCache_ListIndex,
                    contestOff, gameOff, (unsigned)a6[5],
                    (unsigned)topTeamIdx, (unsigned)botTeamIdxPreview);
            SWOS_DBG_FLUSH();
        }
        TeamFileEx *_topTeamData = VerifyTeamLoaded(topTeamIdx);
        _careerGameTopTeamName = (char *)_topTeamData->team.teamName;

        uint16_t botTeamIdx = BE_READ_U16(a4 + 0x0E);
        TeamFileEx *botTeamData = VerifyTeamLoaded(botTeamIdx);
        /* Check if bottom team is player-controlled (byte 4 == 1 → sne → 0, else -1) */
        uint16_t ctrl;
        if (botTeamData->team.teamControls == 1)
            ctrl = 0;
        else
            ctrl = 0xFFFF;
        _careerGameListTeamControl = ctrl;
        _careerGameBottomTeamName = (char *)botTeamData->team.teamName;

        /* Check if match has been played (matchFlags != 0). a4 is the cache
         * entry base; the first match slot starts at +0x0A. */
        TeamSelectionKeyCacheMatchSlot *matchSlot = (TeamSelectionKeyCacheMatchSlot *)(a4 + 0x0A);
        if (matchSlot->matchFlags != 0) {
            uint16_t resultFlags = (uint16_t)matchSlot->matchFlags;
            uint16_t leg2Scores = matchSlot->packedGoals;
            uint16_t leg1Scores = matchSlot->aggregate;
            uint16_t penScores  = matchSlot->penaltyScore;
            uint8_t *resultStr = BuildMatchResultString(
                1, leg2Scores, leg1Scores, penScores,
                (uint8_t *)_careerGameTopTeamName,    /* teamA (a1) */
                (uint8_t *)_careerGameBottomTeamName,  /* teamB (a2) */
                resultFlags);
            if (resultStr[0] != 0) {
                a2 = resultStr;
                goto pastTeamLoad;
            }
        }
        a2 = 0;
        goto pastTeamLoad;
    }
    /* a6[5] == 0: no teams loaded */

pastTeamLoad:
    ;
    /* Determine slot type and competition name.
     *
     * The Amiga code did `((uint8_t **)&currentCompetitionPtr)[slotType]`
     * — relying on the four globals being adjacent 4-byte pointers in
     * BSS / careerFileBufferBuf overlay. On 64-bit the array-index
     * stride is 8 and reads land in the wrong slot (and the overlay
     * offsets are still 4 B apart pending a wider careerFileBuffer
     * format). Switch on slotType to fetch the right named pointer
     * directly — pointer-width-correct on both targets. */
    uint16_t slotType = a6[0] & 0xFF;
    uint8_t *compInfo;
    /* slot 4 = the playoffs/diy competition (diyCompetitionPtr). The Amiga
     * `(&currentCompetitionPtr)[slotType]` indexed all FIVE adjacent pointers;
     * this switch was missing case 4, so a playoff entry (slotType 4, handled by
     * the second switch below as aPlayoffs) fell through to default=NULL and
     * crashed on compInfo[5] when a playoff slot is shown in the game list. */
    extern uint8_t *_diyCompetitionPtr;
    switch (slotType) {
        case 0: compInfo = _currentCompetitionPtr;     break;
        case 1: compInfo = _secondaryCompetitionPtr;   break;
        case 2: compInfo = _tertiaryCompetitionPtr;    break;
        case 3: compInfo = _quaternaryCompetitionPtr;  break;
        case 4: compInfo = _diyCompetitionPtr;         break;
        default: compInfo = (uint8_t *)0;             break;
    }
    uint16_t d5; /* color/stringFlags value */
    uint8_t *namePtr; /* competition name pointer */

    switch (slotType) {
    case 0: /* league */
        namePtr = (uint8_t *)_divStringTable;
        d5 = 2;
        break;
    case 1: /* cup type 1 */
        namePtr = _aCup_1;
        d5 = 6;
        break;
    case 2: /* cup type 2 */
        namePtr = _aCup_2;
        d5 = 0x0F;
        break;
    case 3:
        namePtr = _aError;
        d5 = 0x0D;
        break;
    case 4:
        namePtr = _aPlayoffs;
        d5 = 0x0E;
        break;
    default:
        /* Unknown slot type — show dashes */
        a5->fg = (uintptr_t)_aMinus;
        a5->isInvisible = 0;
        a5++; /* next entry */
        a5->fg = (uintptr_t)_aMinus;
        a5->isInvisible = 0;
        a5++; /* next entry */
        d5 = 1;
        goto afterHeader;
    }

    {
        /* Get competition slot ptr table entry
         * careerCompetitionSlotPtrTable is really an array of 5 pointers (20 bytes) */
        uint8_t *a3slot = _careerCompetitionSlotPtrTable[slotType];
        uint16_t slotVal = *(uint16_t *)a3slot;

        if (slotVal == 0) {
            /* --- League-style header --- */
            /* Height check */
            uint16_t checkY = d2 + 8 - _gameListY;
            if (checkY >= _contestEntryHeight)
                goto hideAll;

            a5->y = d2;
            d3 += 8;
            d4 += 8;

            /* Get division/competition name string */
            uint8_t strTableOff = compInfo[5];
            if (strTableOff == 0) {
                namePtr = (uint8_t *)_divStringTable[_currentDivisionIndex];
            } else {
                /* Embedded char *[] name table — 2 entries per
                 * division (full name + abbreviation). Amiga stride
                 * was divIdx * 8 (= 2 × 4-byte slots), reading the
                 * second slot via base+4. On 64-bit each entry is 8
                 * bytes, so use typed `char **` indexing with
                 * `divIdx * 2 + 1` to get the abbreviation. Same fix
                 * as in SelectTeamsFinalMenu (teams.c). */
                int16_t off = (int8_t)strTableOff;
                char **nameTbl = (char **)(compInfo + 5 + off);
                namePtr = (uint8_t *)nameTbl[_currentDivisionIndex * 2 + 1];
            }

            a5->fg = (uintptr_t)namePtr;
            a5->isInvisible = 0;
            a5++; /* next entry */

            /* Second header line — invisible */
            a5->y = d2 + 8;
            a5->isInvisible = 1;
            a5++; /* next entry */
        } else if (slotVal == 1) {
            /* --- Cup-style header (single line comp name + round info) --- */
            uint8_t strTableOff = compInfo[5];
            if (strTableOff != 0) {
                /* Typed `char **` access — slot 1 is the abbreviation
                 * (full name + abbreviation pair per div). */
                int16_t off = (int8_t)strTableOff;
                char **nameTbl = (char **)(compInfo + 5 + off);
                namePtr = (uint8_t *)nameTbl[1];
            }

            a5->fg = (uintptr_t)namePtr;

            /* Height check depends on whether a4 == a6 */
            if (a4 == a6) {
                /* Need 16 pixels of space */
                uint16_t checkY = d2 + 16 - _gameListY;
                if (checkY >= _contestEntryHeight)
                    goto hideAll;
            } else {
                /* Need 8 pixels */
                uint16_t checkY = d2 + 8 - _gameListY;
                if (checkY >= _contestEntryHeight)
                    goto hideAll;
            }

            a5->y = d2;
            d3 += 16;
            d4 += 8;
            a5->isInvisible = 0;
            a5++; /* next entry */

            /* Round info line */
            a5->y = d2 + 8;

            /* Get round string */
            int16_t roundIdx = (int8_t)a6[9];
            uint8_t *roundStr = (uint8_t *)_roundStringTable[roundIdx];
            if (a6[8] == 3) {
                /* Replay — use rndStringTable instead */
                roundStr = (uint8_t *)_rndStringTable[roundIdx];
            }

            /* Copy round string into entry's string buffer */
            uint8_t *dst = (uint8_t *)a5->fg;
            dst = StringCopy((const uint8_t *)roundStr, dst);

            /* Append leg/replay suffix */
            uint8_t *suffixStr;
            uint8_t legType = a6[8];
            if (legType == 1) {
                suffixStr = _aLeg1;
                goto appendSuffix;
            } else if (legType == 2) {
                suffixStr = _aLeg2;
                goto appendSuffix;
            } else if (legType == 3) {
                suffixStr = _aReplay;
                goto appendSuffix;
            }
            goto noSuffix;

        appendSuffix:
            *dst++ = ' ';
            dst = StringCopy((const uint8_t *)suffixStr, dst);

        noSuffix:
            *dst = 0; /* null terminate */
            a5->isInvisible = 0;
            a5++; /* next entry */

            /* Check if should add extra spacing */
            if (a6[5] == 0) {
                d4 += 8;
            } else if (a6[5] == 1 && a2 == 0) {
                d2 += 4;
                d4 += 8;  /* fall-through to loc_13C77E in asm */
            }
        } else {
            /* --- Default (slotVal >= 2) header: same as cup but different path --- */
            uint8_t strTableOff = compInfo[5];
            if (strTableOff != 0) {
                int16_t off = (int8_t)strTableOff;
                char **nameTbl = (char **)(compInfo + 5 + off);
                namePtr = (uint8_t *)nameTbl[1];
            }

            a5->fg = (uintptr_t)namePtr;

            /* Height check */
            if (a4 == a6) {
                uint16_t checkY = d2 + 16 - _gameListY;
                if (checkY >= _contestEntryHeight)
                    goto hideAll;
            } else {
                uint16_t checkY = d2 + 8 - _gameListY;
                if (checkY >= _contestEntryHeight)
                    goto hideAll;
            }

            a5->y = d2;
            d3 += 16;
            d4 += 8;
            a5->isInvisible = 0;
            a5++; /* next entry */

            /* Round info line */
            a5->y = d2 + 8;

            int16_t roundIdx = (int8_t)a6[9];
            uint8_t *roundStr = (uint8_t *)_roundStringTable[roundIdx];
            if (a6[8] == 3) {
                roundStr = (uint8_t *)_rndStringTable[roundIdx];
            }

            uint8_t *dst = (uint8_t *)a5->fg;
            dst = StringCopy((const uint8_t *)roundStr, dst);

            uint8_t legType = a6[8];
            if (legType == 1) {
                *dst++ = ' ';
                dst = StringCopy((const uint8_t *)_aLeg1, dst);
            } else if (legType == 2) {
                *dst++ = ' ';
                dst = StringCopy((const uint8_t *)_aLeg2, dst);
            } else if (legType == 3) {
                *dst++ = ' ';
                dst = StringCopy((const uint8_t *)_aReplay, dst);
            }

            *dst = 0;
            a5->isInvisible = 0;
            a5++; /* next entry */

            /* Check spacing */
            if (a6[5] == 0) {
                d4 += 8;
            } else if (a6[5] == 1 && a2 == 0) {
                d2 += 4;
                d4 += 8;  /* fall-through to loc_13C77E in asm */
            }
        }
    }

afterHeader:
    ;
    /* Apply stringFlags color to entries 33-42 (10 entries) */
    {
        MenuEntry *colorEntry = &_menuEntries[33];
        for (int16_t i = 9; i >= 0; i--) {
            uint16_t sf = colorEntry->stringFlags;
            sf = (sf & 0xFFF0) | d5;
            colorEntry->stringFlags = sf;
            colorEntry++;
        }
    }

    /* If a4 == a6 (no game data, just header), hide last two filled entries */
    if (a4 != a6) {
        /* Hide the two entries before current a5 */
        MenuEntry *prev1 = a5 - 1;
        prev1->isInvisible = 1;
        MenuEntry *prev2 = a5 - 2;
        prev2->isInvisible = 1;
    }

    /* If a6[5] == 0, no teams — hide remaining 8 entries and skip to end */
    if (a6[5] == 0) {
        for (int16_t i = 7; i >= 0; i--) {
            a5->y = d2;
            a5->isInvisible = 1;
            a5++;
        }
        goto calcColumnMarker;
    }

    /* --- Fill team/score entries --- */
    /* Entry: home/away indicator (or invisible for gameType==3) */
    a5->y = d2;
    a5->isInvisible = 1;

    if (_gameType != 3) {
        uint8_t homeAway = a4[0x0A];
        if (homeAway != 0) {
            if (homeAway == 1) {
                a5->fg = (uintptr_t)_aH; /* "(H)" */
            } else if (homeAway == 2) {
                a5->fg = (uintptr_t)_aA_2; /* "(A)" */
            }
            /* asm: clr.w 4(a5) regardless of homeAway value (1, 2, or other) */
            a5->isInvisible = 0;
        }
        /* if homeAway == 0 or gameType == 3: entry stays hidden (isInvisible=1) */
    }

    a5++; /* next entry */

    /* Highlight marker entry — set y and height */
    a5->y = d2 - 1;
    a5->dwHeight = 7;

    /* Check if this is the saved/active contest entry */
    if ((uintptr_t)a6 == _savedContestEntryPtr &&
        (uintptr_t)a4 == _savedContestRoundPtr) {
        a5->isInvisible = 0;
    } else {
        a5->isInvisible = 1;
    }

    a5++; /* next entry */

    /* Top team name (player-controlled team) */
    a5->y = d2;
    if (_careerGameListTeamControl == 0)
        a5->fg = (uintptr_t)_careerGameTopTeamName;
    else
        a5->fg = (uintptr_t)_careerGameBottomTeamName;
    a5->isInvisible = 0;
    a5++; /* next entry */

    /* Score entries — depends on whether match was played */
    TeamSelectionKeyCacheMatchSlot *matchSlot = (TeamSelectionKeyCacheMatchSlot *)(a4 + 0x0A);
    if (matchSlot->matchFlags != 0) {
        /* Match played — show scores.
         *
         * packedGoals at a4[0x10] is stored host-native (writer at
         * career_status.c:3741). (packed>>8) is team1Score (fixture top
         * team at +0x0C), (packed&0xFF) is team2Score (bottom at +0x0E).
         * The previous code did byte access — a4[0x10] / a4[0x11] —
         * which assumed BE byte order; on LE x86 that swapped the
         * scores so the career fixture row showed the user team's
         * score in the opponent column ("AC MILAN 4-1 LAZIO" when
         * reality was LAZIO 4-1 AC MILAN). */
        uint16_t packedScore = matchSlot->packedGoals;
        int8_t team1Score = (int8_t)(packedScore >> 8);
        int8_t team2Score = (int8_t)(packedScore & 0xFF);

        /* Top score — write as uint16_t to match asm move.w to offset $26 */
        a5->y = d2;
        {
            /* careerGameListTeamControl==0 means top fixture team is user
             * (player-controlled). The display puts the user team first, so
             * topScore should be the user's score. */
            int8_t topScore = (_careerGameListTeamControl == 0) ? team1Score : team2Score;
            a5->fgWord = (uint16_t)(int16_t)topScore;
        }
        a5->isInvisible = 0;
        a5++; /* next entry */

        /* Dash separator */
        a5->y = d2;
        a5->fg = (uintptr_t)_aMinus;
        a5->isInvisible = 0;
        a5++; /* next entry */

        /* Bottom score — write as uint16_t to match asm move.w to offset $26 */
        a5->y = d2;
        {
            int8_t botScore = (_careerGameListTeamControl == 0) ? team2Score : team1Score;
            a5->fgWord = (uint16_t)(int16_t)botScore;
        }
        a5->isInvisible = 0;
        a5++; /* next entry */
    } else {
        /* Match not played — show "V" */
        a5->y = d2;
        a5->isInvisible = 1;
        a5++; /* top score hidden */

        a5->y = d2;
        a5->fg = (uintptr_t)_aV;
        a5->isInvisible = 0;
        a5++; /* "V" separator */

        a5->y = d2;
        a5->isInvisible = 1;
        a5++; /* bottom score hidden */
    }

    /* Bottom team name (opponent) */
    a5->y = d2;
    if (_careerGameListTeamControl == 0)
        a5->fg = (uintptr_t)_careerGameBottomTeamName;
    else
        a5->fg = (uintptr_t)_careerGameTopTeamName;
    a5->isInvisible = 0;
    a5++; /* next entry */

    /* Result string entry */
    a5->y = d2 + 8;
    a5->isInvisible = 1;
    if (a2 != 0) {
        d4 += 8;
        uint8_t *dst = (uint8_t *)a5->fg;
        StringCopy((const uint8_t *)a2, dst);
        a5->isInvisible = 0;
    }
    a5++; /* next entry */

calcColumnMarker:
    ;
    /* Calculate column marker update
     * asm: move.l a4,d0; sub.l a6,d0; ext.l d0; divu.w #$C,d0; addq.w #1,d0; cmp.b 5(a6),d0
     * ext.l sign-extends low word to long before unsigned divide */
    {
        uint8_t numRounds = a6[5];
        if (numRounds != 0) {
            int16_t diff16 = (int16_t)((uintptr_t)a4 - (uintptr_t)a6);
            uint32_t dividend = (uint32_t)(int32_t)diff16;
            uint16_t gameIndex = (uint16_t)(dividend / 12) + 1;
            if ((uint8_t)gameIndex != numRounds)
                goto skipExtraPad;
        }
        d4 += 4;
    }
skipExtraPad:
    _columnMarker += d4;

    /* Draw the entries */
    if (a4 == a6) {
        /* No game — draw all 11 entries at once */
        MenuEntry *drawStart = &_menuEntries[33];
        DrawMultipleItems(11, drawStart);
        return;
    }

    /* Has game — draw entry 33 separately, then entries 36-43 (8 entries) */
    {
        MenuEntry *first = &_menuEntries[33];
        DrawMenuItem(first);
        MenuEntry *rest = &_menuEntries[36]; /* entry 36 = menuEntries + 0x7E0/0x38 */
        DrawMultipleItems(8, rest);
        return;
    }

hideAll:
    _cglmVar3 = 1;
    {
        MenuEntry *hideEntry = &_menuEntries[33];
        for (int16_t i = 10; i >= 0; i--) {
            hideEntry->y = d2;
            hideEntry->isInvisible = 1;
            hideEntry++;
        }
    }
}
