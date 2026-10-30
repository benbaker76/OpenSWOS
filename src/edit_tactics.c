/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* edit_tactics.c — Edit tactics menu functions.
 *
 * Chunk 82: ChooseTacticsInit, ShowStandardChooseTacticsMenu,
 *           EditTacticsInit, EditTacticsOnRestore,
 *           EnableOrDisablePlayerFacesItems, CopyCurrentTacticName,
 *           EditTactics_UpdateExtraPlayerMenuVisibility, SetBigCheckSprite,
 *           CopyCurrentTactics, ApplyCurrentEditedTactics,
 *           SaveCurrentTactics, RestoreTactics
 * Chunk 83: DrawPlayerNamesAndFaces, EditTacticsMenuSavePlayerPositions,
 *           EditTactics_CommitPlayerPositionsAndRecalcSkill,
 *           ImportTacticsInit, ImportTacticsSelectTactics, ExitImportTactics,
 *           MarkSelectedTeamsTacticsChanged, FillSkinColorConversionTable
 * Chunk 84: SelectCopyTactics, UndoTactics, AbortEditTactics, FlipOnOff,
 *           ImportTactics, SaveAndExitEditTactics,
 *           EditTacticsOnSelect28, EditTacticsAfterDrawPlayerFace,
 *           EditTactics28AfterDraw, EditTactics2AfterDraw
 * Chunk 85: GetPlayerQuadrant, SetPlayerQuadrant, GetBallQuadrant,
 *           GetBallXYPositionFromQuadrant, GetPlayerXYPositionFromQuadrant,
 *           EditTactics_GetDirectionDelta, GetEditTacticsItemXYPosition,
 *           EditTactics_TryMoveQuadrantCursor,
 *           EditTactics_SetAllPlayersQuadrantForBallLoc,
 *           EditTactics_FlipMaybe_MirrorPlayersInArea,
 *           EditTactics_FlipMaybe_MirrorBallArea,
 *           EditTactics_CommitDraggedPlayerPosition,
 *           SortEditTacticsDrawOrderByY, SortPlayerAreasByY,
 *           EditTacticsDrawCursor, DrawPlayerDirections
 * Chunk 86: InitLittlePlayerSprites, DrawLittlePlayersAndBall,
 *           DrawEditTacticsHighlightedPlayerRoute
 * Chunk 87: EditTacicsMovePlayersOrBall, CopyTactics,
 *           DrawEditTacticsPitchAndPlayers, EditTacticsReadControls,
 *           HandlePlayersOrBallInEditTactics, EditTacticsStatus,
 *           EditTactics_HandlePitchInput
 * Chunk 88: GetPlayerInfoBreakPosition, EditTactics_FindFreePairId,
 *           EditTactics_ClearPairId, EditTactics_SwapPlayersInSlots,
 *           EditTacticsCommon, EditTacticsPlayerNamesCommon
 * Chunk 89: LoadTactics
 */

#include "shirt_numbers.h"
#include "edit_tactics.h"
#include "endian_helpers.h"
#include "menu_system.h"
#include "string_utils.h"
#include "file_io.h"
#include "teams.h"
#include "menus.h"
#include "math68k.h"
#include "panic.h"
#include "sprites.h"
#include "team_colors.h"
#include "input_text.h"
#include "career_teams.h"
#include "career_status.h"
#include "play_match_menu.h"
#include "player_sprites.h"
#include "match_sim.h"

/* ---- Menu entries (defined in asm) ---- */
extern MenuEntry _menuEntries[];

/* ---- Tactics data (defined in asm) ---- */
extern TeamTactics _teamTacticsUSERA;    /* start of USER A tactics (name at offset 0) */
extern TeamTactics _teamTacticsUSERB;
extern TeamTactics _teamTacticsUSERC;
extern TeamTactics _teamTacticsUSERD;
extern TeamTactics _teamTacticsUSERE;
extern TeamTactics _teamTacticsUSERF;

extern void *_teamTacticsTable[];  /* pointer table indexed by chosenTactics */
extern TeamTactics _editTacticsCurrentTactics;  /* 370-byte tactics buffer */
extern uint8_t _savedCurrentTactics;        /* 370-byte saved tactics buffer */

/* ---- Globals (defined in asm) ---- */
extern uint16_t _chosenTactics;
extern uint16_t _flipOnOff;
extern TeamFileEx *_chooseTacticsTeamPtr;
extern uint16_t _selTeamNumber;
extern uint16_t _inEditTactics;
extern uint16_t _careerNumPlayers;
extern uint16_t _editTacticsNumPlayers;
extern uint16_t _editTacticsSelectedPairSlotIndex;
extern MenuEntry *_editTacticsSelectedPlayerMenuEntryPtr;
extern uint16_t _editTacticsSelectedPlayerNameIndex;
extern uint16_t _teamSkillCurrent;
extern uint16_t _teamSkillSnapshot;

/* ---- Globals for Chunk 84 ---- */
extern uint16_t _tacticsChangedFlag;
extern uint8_t _importTacticsMenu[];
extern uint8_t _selectedFilename[];
extern uintptr_t _extension;              /* file extension from GetFilenameAndExtension */
extern uint8_t _importTacticsFilename;   /* filename buffer */

/* ---- String literals (defined in asm) ---- */
extern uint8_t _aLoadEditedTact[];               /* "LOAD EDITED TACTICS" */
extern uint8_t _aNotATacticsFil[];               /* "NOT A TACTICS FILE" */
extern uint8_t _aEditTactics[];                  /* "EDIT TACTICS" */
extern uint8_t _aEditedTacticsWillBeLost[];      /* "\x01EDITED TACTICS WILL BE LOST" */
extern uint8_t _aContinue[];                     /* "CONTINUE" */
extern uint8_t _aAbort[];                        /* "ABORT" */
extern uint8_t _aSaveEditedTact[];               /* "SAVE EDITED TACTICS" */
extern uint8_t _aTacticsHaveBeenChanged[];       /* "\x01TACTICS HAVE BEEN CHANGED" */
extern uint8_t _aSave[];                         /* "SAVE" */
extern uint8_t _aExit[];                         /* "EXIT" */
extern int16_t _editTacticsRotateRedrawList[];   /* {10, 27, -1} */

/* ---- Globals for Chunk 85 ---- */
extern uint16_t _editTacticsPitchStatus;
extern uint16_t _markedPlayerNumber;
extern uint16_t _ballRow;
extern uint16_t _ballColumn;
extern uint16_t _editTacticsBallLocation;
extern uint16_t _editTacticsLastBallLocation;
extern uint16_t _editTacticsPlayerX;
extern uint16_t _editTacticsBallRow;
extern uint16_t _editTacticsBallColumn;
extern uint16_t _editTacticsDisableLittlePlayersAndBallDraw;
extern uint16_t _editTacticsTransitionActive;
extern uint16_t _editTacticsLastTick;
extern int16_t _editTacticsPlayerPositions[];    /* 12 entries: -1,0,1,...,10 */
extern uintptr_t _editTacticsPlayerNumbers;       /* uint8_t*-shaped pointer; retyped uintptr_t for 64-bit. See project_uint32_pointer_storage_trap.md. */
extern int16_t _playerAreas[];                   /* 35 entries: 0..34 */
extern uint16_t _menuControlsDirection;
extern uint16_t _menuControlsDirection2;
extern volatile uint16_t _fire;
extern uint16_t _currentGameTick;
extern uint16_t _panicReasonCode;

/* ---- Lookup tables (defined in asm) ---- */
extern int16_t _ballXQuadrantCoordinates[];      /* 5 entries */
extern int16_t _ballYQuadrantCoordinates[];      /* 7 entries */
extern int16_t _xQuadrantToCoordinates[];        /* 15 entries */
extern int16_t _yQuadrantToCoordinates[];        /* 16 entries */
extern int8_t _arrowSpritesOffsets[];             /* 16 bytes (8 pairs of x,y) */
extern uint16_t _tacticCursorFrames[];            /* 4 entries */
extern uint8_t _editTacticsDirectionDeltaTable[]; /* 16 bytes (8 pairs) */

/* ---- Globals for Chunk 86 ---- */
extern uint16_t _littlePlayersConvertTable[];    /* 16-word color conversion table */
extern uint8_t _currentMatchPlayers[];           /* player number array for career team */
extern TeamFileEx _careerTeam;                    /* start of career team data */

/* ---- Globals for Chunk 87 ---- */
extern int16_t _fireCounter;
extern uint8_t _up;
extern uint8_t _down;
extern uint8_t _left;
extern uint8_t _right;
extern uint16_t _editTacticsInputRepeatCounter;
extern uint16_t _editTacticsNearestSelectPitchWidth;

/* ---- Globals for Chunk 88 ---- */
extern void *_positionsTable[];       /* pointer table indexed by formation */
extern uint16_t _editTacticsSelectedPlayerSlotIndex;
extern MenuEntry *_editTacticsSelectedPairMenuEntryPtr;
extern uint16_t _gameType;
extern uint8_t _playerPositions[];       /* player positions translation table (current team) */
extern uint16_t _cGetPlayerAtIndexFileIdx;
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);

/* ---- Menu definitions (defined in asm) ---- */
extern uint8_t _chooseTacticsMenu[];
extern uint8_t _editTacticsMenu[];

/* ---- C-callable bridges for asm functions (defined in swos2_unit.s) ---- */
extern void SetBenchPlayersNumbers(TeamFileEx *team);
extern void EditTactics_CommitPlayerPositionsAndRecalcSkill(void);
extern void EditTacticsMenuSavePlayerPositions(void);
extern void InitLittlePlayerSprites(void);
extern void InitializeTacticsPositions(void);
extern void DrawEditTacticsPitchAndPlayers(void);
/* EditTactics_ApplyPlayerRowHighlighting declared in edit_tactics.h */
extern void RotatePlayersLeft(TeamFileEx *team, uint16_t maxSubs);

/* ---- Sprite constants ----
 * SPR_CHECK_BIG/SPR_CROSS_BIG/SPR_CHECK_SMALL/SPR_CROSS_SMALL come from
 * play_match_menu.h (included above). */

/* ================================================================
 * ChooseTacticsInit  (SWOS2.s line 86669)
 *
 * Initializes the choose tactics menu by setting the fg string
 * pointer of menu entries 1-6 to the tactics name addresses
 * (USER A through USER F). Also calls CheckIsTeamNational.
 * ================================================================ */
__attribute__((used, externally_visible))
void ChooseTacticsInit(void)
{
    CheckIsTeamNational();
    _menuEntries[1].fg = (uintptr_t)&_teamTacticsUSERA;
    _menuEntries[2].fg = (uintptr_t)&_teamTacticsUSERB;
    _menuEntries[3].fg = (uintptr_t)&_teamTacticsUSERC;
    _menuEntries[4].fg = (uintptr_t)&_teamTacticsUSERD;
    _menuEntries[5].fg = (uintptr_t)&_teamTacticsUSERE;
    _menuEntries[6].fg = (uintptr_t)&_teamTacticsUSERF;
}

/* ================================================================
 * ShowStandardChooseTacticsMenu  (SWOS2.s line 86450)
 *
 * Shows the choose tactics menu, then if a tactic was chosen
 * (chosenTactics != 0xFFFF), initializes little player sprites
 * and shows the edit tactics menu. Clears chooseTacticsTeamPtr
 * when done.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowStandardChooseTacticsMenu(TeamFileEx *teamPtr)
{
    _chooseTacticsTeamPtr = teamPtr;
    ShowMenu(_chooseTacticsMenu);

    if (_chosenTactics != 0xFFFF) {
        InitLittlePlayerSprites();
        ShowMenu(_editTacticsMenu);
    }

    _chooseTacticsTeamPtr = (TeamFileEx *)0;
}

/* ================================================================
 * EditTacticsInit  (SWOS2.s line 87481)
 *
 * Initializes the edit tactics menu. Sets bench player numbers,
 * determines editTacticsNumPlayers based on whether selected team
 * matches the career team, then clears selection state and
 * falls through to EditTacticsOnRestore.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsInit(void)
{
    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    SetBenchPlayersNumbers(teamEx);

    uint16_t _numPlayers;
    if (_selTeamNumber == 0xFFFF || PACK_BE_U16(teamEx->team.countryNumber, teamEx->team.teamOrdinal) != _selTeamNumber) {
        _numPlayers = 16;
    } else {
        _numPlayers = _careerNumPlayers;
    }
    _editTacticsNumPlayers = _numPlayers;

    _editTacticsSelectedPairSlotIndex = 0xFFFF;
    _editTacticsSelectedPlayerMenuEntryPtr = (MenuEntry *)0;
    _editTacticsSelectedPlayerNameIndex = 0xFFFF;

    EditTactics_CommitPlayerPositionsAndRecalcSkill();
    EditTacticsMenuSavePlayerPositions();

    /* Fallthrough to EditTacticsOnRestore */
    EditTacticsOnRestore();
}

/* ================================================================
 * EditTacticsOnRestore  (SWOS2.s line 87506)
 *
 * Called when returning to the edit tactics menu. Enables/disables
 * player face items, copies tactic name, updates extra player
 * visibility, and sets the big check sprite.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsOnRestore(void)
{
    EnableOrDisablePlayerFacesItems();
    CopyCurrentTacticName();
    EditTactics_UpdateExtraPlayerMenuVisibility();
    SetBigCheckSprite();
}

/* ================================================================
 * EnableOrDisablePlayerFacesItems  (SWOS2.s line 87869)
 *
 * Enables or disables 10 player face items in the edit tactics
 * menu based on the flipOnOff flag. Items start at menuEntries
 * index 30 (offset 0x658 / 56 = 29, but MENUENTRY_ALREADYDRAWN
 * offset $658 from menuEntries base = entry index
 * 0x658/56 = 28.89... Let me recalculate: 0x658 = 1624,
 * 1624/56 = 29). So entries 29..38 (10 entries).
 * ================================================================ */
__attribute__((used, externally_visible))
void EnableOrDisablePlayerFacesItems(void)
{
    /* menuEntries + 0x658 = entry at byte offset 1624 = index 29
     * (0x658 / 56 = 29) */
    MenuEntry *entry = &_menuEntries[29];
    for (int16_t i = 9; i >= 0; i--) {
        if (_flipOnOff == 0) {
            entry->isDisabled = 1;
        } else {
            entry->isDisabled = 0;
        }
        entry++;
    }
}

/* ================================================================
 * CopyCurrentTacticName  (SWOS2.s line 87887)
 *
 * Builds the string "EDIT TACTICS (tacticname)" in the fg string
 * buffer of menu entry 1. Uses StringCopy for the parts.
 * ================================================================ */
__attribute__((used, externally_visible))
void CopyCurrentTacticName(void)
{
    /* menuEntries[1].fg holds a pointer to the output string buffer */
    uint8_t *dst = (uint8_t *)_menuEntries[1].fg;

    /* Copy "EDIT TACTICS" */
    dst = StringCopy(_aEditTactics, dst);

    /* Append " (" */
    *dst++ = ' ';
    *dst++ = '(';

    /* Copy current tactics name */
    dst = StringCopy((uint8_t *)_editTacticsCurrentTactics.name, dst);

    /* Append ")" and NUL */
    *dst++ = ')';
    *dst = 0;
}

/* ================================================================
 * EditTactics_UpdateExtraPlayerMenuVisibility  (SWOS2.s line 87992)
 *
 * Shows/hides extra player menu entries (17th and 18th players)
 * based on editTacticsNumPlayers. If 16, both hidden. If 17,
 * first shown. If 18, both shown.
 *
 * Entries at offsets 0x5E8 and 0x620:
 *   0x5E8 / 56 = 26.857... → but with MENUENTRY_ALREADYDRAWN=0,
 *   0x5E8 = 1512, 1512/56 = 27. Entry 27.
 *   0x620 = 1568, 1568/56 = 28. Entry 28.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_UpdateExtraPlayerMenuVisibility(void)
{
    /* Start with both invisible */
    _menuEntries[27].isInvisible = 1;
    _menuEntries[28].isInvisible = 1;

    int16_t extra = (int16_t)(_editTacticsNumPlayers - 16);
    if (extra == 0)
        return;

    /* At least 1 extra player - show entry 27 */
    _menuEntries[27].isInvisible = 0;

    if (extra == 1)
        return;

    /* 2 extra players - show entry 28 */
    _menuEntries[28].isInvisible = 0;
}

/* ================================================================
 * SetBigCheckSprite
 *
 * Big "did this change help the team?" mark on menuEntries[0], shown while
 * editing the formation. Compares the team's freshly-recomputed skill
 * (_teamSkillCurrent — refreshed by ComputeOldPlayerSkillForViewResult) against
 * the baseline captured on entry (_teamSkillSnapshot):
 *   - equal                        → hidden
 *   - recomputed > baseline (team STRONGER) → SPR_CHECK_BIG (green tick, 206)
 *   - recomputed < baseline (team WEAKER)   → SPR_CROSS_BIG (red cross, 207)
 * ================================================================ */
__attribute__((used, externally_visible))
void SetBigCheckSprite(void)
{
    MenuEntry *entry = &_menuEntries[0];

    /* Start invisible, clear bit 13 of stringFlags */
    entry->isInvisible = 1;
    entry->stringFlags &= ~(1u << 13);

    if (_teamSkillCurrent == _teamSkillSnapshot)
        return;

    if (_teamSkillCurrent > _teamSkillSnapshot) {
        entry->fgWord = SPR_CHECK_BIG;   /* stronger → green tick */
    } else {
        entry->fgWord = SPR_CROSS_BIG;   /* weaker → red cross */
    }

    /* Set bit 13 of stringFlags and make visible */
    entry->stringFlags |= (1u << 13);
    entry->isInvisible = 0;
}

/* ================================================================
 * CopyCurrentTactics  (SWOS2.s line 89419)
 *
 * Copies 370 bytes from teamTacticsTable[chosenTactics] into
 * editTacticsCurrentTactics.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void CopyCurrentTactics(void)
{
    /* volatile: LTO with -Ofast may eliminate this copy because
     * teamTacticsTable[18] aliases editTacticsCurrentTactics. */
    volatile uint8_t *src = (volatile uint8_t *)_teamTacticsTable[_chosenTactics];
    volatile uint8_t *dst = (volatile uint8_t *)&_editTacticsCurrentTactics;

    for (int16_t i = 369; i >= 0; i--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * ApplyCurrentEditedTactics  (SWOS2.s line 89433)
 *
 * Copies 370 bytes from editTacticsCurrentTactics back into
 * teamTacticsTable[chosenTactics], then calls
 * InitializeTacticsPositions.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void ApplyCurrentEditedTactics(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)_teamTacticsTable[_chosenTactics];
    volatile uint8_t *src = (volatile uint8_t *)&_editTacticsCurrentTactics;

    for (int16_t i = 369; i >= 0; i--) {
        *dst++ = *src++;
    }

    InitializeTacticsPositions();
}

/* ================================================================
 * SaveCurrentTactics  (SWOS2.s line 89485)
 *
 * Copies 370 bytes from editTacticsCurrentTactics into
 * savedCurrentTactics.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void SaveCurrentTactics(void)
{
    volatile uint8_t *src = (volatile uint8_t *)&_editTacticsCurrentTactics;
    volatile uint8_t *dst = (volatile uint8_t *)&_savedCurrentTactics;

    for (int16_t i = 369; i >= 0; i--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * RestoreTactics  (SWOS2.s line 89496)
 *
 * Copies 370 bytes from savedCurrentTactics back into
 * editTacticsCurrentTactics.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void RestoreTactics(void)
{
    volatile uint8_t *src = (volatile uint8_t *)&_savedCurrentTactics;
    volatile uint8_t *dst = (volatile uint8_t *)&_editTacticsCurrentTactics;

    for (int16_t i = 369; i >= 0; i--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * DrawPlayerNamesAndFaces  (SWOS2.s line 88239)
 *
 * Draws player name/face menu items in the edit tactics menu.
 * Draws entry at offset 0x268 (index 11), then 10 entries starting
 * at 0x658 (index 29), then entry 0 (the check sprite).
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawPlayerNamesAndFaces(void)
{
    CallDrawMenuItem(&_menuEntries[11]);
    CallDrawMultipleItems(10, &_menuEntries[29]);
    CallDrawMenuItem(&_menuEntries[0]);
}

/* ================================================================
 * EditTacticsMenuSavePlayerPositions  (SWOS2.s line 88253)
 *
 * Copies playerPositionsArray (32 bytes) into oldPlayerPositionsArray,
 * snapshots the team total (_teamSkillSnapshot = _teamSkillCurrent), then calls
 * SetBigCheckSprite.
 * Note: original asm does full d0-d7/a0-a6 save/restore internally.
 * ================================================================ */
extern uint8_t _playerPositionsArray;   /* 32-byte array */
extern uint8_t _oldPlayerPositionsArray; /* 32-byte array */

__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void EditTacticsMenuSavePlayerPositions(void)
{
    _teamSkillSnapshot = _teamSkillCurrent;
    uint8_t *src = &_playerPositionsArray;
    uint8_t *dst = &_oldPlayerPositionsArray;
    for (int16_t i = 31; i >= 0; i--) {
        *dst++ = *src++;
    }
    SetBigCheckSprite();
}

/* ================================================================
 * EditTactics_CommitPlayerPositionsAndRecalcSkill  (SWOS2.s line 88268)
 *
 * Copies playerPositionsArray (32 bytes) to oldPlayerPositionsArray,
 * snapshots the team total (_teamSkillSnapshot = _teamSkillCurrent), sets team flag,
 * temporarily changes team's tactics byte to 0x12, calls
 * ComputeOldPlayerSkillForViewResult with d0=5, restores the
 * original tactics byte, then falls through to SetBigCheckSprite.
 * ================================================================ */
extern uint16_t ComputeOldPlayerSkillForViewResult(TeamFileEx *team, uint16_t goalsCount);

__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void EditTactics_CommitPlayerPositionsAndRecalcSkill(void)
{
    _teamSkillSnapshot = _teamSkillCurrent;
    uint8_t *src = &_playerPositionsArray;
    uint8_t *dst = &_oldPlayerPositionsArray;
    for (int16_t i = 31; i >= 0; i--) {
        *dst++ = *src++;
    }

    TeamFileEx *teamPtr = (TeamFileEx *)_chooseTacticsTeamPtr;
    teamPtr->team.playerValuesDirty = TEAM_VALUES_DIRTY;

    /* Temporarily set tactics byte to 0x12, compute skill, restore */
    uint8_t savedTactics = teamPtr->team.tactics;
    teamPtr->team.tactics = 0x12;
    ComputeOldPlayerSkillForViewResult(teamPtr, 5);
    teamPtr->team.tactics = savedTactics;

    /* Fallthrough to SetBigCheckSprite */
    SetBigCheckSprite();
}

/* ================================================================
 * ImportTacticsInit  (SWOS2.s line 89824)
 *
 * No-op initialization for the import tactics menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ImportTacticsInit(void)
{
    /* Empty — original was just rts */
}

/* ================================================================
 * ImportTacticsSelectTactics  (SWOS2.s line 89829)
 *
 * Menu callback when a tactics entry is selected in the import
 * tactics menu. Calculates the ordinal difference between the
 * selected entry and entry 1 (base), stores in importMenuSelectedTactics,
 * then exits the menu.
 * ================================================================ */
extern uint16_t _importMenuSelectedTactics;

__attribute__((used, externally_visible))
void ImportTacticsSelectTactics(MenuEntry *cur)
{
    _importMenuSelectedTactics = cur->ordinal - _menuEntries[1].ordinal;
    SetExitMenuFlag();
}

/* ================================================================
 * ExitImportTactics  (SWOS2.s line 89838)
 *
 * Sets importMenuSelectedTactics to -1 (abort) and exits the menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitImportTactics(void)
{
    _importMenuSelectedTactics = 0xFFFF;
    SetExitMenuFlag();
}

/* ================================================================
 * MarkSelectedTeamsTacticsChanged  (SWOS2.s line 89983)
 *
 * Marks the career team and all selected teams whose tactics index
 * matches chosenTactics with the "tactics changed" flag.
 * ================================================================ */

extern uint16_t _numSelectedTeams;

#define SELECTED_TEAM_STRIDE   684

__attribute__((used, externally_visible))
void MarkSelectedTeamsTacticsChanged(void)
{
    if (_selTeamNumber != 0xFFFF) {
        _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;
    }

    uint16_t count = _numSelectedTeams;
    if (count == 0)
        return;

    TeamFileEx *team = _selectedTeams;
    for (int16_t i = (int16_t)(count - 1); i >= 0; i--) {
        if (team->team.tactics == (uint8_t)_chosenTactics) {
            team->team.playerValuesDirty = TEAM_VALUES_DIRTY;
        }
        team = (TeamFileEx *)((uint8_t *)team + SELECTED_TEAM_STRIDE);
    }
}

/* ================================================================
 * FillSkinColorConversionTable  (SWOS2.s line 90889)
 *
 * Fills a sprite color conversion table from the skin/hair color
 * conversion table. Reads entries from the table until a negative
 * terminator is encountered.
 * ================================================================ */
extern void *_skinAndHairColorConversionTable[];  /* array of pointers to conversion subtables */

__attribute__((used, externally_visible))
void FillSkinColorConversionTable(uint16_t skinColor, uint16_t *convertTable)
{
    int16_t idx = (int16_t)(int8_t)(uint8_t)skinColor;  /* ext.w d0 */
    /* Same 0xFF-sentinel guard as ConvertColorTableToSkinColor: the
     * skinAndHairColorConversionTable has only 3 entries (skin tones 0/1/2),
     * so a face value of 0xFF (sign-extended to -1) reads
     * skinAndHairColorConversionTable[-1] OOB, returns a garbage pointer,
     * and the (idx,value) walk corrupts random memory — including the
     * caller's convertTable, mapping shirt/shorts/socks/skin all to
     * whatever happens to be there (we observed every player body part
     * rendering as palette 0 except the eyes, which survived because
     * the garbage walk didn't touch their palette index). */
    if (idx < 0)
        return;
    int16_t *subtable = (int16_t *)_skinAndHairColorConversionTable[idx];

    for (;;) {
        int16_t entry = *subtable++;
        if (entry < 0)
            break;
        convertTable[entry] = (uint16_t)*subtable++;
    }
}

/* ================================================================
 * SelectCopyTactics  (SWOS2.s line 87689)
 *
 * Menu callback: moves cursor to entry 3 (copy tactics option).
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectCopyTactics(void)
{
    SetCurrentEntry(3);
}

/* ================================================================
 * UndoTactics  (SWOS2.s line 87696)
 *
 * Menu callback: restores tactics to last saved state, redraws
 * the status entry (index 2), recalculates skill, and redraws
 * player names/faces.
 * ================================================================ */
__attribute__((used, externally_visible))
void UndoTactics(void)
{
    RestoreTactics();
    CallDrawMenuItem(&_menuEntries[2]);
    EditTactics_CommitPlayerPositionsAndRecalcSkill();
    DrawPlayerNamesAndFaces();
}

/* ================================================================
 * AbortEditTactics  (SWOS2.s line 87706)
 *
 * Menu callback: aborts edit tactics. If tactics were changed,
 * shows a confirmation dialog ("EDITED TACTICS WILL BE LOST").
 * If user picks "CONTINUE" (proceed with abort), exits the menu.
 * If user picks "ABORT" (cancel the abort), returns to editing.
 * ================================================================ */
__attribute__((used, externally_visible))
void AbortEditTactics(void)
{
    EditTacticsMenuSavePlayerPositions();
    if (_tacticsChangedFlag != 0) {
        int16_t result = DoContinueAbortMenu(
            _aEditTactics,
            _aEditedTacticsWillBeLost,
            _aContinue,
            _aAbort,
            1
        );
        if (result != 0)
            return;
    }
    SetExitMenuFlag();
}

/* ================================================================
 * FlipOnOff  (SWOS2.s line 87820)
 *
 * Menu callback: toggles player face display on/off, then
 * enables/disables the corresponding menu entries and redraws.
 * ================================================================ */
__attribute__((used, externally_visible))
void FlipOnOff(void)
{
    EditTacticsMenuSavePlayerPositions();
    _flipOnOff ^= 1;
    EnableOrDisablePlayerFacesItems();
    DrawMenu();
}

/* ================================================================
 * ImportTactics  (SWOS2.s line 87788)
 *
 * Menu callback: shows the import tactics submenu. If a standard
 * tactic is selected (not TACTIC_IMPORTED=1000), saves current
 * tactics, copies 370 position bytes from the selected tactic
 * into editTacticsCurrentTactics (preserving the name header),
 * then marks teams as changed and recalculates skill.
 * ================================================================ */
#define TACTIC_IMPORTED         1000

__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void ImportTactics(void)
{
    EditTacticsMenuSavePlayerPositions();

    ShowMenu(_importTacticsMenu);

    int16_t selected = (int16_t)_importMenuSelectedTactics;
    if (selected < 0)
        return;

    if (selected != TACTIC_IMPORTED) {
        SaveCurrentTactics();
        _tacticsChangedFlag = 1;

        /* Copy positions + pairIds + ballOutOfPlay from the selected tactic (361 bytes) */
        TeamTactics *srcTactics = (TeamTactics *)_teamTacticsTable[selected];
        uint8_t *src = (uint8_t *)srcTactics->positions;
        uint8_t *dst = (uint8_t *)_editTacticsCurrentTactics.positions;
        for (int16_t i = 369; i >= 0; i--) {
            *dst++ = *src++;
        }
    }

    /* Mark team as changed */
    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    teamEx->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    MarkSelectedTeamsTacticsChanged();
    CopyCurrentTacticName();
    EditTactics_CommitPlayerPositionsAndRecalcSkill();
}

/* ================================================================
 * SaveAndExitEditTactics  (SWOS2.s line 87724)
 *
 * Menu callback: saves edited tactics to disk. If tactics were
 * changed, applies them and shows a save/exit confirmation.
 * If user chooses "SAVE", prompts for disk, builds filename,
 * writes the .TAC file, updates the import filename, and exits.
 * ================================================================ */
__attribute__((used, externally_visible))
void SaveAndExitEditTactics(void)
{
    EditTacticsMenuSavePlayerPositions();

    if (_tacticsChangedFlag != 0) {
        ApplyCurrentEditedTactics();
        MarkSelectedTeamsTacticsChanged();

        int16_t result = DoContinueAbortMenu(
            _aSaveEditedTact,
            _aTacticsHaveBeenChanged,
            _aSave,
            _aExit,
            0
        );

        if (result == 0) {
            /* User chose "SAVE" — proceed to file save dialog */
            if (Asset_PromptInsertSaveDiskDf0(_aSaveEditedTact) != 0)
                return;

            /* Build default filename: tactics name + ".TAC" */
            uint8_t *end = StringCopy((uint8_t *)_editTacticsCurrentTactics.name,
                                       &_importTacticsFilename);
            *end++ = '.';
            *end++ = 'T';
            *end++ = 'A';
            *end++ = 'C';
            *end = '\0';

            /* Show file selection dialog */
            if (SelectFileToSaveDialog(0x2E544143,
                                        &_importTacticsFilename,
                                        _aSaveEditedTact) != 0)
                goto cleanup;

            /* Copy selected filename (without extension) as new tactics name */
            StringCopyUntilChar(_selectedFilename,
                                 (uint8_t *)_editTacticsCurrentTactics.name, 0x2E);

            /* Write the tactics file */
            if (WriteTacticsFile(_selectedFilename,
                                  (uint8_t *)&_editTacticsCurrentTactics) != 0)
                goto cleanup;

            CopyCurrentTacticName();
            StringCopy(_selectedFilename, &_importTacticsFilename);
            ApplyCurrentEditedTactics();
            ShowInsertDisk2Dialog(_aSaveEditedTact);
            SetExitMenuFlag();
            return;

cleanup:
            ShowInsertDisk2Dialog(_aSaveEditedTact);
            return;
        }
    }
    SetExitMenuFlag();
}

/* ================================================================
 * EditTacticsOnSelect28  (SWOS2.s line 87906)
 *
 * Menu callback: rotates players left for the current team
 * (maxSubs=5), then redraws entries 10, 27, and terminates.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsOnSelect28(void)
{
    RotatePlayersLeft((TeamFileEx *)_chooseTacticsTeamPtr, 5);
    DrawMenuItemsWaitForVSync(_editTacticsRotateRedrawList);
}

/* ================================================================
 * EditTacticsAfterDrawPlayerFace  (SWOS2.s line 87919)
 *
 * After-draw callback for player face entries. Computes the
 * player index from the entry ordinal (faces start at ordinal 29),
 * applies row highlighting, then draws the info panel entry (39).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsAfterDrawPlayerFace(MenuEntry *cur)
{
    uint16_t playerIdx = cur->ordinal - 29 + 1;
    EditTactics_ApplyPlayerRowHighlighting((int16_t)playerIdx, cur);
    DrawMenuItem(&_menuEntries[39]);
}

/* ================================================================
 * EditTactics28AfterDraw  (SWOS2.s line 87929)
 *
 * After-draw callback for entry 28 area. Draws menu entry at
 * index 27 (offset 0x5E8 / 56 = 27).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics28AfterDraw(void)
{
    DrawMenuItem(&_menuEntries[27]);
}

/* ================================================================
 * EditTactics2AfterDraw  (SWOS2.s line 87901)
 *
 * After-draw callback for the edit tactics pitch area.
 * Draws the pitch background, player sprites, cursor, etc.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics2AfterDraw(void)
{
    DrawEditTacticsPitchAndPlayers();
}

/* ================================================================
 * Chunk 85 — Edit Tactics Pitch Coordinate + Drawing Helpers
 * ================================================================ */

/* ================================================================
 * GetPlayerQuadrant  (SWOS2.s line 88977)
 *
 * Extracts X and Y quadrant from the current tactics for a given
 * player at a given ball location. The byte at
 * editTacticsCurrentTactics[9 + (player-1)*35 + ballLoc] encodes
 * the position: high nibble = X quadrant, low nibble = Y quadrant.
 * ================================================================ */
__attribute__((used, externally_visible))
void GetPlayerQuadrant(uint16_t player, uint16_t ballLoc,
                        int16_t *outXQuad, int16_t *outYQuad)
{
    uint8_t *positions = (uint8_t *)_editTacticsCurrentTactics.positions;
    uint16_t offset = (player - 1) * 35 + ballLoc;
    uint16_t val = positions[offset];
    *outXQuad = (int16_t)(val >> 4);
    *outYQuad = (int16_t)(val & 0x0F);
}

/* ================================================================
 * SetPlayerQuadrant  (SWOS2.s line 88992)
 *
 * Sets the position byte for a player at a given ball location.
 * Encodes xQuad in high nibble and yQuad in low nibble.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetPlayerQuadrant(uint16_t player, int16_t xQuad, int16_t yQuad,
                        uint16_t ballLoc)
{
    _tacticsChangedFlag = 1;
    uint8_t *positions = (uint8_t *)_editTacticsCurrentTactics.positions;
    uint16_t offset = (player - 1) * 35 + ballLoc;
    positions[offset] = (uint8_t)((xQuad << 4) | yQuad);
}

/* ================================================================
 * GetBallQuadrant  (SWOS2.s line 89034)
 *
 * Converts a ball location index (0..34) to column and row.
 * row = ballLoc / 5, col = ballLoc % 5.
 * ================================================================ */
__attribute__((used, externally_visible))
void GetBallQuadrant(uint16_t ballLoc, int16_t *outCol, int16_t *outRow)
{
    uint32_t loc = (uint32_t)ballLoc;  /* ext.l d0 */
    *outRow = (int16_t)(loc / 5);
    *outCol = (int16_t)(loc % 5);
}

/* ================================================================
 * GetBallXYPositionFromQuadrant  (SWOS2.s line 89012)
 *
 * Converts ball row/column to screen X/Y coordinates on the pitch
 * sprite using lookup tables.
 * ================================================================ */
__attribute__((used, externally_visible))
void GetBallXYPositionFromQuadrant(int16_t row, int16_t col,
                                     int16_t *outX, int16_t *outY)
{
    *outX = 129 - _ballXQuadrantCoordinates[row] + 22;
    *outY = 199 - _ballYQuadrantCoordinates[col] + 36;
}

/* ================================================================
 * GetPlayerXYPositionFromQuadrant  (SWOS2.s line 88941)
 *
 * Gets the screen X/Y position of a player on the pitch sprite.
 * If the player's quadrant is (15,15), returns the ball position
 * instead (with Y-3 offset).
 * ================================================================ */
__attribute__((used, externally_visible))
void GetPlayerXYPositionFromQuadrant(uint16_t player, uint16_t ballLoc,
                                       int16_t *outX, int16_t *outY)
{
    int16_t xQuad, yQuad;
    GetPlayerQuadrant(player, ballLoc, &xQuad, &yQuad);

    if (xQuad == 0x0F && yQuad == 0x0F) {
        /* Player is "on the ball" — return ball position */
        int16_t bCol, bRow;
        GetBallQuadrant(ballLoc, &bCol, &bRow);
        GetBallXYPositionFromQuadrant(bCol, bRow, outX, outY);
        *outY -= 3;
        return;
    }

    *outX = 0x81 - _xQuadrantToCoordinates[xQuad] + 0x16;
    *outY = 0xC7 - _yQuadrantToCoordinates[yQuad] + 0x24;
}

/* ================================================================
 * EditTactics_GetDirectionDelta  (SWOS2.s line 89175)
 *
 * Reads the current direction input (menuControlsDirection2) and
 * converts it to X/Y delta values using a lookup table.
 * Returns (0,0) if no direction is held.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_GetDirectionDelta(int16_t *outDx, int16_t *outDy)
{
    *outDx = 0;
    *outDy = 0;
    int16_t dir = (int16_t)_menuControlsDirection2;
    if (dir < 0)
        return;
    uint8_t *table = _editTacticsDirectionDeltaTable;
    uint16_t idx = (uint16_t)dir * 2;
    *outDx = -((int8_t)table[idx]);
    *outDy = -((int8_t)table[idx + 1]);
}

/* ================================================================
 * GetEditTacticsItemXYPosition  (SWOS2.s line 88749)
 *
 * Dispatcher for getting the X/Y position of a draw-list item.
 * idx < 0: returns old ball position (editTacticsBallRow/Column)
 * idx == 0: returns current ball position (ballRow/Column)
 * idx > 0: returns player position for the current ball location
 *           (or editTacticsPlayerX during status 6)
 * ================================================================ */
__attribute__((used, externally_visible))
void GetEditTacticsItemXYPosition(int16_t idx, int16_t *outX, int16_t *outY)
{
    if (idx < 0) {
        GetBallXYPositionFromQuadrant((int16_t)_editTacticsBallRow,
                                        (int16_t)_editTacticsBallColumn,
                                        outX, outY);
        return;
    }
    if (idx == 0) {
        GetBallXYPositionFromQuadrant((int16_t)_ballRow,
                                        (int16_t)_ballColumn,
                                        outX, outY);
        return;
    }
    /* idx > 0: player */
    uint16_t bl = _editTacticsBallLocation;
    if (_editTacticsPitchStatus == 6)
        bl = _editTacticsPlayerX;
    GetPlayerXYPositionFromQuadrant((uint16_t)idx, bl, outX, outY);
}

/* ================================================================
 * EditTactics_TryMoveQuadrantCursor  (SWOS2.s line 88557)
 *
 * Attempts to move the cursor by the current direction delta.
 * Clamps row to [0..4] and column to [0..6].
 * Returns 0 if cursor was moved, nonzero if it stayed.
 * Updates outRow, outCol, outBallLoc with the new position.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t EditTactics_TryMoveQuadrantCursor(int16_t row, int16_t col,
                                             int16_t *outRow, int16_t *outCol,
                                             int16_t *outBallLoc)
{
    int16_t dx, dy;
    EditTactics_GetDirectionDelta(&dx, &dy);

    uint16_t noMove = 1;
    int16_t newRow = row + dx;
    if (newRow >= 0 && newRow < 5) {
        row = newRow;
        noMove = 0;
    }
    int16_t newCol = col + dy;
    if (newCol >= 0 && newCol < 7) {
        col = newCol;
        noMove = 0;
    }
    *outRow = row;
    *outCol = col;
    *outBallLoc = col * 5 + row;
    return noMove;
}

/* ================================================================
 * EditTactics_SetAllPlayersQuadrantForBallLoc  (SWOS2.s line 89834)
 *
 * For each of the 10 outfield players (1..10), copies their
 * quadrant position from ball location `ballLoc` to area `areaIdx`.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_SetAllPlayersQuadrantForBallLoc(uint16_t ballLoc,
                                                   uint16_t areaIdx)
{
    for (uint16_t p = 1; p <= 10; p++) {
        int16_t xq, yq;
        GetPlayerQuadrant(p, ballLoc, &xq, &yq);
        SetPlayerQuadrant(p, xq, yq, areaIdx);
    }
}

/* ================================================================
 * EditTactics_FlipMaybe_MirrorPlayersInArea  (SWOS2.s line 89776)
 *
 * If flipOnOff is active and the area is not in the center column
 * (remainder != 2), mirrors all player X positions in the given
 * area (flips high nibble: 0xEF - highNibble), then swaps paired
 * players using the pairIds table.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_FlipMaybe_MirrorPlayersInArea(uint16_t areaIdx)
{
    if (_flipOnOff == 0)
        return;

    /* Check if area is in center column (remainder == 2 from /5) */
    uint16_t rem = areaIdx % 5;
    if (rem == 2)
        return;

    uint8_t *positions = (uint8_t *)_editTacticsCurrentTactics.positions;
    uint8_t *pairIds = _editTacticsCurrentTactics.pairIds;

    /* Mirror X positions for 10 outfield players */
    uint8_t *p = positions;
    for (int16_t i = 9; i >= 0; i--) {
        uint8_t val = p[areaIdx];
        uint8_t mirrored = (uint8_t)(0xEF - val);
        mirrored &= 0xF0;
        mirrored |= (val & 0x0F);
        p[areaIdx] = mirrored;
        p += 0x23; /* next player: stride = 35 */
    }

    /* Swap paired players using the pair-id table */
    for (int16_t pairVal = 4; pairVal >= 0; pairVal--) {
        for (int16_t j = 9; j >= 0; j--) {
            if (pairIds[j] == (uint8_t)pairVal) {
                /* Found first of pair at index j — find second */
                int16_t k = j - 1;
                while (k >= 0) {
                    if (pairIds[k] == (uint8_t)pairVal)
                        break;
                    k--;
                }
                if (k < 0) {
                    _panicReasonCode = 0x1387;
                    PanicHaltLoop();
                }
                /* Swap positions between player j and k at this area */
                uint16_t offJ = (uint16_t)j * 0x23 + areaIdx;
                uint16_t offK = (uint16_t)k * 0x23 + areaIdx;
                uint8_t tmp = positions[offJ];
                positions[offJ] = positions[offK];
                positions[offK] = tmp;
                break; /* move to next pairVal */
            }
        }
    }
}

/* ================================================================
 * EditTactics_FlipMaybe_MirrorBallArea  (SWOS2.s line 89753)
 *
 * If flipOnOff is active and the ball location is not in the center
 * column, computes the mirrored ball location and copies all player
 * positions there, then mirrors them.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_FlipMaybe_MirrorBallArea(uint16_t ballLoc)
{
    if (_flipOnOff == 0)
        return;

    uint16_t loc = ballLoc;
    uint16_t row = loc / 5;
    uint16_t col = loc % 5;
    if (col == 2)
        return;

    /* Mirror: newArea = row*5 + (4 - col) */
    uint16_t mirroredArea = row * 5 + (4 - col);
    EditTactics_SetAllPlayersQuadrantForBallLoc(ballLoc, mirroredArea);
    EditTactics_FlipMaybe_MirrorPlayersInArea(mirroredArea);
}

/* ================================================================
 * EditTactics_CommitDraggedPlayerPosition  (SWOS2.s line 89213)
 *
 * Commits the player position after dragging. Copies all player
 * positions from editTacticsPlayerX to the ball location area,
 * then optionally mirrors if the player and ball are on opposite
 * sides of center.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_CommitDraggedPlayerPosition(void)
{
    _tacticsChangedFlag = 1;
    uint16_t playerX = _editTacticsPlayerX;
    uint16_t ballLoc = _editTacticsBallLocation;
    EditTactics_SetAllPlayersQuadrantForBallLoc(playerX, ballLoc);

    /* Check if mirroring is needed based on column positions */
    uint16_t playerCol = playerX % 5;
    if (playerCol == 2)
        return; /* center column — no mirroring */

    uint16_t ballCol = ballLoc % 5;
    if (ballCol == 2)
        return; /* center column — no mirroring */

    /* Mirror if player and ball are on opposite sides */
    if (ballCol >= 3) {
        /* Ball on right side */
        if (playerCol >= 3)
            goto doMirrorBall; /* both right: mirror ball area */
        /* player left, ball right: mirror players in ball area first */
    } else {
        /* Ball on left side */
        if (playerCol < 3)
            goto doMirrorBall; /* both left: mirror ball area */
        /* player right, ball left: mirror players in ball area first */
    }
    /* Opposite sides: mirror players in ball area */
    EditTactics_FlipMaybe_MirrorPlayersInArea(ballLoc);

doMirrorBall:
    EditTactics_FlipMaybe_MirrorBallArea(ballLoc);
}

/* ================================================================
 * SortEditTacticsDrawOrderByY  (SWOS2.s line 88724)
 *
 * Bubble-sorts the editTacticsPlayerPositions array (12 entries)
 * by Y coordinate so items are drawn back-to-front.
 * ================================================================ */
__attribute__((used, externally_visible))
void SortEditTacticsDrawOrderByY(void)
{
    int16_t *arr = _editTacticsPlayerPositions;
    int16_t swapped;
    do {
        swapped = 0;
        for (int16_t i = 10; i >= 0; i--) {
            int16_t x1, y1, x2, y2;
            GetEditTacticsItemXYPosition(arr[0], &x1, &y1);
            GetEditTacticsItemXYPosition(arr[1], &x2, &y2);
            if (y2 < y1) {
                int16_t tmp = arr[0];
                arr[0] = arr[1];
                arr[1] = tmp;
                swapped = 1;
            }
            arr++;
        }
        arr = _editTacticsPlayerPositions;
    } while (swapped);
}

/* ================================================================
 * SortPlayerAreasByY  (SWOS2.s line 88822)
 *
 * Bubble-sorts the playerAreas array (35 entries) by Y coordinate
 * of the marked player at each area's ball location.
 * ================================================================ */
__attribute__((used, externally_visible))
void SortPlayerAreasByY(void)
{
    int16_t *arr = _playerAreas;
    int16_t swapped;
    do {
        swapped = 0;
        for (int16_t i = 33; i >= 0; i--) {
            int16_t x1, y1, x2, y2;
            GetPlayerXYPositionFromQuadrant(_markedPlayerNumber, (uint16_t)arr[0],
                                              &x1, &y1);
            GetPlayerXYPositionFromQuadrant(_markedPlayerNumber, (uint16_t)arr[1],
                                              &x2, &y2);
            if (y2 < y1) {
                int16_t tmp = arr[0];
                arr[0] = arr[1];
                arr[1] = tmp;
                swapped = 1;
            }
            arr++;
        }
        arr = _playerAreas;
    } while (swapped);
}

/* ================================================================
 * EditTacticsDrawCursor  (SWOS2.s line 88903)
 *
 * Draws the animated cursor on the pitch sprite. If a player is
 * selected (markedPlayerNumber != 0), cursor is at the player's
 * position; otherwise at the ball position.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsDrawCursor(void)
{
    if (_editTacticsPitchStatus != 2)
        return;

    int16_t x, y;
    if (_markedPlayerNumber != 0) {
        GetPlayerXYPositionFromQuadrant(_markedPlayerNumber,
                                          _editTacticsBallLocation,
                                          &x, &y);
        y += 1;
    } else {
        GetBallXYPositionFromQuadrant((int16_t)_ballRow, (int16_t)_ballColumn,
                                        &x, &y);
        y -= 1;
    }

    /* Animate cursor: 4 frames, cycling every 4 ticks */
    uint16_t frame = (_currentGameTick >> 2) & 3;
    uint16_t spriteId = _tacticCursorFrames[frame];
    DrawSpriteCentered(spriteId, x, y);
}

/* ================================================================
 * DrawPlayerDirections  (SWOS2.s line 88854)
 *
 * Draws direction arrows showing player movement between the
 * current and previous ball locations (only when status == 3,
 * i.e., fire is held and ball locations differ).
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawPlayerDirections(void)
{
    if (_editTacticsPitchStatus != 3)
        return;

    if (_editTacticsBallLocation == _editTacticsLastBallLocation)
        return;

    for (uint16_t p = 1; p <= 10; p++) {
        /* Get position at current ball location */
        int16_t curX, curY;
        GetPlayerXYPositionFromQuadrant(p, _editTacticsBallLocation,
                                          &curX, &curY);
        /* Get position at previous ball location */
        int16_t prevX, prevY;
        GetPlayerXYPositionFromQuadrant(p, _editTacticsLastBallLocation,
                                          &prevX, &prevY);

        /* Calculate direction from prev to cur */
        int32_t outDeltaX, outDeltaY;
        uint16_t dir = CalculateDeltaXAndY(
            curX, curY, prevX, prevY, 0,
            &outDeltaX, &outDeltaY);
        if (dir == 0xFFFF)
            continue; /* same position, no arrow */

        /* Convert direction to arrow sprite index */
        uint8_t dirByte = (uint8_t)((dir + 16) & 0xFF);
        uint16_t arrowIdx = dirByte >> 5;

        /* Apply arrow offset */
        int16_t drawX = curX + (int8_t)_arrowSpritesOffsets[arrowIdx * 2];
        int16_t drawY = curY + (int8_t)_arrowSpritesOffsets[arrowIdx * 2 + 1];

        uint16_t spriteId = 173 + arrowIdx;
        DrawSpriteCentered(spriteId, drawX, drawY);
    }
}

/* ================================================================
 * InitLittlePlayerSprites  (SWOS2.s line 88219)
 *
 * Initializes the edit tactics pitch display: clears state, copies
 * tactics, sets up player dress colors, and copies/colorizes the
 * little player and goalie sprites.
 * ================================================================ */
#define SHIRT_ORDINARY_VAL          0

__attribute__((used, externally_visible))
void InitLittlePlayerSprites(void)
{
    _editTacticsTransitionActive = 0;
    _tacticsChangedFlag = 0;
    _markedPlayerNumber = 0;
    _ballRow = 2;
    _ballColumn = 3;
    _editTacticsBallLocation = 17;

    CopyCurrentTactics();
    SaveCurrentTactics();

    /* Set up player numbers pointer */
    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    uint8_t *playerNums = teamEx->team.playerNumbers;
    if (teamEx == &_careerTeam) {
        playerNums = _currentMatchPlayers;
    }
    _editTacticsPlayerNumbers = (uintptr_t)playerNums;

    /* Convert dress colors into the little players color table */
    uint16_t stripesColor = ConvertPlayerDressColor(
        (uint16_t)teamEx->team.prStripesColor);
    _littlePlayersConvertTable[11] = stripesColor;   /* offset 0x16 */

    uint16_t basicColor = ConvertPlayerDressColor(
        (uint16_t)teamEx->team.prBasicColor);
    if (teamEx->team.prShirtType == SHIRT_ORDINARY_VAL) {
        basicColor = stripesColor;
    }
    _littlePlayersConvertTable[10] = basicColor;     /* offset 0x14 */

    uint16_t shortsColor = ConvertPlayerDressColor(
        (uint16_t)teamEx->team.prShortsColor);
    _littlePlayersConvertTable[14] = shortsColor;    /* offset 0x1C */

    uint16_t socksColor = ConvertPlayerDressColor(
        (uint16_t)teamEx->team.prSocksColor);
    _littlePlayersConvertTable[15] = socksColor;     /* offset 0x1E */

    /* Copy and colorize outfield player sprites (3 skin tones) */
    uint16_t shirtType = (uint16_t)(int8_t)teamEx->team.prShirtType;
    uint16_t srcBase = shirtType * 3 + 130;

    CopyWholeSprite(srcBase, 124);
    CopyWholeSprite(srcBase + 1, 125);
    CopyWholeSprite(srcBase + 2, 126);

    ConvertSpriteColors(124, _littlePlayersConvertTable);
    ConvertSpriteColors(125, _littlePlayersConvertTable);
    ConvertSpriteColors(126, _littlePlayersConvertTable);

    /* Copy and colorize goalkeeper sprites (3 skin tones) */
    CopyWholeSprite(142, 127);
    CopyWholeSprite(143, 128);
    CopyWholeSprite(144, 129);

    ConvertSpriteColors(127, _littlePlayersConvertTable);
    ConvertSpriteColors(128, _littlePlayersConvertTable);
    ConvertSpriteColors(129, _littlePlayersConvertTable);
}

/* ================================================================
 * DrawLittlePlayersAndBall  (SWOS2.s line 88613)
 *
 * Draws all little players and the ball on the edit tactics pitch.
 * Iterates through editTacticsPlayerPositions (12 entries: -1 for
 * ghost ball, 0 for real ball, 1-10 for players). Handles blinking
 * for selected items based on editTacticsPitchStatus.
 * ================================================================ */
#define SPR_BALL_SMALL              123
#define SPR_SMALL_PL_WHITE          124  /* SPR_EDIT_TACTICS_SMALL_PL_WHITE_GUY */
#define SPR_SMALL_GK_WHITE          127  /* goalie version */
#define SPR_SHIRT_NUMBER_BASE       161  /* SPR_NOT_PLAYING_WHOLE_SEASON */
__attribute__((used, externally_visible))
void DrawLittlePlayersAndBall(void)
{
    if (_editTacticsDisableLittlePlayersAndBallDraw != 0)
        return;

    SortEditTacticsDrawOrderByY();

    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    int16_t *posArr = _editTacticsPlayerPositions;
    uint8_t *playerNums = (uint8_t *)_editTacticsPlayerNumbers;

    for (int16_t iter = 11; iter >= 0; iter--) {
        int16_t idx = *posArr++;

        if (idx < 0) {
            /* Ghost ball (idx == -1): only in status 3 */
            if (_editTacticsPitchStatus != 3)
                goto nextIter;
            if (_editTacticsBallLocation == _editTacticsBallRow)
                goto nextIter;
            /* Blink: skip if tick & 2 == 0 */
            if ((_currentGameTick & 2) == 0)
                goto nextIter;
            int16_t gx, gy;
            GetBallXYPositionFromQuadrant((int16_t)_editTacticsBallRow,
                                           (int16_t)_editTacticsBallColumn, &gx, &gy);
            DrawSpriteCentered(SPR_BALL_SMALL, gx, gy);
            goto nextIter;
        }

        if (idx == 0) {
            /* Real ball */
            if (_editTacticsPitchStatus == 6 || _editTacticsPitchStatus == 4) {
                if (_markedPlayerNumber == 0) {
                    /* Ball is selected -> blink */
                    if ((_currentGameTick & 4) == 0)
                        goto nextIter;
                }
            }
            int16_t bx, by;
            GetBallXYPositionFromQuadrant((int16_t)_ballRow,
                                           (int16_t)_ballColumn, &bx, &by);
            DrawSpriteCentered(SPR_BALL_SMALL, bx, by);
            goto nextIter;
        }

        /* Player (idx = 1..10) */
        int8_t playerNum = (int8_t)playerNums[idx];

        /* In status 5, only draw the marked player */
        if (_editTacticsPitchStatus == 5) {
            if ((uint16_t)idx != _markedPlayerNumber)
                goto nextIter;
        }

        /* Get player position */
        uint16_t ballLoc = _editTacticsBallLocation;
        if (_editTacticsPitchStatus == 6)
            ballLoc = _editTacticsPlayerX;

        int16_t px, py;
        GetPlayerXYPositionFromQuadrant((uint16_t)idx, ballLoc, &px, &py);

        /* Determine skin color sprite */
        uint8_t posAndFace = teamEx->players[playerNum].positionAndFace;
        uint16_t skinIdx = (posAndFace & PF_FACE_MASK) >> 3;   /* 0..3 */
        uint16_t playerSprite = SPR_SMALL_PL_WHITE + skinIdx;
        DrawSpriteCentered(playerSprite, px, py);

        /* Check if we should skip shirt number (blinking) */
        if (_editTacticsPitchStatus == 4 && (uint16_t)idx == _markedPlayerNumber) {
            if ((_currentGameTick & 4) == 0)
                goto nextIter;
        }

        /* Draw shirt number above player */
        int8_t shirtNum = teamEx->players[playerNum].shirtNumber;
        int16_t numSprite = ShirtNumber_MenuSprite(shirtNum);  /* (1..99: shirt_numbers.c) */
        if (numSprite >= 0)
            DrawSpriteCentered((uint16_t)numSprite, px, py - 9);

nextIter:
        ;
    }

    /* Draw goalkeeper sprite at fixed position (bottom of pitch) */
    int8_t gkNum = (int8_t)playerNums[0];
    uint8_t gkPosAndFace = teamEx->players[gkNum].positionAndFace;
    uint16_t gkSkinIdx = (gkPosAndFace & PF_FACE_MASK) >> 3;
    uint16_t gkSprite = SPR_SMALL_GK_WHITE + gkSkinIdx;
    DrawSpriteCentered(gkSprite, 87, 234);
}

/* ================================================================
 * DrawEditTacticsHighlightedPlayerRoute  (SWOS2.s line 88755)
 *
 * When a player is being dragged across ball areas (disable flag
 * is set), draws that player's sprite at every ball area position.
 * Shows the marked player's position for all 35 ball locations.
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawEditTacticsHighlightedPlayerRoute(void)
{
    /* Only draw when little players are disabled (route view active) */
    if (_editTacticsDisableLittlePlayersAndBallDraw == 0)
        return;
    if (_markedPlayerNumber == 0)
        return;

    SortPlayerAreasByY();

    int16_t *areas = _playerAreas;
    uint8_t *playerNums = (uint8_t *)_editTacticsPlayerNumbers;
    TeamFileEx *teamEx = _chooseTacticsTeamPtr;

    /* Get marked player's info */
    uint16_t markedIdx = _markedPlayerNumber;
    int8_t markedNum = (int8_t)playerNums[markedIdx];

    /* Determine skin color sprite for the player */
    uint8_t posAndFace = teamEx->players[markedNum].positionAndFace;
    uint16_t skinIdx = (posAndFace & PF_FACE_MASK) >> 3;
    uint16_t playerSprite = SPR_SMALL_PL_WHITE + skinIdx;

    /* Determine shirt number sprite */
    int8_t shirtNum = teamEx->players[markedNum].shirtNumber;
    int16_t numSprite = ShirtNumber_MenuSprite(shirtNum);  /* (1..99: shirt_numbers.c) */

    /* Loop over all 35 ball areas */
    for (int16_t i = 34; i >= 0; i--) {
        uint16_t areaIdx = (uint16_t)*areas++;

        /* Get player position for this area */
        int16_t px, py;
        GetPlayerXYPositionFromQuadrant(markedIdx, areaIdx, &px, &py);

        /* Draw player sprite */
        DrawSpriteCentered(playerSprite, px, py);

        /* Check if we should skip the number (blinking in status 4) */
        if (_editTacticsPitchStatus == 4 &&
            areaIdx == _editTacticsBallLocation) {
            if ((_currentGameTick & 4) == 0)
                continue;
        }

        /* Draw shirt number above player */
        if (numSprite >= 0)
            DrawSpriteCentered((uint16_t)numSprite, px, py - 9);
    }
}

/* ================================================================
 * DrawEditTacticsPitchAndPlayers  (SWOS2.s line 88495)
 *
 * Draws the edit tactics pitch background, then overlays direction
 * arrows, cursor, little players/ball, and highlighted player route.
 * ================================================================ */
#define SPR_TACTICS_PITCH   226

__attribute__((used, externally_visible))
void DrawEditTacticsPitchAndPlayers(void)
{
    /* Draw pitch background strips */
    DrawMenuItemBackgroundMasked(21, 25, 134, 11);
    DrawMenuItemBackgroundMasked(21, 25, 1, 211);
    DrawMenuItemBackgroundMasked(152, 25, 3, 211);

    /* Draw pitch sprite */
    DrawSprite16Pixels(SPR_TACTICS_PITCH, 22, 36);

    /* Draw overlays */
    DrawPlayerDirections();
    EditTacticsDrawCursor();
    DrawLittlePlayersAndBall();
    DrawEditTacticsHighlightedPlayerRoute();
}

/* ================================================================
 * HandlePlayersOrBallInEditTactics  (SWOS2.s line 88692)
 *
 * Finds the nearest player or ball in the current joystick direction
 * and selects it (sets markedPlayerNumber). Uses expanding cone
 * search: starts with width 384, expands by 64 up to 1280, then
 * by 192 up to 7680.
 * ================================================================ */
__attribute__((used, externally_visible))
void HandlePlayersOrBallInEditTactics(void)
{
    if ((int16_t)_menuControlsDirection < 0)
        return;

    /* Get current selection's position */
    int16_t selX, selY;
    if (_markedPlayerNumber != 0) {
        GetPlayerXYPositionFromQuadrant(_markedPlayerNumber,
                                          _editTacticsBallLocation, &selX, &selY);
    } else {
        GetBallXYPositionFromQuadrant((int16_t)_ballRow,
                                        (int16_t)_ballColumn, &selX, &selY);
    }

    int16_t bestIdx = -1;
    int32_t bestDist = -1;  /* 0xFFFFFFFF unsigned, treated as max */
    _editTacticsNearestSelectPitchWidth = 384;

outerLoop:
    for (uint16_t i = 0; i < 11; i++) {
        if (i == _markedPlayerNumber)
            continue;

        int16_t px, py;
        if (i == 0) {
            GetBallXYPositionFromQuadrant((int16_t)_ballRow,
                                            (int16_t)_ballColumn, &px, &py);
        } else {
            GetPlayerXYPositionFromQuadrant(i, _editTacticsBallLocation,
                                              &px, &py);
        }

        int16_t dx = px - selX;
        int16_t dy = py - selY;

        /* Direction-based cone filter */
        uint16_t dirBits = _menuControlsDirection & 6;
        int16_t width = (int16_t)_editTacticsNearestSelectPitchWidth;

        if (dirBits == 0) {
            /* Up */
            if (dy >= 0) continue;
            int16_t limit = (int16_t)((int32_t)dy * (int32_t)width >> 8);
            if (dx < limit) continue;
            if (dx > -limit) continue;
        } else if (dirBits == 2) {
            /* Right */
            if (dx < 0) continue;
            int16_t limit = (int16_t)((int32_t)dx * (int32_t)width >> 8);
            if (dy > limit) continue;
            if (dy < -limit) continue;
        } else if (dirBits == 4) {
            /* Down */
            if (dy < 0) continue;
            int16_t limit = (int16_t)((int32_t)dy * (int32_t)width >> 8);
            if (dx > limit) continue;
            if (dx < -limit) continue;
        } else {
            /* Left (dirBits == 6) */
            if (dx >= 0) continue;
            int16_t limit = (int16_t)((int32_t)dx * (int32_t)width >> 8);
            if (dy < limit) continue;
            if (dy > -limit) continue;
        }

        /* Calculate squared distance */
        int32_t dist = (int32_t)dx * (int32_t)dx + (int32_t)dy * (int32_t)dy;
        if ((uint32_t)dist >= (uint32_t)bestDist)
            continue;
        bestDist = dist;
        bestIdx = (int16_t)i;
    }

    if (bestIdx < 0) {
        /* Widen cone and retry */
        _editTacticsNearestSelectPitchWidth += 64;
        if (_editTacticsNearestSelectPitchWidth < 1280)
            goto outerLoop;
        _editTacticsNearestSelectPitchWidth += 192;
        if (_editTacticsNearestSelectPitchWidth < 7680)
            goto outerLoop;

        /* Still nothing found: exit pitch editing if pressing RIGHT or DOWN
         * (menu items are below/right of pitch). UP/LEFT stay in pitch. */
        uint16_t dirBits = _menuControlsDirection & 6;
        if (dirBits >= 2 && dirBits <= 4) {
            _editTacticsPitchStatus = 0xFFFF;
            return;
        }
        return;
    }

    _markedPlayerNumber = (uint16_t)bestIdx;
}

/* ================================================================
 * EditTacticsReadControls  (SWOS2.s line 88386)
 *
 * Handles joystick/button input for the edit tactics pitch when
 * no specific player is marked (ball/area cursor movement).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsReadControls(void)
{
    if (_editTacticsPitchStatus == 6) {
        /* Status 6: dragging player - fire releases */
        if (_fire != 0) {
            SaveCurrentTactics();
            EditTactics_CommitDraggedPlayerPosition();
            _editTacticsPitchStatus = 0xFFFF;
            return;
        }
        goto moveBallCursor;
    }

    if (_editTacticsPitchStatus == 3) {
        /* Status 3: browsing ball areas */
        if (_fire != 0) goto tryMoveBallArea;
        /* Fire released: commit ball position */
        _ballRow = _editTacticsBallRow;
        _ballColumn = _editTacticsBallColumn;
        _editTacticsBallLocation = _editTacticsLastBallLocation;
        goto setStatus2;
    }

    if (_editTacticsPitchStatus == 4) {
        /* Status 4: player/ball selected, fire handling */
        if (_fire != 0) {
            if (_fireCounter <= -7) goto startBallAreaBrowse;
            goto moveBallCursor;
        }
        if (_fireCounter == 0) goto moveBallCursor;
        if (_fireCounter <= 7) goto setStatus2;
        goto moveBallCursor;
    }

    /* Default status (2): idle, waiting for input */
    if (_fire == 0) {
        if (_fireCounter == 0) {
            HandlePlayersOrBallInEditTactics();
            return;
        }
        if (_fireCounter <= 7) goto setStatus4;
        return;
    }
    /* fire != 0 */
    if (_fireCounter <= -7) goto startBallAreaBrowse;
    return;

setStatus4:
    _editTacticsPitchStatus = 4;
    return;

startBallAreaBrowse:
    _editTacticsBallRow = _ballRow;
    _editTacticsBallColumn = _ballColumn;
    _editTacticsLastBallLocation = _editTacticsBallLocation;
    _editTacticsPitchStatus = 3;
    return;

setStatus2:
    _editTacticsPitchStatus = 2;
    return;

tryMoveBallArea:
    if ((int16_t)_menuControlsDirection < 0)
        return;
    {
        int16_t outRow, outCol, outBallLoc;
        uint16_t noMove = EditTactics_TryMoveQuadrantCursor(
            (int16_t)_editTacticsBallRow, (int16_t)_editTacticsBallColumn,
            &outRow, &outCol, &outBallLoc);
        if (noMove != 0) return;
        _editTacticsBallRow = (uint16_t)outRow;
        _editTacticsBallColumn = (uint16_t)outCol;
        _editTacticsLastBallLocation = (uint16_t)outBallLoc;
    }
    return;

moveBallCursor:
    if ((int16_t)_menuControlsDirection < 0)
        return;
    {
        int16_t outRow, outCol, outBallLoc;
        uint16_t noMove = EditTactics_TryMoveQuadrantCursor(
            (int16_t)_ballRow, (int16_t)_ballColumn,
            &outRow, &outCol, &outBallLoc);
        if (noMove != 0) return;
        _ballRow = (uint16_t)outRow;
        _ballColumn = (uint16_t)outCol;
        _editTacticsBallLocation = (uint16_t)outBallLoc;
    }
}

/* ================================================================
 * EditTactics_HandlePitchInput  (SWOS2.s line 88234)
 *
 * Handles joystick/button input when a player IS marked on the
 * edit tactics pitch. Manages player position movement, ball area
 * switching (status 5), and position commit.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_HandlePitchInput(void)
{
    if (_editTacticsPitchStatus == 4) goto status4;
    if (_editTacticsPitchStatus == 5) goto status5;

    /* Default status (2): waiting for fire press/release */
    if (_fire == 0) {
        if (_fireCounter == 0) {
            HandlePlayersOrBallInEditTactics();
            return;
        }
        if (_fireCounter <= 7) goto saveAndSetStatus4;
        return;
    }
    /* fire != 0 */
    if (_fireCounter <= -14) goto startStatus5;
    return;

status5:
    /* Ball area browsing with marked player */
    if (_fire == 0 && _fireCounter != 0) {
        if (_fireCounter <= 7) goto backToStatus2;
    }

    /* Repeat counter for ball area scrolling */
    _editTacticsInputRepeatCounter++;
    _editTacticsInputRepeatCounter &= 7;
    if (_editTacticsInputRepeatCounter != 0)
        return;

    /* Check direction keys for ball area navigation */
    {
        int16_t newLoc;
        if (_up) {
            newLoc = (int16_t)_editTacticsBallLocation + 5;
            if (newLoc >= 35) {
                newLoc -= 34;
                if (newLoc == 5) newLoc = 0;
            }
        } else if (_down) {
            newLoc = (int16_t)_editTacticsBallLocation - 5;
            if (newLoc < 0) {
                newLoc += 34;
                if (newLoc == 29) newLoc = 34;
            }
        } else if (_left) {
            newLoc = (int16_t)_editTacticsBallLocation + 1;
            if (newLoc == 35) newLoc = 0;
        } else if (_right) {
            newLoc = (int16_t)_editTacticsBallLocation - 1;
            if (newLoc < 0) newLoc = 34;
        } else {
            return;
        }

        _editTacticsBallLocation = (uint16_t)newLoc;
        uint32_t divResult = (uint32_t)(uint16_t)newLoc;
        _ballColumn = (uint16_t)(divResult / 5);
        _ballRow = (uint16_t)(divResult % 5);
    }
    return;

status4:
    /* Player selected, moving quadrant position */
    if (_fire == 0) {
        if (_fireCounter == 0) goto movePlayerQuadrant;
        if (_fireCounter <= 7) goto commitAndDone;
        goto movePlayerQuadrant;
    }
    if (_fireCounter <= -14) goto startStatus5;

movePlayerQuadrant:
    if ((int16_t)_menuControlsDirection < 0)
        return;
    {
        int16_t dx, dy;
        EditTactics_GetDirectionDelta(&dx, &dy);

        int16_t xQuad, yQuad;
        GetPlayerQuadrant(_markedPlayerNumber, _editTacticsBallLocation,
                           &xQuad, &yQuad);

        /* Try to move X */
        int16_t newX = xQuad + dx;
        if (newX >= 0 && newX < 15)
            xQuad = newX;

        /* Try to move Y */
        int16_t newY = yQuad + dy;
        if (newY >= 0 && newY < 16)
            yQuad = newY;

        /* Check if position conflicts with another player */
        int16_t targetX = xQuad;
        int16_t targetY = yQuad;
        for (uint16_t p = 1; p < 11; p++) {
            int16_t px, py;
            GetPlayerQuadrant(p, _editTacticsBallLocation, &px, &py);
            if (px == targetX && py == targetY)
                return;  /* Occupied, don't move */
        }

        /* Position is free: commit */
        SetPlayerQuadrant(_markedPlayerNumber, targetX, targetY,
                           _editTacticsBallLocation);
        EditTactics_FlipMaybe_MirrorBallArea(_editTacticsBallLocation);
    }
    return;

saveAndSetStatus4:
    SaveCurrentTactics();
    _editTacticsPitchStatus = 4;
    EditTacticsMenuSavePlayerPositions();
    DrawPlayerNamesAndFaces();
    return;

commitAndDone:
    _editTacticsPitchStatus = 7;
    CallDrawMenuItem(&_menuEntries[2]);
    EditTactics_CommitPlayerPositionsAndRecalcSkill();
    DrawPlayerNamesAndFaces();

backToStatus2:
    _editTacticsPitchStatus = 2;
    return;

startStatus5:
    _editTacticsInputRepeatCounter = 0;
    _editTacticsPitchStatus = 5;
}

/* ================================================================
 * EditTacticsStatus  (SWOS2.s line 88167)
 *
 * Main state dispatcher for the edit tactics pitch. Handles
 * transition timing and delegates to HandlePitchInput (when
 * player marked) or ReadControls (when no player marked).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsStatus(uint16_t status)
{
    if (_editTacticsTransitionActive == 0) {
        _editTacticsPitchStatus = status;
        _markedPlayerNumber = 0;
        _editTacticsLastTick = _currentGameTick;
        _editTacticsTransitionActive = 1;
    }

    /* Advance tick tracking */
    _editTacticsLastTick = _currentGameTick;

    /* Status 0: wait for fire release */
    if (_editTacticsPitchStatus == 0) {
        if (_fire != 0)
            goto drawAndReturn;
        _editTacticsPitchStatus = 2;
    }

    /* Status 1: wait for fire release (copy tactics mode) */
    if (_editTacticsPitchStatus == 1) {
        if (_fire != 0)
            goto drawAndReturn;
        _editTacticsPitchStatus = 6;
    }

    /* Status -1 (0xFFFF): done */
    if (_editTacticsPitchStatus == 0xFFFF) {
        _editTacticsTransitionActive = 0;
        return;
    }

    /* Dispatch based on whether a player is marked */
    if (_markedPlayerNumber != 0) {
        EditTactics_HandlePitchInput();
    } else {
        EditTacticsReadControls();
    }

drawAndReturn:
    DrawEditTacticsPitchAndPlayers();
}

/* ================================================================
 * EditTacicsMovePlayersOrBall  (SWOS2.s line 87723)
 *
 * Menu action handler: initiates player/ball movement mode.
 * Saves positions, draws names, then enters EditTacticsStatus(0).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacicsMovePlayersOrBall(void)
{
    if (_editTacticsTransitionActive == 0) {
        EditTacticsMenuSavePlayerPositions();
        DrawPlayerNamesAndFaces();
    }
    EditTacticsStatus(0);
    if (_editTacticsTransitionActive != 0)
        return;
    CallDrawMenuItem(&_menuEntries[2]);
    SetCurrentEntry(4);
}

/* ================================================================
 * CopyTactics  (SWOS2.s line 87742)
 *
 * Menu action handler: initiates copy/drag mode for copying player
 * positions from one ball area to another. Changes menu entry
 * colors and enters EditTacticsStatus(1).
 * ================================================================ */
#define COLOR_PINK_TO_BROWN     9
#define COLOR_YELLOW_GREEN_BLK  14
#define COLOR_ORANGE_TO_BROWN   4

__attribute__((used, externally_visible))
void CopyTactics(void)
{
    if (_editTacticsTransitionActive == 0) {
        /* Set entry 4's bg color to pink */
        _menuEntries[4].bgColor = (_menuEntries[4].bgColor & 0xFFF0) | COLOR_PINK_TO_BROWN;
        CallDrawMenuItem(&_menuEntries[4]);
        EditTacticsMenuSavePlayerPositions();
        DrawPlayerNamesAndFaces();
        _editTacticsPlayerX = _editTacticsBallLocation;
    }
    EditTacticsStatus(1);
    if (_editTacticsTransitionActive != 0)
        return;
    CallDrawMenuItem(&_menuEntries[2]);
    /* Restore entry 4's bg color to yellow-green */
    _menuEntries[4].bgColor = (_menuEntries[4].bgColor & 0xFFF0) | COLOR_YELLOW_GREEN_BLK;
    SetCurrentEntry(COLOR_ORANGE_TO_BROWN);
    CallDrawMenuItem(&_menuEntries[4]);
    EditTactics_CommitPlayerPositionsAndRecalcSkill();
    DrawPlayerNamesAndFaces();
}

/* ================================================================
 * GetPlayerInfoBreakPosition  (SWOS2.s line 79298)
 *
 * Given a team pointer and a requested slot, remaps the slot via
 * the current formation's position table (for slots 0-10), then
 * calls GetPlayerAtIndex to obtain the player info pointer.
 *
 * Asm returns: a0=player ptr, d0=remapped slot, d1=file index.
 * C version uses output pointers for the extra return values.
 * Asm body kept for asm callers (multi-register return).
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *GetPlayerInfoBreakPosition(TeamFileEx *teamPtr, uint16_t requestedSlot,
                                      uint16_t *outSlot, uint16_t *outPlayerIndex)
{
    uint16_t slot = requestedSlot;
    if (slot < 11) {
        int16_t tacticsIdx = (int8_t)teamPtr->team.tactics;
        TeamTactics *tactics = (TeamTactics *)_teamTacticsTable[tacticsIdx];
        int16_t formIdx = (int8_t)tactics->ballOutOfPlayTactics;
        uint8_t *positions = (uint8_t *)_positionsTable[formIdx];
        slot = (uint8_t)(positions[requestedSlot] - 1);
    }
    uint16_t teamNumber = PACK_BE_U16(teamPtr->team.countryNumber, teamPtr->team.teamOrdinal);
    uint8_t *player = GetPlayerAtIndex(teamNumber, slot);
    *outSlot = slot;
    *outPlayerIndex = _cGetPlayerAtIndexFileIdx;
    return player;
}

/* ================================================================
 * EditTactics_FindFreePairId  (SWOS2.s line 87515)
 *
 * Scans the 10-byte pairIds table in tactics data, builds a bitmask of
 * used pair IDs, and returns the first free pair ID (lowest bit
 * not set).
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t EditTactics_FindFreePairId(void)
{
    uint8_t *table = _editTacticsCurrentTactics.pairIds;
    uint16_t used = 0;
    int16_t i;
    for (i = 9; i >= 0; i--) {
        int8_t val = (int8_t)table[i];
        if (val >= 0) {
            used |= (uint16_t)(1 << (int16_t)val);
        }
    }
    uint16_t id = 0;
    while (used & (1 << id))
        id++;
    return id;
}

/* ================================================================
 * EditTactics_ClearPairId  (SWOS2.s line 87540)
 *
 * Scans the 10-byte pairIds table and sets all entries matching the
 * given pair ID value to 0xFF (unlinked).
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_ClearPairId(uint16_t value)
{
    uint8_t *table = _editTacticsCurrentTactics.pairIds;
    int16_t i;
    for (i = 9; i >= 0; i--) {
        if (table[i] == (uint8_t)value)
            table[i] = 0xFF;
    }
}

/* ================================================================
 * EditTactics_SwapPlayersInSlots  (SWOS2.s line 87610)
 *
 * Swaps two players identified by their slot indices. If one or both
 * slots cross the playing/bench boundary, uses GetPlayerAtIndex to
 * look up the players and swaps their position bytes and shirt
 * numbers. Also swaps entries in the appropriate player number array.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTactics_SwapPlayersInSlots(uint16_t slot1, uint16_t slot2)
{
    TeamFileEx *teamEx = _chooseTacticsTeamPtr;

    /* Check if we need to do the GetPlayerAtIndex-based swap.
     * Skip when both slots are in the same category:
     * - both < 11 (playing) → skip
     * - both >= 11 and both < 16 (bench) → skip
     * Otherwise do the full swap. */
    int doPlayerSwap = 0;
    if (slot1 >= 16 || slot2 >= 16) {
        doPlayerSwap = 1;
    } else if (slot1 < 11 && slot2 >= 11) {
        doPlayerSwap = 1;
    } else if (slot1 >= 11 && slot2 < 11) {
        doPlayerSwap = 1;
    }

    if (doPlayerSwap) {
        uint16_t teamNumber = PACK_BE_U16(teamEx->team.countryNumber, teamEx->team.teamOrdinal);
        uint8_t *player1 = GetPlayerAtIndex(teamNumber, slot1);
        PlayerFile *pf1 = (PlayerFile *)(player1 + 76);
        uint16_t fileIdx1 = _cGetPlayerAtIndexFileIdx;
        uint8_t *player2 = GetPlayerAtIndex(teamNumber, slot2);
        PlayerFile *pf2 = (PlayerFile *)(player2 + 76);
        uint16_t fileIdx2 = _cGetPlayerAtIndexFileIdx;

        /* Swap position bytes in playerPositionsArray */
        uint8_t tmp = (&_playerPositionsArray)[fileIdx2];
        (&_playerPositionsArray)[fileIdx2] = (&_playerPositionsArray)[fileIdx1];
        (&_playerPositionsArray)[fileIdx1] = tmp;

        /* Swap shirt numbers between players */
        int8_t tmp2 = pf2->shirtNumber;
        pf2->shirtNumber = pf1->shirtNumber;
        pf1->shirtNumber = tmp2;
    }

    /* Swap entries in the player number mapping array */
    if (_gameType == 4) {
        uint8_t tmp = _currentMatchPlayers[slot2];
        _currentMatchPlayers[slot2] = _currentMatchPlayers[slot1];
        _currentMatchPlayers[slot1] = tmp;
    } else {
        uint8_t *numbers = teamEx->team.playerNumbers;
        uint8_t tmp = numbers[slot2];
        numbers[slot2] = numbers[slot1];
        numbers[slot1] = tmp;
    }
}

/* ================================================================
 * EditTacticsCommon  (SWOS2.s line 87444)
 *
 * Called when a player pair entry is selected in the edit tactics
 * menu. Handles linking/unlinking player pairs via the pairIds table.
 * First click selects a player; second click on a different player
 * links them as a pair; clicking the same player again cancels.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsCommon(MenuEntry *cur)
{
    if ((int16_t)_editTacticsSelectedPlayerNameIndex >= 0)
        return;

    /* Compute relative index: current entry vs menuEntries[29] */
    uint16_t relIdx = cur->ordinal - _menuEntries[29].ordinal + 1;
    uint16_t savedRelIdx = relIdx;

    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    uint8_t savedTactics = teamEx->team.tactics;
    teamEx->team.tactics = _editTacticsCurrentTactics.ballOutOfPlayTactics;

    uint16_t slot, playerIndex;
    GetPlayerInfoBreakPosition(teamEx, relIdx, &slot, &playerIndex);

    teamEx = _chooseTacticsTeamPtr;
    teamEx->team.tactics = savedTactics;
    slot -= 1;

    if ((int16_t)_editTacticsSelectedPairSlotIndex < 0) {
        /* No previous selection - start new selection */
        _editTacticsSelectedPairSlotIndex = slot;
        MenuEntry *entry = CalcMenuEntryAddress(11 + savedRelIdx);
        _editTacticsSelectedPairMenuEntryPtr = entry;
        CallDrawMenuItem(entry);
        return;
    }

    if (_editTacticsSelectedPairSlotIndex == slot) {
        /* Same slot clicked again - cancel selection */
        _editTacticsSelectedPairSlotIndex = 0xFFFF;
        CallDrawMenuItem(_editTacticsSelectedPairMenuEntryPtr);
        return;
    }

    /* Different slot - link/unlink pair */
    uint8_t *pairIds = _editTacticsCurrentTactics.pairIds;
    uint8_t pairIdCurrent = pairIds[slot];
    uint8_t pairIdPrevious = pairIds[_editTacticsSelectedPairSlotIndex];

    /* Clear both pair IDs from the table */
    EditTactics_ClearPairId((uint16_t)pairIds[slot]);
    EditTactics_ClearPairId((uint16_t)pairIds[_editTacticsSelectedPairSlotIndex]);

    /* Assign new pair ID unless both already had the same valid ID */
    if ((int8_t)pairIdCurrent < 0 || (int8_t)pairIdPrevious < 0 ||
        pairIdCurrent != pairIdPrevious) {
        uint16_t newPairId = EditTactics_FindFreePairId();
        pairIds[slot] = (uint8_t)newPairId;
        pairIds[_editTacticsSelectedPairSlotIndex] = (uint8_t)newPairId;
    }

    _editTacticsSelectedPairSlotIndex = 0xFFFF;

    /* Redraw the selected pair entry and all pair entries */
    CallDrawMenuItem(_editTacticsSelectedPairMenuEntryPtr);
    CallDrawMultipleItems(10, &_menuEntries[29]);
}

/* ================================================================
 * EditTacticsPlayerNamesCommon  (SWOS2.s line 87555)
 *
 * Called when a player name is selected in the edit tactics menu.
 * First click selects a player name; second click on a different
 * player swaps them (positions and shirt numbers); clicking the
 * same player cancels the selection.
 * ================================================================ */
__attribute__((used, externally_visible))
void EditTacticsPlayerNamesCommon(MenuEntry *cur)
{
    if ((int16_t)_editTacticsSelectedPairSlotIndex >= 0)
        return;

    /* Compute relative index: current entry vs menuEntries[11] */
    uint16_t relIdx = cur->ordinal - _menuEntries[11].ordinal;

    TeamFileEx *teamEx = _chooseTacticsTeamPtr;
    uint8_t savedTactics = teamEx->team.tactics;
    teamEx->team.tactics = _editTacticsCurrentTactics.ballOutOfPlayTactics;

    uint16_t slot, playerIndex;
    GetPlayerInfoBreakPosition(teamEx, relIdx, &slot, &playerIndex);

    teamEx = _chooseTacticsTeamPtr;
    teamEx->team.tactics = savedTactics;

    if ((int16_t)_editTacticsSelectedPlayerNameIndex >= 0) {
        if (_editTacticsSelectedPlayerNameIndex == playerIndex) {
            /* Same player - cancel by setting to 0xFFFF */
            playerIndex = 0xFFFF;
            /* Fall through to update selection state */
        } else {
            /* Different player - swap them */
            EditTactics_SwapPlayersInSlots(
                _editTacticsSelectedPlayerSlotIndex, slot);
            SetBenchPlayersNumbers(_chooseTacticsTeamPtr);
            teamEx = _chooseTacticsTeamPtr;
            teamEx->team.playerValuesDirty = TEAM_VALUES_DIRTY;
            EditTactics_CommitPlayerPositionsAndRecalcSkill();
            _editTacticsSelectedPlayerNameIndex = 0xFFFF;
            DrawMenu();
            return;
        }
    }

    /* Start or update selection */
    _editTacticsSelectedPlayerNameIndex = playerIndex;
    _editTacticsSelectedPlayerSlotIndex = slot;

    MenuEntry *prevEntry = _editTacticsSelectedPlayerMenuEntryPtr;
    _editTacticsSelectedPlayerMenuEntryPtr = cur;

    if (prevEntry != (MenuEntry *)0) {
        CallDrawMenuItem(prevEntry);
    }
    CallDrawMenuItem(cur);
    EditTacticsMenuSavePlayerPositions();
    DrawPlayerNamesAndFaces();
}

/* ================================================================
 * LoadTactics  (SWOS2.s line 88770)
 *
 * Menu callback: prompts for save disk, lets user select a .TAC
 * file, saves current tactics, loads selected tactics file into
 * editTacticsCurrentTactics, copies filename to importTacticsFilename,
 * sets importMenuSelectedTactics=1000 and tacticsChangedFlag=1.
 * Shows error if wrong extension or load fails.
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadTactics(void)
{
    _importMenuSelectedTactics = 0xFFFF;

    if (Asset_PromptInsertSaveDiskDf0(_aLoadEditedTact) != 0)
        goto end;

    if (GetFilenameAndExtension(0, _aLoadEditedTact, _aLoadEditedTact) != 0)
        goto cleanup;

    if (_extension != 0x2E544143u) { /* '.TAC' */
        ShowErrorMenu(_aNotATacticsFil);
        goto cleanup;
    }

    SaveCurrentTactics();

    if (ReadTacticsFile(_selectedFilename) != 0)
        goto cleanup;

    StringCopy(_selectedFilename, &_importTacticsFilename);
    _importMenuSelectedTactics = 1000;
    _tacticsChangedFlag = 1;

cleanup:
    ShowInsertDisk2Dialog(_aLoadEditedTact);
end:
    SetExitMenuFlag();
}

/* ================================================================
 * Chunk 117: EditTeamsFinish
 * ================================================================ */

/* ================================================================
 * Chunk 133: EditTeams functions
 * ================================================================ */

/* --- Externs for EditTeams --- */
extern uint8_t *_editTeamsSelectedTeam;   /* pointer to source team data */
#ifdef SWOS_SDL_BUILD
#include "custom_team.h"    /* the EDIT TEAM screen also edits a custom team for online play */
#endif
extern uint16_t _saveCustomTeams;
extern uint16_t _teamIsImported;
extern uint16_t _controlMask;
extern uint16_t _kTeamColorsTable[];      /* KIT_COLOURS word entries (purple the 11th) */
/* edit-teams kit sprite-copy descriptors; .colTable[16] is the palette remap
 * (was the editTeams*ColTable .equ aliases at descriptor +4). */
extern EditTeamsSpriteCopy _editTeamsPrimaryShirts;
extern EditTeamsSpriteCopy _editTeamsShorts;
extern EditTeamsSpriteCopy _editTeamsSocks;
extern EditTeamsSpriteCopy _editTeamsSecondaryShirt;
extern EditTeamsSpriteCopy _editTeamsSecondaryShorts;
extern EditTeamsSpriteCopy _editTeamsSecondarySocks;
extern uint16_t _shirtTypesTable[];       /* 4 entries of 8 words each (4 shirt types) */
extern uint16_t _shirtsIndexTable[];      /* 8 word entries (byte offsets into color table) */
extern void *_playerPositionsStringTable[];       /* 8 string pointers */

/* EditTeamsGetPlayerPointer — return PlayerFile pointer for player N in selTeamsPtr */
static PlayerFile *EditTeamsGetPlayerFileC(MenuEntry *entry)
{
    uint16_t idx = entry->ordinal - 23;
    TeamFileEx *team = _selectedTeams;
    return &team->players[idx];
}

/* PrimaryKitColorConversion — build primary shirt/shorts/socks color tables */
static void PrimaryKitColorConversionC(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint16_t *colTable = _editTeamsPrimaryShirts.colTable;
    uint16_t stripesCol = _kTeamColorsTable[tf->prStripesColor];
    uint16_t basicCol = _kTeamColorsTable[tf->prBasicColor];
    uint16_t *types = &_shirtTypesTable[tf->prShirtType * 8];
    uint16_t *indices = _shirtsIndexTable;

    for (int16_t i = 7; i >= 0; i--) {
        uint16_t idx = *indices++;
        if (*types++ == 2)
            colTable[idx / 2] = basicCol;
        else
            colTable[idx / 2] = stripesCol;
    }

    /* Shorts */
    uint16_t shortsCol = _kTeamColorsTable[tf->prShortsColor];
    _editTeamsShorts.colTable[11] = shortsCol;   /* slot 11 */

    /* Socks */
    uint16_t socksCol = _kTeamColorsTable[tf->prSocksColor];
    _editTeamsSocks.colTable[11] = socksCol;   /* slot 11 */
}

/* SecondaryKitColorConversion — build secondary shirt/shorts/socks color tables */
static void SecondaryKitColorConversionC(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint16_t *colTable = _editTeamsSecondaryShirt.colTable;
    uint16_t stripesCol = _kTeamColorsTable[tf->secStripesColor];
    uint16_t basicCol = _kTeamColorsTable[tf->secBasicColor];
    uint16_t *types = &_shirtTypesTable[tf->secShirtType * 8];
    uint16_t *indices = _shirtsIndexTable;

    for (int16_t i = 7; i >= 0; i--) {
        uint16_t idx = *indices++;
        if (*types++ == 2)
            colTable[idx / 2] = basicCol;
        else
            colTable[idx / 2] = stripesCol;
    }

    /* Shorts */
    uint16_t shortsCol = _kTeamColorsTable[tf->secShortsColor];
    _editTeamsSecondaryShorts.colTable[11] = shortsCol;   /* slot 11 */

    /* Socks */
    uint16_t socksCol = _kTeamColorsTable[tf->secSocksColor];
    _editTeamsSecondarySocks.colTable[11] = socksCol;   /* slot 11 */
}

/* MarkFirstKitShirt — highlight selected primary shirt type in menu */
static void MarkFirstKitShirtC(void)
{
    TeamFile *tf = &_selectedTeams->team;
    MenuEntry *e = &_menuEntries[0x348 / 56]; /* menuEntries + $348 / 56 = entry 15 */
    uint8_t shirtType = tf->prShirtType;

    for (int16_t i = 3; i >= 0; i--) {
        e->bgColor = 9;  /* PINK_TO_BROWN_9 */
        if ((uint8_t)i == shirtType)
            e->bgColor = 10;  /* RED_10 */
        e = (MenuEntry *)((uint8_t *)e - MENUENTRY_STRIDE);
    }
}

/* MarkSecondKitShirt — highlight selected secondary shirt type in menu */
static void MarkSecondKitShirtC(void)
{
    TeamFile *tf = &_selectedTeams->team;
    MenuEntry *e = &_menuEntries[0x428 / 56]; /* entry 19 */
    uint8_t shirtType = tf->secShirtType;

    for (int16_t i = 3; i >= 0; i--) {
        e->bgColor = 9;  /* PINK_TO_BROWN_9 */
        if ((uint8_t)i == shirtType)
            e->bgColor = 10;  /* RED_10 */
        e = (MenuEntry *)((uint8_t *)e - MENUENTRY_STRIDE);
    }
}

/* EditTeamsInit — copy team to edit buffer, clear import flag */
__attribute__((used, externally_visible))
void EditTeamsInit(void)
{
    uint16_t *src = (uint16_t *)_editTeamsSelectedTeam;
    uint16_t *dst = (uint16_t *)&_selTeamsPtr;
    for (int16_t i = 341; i >= 0; i--)
        *dst++ = *src++;
    _teamIsImported = 0;
    EditTeamsFinish();
}

/* EditTeamsFinish — reinit edit teams menu after undo/load */
__attribute__((used, externally_visible))
void EditTeamsFinish(void)
{
    InitEditTeamsMenu();
    /* nullsub_16 was called here in asm — it's just rts, so omitted */
}

/* EditTeamsUndo — restore team from original, reinit menu */
__attribute__((used, externally_visible))
void EditTeamsUndo(void)
{
#ifdef SWOS_SDL_BUILD
    /* A custom team for online play (src/custom_team.c): this button is LINEUP there,
     * the team screen a match shows, as in SWOS 2020's custom team editor. */
    if (CustomTeam_Editing()) {
        CustomTeam_Lineup();
        return;
    }
#endif
    _teamIsImported = 0;
    uint16_t *src = (uint16_t *)_editTeamsSelectedTeam;
    uint16_t *dst = (uint16_t *)&_selTeamsPtr;
    for (int16_t i = 341; i >= 0; i--)
        *dst++ = *src++;
    InitEditTeamsMenu();
    DrawMenu();
}

/* EditTeamsOK — save edited team if changed */
__attribute__((used, externally_visible))
void EditTeamsOK(void)
{
    uint16_t *src = (uint16_t *)_editTeamsSelectedTeam;
    uint16_t *cmp = (uint16_t *)&_selTeamsPtr;

    for (int16_t i = 341; i >= 0; i--) {
        if (*src++ != *cmp++) {
            /* Teams differ — save changes */
            _saveCustomTeams = 1;
            uint16_t *dst = (uint16_t *)_editTeamsSelectedTeam;
            uint16_t *from = (uint16_t *)&_selTeamsPtr;
            for (int16_t j = 341; j >= 0; j--)
                *dst++ = *from++;
            SetExitMenuFlag();
            return;
        }
    }
    /* No changes */
    SetExitMenuFlag();
}

/* EditTeamsChangeShirtsPrimary — change primary shirt color on joystick input */
__attribute__((used, externally_visible))
void EditTeamsChangeShirtsPrimary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0;

    if (_controlMask == 0x41) {          /* left+fire */
        d0 = tf->prStripesColor;
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
        tf->prStripesColor = d0;
    } else if (_controlMask == 0x81) {   /* right+fire */
        d0 = tf->prStripesColor;
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
        tf->prStripesColor = d0;
    } else if (_controlMask == 0x101) {  /* up+fire */
        if (tf->prShirtType == 0) {  /* SHIRT_ORDINARY */
            d0 = tf->prStripesColor;
            d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
            tf->prStripesColor = d0;
        } else {
            d0 = tf->prBasicColor;
            d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
            tf->prBasicColor = d0;
        }
    } else if (_controlMask == 0x201) {  /* down+fire */
        if (tf->prShirtType == 0) {  /* SHIRT_ORDINARY */
            d0 = tf->prStripesColor;
            d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
            tf->prStripesColor = d0;
        } else {
            d0 = tf->prBasicColor;
            d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
            tf->prBasicColor = d0;
        }
    } else {
        return;
    }

    PrimaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(1, &_menuEntries[0x118 / 56]);
}

/* EditTeamsChangeShirtsSecondary — change secondary shirt color on joystick input */
__attribute__((used, externally_visible))
void EditTeamsChangeShirtsSecondary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0;

    if (_controlMask == 0x41) {          /* left+fire */
        d0 = tf->secStripesColor;
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
        tf->secStripesColor = d0;
    } else if (_controlMask == 0x81) {   /* right+fire */
        d0 = tf->secStripesColor;
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
        tf->secStripesColor = d0;
    } else if (_controlMask == 0x101) {  /* up+fire */
        if (tf->secShirtType == 0) {
            d0 = tf->secStripesColor;
            d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
            tf->secStripesColor = d0;
        } else {
            d0 = tf->secBasicColor;
            d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
            tf->secBasicColor = d0;
        }
    } else if (_controlMask == 0x201) {  /* down+fire */
        if (tf->secShirtType == 0) {
            d0 = tf->secStripesColor;
            d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
            tf->secStripesColor = d0;
        } else {
            d0 = tf->secBasicColor;
            d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
            tf->secBasicColor = d0;
        }
    } else {
        return;
    }

    SecondaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(1, &_menuEntries[0x1F8 / 56]);
}

/* EditTeamsChangeShortsPrimary — change primary shorts color */
__attribute__((used, externally_visible))
void EditTeamsChangeShortsPrimary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0 = tf->prShortsColor;

    if (_controlMask == 0x41 || _controlMask == 0x101) {
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
    } else if (_controlMask == 0x81 || _controlMask == 0x201) {
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
    } else {
        return;
    }

    tf->prShortsColor = d0;
    PrimaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(1, &_menuEntries[0x150 / 56]);
}

/* EditTeamsChangeShortsSecondary — change secondary shorts color */
__attribute__((used, externally_visible))
void EditTeamsChangeShortsSecondary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0 = tf->secShortsColor;

    if (_controlMask == 0x41 || _controlMask == 0x101) {
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
    } else if (_controlMask == 0x81 || _controlMask == 0x201) {
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
    } else {
        return;
    }

    tf->secShortsColor = d0;
    SecondaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(1, &_menuEntries[0x230 / 56]);
}

/* EditTeamsChangeSocksPrimary — change primary socks color */
__attribute__((used, externally_visible))
void EditTeamsChangeSocksPrimary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0 = tf->prSocksColor;

    if (_controlMask == 0x41 || _controlMask == 0x101) {
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
    } else if (_controlMask == 0x81 || _controlMask == 0x201) {
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
    } else {
        return;
    }

    tf->prSocksColor = d0;
    PrimaryKitColorConversionC();
    CallDrawMultipleItems(1, &_menuEntries[0x188 / 56]);
}

/* EditTeamsChangeSocksSecondary — change secondary socks color */
__attribute__((used, externally_visible))
void EditTeamsChangeSocksSecondary(void)
{
    TeamFile *tf = &_selectedTeams->team;
    uint8_t d0 = tf->secSocksColor;

    if (_controlMask == 0x41 || _controlMask == 0x101) {
        d0 = (d0 == 0) ? KIT_COLOURS - 1 : d0 - 1;
    } else if (_controlMask == 0x81 || _controlMask == 0x201) {
        d0 = (d0 + 1 == KIT_COLOURS) ? 0 : d0 + 1;
    } else {
        return;
    }

    tf->secSocksColor = d0;
    SecondaryKitColorConversionC();
    CallDrawMultipleItems(1, &_menuEntries[0x268 / 56]);
}

/* EditTeamsSelectPrimaryShirtType — set primary shirt type from menu ordinal */
__attribute__((used, externally_visible))
void EditTeamsSelectPrimaryShirtType(MenuEntry *entry)
{
    TeamFile *tf = &_selectedTeams->team;
    tf->prShirtType = (uint8_t)(entry->ordinal - 12);
    MarkFirstKitShirtC();
    PrimaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(4, &_menuEntries[0x2A0 / 56]);
    CallDrawMultipleItems(1, &_menuEntries[0x118 / 56]);
}

/* EditTeamsSelectSecondaryShirtType — set secondary shirt type from menu ordinal */
__attribute__((used, externally_visible))
void EditTeamsSelectSecondaryShirtType(MenuEntry *entry)
{
    TeamFile *tf = &_selectedTeams->team;
    tf->secShirtType = (uint8_t)(entry->ordinal - 16);
    MarkSecondKitShirtC();
    SecondaryKitColorConversionC();
    /* nullsub_16 */
    CallDrawMultipleItems(4, &_menuEntries[0x380 / 56]);
    CallDrawMultipleItems(1, &_menuEntries[0x1F8 / 56]);
}

/* EditTeamsChangePlayerName — edit player name via text input */
__attribute__((used, externally_visible))
void EditTeamsChangePlayerName(MenuEntry *entry)
{
    InputText(23, (uint8_t *)entry);
}

/* EditTeamsSelectTeamName — edit team name via text input */
__attribute__((used, externally_visible))
void EditTeamsSelectTeamName(MenuEntry *entry)
{
    InputText(17, (uint8_t *)entry);
}

/* EditTeamsSelectCoachName — edit coach name via text input */
__attribute__((used, externally_visible))
void EditTeamsSelectCoachName(MenuEntry *entry)
{
    InputText(23, (uint8_t *)entry);
}

/* EditTeamsChangePlayerFace — cycle through player face sprites */
__attribute__((used, externally_visible))
void EditTeamsChangePlayerFace(MenuEntry *entry)
{
    PlayerFile *pf = EditTeamsGetPlayerFileC(entry);
    uint8_t posAndFace = pf->positionAndFace;
    uint16_t faceIdx = (posAndFace & PF_FACE_MASK) >> 3;

    faceIdx++;
    if (faceIdx == 3)
        faceIdx = 0;

    pf->positionAndFace = (posAndFace & (uint8_t)~PF_FACE_MASK) | (uint8_t)(faceIdx << PF_FACE_SHIFT);

    /* Update menu entry sprite to show new face */
    entry->fgWord = (uint16_t)(faceIdx + 186);  /* SPR_PL_FACE_WHITE_GUY */
    CallDrawMenuItem(entry);
}

/* InitEditTeamsMenu — set player faces, names, kit colors in menu */
__attribute__((used, externally_visible))
void InitEditTeamsMenu(void)
{
    /* Show/hide import entry based on teamIsImported */
    _menuEntries[0x460 / 56].isInvisible = 0;
    if (_teamIsImported != 0)
        _menuEntries[0x460 / 56].isInvisible = 1;
#ifdef SWOS_SDL_BUILD
    /* A custom team for online play: IMPORT stays (another database team can be taken
     * at any time), and the UNDO button (entry 22) is LINEUP. */
    if (CustomTeam_Editing()) {
        _menuEntries[0x460 / 56].isInvisible = 0;
        _menuEntries[22].fg = (uintptr_t)CustomTeam_LineupLabel();
    }
#endif

    TeamFileEx *teamEx = _selectedTeams;

    /* Team name → entry 0 */
    _menuEntries[0].fg = (uintptr_t)(uint8_t *)teamEx->team.teamName;

    /* Coach name → entry at offset 0xA8 */
    _menuEntries[0xA8 / 56].fg = (uintptr_t)(uint8_t *)teamEx->team.coachName;

    PrimaryKitColorConversionC();
    SecondaryKitColorConversionC();
    MarkFirstKitShirtC();
    MarkSecondKitShirtC();

    /* Set player faces and names */
    MenuEntry *faceEntry = &_menuEntries[0x508 / 56];
    MenuEntry *nameEntry = &_menuEntries[0x888 / 56];

    for (int16_t i = 15; i >= 0; i--) {
        PlayerFile *pf = &teamEx->players[15 - i];
        uint8_t posAndFace = pf->positionAndFace;
        uint16_t faceIdx = (posAndFace & PF_FACE_MASK) >> 3;
        faceEntry->fgWord = (uint16_t)(faceIdx + 186);  /* SPR_PL_FACE_WHITE_GUY */
        faceEntry = (MenuEntry *)((uint8_t *)faceEntry + MENUENTRY_STRIDE);

        nameEntry->fg = (uintptr_t)(uint8_t *)pf->playerName;
        nameEntry = (MenuEntry *)((uint8_t *)nameEntry + MENUENTRY_STRIDE);
    }
}

/* EditTeamsPlayersBeforeDraw — set up per-player overlay entries before draw */
__attribute__((used, externally_visible))
void EditTeamsPlayersBeforeDraw(MenuEntry *entry)
{
    uint16_t playerIdx = entry->ordinal - 23;
    TeamFileEx *teamEx = _selectedTeams;
    PlayerFile *pf = &teamEx->players[playerIdx];

    uint8_t posAndFace = pf->positionAndFace;
    int16_t y = entry->y;

    MenuEntry *e = &_menuEntries[0xC08 / 56];

    /* Player number (shirt number) */
    uint8_t shirtNum = (uint8_t)pf->shirtNumber;
    e->fgWord = (uint16_t)shirtNum;
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    /* Second entry — just set y */
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    /* Position string */
    uint16_t posIdx = (posAndFace & PF_POSITION_MASK) >> 3;  /* extract top 3 bits, shift to word index */
    e->fg = (uintptr_t)_playerPositionsStringTable[posIdx / 4];
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    /* Hidden entries */
    e->isInvisible = 1;
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    e->isInvisible = 1;
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    e->isInvisible = 1;
    e->isInvisible = 1;  /* written twice in asm */
    e->y = y;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);

    /* Draw all 6 entries */
    DrawMultipleItems(6, &_menuEntries[0xC08 / 56]);
}

/* ================================================================
 * Chunk 148: EditTeamsCallback
 * ================================================================ */

extern uint8_t _editTeamsMenu[];
extern void ShowMenu(uint8_t *menu);

/* EditTeamsCallback: called when a team is selected from team selection.
 * Stores team pointer, then shows the edit teams sub-menu.
 * a0 = selected team pointer. */
__attribute__((used, externally_visible))
void EditTeamsCallback(TeamFileEx *team)
{
    _editTeamsSelectedTeam = (uint8_t *)team;
    ShowMenu(_editTeamsMenu);
}


/* ==========================================================================
 * Chunk 203 — EditCustomTeam + ImportTeamsMenu
 * (SWOS2.s lines 44496-44537, 45967-46026)
 * ========================================================================== */

extern uint16_t _numSelectedTeams;
extern uint8_t  _lastLoadedExtNumber;
extern uint8_t  _teamFileBuffer[];
extern uint8_t  _lastSelectedTeam[];
extern uint8_t  _aEditCustomTeam[];
extern uint8_t  _aSelectTeamToEd[];
extern uint8_t  _aCustomTeamsChanged[];
extern uint8_t  _aCustomsEdt[];
extern uint8_t  _aSave[];
extern uint8_t  _aAbort[];
extern uint8_t  _aSelectTeamToIm[];

extern int16_t CheckContinueAbortPrompt(uint8_t *header);
extern int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing,
                                     uint8_t *contText, uint8_t *abortTextStr,
                                     uint16_t defaultEntry);
extern int16_t Asset_PromptInsertSaveDiskDf0(uint8_t *title);
extern int16_t WriteFile(uint8_t *filename, uint8_t *src, uint32_t size);
extern void ShowFsErrorByCode(uint16_t errCode);
extern void ShowInsertDisk2Dialog(uint8_t *header);

/* EditCustomTeam — show "EDIT CUSTOM TEAMS" flow: choose team, edit, optionally save. */
__attribute__((used, externally_visible))
void EditCustomTeam(void)
{
    if (CheckContinueAbortPrompt(_aEditCustomTeam) != 0)
        return;

    _numSelectedTeams = 0;
    _saveCustomTeams = 0;
    ChooseTeamsDialog(0, 0, 1, _aSelectTeamToEd, (void *)EditTeamsCallback);

    if (_saveCustomTeams == 0)
        goto done;

retry:
    if (DoContinueAbortMenu(_aEditCustomTeam, _aCustomTeamsChanged,
                              _aSave, _aAbort, 0) != 0)
        goto done;

    /* Prompt for save disk */
    if (Asset_PromptInsertSaveDiskDf0(_aEditCustomTeam) != 0)
        goto retry;

    /* Write team file: size = teamCount * 684 + 2 (for the count word).
     *
     * teamFileBuffer was byte-swapped to host-native after load (see
     * byteswap_loaded_team_file in teams.c), so on LE x86 the count
     * word and each TeamFile.globalTeamNumber sit in LE byte order.
     * The on-disk .EDT format is BE (Amiga m68k native) and the load
     * path swaps BE→host-native unconditionally, so writing the
     * host-native bytes verbatim produces a file whose count and
     * globalTeamNumbers re-swap to garbage on the next load. Pre-swap
     * back to BE before WriteFile and restore host-native after, so
     * any subsequent in-game uses of teamFileBuffer keep working. */
    {
        uint16_t teamCount = *(uint16_t *)_teamFileBuffer;
        uint32_t fileSize = (uint32_t)teamCount * 684 + 2;
#ifdef SWOS_SDL_BUILD
        /* Convert host-native → BE in-place before write. Pre-fix bug
         * (commit e986990) used byteswap_loaded_team_file in both
         * directions, but that function reads the loop count via
         * BE_READ_U16, which on host-native input mis-decodes to
         * ~12288 and the loop stomps ~8 MB past the buffer. */
        extern void byteswap_team_file_to_disk(void);
        extern void byteswap_loaded_team_file(void);
        byteswap_team_file_to_disk();
#endif
        int16_t result = WriteFile(_aCustomsEdt, _teamFileBuffer, fileSize);
#ifdef SWOS_SDL_BUILD
        /* Restore host-native after write — buffer is BE-on-disk now
         * (just-written bytes), so re-swap with the load-time helper. */
        byteswap_loaded_team_file();
#endif
        if (result != 0) {
            ShowFsErrorByCode((uint16_t)result);
            goto retry;
        }
    }

done:
    _lastLoadedExtNumber = 0xFF;
    ShowInsertDisk2Dialog(_aEditCustomTeam);
}

/* ImportTeamsMenu — import a team from club/national teams into the editor buffer. */
__attribute__((used, externally_visible))
void ImportTeamsMenu(void)
{
    /* Save teamFileBuffer to editTeamsBuffer */
    {
        uint16_t *src = (uint16_t *)_teamFileBuffer;
        uint16_t teamCount = *src;
        uint32_t wordCount = (uint32_t)teamCount * 684 + 2;
        wordCount >>= 1; /* convert bytes to words */
        volatile uint16_t *s = (volatile uint16_t *)src;
        volatile uint16_t *d = (volatile uint16_t *)_editTeamsBuffer;
        while (wordCount > 0) {
            *d++ = *s++;
            wordCount--;
        }
    }

    /* Save lastSelectedTeam area (0xF6 words = 246 words = 492 bytes) */
    {
        volatile uint16_t *src = (volatile uint16_t *)_lastSelectedTeam;
        volatile uint16_t *dst = (volatile uint16_t *)_editTeamsSaveVarsArea;
        for (int16_t i = 0xF5; i >= 0; i--)
            *dst++ = *src++;
    }

    ShowInsertDisk2Dialog(_aSelectTeamToIm);

    /* Show team selection dialog (club + national, no custom) */
    ChooseTeamsDialog(1, 1, 0, _aSelectTeamToIm, (void *)0);

    extern uint16_t _selectedTeamCountryNumber;
    extern TeamFileEx *_selectedTeamPtr;

    if ((int16_t)_selectedTeamCountryNumber != -1) {
        /* Save selTeamsPtr team number and selTeamImportField, copy selected team data */
        uint16_t savedTeamNum = *(uint16_t *)&_selectedTeams->team.countryNumber;
        uint8_t savedImportField = _selTeamImportField;

        /* Copy 342 words (684 bytes) from selectedTeamPtr to selTeamsPtr */
        volatile uint16_t *src = (volatile uint16_t *)_selectedTeamPtr;
        volatile uint16_t *dst = (volatile uint16_t *)&_selTeamsPtr;
        for (int16_t i = 341; i >= 0; i--)
            *dst++ = *src++;

        /* Restore saved values */
#ifdef SWOS_SDL_BUILD
        /* A custom team for online play IS the database team it was made from, renamed
         * and redressed -- so it takes that team whole, number included: the number is
         * what its players are checked against (CustomTeam_Allowed). */
        if (!CustomTeam_Editing())
#endif
        {
        _selTeamImportField = savedImportField;
        *(uint16_t *)&_selectedTeams->team.countryNumber = savedTeamNum;
        }

        _teamIsImported = 1;
        SetCurrentEntry(21);
    }

    /* Restore lastSelectedTeam area from editTeamsSaveVarsArea */
    {
        volatile uint16_t *src = (volatile uint16_t *)_editTeamsSaveVarsArea;
        volatile uint16_t *dst = (volatile uint16_t *)_lastSelectedTeam;
        for (int16_t i = 245; i >= 0; i--)
            *dst++ = *src++;
    }

    _lastLoadedExtNumber = 0xFF;

    /* Restore teamFileBuffer from editTeamsBuffer */
    {
        uint16_t *srcBuf = (uint16_t *)_editTeamsBuffer;
        uint16_t teamCount = *srcBuf;
        uint32_t wordCount = (uint32_t)teamCount * 684 + 2;
        wordCount >>= 1;
        volatile uint16_t *s = (volatile uint16_t *)srcBuf;
        volatile uint16_t *d = (volatile uint16_t *)_teamFileBuffer;
        while (wordCount > 0) {
            *d++ = *s++;
            wordCount--;
        }
    }

    InitEditTeamsMenu();
}

/* Helper: set bg color index — set only the low nibble (color index) of
 * the entry's bgColor word, preserving the upper 12 bits. */
static void setBgColor(MenuEntry *e, uint16_t colorIdx) {
    e->bgColor = (e->bgColor & 0xFFF0) | colorIdx;
}

/* ================================================================
 * ChooseTeamForEditTactics  (SWOS2.s line 52506)
 *
 * Display a list of teams to choose from for editing tactics.
 * Sorts teams alphabetically (bubble sort), shows menu, returns
 * selected team pointer (or NULL if cancelled).
 *
 * in:  a0 = menu title string
 *      a1 = pointer to team numbers array (uint16_t[])
 *      d0 = number of teams
 * out: a0 = team pointer (or NULL)
 * ================================================================ */
extern uintptr_t _chooseTeamMenuName;
extern uint16_t _numTeamsCountryBuffer;
extern void *_dwArrayCountries[];
extern uint8_t _indexCountries[];
extern uint8_t _selectTeamsForTacticsMenu[];
extern uint16_t _teamSelected;

__attribute__((used, externally_visible))
TeamFileEx *ChooseTeamForEditTactics(uint8_t *menuTitle, uint16_t *teamNumbers,
                                      uint16_t numTeams)
{
    _chooseTeamMenuName = (uintptr_t)menuTitle;
    _numTeamsCountryBuffer = numTeams;

    void **nameArray = _dwArrayCountries;
    uint8_t *idxArray = _indexCountries;
    uint16_t count = 0;

    uint16_t *teamNums = teamNumbers;
    uint16_t i;
    for (i = 0; i < numTeams; i++) {
        uint16_t teamNum = teamNums[i];
        TeamFileEx *teamEx = VerifyTeamLoaded(teamNum);
        idxArray[i] = (uint8_t)count;
        nameArray[count] = teamEx->team.teamName;
        count++;
    }

    /* Bubble sort by team name */
    if (numTeams >= 2) {
        int16_t limit = (int16_t)numTeams - 2;
        int16_t swapped;
        do {
            swapped = 0;
            for (int16_t j = 0; j <= limit; j++) {
                uint8_t idx1 = idxArray[j];
                uint8_t idx2 = idxArray[j + 1];
                uint8_t *name1 = (uint8_t *)nameArray[idx1];
                uint8_t *name2 = (uint8_t *)nameArray[idx2];
                /* Compare strings */
                uint8_t *p1 = name1;
                uint8_t *p2 = name2;
                int cmpResult = 0;
                while (1) {
                    uint8_t c1 = *p1++;
                    if (c1 == 0) break;
                    uint8_t c2 = *p2++;
                    if (c1 < c2) break;
                    if (c1 > c2) {
                        /* Swap */
                        uint8_t tmp = idxArray[j];
                        idxArray[j] = idxArray[j + 1];
                        idxArray[j + 1] = tmp;
                        swapped = 1;
                        cmpResult = 1;
                        break;
                    }
                }
                (void)cmpResult;
            }
        } while (swapped);
    }

    /* Show the selection menu */
    ShowMenu(_selectTeamsForTacticsMenu);

    int16_t selected = (int16_t)_teamSelected;
    if (selected >= 0) {
        uint8_t finalIdx = _indexCountries[selected];
        uint16_t teamNum = teamNumbers[finalIdx];
        return VerifyTeamLoaded(teamNum);
    }

    return (TeamFileEx *)0;
}

/* ================================================================
 * EditTactics  (SWOS2.s line 68771)
 *
 * Entry point for Edit Tactics menu. Determines which team to edit:
 * - In career mode: uses careerTeam
 * - With multiple selected teams: shows team selection menu
 * - With one team: uses that team
 * - No teams: loads default team (Leicester City)
 * ================================================================ */
extern uint8_t _aEditTacticsSel[];  /* "EDIT TACTICS - SELECT TEAM" */
extern uint8_t _editTacticsCountryBuffer[];
extern uint8_t _teamFileBuffer[];

__attribute__((used, externally_visible))
void EditTactics(void)
{
    _inEditTactics = 1;

    TeamFileEx *teamPtr = &_careerTeam;

    if (_gameType == GAME_TYPE_CAREER) {
        /* Career mode: use career team directly */
        goto show_menu;
    }

    if (_numSelectedTeams == 0) {
        goto no_teams;
    }

    {
        TeamFileEx *selPtr = _selectedTeams;
        uint16_t *countryBuf = (uint16_t *)_editTacticsCountryBuffer;
        uint16_t found = 0;
        TeamFileEx *lastNonComputer = (TeamFileEx *)0;

        for (uint16_t i = 0; i < _numSelectedTeams; i++) {
            TeamFileEx *t = (TeamFileEx *)selPtr + i;
            if (t->team.teamControls != TEAM_COMPUTER) {
                *countryBuf++ = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
                lastNonComputer = t;
                found++;
            }
        }

        if (found == 0) {
            if (_numSelectedTeams != 0) {
                goto check_sel_team;
            }
            goto no_teams;
        }

        teamPtr = lastNonComputer;

        if (found == 1) {
            goto show_menu;
        }

        /* Multiple non-computer teams: show selection */
        teamPtr = ChooseTeamForEditTactics(_aEditTacticsSel,
                    (uint16_t *)_editTacticsCountryBuffer, found);
        if (teamPtr != (TeamFileEx *)0) {
            goto show_menu;
        }
        return;
    }

no_teams:
    /* Load English Premier Division and pick Leicester City (18th team) */
    LoadTeamFile(8);
    AddTeamToBuffer((TeamFileEx *)(_teamFileBuffer + 0x2D6E));
    InitTeams();
    goto show_menu_from_sel;

check_sel_team:
    teamPtr = &_careerTeam;
    if (_selTeamNumber != 0xFFFF) {
        goto show_menu;
    }

show_menu_from_sel:
    teamPtr = _selectedTeams;

show_menu:
    ShowStandardChooseTacticsMenu(teamPtr);
}

/* ================================================================
 * EditTactics_ApplyPlayerRowHighlighting  (SWOS2.s line 70226)
 *
 * Sets the background color of menuEntries[39] based on the player
 * row index (position group: GK=0, defenders=1-10, subs=11+, slot16).
 * Also checks injury, selection, pair-swap, and flip-on states.
 *
 * in:  d7 = player ordinal (0-16)
 *      a5 = current menu entry
 * ================================================================ */
extern uint16_t _editTacticsFlipPairColorTable[];

__attribute__((used, externally_visible))
void EditTactics_ApplyPlayerRowHighlighting(int16_t playerOrdinal, MenuEntry *cur)
{
    MenuEntry *entry39 = &_menuEntries[39];

    if (playerOrdinal == 16) {
        setBgColor(entry39, BLUE_TO_BLACK_8);
    } else if (playerOrdinal < 11) {
        if (playerOrdinal == 0) {
            setBgColor(entry39, YELLOW_GREEN_BLACK_14);
        } else {
            setBgColor(entry39, BRIGHT_BLUE_TO_BLUE_13);
        }
    } else if (playerOrdinal == 11) {
        setBgColor(entry39, YELLOW_GREEN_BLACK_14);
    } else {
        setBgColor(entry39, BLUE_TO_PURPLE_11);
    }

    /* Save/restore tactics byte, get player info */
    TeamFileEx *teamBase = _chooseTacticsTeamPtr;
    uint8_t savedTactics = teamBase->team.tactics;
    teamBase->team.tactics = _editTacticsCurrentTactics.ballOutOfPlayTactics;

    uint16_t slot, playerIndex;
    uint8_t *playerPtr = GetPlayerInfoBreakPosition(teamBase, (uint16_t)playerOrdinal, &slot, &playerIndex);
    PlayerFile *pf = (PlayerFile *)(playerPtr + 76);

    teamBase = _chooseTacticsTeamPtr;
    teamBase->team.tactics = savedTactics;

    /* Check cardsInjuries for injury */
    uint16_t posField = (uint16_t)pf->cardsInjuries & 0xE0;
    if (posField != 0 && posField != 0x20) {
        setBgColor(entry39, RED_10);
    }

    /* Selected player highlight */
    if (playerIndex == _editTacticsSelectedPlayerNameIndex) {
        setBgColor(entry39, ORANGE_TO_BROWN_6);
    }

    /* Pair swap highlight */
    if ((int16_t)_editTacticsSelectedPairSlotIndex >= 0) {
        uint16_t pairSlot = _editTacticsSelectedPairSlotIndex + 1;
        if (slot == pairSlot) {
            setBgColor(entry39, ORANGE_TO_BROWN_12);
        }
    }

    /* Flip-on pair coloring */
    if (_flipOnOff != 0) {
        if (slot != 0 && slot < 11) {
            uint8_t *pairIds = _editTacticsCurrentTactics.pairIds;
            int8_t pairVal = (int8_t)pairIds[slot - 1];
            if (pairVal >= 0) {
                uint16_t colorIdx = _editTacticsFlipPairColorTable[pairVal];
                setBgColor(entry39, colorIdx);
            } else {
                setBgColor(entry39, PINK_TO_BROWN_0);
            }
        } else {
            setBgColor(entry39, PINK_TO_BROWN_0);
        }
    }

    /* Copy Y position and set player face */
    entry39->y = cur->y;
    SetMenuEntryPlayerFace(entry39, (PlayerFile *)playerPtr);

    /* Check position changes (only for first 11 players) */
    if (slot < 11) {
        uint8_t *posArray = &_playerPositionsArray;
        uint8_t *oldPosArray = &_oldPlayerPositionsArray;
        uint8_t curPos = posArray[playerIndex];
        uint8_t oldPos = oldPosArray[playerIndex];
        if (curPos > oldPos) {
            entry39->fgWord = SPR_CHECK_SMALL;   /* value rose → green tick */
        } else if (curPos < oldPos) {
            entry39->fgWord = SPR_CROSS_SMALL;   /* value fell → red cross */
        }
    }
}

/* ================================================================
 * EditTacticsAfterDrawPlayerNames  (SWOS2.s line 70061)
 *
 * After-draw callback for edit tactics player name entries.
 * Sets up the info panel (entries 40-46) with player details:
 * shirt number, name, position, skills, value/status, face indicator.
 *
 * in:  a5 = current menu entry
 * ================================================================ */
extern uint8_t _fundsBuffer[];

__attribute__((used, externally_visible))
void EditTacticsAfterDrawPlayerNames(MenuEntry *cur)
{
    uint16_t d7 = cur->ordinal - 0xB;  /* entry ordinal - 11 */

    EditTactics_ApplyPlayerRowHighlighting((int16_t)d7, cur);

    MenuEntry *entry41 = &_menuEntries[41];
    uint16_t d5; /* flag: 1 if slot 16 (no player details) */

    if (d7 == 0x10) {
        d5 = 1;
        setBgColor(entry41, BLUE_TO_BLACK_8);
    } else if (d7 < 0xB) {
        d5 = 0;
        if (d7 == 0) {
            setBgColor(entry41, YELLOW_GREEN_BLACK_14);
        } else {
            setBgColor(entry41, BRIGHT_BLUE_TO_BLUE_13);
        }
    } else {
        d5 = 0;
        setBgColor(entry41, BLUE_TO_PURPLE_11);
    }

    /* Get player info */
    TeamFileEx *teamBase = _chooseTacticsTeamPtr;
    uint8_t savedTactics = teamBase->team.tactics;
    teamBase->team.tactics = _editTacticsCurrentTactics.ballOutOfPlayTactics;

    uint16_t d7_slot, d1_fileIdx;
    uint8_t *playerPtr = GetPlayerInfoBreakPosition(teamBase, d7, &d7_slot, &d1_fileIdx);
    PlayerFile *pf = (PlayerFile *)(playerPtr + 76);

    teamBase = _chooseTacticsTeamPtr;
    teamBase->team.tactics = savedTactics;

    /* Check if player is injured/suspended. Use the DISPLAY query so the red
     * name shows even under the testers' rule (test_rules.h) (which forces plain
     * IsPlayerInjured to "available" for gameplay) — otherwise the name would
     * not match the drawn "3"/injury indicator (entry39, which reads the raw
     * cardsInjuries directly). */
    if (IsPlayerInjuredForDisplay((PlayerFile *)playerPtr, (int16_t)_inEditTactics)) {
        setBgColor(entry41, RED_10);
    }

    /* Check if this player is selected */
    if (d1_fileIdx == _editTacticsSelectedPlayerNameIndex) {
        setBgColor(entry41, ORANGE_TO_BROWN_6);
    }

    /* Check pair swap highlight */
    if ((int16_t)_editTacticsSelectedPairSlotIndex >= 0) {
        uint16_t pairSlot = _editTacticsSelectedPairSlotIndex + 1;
        if (d7_slot == pairSlot) {
            setBgColor(entry41, ORANGE_TO_BROWN_12);
        }
    }

    /* Set up detail entries (40-44) */
    MenuEntry *e = &_menuEntries[40];
    uint16_t entryY = cur->y;

    /* Entry 40: shirt number */
    e->isInvisible = 1;
    if (!d5) {
        e->fgWord = (uint16_t)(uint8_t)pf->shirtNumber;  /* ENTRY_NUMBER: write high word */
        e->isInvisible = 0;
    }
    e->y = entryY;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);  /* entry 41 */

    /* Entry 41: player name (surname) */
    uint8_t *surname = ExtractSurname((uint8_t *)pf->playerName, 0x800D);
    e->fg = (uintptr_t)surname;
    e->y = entryY;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);  /* entry 42 */

    /* Entry 42: position string */
    uint8_t posAndFace = pf->positionAndFace;
    uint16_t posIdx = ((uint16_t)posAndFace & PF_POSITION_MASK) >> 3;
    e->fg = (uintptr_t)_playerPositionsStringTable[posIdx / 4];
    e->y = entryY;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);  /* entry 43 */

    /* Entry 43: top skills */
    FormTopSkillsString((uint8_t *)e->fg, (PlayerFile *)playerPtr);
    e->y = entryY;
    e = (MenuEntry *)((uint8_t *)e + MENUENTRY_STRIDE);  /* entry 44 */

    /* Entry 44: value/status */
    e->isInvisible = 1;
    e->y = entryY;

    if (IsTeamNational(_chooseTacticsTeamPtr)) {
        InitPlayerValueDisplayStruct(playerPtr, (uint8_t *)e);
    } else {
        FormatPlayerValueOrStatusText(playerPtr, _fundsBuffer);
        e->fg = (uintptr_t)_fundsBuffer;
    }
    e->isInvisible = 0;

    /* Determine color for entry 44 based on player status.
     * Asm writes low nibble of stringFlags (offset 0x24 = MENUENTRY_STRINGFLAGS). */
    uint16_t colorVal;
    if ((uint8_t)pf->form == PLAYER_FORM_TRIALIST) {
        colorVal = 0xD;
    } else if ((uint8_t)pf->form == PLAYER_FORM_RESERVE) {
        colorVal = 1;
    } else if (pf->positionAndFace & PF_LOAN) {
        colorVal = 0xF;
    } else {
        colorVal = 2;
    }

    e->stringFlags = (e->stringFlags & 0xFFF0) | colorVal;

    DrawMultipleItems(6, &_menuEntries[39]);
}
