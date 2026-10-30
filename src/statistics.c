/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "statistics.h"
#include "translations.h"
#include "comp_identity.h"
#include "game_types.h"
#include "world.h"
#include "menu_system.h"
#include "play_match_menu.h"
#include "input_text.h"
#include "endian_helpers.h"
#include "dos_mode.h"      /* _gDosView: the DOS release lays this panel out for 320x200 */
#include "display.h"    /* _screen_width, WIDE_MARGIN — center the stats overlay in widescreen */
#include "auto_skip_debug.h"

/* ---- External globals ---- */
extern uint16_t _showStats;
extern uint16_t _overlayActiveFlag;
extern uint16_t _statsTimer;
extern uint16_t _showingUserStats;
extern uint16_t _isGoalAttempt;

extern uint16_t _gameStatePl;
extern uint16_t _playingPenalties;
extern uint16_t _lastFrameTicks;
extern uint16_t _fireBlocked;
extern uint16_t _trainingGame;

extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;

extern TeamGeneralInfo *_lastTeamPlayed;
extern TeamGeneralInfo  _topTeamData;
extern TeamGeneralInfo  _bottomTeamData;
extern TeamGame         _topTeamInGame;
extern TeamGame         _bottomTeamInGame;

extern Sprite      _ballSprite;
extern uint16_t _ballInGoalkeeperArea;
extern uint16_t _strikeDestX;

extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint8_t  _statsNumber[];      /* 6-byte buffer in asm data section */

extern uint8_t  _bigCharsTable[];

/* ---- External string constants ---- */
extern uint8_t  _aMATCHSTATS[];      /* "M A T C H   S T A T S" */
extern uint8_t  _aGoals[];           /* "GOALS" */
extern uint8_t  _aPossession[];      /* "POSSESSION" */
extern uint8_t  _aMinus[];           /* "-" */
extern uint8_t  _aGoalAttempts[];    /* "GOAL ATTEMPTS" */
extern uint8_t  _aOnTarget[];        /* "ON TARGET" */
extern uint8_t  _aCornersWon[];      /* "CORNERS WON" */
extern uint8_t  _aFoulsConceded[];   /* "FOULS CONCEDED" */
extern uint8_t  _aBookings[];        /* "BOOKINGS" */
extern uint8_t  _aSendingsOff[];     /* "SENDINGS OFF" */

/* ---- External functions ---- */
extern void Player1StatusProc(void);
extern void Player2StatusProc(void);
extern void StatisticsOff(void);
extern uint16_t DrawMenuTextCentered(uint16_t x, uint16_t y, uint16_t color,
                                      uint8_t *text, uint8_t *charsTable);
extern void Int2Ascii(uint16_t value, uint8_t *outBuf);
extern void DarkenRectangle(uint16_t sprIdx, uint16_t x, uint16_t y);

/* ---- Constants ---- */
#define SPR_SQUARE_GRID     1093


/* ================================================================
 * DrawGrid  (SWOS2.s line 18750)
 *
 * Draws darkened grid background using 16x16 tiles.
 * d1=x, d2=y, d3=width, d4=height
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawGrid(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t cols = (width >> 4) - 1;
    uint16_t rows = (height >> 4) - 1;
    uint16_t origX = x;
    int16_t r, c;
    uint16_t cx, cy;

    cy = y;
    for (r = (int16_t)rows; r >= 0; r--)
    {
        cx = origX;
        for (c = (int16_t)cols; c >= 0; c--)
        {
            DarkenRectangle(SPR_SQUARE_GRID, cx, cy);
            cx += 16;
        }
        cy += 16;
    }
}


/* ================================================================
 * WriteStatsNumber  (SWOS2.s line 18702)
 *
 * Converts number to ASCII and draws it centered at (x, y).
 * Uses statsNumber as scratch buffer.
 * ================================================================ */
static void writeStatsNumber(uint16_t number, uint16_t x, uint16_t y,
                             uint16_t color, uint8_t *charTable)
{
    Int2Ascii(number, _statsNumber);
    DrawMenuTextCentered(x, y, color, _statsNumber, charTable);
}


/* ================================================================
 * WriteStatsPercent  (SWOS2.s line 18723)
 *
 * Converts number to ASCII, appends '%', draws centered.
 * ================================================================ */
static void writeStatsPercent(uint16_t number, uint16_t x, uint16_t y,
                              uint16_t color, uint8_t *charTable)
{
    uint8_t *p;

    Int2Ascii(number, _statsNumber);

    /* Find end of string (Int2Ascii leaves pointer at terminator via a1) */
    p = _statsNumber;
    while (*p)
        p++;
    *p++ = '%';
    *p = 0;

    DrawMenuTextCentered(x, y, color, _statsNumber, charTable);
}


/* ================================================================
 * DrawStatistics  (SWOS2.s line 18483)
 *
 * Renders the full match statistics overlay:
 * - Grid backgrounds
 * - Team names
 * - Goals, possession, goal attempts, on target,
 *   corners won, fouls conceded, bookings, sendings off
 * ================================================================ */
__attribute__((used, externally_visible))
void DrawStatistics(void)
{
    TeamGeneralInfo *topInfo;    /* team stats pointer for top/first team */
    TeamGeneralInfo *botInfo;    /* team stats pointer for bottom/second team */
    TeamStatisticsData *a5;      /* stats for top team */
    TeamStatisticsData *a6;      /* stats for bottom team */
    uint8_t *charTable = _bigCharsTable;
    uint16_t color = 2;
    uint16_t y;
    uint16_t d7;

    /* Widescreen: the stats overlay is laid out on 336-frame coords (the DrawGrid /
     * DrawMenuTextCentered / writeStatsNumber x's below). Shift the
     * draw base by WIDE_MARGIN so the whole panel centers, same as the menus; restored on
     * exit. WIDE_MARGIN folds to 0 on Amiga / non-widescreen (no shift). */
    const int saved_x_offset = _menu_draw_x_offset;
    const int dosv = (_gDosView != 0);
    /* Centre the panel against the width it was laid out for. The DOS layout below
     * is already 320-wide (rows 16..304, centred on 160), so in the DOS view the
     * margin is measured from 320, not from 336 -- WIDE_MARGIN there is -8, which
     * put the board 8 pixels left of centre (ReportBot 0010). The result scoreboard
     * is the other way round: swos.asm writes its X's 8 less than SWOS2.s (128 v 136,
     * 157 v 165), so WIDE_MARGIN's -8 is exactly right for it. */
    _menu_draw_x_offset = (_screen_width - (dosv ? DOS_VIEW_WIDTH : SCREEN_WIDTH)) / 2;

    /* The DOS release lays this panel out for its 320x200 screen, and the
     * difference is not a margin: every row is 16 higher, the grids are 16
     * narrower, and the centre and right columns pull in. The decisive number is
     * that the DOS rows start at 39, and 39 + 8*18 + 16 = 199 -- SENDINGS OFF
     * ends exactly on the last line of a 200-line screen. Ours started at 55 and
     * ran to 215, which fits 272 and falls off 200: reported as the stats board
     * being cut off in DOS mode (ReportBot 0007).
     *
     * swos.asm DrawStatistics against SWOS2.s:22799. The left column is 80 in
     * both releases, which is why it stays a literal below. */
    const uint16_t kTitleGridY = dosv ?  19 :  35;
    const uint16_t kTitleGridW = dosv ? 160 : 176;
    const uint16_t kRowY0      = dosv ?  39 :  55;
    const uint16_t kRowW       = dosv ? 288 : 304;
    const uint16_t kTitleTextY = dosv ?  23 :  39;
    const uint16_t kColCentre  = dosv ? 160 : 168;
    const uint16_t kColRight   = dosv ? 240 : 256;
    /* ---- layout constants above; no raw coordinates past this line ---- */

    /* Draw the title grid (x 80, kTitleGridY, kTitleGridW wide, 16 tall) */
    DrawGrid(80, kTitleGridY, kTitleGridW, 16);

    /* Draw the 9 data rows from kRowY0, each kRowW wide, 16 tall,
     * spaced 18 apart. (1 title row + 9 data rows = 10 DrawGrid
     * calls total, matching SWOS2.s line 22799-22850. The previous
     * code had a 10th data row that left a visible empty band below
     * SENDINGS OFF.) Rows host: team names, GOALS, POSSESSION, GOAL
     * ATTEMPTS, ON TARGET, CORNERS WON, FOULS CONCEDED, BOOKINGS,
     * SENDINGS OFF. */
    y = kRowY0;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);
    y += 18;
    DrawGrid(16, y, kRowW, 16);

    /* Determine team order: topInfo = team whose inGameTeamPtr matches topTeamInGame */
    topInfo = &_topTeamData;
    botInfo = &_bottomTeamData;
    if (topInfo->inGameTeamPtr != &_topTeamInGame)
    {
        /* Swap */
        TeamGeneralInfo *tmp = topInfo;
        topInfo = botInfo;
        botInfo = tmp;
    }
    a5 = topInfo->teamStatsPtr;
    a6 = botInfo->teamStatsPtr;

    /* Title: "M A T C H   S T A T S" */
    y = kTitleTextY;
    DrawMenuTextCentered(kColCentre, y, color, _aMATCHSTATS, charTable);

    /* Team names */
    y += 20;
    {
        uint16_t topX = 80;
        if (_trainingGame)
            topX += 5;
        DrawMenuTextCentered(topX, y, color, (uint8_t *)_topTeamInGame.teamName, charTable);
    }
    {
        uint16_t botX = kColRight;
        if (_trainingGame)
            botX -= 5;
        DrawMenuTextCentered(botX, y, color, (uint8_t *)_bottomTeamInGame.teamName, charTable);
    }

    /* GOALS */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aGoals, charTable);
    writeStatsNumber(_statsTeam1Goals, 80, y, color, charTable);
    writeStatsNumber(_statsTeam2Goals, kColRight, y, color, charTable);

    /* POSSESSION */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aPossession, charTable);
    d7 = (uint16_t)a5->ballPossession + (uint16_t)a6->ballPossession;
    if (d7 == 0)
    {
        DrawMenuTextCentered(80, y, color, _aMinus, charTable);
        DrawMenuTextCentered(kColRight, y, color, _aMinus, charTable);
    }
    else
    {
        uint16_t pct = (uint16_t)(((uint32_t)(uint16_t)a5->ballPossession * 100u) / d7);
        writeStatsPercent(pct, 80, y, color, charTable);
        writeStatsPercent((uint16_t)(100 - pct), kColRight, y, color, charTable);
    }

    /* GOAL ATTEMPTS */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aGoalAttempts, charTable);
    writeStatsNumber((uint16_t)a5->goalAttempts, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->goalAttempts, kColRight, y, color, charTable);

    /* ON TARGET */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aOnTarget, charTable);
    writeStatsNumber((uint16_t)a5->onTarget, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->onTarget, kColRight, y, color, charTable);

    /* CORNERS WON */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aCornersWon, charTable);
    writeStatsNumber((uint16_t)a5->cornersWon, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->cornersWon, kColRight, y, color, charTable);

    /* FOULS CONCEDED */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aFoulsConceded, charTable);
    writeStatsNumber((uint16_t)a5->foulsConceded, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->foulsConceded, kColRight, y, color, charTable);

    /* BOOKINGS */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aBookings, charTable);
    writeStatsNumber((uint16_t)a5->bookings, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->bookings, kColRight, y, color, charTable);

    /* SENDINGS OFF */
    y += 18;
    DrawMenuTextCentered(kColCentre, y, color, _aSendingsOff, charTable);
    writeStatsNumber((uint16_t)a5->sendingsOff, 80, y, color, charTable);
    writeStatsNumber((uint16_t)a6->sendingsOff, kColRight, y, color, charTable);

    _menu_draw_x_offset = saved_x_offset;
}


/* ================================================================
 * UpdateAndDrawStatistics  (SWOS2.s line 18356)
 *
 * Main statistics update loop:
 * - Tracks shot attempts and on-target shots
 * - Manages stats overlay timer
 * - Draws stats when active
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateAndDrawStatistics(void)
{
    /* If showStats just set, trigger user stats display */
    if (_showStats)
    {
        _showStats = 0;
        _showingUserStats = 1;
        _statsTimer = 32000;
        DrawStatistics();
    }

    /* While showing user stats, poll inputs */
    if (_showingUserStats)
    {
        Player1StatusProc();
        Player2StatusProc();
        if (_pl1Fire)
        {
            _fireBlocked = 1;
            StatisticsOff();
            return;
        }
        if (_pl2Fire)
        {
            _fireBlocked = 1;
            StatisticsOff();
            return;
        }
        return;
    }

    /* -- Shot tracking logic (only when not in penalties) -- */
    if (!_playingPenalties)
    {
        /* Increment possession counter for last team that touched ball */
        if (_gameStatePl == ST_GAME_IN_PROGRESS)
        {
            TeamGeneralInfo *team = _lastTeamPlayed;
            if (team == &_topTeamData || team == &_bottomTeamData)
            {
                TeamStatisticsData *statsPtr = team->teamStatsPtr;
                statsPtr->ballPossession++;
            }
        }

        /* Goal attempt tracking */
        if (_gameStatePl == ST_GAME_IN_PROGRESS)
        {
            if (_isGoalAttempt)
            {
                /* isGoalAttempt is set — check if ball is moving away from goal */
                TeamGeneralInfo *team = &_topTeamData;
                int32_t deltaY = _ballSprite.deltaY.raw;
                if (_ballSprite.y.s.i >= 449)
                {
                    team = &_bottomTeamData;
                    deltaY = -deltaY;
                }
                if (team == _lastTeamPlayed || deltaY >= 0)
                {
                    /* Ball moving away or same team → clear attempt */
                    _isGoalAttempt = 0;
                    goto check_show_stats_flag;
                }
                goto check_show_stats_flag;
            }

            /* Check if ball is in goalkeeper area */
            if (_ballInGoalkeeperArea)
            {
                TeamGeneralInfo *team = &_topTeamData;
                int32_t deltaY = _ballSprite.deltaY.raw;
                if (_ballSprite.y.s.i > 449)
                {
                    /* Ball in lower half — keep topTeamData */
                }
                else
                {
                    /* Ball in upper half */
                    team = &_bottomTeamData;
                    deltaY = -deltaY;
                }

                /* deltaY > 0 means ball moving toward goal */
                if (deltaY > 0
                    && team == _lastTeamPlayed
                    && team->playerHasBall == 0
                    && _strikeDestX >= 240
                    && _strikeDestX <= 431)
                {
                    /* Check if on target (between goalposts at 303-367) */
                    if (_strikeDestX >= 303 && _strikeDestX <= 367)
                    {
                        /* On target — increment on-target stat for attacking team */
                        TeamStatisticsData *statsPtr;
                        if (_ballSprite.y.s.i > 449)
                            statsPtr = _topTeamData.teamStatsPtr;
                        else
                            statsPtr = _bottomTeamData.teamStatsPtr;
                        statsPtr->onTarget++;
                    }

                    /* Increment goal attempts for attacking team */
                    {
                        TeamStatisticsData *statsPtr;
                        if (_ballSprite.y.s.i > 449)
                            statsPtr = _topTeamData.teamStatsPtr;
                        else
                            statsPtr = _bottomTeamData.teamStatsPtr;
                        statsPtr->goalAttempts++;
                    }

                    _isGoalAttempt = 1;
                }
            }
        }
        else
        {
            /* Not in game → clear goal attempt */
            _isGoalAttempt = 0;
        }
    }

check_show_stats_flag:
    /* Check if showStats was set during this frame */
    if (_showStats)
    {
        _showStats = 0;
        _showingUserStats = 1;
    }

    /* Stats timer management */
    if (_statsTimer == 0)
        return;

    if ((int16_t)_statsTimer < 0)
    {
        StatisticsOff();
        return;
    }

    if (_statsTimer == 32000)
    {
        _statsTimer = 30000;
        _overlayActiveFlag = 1;
    }
    else
    {
        _statsTimer -= _lastFrameTicks;
        if ((int16_t)_statsTimer <= 0)
        {
            StatisticsOff();
            return;
        }
    }

    DrawStatistics();
}


/* ================================================================
 * TOP GOAL SCORERS — Chunk 134
 *
 * highestScorersTable layout (92 bytes, in BSS):
 *   offset 0-29:  uint16_t sortIndices[15] — byte offsets into entries
 *   offset 30:    uint8_t  minScore
 *   offset 31:    uint8_t  count (replaceable slots at minScore)
 *   offset 32-91: 15 × 4-byte entries:
 *                   +0 (word): teamId (0xFFFF = empty)
 *                   +2 (byte): playerIdx
 *                   +3 (byte): score (goals)
 * ================================================================ */

/* ---- Additional external globals for top goal scorers ---- */
extern uint8_t  _highestScorersTable[];   /* 92 bytes BSS */
extern uintptr_t _diyStatsName;            /* pointer stored as uint32_t */
extern uint16_t _numSelectedTeams;
extern uint16_t _selTeamNumber;
extern uint16_t _gameType;
extern uint16_t _careerLoadedFlag;
extern TeamFileEx _careerTeam;
extern uint16_t _careerNumPlayers;
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_diyNameOfCompetitionPtr;
extern uint16_t _diyUnknownWord_2C;
extern MenuEntry _menuEntries[];
extern uint8_t  _aLparen[];              /* "(" */
extern uint8_t  _aRparen[];              /* ")" */
extern uint8_t  _aCup_10[];              /* "\x04C\x00U\x00P\x00 " */

/* Menu data (declared in SWOS2.s, used as a6 for ShowMenu) */
extern uint8_t  _competitionTopGoalScorersMenu[];
extern uint8_t  _topGoalScorersMenu[];

/* ---- External functions ---- */
extern void ShowMenu(uint8_t *menu);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern TeamFileEx *GetTeamPointer(uint16_t teamNumber);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern int16_t FindTeamIdInDiyTeamsList(uint16_t *teamIdPtr);
extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNumber);
extern uint8_t *ExtractSurname(uint8_t *name, uint16_t maxLen);
extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);


/* ================================================================
 * InitHighestScorersTable  (SWOS2.s line 51429)
 *
 * Clears the highest scorers table: initializes sort indices,
 * sets minScore to 0, count to 15, and all entries to empty.
 * ================================================================ */
static void InitHighestScorersTable(void)
{
    uint8_t *base = _highestScorersTable;
    uint16_t idx = 0;
    int16_t i;
    uint16_t *sortPtr = (uint16_t *)base;

    base[30] = 0;        /* minScore = 0 */
    base[31] = 15;       /* count = 15 */

    for (i = 14; i >= 0; i--)
    {
        *sortPtr++ = idx;                            /* sort index */
        *(uint16_t *)(base + idx + 32) = 0xFFFF;     /* teamId = -1 */
        *(base + idx + 34) = 0xFF;                    /* playerIdx = -1 */
        *(base + idx + 35) = 0;                       /* score = 0 */
        idx += 4;
    }
}


/* ================================================================
 * UpdateHighestScorersMinScoreAndCount  (SWOS2.s line 51529)
 *
 * Scans all 15 entries to find the new minimum score and count
 * of entries at that minimum. Updates minScore and count fields.
 * Returns the new minScore.
 * ================================================================ */
static uint8_t UpdateHighestScorersMinScoreAndCount(uint8_t *base)
{
    uint8_t *ptr = base;
    uint8_t minVal = 0xFF;
    uint8_t cnt = 0;
    int16_t i;

    for (i = 14; i >= 0; i--)
    {
        uint8_t score = *(ptr + 35);  /* entry score at offset 35 from current pos */
        if (minVal > score)
        {
            /* New minimum found */
            minVal = score;
            cnt = 1;
        }
        else if (minVal == score)
        {
            cnt++;
        }
        ptr += 4;
    }

    base[30] = minVal;   /* minScore */
    base[31] = cnt;      /* count */
    return minVal;
}


/* ================================================================
 * FindReplaceableLowestScorerSlot  (SWOS2.s line 51554)
 *
 * Finds an entry to replace in the table. Prefers:
 * 1. An entry at minScore that has teamId == -1 (empty)
 * 2. An entry at minScore whose team isInDIYTeam == 1
 * 3. Falls back to first entry at minScore
 *
 * Returns pointer to the slot (a3 in asm).
 * ================================================================ */
static uint8_t *FindReplaceableLowestScorerSlot(uint8_t *base, uint8_t minScore)
{
    uint8_t *ptr = base;
    int16_t i;

    /* First pass: find empty slot or DIY team at minScore.
     * The teamId at offset 32 was stored as a `{country, ordinal}`
     * byte pair (see store site below) so use BE_READ_U16 to recover
     * the `(country<<8)|ordinal` value VerifyTeamLoaded expects on
     * the LE host. */
    for (i = 14; i >= 0; i--)
    {
        if (*(ptr + 35) == minScore)  /* score matches minScore */
        {
            uint16_t teamId = BE_READ_U16(ptr + 32);
            if (teamId == 0xFFFF)
                return ptr;  /* empty slot */

            /* Check if team's isInDIYTeam flag is set */
            {
                uint16_t d0 = teamId;
                uint8_t d1 = *(ptr + 34);  /* playerIdx */
                int16_t d1w = (int8_t)d1;  /* ext.w d1 */
                TeamFileEx *teamPtr = VerifyTeamLoaded(d0);
                if (teamPtr->team.teamControls == 1)   /* isInDIYTeam == 1 */
                    return ptr;
            }
        }
        ptr += 4;
    }

    /* Second pass: find first entry at minScore */
    ptr = base;
    for (;;)
    {
        if (*(ptr + 35) == minScore)
            return ptr;
        ptr += 4;
    }
}


/* ================================================================
 * AddTeamScorersToHighestScorersTable  (SWOS2.s line 51449)
 *
 * Adds goal scorers from one team to the highest scorers table.
 * d0 = statIndex (competition index, or negative for all-comps sum)
 * a0 = pointer to team data (684 bytes: TeamFile + 16 PlayerFile)
 * ================================================================ */
static void AddTeamScorersToHighestScorersTable(int16_t statIndex,
                                                 uint8_t *teamPtr)
{
    uint8_t *base = _highestScorersTable;
    uint8_t *playerBase;
    uint8_t *teamBase;
    uint16_t teamNumber;
    uint8_t minScore;
    int16_t _numPlayers;
    int16_t playerIdx;

#ifdef SWOS_DEBUG_AUTOSKIP
    uint16_t debug_teamId = BE_READ_U16(teamPtr);
    int debug_scorersAdded = 0;
#endif

    /* For DIY game types with negative statIndex, verify team is in DIY list */
    if (statIndex < 0)
    {
        if (_gameType == 3 || _gameType == 4)
        {
            if (FindTeamIdInDiyTeamsList((uint16_t *)teamPtr) != 0) {
#ifdef SWOS_DEBUG_AUTOSKIP
                SWOS_AUTOSKIP_LOG("[bug2-scorers]     AddTeamScorers teamId=0x%04X SKIP (not in DIY list)",
                                  (unsigned)debug_teamId);
#endif
                return;  /* team not in DIY list, skip */
            }
        }
    }

    teamBase = teamPtr;
    playerBase = teamPtr;
    /* TeamFile leading bytes are { countryByte, ordinalByte }; original
     * asm reads them as a single big-endian uint16 (move.w). */
    teamNumber = BE_READ_U16(teamBase);
    minScore = base[30];  /* current minScore */

    /* Determine number of players to iterate */
    _numPlayers = 15;  /* default: 16 players (dbf loop 15→0) */
    if (teamNumber == _selTeamNumber)
    {
        _numPlayers = (int16_t)_careerNumPlayers - 1;
    }

    playerIdx = 0;

    for (; _numPlayers >= 0; _numPlayers--)
    {
        uint16_t score;

        /* For career team, use GetPlayerAtIndex to remap */
        if (teamNumber == _selTeamNumber)
        {
            playerBase = GetPlayerAtIndex(teamNumber, (uint16_t)_numPlayers);
            /* GetPlayerAtIndex stores fileIndex in _cGetPlayerAtIndexFileIdx */
            /* In asm, d1 returns the file index → stored in d7 (playerIdx) */
            extern uint16_t _cGetPlayerAtIndexFileIdx;
            playerIdx = (int16_t)_cGetPlayerAtIndexFileIdx;
        }

        /* playerBase points at the team base (career: the GetPlayerAtIndex
         * result), so the PlayerFile sits at +sizeof(TeamFile). This replaces the
         * old raw 0x6D..0x70 (= 76 + offsetof(PlayerFile, goalStats)). */
        PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));
        if (statIndex < 0)
        {
            /* Sum all 4 competition goal stats */
            uint16_t total = (uint16_t)pf->goalStats[0] + pf->goalStats[1]
                           + pf->goalStats[2] + pf->goalStats[3];
            if (total > 255)
                total = 255;
            score = total;
            if (score == 0)
                goto next_player;
        }
        else
        {
            /* Specific competition index */
            score = pf->goalStats[statIndex];
            if (score == 0)
                goto next_player;
        }

        /* Check if score qualifies for the table */
        if ((uint8_t)score < minScore)
            goto next_player;

        if ((uint8_t)score == minScore)
        {
            /* Score equals minimum — only skip if team isInDIYTeam */
            if (*(teamBase + 4) == 1)  /* isInDIYTeam */
                goto next_player;
        }

        /* Find a slot to replace */
        {
#ifdef SWOS_DEBUG_AUTOSKIP
            debug_scorersAdded++;
#endif
            uint8_t *slot = FindReplaceableLowestScorerSlot(base, minScore);
            slot[35] = (uint8_t)score;       /* store score */
            slot[34] = (uint8_t)playerIdx;   /* store playerIdx */
            /* teamNumber is the BE-packed (countryNumber<<8)|teamOrdinal
             * value (read via BE_READ_U16(teamBase) above). Store via
             * BE_WRITE_U16 so the byte layout matches the team-file
             * convention and BE_READ_U16 retrievals at lines 578/777/
             * 819-820/965/1118 see the same value on both BE and LE
             * hosts. (Native uint16 store on LE byte-swaps relative to
             * the BE reads, producing a corrupted teamId — e.g. team
             * "VFB STUTTGART" 0x0E27 was read back as 0x000E=14, hitting
             * VerifyTeamLoaded panic.) */
            BE_WRITE_U16(slot + 32, teamNumber);

            if ((uint8_t)score != minScore)
            {
                /* We replaced a slot with a higher score — decrement replaceable count */
                base[31]--;
                if (base[31] == 0)
                {
                    /* No more replaceable slots — recalculate min and count */
                    minScore = UpdateHighestScorersMinScoreAndCount(base);
                }
            }
        }

next_player:
        if (teamNumber != _selTeamNumber)
        {
            /* Non-career: advance playerBase by 38 (PlayerFile size) */
            playerBase += 0x26;
        }
        playerIdx++;
    }
#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug2-scorers]     AddTeamScorers teamId=0x%04X DONE scorersAdded=%d",
                      (unsigned)debug_teamId, debug_scorersAdded);
#endif
}


/* ================================================================
 * PopulateHighestScorersTableFromSelectedTeams  (SWOS2.s line 51402)
 *
 * Iterates selected teams, adding their scorers to the table.
 * Skips the career team (selTeamNumber), but if game type is
 * Career and careerLoadedFlag==0, also adds career team scorers.
 * d0 = statIndex passed through to AddTeamScorersToHighestScorersTable
 * ================================================================ */
static void PopulateHighestScorersTableFromSelectedTeams(int16_t statIndex)
{
    TeamFileEx *teamsPtr = _selectedTeams;
    uint16_t numTeams = _numSelectedTeams;
    uint16_t careerTeamNum;

#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug2-scorers]   PopulateFromSelectedTeams statIdx=%d numSelectedTeams=%u",
                      (int)statIndex, (unsigned)numTeams);
#endif
    if (numTeams == 0)
        return;

    careerTeamNum = _selTeamNumber;
    numTeams--;

    for (; (int16_t)numTeams >= 0; numTeams--)
    {
        uint16_t pid = PACK_BE_U16(teamsPtr->team.countryNumber,
                                   teamsPtr->team.teamOrdinal);
        if (pid != careerTeamNum)
        {
            AddTeamScorersToHighestScorersTable(statIndex, (uint8_t *)teamsPtr);
        }
        teamsPtr++;
    }

    /* In Career mode with careerLoadedFlag==0, add career team too */
    if (_gameType == GAME_TYPE_CAREER)
    {
        if (_careerLoadedFlag != 0)
            return;
        AddTeamScorersToHighestScorersTable(statIndex, (uint8_t *)&_careerTeam);
    }
}


/* ================================================================
 * SortHighestScorerTable  (SWOS2.s line 51584)
 *
 * Bubble-sorts the sort indices in descending order of score.
 * Ties are broken alphabetically by player surname.
 * ================================================================ */

/* GetScorerSurname: returns pointer to last word (surname) in player name */
static uint8_t *GetScorerSurname(uint8_t *entryBase)
{
    uint16_t teamId = BE_READ_U16(entryBase + 32);
    uint8_t playerIdx = *(entryBase + 34);
    int16_t idxSigned = (int8_t)playerIdx;
    uint8_t *playerPtr;
    uint8_t *nameStart;
    uint8_t *surname;

    playerPtr = GetPlayer(teamId, (uint16_t)idxSigned);
    /* Player name at offset 0x4C + PLAYERFILE_PLAYERNAME(3) = 0x4F from GetPlayer result */
    nameStart = playerPtr + 0x4F;
    surname = nameStart;

    while (*nameStart)
    {
        if (*nameStart == ' ')
        {
            nameStart++;
            surname = nameStart;
        }
        else
        {
            nameStart++;
        }
    }

    return surname;
}

static void SortHighestScorerTable(void)
{
    uint8_t *base = _highestScorersTable;
    uint16_t *sortArray = (uint16_t *)base;
    int16_t outer, inner;

    for (outer = 13; outer >= 0; outer--)
    {
        uint16_t *cmp = sortArray;
        for (inner = outer; inner >= 0; inner--)
        {
            uint8_t *entryA = base + cmp[0];  /* current */
            uint8_t *entryB = base + cmp[1];  /* next */

            uint16_t teamIdA = BE_READ_U16(entryA + 32);
            uint16_t teamIdB = BE_READ_U16(entryB + 32);

            /* Empty entries (teamId == -1) sort to end */
            if (teamIdA == 0xFFFF)
                goto do_swap;

            if (teamIdB == 0xFFFF)
                goto no_swap;

            /* Compare scores (higher first) */
            {
                uint8_t scoreA = *(entryA + 35);
                uint8_t scoreB = *(entryB + 35);

                if (scoreA > scoreB)
                    goto no_swap;
                if (scoreA < scoreB)
                    goto do_swap;
            }

            /* Equal scores — compare surnames alphabetically */
            {
                uint8_t *surnameA = GetScorerSurname(entryA);
                uint8_t *surnameB = GetScorerSurname(entryB);
                uint8_t chB;

                for (;;)
                {
                    chB = *surnameB++;
                    if (chB == 0)
                        goto no_swap;  /* B is shorter or equal → keep order */
                    if (chB < *surnameA)
                        goto no_swap;
                    if (chB > *surnameA)
                        goto do_swap;
                    surnameA++;
                }
            }

do_swap:
            {
                uint16_t tmp = cmp[0];
                cmp[0] = cmp[1];
                cmp[1] = tmp;
            }
no_swap:
            cmp++;
        }
    }
}


/* ================================================================
 * ShowCompetitionTopScorers  (SWOS2.s line 51366, loc_1382F6)
 *
 * Initializes, populates, sorts the highest scorers table, then
 * displays the competition top goal scorers menu.
 * d0 = statIndex, a0 = name pointer for menu title
 * ================================================================ */
static void ShowCompetitionTopScorers(int16_t statIndex, uint8_t *namePtr)
{
    if (statIndex == 4)
        statIndex = 0;

    InitHighestScorersTable();
    PopulateHighestScorersTableFromSelectedTeams(statIndex);
    _diyStatsName = (uintptr_t)namePtr;
    SortHighestScorerTable();
    ShowMenu(_competitionTopGoalScorersMenu);
}


/* ================================================================
 * TopGoalScorersMenu_0  (SWOS2.s line 51389)
 *
 * Entry point for career/overall top goal scorers display.
 * Uses statIndex = -1 (all competitions summed).
 * ================================================================ */
__attribute__((used, externally_visible))
void TopGoalScorersMenu_0(void)
{
#ifdef SWOS_DEBUG_AUTOSKIP
    {
        extern CompetitionSlotBuffer _primaryCompetitionBuf;  /* slot-0 untangle (S6): block-local (file-scope decl is below this fn) */
        uint16_t compTypeWord = (uint16_t)_primaryCompetitionBuf.tourn.header.competitionType;
        SWOS_AUTOSKIP_LOG("[bug2-scorers] TopGoalScorersMenu_0 ENTER statIdx=-1 "
                          "numSelectedTeams=%u numDiyTeams=%u gameType=%u "
                          "careerLoadedFlag=%u compTypeWord=0x%04X selTeamNumber=0x%04X",
                          (unsigned)_numSelectedTeams, (unsigned)_primaryCompetitionBuf.tourn.header.numTeams,
                          (unsigned)_gameType, (unsigned)_careerLoadedFlag,
                          (unsigned)compTypeWord, (unsigned)_selTeamNumber);
    }
#endif
    InitHighestScorersTable();
    PopulateHighestScorersTableFromSelectedTeams(-1);
    SortHighestScorerTable();
#ifdef SWOS_DEBUG_AUTOSKIP
    {
        uint8_t *base = _highestScorersTable;
        uint16_t *sortArray = (uint16_t *)base;
        int validCount = 0;
        int totalGoals = 0;
        for (int i = 0; i < 15; i++) {
            uint8_t *entry = base + sortArray[i];
            uint16_t teamId = *(uint16_t *)(entry + 32);
            uint8_t score   = *(entry + 35);
            if (teamId != 0xFFFF) { validCount++; totalGoals += score; }
        }
        SWOS_AUTOSKIP_LOG("[bug2-scorers] TopGoalScorersMenu_0 EXIT "
                          "filledEntries=%d/15 totalGoals=%d", validCount, totalGoals);
    }
#endif
    ShowMenu(_topGoalScorersMenu);
}


/* ================================================================
 * StatsHighestScorerList  (SWOS2.s line 55634)
 *
 * Entry point from stats menu — loads statIndex and name
 * from globals, then calls ShowCompetitionTopScorers.
 * ================================================================ */
__attribute__((used, externally_visible))
void StatsHighestScorerList(void)
{
    uint8_t *namePtr = _diyNameOfCompetitionPtr;
    int16_t statIdx = (int16_t)_diyUnknownWord_2C;
    ShowCompetitionTopScorers(statIdx, namePtr);
}


/* ================================================================
 * CompetitionTopGoalScorersInit  (SWOS2.s line 52406)
 *
 * Menu init callback for competition top goal scorers.
 * Populates menu entries with scorer name, (team), and score.
 * ================================================================ */
__attribute__((used, externally_visible))
void CompetitionTopGoalScorersInit(void)
{
    MenuEntry *a5 = &_menuEntries[0];
    uint8_t *base = _highestScorersTable;
    uint16_t *sortArray = (uint16_t *)base;
    int16_t i;

    /* First entry: set title text pointer from diyStatsName */
    a5->fg = _diyStatsName;

    /* Skip to entry at offset 0xA8/56 = 3 */
    a5 = &_menuEntries[3];

    for (i = 14; i >= 0; i--)
    {
        uint8_t *entry;
        uint16_t sortIdx = *sortArray++;
        entry = base + sortIdx;

        if (*(uint16_t *)(entry + 32) == 0xFFFF)
        {
            /* Empty entry — hide 4 sub-entries */
            a5->isInvisible = 1;
            a5 += 1;  /* 0x38/56 = 1 entry */
            a5->isInvisible = 1;
            a5 += 1;
            a5->isInvisible = 1;
            a5 += 1;
            a5->isInvisible = 1;
            a5 += 1;
            continue;
        }

        /* Valid entry */
        {
            uint16_t teamId = BE_READ_U16(entry + 32);
            uint8_t playerIdxByte = *(entry + 34);
            int16_t playerIdxSigned = (int8_t)playerIdxByte;
            TeamFileEx *teamData;
            uint8_t *teamName;
            uint8_t *playerPtr;
            uint8_t *dest;

            /* Get team name */
            teamData = GetTeamPointer(teamId);
            teamName = teamData->team.teamName;

            /* Get player name */
            playerPtr = GetPlayer(teamId, (uint16_t)playerIdxSigned);

            /* Entry 1: player name */
            a5 += 1;
            dest = (uint8_t *)a5->fg;  /* string buffer pointer */
            {
                uint8_t *src = playerPtr + 0x4F;  /* 0x4C + 3 = playerName */
                uint8_t *d = dest;
                d = StringCopy(src, d);
            }

            /* Entry 2: "(teamname)" */
            a5 += 1;
            dest = (uint8_t *)a5->fg;
            {
                uint8_t *d = dest;
                d = StringCopy(_aLparen, d);
                d = StringCopy(teamName, d);
                d = StringCopy(_aRparen, d);
                *d = 0;
            }

            /* Entry 3: score number */
            a5 += 1;
            {
                uint8_t score = *(entry + 35);
                a5->fgWord = (uint16_t)score;  /* ENTRY_NUMBER: write high word */
            }

            a5 += 1;
        }
    }
}


/* ================================================================
 * TopGoalScorersInit  (SWOS2.s line 52825)
 *
 * Menu init for career top goal scorers (multi-competition view).
 * Adjusts entry layout based on which competitions are active.
 * ================================================================ */
__attribute__((used, externally_visible))
void TopGoalScorersInit(void)
{
    /* If no tertiary competition, shift entries down */
    if (_tertiaryCompetitionPtr == 0)
    {
        /* Shift column headers: move entry[23]→entry[22], entry[24]→entry[23] */
        MenuEntry *e22 = &_menuEntries[0x508 / 56];  /* entry 22 (at +$508) */
        MenuEntry *e23 = &_menuEntries[0x540 / 56];  /* entry 23 (at +$540) */
        MenuEntry *e24 = &_menuEntries[0x578 / 56];  /* entry 24 (at +$578) */
        e22->x = e23->x;
        e23->x = e24->x;

        /* Shift row label entries similarly */
        MenuEntry *e1 = &_menuEntries[0x38 / 56];   /* entry 1 (at +$38) */
        MenuEntry *e2 = &_menuEntries[0x70 / 56];   /* entry 2 (at +$70) */
        MenuEntry *e3 = &_menuEntries[0xA8 / 56];   /* entry 3 (at +$A8) */
        e1->x = e2->x;
        e2->x = e3->x;

        /* Hide entry 3 */
        _menuEntries[0xA8 / 56].isInvisible = 1;

        /* Set CUP label on entry 2 */
        MenuEntry *cupEntry = &_menuEntries[0x70 / 56];
        cupEntry->fg = (uintptr_t)_aCup_10;
    }

    /* If no secondary competition, shift further */
    if (_secondaryCompetitionPtr == 0)
    {
        MenuEntry *e22 = &_menuEntries[0x508 / 56];
        MenuEntry *e23 = &_menuEntries[0x540 / 56];
        e22->x = e23->x;

        MenuEntry *e1 = &_menuEntries[0x38 / 56];
        MenuEntry *e2 = &_menuEntries[0x70 / 56];
        e1->x = e2->x;

        /* Hide entries 2 and 1 */
        _menuEntries[0x70 / 56].isInvisible = 1;
        _menuEntries[0x38 / 56].isInvisible = 1;
    }
}


/* ================================================================
 * TopGoalScorersBeforeDrawCommon  (SWOS2.s line 52857)
 *
 * BeforeDraw callback for each scorer row in career top scorers.
 * entry (a5) ordinal determines which scorer to display.
 * Sets up 6 overlay menu entries with: surname, (team), and
 * per-competition goal counts + total.
 * ================================================================ */
__attribute__((used, externally_visible))
void TopGoalScorersBeforeDrawCommon(MenuEntry *entry)
{
    uint8_t *base = _highestScorersTable;
    uint16_t ordinal = entry->ordinal;
    int16_t sortPos = (int16_t)(ordinal - 6);
    int16_t sortOffset = sortPos * 2;  /* word offset into sort array */
    uint16_t yCoord = entry->y;
    MenuEntry *a4 = &_menuEntries[0x498 / 56];  /* entry 21 */
    uint16_t sortIdx;
    uint8_t *scorerEntry;

    uint8_t minScoreVal = base[30];

    sortIdx = *(uint16_t *)(base + sortOffset);
    scorerEntry = base + sortIdx;

    /* Check if entry is valid */
    if (*(uint16_t *)(scorerEntry + 32) == 0xFFFF)
    {
        /* Invalid entry — hide all 6 overlay entries and the main entry */
        entry->isInvisible = 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        a4->y = yCoord;
        a4->isInvisible = 1;
        a4 += 1;
        return;
    }

    /* Valid scorer entry */
    {
        uint16_t teamId = BE_READ_U16(scorerEntry + 32);
        uint8_t playerIdxByte = *(scorerEntry + 34);
        int16_t playerIdxSigned = (int8_t)playerIdxByte;
        TeamFileEx *teamData;
        uint8_t *teamName;
        uint8_t *playerPtr;  /* result of GetPlayer, a5 in asm */
        uint8_t *_surnameBuf;
        uint8_t *dest;
        uint16_t d0val;

        teamData = GetTeamPointer(teamId);
        teamName = teamData->team.teamName;

        playerPtr = GetPlayer(teamId, (uint16_t)playerIdxSigned);

        /* Extract surname using ExtractSurname (name at offset 0x4F, maxLen 0x15=21) */
        _surnameBuf = ExtractSurname(playerPtr + 0x4F, 0x15);

        /* Entry 0: player surname */
        a4->fg = (uintptr_t)_surnameBuf;
        a4->y = yCoord;
        a4->isInvisible = 0;
        a4 += 1;

        /* Entry 1: "(teamname)" */
        dest = (uint8_t *)a4->fg;
        {
            uint8_t *d = dest;
            d = StringCopy(_aLparen, d);
            d = StringCopy(teamName, d);
            d = StringCopy(_aRparen, d);
            *d = 0;
        }
        a4->y = yCoord;
        a4->isInvisible = 0;
        a4 += 1;

        /* Entry 2: league goals (from secondaryCompetitionPtr) */
        d0val = 0;
        a4->isInvisible = 1;
        if (_secondaryCompetitionPtr != 0)
        {
            d0val = *(playerPtr + 0x6D);     /* goalStats[0] */
            a4->fgWord = d0val;  /* ENTRY_NUMBER: write high word */
            a4->y = yCoord;
            a4->isInvisible = 0;
        }
        a4 += 1;

        /* Entry 3: cup goals (from secondaryCompetitionPtr) */
        a4->isInvisible = 1;
        if (_secondaryCompetitionPtr != 0)
        {
            d0val = *(playerPtr + 0x6E);     /* goalStats[1] */
            a4->fgWord = d0val;  /* ENTRY_NUMBER: write high word */
            a4->y = yCoord;
            a4->isInvisible = 0;
        }
        a4 += 1;

        /* Entry 4: tertiary competition goals */
        a4->isInvisible = 1;
        if (_tertiaryCompetitionPtr != 0)
        {
            d0val = *(playerPtr + 0x6F);     /* goalStats[2] */
            a4->fgWord = d0val;  /* ENTRY_NUMBER: write high word */
            a4->y = yCoord;
            a4->isInvisible = 0;
        }
        a4 += 1;

        /* Entry 5: total score from scorer table entry */
        {
            uint8_t totalScore = *(scorerEntry + 35);
            d0val = totalScore;
            a4->fgWord = d0val;  /* ENTRY_NUMBER: write high word */
            a4->y = yCoord;
            a4->isInvisible = 0;
        }
        a4 += 1;

        /* Draw all 6 overlay entries starting from entry 21 */
        {
            MenuEntry *drawStart = &_menuEntries[0x498 / 56];
            DrawMultipleItems(6, drawStart);
        }
    }
}


/* ================================================================
 * Chunk 137 — Management Record cluster
 * ================================================================ */

/* Externs for Chunk 137 */
extern uint16_t _currentSeasonNumber;
extern uint16_t _currentSeasonShowing;
extern uint8_t  _seasonInformationsArray[];
extern uint16_t _youreSacked;
extern uint16_t _gamesPlayed;
extern uint16_t _numWins;
extern uint16_t _numDrawn;
extern uint16_t _numLost;
extern uint16_t _goalsScored;
extern uint16_t _goalsTaken;
extern uint8_t  _aManagementReco[];     /* "MANAGEMENT RECORD OF %a %b" */
extern uint8_t  _aPl0W1D2L3F4A5[];     /* "PL %0    W %1    D %2    L %3    F %4  "... */
extern uint8_t  _aDash[];              /* "-" */
extern uint8_t  _managerNameBuffer[];
extern uint8_t  _managerSurnameBuffer[];
extern uint16_t _playPromotedAnimation;
extern uint16_t _playRelegatedAnimation;

extern void DrawMenu(void);


/* ================================================================
 * GetCurrentSeasonPointer  (SWOS2.s line 58420)
 *
 * Returns pointer to current season info. Stride is sizeof so the
 * 64-bit pointer-field growth in SeasonInformations (106 → 122) is
 * picked up automatically — readers and writers must agree on stride
 * or row N writes land in row N+ε for the reader (caused MANAGEMENT
 * RECORD garbage-pointer SIGSEGV when sizeof-typed readers raced the
 * 106-byte-stride writer).
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *GetCurrentSeasonPointer(void)
{
    uint16_t idx = _currentSeasonShowing;
    uint32_t off = (uint32_t)idx * (uint32_t)sizeof(SeasonInformations);
    return _seasonInformationsArray + off;
}


/* ================================================================
 * ManagementRecordInit  (SWOS2.s line 57924)
 *
 * Initializes the management record menu header and stats line.
 * ================================================================ */
__attribute__((used, externally_visible))
void ManagementRecordInit(void)
{
    uint8_t *dest;

    _currentSeasonNumber = _currentSeasonShowing;

    /* Format title: "MANAGEMENT RECORD OF <name> <surname>" */
    dest = (uint8_t *)_menuEntries[0].fg;
    PrintFormatted(tr_fmt(_aManagementReco), dest,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerNameBuffer, _managerSurnameBuffer, (const uint8_t *)0);

    /* Format stats line (entry at offset $380 = entry 16) */
    {
        MenuEntry *statsEntry = &_menuEntries[0x380 / 56];
        dest = (uint8_t *)statsEntry->fg;

        if (_gamesPlayed == 0)
        {
            PrintFormatted(_aDash, dest,
                            0, 0, 0, 0, 0, 0, 0,
                            (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);
        }
        else
        {
            PrintFormatted(_aPl0W1D2L3F4A5, dest,
                            _gamesPlayed, _numWins, _numDrawn, _numLost,
                            _goalsScored, _goalsTaken, 0,
                            (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);
        }
    }
}


/* ================================================================
 * ManagementArrowUpSelect  (SWOS2.s line 57954)
 *
 * Handler for up-arrow in management record menu.
 * Decrements season number and redraws.
 * ================================================================ */
__attribute__((used, externally_visible))
void ManagementArrowUpSelect(void)
{
    if (_currentSeasonNumber == 0)
        return;
    _currentSeasonNumber -= 1;
    DrawMenu();
}


/* ================================================================
 * ManagementArrowDownSelect  (SWOS2.s line 57964)
 *
 * Handler for down-arrow in management record menu.
 * Increments season number (up to currentSeasonShowing) and redraws.
 * ================================================================ */
__attribute__((used, externally_visible))
void ManagementArrowDownSelect(void)
{
    if (_currentSeasonShowing == _currentSeasonNumber)
        return;
    _currentSeasonNumber += 1;
    DrawMenu();
}


/* SeasonInformations struct field offsets (from SWOS2.s EQUs) */
#define SI_CLUBNO                0   /* uint16_t */
#define SI_CLUBNAME              2   /* char[18] */
#define SI_TROPHYFLAGS          20   /* uint16_t */
#define SI_LEAGUESTRPTR         22   /* uint32_t (ptr) */
#define SI_STRPTR1              26   /* uint32_t (ptr) */
#define SI_STRPTR2              30   /* uint32_t (ptr) */
#define SI_STRPTR3              34   /* uint32_t (ptr) */
#define SI_BESTSCORERNUMGOALS   38   /* uint16_t */
#define SI_BESTSCORERNAME       40   /* char[23] */
#define SI_2NDSCORERNAME        63   /* char[23] */
#define SI_COMP1RESULT          86   /* int8_t */
#define SI_COMP2RESULT          87   /* int8_t */
#define SI_COMP3RESULT          88   /* int8_t */
#define SI_COMP4RESULT          89   /* int8_t */
#define SI_TOTALGOALSSCORED     90   /* uint16_t */
#define SI_TOTALGOALSTAKEN      92   /* uint16_t */
#define SI_TOTALGAMES           94   /* uint8_t */
#define SI_TOTALWINS            95   /* uint8_t */
#define SI_TOTALDRAWN           96   /* uint8_t */
#define SI_TOTALLOST            97   /* uint8_t */
#define SI_FRIENDLYWINS         98   /* uint8_t */
#define SI_FRIENDLYDRAWS        99   /* uint8_t */
#define SI_FRIENDLYLOST        100   /* uint8_t */
#define SI_TOURWINS            101   /* uint8_t */
#define SI_TOURDRAWS           102   /* uint8_t */
#define SI_TOURLOST            103   /* uint8_t */
#define SI_TOURNAMENTSTATUS    104   /* uint8_t */
#define SI_MANAGERSTATUS       105   /* int8_t */

/* ================================================================
 * Career_StorePromotionRelegationInSeasonInfo  (SWOS2.s line 58429)
 *
 * Stores promotion (3) or relegation (4) flag at offset $68 (104)
 * in the current season info.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_StorePromotionRelegationInSeasonInfo(void)
{
    uint8_t flag;

    if (_youreSacked != 0)
        return;

    if (_playPromotedAnimation != 0)
        flag = 3;
    else if (_playRelegatedAnimation != 0)
        flag = 4;
    else
        return;

    ((SeasonInformations *)GetCurrentSeasonPointer())->tournamentStatus = flag;
}

/* ================================================================
 * Chunk 199 — Career_FillCurrentSeasonInfo + EncodeCompetitionPositionForSeasonInfo
 * ================================================================ */

/* ---- Externs for Chunk 199 ---- */
extern uint16_t _wonWorldCup;
extern uint16_t _wonContinentalCup;
extern uint16_t _majorTrophyBonusActive;
extern uint16_t _cupBonus2Active;
extern uint16_t _cupBonus1Active;
extern uint16_t _playWinCupAnimation;
extern uint16_t _currentDivisionIndex;
extern uint16_t _minorTrophyBonusActive;
extern uint16_t _leagueTitleBonusActive;
extern uint16_t _wonInvitationTournament;
extern uint16_t _seasonFinished;
extern uint16_t _boardConfidenceMod0;
extern uint16_t _boardConfidenceMod1;
extern uint16_t _boardConfidenceMod2;
extern uint16_t _boardConfidenceMod3;
extern uint16_t _boardConfidenceValue0;
extern uint16_t _boardConfidenceValue1;
extern uint16_t _boardConfidenceMod4;
extern uint16_t _boardConfidenceMod5;
extern uint16_t _boardConfidenceMod6;
extern uint16_t _boardConfidenceMod7;
extern uint16_t _boardConfidenceMod8;
extern uint16_t _boardConfidenceMod9;
extern uint8_t *_currentCompetitionPtr;
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_quaternaryCompetitionPtr;
extern CompetitionSlotBuffer _primaryCompetitionBuf;           /* slot-0 untangle (S6): typed buffer */
extern CompetitionSlotBuffer _competitionContextBuf;          /* slot-1 untangle (S4): typed buffer */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;   /* slot-2 untangle (S3): typed buffer */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf;   /* slot-3 untangle (S5): typed buffer */
extern uint16_t _qualifyingStage1PendingFlag0;
extern uint16_t _qualifyingStage1PendingFlag1;
extern uint16_t _qualifyingStage2PendingFlag0;
extern uint16_t _qualifyingStage2PendingFlag1;
extern uint16_t _diyFoundTeamIndex;
extern uint8_t  _aSeveral[];             /* "SEVERAL" */
extern uint8_t  _worldCupQualifyingGroup1Copy[];
extern uint8_t  _oceaniaWorldCupQualificationCopy[];
extern uint8_t  _ecQualifyingGroup1Copy[];
extern uint8_t  _ecQualifyingGroup8Copy[];

extern void UpdateBoardConfidenceModifiers(void);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern uint16_t FindTeamInDIY(uint16_t teamNo, DIYCompetition *comp);


/* ================================================================
 * EncodeCompetitionPositionForSeasonInfo  (SWOS2.s line 53266)
 *
 * Encodes team's position in a competition into a season info byte.
 * Three paths based on competition type (byte at compBuf+1):
 *   Type 0 (league): checks qualifying flags, may negate position
 *   Type 1 (cup):    bracket tree search for round reached
 *   Type 2+ (tourn): iterates groups to find position
 *
 * Takes: position from FindTeamInDIY (d1), compBuf (a6), compDef (a4)
 * Returns: encoded position (int16_t)
 * ================================================================ */
static int16_t EncodeCompetitionPositionForSeasonInfo_impl(
    int16_t pos, uint8_t *compBuf, uint8_t *compDef)
{
    int16_t d1 = pos + 1;
    /* Asm reads `1(a6)` — on m68k BE that's the LOW byte of the uint16
     * competitionType (the actual 0/1/2 enum value). On x86 LE the
     * byte at offset 1 is the HIGH byte, always zero for legal types,
     * so the previous translation collapsed every comp into the league
     * path and produced positional results (1, 2, ...) for cups
     * instead of the negative -100/-101/-102 codes the renderer turns
     * into WINNERS/RUNNER-UP/REACHED SEMI-FINAL labels. Same family as
     * the AutoAdvance dispatch fix in `_Career_AutoAdvanceCompetition
     * AndPersistSlotState` — see project memory `low-byte-of-uint16-field`. */
    DIYCompetitionEx *compEx = (DIYCompetitionEx *)compBuf;
    DIYCompetition   *comp   = &compEx->base;
    uint8_t type = (uint8_t)(uint16_t)comp->competitionType;

    if (type == 0) {
        /* --- League path --- */
        if (comp->matchStarted == 0)
            return -106;   /* league not started/invalid */

        /* Negate position if compDef is a World Cup or EC qualifying group */
        uint8_t defId = compDef ? comp_id(compDef) : 0xFFu;
        if (comp_is_wc_qualifier(defId))
            goto negate;
        if (comp_is_ec_qualifier(defId))
            goto negate;

        /* Check finished flag at extended +0x60 (minSubstitutes) */
        if (compEx->minSubstitutes == 0)
            return d1;     /* not finished, return position as-is */

    negate:
        return -d1;
    }

    if (type == 1) {
        /* --- Cup path --- */
        if (comp->competitionFinished == 0) {
            /* Cup not finished — check if team is still in it */
            FindTeamInDIY(_selTeamNumber, comp);
            if (_diyFoundTeamIndex == 0)
                return -104;   /* team not found in cup */
        }

        if (d1 == 1) return d1;       /* winner */
        if (d1 == 2) return -100;      /* finalist */
        if (d1 <= 4) return -101;      /* semi-final */
        if (d1 <= 8) return -102;      /* quarter-final */

        /* Beyond 8: binary tree bracket position.
         * Asm: d2=-1, d0=numTeams; do { d2++; d0>>=1; } while d1<d0 */
        {
            int16_t d2 = -1;
            uint16_t d0 = (uint16_t)comp->numTeams;
            do {
                d2++;
                d0 >>= 1;
            } while ((uint16_t)d1 < d0);

            d1 = comp->subRoundCounter; /* starting round */
            d1 += d2;
            return -(d1 + 110);
        }
    }

    /* --- Tournament/Groups path (type >= 2) --- */
    if (comp->currentRoundIndex == 0) {
        /* Tournament not finished — check if team is still in it */
        FindTeamInDIY(_selTeamNumber, comp);
        if (_diyFoundTeamIndex == 0)
            return -104;   /* team not found */
    }

    if (d1 == 1) return d1;   /* winner */

    /* Check playoff groups special cases — read the first DIYTournamentSlot
     * record at parent +0x15A; its _pad_00[2] bytes are interpreted as a
     * uint16 group-presence flag. */
    {
        DIYTournamentSlot *slot0 = (DIYTournamentSlot *)(compBuf + 0x15A);
        uint16_t slot0HeadFlag = *(uint16_t *)slot0->_pad_00;
        if (d1 == 3 && slot0HeadFlag != 0)
            return -103;   /* third place in playoff */
        if (d1 == 4 && slot0HeadFlag != 0)
            return -105;   /* fourth place in playoff */
    }

    /* Iterate through groups to find which group the position falls into.
     * Groups are 0x76 (118) bytes each, starting at compBuf + 0x15A.
     * Loop: advance through slot records while d1 <= group.numTeams. */
    {
        int16_t d2 = 0;
        DIYTournamentSlot *slot = (DIYTournamentSlot *)(compBuf + 0x15A) - 1; /* before first group */
        int16_t numGroups = comp->totalMatchesInRound;

        for (;;) {
            slot++;           /* advance to next group */
            d2++;
            if ((uint16_t)d2 > (uint16_t)numGroups)
                break;            /* past all groups */
            if ((uint16_t)d1 > (uint16_t)slot->numTeams)
                break;            /* pos exceeds this group's team count */
        }

        slot--;               /* back up to previous group */

        if (slot->isLeagueFormat != 0) {
            /* League-type group */
            if ((uint16_t)d2 > (uint16_t)numGroups)
                return d1;        /* past all groups, return position as-is */
            /* Fall through to position encoding */
        } else {
            /* Cup-type group: check team counts for special returns */
            int16_t groupTeams = (int16_t)slot->numTeams;
            if (groupTeams == 2) return -100;
            if (groupTeams == 4) return -101;
            if (groupTeams == 8) return -102;
            /* Fall through to position encoding */
        }

        d2--;
        return -(d2 + 110);
    }
}


/* ================================================================
 * Career_FillCurrentSeasonInfo  (SWOS2.s line 53078)
 *
 * Fills the current season's management record with:
 * - Trophy flags (10 booleans packed into uint16)
 * - Board confidence modifiers/values
 * - Best skilled player(s) name and skill sum
 * - Competition position results (primary/secondary/tertiary/quaternary)
 * - Qualifying stage status
 * - Manager status (sacked/season finished)
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_FillCurrentSeasonInfo(void)
{
    SeasonInformations *a5;
    uint16_t d0;
    int16_t d1;

    if (_gameType != 4)
        return;

    UpdateBoardConfidenceModifiers();
    a5 = (SeasonInformations *)GetCurrentSeasonPointer();

    /* --- Build trophy flags word --- */
    d0 = 0;
    if (_wonWorldCup != 0)              d0 |= (1 << 0);
    if (_wonContinentalCup != 0)        d0 |= (1 << 1);
    if (_majorTrophyBonusActive != 0)   d0 |= (1 << 2);
    if (_cupBonus2Active != 0)          d0 |= (1 << 3);
    if (_cupBonus1Active != 0)          d0 |= (1 << 4);
    if (_playWinCupAnimation != 0) {
        if (_currentDivisionIndex == 0)
            d0 |= (1 << 5);
        else
            d0 |= (1 << 6);
    }
    if (_minorTrophyBonusActive != 0)   d0 |= (1 << 7);
    if (_leagueTitleBonusActive != 0)   d0 |= (1 << 8);
    if (_wonInvitationTournament != 0)  d0 |= (1 << 9);

    a5->trophyFlags = d0;

    /* --- Manager status for final season / sacked --- */
    if (_currentSeasonShowing == 0x13 && _seasonFinished != 0)
        a5->managerStatus = 2;
    if (_youreSacked != 0)
        a5->managerStatus = (int8_t)0xFF;

    /* --- Board confidence modifiers and values --- */
    a5->totalGames        = (uint8_t)_boardConfidenceMod0;
    a5->totalWins         = (uint8_t)_boardConfidenceMod1;
    a5->totalDrawn        = (uint8_t)_boardConfidenceMod2;
    a5->totalLost         = (uint8_t)_boardConfidenceMod3;
    a5->totalGoalsScored  = _boardConfidenceValue0;
    a5->totalGoalsTaken   = _boardConfidenceValue1;
    a5->friendlyWins      = (uint8_t)_boardConfidenceMod4;
    a5->friendlyDraws     = (uint8_t)_boardConfidenceMod5;
    a5->friendlyLost      = (uint8_t)_boardConfidenceMod6;
    a5->tourWins          = (uint8_t)_boardConfidenceMod7;
    a5->tourDraws         = (uint8_t)_boardConfidenceMod8;
    a5->tourLost          = (uint8_t)_boardConfidenceMod9;

    /* --- Find best-skilled player(s) --- */
    {
        uint16_t bestSkill = 0;   /* d3 */
        uint16_t bestCount = 0;   /* d4 */
        uint8_t *bestPlayer = 0;  /* a1 */
        uint8_t *best2nd = 0;     /* a2 */
        int16_t i = (int16_t)_careerNumPlayers - 1;

        while (i >= 0) {
            uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
            PlayerFile *pf = (PlayerFile *)(player + sizeof(TeamFile));
            uint16_t skill = (uint16_t)pf->goalStats[0] + (uint16_t)pf->goalStats[1]
                           + (uint16_t)pf->goalStats[2] + (uint16_t)pf->goalStats[3];

            if (skill > bestSkill) {
                bestCount = 1;
                bestSkill = skill;
                bestPlayer = player;
            } else if (skill == bestSkill) {
                bestCount++;
                best2nd = bestPlayer;
                bestPlayer = player;
            }
            i--;
        }

        a5->bestScorerNumGoals = bestSkill;

        if (bestCount == 1) {
            StringCopy(bestPlayer + 0x4F, (uint8_t *)a5->bestScorerName);
            a5->secondScorerName[0] = 0;
        } else if (bestCount == 2) {
            StringCopy(bestPlayer + 0x4F, (uint8_t *)a5->bestScorerName);
            StringCopy(best2nd    + 0x4F, (uint8_t *)a5->secondScorerName);
        } else {
            /* 3+ players tied: "SEVERAL" */
            StringCopy(_aSeveral, (uint8_t *)a5->bestScorerName);
            a5->secondScorerName[0] = 0;
        }
    }

    /* --- Competition position results --- */
    a5->tournamentStatus = 0;

    /* Primary competition */
    {
        d1 = (int16_t)FindTeamInDIY(_selTeamNumber,
                                      (DIYCompetition *)&_primaryCompetitionBuf);
        uint8_t *compDef = _currentCompetitionPtr;

        if (d1 >= 0) {
            d1 = EncodeCompetitionPositionForSeasonInfo_impl(
                d1, (uint8_t *)&_primaryCompetitionBuf, compDef);
            a5->comp1Result = (int8_t)d1;

            /* Check qualifying stage status (WC or EC qualifier) */
            uint8_t defId = compDef ? comp_id(compDef) : 0xFFu;
            if (comp_is_wc_qualifier(defId) || comp_is_ec_qualifier(defId))
            {
                a5->tournamentStatus = 1;
                if (_qualifyingStage1PendingFlag0 == 0 &&
                    _qualifyingStage1PendingFlag1 == 0)
                {
                    a5->tournamentStatus = 2;
                    if (_qualifyingStage2PendingFlag0 == 0 &&
                        _qualifyingStage2PendingFlag1 == 0)
                    {
                        a5->tournamentStatus = 0;
                    }
                }
            }
        }
    }

    /* Secondary competition */
    if (a5->strPtr1 != 0) {
        d1 = (int16_t)FindTeamInDIY(_selTeamNumber,
                                      (DIYCompetition *)&_competitionContextBuf);
        uint8_t *compDef = _secondaryCompetitionPtr;
        d1 = EncodeCompetitionPositionForSeasonInfo_impl(
            d1, (uint8_t *)&_competitionContextBuf, compDef);
        a5->comp2Result = (int8_t)d1;
    }

    /* Tertiary competition */
    if (a5->strPtr2 != 0) {
        d1 = (int16_t)FindTeamInDIY(_selTeamNumber,
                                      (DIYCompetition *)&_tertiaryCompetitionContextBuf);
        uint8_t *compDef = _tertiaryCompetitionPtr;
        d1 = EncodeCompetitionPositionForSeasonInfo_impl(
            d1, (uint8_t *)&_tertiaryCompetitionContextBuf, compDef);
        a5->comp3Result = (int8_t)d1;
    }

    /* Quaternary competition */
    if (a5->strPtr3 != 0) {
        d1 = (int16_t)FindTeamInDIY(_selTeamNumber,
                                      (DIYCompetition *)&_euroCupsCompetitionContextBuf);
        uint8_t *compDef = _quaternaryCompetitionPtr;
        d1 = EncodeCompetitionPositionForSeasonInfo_impl(
            d1, (uint8_t *)&_euroCupsCompetitionContextBuf, compDef);
        a5->comp4Result = (int8_t)d1;
    }
}


/* ================================================================
 * Chunk 138 — FillManagementRecordInfo + helpers
 * ================================================================ */

/* --- String externs for Chunk 138 --- */
extern uint8_t _aFriendly[];          /* "FRIENDLY" */
extern uint8_t _aFriendlies[];        /* "FRIENDLIES" */
extern uint8_t _aWon0Drawn1Lost[];    /* "WON %0  DRAWN %1  LOST %2" */
extern uint8_t _aEuropeanTour[];      /* "EUROPEAN TOUR" */
extern uint8_t _aOceaniaTour[];       /* "OCEANIA TOUR" */
extern uint8_t _aWinners[];           /* "WINNERS" */
extern uint8_t _aRunnerUp[];          /* "RUNNER-UP" */
extern uint8_t _aFinalist[];          /* "FINALIST" */
extern uint8_t _aReachedSemiFin[];    /* "REACHED SEMI-FINAL" */
extern uint8_t _aReachedQuarter[];    /* "REACHED QUARTER-FINAL" */
extern uint8_t _aThirdPlace[];        /* "THIRD PLACE" */
extern uint8_t _aFourthPlace[];       /* "FOURTH PLACE" */
extern uint8_t _aStillInCompeti[];    /* "STILL IN COMPETITION" */
extern uint8_t _aKnockedOutInRo[];    /* "KNOCKED OUT IN ROUND %0" */
extern uint8_t _aPosition0[];         /* "POSITION: %0" */
extern uint8_t _aQualified[];         /* "(QUALIFIED)" */
extern uint8_t _aNotQualified[];      /* "(NOT QUALIFIED)" */
extern uint8_t _aPromoted[];          /* "(PROMOTED)" */
extern uint8_t _aRelegated[];         /* "(RELEGATED)" */
extern uint8_t _a12Season[];          /* "%1/%2 SEASON" */
extern uint8_t _a102Season[];         /* "%1/0%2 SEASON" */
extern uint8_t _aNational[];          /* "NATIONAL" */
extern uint8_t _aA[];                 /* "(%a)" */
extern uint8_t _aA0[];                /* "%a  %0" */
extern uint8_t _aSeasonSTopScor[];    /* "SEASON'S TOP SCORER" */
extern uint8_t _aSeasonSTopScor_0[];  /* "SEASON'S TOP SCORERS" */
extern uint8_t _aResigned[];          /* "- RESIGNED -" */
extern uint8_t _aRetired[];           /* "- RETIRED -" */
extern void *_countriesTable[];
extern uint16_t _trophySprites[];

#define CN_EUROPE   80
#define CN_OCEANIA  85


/* ================================================================
 * SetTrophySprite  (SWOS2.s line 58364)
 *
 * Sets sprite ID for one trophy slot, makes it visible, advances
 * slot pointer, and shifts all 4 trophy X positions left by 10.
 * ================================================================ */
static MenuEntry *SetTrophySprite_Internal(uint16_t trophyIndex, MenuEntry *slot)
{
    MenuEntry *base = &_menuEntries[17]; /* entry at $3B8/56 = 17 */

    /* Set sprite ID in high word of fg (68k big-endian: move.w to offset 38) */
    slot->fgWord = _trophySprites[trophyIndex];

    /* Make this slot visible */
    slot->isInvisible = 0;

    /* Advance to next slot */
    slot++;

    /* Shift all 4 trophy slot X positions left by 10 */
    base[0].x -= 10;
    base[1].x -= 10;
    base[2].x -= 10;
    base[3].x -= 10;

    return slot;
}


/* ================================================================
 * SetTrophySprites  (SWOS2.s line 58324)
 *
 * Initialize trophy slot entries and populate from season info flags.
 * ================================================================ */
static void SetTrophySprites_Internal(SeasonInformations *si)
{
    MenuEntry *base = &_menuEntries[17]; /* entry at $3B8/56 = 17 */
    uint16_t d0;
    uint16_t flags;
    uint16_t bit;
    MenuEntry *slot;

    /* Set all 4 trophy slots invisible */
    base[0].isInvisible = 1;
    base[1].isInvisible = 1;
    base[2].isInvisible = 1;
    base[3].isInvisible = 1;

    /* Set X positions starting at 176, spaced 20 apart */
    d0 = 176;
    base[0].x = d0; d0 += 20;
    base[1].x = d0; d0 += 20;
    base[2].x = d0; d0 += 20;
    base[3].x = d0;

    /* Read trophy flags */
    flags = si->trophyFlags;
    if (flags == 0)
        return;

    /* Loop through bits 0-9, set sprite for each trophy */
    slot = &_menuEntries[17];
    for (bit = 0; bit < 10; bit++)
    {
        if (flags & (1 << bit))
        {
            slot = SetTrophySprite_Internal(bit, slot);
        }
    }
}


/* ================================================================
 * RenderCompLine  (SWOS2.s line 58160)
 *
 * Render one competition result block (2-3 lines) in the
 * management record menu. Modifies the running Y position.
 * ================================================================ */
static void RenderCompLine_Internal(const uint8_t *compName, int8_t resultCodeByte,
                                    uint8_t qualFlag, SeasonInformations *si, uint16_t *pY)
{
    const uint8_t *origCompName = compName;
    MenuEntry *entry7;
    MenuEntry *entry8;
    uint8_t *dest;
    int16_t resultCode;
    const uint8_t *fmt;
    uint16_t n0 = 0, n1 = 0, n2 = 0;
    uint16_t colorBits;

    if (compName == (const uint8_t *)0)
        return;

    /* Entry 7 = competition name line */
    entry7 = &_menuEntries[7]; /* $188/56 = 7 */
    entry7->y = *pY;

    /* Change "FRIENDLY" to "FRIENDLIES" for the header */
    if (compName == _aFriendly)
        compName = _aFriendlies;

    /* Copy competition name to entry 7's string buffer */
    dest = (uint8_t *)entry7->fg;
    StringCopy(compName, dest);

    /* Sign-extend result code byte to word */
    resultCode = (int16_t)resultCodeByte;

    /* Entry 8 = result/status line */
    entry8 = &_menuEntries[8]; /* $1C0/56 = 8 */
    entry8->y = *pY;

    /* Set color to 2 (clear low nibble, set to 2) */
    entry8->stringFlags = (entry8->stringFlags & 0xFFF0) | 2;

    /* Default format = dash */
    fmt = _aDash;

    if (resultCode == 0)
    {
        /* No result — just print dash */
        goto do_print;
    }

    /* --- FRIENDLY path --- */
    if (origCompName == _aFriendly)
    {
        n0 = (uint16_t)(int16_t)(int8_t)si->friendlyWins;
        n1 = (uint16_t)(int16_t)(int8_t)si->friendlyDraws;
        n2 = (uint16_t)(int16_t)(int8_t)si->friendlyLost;
        fmt = _aDash;
        if (n0 != 0 || n1 != 0 || n2 != 0)
            fmt = _aWon0Drawn1Lost;
        goto do_print;
    }

    /* --- TOUR path (European Tour .. Oceania Tour) --- */
    if (origCompName >= _aEuropeanTour && origCompName <= _aOceaniaTour)
    {
        n0 = (uint16_t)(int16_t)(int8_t)si->tourWins;
        n1 = (uint16_t)(int16_t)(int8_t)si->tourDraws;
        n2 = (uint16_t)(int16_t)(int8_t)si->tourLost;
        fmt = _aDash;
        if (n0 != 0 || n1 != 0 || n2 != 0)
            fmt = _aWon0Drawn1Lost;
        goto do_print;
    }

    /* --- League/Cup result codes --- */
    if (resultCode == 1)
    {
        /* WINNERS — set color to 15 (yellow/gold) */
        entry8->stringFlags = (entry8->stringFlags & 0xFFF0) | 0xF;
    }

    fmt = _aWinners;
    if (resultCode == 1)         goto do_print;
    fmt = _aRunnerUp;
    if (resultCode == 2)         goto do_print;
    fmt = _aFinalist;
    if (resultCode == -100)      goto do_print;
    fmt = _aReachedSemiFin;
    if (resultCode == -101)      goto do_print;
    fmt = _aReachedQuarter;
    if (resultCode == -102)      goto do_print;
    fmt = _aThirdPlace;
    if (resultCode == -103)      goto do_print;
    fmt = _aFourthPlace;
    if (resultCode == -105)      goto do_print;
    fmt = _aStillInCompeti;
    if (resultCode == -104)      goto do_print;
    fmt = _aDash;
    if (resultCode == -106)      goto do_print;

    if (resultCode >= 0)
    {
        /* Positive position */
        fmt = _aPosition0;
        n0 = (uint16_t)resultCode;
        goto do_print;
    }

    if (resultCode > -110)
    {
        /* Negative position (unknown code): display as positive */
        fmt = _aPosition0;
        n0 = (uint16_t)(-resultCode);
        goto do_print;
    }

    /* Knocked out in round N (resultCode <= -110) */
    {
        int16_t round = resultCode + 110;
        n0 = (uint16_t)(-round);
        fmt = _aKnockedOutInRo;
    }

do_print:
    /* Print result to entry 8's string buffer */
    dest = (uint8_t *)entry8->fg;
    PrintFormatted(fmt, dest,
                    n0, n1, n2, 0, 0, 0, 0,
                    (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);

    /* Draw 2 entries (entry 7 and entry 8) */
    DrawMultipleItems(2, &_menuEntries[7]);

    *pY += 10;

    /* --- Qualification/promotion line --- */
    if (qualFlag == 0)
        return;

    entry8 = &_menuEntries[8];
    entry8->y = *pY;

    /* Clear output buffer first byte */
    dest = (uint8_t *)entry8->fg;
    *dest = 0;

    if (qualFlag == 1)
    {
        fmt = _aQualified;
        colorBits = 6;
    }
    else if (qualFlag == 2)
    {
        fmt = _aNotQualified;
        colorBits = 10;
    }
    else if (qualFlag == 3)
    {
        fmt = _aPromoted;
        colorBits = 6;
    }
    else
    {
        fmt = _aRelegated;
        colorBits = 10;
    }

    StringCopy(fmt, dest);
    entry8->stringFlags = (entry8->stringFlags & 0xFFF0) | colorBits;

    /* Draw 2 entries again (with qualification text) */
    DrawMultipleItems(2, &_menuEntries[7]);

    *pY += 10;
}


/* ================================================================
 * FillManagementRecordInfo  (SWOS2.s line 57944)
 *
 * Populate the entire management record menu with season statistics,
 * competition results, top scorer, manager status, and trophies.
 * Called as a menu beforeDraw callback.
 * ================================================================ */
__attribute__((used, externally_visible))
void FillManagementRecordInfo(void)
{
    SeasonInformations *si;
    uint16_t d5;
    MenuEntry *entry;
    uint8_t *dest;
    uint16_t yearLo;

    /* Compute season info pointer */
    si = &((SeasonInformations *)_seasonInformationsArray)[_currentSeasonNumber];

    /* Initialize trophy sprites */
    SetTrophySprites_Internal(si);

    d5 = 55; /* starting Y */

    /* --- Season title line (entry 6) --- */
    entry = &_menuEntries[6]; /* $150/56 = 6 */
    dest = (uint8_t *)entry->fg;
    {
        uint16_t seasonNum = _currentSeasonNumber;
        uint16_t d0 = seasonNum + 1;
        uint16_t d1 = seasonNum + World_FirstYear();     /* (the world's years: world.h) */
        uint16_t d2 = (uint16_t)((d1 + 1) % 100);
        const uint8_t *fmt;

        if (d2 >= 100)
            d2 -= 100;

        if (d2 >= 10)
            fmt = _a12Season;     /* "%1/%2 SEASON" */
        else
            fmt = _a102Season;    /* "%1/0%2 SEASON" */

        PrintFormatted(fmt, dest,
                        d0, d1, d2, 0, 0, 0, 0,
                        (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);
    }
    entry->y = d5;
    d5 += 15;

    /* --- Club name line (entry 4) --- */
    entry = &_menuEntries[4]; /* $E0/56 = 4 */
    dest = (uint8_t *)entry->fg;
    StringCopy((uint8_t *)si->clubName, dest);
    entry->y = d5;
    d5 += 10;

    /* --- Country line (entry 5) --- */
    entry = &_menuEntries[5]; /* $118/56 = 5 */
    dest = (uint8_t *)entry->fg;
    {
        /* HIGH byte. clubNo is the packed team id, (country << 8) | ordinal,
         * and the original reads it with `move.b (a6),d0` — offset 0 of a
         * big-endian word, i.e. the country. Casting the uint16 to uint8 on a
         * little-endian host takes the LOW half instead, which is the team
         * ordinal: a national side came out as whatever country that ordinal
         * happened to index, e.g. "BRAZIL (BELGIUM)". Every other reader in the
         * tree already does `_selTeamNumber >> 8` (career_init.c:1169 spells it
         * out); this was the one that didn't. SWOS2.s:88206. */
        uint8_t countryCode = (uint8_t)(si->clubNo >> 8);
        const uint8_t *countryStr;

        if (countryCode >= CN_EUROPE && countryCode <= CN_OCEANIA)
        {
            countryStr = _aNational;
        }
        else
        {
            const uint8_t *cPtr = (const uint8_t *)(_countriesTable[countryCode]);
            countryStr = cPtr + 1; /* skip first byte */
        }

        PrintFormatted(_aA, dest,
                        0, 0, 0, 0, 0, 0, 0,
                        countryStr, (const uint8_t *)0, (const uint8_t *)0);
    }
    entry->y = d5;
    d5 += 17;

    /* --- Competition results (4 lines) --- */
    RenderCompLine_Internal(si->leagueStrPtr,
                            si->comp1Result, si->tournamentStatus, si, &d5);
    RenderCompLine_Internal(si->strPtr1,
                            si->comp2Result, 0, si, &d5);
    RenderCompLine_Internal(si->strPtr2,
                            si->comp3Result, 0, si, &d5);
    RenderCompLine_Internal(si->strPtr3,
                            si->comp4Result, 0, si, &d5);

    d5 += 8;

    /* --- Overall stats header (entry 10) --- */
    entry = &_menuEntries[10]; /* $230/56 = 10 */
    entry->y = d5;
    d5 += 9;

    /* --- Overall stats line (entry 11) --- */
    entry = &_menuEntries[11]; /* $268/56 = 11 */
    dest = (uint8_t *)entry->fg;

    if (si->bestScorerName[0] == 0)
    {
        /* No scorer name — show dash */
        StringCopy(_aDash, dest);
    }
    else
    {
        uint16_t savedY = d5;
        uint16_t totalGames = (uint16_t)si->totalGames;
        const uint8_t *fmt;

        if (totalGames == 0)
        {
            fmt = _aDash;
            PrintFormatted(fmt, dest,
                            0, 0, 0, 0, 0, 0, 0,
                            (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);
        }
        else
        {
            uint16_t tw = (uint16_t)si->totalWins;
            uint16_t td = (uint16_t)si->totalDrawn;
            uint16_t tl = (uint16_t)si->totalLost;
            uint16_t gs = si->totalGoalsScored;
            uint16_t gt = si->totalGoalsTaken;
            PrintFormatted(_aPl0W1D2L3F4A5, dest,
                            totalGames, tw, td, tl, gs, gt, 0,
                            (const uint8_t *)0, (const uint8_t *)0, (const uint8_t *)0);
        }
        d5 = savedY;
    }

    entry->y = d5;

    /* Draw overall stats block (entries 10-11) */
    DrawMultipleItems(2, &_menuEntries[10]);

    d5 += 13;

    /* --- Manager status (entry 9) --- */
    _menuEntries[9].isInvisible = 1;

    if (si->managerStatus != 0)
    {
        uint16_t color;
        int8_t status = si->managerStatus;

        _menuEntries[9].isInvisible = 0;
        entry = &_menuEntries[9]; /* $1F8/56 = 9 */
        entry->y = d5;

        color = 10;
        if (status >= 0)
        {
            /* Not sacked — check resigned/retired */
            color = 6;
            entry->fg = (uintptr_t)_aResigned;
            if (status != 1)
            {
                color = 15;
                entry->fg = (uintptr_t)_aRetired;
            }
        }

        /* Update stringFlags: set color and bit 13 */
        {
            uint16_t sf = entry->stringFlags;
            sf = (sf & 0xFFF0) | color;
            sf |= (1 << 13);
            entry->stringFlags = sf;
        }

        /* Draw manager status entry */
        DrawMultipleItems(1, &_menuEntries[9]);
    }

    d5 += 13;

    /* --- Top scorer header (entry 12) --- */
    entry = &_menuEntries[12]; /* $2A0/56 = 12 */
    entry->fg = (uintptr_t)_aSeasonSTopScor; /* "SEASON'S TOP SCORER" */

    if (si->bestScorerName[0] != 0 && si->secondScorerName[0] != 0)
    {
        entry->fg = (uintptr_t)_aSeasonSTopScor_0; /* "SEASON'S TOP SCORERS" */
    }

    entry->y = d5;
    d5 += 10;

    /* --- Top scorer line 1 (entry 13) --- */
    entry = &_menuEntries[13]; /* $2D8/56 = 13 */
    dest = (uint8_t *)entry->fg;

    {
        uint16_t numGoals = si->bestScorerNumGoals;

        if (numGoals == 0 || si->bestScorerName[0] == 0)
        {
            StringCopy(_aDash, dest);
        }
        else
        {
            const uint8_t *scorerName = (const uint8_t *)si->bestScorerName;
            PrintFormatted(_aA0, dest,
                            numGoals, 0, 0, 0, 0, 0, 0,
                            scorerName, (const uint8_t *)0, (const uint8_t *)0);
        }
    }

    entry->y = d5;

    /* Draw top scorer block (entries 12-13) */
    DrawMultipleItems(2, &_menuEntries[12]);

    d5 += 8;

    /* --- Top scorer line 2 / second scorer (entry 14) --- */
    _menuEntries[14].isInvisible = 1; /* $310/56 = 14 */

    if (si->bestScorerName[0] != 0 && si->secondScorerName[0] != 0)
    {
        uint16_t numGoals;

        _menuEntries[14].isInvisible = 0;
        entry = &_menuEntries[14];
        dest = (uint8_t *)entry->fg;

        numGoals = si->bestScorerNumGoals;

        PrintFormatted(_aA0, dest,
                        numGoals, 0, 0, 0, 0, 0, 0,
                        (const uint8_t *)si->secondScorerName,
                        (const uint8_t *)0, (const uint8_t *)0);

        entry->y = d5;

        DrawMultipleItems(1, &_menuEntries[14]);

        d5 += 10;
    }

    d5 += 4;
    d5 = 36; /* override — fixed Y for trophy row */

    /* --- Trophy Y positions (entries 17-20) --- */
    /* asm: tst.w SEASONINFORMATIONS_TROPHYFLAGS(a6) — must read the struct
     * field, not `si + SI_TROPHYFLAGS` (that scales by sizeof(SeasonInformations)
     * and reads ~2.4 KB out of bounds, leaving the trophy row Y stale). */
    if (si->trophyFlags != 0)
    {
        _menuEntries[17].y = d5;
        _menuEntries[18].y = d5;
        _menuEntries[19].y = d5;
        _menuEntries[20].y = d5;
    }
}
