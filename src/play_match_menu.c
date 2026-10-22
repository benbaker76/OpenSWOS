/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* play_match_menu.c — Play Match Menu UI functions
 *
 * Chunk 98: PlayMatchMenuInit, PlayMatchMenuReinit, SetCurrentSelectedPlayerIndexPointer,
 * OnSwapPlayer, PlayMatchMenuSwapPlayers, HandleSquadSwapSelectionToggle,
 * PlayMatchChangeTactics, ExitPlayMatch, PlayMatchSelected, EditTacticsInPlayMatchMenu,
 * ViewOpponentsSelect, ReturnFromViewOpponent, PlayMatchOnSelect30, PlayMatch30AfterDraw,
 * UpdateTeamCheckmark, SetGameStartButtonText, ShowOrHidePlayMatchButton,
 * SetUpTacticsButtons, SetPlayerSurnamesOnSidePitch, ShowUseMatchTeamIfTraining,
 * SetVariableButtons, DrawPlayerEntries, RedrawPlayerNames, UpdateLittleCheckmark,
 * SavePositionsAndUpdateLittleCheckmark, UpdateCheckMarkSprite, UseMatchTeamSelect,
 * TogglePlayerCoach, GetPlayerPosition.
 */

#include "play_match_menu.h"
#include "endian_helpers.h"
#include "menu_system.h"
#include "game_types.h"
#ifdef SWOS_SDL_BUILD
#include "uniform.h"            /* the UNIFORM button (entry 68) */
#include "custom_team.h"        /* (CUSTOM) where the country would be named */
#endif
#include "shooting_practice.h"  /* the SHOOTING PRACTICE button (entry 69) */

/* Prevent GCC from converting byte loops to memcpy/memset (no libc) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

/* ---- Assembly externs (globals in SWOS2.s) ---- */
extern MenuEntry _menuEntries[];
extern uint16_t _fireResetFlag;
extern uint32_t _irqCookieObfuscated;
extern uint16_t _player1ClearFlag;
extern uint16_t _player2ClearFlag;
extern uint16_t _exitGameFlag;
extern uint16_t _isCareer;
extern uint16_t _longFireFlag;
extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;
extern uint16_t _numPlayers;
extern uint16_t _careerNumPlayers;
extern uint16_t _gameType;
extern uint16_t _trainingGame;
extern uint16_t _coachOrPlayer;
extern uint16_t _currentTeamNumber;
extern uint16_t _selTeamNumber;
extern uint16_t _panicReasonCode;
extern uint16_t _teamSkillCurrent;
extern uint16_t _teamSkillSnapshot;

extern TeamFileEx _currentTeam;
extern TeamFileEx _careerTeam;
extern uint8_t _playerPositions[];
extern uint8_t _currentMatchPlayers[];
extern uint8_t _playerPositionsArray[];
extern uint8_t _oldPlayerPositionsArray[];
extern void *_teamTacticsTable[];
extern void *_positionsTable[];
extern void *_countriesTable[];

/* Globals defined in play match menu data section of SWOS2.s */
extern uint16_t _showingCpuTeams;
extern uint16_t _squadChangesAllowed;
extern TeamFileEx *_playMatchTeam1Ptr;
extern TeamFileEx *_playMatchTeam2Ptr;
extern uint16_t _maxSubstitutesEntries;
extern uint16_t _pmMenuNumPlayers;
extern uint16_t _numPlayersToShowInMenu;
extern uint16_t _viewingOpponentTeam;
extern uint16_t _is_A_team;
extern int16_t  _selectedPlayerPosition;
extern int16_t  _selectedPlayerTacticsPosition;
extern uintptr_t _pmSelectedPlayerEntry;
extern uint16_t _playMatchControlFlags;
extern uint16_t _playerNumThatStarted;
extern int16_t  _playMatchExcludeFlags;
extern uint16_t _playMatchButtonVisibilityState;
extern uint16_t _careerTeamCoachOnly;
extern uint16_t _isFirstPlayerChoosingTeam;
extern uintptr_t _currentSelectedPlayerIndexPtr;
extern uint16_t _currentTeamSelectedPlayerPosition;
extern uint16_t _selTeamSelectedPlayerPosition;
extern uint16_t _team1SelectedPlayerIndex;
extern uint16_t _team2SelectedPlayerIndex;
extern uint32_t _allowShorterMenuItemsWithFrames;

/* String constants from SWOS2.s */
extern uint8_t _aViewResult[];
extern uint8_t _aPlayCoachMatch[];
extern uint8_t _aCoachMatch[];
extern uint8_t _aViewMatch[];
extern uint8_t _aViewOppo[];
extern uint8_t _aViewTeam[];
extern uint8_t _aViewOtherTeam[];
extern uint8_t _aViewATeam[];
extern uint8_t _aViewBTeam[];
extern uint8_t _aCoach_1[];
extern uint8_t _aPlayer_1[];
extern uint8_t _fundsBuffer[];
extern void *_playerPositionsStringTable[];
extern uint8_t _teamAName[];
extern uint8_t _teamBName[];
extern void *_sidePitchSurnamePositionsTable[];
extern int16_t _playMatchRedrawList[];
extern int16_t _togglePlayerCoachRedrawItemList[];

/* ---- C-ported function externs ---- */
extern void CheckIsTeamNational(void);
extern void SetBenchPlayersNumbers(TeamFileEx *team);
extern void DrawMenu(void);
extern void CallDrawMenuItem(MenuEntry *entry);
extern void SetCurrentEntry(uint16_t index);
extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
extern void CallDrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
extern void DrawMenuItemsWaitForVSync(const int16_t *list);
extern void SetExitMenuFlag(void);
extern MenuEntry *CalcMenuEntryAddress(uint16_t ordinal);
extern uint8_t *ExtractSurname(uint8_t *name, uint16_t maxLenAndFlags);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern uint16_t IsTeamNational(TeamFileEx *team);
extern void InitPlayerValueDisplayStruct(uint8_t *playerEntry, uint8_t *dst);
extern uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);
/* Honest injury/suspension status for DISPLAY decisions — no beta override. */
extern uint16_t IsPlayerInjuredForDisplay(PlayerFile *playerFile, int16_t mode);
extern uint16_t GetNumberOfSubstitutes(uint8_t *team, uint16_t numPl);
extern uint16_t GetNumberOfSubstitutesInSelectionList(uint8_t *team, uint16_t extraCount);
extern uint32_t GetNumberOfAvailablePlayers(uint8_t *team, int16_t excludeCardsFlag, uint16_t numPl);
extern uint32_t GetNumberOfAvailableSubstitutes(uint8_t *team, int16_t flag);
extern void RotatePlayersLeft(TeamFileEx *team, uint16_t maxSubs);
extern int16_t CheckIfTeamIsChanged(void);
extern uint8_t *GetPlayerInfoBreakPosition(TeamFileEx *teamPtr, uint16_t requestedSlot,
                                             uint16_t *outSlot, uint16_t *outPlayerIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint16_t _cGetPlayerAtIndexFileIdx;
extern uint16_t ComputeOldPlayerSkillForViewResult(TeamFileEx *team, uint16_t goalsCount);
extern void ShowStandardChooseTacticsMenu(TeamFileEx *team);
extern void TrainingMatch_RestoreLastMatchState(void);
extern void TrainingMatch_RestoreSavedTeamSelectionState(void);

extern void FormTopSkillsString(uint8_t *dst, PlayerFile *playerFile);
extern void FormatPlayerValueOrStatusText(uint8_t *base, uint8_t *dst);
extern void SetSquadMenuEntryStatusSprite(MenuEntry *entry, PlayerFile *playerFile, uint16_t flag);
extern void TrainingMatch_BuildCurrentTeamFromCareerTeam(void);
extern void TrainingMatch_ApplyUseMatchTeamAndSwapSubs(void);

/* ---- Helper: menu entry from byte offset ---- */

/* Helper: access bg as the high 16 bits (big-endian BACKANDFRAMECOLOR) */

/* ================================================================
 * GetPlayerPosition  (SWOS2.s line 79648)
 *
 * Reverse lookup: given team + player file index, return position slot.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t GetPlayerPosition(TeamFileEx *team, int16_t playerIndex) {
    int16_t total;
    if (team == &_careerTeam)
        total = (int16_t)_careerNumPlayers;
    else if (team == &_currentTeam)
        total = (int16_t)_numPlayers;
    else
        total = 16;

    int16_t i;
    for (i = total - 1; i >= 0; i--) {
        uint16_t slot, fIdx;
        GetPlayerInfoBreakPosition(team, (uint16_t)i, &slot, &fIdx);
        if ((int16_t)fIdx == playerIndex)
            return i;
    }
    _panicReasonCode = 0x5996;
    return -1;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void SetCurrentSelectedPlayerIndexPointer(void) {
    uint16_t *a0 = &_team1SelectedPlayerIndex;
    uint16_t *a1 = &_team2SelectedPlayerIndex;
    if (!_isFirstPlayerChoosingTeam) { uint16_t *t = a0; a0 = a1; a1 = t; }
    if (_viewingOpponentTeam)        { uint16_t *t = a0; a0 = a1; a1 = t; }
    _currentSelectedPlayerIndexPtr = (uintptr_t)a0;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void UpdateCheckMarkSprite(void) {
    MenuEntry *e = &_menuEntries[0];
    e->isInvisible = 1;
    if (_viewingOpponentTeam) return;
    e->stringFlags &= ~(1u << 13);
    if (_teamSkillCurrent == _teamSkillSnapshot) return;
    e->fgWord = (_teamSkillCurrent > _teamSkillSnapshot) ? SPR_CHECK_BIG : SPR_CROSS_BIG;
    e->stringFlags |= (1u << 13);
    e->isInvisible = 0;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void UpdateLittleCheckmark(void) {
    _teamSkillSnapshot = _teamSkillCurrent;
    int16_t i; for (i = 0; i < 32; i++) _oldPlayerPositionsArray[i] = _playerPositionsArray[i];
    UpdateCheckMarkSprite();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void SavePositionsAndUpdateLittleCheckmark(void) {
    _teamSkillSnapshot = _teamSkillCurrent;
    int16_t i; for (i = 0; i < 32; i++) _oldPlayerPositionsArray[i] = _playerPositionsArray[i];
    TeamFileEx *team = _playMatchTeam1Ptr;
    team->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    ComputeOldPlayerSkillForViewResult((TeamFileEx *)_playMatchTeam1Ptr, _maxSubstitutesEntries);
    UpdateCheckMarkSprite();
}

/* ================================================================ */
__attribute__((used, externally_visible))
#ifdef SWOS_SDL_BUILD
/* Set while the online lobby shows this screen (online_menus.c): the PLAY button's
 * label -- "CONFIRM", since there it confirms the side rather than starting a match. */
const char *g_playMatchStartLabel;
#endif

void SetGameStartButtonText(void) {
    MenuEntry *e = &_menuEntries[9];
    e->bgColor = (e->bgColor & 0xFFF0) | YELLOW_GREEN_BLACK_14;
    e->fg = (uintptr_t)_aViewResult;
    if (!_squadChangesAllowed) return;

    TeamFileEx *t1 = _playMatchTeam1Ptr;
    TeamFileEx *t2 = _playMatchTeam2Ptr;
    if (t1->team.teamControls == TEAM_COMPUTER &&
        t2->team.teamControls == TEAM_COMPUTER) {
        e->fg = (uintptr_t)_aViewMatch;
    } else {
        e->fg = (uintptr_t)_aPlayCoachMatch;
        if (t1->team.teamControls != TEAM_PLAYER_COACH)
            e->fg = (uintptr_t)_aCoachMatch;
    }
#ifdef SWOS_SDL_BUILD
    if (g_playMatchStartLabel)
        e->fg = (uintptr_t)g_playMatchStartLabel;
#endif
    e->bgColor = (e->bgColor & 0xFFF0) | RED_10;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void ShowUseMatchTeamIfTraining(void) {
    _menuEntries[5].isInvisible = 1;
    if (!_trainingGame) return;
    if (_is_A_team) return;
    if (!CheckIfTeamIsChanged()) return;
    _menuEntries[5].isInvisible = 0;
}

/* ================================================================
 * ShowOrHidePlayMatchButton  (line 81484)
 *
 * Very complex control flow. Translated label-by-label.
 * Labels mapped:
 *   loc_14855C  → notViewingOpponent
 *   loc_148590  → afterTrainingTextSet
 *   loc_1485D8  → careerPath
 *   loc_1485FE  → checkAvailSubs
 *   loc_148606  → checkMaxDisabled
 *   loc_14860E  → checkSubstitutes
 *   loc_148654  → checkAvailPlayersLimit
 *   loc_1486D0  → canPlay
 *   loc_1486D4  → nonCareerSubCheck
 *   loc_14872A  → nonCareerAvailPlayersLimit
 *   loc_14874A  → canPlay (shared)
 *   loc_148748  → cannotPlay
 *   loc_148760  → setVisState1
 *   loc_148768  → showPlayBtn (clr entry $1F8 invisible)
 *   loc_14876E  → hidePlayBtnPath
 *   loc_148780  → hidePlayBtn (set entry $1F8 invisible=1)
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowOrHidePlayMatchButton(void) {
    _menuEntries[8].isInvisible = 1;

    if (_viewingOpponentTeam) {
        TeamFileEx *t2 = _playMatchTeam2Ptr;
        if (t2->team.teamControls != 1) goto hidePlayBtnPath;
        if (!_squadChangesAllowed) goto hidePlayBtnPath;
        _menuEntries[8].isInvisible = 0;
        if (!_playMatchButtonVisibilityState) goto hidePlayBtnPath;
        goto canPlay;
    }

    /* notViewingOpponent */
    _menuEntries[8].isInvisible = 0;
    {
        MenuEntry *vb = &_menuEntries[7];
        vb->stringFlags &= ~(1u << 13);
        if (_trainingGame) {
            vb->fg = (uintptr_t)_aViewATeam;
            if (!_is_A_team)
                vb->fg = (uintptr_t)_aViewBTeam;
        }
    }

    {
        TeamFileEx *team = _playMatchTeam1Ptr;
        uint32_t subsR = GetNumberOfAvailableSubstitutes((uint8_t *)team, _playMatchExcludeFlags);
        uint16_t d2 = (uint16_t)(subsR & 0xFFFF); /* d7→d2: available subs (goalkeepers) */

        uint32_t plR = GetNumberOfAvailablePlayers((uint8_t *)team, _playMatchExcludeFlags, 0);
        /* d7 = available, d6 = disabled (after bridge unpack) */
        /* But C returns packed: low=available, high=disabled */
        uint16_t d7_avail = (uint16_t)(plR & 0xFFFF);
        uint16_t d6_disabled = (uint16_t)(plR >> 16);

        if (_gameType == GAME_TYPE_CAREER) {
            /* careerPath */
            if (team == &_currentTeam) {
                uint32_t csR = GetNumberOfAvailableSubstitutes((uint8_t *)&_careerTeam, _playMatchExcludeFlags);
                uint16_t careerSubs = (uint16_t)(csR & 0xFFFF);
                if (careerSubs >= 2) goto checkMaxDisabled;
            }
            /* checkAvailSubs */
            if (d2 < 1) goto cannotPlay;
        checkMaxDisabled:
            if (d6_disabled > 4) goto cannotPlay;
        } else {
            if (d2 < 1) goto cannotPlay;
            if (d6_disabled > 2) goto cannotPlay;
        }

        /* checkSubstitutes */
        if (_gameType == GAME_TYPE_CAREER) {
            uint16_t ns = GetNumberOfSubstitutes((uint8_t *)team, 1);
            if (ns != 1) goto showPlayBtnPath;

            if (_maxSubstitutesEntries > 5) {
                uint16_t nss = GetNumberOfSubstitutesInSelectionList((uint8_t *)team, 1);
                if (nss != 1) goto showPlayBtnPath;
            }

            /* checkAvailPlayersLimit */
            uint32_t apR = GetNumberOfAvailablePlayers((uint8_t *)team, _playMatchExcludeFlags,
                                                         11 + _maxSubstitutesEntries);
            uint16_t disabled2 = (uint16_t)(apR >> 16);
            if (disabled2 != 0) goto showPlayBtnPath;

            if (_trainingGame && team == &_careerTeam) {
                uint16_t ns2 = GetNumberOfSubstitutes((uint8_t *)&_currentTeam, 1);
                if (ns2 != 1) {
                    /* Set blink on view button */
                    MenuEntry *vb = &_menuEntries[7];
                    vb->stringFlags |= (1u << 13);
                    uint32_t sr2 = GetNumberOfAvailableSubstitutes((uint8_t *)team, _playMatchExcludeFlags);
                    if ((uint16_t)(sr2 & 0xFFFF) < 2) goto cannotPlay;
                    goto showPlayBtnPath;
                }
            }
            goto canPlay;
        } else {
            /* nonCareerSubCheck */
            uint16_t ns = GetNumberOfSubstitutes((uint8_t *)team, 1);
            if (ns != 1) goto showPlayBtnPath;

            if (_maxSubstitutesEntries > 5) {
                uint32_t sr3 = GetNumberOfAvailableSubstitutes((uint8_t *)team, _playMatchExcludeFlags);
                if ((uint16_t)(sr3 & 0xFFFF) < 2) goto nonCareerAvailPlayersLimit;
                uint16_t nss2 = GetNumberOfSubstitutesInSelectionList((uint8_t *)team, 1);
                if (nss2 != 1) goto showPlayBtnPath;
            }

        nonCareerAvailPlayersLimit:
            {
                uint32_t apR2 = GetNumberOfAvailablePlayers((uint8_t *)team, _playMatchExcludeFlags, 11);
                uint16_t disabled3 = (uint16_t)(apR2 >> 16);
                if (disabled3 != 0) goto showPlayBtnPath;
                goto canPlay;
            }
        }
    }

cannotPlay:
    /* loc_148748: bra.s loc_148768 — jump directly to showPlayBtn,
     * bypassing canPlay's visibility-state check. When the player roster
     * is invalid (not enough subs/players), the button is always shown
     * so the user can still VIEW RESULT. */
    goto showPlayBtn;

canPlay:
    /* loc_14874A */
    if (_playMatchTeam1Ptr == &_currentTeam) {
        if (!_playMatchButtonVisibilityState) goto hidePlayBtn;
        /* else fall through to showPlayBtn */
    } else {
        _playMatchButtonVisibilityState = 1;
    }
showPlayBtn:
    /* loc_148768 */
    _menuEntries[9].isInvisible = 0;
    return;

showPlayBtnPath:
    /* loc_14876E */
hidePlayBtnPath:
    if (_playMatchTeam1Ptr != &_currentTeam)
        _playMatchButtonVisibilityState = 0;
hidePlayBtn:
    /* loc_148780 */
    _menuEntries[9].isInvisible = 1;
    return;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void UpdateTeamCheckmark(void) {
    _menuEntries[37].isInvisible = 1;
    if (!_squadChangesAllowed) return;

    int16_t d1;
    TeamFileEx *team1 = _playMatchTeam1Ptr;

    if (_viewingOpponentTeam) {
        uint16_t *ip = (uint16_t *)_currentSelectedPlayerIndexPtr;
        d1 = (int16_t)*ip;
        if (d1 < 0) return;
        d1 = GetPlayerPosition(_playMatchTeam2Ptr, d1);
    } else {
        if (team1 == &_careerTeam)
            d1 = (int16_t)_selTeamSelectedPlayerPosition;
        else if (team1 == &_currentTeam)
            d1 = (int16_t)_currentTeamSelectedPlayerPosition;
        else {
            uint16_t *ip = (uint16_t *)_currentSelectedPlayerIndexPtr;
            d1 = (int16_t)*ip;
        }
        if (d1 < 0) return;
        d1 = GetPlayerPosition(team1, d1);
        if ((uint16_t)d1 >= _pmMenuNumPlayers) {
            if (team1 == &_currentTeam) _currentTeamSelectedPlayerPosition = 0xFFFF;
            else _selTeamSelectedPlayerPosition = 0xFFFF;
            return;
        }
    }

    {
        MenuEntry *ck = &_menuEntries[37];
        ck->stringFlags |= (1u << 13);
        MenuEntry *pe = CalcMenuEntryAddress(13 + d1);
        ck->y = pe->y;
        ck->isInvisible = 0;
    }
}

/* ================================================================ */
__attribute__((used, externally_visible))
void SetUpTacticsButtons(void) {
    TeamFileEx *team = _playMatchTeam1Ptr;
    if (_viewingOpponentTeam) team = _playMatchTeam2Ptr;
    uint8_t curTact = team->team.tactics;

    MenuEntry *e = &_menuEntries[38];
    int16_t i;
    for (i = 0; i < 18; i++, e++) {
        uint16_t c = e->bgColor & 0xFFF0;
        if (_viewingOpponentTeam || _showingCpuTeams) {
            c |= (i == curTact) ? PINK_TO_BROWN_1 : PINK_TO_BROWN_9;
        } else {
            c |= (i == curTact) ? ORANGE_TO_BROWN_5 : ORANGE_TO_BROWN_4;
        }
        e->bgColor = c;
        e->isDisabled = (_showingCpuTeams || _viewingOpponentTeam) ? 1 : 0;
    }
}

/* ================================================================ */
__attribute__((used, externally_visible))
void SetPlayerSurnamesOnSidePitch(void) {
    MenuEntry *entry = &_menuEntries[57];
    TeamFileEx *team = _playMatchTeam1Ptr;
    if (_viewingOpponentTeam) team = _playMatchTeam2Ptr;

    int8_t tIdx = (int8_t)team->team.tactics;
    TeamTactics *tac = (TeamTactics *)_teamTacticsTable[tIdx];
    int8_t fIdx = (int8_t)tac->ballOutOfPlayTactics;
    int16_t *pos = (int16_t *)_sidePitchSurnamePositionsTable[fIdx];

    uint16_t tNum = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
    int16_t i;
    for (i = 0; i <= 10; i++, entry++) {
        entry->x = *pos++ + 196;
        entry->y = *pos++ + 40;
        entry->stringFlags = *pos++;
        uint8_t *pl = GetPlayerAtIndex(tNum, (uint16_t)i);
        PlayerFile *plPf = (PlayerFile *)(pl + 76);
        uint8_t *surname;
        if (entry->x == 0x106)
            surname = ExtractSurname((uint8_t *)plPf->playerName, 0x400F);
        else
            surname = ExtractSurname((uint8_t *)plPf->playerName, 0x400A);
        StringCopy(surname, (uint8_t *)entry->fg);
    }
}

/* ================================================================
 * UNIFORM (entry 68) -- OpenSWOS, SDL only: the kit each side wears for this match
 * (src/uniform_menu.c, docs/UNIFORM.md). It is there whenever this screen is about to
 * start a match somebody can still arrange: not while the other team is being looked
 * at, not before a training match, not when the screen only shows a result.
 *
 * The bottom row has no room for a fifth button, so UNIFORM shares EDIT TACTICS' place:
 * the two are stacked, each 10 px high, in the 23 the PLAY button beside them takes.
 * Where EDIT TACTICS is not shown (computer v computer) UNIFORM has the place alone.
 * Positions are taken from EXIT's (entry 8), which never moves, because this runs
 * again on every change of the screen without the menu being unpacked afresh.
 * ================================================================ */
enum { kUniformEntry = 68 };

#ifdef SWOS_SDL_BUILD
/* Set by a screen that shows this one for the line-up alone (the custom team editor,
 * whose kits are edited beside it): no UNIFORM button. */
int32_t g_playMatchNoUniform;
#endif

static void UpdateUniformButton(void) {
    MenuEntry *exitBtn = &_menuEntries[8];
    MenuEntry *tactics = &_menuEntries[10];
    MenuEntry *viewOpp = &_menuEntries[7];
    MenuEntry *uniform = &_menuEntries[kUniformEntry];
#ifdef SWOS_SDL_BUILD
    int16_t shown = _squadChangesAllowed && !_viewingOpponentTeam && !_trainingGame &&
                    !g_playMatchNoUniform;
#else
    int16_t shown = 0;          /* the screen behind it is a JSON menu, which the Amiga does not run */
#endif

    uniform->isInvisible = !shown;
    tactics->y = exitBtn->y;
    tactics->dwHeight = exitBtn->dwHeight;
    tactics->downEntry = -1;
    viewOpp->rightEntry = 10;
    if (!shown)
        return;
    uniform->x = tactics->x;
    uniform->dwWidth = tactics->dwWidth;
    uniform->leftEntry = 7;
    if (tactics->isInvisible) {
        uniform->y = exitBtn->y;
        uniform->dwHeight = exitBtn->dwHeight;
        uniform->upEntry = 55;                  /* the formation above, as EDIT TACTICS' */
        viewOpp->rightEntry = kUniformEntry;
        /* Two computer teams: the button beside this one is VIEW OTHER TEAM, 93 wide,
         * which reaches under it (ReportBot 0031). It gets the width and the words it
         * has when a player's team is shown. */
        if (viewOpp->x + viewOpp->dwWidth > uniform->x - 1) {
            viewOpp->dwWidth = uniform->x - 1 - viewOpp->x;
            if (viewOpp->fg == (uintptr_t)_aViewOtherTeam)
                viewOpp->fg = (uintptr_t)_aViewOppo;
        }
    } else {
        tactics->y = exitBtn->y - 2;
        tactics->dwHeight = 10;
        tactics->downEntry = kUniformEntry;
        uniform->y = exitBtn->y + 9;
        uniform->dwHeight = 10;
        uniform->upEntry = 10;
    }
}

/* UniformSelected -- the UNIFORM button. This screen is shown to each player in turn
 * with the teams the other way round, so "first team" is the one it shows only for the
 * first of them. */
__attribute__((used, externally_visible))
void UniformSelected(void) {
#ifdef SWOS_SDL_BUILD
    UniformMenu_Open(_playMatchTeam1Ptr, _playMatchTeam2Ptr, _isFirstPlayerChoosingTeam != 0);
#endif
}

/* ================================================================
 * SHOOTING PRACTICE (entry 69) -- OpenSWOS, SDL only: strips the computer
 * side down to its goalkeeper for a friendly, so the human's 11 can
 * practise finishing into an open defence (src/shooting_practice.c).
 *
 * Shares VIEW OPPO's column the same way UNIFORM shares EDIT TACTICS' (see
 * UpdateUniformButton below): VIEW OPPO narrows to the top half, this takes
 * the bottom half, using exitBtn as the same "full height" reference
 * UpdateUniformButton restores EDIT TACTICS to when UNIFORM isn't shown.
 * The old spot (reusing USE MATCH TEAM's slot, entry 5) looked free on
 * paper but sits on top of the tactics/formation grid, which IS drawn
 * there on a plain friendly screen -- entry 5 is only text-empty during
 * one, not pixel-empty.
 * ================================================================ */
enum { kShootingPracticeEntry = 69 };

/* Single short word, like UNIFORM -- the column is ~68px, no room for an
 * ON/OFF suffix. State shows as background colour instead (bgColor below). */
char g_shootingPracticeLabel[16] = "PRACTICE";

/* GAME_TYPE_CAREER (=4) is the only named constant shared via
 * play_match_menu.h; 0 is "a friendly" everywhere else in the codebase
 * (e.g. arcade.c, online_menus.c, netplay_lobby.c) but has no shared name. */
#define GAME_TYPE_FRIENDLY 0

static void UpdateShootingPracticeButton(void) {
    MenuEntry *exitBtn = &_menuEntries[8];
    MenuEntry *viewOpp = &_menuEntries[7];
    MenuEntry *btn = &_menuEntries[kShootingPracticeEntry];
#ifdef SWOS_SDL_BUILD
    TeamFileEx *t1 = _playMatchTeam1Ptr;
    TeamFileEx *t2 = _playMatchTeam2Ptr;
    int16_t humanVsComputer =
        (t1->team.teamControls != TEAM_COMPUTER && t2->team.teamControls == TEAM_COMPUTER) ||
        (t1->team.teamControls == TEAM_COMPUTER && t2->team.teamControls != TEAM_COMPUTER);
    int16_t shown = _squadChangesAllowed && !_viewingOpponentTeam && !_trainingGame &&
                    _gameType == GAME_TYPE_FRIENDLY && humanVsComputer;
#else
    int16_t shown = 0;          /* the Amiga has no computer opponent to strip -- never shown */
#endif
    btn->isInvisible = !shown;

    /* Restore VIEW OPPO to its full-height default first -- same thing
     * UpdateUniformButton does to EDIT TACTICS before deciding whether to
     * share its column. */
    viewOpp->y = exitBtn->y;
    viewOpp->dwHeight = exitBtn->dwHeight;
    viewOpp->downEntry = -1;

    if (!shown) {
        g_playMatchShootingPractice = 0;   /* never leak a stale ON into a match it can't apply to */
        return;
    }

    btn->x = viewOpp->x;
    btn->dwWidth = viewOpp->dwWidth;
    btn->leftEntry = -1;
    btn->rightEntry = 8;
    btn->upEntry = 7;
    btn->downEntry = -1;

    viewOpp->y = exitBtn->y - 2;
    viewOpp->dwHeight = 10;
    viewOpp->downEntry = kShootingPracticeEntry;

    btn->y = exitBtn->y + 9;
    btn->dwHeight = 10;

    btn->fg = (uintptr_t)g_shootingPracticeLabel;
    btn->bgColor = (btn->bgColor & 0xFFF0) |
        (g_playMatchShootingPractice ? YELLOW_GREEN_BLACK_14 : BRIGHT_BLUE_TO_BLUE_13);
}

/* ShootingPracticeToggleSelected -- the SHOOTING PRACTICE button. */
__attribute__((used, externally_visible))
void ShootingPracticeToggleSelected(void) {
#ifdef SWOS_SDL_BUILD
    g_playMatchShootingPractice = !g_playMatchShootingPractice;
    UpdateShootingPracticeButton();
    DrawMenu();
#endif
}

/* ================================================================ */
__attribute__((used, externally_visible))
void SetVariableButtons(void) {
    SetGameStartButtonText();
    ShowUseMatchTeamIfTraining();

    MenuEntry *vb = &_menuEntries[7];
    vb->fg = (uintptr_t)_aViewOppo;
    if (_viewingOpponentTeam) vb->fg = (uintptr_t)_aViewTeam;
    if (_showingCpuTeams) { vb->fg = (uintptr_t)_aViewOtherTeam; vb->dwWidth = 93; }

    vb->stringFlags &= ~(1u << 13);

    if (_trainingGame) {
        vb->fg = (uintptr_t)_aViewATeam;
        if (!_is_A_team) {
            vb->fg = (uintptr_t)_aViewBTeam;
            uint16_t ns = GetNumberOfSubstitutes((uint8_t *)&_currentTeam, 1);
            if (ns != 1)
                vb->stringFlags |= (1u << 13);
        }
    }

    /* Team country name */
    {
        MenuEntry *top = &_menuEntries[2];
        uint8_t *nameOut = (uint8_t *)top->fg;
        if (!(_trainingGame && !_is_A_team)) {
            TeamFileEx *tm = _playMatchTeam1Ptr;
            if (_viewingOpponentTeam) tm = _playMatchTeam2Ptr;
            uint8_t *cname = (uint8_t *)_countriesTable[tm->team.countryNumber];
            cname++;
#ifdef SWOS_SDL_BUILD
            /* A custom team (src/custom_team.c) is a database team under another name,
             * so this would name the country of the team it was made from -- BORUSSIA
             * FULDA, made from BRAZIL, "(SOUTH AMERICA)". It says what the team is. */
            static uint8_t custom[] = "CUSTOM";
            if (CustomTeam_IsCustom(tm))
                cname = custom;
#endif
            *nameOut++ = '(';
            nameOut = StringCopy(cname, nameOut);
            *nameOut++ = ')';
        }
        *nameOut = 0;
    }

    UpdateCheckMarkSprite();

    TeamFileEx *curTeam = _playMatchTeam1Ptr;

    if (_viewingOpponentTeam) {
        /* Viewing opponent */
        _menuEntries[4].isInvisible = 0;
        _menuEntries[3].isInvisible = 0;
        _menuEntries[3].y = 204;
        _menuEntries[4].y = 204;
        _menuEntries[6].isInvisible = 1;
        _menuEntries[1].bgColor = (_menuEntries[1].bgColor & 0xFFF0) | ORANGE_TO_BROWN_12;

        curTeam = _playMatchTeam2Ptr;
        _menuEntries[10].isInvisible = 1;

        TeamFileEx *t2 = _playMatchTeam2Ptr;
        if (t2->team.teamControls != TEAM_COMPUTER || !_squadChangesAllowed)
            _menuEntries[7].isInvisible = 1;

        _menuEntries[11].isInvisible = 0;
        _menuEntries[29].isInvisible = 1;
        _menuEntries[30].isInvisible = 1;
    } else {
        /* Viewing own team */
        _menuEntries[4].isInvisible = 0;
        _menuEntries[3].isInvisible = 1;
        _menuEntries[3].y = 204;
        _menuEntries[4].y = 204;
        _menuEntries[6].isInvisible = 0;

        MenuEntry *cb = &_menuEntries[6];
        TeamFileEx *t1 = _playMatchTeam1Ptr;

        if (_showingCpuTeams) {
            _menuEntries[6].isInvisible = 1;
            _menuEntries[3].isInvisible = 0;
        }

        cb->fg = (uintptr_t)_aCoach_1;
        if (t1->team.teamControls == TEAM_PLAYER_COACH)
            cb->fg = (uintptr_t)_aPlayer_1;

        cb->bgColor = (cb->bgColor & 0xFFF0) | BRIGHT_BLUE_TO_BLUE_13;
        if (t1->team.teamControls == TEAM_PLAYER_COACH)
            cb->bgColor = (cb->bgColor & 0xFFF0) | BLUE_TO_PURPLE_11;

        if (!_squadChangesAllowed) {
            _menuEntries[3].isInvisible = 0;
            _menuEntries[6].isInvisible = 1;
        }

        cb->isDisabled = 0;
        if (_is_A_team) _menuEntries[3].isInvisible = 1;

        if (_trainingGame) {
            if (t1->team.teamControls != TEAM_PLAYER_COACH) {
                TeamFileEx *t2 = _playMatchTeam2Ptr;
                if (t2->team.teamControls == TEAM_PLAYER_COACH) {
                    if (t1->team.teamControls != TEAM_PLAYER_COACH)
                        cb->isDisabled = 1;
                }
            }
        }

        /* Edit tactics button */
        MenuEntry *eb = &_menuEntries[1];
        eb->bgColor = (eb->bgColor & 0xFFF0) | BLUE_TO_PURPLE_11;
        if (t1->team.teamControls != TEAM_PLAYER_COACH) {
            eb->bgColor = (eb->bgColor & 0xFFF0) | BRIGHT_BLUE_TO_BLUE_13;
            if (t1->team.teamControls != TEAM_COACH)
                eb->bgColor = (eb->bgColor & 0xFFF0) | ORANGE_TO_BROWN_12;
        }
        if (_trainingGame && t1->team.teamControls == TEAM_COMPUTER)
            eb->bgColor = (eb->bgColor & 0xFFF0) | BRIGHT_BLUE_TO_BLUE_13;

        _menuEntries[10].isInvisible = 0;
        if (_showingCpuTeams) _menuEntries[10].isInvisible = 1;
        if (_is_A_team) _menuEntries[10].isInvisible = 1;

        _menuEntries[7].isInvisible = 0;
        _menuEntries[11].isInvisible = 1;
        _menuEntries[29].isInvisible = 1;
        _menuEntries[30].isInvisible = 1;

        if (!_showingCpuTeams) {
            int16_t diff = (int16_t)_numPlayersToShowInMenu - (int16_t)_pmMenuNumPlayers;
            if (diff != 0) {
                _menuEntries[29].isInvisible = 0;
                if (diff != 1) _menuEntries[30].isInvisible = 0;
            }
        }
    }

    /* Set team name and coach name */
    {
        MenuEntry *nm = &_menuEntries[1];

        if (_trainingGame && !_is_A_team) {
            /* B team: show team B name */
            if (_playMatchTeam1Ptr == &_careerTeam)
                nm->fg = (uintptr_t)_teamBName;
            else
                nm->fg = (uintptr_t)_teamAName;
        } else {
            TeamFileEx *tm = _playMatchTeam1Ptr;
            if (_viewingOpponentTeam) tm = _playMatchTeam2Ptr;
            nm->fg = (uintptr_t)tm->team.teamName;
            if (_trainingGame) {
                nm->fg = (uintptr_t)_teamAName;
                if (_playMatchTeam1Ptr != &_careerTeam)
                    nm->fg = (uintptr_t)_teamBName;
            }
        }
    }

    {
        TeamFileEx *tm = _playMatchTeam1Ptr;
        if (_viewingOpponentTeam) tm = _playMatchTeam2Ptr;
        _menuEntries[4].fg = (uintptr_t)tm->team.coachName;
    }

    UpdateUniformButton();
    UpdateShootingPracticeButton();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchMenuReinit(void) {
    UpdateTeamCheckmark();
    _pmMenuNumPlayers = 16 - 5 + _maxSubstitutesEntries;

    uint16_t np = _numPlayers;
    if (_playMatchTeam1Ptr != &_currentTeam) {
        if (_selTeamNumber == 0xFFFF) {
            np = 16;
        } else {
            if (PACK_BE_U16(_playMatchTeam1Ptr->team.countryNumber, _playMatchTeam1Ptr->team.teamOrdinal) == _selTeamNumber)
                np = _careerNumPlayers;
            else
                np = 16;
        }
    }
    _numPlayersToShowInMenu = np;
    _pmSelectedPlayerEntry = 0;
    _selectedPlayerPosition = -1;

    SetVariableButtons();
    SetUpTacticsButtons();
    SetPlayerSurnamesOnSidePitch();
    ShowOrHidePlayMatchButton();

    int16_t extra = 4 - (int16_t)_maxSubstitutesEntries;
    if (extra >= 0) {
        MenuEntry *sub = &_menuEntries[28];
        int16_t j;
        for (j = extra; j >= 0; j--) {
            sub->isInvisible = 1;
            sub--;
            _menuEntries[12].y -= 9;
            _menuEntries[30].y -= 9;
            _menuEntries[29].y -= 9;
        }
    }

    SavePositionsAndUpdateLittleCheckmark();
    UpdateLittleCheckmark();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchMenuInit(void) {
    if (!_allowShorterMenuItemsWithFrames)
        _fireResetFlag = 1;

    uint32_t d0 = _irqCookieObfuscated ^ 0xCFD1F85F;
    if (d0 != 0x17B8) return;

    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    if (!(_playMatchControlFlags & 0x01)) _player1ClearFlag = 0xFF;
    if (!(_playMatchControlFlags & 0x02)) _player2ClearFlag = 0xFF;

    _showingCpuTeams = 0;
    {
        if (_playMatchTeam1Ptr->team.teamControls == TEAM_COMPUTER) {
            if (_playMatchTeam2Ptr->team.teamControls == TEAM_COMPUTER)
                _showingCpuTeams = 1;
        }
    }

    _viewingOpponentTeam = 0;
    SetCurrentSelectedPlayerIndexPointer();
    CheckIsTeamNational();
    SetBenchPlayersNumbers((TeamFileEx *)_playMatchTeam1Ptr);
    _is_A_team = 0;
    PlayMatchMenuReinit();

    /* If entry 9 (PLAY/VIEW RESULT button) is invisible, default to entry 7 (VIEW OPPO).
     * Original asm: lea ($2A26).w,a0; tst.w 4(a0) — $2A26 = menuEntries[9] in original binary,
     * offset 4 = isInvisible (coincidentally same as TEAMFILE_TEAMCONTROLS EQU). */
    if (_menuEntries[9].isInvisible != 0)
        SetCurrentEntry(7);
}

/* ================================================================ */
__attribute__((used, externally_visible))
void OnSwapPlayer(MenuEntry *a5) {
    TeamFileEx *t1 = _playMatchTeam1Ptr;

    if (_viewingOpponentTeam) {
        t1 = _playMatchTeam2Ptr;
        goto cpuPath;
    }
    if (_showingCpuTeams) goto cpuPath;

    /* Normal player path */
    {
        int16_t d6 = a5->ordinal - _menuEntries[13].ordinal;
        if (d6 == 16) d6 = d6 - 5 + _maxSubstitutesEntries;

        uint16_t slot, fIdx;
        GetPlayerInfoBreakPosition((TeamFileEx *)_playMatchTeam1Ptr, (uint16_t)d6, &slot, &fIdx);
        d6 = (int16_t)slot;
        int16_t d7 = (int16_t)fIdx;

        if (_selectedPlayerPosition >= 0) {
            if (d7 == _selectedPlayerPosition) {
                d7 = -1;
                if (!HandleSquadSwapSelectionToggle(d6)) return;
            } else {
                TeamFileEx *team = _playMatchTeam1Ptr;
                PlayMatchMenuSwapPlayers(team, _selectedPlayerTacticsPosition, d6);
                SetBenchPlayersNumbers((TeamFileEx *)team);
                team->team.playerValuesDirty = TEAM_VALUES_DIRTY;
                SavePositionsAndUpdateLittleCheckmark();
                _selectedPlayerPosition = -1;
                SetPlayerSurnamesOnSidePitch();
                ShowOrHidePlayMatchButton();
                UpdateTeamCheckmark();
                ShowUseMatchTeamIfTraining();
                DrawMenu();
                return;
            }
        } else {
            if (_longFireFlag) return;
        }

        _selectedPlayerPosition = d7;
        _selectedPlayerTacticsPosition = d6;
        {
            MenuEntry *old = (MenuEntry *)_pmSelectedPlayerEntry;
            _pmSelectedPlayerEntry = (uintptr_t)a5;
            if (old) { CallDrawMenuItem(old); }
        }
        CallDrawMenuItem(a5);
        UpdateLittleCheckmark();
        RedrawPlayerNames();
        return;
    }

cpuPath:
    if (_longFireFlag) return;
    if (!_squadChangesAllowed) return;
    {
        int16_t d1 = a5->ordinal - _menuEntries[13].ordinal;
        uint16_t slot, fIdx;
        uint8_t *pl = GetPlayerInfoBreakPosition((TeamFileEx *)t1, (uint16_t)d1, &slot, &fIdx);
        PlayerFile *plPf = (PlayerFile *)(pl + 76);
        if (((plPf->positionAndFace & PF_POSITION_MASK) >> 5) == 0) return;

        uint16_t *ip = (uint16_t *)_currentSelectedPlayerIndexPtr;
        if (*ip == fIdx)
            *ip = 0xFFFF;
        else
            *ip = fIdx;
        UpdateTeamCheckmark();
        DrawMenu();
    }
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchMenuSwapPlayers(TeamFileEx *team, int16_t d3, int16_t d4) {
    /* Original asm only takes D3 (selected slot) and D4 (dest slot).
     * Primary swap runs when the two slots are in different groups
     * (one starting, one bench) OR either slot >= 16 (extended squad). */
    int skipPrimary = 0;
    if ((uint16_t)d3 < 16 && (uint16_t)d4 < 16) {
        /* Both < 16: skip primary if same group */
        if ((uint16_t)d3 < 11 && (uint16_t)d4 < 11)
            skipPrimary = 1;  /* both starting 11 */
        else if ((uint16_t)d3 >= 11 && (uint16_t)d4 >= 11)
            skipPrimary = 1;  /* both bench */
    }

    if (!skipPrimary) {
        /* Primary swap: playerPositionsArray + shirt numbers.
         * Asm calls GetPlayerAtIndex(teamNum, d3/d4) which translates
         * through _playerPositions/currentMatchPlayers/playerNumbers,
         * then uses the translated file indices to index playerPositionsArray. */
        uint16_t tNum = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
        uint8_t *p1 = GetPlayerAtIndex(tNum, (uint16_t)d3);
        uint16_t fileIdx1 = _cGetPlayerAtIndexFileIdx;
        uint8_t *p2 = GetPlayerAtIndex(tNum, (uint16_t)d4);
        uint16_t fileIdx2 = _cGetPlayerAtIndexFileIdx;

        uint8_t tmp = _playerPositionsArray[fileIdx2];
        _playerPositionsArray[fileIdx2] = _playerPositionsArray[fileIdx1];
        _playerPositionsArray[fileIdx1] = tmp;

        PlayerFile *pf1 = (PlayerFile *)(p1 + 76);
        PlayerFile *pf2 = (PlayerFile *)(p2 + 76);
        int8_t ts = pf2->shirtNumber;
        pf2->shirtNumber = pf1->shirtNumber;
        pf1->shirtNumber = ts;
    }

    /* Secondary swap: swap in _playerPositions/currentMatchPlayers/playerNumbers
     * using d3/d4 (slot positions) */
    {
        uint8_t *pn;
        uint16_t tNum = PACK_BE_U16(team->team.countryNumber, team->team.teamOrdinal);
        if (team == &_currentTeam) {
            pn = (uint8_t *)_playerPositions;
        } else if (_gameType == GAME_TYPE_CAREER && (team == &_careerTeam || tNum == _selTeamNumber)) {
            /* The player's matchday lineup lives in currentMatchPlayers. In a CUP,
             * playMatchTeam1Ptr is a selTeams COPY (not &careerTeam) whose team
             * number still equals selTeamNumber. The asm's pointer-only test
             * (`==&careerTeam`) only works because the Amiga's playMatchTeam1Ptr IS
             * &careerTeam; on SDL the cup uses a copy, so that test fails and the
             * secondary swap edits team->playerNumbers instead — the lineup never
             * changes and a carded substitution silently fails. Key on
             * selTeamNumber to match the PRIMARY swap's GetPlayerAtIndex
             * (teams.c:924), so both halves of the swap agree. */
            pn = (uint8_t *)_currentMatchPlayers;
        } else {
            pn = team->team.playerNumbers;
        }
        uint8_t t = pn[(uint16_t)d4];
        pn[(uint16_t)d4] = pn[(uint16_t)d3];
        pn[(uint16_t)d3] = t;
    }

    if (_trainingGame && !_is_A_team) {
        uint16_t lim = 11 + _maxSubstitutesEntries;
        if ((d3 < (int16_t)lim && d4 >= (int16_t)lim) ||
            (d3 >= (int16_t)lim && d4 < (int16_t)lim))
            TrainingMatch_BuildCurrentTeamFromCareerTeam();
    }
}

/* ================================================================ */
__attribute__((used, externally_visible))
int16_t HandleSquadSwapSelectionToggle(int16_t d6) {
    uint16_t tNum = _currentTeamNumber;
    uint16_t *selPos = &_currentTeamSelectedPlayerPosition;

    if (_playMatchTeam1Ptr == &_currentTeam) {
        /* defaults already set */
    } else if (_playMatchTeam1Ptr == &_careerTeam) {
        tNum = _selTeamNumber;
        selPos = &_selTeamSelectedPlayerPosition;
    } else {
        tNum = PACK_BE_U16(_playMatchTeam1Ptr->team.countryNumber, _playMatchTeam1Ptr->team.teamOrdinal);
        selPos = (uint16_t *)_currentSelectedPlayerIndexPtr;
    }

    if (!_longFireFlag) return 1;
    if (_longFireFlag != 1) return 0;
    if (!_squadChangesAllowed) return 0;
    if ((uint16_t)d6 >= _pmMenuNumPlayers) return 0;

    int16_t curSel = (int16_t)_selectedPlayerPosition;
    if (curSel == (int16_t)*selPos) {
        curSel = -1;
    } else {
        /* Asm calls GetPlayer (not GetPlayerAtIndex) — selectedPlayerPosition
         * is already a file index, no translation needed. */
        uint8_t *pl = GetPlayer(tNum, (uint16_t)_selectedPlayerPosition);
        PlayerFile *plPf = (PlayerFile *)(pl + 76);
        if (((plPf->positionAndFace & PF_POSITION_MASK) >> 5) == 0) return 0;
    }

    *selPos = (uint16_t)curSel;
    _selectedPlayerPosition = -1;
    UpdateTeamCheckmark();
    ShowUseMatchTeamIfTraining();
    DrawMenu();
    return 0;
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchChangeTactics(MenuEntry *a5) {
    UpdateLittleCheckmark();
    RedrawPlayerNames();
    int16_t nt = a5->ordinal - _menuEntries[38].ordinal;
    TeamFileEx *t = _playMatchTeam1Ptr;
    if ((int8_t)t->team.tactics == (int8_t)nt) return;
    t->team.tactics = (uint8_t)nt;
    t->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    SavePositionsAndUpdateLittleCheckmark();
    SetUpTacticsButtons();
    SetPlayerSurnamesOnSidePitch();
    ShowUseMatchTeamIfTraining();
    UpdateTeamCheckmark();
    DrawMenu();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void ExitPlayMatch(void) {
    _exitGameFlag = 1;
    _player1ClearFlag = 0;
    _player2ClearFlag = 0;
    _isCareer = 0;
    if (_playMatchTeam1Ptr == &_careerTeam ||
        _playMatchTeam2Ptr == &_careerTeam)
        _isCareer = 1;
    SetExitMenuFlag();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchSelected(void) {
    if (_careerTeam.team.teamControls == TEAM_COMPUTER)
        _careerTeamCoachOnly = 1;
    if (_pl1Fire) {
        _playerNumThatStarted = 1;
        _player1ClearFlag = 0; _player2ClearFlag = 0;
        SetExitMenuFlag();
    } else if (_pl2Fire) {
        _playerNumThatStarted = 2;
        _player1ClearFlag = 0; _player2ClearFlag = 0;
        SetExitMenuFlag();
    }
}

/* ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsInPlayMatchMenu(void) {
    UpdateLittleCheckmark();
    ShowStandardChooseTacticsMenu((TeamFileEx *)_playMatchTeam1Ptr);
}

/* ================================================================ */
__attribute__((used, externally_visible))
void ViewOpponentsSelect(void) {
    if (_viewingOpponentTeam) {
        _viewingOpponentTeam = 0;
        goto common;
    }
    if (_trainingGame) {
        _is_A_team ^= 1;
        TeamFileEx *tmp = _playMatchTeam1Ptr;
        _playMatchTeam1Ptr = _playMatchTeam2Ptr;
        _playMatchTeam2Ptr = tmp;
        _numPlayersToShowInMenu = _careerNumPlayers;
        if (_playMatchTeam1Ptr != &_careerTeam)
            _numPlayersToShowInMenu = _numPlayers;
        _selectedPlayerPosition = -1;
        SavePositionsAndUpdateLittleCheckmark();
        UpdateLittleCheckmark();
        SetVariableButtons();
        SetUpTacticsButtons();
        SetPlayerSurnamesOnSidePitch();
        ShowOrHidePlayMatchButton();
        UpdateTeamCheckmark();
        DrawMenu();
        return;
    }
    _viewingOpponentTeam = 1;
    {
        if (_playMatchTeam2Ptr->team.teamControls != TEAM_COMPUTER || !_squadChangesAllowed)
            SetCurrentEntry(11);
    }
common:
    SetCurrentSelectedPlayerIndexPointer();
    UpdateLittleCheckmark();
    SetVariableButtons();
    SetUpTacticsButtons();
    SetPlayerSurnamesOnSidePitch();
    ShowOrHidePlayMatchButton();
    UpdateTeamCheckmark();
    DrawMenu();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void ReturnFromViewOpponent(void) {
    _viewingOpponentTeam = 0;
    SetCurrentSelectedPlayerIndexPointer();
    SetVariableButtons();
    SetCurrentEntry(7);
    SetUpTacticsButtons();
    SetPlayerSurnamesOnSidePitch();
    ShowOrHidePlayMatchButton();
    UpdateTeamCheckmark();
    DrawMenu();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatchOnSelect30(void) {
    RotatePlayersLeft((TeamFileEx *)_playMatchTeam1Ptr, _maxSubstitutesEntries);
    DrawMenuItemsWaitForVSync(_playMatchRedrawList);
}

/* ================================================================ */
__attribute__((used, externally_visible))
void PlayMatch30AfterDraw(void) {
    CallDrawMenuItem(&_menuEntries[29]);
}

/* ================================================================ */
__attribute__((used, externally_visible))
void RedrawPlayerNames(void) {
    CallDrawMultipleItems(11, &_menuEntries[13]);
    CallDrawMenuItem(&_menuEntries[0]);
}

/* ================================================================ */
__attribute__((used, externally_visible))
void UseMatchTeamSelect(void) {
    TrainingMatch_RestoreLastMatchState();
    uint8_t saved = _currentTeam.team.tactics;
    TrainingMatch_ApplyUseMatchTeamAndSwapSubs();
    _currentTeam.team.tactics = saved;
    TrainingMatch_RestoreSavedTeamSelectionState();
    SetBenchPlayersNumbers(&_careerTeam);
    TrainingMatch_BuildCurrentTeamFromCareerTeam();
    _selectedPlayerPosition = -1;
    SavePositionsAndUpdateLittleCheckmark();
    UpdateLittleCheckmark();
    SetVariableButtons();
    SetUpTacticsButtons();
    SetPlayerSurnamesOnSidePitch();
    ShowOrHidePlayMatchButton();
    UpdateTeamCheckmark();
    SetCurrentEntry(0x1D);
    DrawMenu();
}

/* ================================================================ */
__attribute__((used, externally_visible))
void TogglePlayerCoach(void) {
    if (_gameType == GAME_TYPE_CAREER) {
        if (_is_A_team) {
            uint8_t nc = (_currentTeam.team.teamControls == TEAM_PLAYER_COACH)
                         ? TEAM_COACH : TEAM_PLAYER_COACH;
            _currentTeam.team.teamControls = nc;
            _careerTeam.team.teamControls = 1;
            _coachOrPlayer = 0;
        } else {
            _coachOrPlayer ^= 1;
            uint8_t nc = _coachOrPlayer ? TEAM_PLAYER_COACH : TEAM_COACH;
            _careerTeam.team.teamControls = nc;
            _selectedTeams->team.teamControls = nc;
            if (_trainingGame)
                _currentTeam.team.teamControls = 1;
        }
    } else {
        TeamFileEx *t = _playMatchTeam1Ptr;
        t->team.teamControls = (t->team.teamControls == TEAM_PLAYER_COACH)
                          ? TEAM_COACH : TEAM_PLAYER_COACH;
    }
    SetVariableButtons();
    DrawMenuItemsWaitForVSync(_togglePlayerCoachRedrawItemList);
}

/* ================================================================
 * DrawPlayerEntries  (line 81983)
 *
 * Before-draw callback for player entries in play match menu.
 * Sets colors, status sprites, shirt numbers, names, positions,
 * skills, and value/status for each player row.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawPlayerEntries(MenuEntry *a5) {
    int16_t d7 = a5->ordinal - 13;
    int16_t d5 = 0;

    MenuEntry *bgE = &_menuEntries[31];  /* entry 31 */
    MenuEntry *numE = &_menuEntries[33]; /* entry 33 */

    if (d7 == 16) {
        d5 = 1;
        d7 = d7 - 5 + _maxSubstitutesEntries;
        bgE->bgColor = (bgE->bgColor & 0xFFF0) | BLUE_TO_BLACK_8;
        numE->bgColor = (numE->bgColor & 0xFFF0) | BLUE_TO_BLACK_8;
    } else if (d7 < 11) {
        if (!_showingCpuTeams && !_viewingOpponentTeam) {
            uint16_t c = (d7 == 0) ? YELLOW_GREEN_BLACK_14 : BRIGHT_BLUE_TO_BLUE_13;
            bgE->bgColor = (bgE->bgColor & 0xFFF0) | c;
            numE->bgColor = (numE->bgColor & 0xFFF0) | c;
        } else {
            bgE->bgColor = (bgE->bgColor & 0xFFF0) | RED_10;
            numE->bgColor = (numE->bgColor & 0xFFF0) | RED_10;
        }
    } else {
        /* >= 11: substitute */
        if (!_viewingOpponentTeam && !_showingCpuTeams) {
            if (d7 == 11 && _maxSubstitutesEntries > 5) {
                bgE->bgColor = (bgE->bgColor & 0xFFF0) | YELLOW_GREEN_BLACK_14;
                numE->bgColor = (numE->bgColor & 0xFFF0) | YELLOW_GREEN_BLACK_14;
            } else {
                bgE->bgColor = (bgE->bgColor & 0xFFF0) | BLUE_TO_PURPLE_11;
                numE->bgColor = (numE->bgColor & 0xFFF0) | BLUE_TO_PURPLE_11;
            }
        } else {
            bgE->bgColor = (bgE->bgColor & 0xFFF0) | ORANGE_TO_BROWN_12;
            numE->bgColor = (numE->bgColor & 0xFFF0) | ORANGE_TO_BROWN_12;
        }
    }

    TeamFileEx *team = _playMatchTeam1Ptr;
    if (_viewingOpponentTeam) team = _playMatchTeam2Ptr;

    uint16_t slot, fIdx;
    uint8_t *player = GetPlayerInfoBreakPosition((TeamFileEx *)team, (uint16_t)d7, &slot, &fIdx);
    int16_t d4 = (int16_t)slot;
    int16_t d1 = (int16_t)fIdx;

    /* Injured check — DISPLAY decision, so it must use the honest status.
     * IsPlayerInjured carries the testers'-rule (test_rules.h) override
     * (returns 0 for everyone), which is a GAMEPLAY-availability override; using
     * it here made an injured/suspended player's row keep its normal colour
     * while SetSquadMenuEntryStatusSprite — which reads pf->cardsInjuries
     * directly and is unaffected by the flag — still drew the "3" indicator.
     * Same split already applied to the edit-tactics squad screen
     * (edit_tactics.c EditTacticsAfterDrawPlayerNames). */
    if (!_viewingOpponentTeam) {
        if (IsPlayerInjuredForDisplay((PlayerFile *)player, _playMatchExcludeFlags)) {
            bgE->bgColor = (bgE->bgColor & 0xFFF0) | RED_10;
            numE->bgColor = (numE->bgColor & 0xFFF0) | RED_10;
        }
    }

    /* Selected player highlight */
    if (!_viewingOpponentTeam && d1 == _selectedPlayerPosition) {
        bgE->bgColor = (bgE->bgColor & 0xFFF0) | ORANGE_TO_BROWN_6;
        numE->bgColor = (numE->bgColor & 0xFFF0) | ORANGE_TO_BROWN_6;
    }

    bgE->y = a5->y;

    /* Status sprite */
    SetSquadMenuEntryStatusSprite(bgE, (PlayerFile *)player, (uint16_t)_playMatchExcludeFlags);

    /* Position change arrows */
    if (!_viewingOpponentTeam && d4 < 11) {
        uint8_t cur = _playerPositionsArray[d1];
        uint8_t old = _oldPlayerPositionsArray[d1];
        if (cur < old) bgE->fgWord = SPR_CROSS_SMALL;       /* value fell → red cross */
        else if (cur > old) bgE->fgWord = SPR_CHECK_SMALL;  /* value rose → green tick */
    }

    /* Checkmark entry sync */
    {
        int16_t selIdx;
        if (_viewingOpponentTeam) {
            selIdx = (int16_t)*(uint16_t *)_currentSelectedPlayerIndexPtr;
        } else if (_playMatchTeam1Ptr == &_careerTeam) {
            selIdx = (int16_t)_selTeamSelectedPlayerPosition;
        } else if (_playMatchTeam1Ptr == &_currentTeam) {
            selIdx = (int16_t)_currentTeamSelectedPlayerPosition;
        } else {
            selIdx = (int16_t)*(uint16_t *)_currentSelectedPlayerIndexPtr;
        }
        if (selIdx == d1) {
            MenuEntry *ck = &_menuEntries[37];
            ck->y = bgE->y;
            ck->bgColor = bgE->bgColor;
            ck->fgWord = bgE->fgWord;
        }
    }

    /* Shirt number entry (entry 32) */
    MenuEntry *shE = bgE + 1;
    PlayerFile *playerPf = (PlayerFile *)(player + 76);
    shE->isInvisible = 1;
    if (!d5) {
        shE->fgWord = (uint16_t)(uint8_t)playerPf->shirtNumber;
        shE->isInvisible = 0;
    }
    shE->y = a5->y;

    /* Surname entry (entry 33 = numE) */
    {
        uint8_t *surname = ExtractSurname((uint8_t *)playerPf->playerName, 0x800D);
        numE->fg = (uintptr_t)surname;
        numE->y = a5->y;
    }

    /* Position text (entry 34) */
    MenuEntry *posE = numE + 1;
    {
        uint16_t posField = (playerPf->positionAndFace & PF_POSITION_MASK) >> 3;
        posE->fg = (uintptr_t)_playerPositionsStringTable[posField >> 2];
        posE->y = a5->y;
    }

    /* Skills string (entry 35) */
    MenuEntry *skE = posE + 1;
    FormTopSkillsString((uint8_t *)skE->fg, (PlayerFile *)player);
    skE->y = a5->y;

    /* Value/status (entry 36) */
    MenuEntry *valE = skE + 1;
    valE->isInvisible = 1;
    valE->y = a5->y;

    {
        if (IsTeamNational((TeamFileEx *)_playMatchTeam1Ptr)) {
            InitPlayerValueDisplayStruct(player, (uint8_t *)valE);
        } else {
            FormatPlayerValueOrStatusText(player, _fundsBuffer);
            valE->fg = (uintptr_t)_fundsBuffer;
        }
    }

    {
        uint16_t cb = 0x0D;
        uint8_t sf = (uint8_t)playerPf->form;
        if (sf == 0x9B)      cb = 0x0D;
        else if (sf == 0x9A) cb = 0x01;
        else {
            cb = 0x0F;
            if (!(playerPf->positionAndFace & PF_LOAN)) cb = PF_LOAN;
        }
        valE->stringFlags = (valE->stringFlags & 0xFFF0) | cb;
        valE->isInvisible = 0;
    }

    DrawMultipleItems(6, &_menuEntries[31]);
}

/* ================================================================
 * Chunk 151: FormTopSkillsString
 *
 * Forms the 3-letter skill abbreviation string shown after a player's
 * name in the play match menu (e.g., "TSP", "HCF").
 *
 * in:  a1 = destination buffer (must hold 4 bytes)
 *      a6 = player base (team entry pointer, player data at +0x4C)
 *
 * The algorithm:
 * - Get position group (bits 7-5 of POSITIONANDFACE field)
 * - Select even/odd skill letter and bit-rotation tables based on
 *   XOR of price and first two name bytes (bit 0)
 * - Extract 7 skill values (3-bit each) from PASSING longword via
 *   rotating by amounts from the bit-rotation table
 * - Pick the top 3 skill values and output their corresponding letters
 * ================================================================ */
extern void *_topSkillLettersEven[];  /* 7 pointers to letter strings */
extern void *_topSkillLettersOdd[];
extern void *_topSkillBitRotEven[];   /* 7 pointers to byte arrays */
extern void *_topSkillBitRotOdd[];

__attribute__((used, externally_visible))
void FormTopSkillsString(uint8_t *dst, PlayerFile *playerFile)
{
    /* playerFile = GetPlayer-style pointer (teamBase + fileIdx*38) cast to PlayerFile *.
     * PlayerFile data is at playerFile[2] (2 * 38 = 76 bytes in). */
    PlayerFile *pf = &playerFile[2];

    uint16_t posGroup = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
    if (posGroup == 0) {
        /* Goalkeeper: no skill string */
        *dst = 0;
        return;
    }
    posGroup -= 1;  /* 0-based index into tables (0-6) */

    /* Determine even/odd variant from XOR of price and name bytes */
    uint8_t price = (uint8_t)pf->price;
    uint8_t name0 = (uint8_t)pf->playerName[0];
    uint8_t name1 = (uint8_t)pf->playerName[1];
    uint8_t xorVal = price ^ name0 ^ name1;

    const uint8_t *letters;
    const uint8_t *bitRot;

    if (xorVal & 1) {
        letters = (const uint8_t *)_topSkillLettersOdd[posGroup];
        bitRot = (const uint8_t *)_topSkillBitRotOdd[posGroup];
    } else {
        letters = (const uint8_t *)_topSkillLettersEven[posGroup];
        bitRot = (const uint8_t *)_topSkillBitRotEven[posGroup];
    }

    /* Get PASSING longword (4 skills packed: passing, velocityHeading,
     * tacklingControl, speedFinishing) and shift left 4.
     *
     * The Amiga asm did `move.l (a0)+,d7` which on m68k BE reads the
     * bytes as (passing<<24)|(velocityHeading<<16)|(tacklingControl
     * <<8)|speedFinishing. A native `*(uint32_t *)` on our LE host
     * reverses the byte order, so the subsequent rot+mask extracts the
     * wrong skill nibbles — giving wrong top-skill letters ("VHF" in
     * place of "STP" etc.) and breaking the star count the same way.
     * Use BE_READ_U32 so both builds see the same packed layout. */
    uint32_t d7 = BE_READ_U32(&pf->passing);
    d7 <<= 4;

    /* Find top 3 skill values. Each skill is extracted as 3 bits after
     * rotating d7 left by the amount from bitRot[]. */
    int16_t best1 = -1, best2 = -1, best3 = -1;   /* skill values */
    uint8_t let1 = 0, let2 = 0, let3 = 0;          /* corresponding letters */

    for (int16_t i = 0; i < 7; i++) {
        uint8_t rot = bitRot[i];
        uint32_t rotated = (d7 << rot) | (d7 >> (32 - rot));
        uint8_t skillVal = (uint8_t)(rotated & 7);

        if ((int8_t)skillVal > (int8_t)best1) {
            best3 = best2; let3 = let2;
            best2 = best1; let2 = let1;
            best1 = skillVal;
            let1 = letters[i];
        } else if ((int8_t)skillVal > (int8_t)best2) {
            best3 = best2; let3 = let2;
            best2 = skillVal;
            let2 = letters[i];
        } else if ((int8_t)skillVal > (int8_t)best3) {
            best3 = skillVal;
            let3 = letters[i];
        }
    }

    *dst++ = let1;
    *dst++ = let2;
    *dst++ = let3;
    *dst = 0;
}

/* ================================================================
 * Chunk 212: ShowPlayMatchMenu (1 → play_match_menu.c)
 * ================================================================ */

extern uint16_t _inEditTactics;
extern uint8_t _playMatchMenu[];
extern void TrainingMatch_AdjustSelectedTeamRosterForTrialists(uint16_t teamNum, uint16_t baseCount, TeamFileEx *teamPtr);

/* ShowPlayMatchMenu (SWOS2.s line 65312)
 * Sets up play match globals from params, optionally adjusts trialists,
 * shows the play match menu, returns playerNumThatStarted. */
__attribute__((used, externally_visible))
int16_t ShowPlayMatchMenu(int16_t controlFlags, int16_t _maxSubstitutes,
                            int16_t excludeFlags, int16_t isFirstPlayer,
                            int16_t squadChanges, TeamFileEx *team1, TeamFileEx *team2)
{
    _isFirstPlayerChoosingTeam = (uint16_t)isFirstPlayer;
    _squadChangesAllowed = (uint16_t)squadChanges;
    _playMatchTeam1Ptr = team1;
    _playMatchTeam2Ptr = team2;
    _maxSubstitutesEntries = (uint16_t)_maxSubstitutes;
    _playMatchControlFlags = (uint16_t)controlFlags;
    _playMatchExcludeFlags = (int16_t)excludeFlags;
    _inEditTactics = (uint16_t)excludeFlags;

    if (_gameType == GAME_TYPE_CAREER) {
        TrainingMatch_AdjustSelectedTeamRosterForTrialists(
            (uint16_t)_playMatchExcludeFlags,
            _maxSubstitutesEntries,
            (TeamFileEx *)_playMatchTeam1Ptr);
    }

    ShowMenu(_playMatchMenu);
    return (int16_t)_playerNumThatStarted;
}
