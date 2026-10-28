/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "game_state.h"
#include "shooting_practice.h"
#include "game_types.h"
#include "endian_helpers.h"
#include "display.h"    /* _screen_width, WIDE_MARGIN — center the in-match HUD in widescreen */
#include "dos_mode.h"   /* _gDosView: the DOS release places the match clock differently */

/* ---- External globals ---- */
extern uint16_t _isGoalAttempt;
extern uint16_t _showStats;
extern uint16_t _overlayActiveFlag;
extern uint16_t _statsTimer;
extern int16_t  _resultTimer;
extern uint16_t _showingUserStats;

extern uint16_t _gameSeconds;
extern uint32_t _gameTime;               /* BCD-encoded: byte3=ones, byte2=tens, byte1=hundreds */
extern uint16_t _secondsSwitchAccumulator;
extern uint16_t _copyTimeSprite;
extern Sprite   _currentTimeSprite;
#ifdef SWOS_SDL_BUILD
/* Widescreen 3rd score-panel tile per row — see sprite_data.c. */
extern Sprite   _gridUpperRowExtraSprite;
extern Sprite   _gridMiddleRowExtraSprite;
extern Sprite   _gridBottomRowExtraSprite;
#define HUD_EXTRA_TILE_VISIBLE(v) do {                     \
        _gridUpperRowExtraSprite.visible  = (v);           \
        _gridMiddleRowExtraSprite.visible = (v);           \
        _gridBottomRowExtraSprite.visible = (v);           \
    } while (0)
#else
#define HUD_EXTRA_TILE_VISIBLE(v) ((void)0)
#endif

extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;

extern uint8_t  _plOrdinalsTable[];      /* 32 bytes: indices 0-15 at offset 0, duplicated at offset 16 */
extern uint8_t  _plNamesLengthTable[];   /* 32 bytes: 16 entries x 2 bytes */
extern uint8_t  _plNamesLengthTableCopy[];

extern Sprite   _goal1TopSprite;
extern Sprite   _goal2BottomSprite;

/* ---- Result display sprite globals ---- */
extern Sprite   _resultOnHalftimeSprite;
extern Sprite   _resultAfterTheGameSprite;
extern Sprite   _gridUpperRowLeftSprite;
extern Sprite   _gridUpperRowRightSprite;
extern Sprite   _gridMiddleRowLeftSprite;
extern Sprite   _gridMiddleRowRightSprite;
extern Sprite   _gridBottomRowLeftSprite;
extern Sprite   _gridBottomRowRightSprite;
extern Sprite   _team1NameSprite;
extern Sprite   _team2NameSprite;
extern Sprite   _team1GoalsDigit2Sprite;
extern Sprite   _team1GoalsDigit1Sprite;
extern Sprite   _team2GoalsDigit2Sprite;
extern Sprite   _team2GoalsDigit1Sprite;
extern Sprite   _team1TotalGoalsDigit2Sprite;
extern Sprite   _team1TotalGoalsDigit1Sprite;
extern Sprite   _team2TotalGoalsDigit2Sprite;
extern Sprite   _team2TotalGoalsDigit1Sprite;
extern Sprite   _dashSprite;
extern Sprite   _team1ScorerSprite[8];
extern Sprite   _team2ScorerSprite[8];

extern uint16_t _team1ScorerSpritesWidths[];  /* 8 entries */
extern uint16_t _team2ScorerSpritesWidths[];  /* 8 entries */
extern uint16_t _team1TotalGoals;
extern uint16_t _team2TotalGoals;
extern uint16_t _team1GoalsDigit2;
extern uint16_t _team2GoalsDigit2;
extern uint16_t _team1GoalsDigit1;
extern uint16_t _team2GoalsDigit1;
extern uint16_t _team1NameLength;
extern uint16_t _dontShowScorers;
extern uint16_t _secondLeg;

extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern uint16_t _lastFrameTicks;
#if defined(RASTER_BARS) && !defined(SWOS_SDL_BUILD)
static uint16_t _rasterFps;           /* the clock's number in a -DRASTER_BARS build */
#endif
extern uint16_t _gameState;

extern ScorerInfo _team1Scorers[8];
extern ScorerInfo _team2Scorers[8];

/* ---- UpdateTime globals ---- */
extern uint16_t _timeVar;
extern uint16_t _timeDelta;
extern int16_t  _gsFramesPerGameSecond;   /* GS_54_49 clock reload — dos_gameplay.c */
extern int16_t  _gsEndGameCounter;        /* GS_55_50 endGameCounter reload — dos_gameplay.c */
extern int16_t  _gsResultHalftimeLength;  /* GS_275_250 result banner — dos_gameplay.c */
extern int16_t  _gsResultBreakLength;     /* GS_165_150 result banner — dos_gameplay.c */
extern uint16_t _gameStatePl;
extern uint16_t _playingPenalties;
extern int16_t  _extraTimeState;
extern int16_t  _penaltiesState;
extern uint16_t _endGameCounter;
extern uint16_t _playing2ndGame;
extern uint16_t _team1GoalsFirstLeg;
extern uint16_t _team2GoalsFirstLeg;
extern TeamGame *_winningTeamPtr;         /* pointer to winning team */
extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint16_t _statsTeam1GoalsCopy;
extern uint16_t _statsTeam2GoalsCopy;
extern TeamGeneralInfo *_lastTeamPlayed;
extern Sprite   _ballSprite;
extern uint16_t _currentPlayerIndicatorPosition;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

/* C-to-asm bridges */
extern void InitDisplaySprites(void);

/* Match state transitions (now in match_setup.c) */
extern void EndFirstHalf(void);
extern void EndFirstExtraTime(void);
extern void EndOfGame(void);
extern void StartFirstExtraTime(void);
extern void StartPenalties(void);

/* Already-ported functions used by UpdateTime */
extern void PlayEndGameWhistleSample(void);
extern void CopySprite(uint16_t srcIdx, uint16_t dstIdx, uint16_t xOff, uint16_t yOff);
extern void BlitSpritePlanar_Long_StridedDest(uint16_t sprIdx);
extern void BlitSpritePlanar_Word_StridedDest(uint16_t sprIdx);

/* ---- Sprite image index constants (NOT struct offsets) ---- */
#define SPR_BIG_0               173
#define SPR_SMALL_ZERO          8
#define SPR_SCROLL_ARROW_UP     183
#define SPR_8_MINS              214

#define SPR_LITTLE_PLAYER_GENERIC    214
#define SPR_PREMIERE_DIVISION_WINNER 215
#define SPR_INVITATION_TOURNAMENT    216
#define SPR_CUP_WINNER               217

/* ---- Game state constants ---- */
#define ST_GAME_IN_PROGRESS         100
#define ST_RESULT_ON_HALFTIME       25
#define ST_RESULT_AFTER_THE_GAME    26

/* gameTime byte accessors (big-endian 68k) */
#define GAMETIME_ONES     (((uint8_t *)&_gameTime)[3])
#define GAMETIME_TENS     (((uint8_t *)&_gameTime)[2])
#define GAMETIME_HUNDREDS (((uint8_t *)&_gameTime)[1])

/* ================================================================
 * ResetGoalAttempt  (SWOS2.s line 18697)
 * ================================================================ */
__attribute__((used, externally_visible))
void ResetGoalAttempt(void)
{
    _isGoalAttempt = 0;
}

/* ================================================================
 * AllowStatistics  (SWOS2.s line 18703)
 *
 * Enables statistics overlay display.
 * Negates resultTimer to make any visible result go away.
 * ================================================================ */
__attribute__((used, externally_visible))
void AllowStatistics(void)
{
    _showStats = 1;
    _overlayActiveFlag = 1;
    _statsTimer = 32000;
    _resultTimer = (int16_t)-_resultTimer;    /* neg.w */
}

/* ================================================================
 * StatisticsOff  (SWOS2.s line 18712)
 * ================================================================ */
__attribute__((used, externally_visible))
void StatisticsOff(void)
{
    _statsTimer = 0;
    _showingUserStats = 0;
    _overlayActiveFlag = 0;
}

/* ================================================================
 * ResetTime  (SWOS2.s line 13398)
 *
 * Clears game clock and sets the time sprite to the initial state.
 * ================================================================ */
__attribute__((used, externally_visible))
void ResetTime(void)
{
    _gameSeconds = 0;
    _gameTime = 0;
    _secondsSwitchAccumulator = 0;
    _copyTimeSprite = 1;
    _currentTimeSprite.imageIndex = SPR_8_MINS;
    _currentTimeSprite.visible = 0;
}

/* ================================================================
 * BumpPlayerLastPlayedHalfAtHalfStart  (SWOS2.s line 13685)
 *
 * Per-player: if not red-carded and not already at 2, set to 1.
 * ================================================================ */
static void bumpPlayerHalfStart(PlayerInfo *player)
{
    if ((uint8_t)player->numYellowCards >= 2)
        return;
    if (player->halfPlayed == 2)
        return;
    player->halfPlayed = 1;
}

/* ================================================================
 * BumpPlayersLastPlayedHalfAtHalfStart  (SWOS2.s line 13667)
 *
 * Iterates both teams' 11 players, marking half-start.
 * ================================================================ */
__attribute__((used, externally_visible))
void BumpPlayersLastPlayedHalfAtHalfStart(void)
{
    int16_t i;

    for (i = 0; i < 11; i++)
    {
        bumpPlayerHalfStart(&_topTeamInGame.players[i]);
        bumpPlayerHalfStart(&_bottomTeamInGame.players[i]);
    }
}

/* ================================================================
 * BumpPlayerLastPlayedHalfAtHalfEnd  (SWOS2.s line 13714)
 *
 * Per-player: if not red-carded and half-played is 1, set to 2.
 * ================================================================ */
static void bumpPlayerHalfEnd(PlayerInfo *player)
{
    if ((uint8_t)player->numYellowCards >= 2)
        return;
    if (player->halfPlayed != 1)
        return;
    player->halfPlayed = 2;
}

/* ================================================================
 * BumpPlayersLastPlayedHalfAtHalfEnd  (SWOS2.s line 13696)
 *
 * Iterates both teams' 11 players, marking half-end.
 * ================================================================ */
__attribute__((used, externally_visible))
void BumpPlayersLastPlayedHalfAtHalfEnd(void)
{
    int16_t i;

    for (i = 0; i < 11; i++)
    {
        bumpPlayerHalfEnd(&_topTeamInGame.players[i]);
        bumpPlayerHalfEnd(&_bottomTeamInGame.players[i]);
    }
}

/* ================================================================
 * CopyPlayerNameLengths  (SWOS2.s line 12308)
 *
 * Copies 16 entries (2 bytes each) from plNamesLengthTable to
 * plNamesLengthTableCopy. Also initializes plOrdinalsTable with
 * sequential indices 0-15 at both offset 0 and offset 16.
 * ================================================================ */
__attribute__((used, externally_visible))
void CopyPlayerNameLengths(void)
{
    uint16_t d0;

    for (d0 = 0; d0 < 16; d0++)
    {
        _plOrdinalsTable[d0 + 16] = (uint8_t)d0;
        _plOrdinalsTable[d0] = (uint8_t)d0;
        _plNamesLengthTableCopy[d0 * 2]     = _plNamesLengthTable[d0 * 2];
        _plNamesLengthTableCopy[d0 * 2 + 1] = _plNamesLengthTable[d0 * 2 + 1];
    }
}

/* ================================================================
 * ClearSaveFlagForGoalSprites  (SWOS2.s line 13375)
 *
 * Sets saveSprite=1 for goal1TopSprite and goal2BottomSprite.
 * ================================================================ */
__attribute__((used, externally_visible))
void ClearSaveFlagForGoalSprites(void)
{
    _goal1TopSprite.saveSprite = 1;
    _goal2BottomSprite.saveSprite = 1;
}

/* ================================================================
 * InitCurrentGoalHighlight  (SWOS2.s line 13290)
 *
 * Clears 19000 bytes at goalBasePtr, resets currentHilPtr,
 * clears replayState, sets throwInDeadVar=1.
 * ================================================================ */
extern uint8_t *_goalBasePtr;
extern uint8_t *_currentHilPtr;
extern uint16_t _replayState;
extern uint16_t _throwInDeadVar;

__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InitCurrentGoalHighlight(void)
{
    uint32_t *p = (uint32_t *)_goalBasePtr;
    int16_t i;
    for (i = 4749; i >= 0; i--)
        *p++ = 0;
    _currentHilPtr = _goalBasePtr;
    _replayState = 0;
    _throwInDeadVar = 1;
}

/* ================================================================
 * ResetResultSprites  (SWOS2.s line 13700)
 *
 * Hides all result/score display sprites and clears scorer data.
 * ================================================================ */
__attribute__((used, externally_visible))
void ResetResultSprites(void)
{
    int16_t i;
    Sprite *s1, *s2;
    ScorerInfo *sc1, *sc2;
    uint16_t *w1, *w2;

    _resultOnHalftimeSprite.visible = 0;
    _resultAfterTheGameSprite.visible = 0;

    _team1NameSprite.imageIndex = 0xC1;
    _team2NameSprite.imageIndex = 0xC2;

    _gridUpperRowLeftSprite.visible = 0;
    _gridUpperRowRightSprite.visible = 0;
    _gridMiddleRowLeftSprite.visible = 0;
    _gridMiddleRowRightSprite.visible = 0;
    _gridBottomRowLeftSprite.visible = 0;
    _gridBottomRowRightSprite.visible = 0;
    HUD_EXTRA_TILE_VISIBLE(0);
    _team1NameSprite.visible = 0;
    _team2NameSprite.visible = 0;
    _team1GoalsDigit2Sprite.visible = 0;
    _team1GoalsDigit1Sprite.visible = 0;
    _team1TotalGoalsDigit2Sprite.visible = 0;
    _team1TotalGoalsDigit1Sprite.visible = 0;
    _team2GoalsDigit2Sprite.visible = 0;
    _team2GoalsDigit1Sprite.visible = 0;
    _team2TotalGoalsDigit2Sprite.visible = 0;
    _team2TotalGoalsDigit1Sprite.visible = 0;
    _team1NameSprite.visible = 0;    /* redundant in original asm */
    _dashSprite.visible = 0;

    s1 = _team1ScorerSprite;
    s2 = _team2ScorerSprite;
    sc1 = _team1Scorers;
    sc2 = _team2Scorers;
    w1 = _team1ScorerSpritesWidths;
    w2 = _team2ScorerSpritesWidths;

    for (i = 7; i >= 0; i--)
    {
        s1->visible = 0;
        s2->visible = 0;
        sc1->shirtNum = 0;
        sc2->shirtNum = 0;
        *w1++ = 0;
        *w2++ = 0;
        s1++;
        s2++;
        sc1++;
        sc2++;
    }

    InitDisplaySprites();
}

/* ================================================================
 * ShowResult  (SWOS2.s line 13749)
 *
 * Timer-driven result display. Shows/hides score sprites,
 * positions them based on camera coordinates, updates per-frame.
 * ================================================================ */
__attribute__((used, externally_visible))
void ShowResult(void)
{
    int16_t d0, d1, d2, d7;
    Sprite *s;

    /* resultTimer == 0 -> nothing to do */
    if (_resultTimer == 0)
        return;

    /* resultTimer < 0 -> jump to hide phase */
    if (_resultTimer < 0)
        goto hide_result;

    /* --- timer init phase: map magic values to countdown values --- */
    if (_resultTimer == 30000)
    {
        _resultTimer = _gsResultHalftimeLength;   /* Amiga 250 / DOS 275 */
        goto show_sprites;
    }
    if (_resultTimer == 31000)
    {
        _resultTimer = _gsResultBreakLength;      /* Amiga 150 / DOS 165 */
        goto show_sprites;
    }
    if (_resultTimer == 32000)
    {
        _resultTimer = 29000;
        goto show_sprites;  /* was: goto loc_10B964 which falls through to show_sprites */
    }

    /* already in countdown phase -> skip to position update (loc_10BAB0) */
    goto countdown;

show_sprites:
    /* Show halftime/fulltime banner depending on gameState */
    if (_gameState == ST_RESULT_ON_HALFTIME)
        _resultOnHalftimeSprite.visible = 1;

    if (_gameState == ST_RESULT_AFTER_THE_GAME)
        _resultAfterTheGameSprite.visible = 1;

    /* Show aggregate/total score digits if secondLeg or dontShowScorers */
    if (_dontShowScorers || _secondLeg)
    {
        _team1TotalGoalsDigit2Sprite.visible = 1;
        d0 = (int16_t)_team1TotalGoals;
        if ((uint16_t)((uint32_t)(uint16_t)d0 / 10u) != 0)
            _team1TotalGoalsDigit1Sprite.visible = 1;

        _team2TotalGoalsDigit2Sprite.visible = 1;
        d0 = (int16_t)_team2TotalGoals;
        if ((uint16_t)((uint32_t)(uint16_t)d0 / 10u) != 0)
            _team2TotalGoalsDigit1Sprite.visible = 1;
    }

    /* Show grid, team names, goal digits, dash, scorer sprites */
    _gridUpperRowLeftSprite.visible = 1;
    _gridUpperRowRightSprite.visible = 1;
    _gridMiddleRowLeftSprite.visible = 1;
    _gridMiddleRowRightSprite.visible = 1;
    _gridBottomRowLeftSprite.visible = 1;
    _gridBottomRowRightSprite.visible = 1;
    /* 3rd tile only once the HUD box exceeds the 2x168 the base tiles cover. */
    /* Whether the tile is in the draw list is match state (sim_state.c: the list's
     * contents and order), so it follows the simulation draw width (display.h). A
     * narrower display than that draws it exactly over the right tile. */
    HUD_EXTRA_TILE_VISIBLE(SIM_HUD_WIDTH > 2 * 168 ? 1 : 0);
    _team1NameSprite.visible = 1;
    _team2NameSprite.visible = 1;
    _team1GoalsDigit2Sprite.visible = 1;

    if (_team1GoalsDigit1 != 0)
        _team1GoalsDigit1Sprite.visible = 1;

    _team2GoalsDigit2Sprite.visible = 1;

    if (_team2GoalsDigit1 != 0)
        _team2GoalsDigit1Sprite.visible = 1;

    _team1NameSprite.visible = 1;    /* redundant in original asm */
    _dashSprite.visible = 1;

    /* Team 1 scorer sprites -- break on width==0 or dontShowScorers */
    s = _team1ScorerSprite;
    for (d0 = 7; d0 >= 0; d0--)
    {
        if (_team1ScorerSpritesWidths[7 - d0] == 0)
            break;
        if (_dontShowScorers)
            break;
        s->visible = 1;
        s++;
    }

    /* Team 2 scorer sprites -- break on width==0 or dontShowScorers */
    s = _team2ScorerSprite;
    for (d0 = 7; d0 >= 0; d0--)
    {
        if (_team2ScorerSpritesWidths[7 - d0] == 0)
            break;
        if (_dontShowScorers)
            break;
        s->visible = 1;
        s++;
    }

    InitDisplaySprites();

countdown:
    /* Decrement timer by lastFrameTicks */
    _resultTimer -= (int16_t)_lastFrameTicks;
    if (_resultTimer > 0)
        goto position_sprites;
    /* resultTimer <= 0: fall through to check if we should trigger stats */

hide_result:
    /* If halftime or fulltime result, trigger stats timer */
    if (_gameState == ST_RESULT_ON_HALFTIME || _gameState == ST_RESULT_AFTER_THE_GAME)
        _statsTimer = 0x7D00;

    _resultTimer = 0;

    /* If sprites aren't visible, nothing to hide */
    if (_team1NameSprite.visible == 0)
        return;

    /* Hide all result sprites */
    _resultOnHalftimeSprite.visible = 0;
    _resultAfterTheGameSprite.visible = 0;
    _gridUpperRowLeftSprite.visible = 0;
    _gridUpperRowRightSprite.visible = 0;
    _gridMiddleRowLeftSprite.visible = 0;
    _gridMiddleRowRightSprite.visible = 0;
    _gridBottomRowLeftSprite.visible = 0;
    _gridBottomRowRightSprite.visible = 0;
    HUD_EXTRA_TILE_VISIBLE(0);
    _team1NameSprite.visible = 0;
    _team2NameSprite.visible = 0;
    _team1GoalsDigit2Sprite.visible = 0;
    _team1GoalsDigit1Sprite.visible = 0;
    _team1TotalGoalsDigit2Sprite.visible = 0;
    _team1TotalGoalsDigit1Sprite.visible = 0;
    _team2GoalsDigit2Sprite.visible = 0;
    _team2GoalsDigit1Sprite.visible = 0;
    _team2TotalGoalsDigit2Sprite.visible = 0;
    _team2TotalGoalsDigit1Sprite.visible = 0;
    _team1NameSprite.visible = 0;    /* redundant in original asm */
    _dashSprite.visible = 0;

    /* Hide scorer sprites */
    {
        Sprite *sa = _team1ScorerSprite;
        Sprite *sb = _team2ScorerSprite;
        for (d0 = 7; d0 >= 0; d0--)
        {
            sa->visible = 0;
            sb->visible = 0;
            sa++;
            sb++;
        }
    }

    InitDisplaySprites();
    return;

position_sprites:
    /* Calculate vertical offset based on how many scorers exist */
    d7 = 0;
    for (d1 = 7; d1 >= 0; d1--)
    {
        d0 = _team1ScorerSpritesWidths[7 - d1] + _team2ScorerSpritesWidths[7 - d1];
        if (d0 == 0)
            break;
        d7 -= 7;
    }
    if (d7 > (int16_t)0xFFF2)   /* -14 */
        d7 = (int16_t)0xFFF2;

    /* Widescreen: each HUD sprite below sets x = cameraX + screen_pos (so after
     * DrawSpriteInCameraView subtracts cameraX it lands at the fixed screen_pos). Adding
     * WIDE_MARGIN to every one shifts the whole score panel / clock into the centered 336
     * region, matching the menus. WIDE_MARGIN folds to 0 on Amiga / non-widescreen. */
    /* --- Position team1NameSprite --- */
    _team1NameSprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 136 - (int16_t)_team1NameLength;
    _team1NameSprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + SIM_RESULT_PANEL_Y + d7 + 10000;
    _team1NameSprite.z.s.i = 10000;

    /* d0 = base row Y for grid sprites */
    d0 = _team1NameSprite.y.s.i - 1015;

    /* Grid upper row */
    _gridUpperRowLeftSprite.x.s.i = Render_CameraX() + HUD_MARGIN;
    _gridUpperRowLeftSprite.y.s.i = d0;
    _gridUpperRowLeftSprite.z.s.i = 9000;
    _gridUpperRowRightSprite.x.s.i = Render_CameraX() + HUD_MARGIN + 168;
    _gridUpperRowRightSprite.y.s.i = d0;
    _gridUpperRowRightSprite.z.s.i = 9000;
#ifdef SWOS_SDL_BUILD
    _gridUpperRowExtraSprite.x.s.i = Render_CameraX() + HUD_MARGIN + HUD_WIDTH - 168;
    _gridUpperRowExtraSprite.y.s.i = d0;
    _gridUpperRowExtraSprite.z.s.i = 9000;
#endif

    d0 += 32;

    /* Grid middle row */
    _gridMiddleRowLeftSprite.x.s.i = Render_CameraX() + HUD_MARGIN;
    _gridMiddleRowLeftSprite.y.s.i = d0;
    _gridMiddleRowLeftSprite.z.s.i = 9000;
    _gridMiddleRowRightSprite.x.s.i = Render_CameraX() + HUD_MARGIN + 168;
    _gridMiddleRowRightSprite.y.s.i = d0;
    _gridMiddleRowRightSprite.z.s.i = 9000;
#ifdef SWOS_SDL_BUILD
    _gridMiddleRowExtraSprite.x.s.i = Render_CameraX() + HUD_MARGIN + HUD_WIDTH - 168;
    _gridMiddleRowExtraSprite.y.s.i = d0;
    _gridMiddleRowExtraSprite.z.s.i = 9000;
#endif

    d0 += 32;

    /* Grid bottom row */
    _gridBottomRowLeftSprite.x.s.i = Render_CameraX() + HUD_MARGIN;
    _gridBottomRowLeftSprite.y.s.i = d0;
    _gridBottomRowLeftSprite.z.s.i = 9000;
    _gridBottomRowRightSprite.x.s.i = Render_CameraX() + HUD_MARGIN + 168;
    _gridBottomRowRightSprite.y.s.i = d0;
    _gridBottomRowRightSprite.z.s.i = 9000;
#ifdef SWOS_SDL_BUILD
    _gridBottomRowExtraSprite.x.s.i = Render_CameraX() + HUD_MARGIN + HUD_WIDTH - 168;
    _gridBottomRowExtraSprite.y.s.i = d0;
    _gridBottomRowExtraSprite.z.s.i = 9000;
#endif

    /* --- Position team2NameSprite --- */
    _team2NameSprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 200;
    _team2NameSprite.y.s.i = _team1NameSprite.y.s.i;
    _team2NameSprite.z.s.i = 10000;

    /* --- Team 1 goals digit 2 (units) --- */
    _team1GoalsDigit2Sprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 151;
    _team1GoalsDigit2Sprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + (SIM_RESULT_PANEL_Y - 5) + d7 + 10000;
    _team1GoalsDigit2Sprite.z.s.i = 10000;
    _team1GoalsDigit2Sprite.imageIndex = SPR_BIG_0 + _team1GoalsDigit2;

    /* --- Team 1 goals digit 1 (tens) --- */
    if (_team1GoalsDigit1Sprite.visible)
    {
        _team1GoalsDigit1Sprite.x.s.i = _team1GoalsDigit2Sprite.x.s.i - 12;
        _team1GoalsDigit1Sprite.y.s.i = _team1GoalsDigit2Sprite.y.s.i;
        _team1GoalsDigit1Sprite.z.s.i = _team1GoalsDigit2Sprite.z.s.i;
        _team1GoalsDigit1Sprite.imageIndex = SPR_BIG_0 + _team1GoalsDigit1;
    }

    /* --- Team 1 total goals (aggregate) --- */
    if (_team1TotalGoalsDigit2Sprite.visible)
    {
        d1 = (int16_t)_team1TotalGoals;
        d2 = (int16_t)((uint32_t)(uint16_t)d1 / 10u);
        d1 = (int16_t)((uint32_t)(uint16_t)d1 % 10u);

        _team1TotalGoalsDigit2Sprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 158;
        if (d1 == 1)
            _team1TotalGoalsDigit2Sprite.x.s.i += 4;
        _team1TotalGoalsDigit2Sprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + (SIM_RESULT_PANEL_Y + 17) + d7 + 10002;
        _team1TotalGoalsDigit2Sprite.z.s.i = 10002;
        _team1TotalGoalsDigit2Sprite.imageIndex = SPR_SMALL_ZERO + (uint16_t)d1;

        if (_team1TotalGoalsDigit1Sprite.visible)
        {
            _team1TotalGoalsDigit1Sprite.x.s.i = _team1TotalGoalsDigit2Sprite.x.s.i - 6;
            if (d2 == 1)
                _team1TotalGoalsDigit1Sprite.x.s.i += 4;
            _team1TotalGoalsDigit1Sprite.y.s.i = _team1TotalGoalsDigit2Sprite.y.s.i - 1;
            _team1TotalGoalsDigit1Sprite.z.s.i = _team1TotalGoalsDigit2Sprite.z.s.i - 1;
            _team1TotalGoalsDigit1Sprite.imageIndex = SPR_SMALL_ZERO + (uint16_t)d2;
        }
    }

    /* --- Team 2 goals digit 2 (units) --- */
    _team2GoalsDigit2Sprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 173;
    _team2GoalsDigit2Sprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + (SIM_RESULT_PANEL_Y - 5) + d7 + 10000;
    _team2GoalsDigit2Sprite.z.s.i = 10000;
    _team2GoalsDigit2Sprite.imageIndex = SPR_SCROLL_ARROW_UP + _team2GoalsDigit2;

    /* --- Team 2 goals digit 1 (tens) --- */
    if (_team2GoalsDigit1Sprite.visible)
    {
        _team2GoalsDigit1Sprite.x.s.i = _team2GoalsDigit2Sprite.x.s.i;
        _team2GoalsDigit2Sprite.x.s.i += 12;    /* original: addi.w #12,SPRITE_X(a0) on digit2 */
        _team2GoalsDigit1Sprite.y.s.i = _team2GoalsDigit2Sprite.y.s.i;
        _team2GoalsDigit1Sprite.z.s.i = _team2GoalsDigit2Sprite.z.s.i;
        _team2GoalsDigit1Sprite.imageIndex = SPR_SCROLL_ARROW_UP + _team2GoalsDigit1;
    }

    /* --- Team 2 total goals (aggregate) --- */
    if (_team2TotalGoalsDigit2Sprite.visible)
    {
        d1 = (int16_t)_team2TotalGoals;
        d2 = (int16_t)((uint32_t)(uint16_t)d1 / 10u);
        d1 = (int16_t)((uint32_t)(uint16_t)d1 % 10u);

        _team2TotalGoalsDigit2Sprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 171;
        _team2TotalGoalsDigit2Sprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + (SIM_RESULT_PANEL_Y + 17) + d7 + 10002;
        _team2TotalGoalsDigit2Sprite.z.s.i = 10002;
        _team2TotalGoalsDigit2Sprite.imageIndex = SPR_SMALL_ZERO + (uint16_t)d1;

        if (_team2TotalGoalsDigit1Sprite.visible)
        {
            /* asm: copy digit2.X -> digit1.X, then digit2.X += 6 */
            _team2TotalGoalsDigit1Sprite.x.s.i = _team2TotalGoalsDigit2Sprite.x.s.i;
            _team2TotalGoalsDigit2Sprite.x.s.i += 6;
            if (d2 == 1)
                _team2TotalGoalsDigit2Sprite.x.s.i -= 4;
            _team2TotalGoalsDigit1Sprite.y.s.i = _team2TotalGoalsDigit2Sprite.y.s.i;
            _team2TotalGoalsDigit1Sprite.z.s.i = _team2TotalGoalsDigit2Sprite.z.s.i;
            _team2TotalGoalsDigit1Sprite.y.s.i -= 1;
            _team2TotalGoalsDigit1Sprite.z.s.i -= 1;
            _team2TotalGoalsDigit1Sprite.imageIndex = SPR_SMALL_ZERO + (uint16_t)d2;
        }
    }

    /* --- Dash sprite --- */
    _dashSprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 165;
    _dashSprite.y.s.i = Render_CameraY() + RENDER_EXTRA_Y + (SIM_RESULT_PANEL_Y + 3) + d7 + 10000;
    _dashSprite.z.s.i = 10000;

    /* --- Team 1 scorer sprites --- */
    {
        Sprite *spr = _team1ScorerSprite;
        uint16_t *widths = _team1ScorerSpritesWidths;
        d1 = (SIM_RESULT_PANEL_Y + 17) + d7;
        for (d0 = 7; d0 >= 0; d0--)
        {
            if (*widths == 0)
                break;
            spr->x.s.i = Render_CameraX() + WIDE_MARGIN + 136 - (int16_t)*(widths);
            spr->y.s.i = Render_CameraY() + RENDER_EXTRA_Y + d1 + 10000;
            spr->z.s.i = 10000;
            d1 += 7;
            widths++;
            spr++;
        }
    }

    /* --- Team 2 scorer sprites --- */
    {
        Sprite *spr = _team2ScorerSprite;
        uint16_t *widths = _team2ScorerSpritesWidths;
        d1 = (SIM_RESULT_PANEL_Y + 17) + d7;
        for (d0 = 7; d0 >= 0; d0--)
        {
            if (*widths == 0)
                break;
            spr->x.s.i = Render_CameraX() + WIDE_MARGIN + 200;
            spr->y.s.i = Render_CameraY() + RENDER_EXTRA_Y + d1 + 10000;
            spr->z.s.i = 10000;
            d1 += 7;
            widths++;
            spr++;
        }
    }

    /* --- Position halftime/fulltime result banners --- */
    d0 = _gridUpperRowLeftSprite.y.s.i + 990;
    _resultOnHalftimeSprite.x.s.i = Render_CameraX() + WIDE_MARGIN + 168;
    _resultOnHalftimeSprite.y.s.i = d0;
    _resultOnHalftimeSprite.z.s.i = 10000;
    _resultAfterTheGameSprite.x.s.i = _resultOnHalftimeSprite.x.s.i;
    _resultAfterTheGameSprite.y.s.i = _resultOnHalftimeSprite.y.s.i;
    _resultAfterTheGameSprite.z.s.i = _resultOnHalftimeSprite.z.s.i;
}


/* ================================================================
 * UpdateTime  (SWOS2.s line 13241)
 *
 * Core game time management:
 * - Tracks minutes/seconds using BCD gameTime
 * - Handles half-time, full-time, extra time, penalties
 * - Builds and positions the time display sprite
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateTime(void)
{
    uint16_t d0u;

    /* Training lasts until Escape. Bypass the entire match clock, including
     * stoppage-time expiry, and remove the minutes from the display list. */
    if (ShootingPractice_IsActive()) {
        if (_currentTimeSprite.visible) {
            _currentTimeSprite.visible = 0;
            InitDisplaySprites();
        }
        return;
    }

    /* --- Timer/visibility section --- */
    /* Original asm always sets timeVar=31000, then immediately converts to 150.
     * The intermediate timer countdown code (sub.w d0,timeVar; bmi/bne) is
     * unreachable due to an unconditional bra.w time_invisible after loading
     * lastFrameTicks. We replicate the reachable path only. */
    _timeVar = 31000;

    /* timeVar is always 31000 here -> convert to 150 */
    _timeVar = 150;

    /* Make time sprite visible if not already */
    if (_currentTimeSprite.visible == 0)
    {
        _currentTimeSprite.visible = 1;
        InitDisplaySprites();
    }

    /* --- Time update section (time_invisible) --- */

    /* If gameSeconds is negative -> last minute countdown in progress */
    if ((int16_t)_gameSeconds < 0)
        goto last_minute_in_progress;

    /* Only update time during normal gameplay (not penalties) */
    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        goto no_update_to_time;
    if (_playingPenalties)
        goto no_update_to_time;

    /* Accumulate time delta */
    _secondsSwitchAccumulator -= _timeDelta;
    if ((int16_t)_secondsSwitchAccumulator >= 0)
        goto no_update_to_time;

    /* GS_54_49: frames per in-game second — Amiga 49, DOS 54 (set per style by
     * ApplyGameStyle/dos_gameplay.c). The whole match-clock cadence keys off this. */
    _secondsSwitchAccumulator += (uint16_t)_gsFramesPerGameSecond;
    _gameSeconds++;

    if (_gameSeconds != 60)
        goto no_update_to_time;

    /* Minute boundary: increment BCD gameTime */
    _gameSeconds = 0;
    GAMETIME_ONES++;
    if (GAMETIME_ONES >= 10)
    {
        GAMETIME_ONES = 0;
        GAMETIME_TENS++;
        if (GAMETIME_TENS >= 10)
        {
            GAMETIME_TENS = 0;
            GAMETIME_HUNDREDS++;
        }
    }

    /* --- check_game_end --- */
check_game_end:
    /* Minute 1: mark half-start.
     * gameTime is written byte-by-byte via the GAMETIME_* macros (m68k BCD
     * layout: byte[1]=hundreds, byte[2]=tens, byte[3]=ones), but compared
     * here as a uint32_t. The constants below are in the m68k BE form
     * (e.g. 0x900 = minute 90); HOST_TO_BE32 swaps them on LE so the
     * uint32_t comparison sees the correct byte pattern. */
    if (_gameTime == HOST_TO_BE32(0x00000001))
        BumpPlayersLastPlayedHalfAtHalfStart();

    /* Minute 46: mark second-half start */
    if (_gameTime == HOST_TO_BE32(0x00000406))
        BumpPlayersLastPlayedHalfAtHalfStart();

    /* Minute 45: half-time */
    if (_gameTime == HOST_TO_BE32(0x00000405))
    {
        if ((int16_t)_gameSeconds >= 0)
            goto last_cycle;
        _gameSeconds = 0;
        PlayEndGameWhistleSample();
        EndFirstHalf();
        BumpPlayersLastPlayedHalfAtHalfEnd();
        goto not_end_of_second_extra;
    }

    /* Minute 90: full-time */
    if (_gameTime == HOST_TO_BE32(0x00000900))
    {
        if ((int16_t)_gameSeconds >= 0)
            goto last_cycle;
        _gameSeconds = 0;
        PlayEndGameWhistleSample();
        BumpPlayersLastPlayedHalfAtHalfEnd();
        _statsTeam1GoalsCopy = _statsTeam1Goals;
        _statsTeam2GoalsCopy = _statsTeam2Goals;

        d0u = _team1TotalGoals;
        if (d0u != _team2TotalGoals)
            goto not_draw;

        /* Tied at 90 minutes */
        if (_secondLeg && _playing2ndGame == 1)
        {
            /* Away goals: each side's goals on the other's ground count twice --
             * team 2's this leg, team 1's in the first (SWOS2.s loc_10B552:
             * d0 = 2*statsTeam2Goals + team2GoalsFirstLeg,
             * d1 = 2*team1GoalsFirstLeg + statsTeam1Goals). */
            uint16_t team2adj = (uint16_t)(_statsTeam2Goals * 2 + _team2GoalsFirstLeg);
            uint16_t team1adj = (uint16_t)(_team1GoalsFirstLeg * 2 + _statsTeam1Goals);
            if (team1adj != team2adj)
            {
                /* Away goals decide */
                if (team1adj > team2adj)
                    goto team1_wins;
                else
                    goto team2_wins;
            }
            /* Still tied -> fall through to extra time check */
        }

        /* Check extra time */
        if (_extraTimeState)
        {
            _extraTimeState = (int16_t)0xFFFF;
            StartFirstExtraTime();
            goto not_end_of_second_extra;
        }
        /* Check penalties */
        if (_penaltiesState)
        {
            _penaltiesState = (int16_t)0xFFFF;
            StartPenalties();
            goto not_end_of_second_extra;
        }
        /* Draw with no tiebreaker -> clear winner, end game */
        _winningTeamPtr = (TeamGame *)0;
        EndOfGame();
        goto not_end_of_second_extra;
    }

    /* Minute 105: end of first extra time */
    if (_gameTime == HOST_TO_BE32(0x00010005))
    {
        if ((int16_t)_gameSeconds >= 0)
            goto last_cycle;
        _gameSeconds = 0;
        PlayEndGameWhistleSample();
        EndFirstExtraTime();
        goto not_end_of_second_extra;
    }

    /* Minute 120: end of second extra time */
    if (_gameTime == HOST_TO_BE32(0x00010200))
    {
        if ((int16_t)_gameSeconds >= 0)
            goto last_cycle;
        _gameSeconds = 0;
        PlayEndGameWhistleSample();

        d0u = _team1TotalGoals;
        if (d0u != _team2TotalGoals)
            goto not_draw;

        /* Tied after extra time */
        if (_secondLeg && _playing2ndGame)
        {
            uint16_t team2adj = (uint16_t)(_statsTeam2Goals * 2 + _team2GoalsFirstLeg);
            uint16_t team1adj = (uint16_t)(_team1GoalsFirstLeg * 2 + _statsTeam1Goals);
            if (team1adj != team2adj)
            {
                if (team1adj > team2adj)
                    goto team1_wins;
                else
                    goto team2_wins;
            }
        }
        /* Still tied -> check penalties */
        if (_penaltiesState)
        {
            _penaltiesState = (int16_t)0xFFFF;
            StartPenalties();
            goto not_end_of_second_extra;
        }
        /* No penalties -> draw, end game */
        _winningTeamPtr = (TeamGame *)0;
        EndOfGame();
        goto not_end_of_second_extra;
    }

    goto not_end_of_second_extra;

not_draw:
    /* d0u has team1TotalGoals, compare with team2TotalGoals */
    if (d0u > _team2TotalGoals)
        goto team1_wins;
team2_wins:
    _winningTeamPtr = &_bottomTeamInGame;
    goto end_game;
team1_wins:
    _winningTeamPtr = &_topTeamInGame;
end_game:
    EndOfGame();
    goto not_end_of_second_extra;

last_cycle:
    _gameSeconds = 0xFFFF;       /* negative = last minute in progress */
    _endGameCounter = (uint16_t)_gsEndGameCounter;   /* GS_55_50: 50 Amiga / 55 DOS */
    goto no_update_to_time;

last_minute_in_progress:
    _endGameCounter -= _lastFrameTicks;
    if ((int16_t)_endGameCounter < 0)
        goto check_game_end;

    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        goto last_cycle;

    {
        int16_t ballY = _ballSprite.y.s.i;

        /* Ball must be between penalty areas (216 < y < 682) */
        if (ballY <= 216)
            goto last_cycle;
        if (ballY >= 682)
            goto last_cycle;

        /* Check which half the ball is in relative to center (449) */
        if (ballY <= 449)
        {
            /* Upper half: if bottom team has ball, extend time */
            if (_lastTeamPlayed == &_bottomTeamData)
                goto last_cycle;
        }
        else
        {
            /* Lower half: if top team has ball, extend time */
            if (_lastTeamPlayed == &_topTeamData)
                goto last_cycle;
        }
    }

no_update_to_time:
#if defined(RASTER_BARS) && !defined(SWOS_SDL_BUILD)
    /* -DRASTER_BARS (include/raster_bars.h): the clock shows the FRAME RATE instead of the
     * minutes -- "50mins." is a solid 50 fps, "25mins." a second in which frames were
     * missed. Frames completed per 50 vertical blanks, so one slow frame in a second
     * shows; rebuilt only when the number changes, like the clock. */
    {
        static uint16_t s_fpsFrames, s_fpsTicks, s_fpsShown = 0xFFFF;
        s_fpsFrames++;
        s_fpsTicks = (uint16_t)(s_fpsTicks + _lastFrameTicks);
        if (s_fpsTicks >= 50) {
            uint16_t fps = (uint16_t)((uint32_t)s_fpsFrames * 50u / s_fpsTicks);
            if (fps > 99) fps = 99;
            s_fpsFrames = s_fpsTicks = 0;
            if (fps != s_fpsShown) {
                s_fpsShown = fps;
                _rasterFps = fps;
                _copyTimeSprite = 1;
            }
        }
    }
#endif
    /* Check if time sprite needs rebuilding */
    if (!_copyTimeSprite)
        goto set_time_sprite_x;

not_end_of_second_extra:
    {
        /* Build time sprite from gameTime digits */
        uint16_t xOff = 0;
        uint16_t yOff = 0;
        uint16_t sprIdx;
        uint16_t digitSprite;
#if defined(RASTER_BARS) && !defined(SWOS_SDL_BUILD)
        uint8_t dHundreds = 0, dTens = (uint8_t)(_rasterFps / 10), dOnes = (uint8_t)(_rasterFps % 10);
#else
        uint8_t dHundreds = GAMETIME_HUNDREDS, dTens = GAMETIME_TENS, dOnes = GAMETIME_ONES;
#endif

        if (dHundreds != 0)
        {
            /* 3-digit time (100+) */
            sprIdx = SPR_INVITATION_TOURNAMENT;
            xOff = 5;       /* width for the hundreds digit (always 1) */
        }
        else if (dTens != 0)
        {
            /* 2-digit time */
            sprIdx = SPR_PREMIERE_DIVISION_WINNER;
        }
        else
        {
            /* 1-digit time */
            sprIdx = SPR_LITTLE_PLAYER_GENERIC;
            goto copy_last_digit;
        }

        /* Copy tens digit */
        digitSprite = SPR_CUP_WINNER + dTens;
        CopySprite(digitSprite, sprIdx, xOff, yOff);
        xOff += 6;

    copy_last_digit:
        /* Copy ones digit */
        digitSprite = SPR_CUP_WINNER + dOnes;
        CopySprite(digitSprite, sprIdx, xOff, yOff);

        /* Set sprite image index */
        _currentTimeSprite.imageIndex = sprIdx;

        /* Blit the assembled time sprite */
        if (sprIdx == SPR_INVITATION_TOURNAMENT)
            BlitSpritePlanar_Long_StridedDest(sprIdx);
        else
            BlitSpritePlanar_Word_StridedDest(sprIdx);

        _copyTimeSprite = 0;
    }

set_time_sprite_x:
    {
        /* Position time sprite relative to camera */
        _currentTimeSprite.x.s.i = Render_CameraX() + HUD_MARGIN + 0x14;
        _currentTimeSprite.y.s.i = Render_CameraY() + 0x2721;   /* top-anchored */

        /* The DOS release moves the clock 16 rows up and 6 columns left of
         * where the Amiga puts it, and the Amiga has no such adjustment --
         * swos.asm UpdateTime @@set_time_sprite_y has the two `sub`s that
         * SWOS2.s loc_10B77A simply does not. Without them the clock sat 16
         * rows too low in the DOS view, over the pitch rather than above it;
         * reported by a tester against a screenshot of the real DOS game. */
        if (_gDosView) {
            _currentTimeSprite.y.s.i -= 16;
            _currentTimeSprite.x.s.i -= 6;
        }

        /* If player indicator is at position 4, shift time down by 8 */
        if (_currentPlayerIndicatorPosition == 4)
            _currentTimeSprite.y.s.i += 8;

        _currentTimeSprite.z.s.i = 10000;
    }
}
