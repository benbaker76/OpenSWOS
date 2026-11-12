/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* match_sim.c — Match simulation / "view result" calculation
 *
 * Ported from SWOS2.s: CalculateViewResult, CalcTeamViewOffset,
 * ScaleViewIndexByTeamPriceRank, AssignSimulatedGoalsToScorers,
 * ApplyRandomPlayerStatusAfterSim, InitPlayerRemapTableForTeam,
 * BuildAvailablePlayersRemapAndMask, ComputeOldPlayerSkillForViewResult,
 * ScaleSkill, GetPlayerPricePercentageChange, AdjustPlayerSkills,
 * ApplyTeamTactics.
 *
 * CalcTeamPriceRankPercentOffset.
 *
 * UpdatePlayerPositionAndReturnRole remains in assembly with C-callable bridge.
 */

#include "match_sim.h"
#include "test_rules.h"
#ifdef SWOS_SDL_BUILD
#include <stdio.h>          /* MatchSim_LogSkillRoll */
#include <stdlib.h>         /* getenv: SWOS_SKILL_DEBUG */
#endif
#include "game_types.h"
#include "diy_competition.h"
#include "deterministic.h"
#include "auto_skip_debug.h"
#include "world_rules.h"         /* a world's CPU cards / injuries, keeper injuries */

/* Team control type constants (matching play_match_menu.h) */
#ifndef TEAM_COMPUTER
#define TEAM_COMPUTER      1
#define TEAM_PLAYER_COACH  2
#define TEAM_COACH         3
#endif

/* ---- Assembly externs (functions still in asm) ---- */
extern uint16_t Rand2(void);
extern uint16_t ShouldApplyRandomPlayerStatusAfterSim(void);

/* C functions (defined in diy_competition.c) */
extern uint16_t FindTeamInDIY(uint16_t teamNo, DIYCompetition *comp);
extern uint16_t GetMoreExpensiveTeamsCount(uint16_t teamNo, DIYCompetition *comp);

/* C-callable bridges to asm functions (defined in swos2_unit.s) */
/* Forward declaration: UpdatePlayerPositionAndReturnRole (Chunk 187) */
uint32_t UpdatePlayerPositionAndReturnRole(TeamFileEx *team, uint16_t playerIdx);
extern void Career_ApplyPostMatchPlayerStatModsIfCareerTeam(TeamFileEx *team, uint8_t *remap);

/* GetNumberOfAvailable* — return packed: d6 in low word, d7 in upper bits */
extern uint32_t GetNumberOfAvailablePlayers(uint8_t *team, int16_t excludeCardsFlag, uint16_t numPl);
extern uint32_t GetNumberOfAvailableSubstitutes(uint8_t *team, int16_t flag);
extern uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);

/* ---- Data externs (still in asm) ---- */
extern TeamFileEx _careerTeam;
extern uint8_t    _currentMatchPlayers[32];
extern uint16_t   _careerNumPlayers;
extern uint16_t   _trainingGame;
extern uint16_t   _gameType;
extern uint16_t   _currentSeasonShowing;
extern uint8_t    _newRandomSeedCopy;
extern uint8_t    _viewRandomPhase;
extern uint8_t    _seasonPlaying;
extern uint8_t    _kRandomTable[];
extern void *_teamTacticsTable[];
extern void *_positionsTable[];
extern uint16_t   _teamSkillCurrent;
extern uint8_t    _playerPositionsArray[];

/* Data tables in asm for this module */
extern uint8_t    _simGoalsByStrengthDiffTable[];
extern uint8_t    _simHighGoalFallbackValues[];
extern void *_simGoalProbByTacticPtrs[];
extern void *_simExtraGoalProbByTacticPtrs[];
extern uint8_t    _simGoalsByIndexDiffTable[];
extern uint8_t    _simExtraGoalThresholdsA[];
extern uint8_t    _simExtraGoalThresholdsB[];
extern uint8_t    _simGoalScorerPairTable[];
extern void *_goalScorerDistributionByBallOutTactic[];
extern uint8_t    _kUserTeamOffsetBiasLUT[];
extern uint8_t    _kTeamViewOffsetBaseLUT[];
extern uint8_t    _availablePickCountByGoalsInTable[];
extern uint8_t    _preferredTacticsSlotsByRole[];
extern uint8_t    _candidatePlayersByGoalsInTable[];
extern void *_playersPositionsByTactics[];
extern uint8_t    _playerStatusBucketThresholds[];

/* Extern globals/functions for Chunk 106 */
extern uint8_t  _resultString[];
extern void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC);
extern uint8_t  _a1stLeg01[];
extern uint8_t  _aAWin23OnAgg[];
extern uint8_t  _aAWin23OnAggAET[];
extern uint8_t  _a23OnAggAWinOnA_0[];
extern uint8_t  _a23OnAggAWinOnA[];
extern uint8_t  _a23OnAggAWin01O[];
extern uint8_t  _a23OnAggAWin01O_0[];
extern uint8_t  _a23OnAgg[];
extern uint8_t  _aReplay_0[];
extern uint8_t  _aAET[];
extern uint8_t  _aAET90Mins23[];
extern uint8_t  _aAETReplay[];
extern uint8_t  _aAET90Mins23Rep[];
extern uint8_t  _aAWin01OnPensAE[];
extern uint8_t  _aAWin01OnPensAE_1[];
extern uint8_t  _aAWin01OnPensAE_3[];
extern uint8_t  _aAWin01OnPensAE_5[];
extern uint8_t  _aAWin01OnPens[];
extern uint8_t  _aAWin01OnPensRe[];
/* Compact ("second string") counterparts — see resultFormatCompact() below. */
extern uint8_t  _a1stLeg01_0[];
extern uint8_t  _aAWinOnAwayGoal[];
extern uint8_t  _aAWinOnAwayGoal_0[];
extern uint8_t  _aAWin01OnPensAE_7[];
extern uint8_t  _aAWin23OnAggAET_0[];
extern uint8_t  _aAWin01OnPens_0[];
extern uint8_t  _aAWin01OnPens_1[];
extern uint8_t  _aAWin01OnPens_2[];
extern uint8_t  _aAWin23OnAgg_0[];
extern uint8_t  _a23OnAgg_0[];
extern uint8_t  _aAWin01OnPensAE_0[];
extern uint8_t  _aAWin01OnPensAE_2[];
extern uint8_t  _aAWin01OnPensAE_4[];
extern uint8_t  _aAWin01OnPensAE_6[];
extern uint8_t  _aAETReplay_0[];
extern uint8_t  _aAET90Mins23Rep_0[];
extern uint8_t  _aAET_0[];
extern uint8_t  _aAET90Mins23_0[];
extern uint8_t  _aReplay_1[];
extern uint8_t  _a23OnAggAWinOnA_0[];
extern uint8_t  _aAWin23OnAggAET[];
extern uint8_t  _a1stLeg01[];

/* ---------------------------------------------------------------------------
 * resultFormatCompact — the `control != 0` format.
 *
 * In SWOS2.s every result format is stored as a PADDED display string
 * immediately followed by a COMPACT one, and `control != 0` selects the second
 * by walking past the first NUL:
 *
 *     aAWin01OnPensAE:   dc.b '    %a WIN %0-%1 ON PENS A.E.T.     ',0
 *     aAWin01OnPensAE_0: dc.b '%a WIN %0-%1 ON PENS A.E.T.',0
 *
 * That is the adjacent-globals pattern: it only works because the assembler
 * laid the two strings out back to back. In C they are separate objects at
 * linker-chosen addresses, so the original `while (*fmt) fmt++; fmt++;` walked
 * off the end of one string into unrelated data. The career fixture list is the
 * only caller that passes control=1, which is why a drawn cup tie lost its
 * second line ("CROATIA WIN 4-3 ON PENS A.E.T.") while every other result
 * screen — all control=0 — looked right.
 *
 * Mapping extracted from the label order in SWOS2.s (each entry is literally
 * "the next label in the data block"). Returns fmt unchanged if it is not one
 * of the known formats, so an unmapped caller keeps the padded string rather
 * than reading out of bounds.
 * ------------------------------------------------------------------------- */
static uint8_t *resultFormatCompact(uint8_t *fmt)
{
    if (fmt == _a1stLeg01)          return _a1stLeg01_0;
    if (fmt == _a23OnAggAWinOnA_0)  return _aAWinOnAwayGoal_0;
    if (fmt == _a23OnAggAWin01O_0)  return _aAWin01OnPensAE_7;
    if (fmt == _aAWin23OnAggAET)    return _aAWin23OnAggAET_0;
    if (fmt == _a23OnAggAWinOnA)    return _aAWinOnAwayGoal;
    if (fmt == _a23OnAggAWin01O)    return _aAWin01OnPens_2;
    if (fmt == _aAWin23OnAgg)       return _aAWin23OnAgg_0;
    if (fmt == _a23OnAgg)           return _a23OnAgg_0;
    if (fmt == _aAWin01OnPensAE_3)  return _aAWin01OnPensAE_4;
    if (fmt == _aAWin01OnPensAE_5)  return _aAWin01OnPensAE_6;
    if (fmt == _aAWin01OnPensAE)    return _aAWin01OnPensAE_0;
    if (fmt == _aAWin01OnPensAE_1)  return _aAWin01OnPensAE_2;
    if (fmt == _aAETReplay)         return _aAETReplay_0;
    if (fmt == _aAET90Mins23Rep)    return _aAET90Mins23Rep_0;
    if (fmt == _aAET)               return _aAET_0;
    if (fmt == _aAET90Mins23)       return _aAET90Mins23_0;
    if (fmt == _aAWin01OnPensRe)    return _aAWin01OnPens_1;
    if (fmt == _aAWin01OnPens)      return _aAWin01OnPens_0;
    if (fmt == _aReplay_0)          return _aReplay_1;
    return fmt;
}

/* buildMatchResultStringControl — static storage for the control flag
 * (originally a dc.w in asm right after the function) */
static uint16_t buildMatchResultStringControl;

/* Forward declaration: GetPlayerPrice (Chunk 187) */
int16_t GetPlayerPrice(TeamFile *teamFile, PlayerFile *playerFile, uint16_t playerNumber);

/* Extern globals for Chunk 105 */
extern uint16_t _pl1Tactics;
extern uint16_t _pl2Tactics;
extern TeamFile *_gameTeam1;
extern TeamFile *_gameTeam2;
extern uint16_t _team1AppPercent;
extern uint16_t _team2AppPercent;
extern uint16_t _plgD0Param;
extern uint16_t _isGameFriendly;
extern uint16_t _gameRandValue;
extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern DIYCompetitionEx _diyCompetitionStart;
extern void SaveRand2State(void);
extern void RestoreRand2State(void);
extern void Randomize2(uint16_t _seed);
extern uint16_t GetPlayerRandomNum(PlayerFile *playerFile);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);

/* ---- Module-local state ---- */
static DIYCompetition *viewResultCtx;
static uint16_t  viewResultModeFlag;
static uint16_t  viewResultStatColumnIndex;
static uint16_t  viewResultTeam1GoalsIn;
static uint16_t  viewResultTeam2GoalsIn;
static TeamFileEx *viewResultTeam1Ptr;
static TeamFileEx *viewResultTeam2Ptr;
static int16_t   team1ViewBaseIndex;
static int16_t   team2ViewBaseIndex;
static uint16_t  team1ViewIndex;
static uint16_t  team2ViewIndex;
static int16_t   viewIndexDelta;
static int16_t   team1ViewOffsetX;
static int16_t   team2ViewOffsetX;
static int16_t   team1ViewOffsetY;
static int16_t   team2ViewOffsetY;
static uint16_t  team1SimulatedGoalsOut;
static uint16_t  team2SimulatedGoalsOut;
static uint8_t   team1PlayerRemapTable[32];
static uint8_t   team2PlayerRemapTable[32];
static uint32_t  team1AvailableMask;
static uint32_t  team2AvailableMask;
static uint16_t  team1AvailableCount;
static uint16_t  team2AvailableCount;
static uintptr_t currentGoalScorerDistPtr;  /* holds a pointer into goalScorerDistributionByBallOutTactic[]; retyped uintptr_t for 64-bit. */
static uint16_t  oldPlayerSkillDivisor;

/* Externally-visible state (defined in asm, referenced by other asm modules) */
extern uint16_t _inEditTactics;
extern uint8_t  _team1GoalCounterByPlayer[];
extern uint8_t  _team2GoalCounterByPlayer[];
static int16_t  careerTeamViewOffsetX;


/* ================================================================
 * CalcTeamViewOffset
 * in:  team -> TeamFile pointer
 * Returns packed uint32_t: high word = X, low word = Y (both int16_t).
 * Called from asm bridge and internally.
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t CalcTeamViewOffset(TeamFileEx *team) {
    uint8_t d0;
    int16_t d1, d2;
    uint16_t d3;

    /* d0 = low byte of globalTeamNumber */
    d0 = (uint8_t)team->team.globalTeamNumber;
    /* XOR with currentSeasonShowing low byte */
    d0 ^= (uint8_t)_currentSeasonShowing;
    /* XOR with newRandomSeedCopy */
    d0 ^= _newRandomSeedCopy;

    /* Look up in kRandomTable */
    d0 = _kRandomTable[d0];
    d3 = d0;

    /* Add viewRandomPhase, mask to 8 bits */
    d0 = (uint8_t)(d0 + _viewRandomPhase);

    /* Look up base X offset from kTeamViewOffsetBaseLUT */
    d1 = (int8_t)_kTeamViewOffsetBaseLUT[d0];
    /* Y offset: index+7, wrapped to 8 bits */
    d2 = (int8_t)_kTeamViewOffsetBaseLUT[(uint8_t)(d0 + 7)];

    /* Double the offsets */
    d1 = d1 + d1;
    d2 = d2 + d2;

    /* Check if team is user-controlled (teamControls == 1) */
    if (team->team.teamControls == 1) {
        /* Compute bias index: d3 += (viewRandomPhase << 2) >> 6; d3 &= 0x3F */
        uint16_t biasIdx = d3;
        uint8_t vrp = _viewRandomPhase;
        biasIdx += (uint16_t)((vrp << 2) >> 6);
        biasIdx &= 0x3F;

        int8_t bias = (int8_t)_kUserTeamOffsetBiasLUT[biasIdx];
        d1 += bias + bias;
        bias = (int8_t)_kUserTeamOffsetBiasLUT[biasIdx];
        d2 += bias + bias;
    }

    /* If not user-controlled, subtract 1 from both */
    if (team->team.teamControls != 1) {
        d1 -= 1;
        d2 -= 1;
    }

    /* Clamp X to [-12, +12] */
    if (d1 < -12) d1 = -12;
    else if (d1 > 12) d1 = 12;

    /* Clamp Y to [-12, +12] */
    if (d2 < -12) d2 = -12;
    else if (d2 > 12) d2 = 12;

    return ((uint32_t)(uint16_t)d1 << 16) | (uint16_t)d2;
}

/* ================================================================
 * ScaleViewIndexByTeamPriceRank
 * in:  value (d7), team (a1)
 * out: scaled value (0..31)
 * ================================================================ */
static uint16_t ScaleViewIndexByTeamPriceRank(uint16_t value, TeamFileEx *team) {
    int16_t pctOff = CalcTeamPriceRankPercentOffset(viewResultCtx, &team->team);
    uint16_t factor = (uint16_t)(pctOff + 100);
    uint32_t result = (uint32_t)value * (uint32_t)factor;
    result /= 100;

    if ((int16_t)result < 0) return 0;
    if (result > 31) return 31;
    return (uint16_t)result;
}

/* ================================================================
 * AssignSimulatedGoalsToScorers
 * in:  team (a0), remapTable (a2), goalCounters (a3), numGoals (d5)
 * ================================================================ */
static void AssignSimulatedGoalsToScorers(TeamFileEx *team, uint8_t *remapTable,
                                          uint8_t *goalCounters, uint16_t numGoals) {
    int16_t d5 = (int16_t)numGoals - 1;
    if (d5 < 0) return;

    uint8_t *playerNums = team->team.playerNumbers;
    if (team == &_careerTeam) {
        playerNums = _currentMatchPlayers;
    }

    /* Get tactic index -> ball-out-of-play tactics -> position list */
    int16_t tacticIdx = (int8_t)team->team.tactics;
    TeamTactics *tactics = (TeamTactics *)_teamTacticsTable[tacticIdx];
    int16_t ballOutTacticIdx = (int8_t)tactics->ballOutOfPlayTactics;
    uint8_t *positions = (uint8_t *)_positionsTable[ballOutTacticIdx];

    /* Get goal scorer distribution pointer */
    currentGoalScorerDistPtr = (uintptr_t)_goalScorerDistributionByBallOutTactic[ballOutTacticIdx];

    for (; d5 >= 0; d5--) {
        uint8_t *distTable = (uint8_t *)currentGoalScorerDistPtr;
        uint16_t d0 = Rand2();

        /* Special case: if d0 >= 0xF8, it's an own goal */
        if (d0 >= 0xF8) {
            team->team.writesZeroHere++;  /* own goals counter (offset 0x16) */
            continue;
        }

        d0 &= 0x7F;
        int16_t d6 = 10;  /* start from position 10 (forward) */

        /* Walk the cumulative probability distribution (11 entries) */
        uint8_t *dp = distTable;
        int16_t i;
        for (i = 0; i < 11; i++) {
            if (d0 < dp[i]) goto found_position;
            d0 -= dp[i];
            d6--;
        }

        /* If we exhausted all 11 entries, do a second random for the last 2 buckets */
        d0 = Rand2();
        d0 &= 0x7F;
        d6 = 1;
        dp = distTable + 11;  /* point to the 12th entry */
        if (d0 < dp[0]) goto found_position;
        d0 -= dp[0];
        d6--;

    found_position:
        /* Map through position table: d6 = positions[d6] - 1 */
        d6 = (uint8_t)positions[d6];
        d6 = (uint8_t)(d6 - 1);

        /* Map through player numbers: d6 = playerNums[d6] */
        d6 = playerNums[(uint8_t)d6];

        /* Check remap table; if negative (0x80+), use remapped player */
        if (!((int8_t)remapTable[(uint8_t)d6] < 0)) {
            /* Not remapped - 50% chance to use the remap */
            uint16_t r = Rand2();
            if (!(r & 1)) {
                d6 = remapTable[(uint8_t)d6];
            }
        }

        /* Increment goal counter for this player */
        goalCounters[(uint8_t)d6]++;

        /* Update player's per-stat-column goal count */
        PlayerFile *playerRec = &team->players[(uint8_t)d6];
        uint16_t statCol = viewResultStatColumnIndex;
        playerRec->goalStats[statCol]++;
    }
}

/* ================================================================
 * ApplyRandomPlayerStatusAfterSim
 * Applies random injury/card/status changes to players after simulation.
 * ================================================================ */
static void ApplyRandomPlayerStatusAfterSimBody(TeamFileEx *team, uint8_t *remapTable);

/* A computer club's records written only for the kinds a world keeps (CPU RED CARDS / CPU
 * INJURIES, world_rules.c): the call opens for either, then the other kind's bits go back. */
static void cpu_kept(TeamFileEx *team, uint8_t *remapTable,
                     void (*body)(TeamFileEx *, uint8_t *))
{
    uint8_t keep = 0, was[16];
    if (team->team.teamControls == 1) {
        if (!WorldRules_CpuAny())
            return;
        if (!WorldRules_CpuOpen(WRULE_CPU_CARDS))    keep |= 0x1F;
        if (!WorldRules_CpuOpen(WRULE_CPU_INJURIES)) keep |= 0xE0;
        for (int32_t i = 0; i < 16; i++)
            was[i] = team->players[i].cardsInjuries;
    }
    body(team, remapTable);
    if (keep)
        for (int32_t i = 0; i < 16; i++)
            team->players[i].cardsInjuries =
                (uint8_t)((team->players[i].cardsInjuries & ~keep) | (was[i] & keep));
}

static void ApplyRandomPlayerStatusAfterSim(TeamFileEx *team, uint8_t *remapTable) {
    if (_trainingGame) return;
    cpu_kept(team, remapTable, ApplyRandomPlayerStatusAfterSimBody);
}

static void ApplyRandomPlayerStatusAfterSimBody(TeamFileEx *team, uint8_t *remapTable) {
    /* The testers' rule (test_rules.h): never assign post-match injuries / cards, so
     * the tester can VIEW RESULT through a season without substitution prompts. */
    if (TestRules_NoInjuriesCards())
        return;

    /* Get number of available players */
    uint32_t result = GetNumberOfAvailablePlayers((uint8_t *)team, -1, 0);
    uint16_t availCount = (uint16_t)(result >> 16); /* d6 from asm */

    int16_t d5;
    if (_gameType == 4) {
        d5 = 4;
    } else {
        d5 = 2;
    }
    d5 -= (int16_t)availCount;
    if (d5 == 0) return;

    uint8_t *playerNums = team->team.playerNumbers;
    if (team == &_careerTeam) {
        playerNums = _currentMatchPlayers;
    }

    int16_t d7;
    for (d7 = 10; d7 >= 0; d7--) {
        int16_t d6 = (int8_t)playerNums[d7];
        uint16_t d0 = Rand2();

        /* Check remap: if negative byte in remapTable, use direct; else 50% chance */
        if (!((int8_t)remapTable[d6] < 0)) {
            if (!(d0 & 1)) {
                d6 = (int8_t)remapTable[d6];
            }
        }

        uint16_t d2 = (uint16_t)d6;
        PlayerFile *playerRec = &team->players[d2];

        uint8_t cardsInj = playerRec->cardsInjuries;
        uint16_t d1 = cardsInj & 0x1C;  /* injury bits */
        uint16_t cardBits = cardsInj & 0x03;

        /* Goalkeeper (d7==0) has much higher skip threshold */
        if (d7 == 0) {
            if (d0 >= 2) goto next_player;
        } else {
            if (d0 >= 16) goto next_player;
        }

        /* Check player's position (role) */
        uint8_t posAndFace = playerRec->positionAndFace;
        uint16_t role = (posAndFace & PF_POSITION_MASK) >> 5;

        if (role == 0) {
            /* Goalkeeper role - check available substitutes */
            uint32_t subResult = GetNumberOfAvailableSubstitutes((uint8_t *)team, -1);
            uint16_t subAvail = (uint16_t)(subResult >> 16); /* d6 from asm */
            if (subAvail != 0) goto next_player;
        }

        /* Second random for status type determination */
        d0 = Rand2();
        d0 &= 0x1F;

        if (d0 < 0x1D) {
            /* Most common case: minor status */
            if (!_inEditTactics) {
                if (d1 == 0) {
                    /* No current injury - check ShouldApply */
                    if (ShouldApplyRandomPlayerStatusAfterSim() == 0) {
                        /* skip this player entirely (loc_1223B2) */
                        d5--;
                        if (d5 == 0) return;
                        continue;
                    }
                    d1 = 4;
                } else {
                    d1 = 8;
                }
            } else {
                goto next_player;
            }
        } else if (d0 == 0x1D) {
            /* Moderate status change */
            if (_inEditTactics) {
                cardBits = 2;
            } else {
                if (d1 == 0) {
                    /* loc_122376: ShouldApply -> if yes d1=0xC, else d1=8 */
                    if (ShouldApplyRandomPlayerStatusAfterSim() != 0) {
                        d1 = 0x0C;
                    } else {
                        d1 = 8;
                    }
                } else {
                    /* loc_122384 */
                    if (ShouldApplyRandomPlayerStatusAfterSim() != 0) {
                        d1 = 0x10;
                    } else {
                        d1 = 8;
                    }
                }
            }
        } else if (d0 == 0x1E) {
            /* Serious status change */
            if (_inEditTactics) {
                cardBits = 2;
            } else {
                /* loc_122384: ShouldApply -> if yes d1=0x10, else d1=8 */
                if (ShouldApplyRandomPlayerStatusAfterSim() != 0) {
                    d1 = 0x10;
                } else {
                    d1 = 8;
                }
            }
        } else {
            /* d0 == 0x1F: most severe */
            if (_inEditTactics) {
                cardBits = 2;
            } else {
                if (d1 == 0) {
                    /* loc_122384 */
                    if (ShouldApplyRandomPlayerStatusAfterSim() != 0) {
                        d1 = 0x10;
                    } else {
                        d1 = 8;
                    }
                } else {
                    /* same path: loc_122384 */
                    if (ShouldApplyRandomPlayerStatusAfterSim() != 0) {
                        d1 = 0x10;
                    } else {
                        d1 = 8;
                    }
                }
            }
        }

        /* Apply new injury bits (loc_122396) */
        {
            uint8_t val = playerRec->cardsInjuries;
            val = (val & ~0x1C) | (uint8_t)(d1 & 0x1C);
            playerRec->cardsInjuries = val;

            val = playerRec->cardsInjuries;
            val = (val & ~0x03) | (uint8_t)(cardBits & 0x03);
            playerRec->cardsInjuries = val;
        }

        /* loc_1223B2: decrement budget */
        d5--;
        if (d5 == 0) return;
        continue;

    next_player:
        ; /* dbf d7 continues the loop */
    }
}

/* ================================================================
 * InitPlayerRemapTableForTeam
 * Assigns random status buckets to players based on match factors.
 * The status bucket lives in cardsInjuries bits 5-7; IsPlayerInjured
 * returns "injured" for any bucket above the minor (0x20) tier, so this
 * function is a second source of post-match unavailability that the
 * substitution / VIEW RESULT prompt treats the same as a real injury.
 * ================================================================ */
__attribute__((used, externally_visible))
static void InitPlayerRemapTableForTeamBody(TeamFileEx *team, uint8_t *remapTable);

void InitPlayerRemapTableForTeam(TeamFileEx *team, uint8_t *remapTable) {
    cpu_kept(team, remapTable, InitPlayerRemapTableForTeamBody);
}

static void InitPlayerRemapTableForTeamBody(TeamFileEx *team, uint8_t *remapTable) {
    /* The testers' rule (test_rules.h): never assign post-match status buckets, so
     * the tester can VIEW RESULT through a season without IsPlayerInjured flagging
     * anyone unavailable. */
    if (TestRules_NoInjuriesCards())
        return;

    uint32_t result = GetNumberOfAvailablePlayers((uint8_t *)team, -1, 0);
    uint16_t availCount = (uint16_t)(result >> 16); /* d6 */

    int16_t d5;
    if (_gameType == 4) {
        d5 = 4;
    } else {
        d5 = 2;
    }
    d5 -= (int16_t)availCount;
    if (d5 <= 0) return;

    uint8_t *playerNums = team->team.playerNumbers;
    int16_t d7;

    if (team == &_careerTeam) {
        playerNums = _currentMatchPlayers;
        if (remapTable == (uint8_t *)0) {
            d7 = (int16_t)(_careerNumPlayers - 1);
            goto loop_start;
        }
    }
    d7 = 10;

loop_start:
    for (; d7 >= 0; d7--) {
        int16_t d6 = (int8_t)playerNums[d7];
        uint16_t d0 = Rand2();

        /* Check remap table if non-null */
        if (remapTable != (uint8_t *)0) {
            if (!((int8_t)remapTable[d6] < 0)) {
                if (!(d0 & 1)) {
                    d6 = (int8_t)remapTable[d6];
                }
            }
        }

        uint16_t d2 = (uint16_t)d6;
        PlayerFile *playerRec = &team->players[d2];

        /* Goalkeeper skip check */
        if (d7 == 0) {
            if (d0 >= 2) continue;
        }

        /* Check current position/role */
        uint8_t posAndFace = playerRec->positionAndFace;
        uint16_t role = (posAndFace & PF_POSITION_MASK) >> 5;

        if (role == 0) {
            /* Goalkeeper: check available substitutes */
            uint32_t subResult = GetNumberOfAvailableSubstitutes((uint8_t *)team, -1);
            uint16_t subAvail = (uint16_t)(subResult >> 16);
            if (subAvail != 0) continue;
        }

        /* Check current status bucket (bits 5-7 of cardsInjuries) */
        uint8_t cardsInj = playerRec->cardsInjuries;
        uint16_t statusBucket = cardsInj & 0xE0;

        if (statusBucket == 0) {
            /* No status - need very low random to trigger */
            if (d0 >= 5) continue;
        } else if (statusBucket == 0x20) {
            /* Status 1 - moderate threshold */
            if (d0 >= 0x40) continue;
        } else {
            /* Higher status - skip */
            continue;
        }

        /* Assign new status bucket from probability thresholds */
        uint8_t *thresholds = _playerStatusBucketThresholds;
        d0 = Rand2();
        d0 &= 0x3F;
        uint16_t newBucket = 0x20;

        int16_t j;
        for (j = 0; j < 8; j++) {
            if (d0 < thresholds[j]) goto apply_bucket;
            d0 -= thresholds[j];
            newBucket += 0x20;
        }

    apply_bucket:
        {
            uint8_t val = playerRec->cardsInjuries;
            val = (val & 0x1F) | (uint8_t)(newBucket & 0xE0);
            playerRec->cardsInjuries = val;
        }

        d5--;
        if (d5 == 0) return;
    }
}

/* ================================================================
 * BuildAvailablePlayersRemapAndMask
 * Builds the player substitution remap table and availability mask.
 * out: *outCount = count, *outMask = bitmask
 * ================================================================ */
static void BuildAvailablePlayersRemapAndMask(TeamFileEx *team, uint8_t *remapTable,
                                              uint16_t *outCount, uint32_t *outMask) {
    uint32_t mask = 0;

    /* Fill remap table with 0xFF (identity/no-remap) — use longword
     * stores matching original asm (move.l d0,(a1)+ x8) to avoid
     * GCC generating a memset call (no libc in this project). */
    {
        uint32_t *p32 = (uint32_t *)remapTable;
        p32[0] = 0xFFFFFFFF; p32[1] = 0xFFFFFFFF;
        p32[2] = 0xFFFFFFFF; p32[3] = 0xFFFFFFFF;
        p32[4] = 0xFFFFFFFF; p32[5] = 0xFFFFFFFF;
        p32[6] = 0xFFFFFFFF; p32[7] = 0xFFFFFFFF;
    }

    uint8_t *playerNums = team->team.playerNumbers;
    if (team == &_careerTeam) {
        playerNums = _currentMatchPlayers;
    }

    /* Get pick count based on goals conceded */
    uint8_t *pickCounts = _availablePickCountByGoalsInTable + (viewResultTeam1GoalsIn << 3);
    uint16_t d0 = Rand2();
    uint8_t d4 = pickCounts[d0 & 7];
    if (d4 == 0) {
        *outCount = 0;
        *outMask = 0;
        return;
    }
    int16_t d4s = (int8_t)d4;

    /* Get candidate players based on goals against */
    uint8_t *candidates = _candidatePlayersByGoalsInTable + ((viewResultTeam2GoalsIn - 1) << 5);
    d0 = Rand2();
    candidates += (d0 & 0x18);  /* offset by 0, 8, 16, or 24 */

    uint16_t d6 = 0;
    d4s--;

    for (; d4s >= 0; d4s--) {
        uint8_t candidateIdx = *candidates++;
        if (candidateIdx == 0) break;

        int16_t d2 = (int8_t)candidateIdx;
        d2 = playerNums[d2];

        /* Check player record */
        PlayerFile *playerRec = &team->players[(uint16_t)d2];

        /* Check status bucket (bits 5-7 of cardsInjuries) - must be 0 */
        if (playerRec->cardsInjuries & 0xE0) continue;

        /* Check injury/card status based on inEditTactics */
        if (!_inEditTactics) {
            uint8_t ci = playerRec->cardsInjuries;
            if ((ci & 0x1C) >= 8) continue;
        } else {
            uint8_t ci = playerRec->cardsInjuries;
            if ((ci & 0x03) >= 1) continue;
        }

        /* Check position/role for goalkeeper */
        uint8_t posAndFace = playerRec->positionAndFace;
        uint16_t role = (posAndFace & PF_POSITION_MASK) >> 5;

        if (role == 0) {
            /* Goalkeeper: random chance to skip */
            d0 = Rand2();
            if (d0 >= 4) {
                d4s++;  /* retry: don't count this against the budget (go back to loop start) */
                continue;
            }
        }

        /* Pick a preferred position slot for this player's role */
        uint8_t *roleSlots = _preferredTacticsSlotsByRole + ((int16_t)role << 3);
        d0 = Rand2();
        uint8_t slotIdx = roleSlots[d0 & 7];

        /* Map through tactics to get position index */
        uint8_t tacticIdx = team->team.tactics;
        uint8_t *tacticPositions = (uint8_t *)_playersPositionsByTactics[(int8_t)tacticIdx];
        uint8_t posIdx = tacticPositions[slotIdx];
        uint8_t targetPlayer = playerNums[posIdx];

        /* Check if target player's remap slot is available (0xFF = available) */
        if (!((int8_t)remapTable[targetPlayer] < 0)) {
            /* Slot taken, try once more with different random */
            d0 = Rand2();
            slotIdx = roleSlots[d0 & 7];

            /* Re-read tactics mapping */
            tacticIdx = team->team.tactics;
            tacticPositions = (uint8_t *)_playersPositionsByTactics[(int8_t)tacticIdx];
            posIdx = tacticPositions[slotIdx];
            targetPlayer = playerNums[posIdx];

            if (!((int8_t)remapTable[targetPlayer] < 0)) {
                continue;  /* Still taken, skip this candidate */
            }
        }

        /* Assign remap: remapTable[targetPlayer] = candidatePlayerNum */
        remapTable[targetPlayer] = (uint8_t)d2;
        mask |= (1u << (uint8_t)d2);
        d6++;
    }

    *outCount = d6;
    *outMask = mask;
}

/* ================================================================
 * ComputeOldPlayerSkillForViewResult
 * Calculates average skill index for team, accounting for goalkeeper.
 * in:  team, goalsCount
 * out: skill index (0..31 range)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t ComputeOldPlayerSkillForViewResult(TeamFileEx *team, uint16_t goalsCount) {
    oldPlayerSkillDivisor = 1;
    uint16_t d5 = 0;
    uint16_t d6 = goalsCount;

    if (d6 != 0) {
        int16_t d3 = (int16_t)d6 - 1;
        uint16_t d2 = 0;
        uint16_t d7 = 11;  /* start from player index 11 */

        for (; d3 >= 0; d3--) {
            /* Call UpdatePlayerPositionAndReturnRole */
            uint32_t res = UpdatePlayerPositionAndReturnRole(team, d7);
            uint16_t posValue = (uint16_t)(res & 0xFFFF);       /* d0 = position value */
            uint16_t roleValue = (uint16_t)((res >> 16) & 0xFFFF); /* d1 = role */

            uint16_t scaled = posValue << 2;  /* lsl.w #2,d0 */
            d2 += 4;  /* addq.w #4,d2 */

            if (roleValue == 0) {
                /* Goalkeeper: undo the scaling */
                scaled = posValue;  /* lsr.w #2,d0 which undoes lsl.w #2 */
                d2 -= 3;  /* subq.w #3,d2 */
            }

            d5 += scaled;
            d7++;
        }

        /* Average: (d2/2 + d5) / d2 */
        uint16_t half = d2 >> 1;
        uint32_t sum = (uint32_t)(half + d5);
        d5 = (uint16_t)(sum / d2);
    }

    /* Second pass: iterate all 11 outfield players (indices 0..10) */
    uint16_t d7;
    for (d7 = 0; d7 < 11; d7++) {
        uint32_t res = UpdatePlayerPositionAndReturnRole(team, d7);
        uint16_t posValue = (uint16_t)(res & 0xFFFF);
        uint16_t roleValue = (uint16_t)((res >> 16) & 0xFFFF);

        if (posValue == 0 && roleValue != 0) {
            oldPlayerSkillDivisor++;
        }
        d5 += posValue;
    }

    /* Divide by 15 */
    uint32_t d0_ext = (uint32_t)d5;
    uint16_t result = (uint16_t)(d0_ext / 15);

    /* Check goalkeeper: if position is 0 (goalkeeper role), halve result.
     * Asm used move.b -$20(a0),d1 for career team — that works because
     * currentMatchPlayers was exactly 0x20 bytes before careerTeam in the
     * asm memory layout.  In C they are separate variables, so use the
     * symbol directly. */
    uint8_t *goalkeepPlayerNums;
    if (team == &_careerTeam) {
        goalkeepPlayerNums = _currentMatchPlayers;
    } else {
        goalkeepPlayerNums = team->team.playerNumbers;
    }
    int16_t gkIdx = (int8_t)goalkeepPlayerNums[0];
    PlayerFile *gkRec = &team->players[(uint16_t)gkIdx];
    uint8_t gkPosAndFace = gkRec->positionAndFace;
    uint16_t gkRole = (gkPosAndFace & PF_POSITION_MASK) >> 5;

    if (gkRole != 0) {
        /* Non-GK in goalkeeper slot: halve result as weakness penalty.
         * Asm: beq.s loc_122D78 (skip halve for real GK); lsr.w #1,d0 */
        result >>= 1;
    }

    /* Final division by oldPlayerSkillDivisor and store */
    result = (uint16_t)((uint32_t)result / oldPlayerSkillDivisor);
    _teamSkillCurrent = result;

    /* Clear flag at offset 0x3B (playerValuesDirty in TeamFile) */
    team->team.playerValuesDirty = TEAM_VALUES_CLEAN;

    return result;
}

/* ================================================================
 * CalculateViewResult — main entry point
 * ================================================================ */
__attribute__((used, externally_visible))
void CalculateViewResult(DIYCompetition *ctx, uint16_t modeFlag,
                          uint16_t team1GoalsIn, uint16_t team2GoalsIn,
                          uint16_t statColumnIndex,
                          TeamFileEx *team1, TeamFileEx *team2,
                          uint16_t *outTeam1Goals, uint16_t *outTeam2Goals) {
    /* Store parameters in module state */
    viewResultCtx = ctx;
    _inEditTactics = ctx->substitutesCount;
    viewResultModeFlag = modeFlag;
    viewResultTeam1GoalsIn = team1GoalsIn;
    viewResultTeam2GoalsIn = team2GoalsIn;
    viewResultStatColumnIndex = statColumnIndex;
    viewResultTeam1Ptr = team1;
    viewResultTeam2Ptr = team2;
    /* A world's computer clubs field available players (CPU RED CARDS / CPU INJURIES): their
     * injured and suspended starters swapped out for this result, the order back after it. */
    WorldRules_CpuLineupsBegin(1, team1, team2, (int16_t)_inEditTactics);

    /* Clear goal counters (32 bytes each) — longword stores to avoid
     * GCC generating memset calls (no libc). */
    {
        uint32_t *p1 = (uint32_t *)_team1GoalCounterByPlayer;
        uint32_t *p2 = (uint32_t *)_team2GoalCounterByPlayer;
        p1[0] = 0; p1[1] = 0; p1[2] = 0; p1[3] = 0;
        p1[4] = 0; p1[5] = 0; p1[6] = 0; p1[7] = 0;
        p2[0] = 0; p2[1] = 0; p2[2] = 0; p2[3] = 0;
        p2[4] = 0; p2[5] = 0; p2[6] = 0; p2[7] = 0;
    }

    /* Build player remap tables and availability */
    BuildAvailablePlayersRemapAndMask(team1, team1PlayerRemapTable,
                                      &team1AvailableCount, &team1AvailableMask);
    BuildAvailablePlayersRemapAndMask(team2, team2PlayerRemapTable,
                                      &team2AvailableCount, &team2AvailableMask);

    /* Init player remap tables */
    InitPlayerRemapTableForTeam(team1, team1PlayerRemapTable);
    InitPlayerRemapTableForTeam(team2, team2PlayerRemapTable);

    /* Apply post-match career stat mods */
    Career_ApplyPostMatchPlayerStatModsIfCareerTeam(team1, team1PlayerRemapTable);
    Career_ApplyPostMatchPlayerStatModsIfCareerTeam(team2, team2PlayerRemapTable);

    /* Apply random player status */
    ApplyRandomPlayerStatusAfterSim(team1, team1PlayerRemapTable);
    ApplyRandomPlayerStatusAfterSim(team2, team2PlayerRemapTable);
    WorldRules_GkAfterSim(team1, team1PlayerRemapTable);     /* (a world's keeper injuries) */
    WorldRules_GkAfterSim(team2, team2PlayerRemapTable);

    /* Compute old player skill (view base index) */
    /* Note: for team1, the goals-in parameter is team2's goals (opponent's goals) */
    team1ViewBaseIndex = (int16_t)ComputeOldPlayerSkillForViewResult(team1, team2GoalsIn);
    team2ViewBaseIndex = (int16_t)ComputeOldPlayerSkillForViewResult(team2, team2GoalsIn);

    /* Scale by price rank unless both teams are user-controlled */
    if (!(team1->team.teamControls == 1 && team2->team.teamControls == 1)) {
        team1ViewBaseIndex = (int16_t)ScaleViewIndexByTeamPriceRank(
            (uint16_t)team1ViewBaseIndex, team1);
        team2ViewBaseIndex = (int16_t)ScaleViewIndexByTeamPriceRank(
            (uint16_t)team2ViewBaseIndex, team2);
    }

    /* Calculate view offsets */
    {
        uint32_t packed = CalcTeamViewOffset(team1);
        team1ViewOffsetX = (int16_t)(packed >> 16);
        team1ViewOffsetY = (int16_t)(packed & 0xFFFF);
    }
    if (viewResultTeam1Ptr == &_careerTeam) {
        careerTeamViewOffsetX = team1ViewOffsetX;
    }

    {
        uint32_t packed = CalcTeamViewOffset(team2);
        team2ViewOffsetX = (int16_t)(packed >> 16);
        team2ViewOffsetY = (int16_t)(packed & 0xFFFF);
    }
    if (viewResultTeam2Ptr == &_careerTeam) {
        careerTeamViewOffsetX = team2ViewOffsetX;
    }

    /* ---- First scoring phase: based on X offsets ---- */
    int16_t d3 = team1ViewBaseIndex + team1ViewOffsetX;
    {
        uint16_t idx = (d3 < 0) ? 0 : (uint16_t)d3;
        if (idx > 31) idx = 31;
        team1ViewIndex = idx;
    }

    int16_t d4 = team2ViewBaseIndex + team2ViewOffsetX;
    {
        uint16_t idx = (d4 < 0) ? 0 : (uint16_t)d4;
        if (idx > 31) idx = 31;
        team2ViewIndex = idx;
    }

    /* Compute strength difference */
    d3 = d3 - d4;
    if (viewResultModeFlag == 0) {
        d3 += 2;
    }
    if (d3 > 23) d3 = 23;
    else if (d3 < -23) d3 = -23;
    viewIndexDelta = d3;

    /* Look up goals from strength difference table */
    int16_t absDelta = d3;
    if (absDelta < 0) absDelta = -absDelta;

    uint8_t *goalLookup = _simGoalsByStrengthDiffTable + ((uint16_t)absDelta << 2);
    uint16_t r = Rand2();
    int16_t d1 = (int8_t)goalLookup[r & 3];

    /* Sentinel check: if negative, use high goal fallback */
    if (d1 < 0) {
        uint8_t *fallback = _simHighGoalFallbackValues;
        r = Rand2();
        uint16_t fbIdx = (uint16_t)((r * 6) >> 8);
        d1 = (int8_t)fallback[fbIdx];
    }
    /* d1 is now extended to int16_t */

    /* Look up additional goals from tactic-based probability */
    uint8_t *tacticGoalProbs = (uint8_t *)_simGoalProbByTacticPtrs[d1];
    r = Rand2();
    int16_t d2 = 0;

    /* Walk up to 4 cumulative thresholds */
    uint8_t *tp = tacticGoalProbs;
    if (r >= tp[0]) {
        r -= tp[0]; d2++;
        if (r >= tp[1]) {
            r -= tp[1]; d2++;
            if (r >= tp[2]) {
                r -= tp[2]; d2++;
                if (r >= tp[3]) {
                    r -= tp[3]; d2++;

                    /* Exceeded 4 thresholds: use extra table */
                    uint8_t *extraProbs = (uint8_t *)_simExtraGoalProbByTacticPtrs[d1];
                    r = Rand2();
                    d2 = 3;
                    if (r >= extraProbs[0]) {
                        r -= extraProbs[0]; d2++;
                        if (r >= extraProbs[1]) {
                            d2++;
                        }
                    }
                }
            }
        }
    }

    d1 += d2;  /* total = base + additional */

    /* If delta was negative, swap d1 and d2 */
    if (d3 < 0) {
        int16_t tmp = d1;
        d1 = d2;
        d2 = tmp;
    }

    team1SimulatedGoalsOut = (uint16_t)d1;
    team2SimulatedGoalsOut = (uint16_t)d2;

    /* Assign goals to scorers for first phase */
    AssignSimulatedGoalsToScorers(team1, team1PlayerRemapTable,
                                  _team1GoalCounterByPlayer, (uint16_t)d1);
    AssignSimulatedGoalsToScorers(team2, team2PlayerRemapTable,
                                  _team2GoalCounterByPlayer, (uint16_t)d2);

    /* Return the simulated goals from first phase */
    *outTeam1Goals = team1SimulatedGoalsOut;
    *outTeam2Goals = team2SimulatedGoalsOut;

    /* Rate-limit [bug3] log: a season-end auto-advance simulates tens of
     * thousands of matches, and unrestricted fprintf here turns the
     * season transition into a multi-minute log-writer crawl. First 200
     * lines is enough to diagnose Bug 3 (VIEW RESULT underperforms) for
     * a single match the user actually clicks; the season-end auto-sim
     * doesn't need to flood the log with every fixture for 65 leagues. */
#ifdef SWOS_DEBUG_AUTOSKIP
    static int s_bug3_log_count = 0;
    if (s_bug3_log_count < 200) {
        s_bug3_log_count++;
        SWOS_AUTOSKIP_LOG("[bug3] CalcViewResult: team1='%.16s' tactics=%d ctrl=%d "
                          "team2='%.16s' tactics=%d ctrl=%d "
                          "team1ViewBaseIdx=%d team2ViewBaseIdx=%d "
                          "team1OffX=%d team1OffY=%d team2OffX=%d team2OffY=%d "
                          "RESULT %u-%u",
                          (const char *)team1->team.teamName,
                          (int)(int8_t)team1->team.tactics,
                          (int)team1->team.teamControls,
                          (const char *)team2->team.teamName,
                          (int)(int8_t)team2->team.tactics,
                          (int)team2->team.teamControls,
                          (int)team1ViewBaseIndex, (int)team2ViewBaseIndex,
                          (int)team1ViewOffsetX, (int)team1ViewOffsetY,
                          (int)team2ViewOffsetX, (int)team2ViewOffsetY,
                          (unsigned)*outTeam1Goals, (unsigned)*outTeam2Goals);
    }
#endif
    WorldRules_CpuLineupsEnd(1);
}

/* ================================================================
 * ExtraTimeSecondPhaseScoring (loc_121E56)
 * Separate entry point called from GetCachedOrCalculateExtraTimeResult.
 * Performs second Y-offset-based scoring phase, adding goals to
 * team1/2SimulatedGoalsOut and assigning to scorers.
 * Returns d3 (team1 extra goals) and d4 (team2 extra goals).
 * ================================================================ */
__attribute__((used, externally_visible))
void ExtraTimeSecondPhaseScoring(uint16_t *outD3, uint16_t *outD4) {
    int16_t d1 = team1ViewBaseIndex + team1ViewOffsetY;
    if (d1 < 0) d1 = 0;
    if (d1 > 31) d1 = 31;
    team1ViewIndex = (uint16_t)d1;

    int16_t d2 = team2ViewBaseIndex + team2ViewOffsetY;
    if (d2 < 0) d2 = 0;
    if (d2 > 31) d2 = 31;
    team2ViewIndex = (uint16_t)d2;

    /* Absolute difference of Y-based indices */
    int16_t yDiff = d1 - d2;
    if (yDiff < 0) yDiff = -yDiff;

    uint8_t *yGoalLookup = _simGoalsByIndexDiffTable + ((uint16_t)yDiff << 2);
    uint16_t r = Rand2();
    int16_t d3 = (int8_t)yGoalLookup[r & 3];

    /* Extra goals from Y-phase thresholds */
    uint8_t *extraA = _simExtraGoalThresholdsA;
    r = Rand2();
    int16_t d4 = 0;

    if (r >= extraA[0]) {
        r -= extraA[0]; d4++;
        if (r >= extraA[1]) {
            r -= extraA[1]; d4++;

            /* Use second threshold table */
            uint8_t *extraB = _simExtraGoalThresholdsB;
            r = Rand2();
            d4 = 1;
            if (r >= extraB[0]) {
                r -= extraB[0]; d4++;
            }
        }
    }

    d3 += d4;

    /* Assign extra goals: higher-indexed team gets d3, other gets d4 */
    if (!((uint16_t)d1 > (uint16_t)d2)) {
        int16_t tmp = d3;
        d3 = d4;
        d4 = tmp;
    }

    team1SimulatedGoalsOut += (uint16_t)d3;
    team2SimulatedGoalsOut += (uint16_t)d4;

    /* Assign second-phase goals to scorers */
    AssignSimulatedGoalsToScorers(viewResultTeam1Ptr, team1PlayerRemapTable,
                                  _team1GoalCounterByPlayer, (uint16_t)d3);
    AssignSimulatedGoalsToScorers(viewResultTeam2Ptr, team2PlayerRemapTable,
                                  _team2GoalCounterByPlayer, (uint16_t)d4);

    *outD3 = (uint16_t)d3;
    *outD4 = (uint16_t)d4;
}

/* ================================================================
 * PenaltyShootoutScorerPair (loc_121FCE)
 * Separate entry point called from GetCachedOrCalculatePenaltyShootoutResult.
 * Returns d5 and d6 via output params.
 * ================================================================ */
__attribute__((used, externally_visible))
void PenaltyShootoutScorerPair(int16_t *outD5, uint16_t *outD6) {
    uint16_t d0 = Rand2();
    uint16_t d6 = d0;
    uint8_t d5byte = _simGoalScorerPairTable[d0 & 0x3F];

    /* If bit 7 of d6 is set, rotate d5 right by 4 (swap nibbles) */
    if (d6 & 0x80) {
        d5byte = (uint8_t)((d5byte >> 4) | (d5byte << 4));
    }

    d6 = d5byte;
    /* d5 = high nibble (sign-extended from byte) */
    *outD5 = (int16_t)(int8_t)(d5byte >> 4);
    /* d6 = low nibble */
    *outD6 = d6 & 0x0F;
}

/* ================================================================
 * Chunk 102: Match Team Init & Play Functions
 * ================================================================ */

/* Additional externs for Chunk 102 */
extern uint16_t _numSelectedTeams;
extern uint16_t _careerLoadedFlag;
extern uint16_t _selTeamNumber;
extern uint16_t _stayingInOldClub;

/* Asm function bridges */
extern void InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);
extern void InitCareerVariables(uint16_t gameType);

/* C functions from other modules */
extern int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing,
                                     uint8_t *contText, uint8_t *abortTextStr,
                                     uint16_t defaultEntry);

/* String externs */
extern uint8_t _aDiyCompetitionPrompt[];
extern uint8_t _aPresetCompetitionPrompt[];
extern uint8_t _aSeasonPrompt[];
extern uint8_t _aCareerPrompt[];
extern uint8_t _aContinue[];
extern uint8_t _aAbort[];

/* ================================================================
 * TeamInit  (SWOS2.s line 30304)
 *
 * Initializes player flags for a team. Clears cards/injuries,
 * resets loan/position bits, clears goal stats.
 * For careerTeam with stayingInOldClub, preserves loan bit and playerValuesDirty.
 *
 * Asm: TeamInit(a0=TeamFile*, d2=numPlayersMinus1)
 * ================================================================ */
static void teamInit(TeamFileEx *tfex, uint16_t numPlayersMinus1)
{
    tfex->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    tfex->team.writesZeroHere = 0;
    tfex->team.andWith0xFE &= 0xFE;  /* bclr #0 */

    for (int16_t i = (int16_t)numPlayersMinus1; i >= 0; i--) {
        PlayerFile *pf = &tfex->players[i];
        pf->cardsInjuries = 0;

        if (tfex != &_careerTeam || !_stayingInOldClub) {
            pf->positionAndFace &= ~PF_LOAN;  /* clear loan bit (bclr #1) */
            pf->form = 0;
        }

        pf->positionAndFace &= ~(1 << 2);  /* bclr #2 */
        pf->goalStats[0] = 0;
        pf->goalStats[1] = 0;
        pf->goalStats[2] = 0;
        pf->goalStats[3] = 0;
    }
}

/* ================================================================
 * InitTeams  (SWOS2.s line 30283)
 *
 * Initializes all selected teams. Then if career is not loaded and
 * selTeamNumber is valid, also inits career team with 32 players.
 *
 * Asm: InitTeams() — no params
 * ================================================================ */
__attribute__((used, externally_visible))
void InitTeams(void)
{
    TeamFileEx *teams = _selectedTeams;

    for (int16_t i = (int16_t)(_numSelectedTeams - 1); i >= 0; i--) {
        teamInit(teams, 15);
        teams++;
    }

    if (!_careerLoadedFlag && _selTeamNumber != 0xFFFF) {
        teamInit(&_careerTeam, 31);
    }
}

/* ================================================================
 * PlayFriendly  (SWOS2.s line 31648)
 *
 * Sets up team pointers and calls InitAndPlayGame for a friendly.
 * a1 = &selTeamsPtr (1st team), a2 = &selTeamsPtr + 684 (2nd team)
 *
 * Asm: PlayFriendly() — no params
 * ================================================================ */
__attribute__((used, externally_visible))
void PlayFriendly(void)
{
    TeamFileEx *teams = _selectedTeams;
    InitAndPlayGame(&teams[0], &teams[1]);
}

/* ================================================================
 * CheckContinueAbortPrompt  (SWOS2.s line 30243)
 *
 * Shows a "continue or abort?" prompt based on game type.
 * Returns 0 (abort/no game) or non-zero (continue).
 *
 * Asm: CheckContinueAbortPrompt(a0=header) -> d0, ZF
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t CheckContinueAbortPrompt(uint8_t *header)
{
    if (_gameType == 0) {  /* GAME_TYPE_NO_GAME */
        return 0;
    }

    uint8_t *changingText;

    if (_gameType == 1) {       /* GAME_TYPE_DIY_COMPETITION */
        changingText = _aDiyCompetitionPrompt;
    } else if (_gameType == 2) { /* GAME_TYPE_PRESET_COMPETITION */
        changingText = _aPresetCompetitionPrompt;
    } else if (_gameType == 3) { /* GAME_TYPE_SEASON */
        changingText = _aSeasonPrompt;
    } else {
        changingText = _aCareerPrompt;
    }

    int16_t result = DoContinueAbortMenu(header, changingText,
                                           _aContinue, _aAbort, 1);
    if (result != 0) {
        return result;  /* user chose continue */
    }

    /* User chose abort — reset career variables */
    InitCareerVariables(0);
    return 0;
}


/* ================================================================
 * ScaleSkill (SWOS2.s line 29374)
 *
 * Scales a raw skill nibble (0-7) by the player's price factor d4
 * and the team's appearance percentage. Adds random rounding based
 * on the fractional remainder of the multiplication.
 *
 * in:  skill  = raw skill value (0-7 range)
 *      factor = percentage factor from GetPlayerPricePercentageChange
 *      teamGame = pointer to TeamGame (topTeamInGame or bottomTeamInGame)
 * out: scaled skill value
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t ScaleSkill(uint16_t skill, int16_t factor, TeamGame *teamGame)
{
    int16_t d2 = factor;

    /* Clamp factor to [75, 125] */
    if (d2 < 75)
        d2 = 75;
    if (d2 > 125)
        d2 = 125;

    /* Get team appearance percentage */
    uint16_t d3;
    if (teamGame == &_topTeamInGame)
        d3 = _team1AppPercent;
    else
        d3 = _team2AppPercent;

    /* d2 = factor * appPercent / 100
     * ASM: mulu.w d3,d2 (16x16→32) then divu.w #100,d2 (32÷16).
     * Must NOT truncate the 32-bit product before dividing. */
    d2 = (int16_t)(((uint32_t)(uint16_t)d2 * (uint32_t)d3) / 100);

    /* d0 = skill * d2 / 100 */
    uint32_t product = (uint16_t)skill * (uint16_t)d2;
    uint16_t result = (uint16_t)(product / 100);
    uint16_t remainder = (uint16_t)(product % 100);  /* swap d2 gives remainder */

    /* Random rounding: if Rand2() * 100 / 256 < remainder, add 1 */
    uint16_t r = Rand2();
    r = (uint16_t)((uint16_t)(r * 100) >> 8);
    if (r < remainder)
        result++;

    return result;
}


/* ================================================================
 * GetPlayerPricePercentageChange (SWOS2.s line 29348)
 *
 * Adds team view offset, price rank offset, and random variation
 * to the player's price change factor d4.
 *
 * in:  factor = current price change factor
 *      teamFile = team file pointer (used for CalcTeamViewOffset)
 * out: adjusted factor
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t GetPlayerPricePercentageChange(int16_t factor, TeamFile *teamFile,
                                        uint16_t callerD0)
{
    int16_t d4 = factor;

    /* Skip team offset adjustments for friendly games and computer-controlled teams */
    if (_isGameFriendly == 0) {
        if (teamFile->teamControls != TEAM_PLAYER_COACH) {
            /* Add caller's d0 value (per-player random from ApplyTeamTactics)
             * Original asm: d0 was saved/restored around CalcTeamViewOffset,
             * then added to d4. This d0 value is GetPlayerRandomNum ^ gameRandValue. */
            d4 += (int16_t)callerD0;

            /* Add price rank offset */
            int16_t pctOff = CalcTeamPriceRankPercentOffset(&_diyCompetitionStart.base, teamFile);
            d4 += pctOff;
        }
    }

    /* Add random variation: [-12, +11] */
    uint16_t r = Rand2();
    r = (uint16_t)((uint16_t)(r * 24) >> 8);  /* random 0..23 */
    d4 += (int16_t)r - 12;

    return d4;
}


/* ================================================================
 * AdjustPlayerSkills (SWOS2.s line 29231)
 *
 * Adjusts all 7 skill values + goalie skill for one player based on
 * the player's calculated price vs their file price, team controls,
 * and home/away factors.
 *
 * in:  playerIdx  = loop index (0=goalkeeper special case)
 *      teamFile   = team file (a2 in asm)
 *      oppTeamFile = opponent team file (a3 in asm)
 *      teamGame   = TeamGame pointer (a4 in asm)
 *      playerBase = GetPlayer result (a5 in asm)
 *      pi         = PlayerInfo for this player in TeamGame (a6 in asm)
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
/* The factor each starting player's skills were scaled by at kick-off, as rolled
 * (ScaleSkill clamps it to 75..125). Written for the log line below, read by nothing
 * the match does -- so it is not match state, and not in the rollback registry. */
static int16_t s_skillFactor[2][11];
#endif

__attribute__((used, externally_visible))
void AdjustPlayerSkills(uint16_t playerIdx, TeamFile *teamFile,
                         TeamFile *oppTeamFile, TeamGame *teamGame,
                         uint8_t *playerBase, PlayerInfo *pi,
                         uint16_t callerD0)
{
    TeamFile *tf = teamFile;
    TeamFile *oppTf = oppTeamFile;
    /* playerBase points to start of team data for this player's slot;
     * player file data starts after TeamFile header (76 bytes) */
    PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));
    int16_t d4;

    if (playerIdx == 0) {
        /* Goalkeeper: use file price directly as factor */
        d4 = (int16_t)(int8_t)pf->price;
    } else {
        /* Field player: calculate price via GetPlayerPrice */
        d4 = GetPlayerPrice(teamFile, (PlayerFile *)playerBase, playerIdx);
    }

    /* Compute percentage: (calculatedPrice * 100) / filePrice */
    int16_t filePrice = (int16_t)(int8_t)pf->price;
    if (filePrice == 0)
        filePrice = 1;

    /* ASM: mulu.w #100,d4 (16x16→32) then divu.w d3,d4 (32÷16) */
    d4 = (int16_t)(((uint32_t)(uint16_t)d4 * 100) / (uint16_t)filePrice);

    /* Computer-controlled teams always use 100% */
    if (tf->teamControls == TEAM_COMPUTER)
        d4 = 100;

    /* Bottom team penalty when not randomize-pitch mode */
    if (_plgD0Param == 0 && teamGame == (TeamGame *)&_bottomTeamInGame) {
        int16_t applyPenalty = 0;
        if (tf->teamControls != TEAM_COMPUTER) {
            /* Own team is human-controlled → apply penalty */
            applyPenalty = 1;
        } else if (oppTf->teamControls != TEAM_PLAYER_COACH) {
            /* Own team is computer, opponent is NOT player+coach → apply penalty */
            applyPenalty = 1;
        }
        /* Own team is computer AND opponent is player+coach → skip */
        if (applyPenalty) {
            d4 -= 12;
            if (d4 < 0)
                d4 = 0;
        }
    }

#ifdef SWOS_SDL_BUILD
    if (getenv("SWOS_SKILL_DEBUG"))
        fprintf(stderr, "[skill] %s slot %u: file price %d, computed %d -> %d%% before the random, controls %d\n",
                teamGame == &_topTeamInGame ? "top" : "bottom", (unsigned)playerIdx,
                (int)(int8_t)pf->price,
                playerIdx ? (int)GetPlayerPrice(teamFile, (PlayerFile *)playerBase, playerIdx) : -1,
                (int)d4, (int)tf->teamControls);
#endif
    /* Apply team view offset + rank + random to factor */
    d4 = GetPlayerPricePercentageChange(d4, teamFile, callerD0);
#ifdef SWOS_SDL_BUILD
    if (playerIdx < 11)                 /* for MatchSim_LogSkillRoll only; never read back */
        s_skillFactor[teamGame == &_topTeamInGame ? 0 : 1][playerIdx] = d4;
#endif

    /* Read packed skills longword from PlayerFile:
     * passing(28), velocityHeading(29), tacklingControl(30), speedFinishing(31).
     * Each byte has two 4-bit nibbles. The mask 0x07777777 clears the unused
     * high nibble of the passing byte and keeps all 7 skill nibbles (0-7 range).
     * After lsl #4, the loop uses rol #4 to extract each nibble in order:
     * passing, shooting, heading, tackling, ballControl, speed, finishing. */
    uint8_t *skillBytes = (uint8_t *)&pf->passing;
    uint32_t skills = ((uint32_t)skillBytes[0] << 24) |
                      ((uint32_t)skillBytes[1] << 16) |
                      ((uint32_t)skillBytes[2] << 8) |
                      ((uint32_t)skillBytes[3]);

    skills &= 0x07777777;
    skills <<= 4;

    /* Scale each of the 7 skill nibbles and store to PlayerInfo */
    int8_t *piSkills = &pi->passing;  /* passing, shooting, heading, tackling, ballControl, speed, finishing */
    for (int16_t i = 0; i < 7; i++) {
        /* Rotate left 4 bits to get next nibble */
        skills = (skills << 4) | (skills >> 28);
        uint16_t rawSkill = (uint16_t)(skills & 0xF);

        uint16_t scaled = ScaleSkill(rawSkill, d4, teamGame);
        if (scaled > 7)
            scaled = 7;
        piSkills[i] = (int8_t)scaled;
    }

    /* Goalie skill calculation (only for goalkeepers — position 0) */
    pi->goalieSkill = 0;
    uint8_t position = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
    if (position == 0) {
        /* Goalkeeper: (price + 3) / 7, using unsigned division matching asm ext.l + divu */
        int16_t priceWord = (int16_t)(int8_t)pf->price;
        priceWord += 3;
        uint32_t priceLong = (uint32_t)(int32_t)priceWord;  /* ext.l: sign extend to 32-bit */
        int16_t gkSkill = (int16_t)(uint16_t)(priceLong / 7);  /* divu.w #7 */

        /* Random addition based on team side */
        uint16_t randBit;
        if (teamGame == &_topTeamInGame) {
            randBit = _gameRandValue & 1;
        } else {
            randBit = (_gameRandValue & 2) >> 1;
        }
        gkSkill += randBit + 1;

        /* Skill boost/clamp based on team controls */
        int16_t needClamp = 0;
        if (oppTf->teamControls == TEAM_PLAYER_COACH) {
            gkSkill += TEAM_COMPUTER;  /* +1 */
            needClamp = 1;
        } else if (tf->teamControls == TEAM_PLAYER_COACH &&
                   oppTf->teamControls == TEAM_COMPUTER) {
            needClamp = 1;
        } else if (tf->teamControls == TEAM_COMPUTER &&
                   oppTf->teamControls == TEAM_PLAYER_COACH) {
            needClamp = 1;
        }

        if (needClamp) {
            if (gkSkill > 7)
                gkSkill = 7;
            if (gkSkill < 0)
                gkSkill = 0;
        }

        pi->goalieSkill = (int8_t)gkSkill;
    }
}


/* ================================================================
 * ApplyTeamTactics (SWOS2.s line 29177)
 *
 * Loops through the first 11 players of a team and adjusts their
 * skills based on calculated price, team factors, and random seeding.
 * Temporarily overrides the team's tactics byte with the player's
 * chosen tactics value.
 *
 * in:  teamGame = pointer to TeamGame (topTeamInGame or bottomTeamInGame)
 * ================================================================ */
__attribute__((used, externally_visible))
void ApplyTeamTactics(TeamGame *teamGame)
{
    /* Determine tactics and team pointers based on which team */
    uint16_t tactics;
    TeamFile *ownTeamFile;
    TeamFile *oppTeamFile;

    if (teamGame == &_topTeamInGame) {
        tactics = _pl1Tactics;
        ownTeamFile = _gameTeam1;
        oppTeamFile = _gameTeam2;
    } else {
        tactics = _pl2Tactics;
        ownTeamFile = _gameTeam2;
        oppTeamFile = _gameTeam1;
    }

    /* Save and temporarily override tactics byte in team file */
    TeamFile *tf = ownTeamFile;
    uint8_t savedTactics = tf->tactics;
    tf->tactics = (uint8_t)tactics;

    /* Loop through 11 starting players */
    for (uint16_t i = 0; i < 11; i++) {
        PlayerInfo *pi = &teamGame->players[i];

        /* Get packed team number: (countryNumber << 8) | teamOrdinal */
        uint16_t teamNumber = ((uint16_t)tf->countryNumber << 8) | tf->teamOrdinal;

        /* Get player file index from PlayerInfo */
        int16_t playerFileIdx = (int16_t)(int8_t)pi->playerIndex;

        /* Get pointer to player in team file */
        uint8_t *playerBase = GetPlayer(teamNumber, (uint16_t)playerFileIdx);

        /* Save random state, seed per-player random, adjust skills, restore.
         * The XOR'd seed value (d0 in asm) is passed through to
         * GetPlayerPricePercentageChange as an additional random factor. */
        SaveRand2State();

        int16_t playerRand = (int16_t)GetPlayerRandomNum((PlayerFile *)playerBase);
        uint16_t seed = (uint16_t)playerRand ^ _gameRandValue;
        Randomize2(seed);

        AdjustPlayerSkills(i, ownTeamFile, oppTeamFile, teamGame,
                            playerBase, pi, seed);

        RestoreRand2State();
    }

    /* Restore original tactics byte */
    tf->tactics = savedTactics;
}

#ifdef SWOS_SDL_BUILD
/* SWOS rolls every player's form for the match at kick-off (AdjustPlayerSkills): their
 * skills are scaled by a factor around 100%, from their price, the team's standing and
 * a random draw seeded per player -- the reason a side can feel sharper in one match
 * than the next. One line per team for SWOS.log: each starting player's factor (clamped
 * as ScaleSkill clamps it), the team's appearance percentage, and the seven skills of
 * all eleven summed as they play this match (0..7 each, so at most 539). */
void MatchSim_LogSkillRoll(void)
{
    int32_t t, i, k;
    for (t = 0; t < 2; t++) {
        TeamGame *tg = t == 0 ? &_topTeamInGame : &_bottomTeamInGame;
        TeamFile *tf = t == 0 ? _gameTeam1 : _gameTeam2;
        char factors[11 * 5 + 1];
        int32_t n = 0, sumFactor = 0, played = 0;
        for (i = 0; i < 11; i++) {
            int32_t f = s_skillFactor[t][i];
            f = f < 75 ? 75 : f > 125 ? 125 : f;
            sumFactor += f;
            n += snprintf(factors + n, sizeof factors - (size_t)n, "%s%d", i ? " " : "", (int)f);
            for (k = 0; k < 7; k++)
                played += (&tg->players[i].passing)[k];
        }
        fprintf(stderr, "[match] skill roll, %s %.17s: form %d%% on average (%s), appearance %u%%, "
                "skills %d in play\n", t == 0 ? "team 1" : "team 2", (const char *)tf->teamName,
                (int)(sumFactor / 11), factors, (unsigned)(t == 0 ? _team1AppPercent : _team2AppPercent),
                (int)played);
    }
}
#endif

/* ===== BuildMatchResultString (Chunk 106) =====
 * Builds a formatted result string from match scores and flags.
 *
 * Parameters:
 *   control - if non-zero, skip to 2nd embedded string in format
 *   leg2    - 2nd leg scores packed: high byte = teamA, low byte = teamB
 *   leg1    - 1st leg scores packed: high byte = teamA, low byte = teamB
 *   pens    - penalty scores packed: high byte = teamA, low byte = teamB
 *   teamA   - pointer to team A name string
 *   teamB   - pointer to team B name string
 *   flags   - bit field controlling which format to use
 *
 * Returns: pointer to resultString (filled with formatted text)
 *
 * Flag bits:
 *   bit 0: swap winner (teamB is winner instead of teamA)
 *   bit 1: has a winner
 *   bit 2: after extra time
 *   bit 3: penalties
 *   bit 4: away goals
 *   bit 5: two-leg aggregate
 *   bit 6: replay
 *   bit 7: show 2nd leg / show detailed scores
 */
__attribute__((used, externally_visible))
uint8_t *BuildMatchResultString(uint16_t control, uint16_t leg2, uint16_t leg1,
                                  uint16_t pens, uint8_t *teamA, uint8_t *teamB,
                                  uint16_t flags)
{
    buildMatchResultStringControl = control;

    uint8_t *a3 = teamA;  /* saved copy of teamA */
    uint8_t *a4 = teamB;  /* saved copy of teamB */
    uint16_t d0 = 0, d1 = 0, d2 = 0, d3 = 0;
    uint8_t *fmt = 0;
    uint8_t *winnerName = 0; /* %a in format — points to winner team name */

    if (flags & (1 << 5)) {
        /* --- Two-leg aggregate path --- */
        if (!(flags & (1 << 7))) {
            /* 1st leg only — no 2nd leg yet */
            d0 = leg1 >> 8;         /* team A 1st leg goals */
            d1 = leg1 & 0xFF;       /* team B 1st leg goals */
            fmt = _a1stLeg01;        /* "1ST LEG %0-%1" */
            /* winnerName not used in this format */
            winnerName = a3;
        } else {
            /* Aggregate: compute total */
            d2 = (leg2 + leg1) >> 8;   /* Note: upper bytes add for teamA aggregate */
            d3 = (uint8_t)((leg2 & 0xFF) + (leg1 & 0xFF));  /* teamB aggregate */
            /* Actually, the asm does: d2=d4, d2+=d5, lsr #8 for teamA agg;
             * d3=d4, d3+=d5, AND #FF for teamB agg.
             * The full 16-bit add then shift/mask. Let me be precise: */
            d2 = (uint16_t)(leg2 + leg1) >> 8;   /* teamA aggregate */
            d3 = (uint16_t)(leg2 + leg1) & 0xFF;  /* teamB aggregate */

            /* Penalty scores for %0-%1 */
            d0 = pens >> 8;
            d1 = pens & 0xFF;

            /* Winner team name: default teamA, swap if bit 0 */
            winnerName = a3;
            if (flags & (1 << 0)) {
                winnerName = a4;
                /* Swap d0/d1 and d2/d3 */
                uint16_t tmp;
                tmp = d0; d0 = d1; d1 = tmp;
                tmp = d2; d2 = d3; d3 = tmp;
            }

            if (flags & (1 << 1)) {
                /* Has a winner */
                if (flags & (1 << 2)) {
                    /* After extra time */
                    if (flags & (1 << 4)) {
                        /* Away goals + AET */
                        fmt = _a23OnAggAWinOnA_0; /* "%2-%3 ON AGG. %a WIN ON AWAY GOALS A.E.T." */
                    } else if (flags & (1 << 3)) {
                        /* Penalties + AET */
                        fmt = _a23OnAggAWin01O_0; /* "%2-%3 ON AGG. %a WIN %0-%1 ON PENS A.E.T." */
                    } else {
                        /* AET only (no pens, no away goals) */
                        fmt = _aAWin23OnAggAET;   /* "%a WIN %2-%3 ON AGG. A.E.T." */
                    }
                } else {
                    /* Not after extra time */
                    if (flags & (1 << 4)) {
                        /* Away goals */
                        fmt = _a23OnAggAWinOnA;   /* "%2-%3 ON AGG. %a WIN ON AWAY GOALS" */
                    } else if (flags & (1 << 3)) {
                        /* Penalties */
                        fmt = _a23OnAggAWin01O;   /* "%2-%3 ON AGG. %a WIN %0-%1 ON PENS" */
                    } else {
                        /* Plain aggregate win */
                        fmt = _aAWin23OnAgg;      /* "%a WIN %2-%3 ON AGG." */
                    }
                }
            } else {
                /* No winner yet — just aggregate score */
                fmt = _a23OnAgg;  /* "%2-%3 ON AGG." */
            }
        }
    } else {
        /* --- Single match path (not two-leg) --- */
        if (!(flags & (1 << 7))) {
            /* No detailed scores → empty result */
            _resultString[0] = 0;
            return _resultString;
        }

        /* Single match with detail */
        d2 = leg1 >> 8;
        d3 = leg1 & 0xFF;
        d0 = pens >> 8;
        d1 = pens & 0xFF;

        winnerName = a3;
        if (flags & (1 << 0)) {
            winnerName = a4;
            /* Only swap d0/d1 here (not d2/d3 — asm doesn't exg d2,d3 in this path) */
            uint16_t tmp = d0; d0 = d1; d1 = tmp;
        }

        if (flags & (1 << 2)) {
            /* After extra time */
            if (flags & (1 << 3)) {
                /* Penalties + AET */
                if (flags & (1 << 6)) {
                    /* Replay */
                    fmt = _aAWin01OnPensAE_3; /* "%a WIN %0-%1 ON PENS A.E.T. (REPLAY)" */
                    if (leg2 != leg1) {
                        fmt = _aAWin01OnPensAE_5; /* "%a WIN %0-%1 ON PENS A.E.T. 90 MINS %2-%3 (REPLAY)" */
                    }
                } else {
                    fmt = _aAWin01OnPensAE; /* "%a WIN %0-%1 ON PENS A.E.T." */
                    if (leg2 != leg1) {
                        fmt = _aAWin01OnPensAE_1; /* "%a WIN %0-%1 ON PENS A.E.T. 90 MINS %2-%3" */
                    }
                }
            } else {
                /* AET without penalties */
                if (flags & (1 << 6)) {
                    /* Replay */
                    fmt = _aAETReplay; /* "A.E.T. (REPLAY)" */
                    if (leg2 != leg1) {
                        fmt = _aAET90Mins23Rep; /* "A.E.T. 90 MINS %2-%3 (REPLAY)" */
                    }
                } else {
                    fmt = _aAET; /* "A.E.T." */
                    if (leg2 != leg1) {
                        fmt = _aAET90Mins23; /* "A.E.T. 90 MINS %2-%3" */
                    }
                }
            }
        } else if (flags & (1 << 3)) {
            /* Penalties without AET */
            if (flags & (1 << 6)) {
                fmt = _aAWin01OnPensRe; /* "%a WIN %0-%1 ON PENS (REPLAY)" */
            } else {
                fmt = _aAWin01OnPens; /* "%a WIN %0-%1 ON PENS" */
            }
        } else if (flags & (1 << 6)) {
            /* Replay only */
            fmt = _aReplay_0; /* "(REPLAY)" */
        } else {
            /* No special flags at all → empty result */
            _resultString[0] = 0;
            return _resultString;
        }
    }

    /* control != 0 selects the COMPACT ("second string") form. The asm reached
     * it by walking past the NUL, which only works because the two strings are
     * adjacent in the data block; in C they are separate objects, so resolve
     * the sibling by name instead. See resultFormatCompact(). */
    if (buildMatchResultStringControl != 0)
        fmt = resultFormatCompact(fmt);

    /* Call PrintFormatted: fmt, out=resultString, d0-d6, %a=winnerName */
    PrintFormatted(fmt, _resultString, d0, d1, d2, d3,
                    0, 0, 0,           /* d4-d6 unused by these formats */
                    winnerName, 0, 0);

    return _resultString;
}

/* CalcTeamPriceRankPercentOffset (SWOS2.s line 25505)
 * Calculates a price-rank percentage offset for a team in a competition.
 * Used by view-result scaling to bias scores based on team expensiveness.
 * a0 = competition context, a1 = team file pointer.
 * Returns half of the clamped [-50, +50] percentage offset. */
__attribute__((used, externally_visible))
int16_t CalcTeamPriceRankPercentOffset(DIYCompetition *comp, TeamFile *team)
{
    /* competitionType must be 0 */
    if (comp->competitionType != 0)
        return 0;

    /* Check minimum number of entries: row[0].gamesPlayed
     * (DIYTeamStats[0] starts at parent +0x2B4, gamesPlayed at +0x2B8). */
    DIYTeamStats *row0 = (DIYTeamStats *)((uint8_t *)comp + DIY_STANDINGS_OFFSET);
    uint16_t numEntries = row0->gamesPlayed;
    if (team->teamControls == TEAM_COMPUTER) {
        /* Cup: need at least 3 entries */
        if (numEntries < 3)
            return 0;
    } else {
        /* League: need at least 10 entries */
        if (numEntries < 10)
            return 0;
    }

    /* Read first two bytes as a packed uint16_t: (countryNumber << 8) | teamOrdinal */
    uint16_t teamNum = ((uint16_t)team->countryNumber << 8) | team->teamOrdinal;
    uint16_t teamIndex = FindTeamInDIY(teamNum, comp);
    uint16_t expensiveCount = GetMoreExpensiveTeamsCount(teamNum, comp);

    int16_t diff = (int16_t)(expensiveCount - teamIndex);
    int32_t scaled = (int32_t)diff * 100;

    int16_t totalTeams = comp->numTeams;
    int16_t divisor = totalTeams - 1;
    if (divisor == 0)
        return 0;

    int16_t pct = (int16_t)(scaled / (int32_t)divisor);

    if (pct > 50) pct = 50;
    if (pct < -50) pct = -50;

    return pct >> 1;
}

/* ================================================================
 * Chunk 187 — GetPlayerPrice + UpdatePlayerPositionAndReturnRole
 *
 * GetPlayerPrice: computes a player's effective price based on
 * skills, tactics position, and distance weighting across all 35
 * pitch quadrants.
 *
 * UpdatePlayerPositionAndReturnRole: determines a player's effective
 * position/role value and stores it in playerPositionsArray.
 * Returns packed (role << 16) | position.
 * ================================================================ */

/* ASM data externs for Chunk 187 */
extern uint8_t _skillsPerQuadrant[];          /* 7×35 skill table (positions 1-7); GK row is 35 bytes before */
extern uint32_t _priceSkillPonders[];         /* 35 longwords: packed skill ponders per quadrant */
extern uint8_t _quadrantCoordinatesToBallQuadrant[]; /* 240 bytes: coordinate->quadrant map */
extern uint8_t _quadrantDistanceFactor[];     /* 35×35 distance factor matrix */
extern uint8_t _skillValuePonders[];          /* 8 bytes: skill value multipliers */
extern uint8_t _plPricesIncreaseTable[];      /* 50 bytes: price increase lookup */
extern void *_teamTacticsTable[];          /* 19 pointers to tactics data */
extern uintptr_t _plPositions;                 /* tactics-data base pointer; retyped uintptr_t for 64-bit. See project_uint32_pointer_storage_trap.md. */
extern uint16_t _totalSkillsFactorSum;        /* accumulator for skill factor sum */
extern uint16_t _numPlayersAtQuadrant;        /* count of players sharing a quadrant */
extern uint8_t _currentMatchPlayers[];
extern uint8_t _playerPositionsArray[];
extern uint16_t _inEditTactics;
extern uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);
extern uint16_t CountNearnessFactor(uint16_t curBallQuadrant, uint16_t dstBallQuadrant,
                                     uint16_t price, const uint8_t *curQuadDistFactor,
                                     const uint8_t *_playerPositions, const uint8_t *coordToQuad);

/* GetPlayerPrice (SWOS2.s line 27392)
 *
 * Calculates a player's effective price based on skills, tactical position,
 * and distance weighting across 35 pitch quadrants.
 *
 * Parameters:
 *   teamFile     - pointer to TeamFile/TeamFileEx (a0 in asm)
 *   playerBase   - teamFile + playerSlot * 38 (a1 in asm)
 *   playerNumber - 1-based player number (d7 in asm), always >= 1
 *
 * Returns: price value (d7 in asm) as int16_t
 */
__attribute__((used, externally_visible))
int16_t GetPlayerPrice(TeamFile *teamFile, PlayerFile *playerFile, uint16_t playerNumber)
{
    /* Player data is at playerFile[2] (2 * 38 = 76 bytes = sizeof(TeamFile)) */
    PlayerFile *plFile = &playerFile[2];
    TeamFile *tf = teamFile;

    /* Position type (0-7) from top 3 bits of positionAndFace */
    uint16_t position = (plFile->positionAndFace & PF_POSITION_MASK) >> 5;

    /* Skill weights for this position -- and position 0 (GK) has NO ROW.
     *
     * Both originals index `skillsPerQuadrant + position * 35 - 35` (SWOS2.s:48254,
     * swos.asm:127992 "make it zero based"), so position 0 reads the 35 bytes BEFORE
     * the table. On the Amiga those are instruction bytes (loc_122D6A+1); in DOS they
     * are zero, and deliberately so -- ReadPitchFile zero-fills everything from
     * g_currentMenu up to skillsPerQuadrant (swos.asm:100633) before every match.
     *
     * In a C port that read is whatever the linker put in front of the array, which is
     * neither: on x86_64 it lands on the tail of _skillQuadrantAdjustDeltas (zeros, so
     * it happens to behave like DOS), and on macOS arm64 on a POINTER TABLE -- so a
     * goalkeeper's weights moved with ASLR and the price changed on every run. That is
     * a simulation value: it reaches AdjustPlayerSkills, and _totalSkillsFactorSum is
     * in the netplay checksum, so two peers computed different state from the same
     * inputs (found 2026-09-17 comparing frame hashes of zkz-11122841 on macOS arm64
     * against Windows and Linux x86_64 -- one registry entry of 355 differed).
     *
     * So position 0 gets a defined row of its own: DOS's zeros, which is also what the
     * x86_64 builds the corpus is verified against have been using all along, so no
     * verified behaviour moves -- only macOS stops being random.
     *
     * The AMIGA row is reconstructible if we ever want that fidelity instead. SWOS-RE's
     * Variables.md and reloc dump put oldPlayerSkillDivisor at 0x00122D8C and
     * oldPlayerSkill at 0x00157400, and skillsPerQuadrant at 0x00122D8E = loc_122D6A+36,
     * so the 35 bytes are that block's opcodes:
     *
     *   43 00 E0 EA 49 0C 01 00 00 67 02 E2 48 48 C0 80 F9 00 12 2D 8C
     *   33 C0 00 15 74 00 42 28 00 3B 4E 75  + the live oldPlayerSkillDivisor word
     *
     * read as SIGNED bytes, so a position-0 player's Amiga price is loud nonsense --
     * and its last two bytes are a VARIABLE the edit-tactics screen writes, which would
     * make the simulation depend on a peer's menu history. That is the same class of
     * bug as the one above, so it stays out until someone wants it deliberately. */
    static const uint8_t k_gkQuadrantWeights[35] = { 0 };
    const uint8_t *skillsPerQ = position ? _skillsPerQuadrant + (position - 1) * 35
                                         : k_gkQuadrantWeights;

    /* Player skills packed: 4 bytes starting at passing (offset 28).
     * Big-endian: passing, velocityHeading, tacklingControl, speedFinishing */
    uint32_t d5 = ((uint32_t)(uint8_t)plFile->passing << 24) |
                  ((uint32_t)(uint8_t)plFile->velocityHeading << 16) |
                  ((uint32_t)(uint8_t)plFile->tacklingControl << 8) |
                  (uint8_t)plFile->speedFinishing;
    d5 &= 0x07777777;   /* mask to 3 bits per nibble */
    d5 += 0x01111111;   /* add 1 to each nibble (range: 1-8) */

    /* Get tactics table for this team */
    int16_t tacticsIdx = (int8_t)tf->tactics;
    uint8_t *tacticsData = (uint8_t *)_teamTacticsTable[tacticsIdx] + 9;
    _plPositions = (uintptr_t)tacticsData;

    /* Player's position data in tactics table (0-based from playerNumber) */
    uint16_t plNum0 = playerNumber - 1;
    uint8_t *playerTactics = tacticsData + plNum0 * 35;

    /* Distance factor pointer starts at last row (row 34) */
    const uint8_t *distFactor = _quadrantDistanceFactor + 34 * 35;

    _totalSkillsFactorSum = 0;

    /* Main loop: iterate over 35 quadrant positions */
    for (int16_t d3 = 34; d3 >= 0; d3--) {
        uint16_t quadPrice = 0;

        /* Map position to ball quadrant */
        uint16_t ballQ = playerTactics[d3];
        ballQ = _quadrantCoordinatesToBallQuadrant[ballQ];

        /* Count players at same quadrant (human teams only) */
        if (tf->teamControls != TEAM_COMPUTER) {
            _numPlayersAtQuadrant = 1;
            if (d3 >= 5 && d3 < 30 && d3 != 7) {
                uint8_t *tacticsBase = (uint8_t *)_plPositions;
                uint16_t count = 0;
                for (int16_t i = 9; i >= 0; i--) {
                    uint16_t q = tacticsBase[d3];
                    q = _quadrantCoordinatesToBallQuadrant[q];
                    if ((uint8_t)q == (uint8_t)ballQ)
                        count++;
                    tacticsBase += 35;
                }
                _numPlayersAtQuadrant = count;
            }
        }

        /* Get packed skill ponder for this ball quadrant */
        uint32_t ponderPacked = _priceSkillPonders[ballQ];

        /* Inner loop: 7 skills */
        uint32_t skillsCopy = d5;
        for (int16_t k = 6; k >= 0; k--) {
            uint16_t skill = skillsCopy & 0xF;
            uint16_t ponder = ponderPacked & 0xF;
            if (ponder != 0) {
                ponder = _skillValuePonders[ponder];
                quadPrice += skill * ponder;
            }
            skillsCopy >>= 4;
            ponderPacked >>= 4;
        }

        /* Apply position skill weight */
        quadPrice >>= 1;
        uint16_t weight = (int8_t)skillsPerQ[ballQ];  /* ext.w from byte */
        uint32_t temp = (uint32_t)quadPrice * (uint16_t)weight;
        uint16_t tempLow = (uint16_t)temp; /* mulu.w produces 32-bit, next mulu uses low 16 */

        /* Apply distance factor */
        uint16_t dist = distFactor[ballQ];
        uint32_t temp2 = (uint32_t)tempLow * dist;
        temp2 >>= 3;

        /* Divide by number of players at quadrant (human only, >1) */
        if (tf->teamControls != TEAM_COMPUTER) {
            uint16_t n = _numPlayersAtQuadrant - 1;
            if (n != 0)
                temp2 = temp2 / n;  /* divu.w: 32/16 */
        }

        uint16_t d7val = (uint16_t)temp2;

        /* Apply nearness factor (human teams only) */
        if (tf->teamControls != TEAM_COMPUTER) {
            d7val = CountNearnessFactor(d3, ballQ, d7val,
                                         distFactor, playerTactics,
                                         _quadrantCoordinatesToBallQuadrant);
        }

        _totalSkillsFactorSum += d7val;
        distFactor -= 35;
    }

    /* Compute final price index */
    int16_t result = (int16_t)_totalSkillsFactorSum;
    /* ext.l + divu.w #350 → unsigned division of signed-extended 32-bit value */
    int32_t extended = (int32_t)result;
    uint16_t priceIdx = (uint16_t)((uint32_t)extended / 350);
    priceIdx -= 8;

    /* Clamp to 0..49 */
    if ((int16_t)priceIdx < 0)
        priceIdx = 0;
    else if (priceIdx > 49)
        priceIdx = 49;

    /* Compare computed price index vs file price (byte-level operations).
     * sub.b: d0.b = (uint8_t)priceIdx - mem.b, sets flags on byte result.
     * bmi: branch if signed byte result < 0. */
    uint8_t filePriceByte = (uint8_t)plFile->price;
    uint8_t priceIdxLow = (uint8_t)priceIdx;
    int8_t byteDiff = (int8_t)(priceIdxLow - filePriceByte);

    if (byteDiff < 0) {
        /* File price > computed: return priceIdx as-is */
        return priceIdx;
    }

    /* Price increase: d0.w = unsigned byte diff (high byte was 0) */
    uint16_t diffIdx = (uint8_t)(priceIdxLow - filePriceByte);
    /* move.b table[diff],d7 + add.b filePrice,d7 → byte operations */
    uint8_t increase = _plPricesIncreaseTable[diffIdx];
    return (int16_t)(uint8_t)(increase + filePriceByte);
}

/* UpdatePlayerPositionAndReturnRole (SWOS2.s line 26116)
 *
 * Determines a player's effective position value and role.
 * For career team: reads slot mapping from currentMatchPlayers[].
 * For other teams: reads from team->playerNumbers[].
 *
 * The function updates playerPositionsArray[] and writes the
 * position value back to the player's field_1 byte.
 *
 * Parameters:
 *   team      - pointer to TeamFile/TeamFileEx (a0 in asm)
 *   playerIdx - player index (d7 in asm)
 *
 * Returns: packed uint32_t with (role << 16) | (position & 0xFFFF)
 *          where role = position type (0-7) from positionAndFace top bits
 *          and position = the effective price/skill value for this player
 */
__attribute__((used, externally_visible))
uint32_t UpdatePlayerPositionAndReturnRole(TeamFileEx *team, uint16_t playerIdx)
{
    /* Get player slot index based on whether this is the career team */
    int16_t slotIdx;
    if (team == &_careerTeam) {
        slotIdx = (int8_t)_currentMatchPlayers[playerIdx];
    } else {
        slotIdx = (int8_t)team->team.playerNumbers[playerIdx];
    }

    int16_t d6 = slotIdx;  /* save slot for playerPositionsArray indexing */

    /* Compute player base: team + slot * PLAYERFILE_SIZE (38) */
    uint8_t *playerBase = (uint8_t *)team + (uint16_t)((uint16_t)slotIdx * PLAYERFILE_SIZE);
    /* Player file data starts after TeamFile header */
    PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));

    /* Check playerValuesDirty in TeamFile header */
    if (team->team.playerValuesDirty == TEAM_VALUES_CLEAN) {
        /* Simple path: return field_1 as position, store in array */
        int16_t pos = (int8_t)pf->cachedMatchValue;
        uint16_t role = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
        _playerPositionsArray[d6] = (uint8_t)pos;
        return ((uint32_t)role << 16) | ((uint16_t)pos & 0xFFFF);
    }

    /* Complex path: someFlag != 0 */
    uint16_t posType = (pf->positionAndFace & PF_POSITION_MASK) >> 5;
    int16_t d7val;

    if (playerIdx == 0) {
        /* Goalkeeper slot */
        if (posType == 0) {
            /* GK in GK slot: use file price, sign-extended */
            d7val = (int8_t)pf->price;
        } else {
            /* Non-GK in GK slot: price = 0 */
            d7val = 0;
            goto storeResult;
        }
    } else if (playerIdx < 11) {
        /* Outfield player (index 1-10) */
        if (posType == 0) {
            /* GK in outfield slot: price = 0 */
            d7val = 0;
            goto storeResult;
        } else {
            /* Outfield player in outfield slot: calculate full price */
            d7val = GetPlayerPrice(&team->team, (PlayerFile *)playerBase, playerIdx);
        }
    } else {
        /* Substitute (index >= 11) */
        /* Both GK and outfield subs: use file price, sign-extended */
        d7val = (int8_t)pf->price;
    }

    /* Ensure minimum price of 1 */
    if (d7val == 0)
        d7val = 1;

    /* Computer-controlled teams skip injury/sold checks */
    if (team->team.teamControls != TEAM_COMPUTER) {
        /* Check if player is "sold" (bit 1 of positionAndFace) */
        if (pf->positionAndFace & PF_LOAN) {
            d7val = 1;  /* sold player = minimum price */
        }

        /* Check if player is injured.
         * Asm does exg a0,a1 so IsPlayerInjured gets a0=playerBase (team+slot*38) */
        if (IsPlayerInjured((PlayerFile *)playerBase, (int16_t)_inEditTactics)) {
            d7val = 0;  /* injured = price 0 */
        }
    }

storeResult:
    /* Store position value */
    {
        uint16_t posVal = (uint16_t)(int16_t)d7val;
        pf->cachedMatchValue = (int8_t)(uint8_t)posVal;
        _playerPositionsArray[d6] = (uint8_t)posVal;

        /* Get role from positionAndFace */
        uint16_t role = (pf->positionAndFace & PF_POSITION_MASK) >> 5;

        return ((uint32_t)role << 16) | (posVal & 0xFFFF);
    }
}
