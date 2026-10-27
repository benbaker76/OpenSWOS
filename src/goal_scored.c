/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* goal_scored.c — Chunk 38b: GoalScored
 *
 * Ported from SWOS2.s lines 12091-12366.
 * Updates goal counters, builds scorer list sprites with names and times.
 */

#include "goal_scored.h"
#include "game_types.h"

/* ---- external asm globals ---- */
extern uint16_t _goalScored;
extern uint16_t _runSlower;
extern uint16_t _lastTeamScoredNumber;
extern Sprite  *_lastPlayerScored;
extern uintptr_t _currentScorer;
extern TeamGame        _topTeamInGame;
extern TeamGame        _bottomTeamInGame;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;
extern TeamGeneralInfo *_lastTeamScored;
extern uint16_t _team1TotalGoals;
extern uint16_t _team2TotalGoals;
extern uint16_t _playingPenalties;
extern uint16_t _team1PenaltyGoals;
extern uint16_t _team2PenaltyGoals;
extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint16_t _team1GoalsDigit1;
extern uint16_t _team1GoalsDigit2;
extern uint16_t _team2GoalsDigit1;
extern uint16_t _team2GoalsDigit2;
extern uint16_t _penalty;
extern uint16_t _panicReasonCode;
extern ScorerInfo      _team1Scorers[];
extern ScorerInfo      _team2Scorers[];
extern Sprite          _team1ScorerSprite[];
extern Sprite          _team2ScorerSprite[];
extern uint16_t _team1ScorerSpritesWidths[];
extern uint16_t _team2ScorerSpritesWidths[];
extern TeamGame       *_topTeamPtr;
extern TeamGame       *_bottomTeamPtr;
extern uint16_t _ingameTeamPlayerOffsets[];
extern uint8_t  _smallCharsTable[];
extern uint8_t  _gameTime[];

/* ---- external functions ---- */
extern void PanicHaltLoop(void);
extern void HWSprite_LoadFromSpriteGraphics(uint16_t spriteIndex);
extern uint16_t Text2Sprite(uint16_t xOff, uint16_t yOff, uint16_t dstSpriteIndex,
                             uint8_t *text, uint8_t *charsTable, uint16_t planeMask,
                             uint16_t *outTotalWidth);

/* ---- Goal time format template strings (writable) ----
 * First 4 bytes get overwritten with game time before rendering.
 * Indices: [0]=normal, [1]=penalty, [2]=own goal
 */
static char goalStr_first_normal[6]   = { '*','*','*','*', 0, 0 };
static char goalStr_already_normal[6] = { '*','*','*','*', ',', 0 };
static char goalStr_first_pen[10]     = { '*','*','*','*', '(','P','E','N',')', 0 };
static char goalStr_already_pen[12]   = { '*','*','*','*', '(','P','E','N',')',',', 0, 0 };
static char goalStr_first_og[10]      = { '*','*','*','*', '(','O','G',')', 0, 0 };
static char goalStr_already_og[6]     = { '*','*','*','*', ',', 0 };

static char *alreadyScoredTable[3] = {
    goalStr_already_normal,     /* [0] normal */
    goalStr_already_pen,        /* [1] penalty */
    goalStr_already_og          /* [2] own goal */
};

static char *firstGoalScoredTable[3] = {
    goalStr_first_normal,       /* [0] normal */
    goalStr_first_pen,          /* [1] penalty */
    goalStr_first_og            /* [2] own goal */
};

/* ================================================================
 * GoalScored
 * (SWOS2.s line 12091)
 *
 * Updates goal counters and builds scorer list sprites.
 * scoringTeam: 1 = team 1 scored, 2 = team 2 scored
 * scorerSprite: pointer to the Sprite struct of the scorer
 * ================================================================ */
void __attribute__((used, externally_visible))
GoalScored(int16_t scoringTeam, Sprite *scorerSprite)
{
    TeamGame       *a2;         /* team file data: scorer's team (for player lookup) */
    TeamGame       *a1_team;    /* team file data: opponent team (for own goal stat) */
    ScorerInfo     *scorers;    /* a3: team1Scorers or team2Scorers */
    Sprite         *scorerSprites; /* a4: team1ScorerSprite or team2ScorerSprite */
    uint16_t       *sprWidths;  /* a6: scorer sprite widths array */
    TeamGame       *inGameTeam; /* a3 temp: in-game team struct */
    int16_t         goalType;   /* d1: 0=normal, 1=penalty, 2=own goal */
    int16_t         scorerKey;  /* d0: shirt number (or +100 for own goal) */
    uint16_t        d7;         /* current sprite row index */
    int16_t         d6;         /* remaining available sprite rows */
    int16_t         d5;         /* goal count-down for loop */
    Sprite         *a5;         /* current scorer sprite pointer */
    uint16_t        d4;         /* current sprite imageIndex */
    uint16_t        xOff;       /* d1: current x offset in sprite */

    /* ---- set global goal flags ---- */
    _goalScored = 1;
    _runSlower = 1;
    _lastTeamScoredNumber = (uint16_t)scoringTeam;
    _lastPlayerScored = scorerSprite;
    _currentScorer = (uintptr_t)scorerSprite;

    /* ---- find in-game team matching scorer's side ---- */
    inGameTeam = &_topTeamInGame;
    if (scorerSprite->teamNumber != 1)
        inGameTeam = &_bottomTeamInGame;

    /* ---- find team general info matching the in-game team ---- */
    a2 = (TeamGame *)_topTeamData.inGameTeamPtr;
    if ((uintptr_t)inGameTeam != (uintptr_t)_topTeamData.inGameTeamPtr)
        a2 = (TeamGame *)_bottomTeamData.inGameTeamPtr;

    /* a2 should be the TeamGame that matches the scorer's side;
     * lastTeamScored stores the TeamGeneralInfo pointer for the scoring team.
     * ASM uses inGameTeamPtr comparison (not teamNumber) to handle teamPlayingUp swap. */
    if ((uintptr_t)inGameTeam == (uintptr_t)_topTeamData.inGameTeamPtr)
        _lastTeamScored = &_topTeamData;
    else
        _lastTeamScored = &_bottomTeamData;

    /* ---- increment goal counters ---- */
    if (scoringTeam == 2)
        goto inc_team2;

    /* team 1 */
    if (_team1TotalGoals == 99)
        return;
    if (_playingPenalties != 0)
        _team1PenaltyGoals++;
    else
        _team1TotalGoals++;
    _statsTeam1Goals++;
    _team1GoalsDigit2++;
    if (_team1GoalsDigit2 == 10) {
        _team1GoalsDigit1++;
        _team1GoalsDigit2 = 0;
    }
    goto skip_if_penalties;

inc_team2:
    if (_team2TotalGoals == 99)
        return;
    if (_playingPenalties != 0)
        _team2PenaltyGoals++;
    else
        _team2TotalGoals++;
    _statsTeam2Goals++;
    _team2GoalsDigit2++;
    if (_team2GoalsDigit2 == 10) {
        _team2GoalsDigit1++;
        _team2GoalsDigit2 = 0;
    }

skip_if_penalties:
    if (_playingPenalties != 0)
        return;

#ifdef SWOS_SDL_BUILD
    /* Arcade golden goal: the first (non-penalty) goal ends the match. No-op
     * unless _gArcadeMode + golden-goal is armed (checked inside). */
    { extern void Arcade_GoalScored(void); Arcade_GoalScored(); }
#endif

    /* null check scorer sprite */
    if (scorerSprite == (Sprite *)0) {
        _panicReasonCode = 9;
        PanicHaltLoop();
    }

    /* ---- determine goal type ---- */
    goalType = GT_NORMAL;
    if (scorerSprite->teamNumber != scoringTeam)
        goalType = GT_OWN_GOAL;
    if (_penalty != 0)
        goalType = GT_PENALTY;

    /* ---- select scorer tables by team ---- */
    if (scoringTeam == 1) {
        scorers = _team1Scorers;
        scorerSprites = _team1ScorerSprite;
        sprWidths = _team1ScorerSpritesWidths;
        if (goalType == GT_OWN_GOAL)
            goto own_goal_ptrs;
        goto normal_ptrs;
    } else {
        scorers = _team2Scorers;
        scorerSprites = _team2ScorerSprite;
        sprWidths = _team2ScorerSpritesWidths;
        if (goalType == GT_OWN_GOAL)
            goto normal_ptrs;
        goto own_goal_ptrs;
    }

normal_ptrs:
    a2 = _topTeamPtr;
    a1_team = _bottomTeamPtr;
    goto team_ptrs_done;

own_goal_ptrs:
    a2 = _bottomTeamPtr;
    a1_team = _topTeamPtr;

team_ptrs_done:
    /* ---- own goal: increment own goal count and clear lastPlayerScored ---- */
    {
        int16_t shirtNum = scorerSprite->playerOrdinal;

        if (goalType == GT_OWN_GOAL) {
            a1_team->numOwnGoals += 1;
            _lastPlayerScored = (Sprite *)0;
        }

        /* ---- compute player data offset and update stats ---- */
        {
            int16_t playerIdx = shirtNum - 1;

            if (goalType != GT_OWN_GOAL) {
                a2->players[playerIdx].goalsScored += 1;
            }

            /* get the shirt number from team file data */
            scorerKey = a2->players[playerIdx].shirtNumber;

            if (goalType == GT_OWN_GOAL)
                scorerKey += 100;
        }
    }

    /* ---- scan scorer slots to find or create entry ---- */
    d7 = 0;
    {
        int16_t slotsRemaining = 7;
        ScorerInfo *curScorer = scorers;

        while (slotsRemaining >= 0) {
            int16_t slotKey = curScorer->shirtNum;

            if (slotKey == 0) {
                /* empty slot: initialize */
                curScorer->numGoals = 0;
                curScorer->numSpritesTaken = 1;
                goto found_slot;
            }

            if (slotKey == scorerKey)
                goto found_slot;

            /* skip this scorer's sprites */
            d7 += (uint16_t)curScorer->numSpritesTaken;
            if (d7 >= 8)
                return;

            curScorer++;
            slotsRemaining--;
            continue;

        found_slot:
            scorers = curScorer;  /* update scorers to point at the matched entry */
            break;
        }

        if (slotsRemaining < 0)
            return;
    }

    /* ---- set up sprite pointers for this scorer ---- */
    d6 = scorers->numSpritesTaken - 1;
    a5 = scorerSprites + d7;

    {
        ScorerInfo *scorerInfo = scorers;  /* a1 in the time-rendering loop */
        int16_t numGoals;
        int16_t goalIdx;
        uint16_t spriteOrdinal;
        uint16_t playerDataOff;
        uint8_t *playerName;
        uint16_t totalWidth;
        uint16_t overflow;

        /* write scorer key */
        scorerInfo->shirtNum = scorerKey;

        /* check if already has max goals */
        numGoals = scorerInfo->numGoals;
        if (numGoals == 10)
            return;

        /* record this goal */
        scorerInfo->numGoals = numGoals + 1;
        {
            GoalInfo *gi = &scorerInfo->goals[numGoals];
            gi->goalType = goalType;

            /* copy game time and convert digits to ASCII */
            gi->time[0] = (int8_t)_gameTime[0];
            gi->time[1] = (int8_t)_gameTime[1];
            gi->time[2] = (int8_t)_gameTime[2];
            gi->time[3] = (int8_t)_gameTime[3];
            gi->time[1] += '0';
            gi->time[2] += '0';
            gi->time[3] += '0';
        }

        /* ---- load sprite graphics for current scorer sprite ---- */
        d4 = (uint16_t)a5->imageIndex;
        HWSprite_LoadFromSpriteGraphics(d4);

        /* ---- find player short name ---- */
        spriteOrdinal = (uint16_t)scorerSprite->playerOrdinal;
        playerDataOff = _ingameTeamPlayerOffsets[spriteOrdinal - 1];
        /* playerDataOff is a byte offset from TeamGame base to the PlayerInfo entry.
         * shortName is at offset 12 within PlayerInfo, and players[] starts at offset 42
         * in TeamGame, so we use raw byte arithmetic here (playerDataOff already includes
         * the players[] base). We add 54 (= 42 + 12 = players[0].shortName offset). */
        playerName = (uint8_t *)a2 + playerDataOff + 54;

        /* skip initials pattern "X. " at start of name */
        if (playerName[0] != 0 && playerName[1] != 0 &&
            playerName[1] == '.' && playerName[2] == ' ') {
            playerName += 3;
        }

        /* ---- render player name into sprite ---- */
        xOff = 0;
        overflow = Text2Sprite(0, 0, d4, playerName, _smallCharsTable, 2, &totalWidth);
        (void)overflow;
        xOff = totalWidth + 5;  /* space between name and goal list */

        /* ---- render goal times ---- */
        d5 = scorerInfo->numGoals - 1;
        goalIdx = 0;

    write_time_field_loop:
        {
            char **formatTable;
            int16_t curGoalType;
            char *templateStr;
            GoalInfo *curGoal;

            /* select template table based on whether this is the last goal */
            formatTable = (d5 != 0) ? alreadyScoredTable : firstGoalScoredTable;

            /* read goal type for current goal being rendered */
            curGoal = &scorerInfo->goals[goalIdx];
            curGoalType = curGoal->goalType;
            templateStr = formatTable[curGoalType];

            /* copy time bytes into template string's **** area */
            templateStr[0] = curGoal->time[0];
            templateStr[1] = curGoal->time[1];
            templateStr[2] = curGoal->time[2];
            templateStr[3] = curGoal->time[3];

            /* skip unused first byte, then skip leading '0's */
            {
                uint8_t *renderStart = (uint8_t *)templateStr + 1;
                if (*renderStart == '0') {
                    renderStart++;
                    if (*renderStart == '0')
                        renderStart++;
                }

                /* render time string into sprite */
                overflow = Text2Sprite(xOff, 0, d4, renderStart, _smallCharsTable, 2, &totalWidth);
            }
            xOff = totalWidth;

            if (overflow == 0) {
                /* text fits: advance to next goal */
                goalIdx++;
                d5--;
                if (d5 >= 0)
                    goto write_time_field_loop;

                /* all goals rendered: save final width */
                sprWidths[d7] = xOff;
                return;
            }

            /* ---- overflow: need a new sprite row ---- */
            sprWidths[d7] = xOff;
            d7++;
            if (d7 >= 8)
                return;

            d6--;
            if (d6 >= 0) {
                /* advance to existing next sprite row */
                a5++;
                d4 = (uint16_t)a5->imageIndex;
            } else {
                /* need to steal a sprite: shift rows down */
                Sprite   *shiftSrc;
                uint16_t *widthSrc;
                uint16_t  lastImageIdx;

                scorers->numSpritesTaken++;

                /* sprites[6] = second-to-last, sprites[7] = last */
                shiftSrc = scorerSprites + 6;   /* a2 = &sprites[6] */
                widthSrc = sprWidths + 6;        /* a3 = &widths[6] */
                lastImageIdx = (uint16_t)(scorerSprites + 7)->imageIndex;

                /* shift imageIndex and widths one position right, from [6] down to a5 */
                while ((uintptr_t)shiftSrc > (uintptr_t)a5) {
                    (shiftSrc + 1)->imageIndex = shiftSrc->imageIndex;
                    widthSrc[1] = widthSrc[0];
                    shiftSrc--;
                    widthSrc--;
                }

                a5++;
                d4 = lastImageIdx;
                a5->imageIndex = (int16_t)d4;
            }

            /* load sprite graphics for new row */
            HWSprite_LoadFromSpriteGraphics(d4);
            xOff = 0;
            goto write_time_field_loop;
        }
    }
}

/* ================================================================
 * RebuildScorerSprites
 *
 * Redraws every scorer-banner sprite from the scorer TABLES alone. A loaded
 * save state (src/challenge.c) restores the tables, the sprite structs, the
 * widths and numSpritesTaken -- the state registry carries them -- but the
 * PIXELS live in sprite-graphics slots, which are display state and
 * deliberately not in the registry. Without this a loaded position's banner
 * draws whatever those slots last held (the "score-panel graphics" issue in
 * docs/CHALLENGE.md).
 *
 * The LAYOUT was decided by GoalScored live -- rows per scorer, stolen rows
 * -- and is already in numSpritesTaken and the sprites' imageIndexes, so
 * nothing here allocates: it renders the same text through the same
 * templates, which fills the same rows to the same widths.
 * ================================================================ */
void __attribute__((used, externally_visible))
RebuildScorerSprites(void)
{
    int16_t t;

    for (t = 0; t < 2; t++) {
        ScorerInfo *scorers   = t == 0 ? _team1Scorers : _team2Scorers;
        Sprite     *sprites   = t == 0 ? _team1ScorerSprite : _team2ScorerSprite;
        uint16_t   *sprWidths = t == 0 ? _team1ScorerSpritesWidths : _team2ScorerSpritesWidths;
        /* GoalScored's own pairing: team 1's scorers are named from _topTeamPtr --
         * except an own goal, whose scorer is in the OTHER team (key >= 100). */
        TeamGame   *own       = t == 0 ? _topTeamPtr : _bottomTeamPtr;
        TeamGame   *other     = t == 0 ? _bottomTeamPtr : _topTeamPtr;
        int16_t     row = 0, slot;

        if (!own || !other)
            return;

        for (slot = 0; slot < 8 && row < 8; slot++) {
            ScorerInfo *sc = &scorers[slot];
            int16_t  rows = sc->numSpritesTaken > 0 ? sc->numSpritesTaken : 1;
            int16_t  lastRow = (int16_t)(row + rows - 1);
            TeamGame *nameTeam = own;
            uint8_t  *playerName = (uint8_t *)0;
            int16_t   key = sc->shirtNum, shirt, i, goalIdx;
            uint16_t  d4, xOff, totalWidth, overflow;

            if (key == 0)
                break;
            if (lastRow >= 8)
                lastRow = 7;
            shirt = key;
            if (key >= 100) {           /* own goal: +100, named from the opponents */
                nameTeam = other;
                shirt = (int16_t)(key - 100);
            }
            /* GoalScored went by the scorer's playerOrdinal, which the table does not
             * keep; the shirt number it does keep is unique within a side's sixteen. */
            for (i = 0; i < 16; i++)
                if (nameTeam->players[i].shirtNumber == (int8_t)shirt) {
                    playerName = (uint8_t *)nameTeam->players[i].shortName;
                    break;
                }
            if (!playerName) {
                row = (int16_t)(lastRow + 1);
                continue;
            }
            if (playerName[0] != 0 && playerName[1] == '.' && playerName[2] == ' ')
                playerName += 3;

            d4 = (uint16_t)sprites[row].imageIndex;
            HWSprite_LoadFromSpriteGraphics(d4);
            overflow = Text2Sprite(0, 0, d4, playerName, _smallCharsTable, 2, &totalWidth);
            (void)overflow;
            xOff = (uint16_t)(totalWidth + 5);

            for (goalIdx = 0; goalIdx < sc->numGoals; ) {
                GoalInfo *g = &sc->goals[goalIdx];
                char *tmpl = ((goalIdx < sc->numGoals - 1) ? alreadyScoredTable
                                                           : firstGoalScoredTable)[g->goalType];
                uint8_t *renderStart;

                tmpl[0] = (char)g->time[0];
                tmpl[1] = (char)g->time[1];
                tmpl[2] = (char)g->time[2];
                tmpl[3] = (char)g->time[3];
                renderStart = (uint8_t *)tmpl + 1;
                if (*renderStart == '0') {
                    renderStart++;
                    if (*renderStart == '0')
                        renderStart++;
                }
                overflow = Text2Sprite(xOff, 0, d4, renderStart, _smallCharsTable, 2,
                                       &totalWidth);
                xOff = totalWidth;
                if (overflow == 0) {
                    goalIdx++;
                    continue;
                }
                /* Same text as live play: the overflow falls on the same boundary the
                 * live match's did, so the scorer's own next row is the right one. */
                sprWidths[row] = xOff;
                if (row >= lastRow)
                    break;              /* the live layout never gets here; stop clean */
                row++;
                d4 = (uint16_t)sprites[row].imageIndex;
                HWSprite_LoadFromSpriteGraphics(d4);
                xOff = 0;
            }
            sprWidths[row] = xOff;
            row = (int16_t)(lastRow + 1);
        }
    }
}
