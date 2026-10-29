/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_status.c — Player status decay, availability, team init, career finances,
 * chairman warnings, board confidence, reputation bonuses.
 *
 * Ported from SWOS2.s: CyclePlayerStatusFlagsForTeam, MaybeDecayPlayerStatusBucket,
 * MaybeDecayPlayerStatusBucketForTeam, InitInGameTeams_PlayerControlled,
 * InitInGameTeams_WithMatchParam, BuildCareerTeamUnavailablePlayerList,
 * ClearCareerMatchPlayerStatusBucket, ShouldApplyRandomPlayerStatusAfterSim,
 * IsPlayerInjured, GetNumberOfSubstitutes, GetNumberOfSubstitutesInSelectionList,
 * GetNumberOfAvailablePlayers, GetNumberOfAvailableSubstitutes,
 * GetNumberOfAvailableStartingPlayers, GetAgeAdjustedPlayerFormFactor,
 * ClampNewBalanceAndUpdateClubStatus, ClearOverdraftStateIfRecovered,
 * SetOverdraftWarningCooldown, SetPerformanceWarningCooldown,
 * IsMajorInternationalTournament, IsMajorTournamentAllowingRandomStatus,
 * IsMajorTournamentAllowingSquadChanges, IsTournamentAllowingSquadChangesIfNotCareer,
 * ShouldSetupPlayers, CalcLeagueMatchGateReceiptsFactor, CalcCupMatchGateReceiptsFactor,
 * UpdateCareerFinancesAfterMatch, MaybeSackManagerAfterSeasonReview,
 * UpdateCareerChairmanWarningsAfterMatch, UpdateCareerAfterMatch,
 * ApplyWeeklyFinancialDeductions, InitializeCareerSeasonFinancials,
 * GetBoardConfidenceScore, UpdateBoardConfidenceScore, PushBoardConfidenceHistory,
 * CalcBoardConfidenceDeltaPercent, UpdateBoardConfidenceModifiers,
 * UpdateCareerReputationBonuses, AddReputationFromBonusValueDiv4,
 * AddReputationFromBonusValueDiv8, InitializeStartingBalanceForNewSeason,
 * GetExpectedLeagueRankIndex.
 */

#include "career_status.h"
#include "league_phase.h"
#include "test_rules.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "career_teams.h"
#include "game_types.h"
#include "menu_system.h"
#include "auto_skip_debug.h"
#include "string_utils.h"
#include "job_offers.h"

/* ---- Assembly externs (functions still in asm) ---- */
extern uint16_t Rand(void);
extern uint16_t Rand2(void);
extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);
extern uint16_t GetAveragePlayerPrice(TeamFileEx *team);
#include "career_params.h"
extern uint16_t CalcGateReceiptsBaseFromTeamValue(uint16_t avgPrice);
extern uint16_t GetTeamValueBase(uint16_t avgPrice);

/* Replaces _CGetTeamValueForGateReceipts asm bridge */
static uint16_t getTeamValueForGateReceipts(TeamFileEx *team) {
    uint16_t teamNum = BE_READ_U16(team);  /* original: move.w (a0),d0 — reads first 2 bytes */
    VerifyTeamLoaded(teamNum);
    uint16_t avgPrice = GetAveragePlayerPrice(team);
    return CalcGateReceiptsBaseFromTeamValue(avgPrice);
}
/* FindTeamInDIY and GetMoreExpensiveTeamsCount declared in diy_competition.h. */
extern void ShowChairmanMessage(void);
extern void AddCareerTeamPlayer(void);
extern void CheckPlayersLeavingComing(void);
extern uint8_t *TeamSelectionKeyCache_GetCurrentListPtr(void);
extern TeamSelectionKeyCacheMatchSlot *TeamSelectionKeyCache_GetCurrentMatchSlot(void);

/* C-callable asm bridges for GetPlayer/GetPlayerAtIndex */
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern uint16_t _cGetPlayerAtIndexFileIdx;

/* Already-ported C functions */
extern uint16_t GetDisplayedPlayerValue(uint8_t *ptr);

/* ---- Data externs ---- */
extern TeamFileEx _careerTeam;
extern uint8_t    _currentMatchPlayers[32];
extern uint16_t   _careerNumPlayers;
extern TeamFileEx _currentTeam;
extern uint8_t    _playerPositions[32];
extern uint16_t   _numPlayers;
extern uint16_t   _trainingGame;
extern uint16_t   _gameType;
extern volatile uint16_t   _isNationalSelector;
extern uint8_t    _careerUnavailablePlayerList[16];
extern uint8_t    _careerUnavailablePlayerLookupTable[];
extern uint8_t    _playerFormModifiers[32];
extern uint16_t   _selTeamNumber;
extern uint16_t   _panicReasonCode;
extern void       PanicHaltLoop(void);

/* Financial globals */
extern int32_t    _newBalance;
extern int32_t    _gateReceipts;
extern int16_t    _gateReceiptsFactor;
extern int32_t    _seasonGateReceiptsAccum;
extern int32_t    _seasonGateReceiptsAccum2;
extern uint16_t   _seasonMatchReceiptsCount;
extern uint16_t   _seasonWeeksDeducted;
extern int32_t    _seasonRunningCostsAccum;
extern int32_t    _seasonRunningCostsAccum2;
extern int32_t    _seasonWagesAccum;
extern int32_t    _playerWagesBill;
extern int32_t    _generalRunningCosts;
extern int32_t    _weeklyPlayerWages;
extern int32_t    _weeklyRunningCosts;
extern int32_t    _totalTeamValue;
extern uint16_t   _teamValueDivisor;
extern uint16_t   _wageScalingDivisor;
extern int32_t    _weeklyWageByPlayerIndex[];
extern uint16_t   _avgTeamValueBases[];
extern int32_t    _old_balance;
extern int32_t    _startingBalance;
extern int32_t    _startingBalanceCopy;
extern int32_t    _competitionBonuses;

/* Warning/sacking state */
extern uint16_t   _overdraftStreak;
extern uint16_t   _overdraftWarningState;
extern uint16_t   _performanceWarningState;
extern uint16_t   _overdraftWarningCooldown;
extern uint16_t   _performanceWarningCooldown;
extern uint16_t   _clubBusinessActive;
extern volatile uint16_t   _youreSacked;
extern volatile uint16_t   _currentMessage;
extern uint16_t   _stayingInOldClub;
extern uint16_t   _careerTeamLostMatch;
extern uint16_t   _competitionSlotUpdatedFlag;
extern uint16_t   _removedOfferPlayersFromSelectedTeamCount;

/* Board confidence */
extern int16_t    _boardConfidenceScore;
extern uint16_t   _boardConfidenceHistory[4];     /* (newest first; read signed) */

/* Trophy/win flags */
extern uint16_t   _wonWorldCup;
extern uint16_t   _wonContinentalCup;
extern uint16_t   _wonInvitationTournament;
extern uint16_t   _playWinCupAnimation;
extern uint16_t   _playPromotedAnimation;
extern uint16_t   _majorTrophyBonusActive;
extern uint16_t   _cupBonus1Active;
extern uint16_t   _cupBonus2Active;
extern uint16_t   _minorTrophyBonusActive;
extern uint16_t   _leagueTitleBonusActive;

/* Competition pointers and contexts */
extern uint8_t *_currentCompetitionPtr;
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_quaternaryCompetitionPtr;
extern CompetitionSlotBuffer _competitionContextBuf;          /* slot-1 untangle (S4): typed buffer */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;   /* slot-2 untangle (S3): typed buffer */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf;   /* slot-3 untangle (S5): typed buffer */
extern CompetitionSlotBuffer _primaryCompetitionBuf;           /* slot-0 untangle (S6): typed buffer */
extern volatile uint16_t   _seasonUpdateInProgress;
extern uint16_t   _teamsLoaded;
extern uint16_t   _numSelectedTeams;

/* Competition copy symbols (addresses used for comparisons) */
extern uint8_t    _worldCup2Copy[];
extern uint8_t    _europeanChampionships2Copy[];
extern uint8_t    _copaAmericaCopy[];
extern uint8_t    _africanNationsCupCopy[];
extern uint8_t    _northAmericanCupCopy[];
extern uint8_t    _asianCupCopy[];
extern uint8_t    _oceaniaCupCopy[];
extern uint8_t    _invitationTournamentCopy[];
extern uint8_t    _tourCopy[];
extern uint8_t    _friendlyCopy[];
extern uint8_t    _worldCupQualifyingGroup1Copy[];
extern uint8_t    _worldCupQualifyingGroup8Copy[];
extern uint8_t    _ecQualifyingGroup1Copy[];
extern uint8_t    _ecQualifyingGroup8Copy[];
extern uint8_t    _northAmericaWorldCupQualificationCopy[];
extern uint8_t    _southAmericaWorldCupQualificationCopy[];
extern uint8_t    _africaWorldCupQualificationCopy[];
extern uint8_t    _asiaWorldCupQualificationCopy[];
extern uint8_t    _oceaniaWorldCupQualificationCopy[];

/* Season/career state */
extern uint16_t   _currentDivisionIndex;
extern uint16_t   _seasonReview;
extern uint16_t   _seasonFinished;
/* secondaryCompFinished migrated to competitionContextBuf.tourn.header.compFinished (slot-1 untangle, S4) */
/* tertiaryCompFinished migrated to tertiaryCompetitionContextBuf.tourn.header.compFinished (slot-2 untangle, S3) */
/* leaguePromotionCount migrated to primaryCompetitionBuf.tourn.header.promotionCount (slot-0 untangle, S6) */
/* leagueRelegationCount migrated to primaryCompetitionBuf.tourn.header.relegationCount (slot-0 untangle, S6) */
/* leagueWarningsEnabled migrated to primaryCompetitionBuf.tourn.leagueWarningsEnabled (slot-0 untangle, S6) */
extern uint16_t   _expectedLeagueRankIndex;
extern uint16_t   _diyFoundTeamIndex;
extern uint16_t   _diyCompHasGroupStage;
/* numDiyTeams migrated to primaryCompetitionBuf.tourn.header.numTeams (slot-0 untangle, S6) */
extern uint16_t   _isCareer;
extern int8_t     _careerJobOfferPhaseBias;
extern uint8_t    _viewRandomPhase;
extern uint8_t    _someTeamFlags;
extern uint16_t   _worldCupQualifyingTeam;
extern uint16_t   _worldCupHostTeam;
extern uint16_t   _euroChampionshipQualTeam;
extern uint16_t   _ballSpeed;
extern volatile uint16_t   _currentGameTick;
extern uint8_t    _lastFinancialWeekIndex;
extern DIYTournamentState _diyFileBufferCopy;

/* Winner teams */
/* primaryCompWinnerTeam migrated to primaryCompetitionBuf.tourn.header.winnerTeam (slot-0 untangle, S6) */
/* secondaryCompWinnerTeam migrated to competitionContextBuf.tourn.header.winnerTeam (slot-1 untangle, S4) */
/* tertiaryCompWinnerTeam migrated to tertiaryCompetitionContextBuf.tourn.header.winnerTeam (slot-2 untangle, S3) */
/* quaternaryCompWinnerTeam migrated to euroCupsCompetitionContextBuf.tourn.header.winnerTeam (slot-3 untangle, S5) */
extern uint16_t   _euroCupSlotIndex;

/* Qualifying flags */
extern uint16_t   _qualifyingStage1PendingFlag0;
extern uint16_t   _qualifyingStage1PendingFlag1;
extern uint16_t   _qualifyingStage2PendingFlag0;
extern uint16_t   _qualifyingStage2PendingFlag1;

/* Reputation */
extern int16_t    _nationalSelectorReputationPoints;
extern int16_t    _careerReputationPoints;
extern uint16_t   _winCupBonusCount;
extern uint16_t   _minorTrophyBonusCount;
extern uint16_t   _majorTrophyBonusCount;
/* majorTrophyBonusValue migrated to primaryCompetitionBuf.tourn.header.trophyBonusValue (slot-0 untangle, S6) */
/* minorTrophyBonusValue migrated to competitionContextBuf.tourn.header.trophyBonusValue (slot-1 untangle, S4) */
/* leagueTitleBonusValue migrated to tertiaryCompetitionContextBuf.tourn.header.trophyBonusValue (slot-2 untangle, S3) */
/* majorTrophyOrCupBonusValue migrated to euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue (slot-3 untangle, S5) */

/* DIY competition buffer — kept as uint8_t[] to avoid size warnings */
extern DIYCompetitionEx _diyCompetitionStart;

/* DIY competition struct cast for diyFileBufferCopy / diyCompetitionStart access */

#define GAME_TYPE_CAREER 4
#define GAME_TYPE_SEASON 3

/* cardsInjuries bit masks */
#define CI_CARD_BITS         0x03   /* bits 0-1: card severity */
#define CI_INJURY_BITS       0x1C   /* bits 2-4: injury severity */
#define CI_STATUS_BUCKET     0xE0   /* bits 5-7: status bucket */

/* PF_POSITION_MASK / PF_POSITION_SHIFT now defined in game_types.h */

/* ================================================================
 * Helper: get the player index list pointer for a given team.
 * Returns the appropriate array (currentMatchPlayers, _playerPositions,
 * or team->team.playerNumbers) and sets *outCount to the player count.
 * ================================================================ */
static uint8_t *getPlayerList(TeamFileEx *team, uint16_t *outCount) {
    if (team == &_careerTeam) {
        *outCount = _careerNumPlayers;
        return _currentMatchPlayers;
    }
    if (team == &_currentTeam) {
        *outCount = _numPlayers;
        return _playerPositions;
    }
    *outCount = 16;
    return team->team.playerNumbers;
}

/* ================================================================
 * MaybeDecayPlayerStatusBucket (static)
 * Probabilistically decays the status bucket field (bits 5-7) of a
 * player's cardsInjuries byte.
 *
 * Input: d1 = current bucket bits (already masked to 0xE0), a1 = player record
 * The pointer a1 is team_base + playerIndex * 38 (shifted).
 * ================================================================ */
static void maybeDecayPlayerStatusBucket(uint16_t bucketBits, PlayerFile *playerFile) {
    uint16_t d1 = bucketBits;

    if (d1 == 0x20) {
        /* Bucket level 1: 50% chance to decay to 0 */
        uint16_t r = Rand2();
        if (r & 1)
            return;  /* no decay */
        d1 = 0;
    } else if (d1 == 0xC0) {
        /* Bucket level 6: 1/8 chance to decay to random level 1-5 */
        uint16_t r = Rand2();
        if ((r & 7) != 0)
            return;  /* no decay */
        r = Rand2();
        uint16_t newLevel = (uint16_t)((r * 5) >> 8);
        d1 = (newLevel << 5) + 0x20;
    } else if (d1 == 0xE0) {
        /* Bucket level 7: never decays */
        return;
    } else {
        /* Other levels: decrement by one step */
        d1 -= 0x20;
        if (d1 == 0x20) {
            /* Reached level 1 after decrement: 50% chance to clear to 0 */
            uint16_t r = Rand2();
            if (r & 1) {
                /* keep at 1, write it */
            } else {
                d1 = 0;
            }
        }
    }

    /* Write back the new bucket value */
    uint8_t ci = playerFile->cardsInjuries;
    ci = (ci & 0x1F) | (uint8_t)d1;
    playerFile->cardsInjuries = ci;
}

/* ================================================================
 * MaybeDecayPlayerStatusBucketForTeam (static)
 * Iterates all players in a team and calls MaybeDecayPlayerStatusBucket
 * for those with nonzero status bucket bits.
 * ================================================================ */
static void maybeDecayPlayerStatusBucketForTeam(TeamFileEx *team) {
    /* Skip computer-controlled teams */
    if (team->team.teamControls == 1)
        return;

    if (team == &_careerTeam) {
        /* Career team: use currentMatchPlayers for indirection */
        int16_t count = (int16_t)_careerNumPlayers - 1;
        for (int16_t i = count; i >= 0; i--) {
            int16_t idx = (int8_t)_currentMatchPlayers[i];
            PlayerFile *pf = &team->players[idx];
            uint16_t bucket = (uint16_t)pf->cardsInjuries & CI_STATUS_BUCKET;
            if (bucket != 0) {
                maybeDecayPlayerStatusBucket(bucket, pf);
            }
        }
    } else {
        /* Non-career: iterate 16 players sequentially */
        PlayerFile *pf = team->players;
        for (int16_t i = 15; i >= 0; i--) {
            uint16_t bucket = (uint16_t)pf->cardsInjuries & CI_STATUS_BUCKET;
            if (bucket != 0) {
                maybeDecayPlayerStatusBucket(bucket, pf);
            }
            pf++;
        }
    }
}

/* ================================================================
 * CyclePlayerStatusFlagsForTeam (static)
 * Decays injury severity (mode==0) or card severity (mode!=0) for
 * all players in a team.
 * ================================================================ */
static void cyclePlayerStatusFlagsForTeam(TeamFileEx *team, uint16_t mode) {
    /* Skip if training game or computer-controlled team */
    if (_trainingGame != 0)
        return;
    if (team->team.teamControls == 1)
        return;

    uint8_t *playerList;
    int16_t count;

    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
        count = (int16_t)_careerNumPlayers - 1;
    } else {
        playerList = team->team.playerNumbers;
        count = 15;
    }

    for (int16_t i = count; i >= 0; i--) {
        int16_t idx = (int8_t)playerList[i];
        PlayerFile *pf = &team->players[idx];
        uint8_t ci = pf->cardsInjuries;

        if (mode == 0) {
            /* Decay injury bits (2-4) */
            uint16_t injury = (uint16_t)ci & CI_INJURY_BITS;
            if (injury == 0)
                continue;

            uint16_t newInjury;
            if (injury == 0x10)
                newInjury = 0x0C;       /* severity 4 → 3 */
            else if (injury == 0x0C)
                newInjury = 0x08;       /* severity 3 → 2 */
            else if (injury == 0x08)
                newInjury = 0x00;       /* severity 2 → 0 (healed) */
            else
                continue;               /* other values: skip */

            ci = (ci & 0xE3) | (uint8_t)newInjury;
            pf->cardsInjuries = ci;
        } else {
            /* Decay card bits (0-1) */
            uint16_t card = (uint16_t)ci & CI_CARD_BITS;
            if (card == 0)
                continue;

            uint16_t newCard;
            if (card == 2)
                newCard = 1;            /* card level 2 → 1 */
            else
                newCard = 0;            /* card level 1 or 3 → 0 */

            ci = (ci & 0xFC) | (uint8_t)newCard;
            pf->cardsInjuries = ci;
        }
    }
}

/* ================================================================
 * InitInGameTeams_PlayerControlled
 * Decays status buckets and cycles injury/card flags for both teams.
 * mode parameter is passed to CyclePlayerStatusFlagsForTeam.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitInGameTeams_PlayerControlled(TeamFileEx *team1, TeamFileEx *team2, uint16_t mode) {
    maybeDecayPlayerStatusBucketForTeam(team1);
    maybeDecayPlayerStatusBucketForTeam(team2);
    cyclePlayerStatusFlagsForTeam(team1, mode);
    cyclePlayerStatusFlagsForTeam(team2, mode);
}

/* ================================================================
 * InitInGameTeams_WithMatchParam / InitInGameTeams_WithMatchParam_Alt
 * Same as PlayerControlled but loads mode from diyCompetitionStart->substitutesCount.
 * The Alt variant is an identical copy in the original game.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitInGameTeams_WithMatchParam(TeamFileEx *team1, TeamFileEx *team2) {
    uint16_t mode = _diyCompetitionStart.base.substitutesCount;
    maybeDecayPlayerStatusBucketForTeam(team1);
    maybeDecayPlayerStatusBucketForTeam(team2);
    cyclePlayerStatusFlagsForTeam(team1, mode);
    cyclePlayerStatusFlagsForTeam(team2, mode);
}

/* ================================================================
 * ShouldApplyRandomPlayerStatusAfterSim
 * Returns 0 if in career mode with national selector and not a major
 * tournament. Returns 1 otherwise.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t ShouldApplyRandomPlayerStatusAfterSim(void) {
    if (_gameType == GAME_TYPE_CAREER) {
        if (_isNationalSelector != 0) {
            uint16_t isMajor = IsMajorTournamentAllowingRandomStatus();
            if (isMajor == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================
 * BuildCareerTeamUnavailablePlayerList
 * Builds a list (max 4) of career team players who are injured.
 * ================================================================ */
__attribute__((used, externally_visible))
void BuildCareerTeamUnavailablePlayerList(void) {
    uint16_t *list = (uint16_t *)_careerUnavailablePlayerList;
    /* Init 4 entries to -1 (each is 4 bytes = 2 words) */
    list[0] = 0xFFFF;
    list[2] = 0xFFFF;
    list[4] = 0xFFFF;
    list[6] = 0xFFFF;

    if (ShouldApplyRandomPlayerStatusAfterSim() != 0)
        return;

    uint16_t slotsLeft = 4;
    TeamFileEx *teamEx = &_careerTeam;
    int16_t count = (int16_t)_careerNumPlayers - 1;
    uint8_t *outPtr = _careerUnavailablePlayerList;

    for (int16_t i = count; i >= 0; i--) {
        int16_t idx = (int8_t)_currentMatchPlayers[i];
        PlayerFile *pf = &teamEx->players[idx];
        uint16_t injury = (uint16_t)pf->cardsInjuries & CI_INJURY_BITS;
        if (injury == 0)
            continue;

        /* Copy 4-byte entry from lookup table */
        uint16_t tableOff = (uint16_t)i * 4;
        uint8_t *src = _careerUnavailablePlayerLookupTable + tableOff;
        outPtr[0] = src[0];
        outPtr[1] = src[1];
        outPtr[2] = src[2];
        outPtr[3] = src[3];
        outPtr += 4;

        slotsLeft--;
        if (slotsLeft == 0)
            return;
    }
}

/* ================================================================
 * ClearCareerMatchPlayerStatusBucket
 * Clears status bucket bits (5-7) for all career team players.
 * ================================================================ */
__attribute__((used, externally_visible))
void ClearCareerMatchPlayerStatusBucket(void) {
    TeamFileEx *teamEx = &_careerTeam;
    int16_t count = (int16_t)_careerNumPlayers - 1;

    for (int16_t i = count; i >= 0; i--) {
        int16_t idx = (int8_t)_currentMatchPlayers[i];
        PlayerFile *pf = &teamEx->players[idx];
        uint8_t ci = pf->cardsInjuries;
        ci &= 0x1F;  /* clear bits 5-7 */
        pf->cardsInjuries = ci;
    }
}

/* ================================================================
 * IsPlayerInjured
 * playerBase = team + playerIndex * 38 (shifted pointer).
 * Checks injury (bits 2-4), cards (bits 0-1), and status bucket
 * (bits 5-7) based on mode:
 *   mode -1: check both injuries and cards
 *   mode  0: check injuries only
 *   mode  1: check cards only
 * Returns 1 if injured, 0 if not.
 * ================================================================ */
/* Core injury/suspension status from the persisted cardsInjuries data —
 * the actual asm logic. IsPlayerInjured()/IsPlayerInjuredForDisplay() below
 * are thin wrappers (only difference: the beta no-injuries override). */
static uint16_t player_injury_status(PlayerFile *playerFile, int16_t mode) {
    PlayerFile *pf = &playerFile[2];
    uint8_t ci = pf->cardsInjuries;

    if (_trainingGame != 0)
        goto check_bucket;

    if (mode != 1) {
        /* Check injury bits (2-4) */
        uint16_t injury = (uint16_t)ci & CI_INJURY_BITS;
        if (injury != 0 && injury != 4)
            return 1;  /* injured (severity > 1) */

        if (mode >= 0)
            goto check_bucket;
    }

    /* Check card bits (0-1) */
    {
        uint16_t card = (uint16_t)ci & CI_CARD_BITS;
        if (card != 0)
            return 1;  /* has card suspension */
    }

check_bucket:
    /* Check status bucket bits (5-7) */
    {
        uint16_t bucket = (uint16_t)ci & CI_STATUS_BUCKET;
        if (bucket == 0 || bucket == 0x20)
            return 0;  /* no bucket or minor (0x20) → not injured */
        return 1;      /* serious bucket → injured */
    }
}

__attribute__((used, externally_visible))
uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode) {
    if (TestRules_NoInjuriesCards()) {
    /* The testers' rule (test_rules.h): nobody is ever unavailable for SELECTION/GAMEPLAY,
     * regardless of what's persisted in cardsInjuries. Catches any injury/
     * card/bucket source the per-write-site gates may have missed (loaded
     * saves with pre-existing injury bits, edit-tactics writes, etc.).
     * NOTE: this is a GAMEPLAY-availability override only — it must NOT
     * suppress the squad-screen DISPLAY (the red suspended/injured name), or
     * the name stops matching the drawn "3"/injury indicator. DISPLAY callers
     * use IsPlayerInjuredForDisplay() which keeps the honest status. */
        return 0;
    }
    return player_injury_status(playerFile, mode);
}

/* DISPLAY-only status query: reports the REAL persisted injury/suspension even
 * under the testers' rule (test_rules.h), so the edit-tactics squad screen colours a
 * suspended/injured player's name red to match its drawn indicator. On non-beta
 * builds this is identical to IsPlayerInjured (both just call the core). */
__attribute__((used, externally_visible))
uint16_t IsPlayerInjuredForDisplay(PlayerFile *playerFile, int16_t mode) {
    return player_injury_status(playerFile, mode);
}

/* ================================================================
 * GetNumberOfSubstitutes
 * Counts players with position == 0 (goalkeeper) in the team.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetNumberOfSubstitutes(TeamFileEx *team, uint16_t numPl) {
    int16_t iter = (int16_t)numPl - 1;
    uint8_t *playerList;
    uint16_t dummy;

    /* Three-way dispatch for player list source */
    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
    } else if (team == &_currentTeam) {
        playerList = _playerPositions;
    } else {
        playerList = team->team.playerNumbers;
    }

    uint16_t count = 0;
    for (; iter >= 0; iter--) {
        int16_t idx = (int8_t)playerList[iter];
        PlayerFile *pf = &team->players[idx];
        uint16_t position = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT;
        if (position == 0)
            count++;
    }
    return count;
}

/* ================================================================
 * GetNumberOfSubstitutesInSelectionList
 * Counts goalkeepers in selection list (indices 11 to extraCount+10).
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetNumberOfSubstitutesInSelectionList(TeamFileEx *team, uint16_t extraCount) {
    int16_t iter = (int16_t)extraCount + 10;
    uint8_t *playerList;

    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
    } else if (team == &_currentTeam) {
        playerList = _playerPositions;
    } else {
        playerList = team->team.playerNumbers;
    }

    uint16_t count = 0;
    for (; iter >= 11; iter--) {
        int16_t idx = (int8_t)playerList[iter];
        PlayerFile *pf = &team->players[idx];
        uint16_t position = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT;
        if (position == 0)
            count++;
    }
    return count;
}

/* ================================================================
 * Common helper for availability checking.
 * Checks if a player is disabled based on injury/card/bucket bits.
 * Returns 1 if disabled, 0 if available.
 *
 * Shared core of GetNumberOfAvailablePlayers / GetNumberOfAvailableSubstitutes
 * / GetNumberOfAvailableStartingPlayers — i.e. SQUAD-LEGALITY, which is what
 * ShowOrHidePlayMatchButton gates the PLAY MATCH / VIEW RESULT button on
 * (SWOS2.s loc_14872A: GetNumberOfAvailablePlayers(d1=11); tst.w d6; bne →
 * loc_14876E hides the button). An injured starter therefore blocks the match
 * until he is substituted — correct, original SWOS behaviour.
 *
 * That makes this a GAMEPLAY-availability query, so it must honour the same
 * testers' rule (test_rules.h) override as IsPlayerInjured(). Without it the
 * beta flag only half-applied: IsPlayerInjured said "nobody is unavailable"
 * while these counters still read cardsInjuries directly, so a tester with a
 * pre-existing injury in a loaded save got NO start button at all and was
 * forced to substitute — exactly what the flag exists to prevent.
 * The squad screen still shows the red name / "3" indicator, since those are
 * DISPLAY queries and route through IsPlayerInjuredForDisplay().
 * ================================================================ */
static uint16_t isPlayerDisabled(PlayerFile *pf, int16_t excludeCardsFlag) {
    if (TestRules_NoInjuriesCards())
        return 0;
    uint8_t ci = pf->cardsInjuries;

    /* Check status bucket bits (5-7) */
    uint16_t bucket = (uint16_t)ci & CI_STATUS_BUCKET;
    if (bucket != 0 && bucket != 0x20)
        return 1;  /* serious bucket → disabled */

    if (_trainingGame != 0)
        return 0;  /* training games ignore injuries/cards */

    if (excludeCardsFlag != 1) {
        /* Check injury severity bits (2-4) */
        uint16_t injury = (uint16_t)ci & CI_INJURY_BITS;
        if (injury != 0 && injury != 4)
            return 1;  /* injured (severity > 1) */

        if (excludeCardsFlag >= 0)
            return 0;  /* mode 0: skip card check */
    }

    /* Check card bits (0-1) */
    uint16_t card = (uint16_t)ci & CI_CARD_BITS;
    if (card != 0)
        return 1;  /* has card → disabled */

    return 0;
}

/* ================================================================
 * GetNumberOfAvailablePlayers
 * Returns (disabled << 16) | (available & 0xFFFF).
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t GetNumberOfAvailablePlayers(TeamFileEx *team, int16_t excludeCardsFlag, uint16_t numPl) {
    uint8_t *playerList;
    uint16_t totalCount;

    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
        totalCount = _careerNumPlayers;
    } else if (team == &_currentTeam) {
        playerList = _playerPositions;
        totalCount = _numPlayers;
    } else {
        playerList = team->team.playerNumbers;
        totalCount = 16;
    }

    if (numPl != 0)
        totalCount = numPl;

    uint16_t disabled = 0;
    uint16_t available = totalCount;
    int16_t iter = (int16_t)totalCount - 1;

    for (; iter >= 0; iter--) {
        int16_t idx = (int8_t)playerList[iter];
        PlayerFile *pf = &team->players[idx];
        if (isPlayerDisabled(pf, excludeCardsFlag)) {
            disabled++;
            available--;
        }
    }

    return ((uint32_t)disabled << 16) | (uint32_t)available;
}

/* ================================================================
 * GetNumberOfAvailableSubstitutes
 * Counts available goalkeeper-position players.
 * Returns (disabled << 16) | (available & 0xFFFF).
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t GetNumberOfAvailableSubstitutes(TeamFileEx *team, int16_t flag) {
    uint8_t *playerList;
    uint16_t totalCount;

    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
        totalCount = _careerNumPlayers;
    } else if (team == &_currentTeam) {
        playerList = _playerPositions;
        totalCount = _numPlayers;
    } else {
        playerList = team->team.playerNumbers;
        totalCount = 16;
    }

    uint16_t disabled = 0;
    uint16_t goalkeepers = 0;
    int16_t iter = (int16_t)totalCount - 1;

    for (; iter >= 0; iter--) {
        int16_t idx = (int8_t)playerList[iter];
        PlayerFile *pf = &team->players[idx];

        /* Check if goalkeeper (position == 0) */
        uint16_t position = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT;
        if (position != 0)
            continue;

        goalkeepers++;
        if (isPlayerDisabled(pf, flag)) {
            disabled++;
            goalkeepers--;
        }
    }

    return ((uint32_t)disabled << 16) | (uint32_t)goalkeepers;
}

/* ================================================================
 * GetNumberOfAvailableStartingPlayers
 * Counts available non-goalkeeper players. Uses career path only
 * (does not check currentTeam).
 * Returns (disabled << 16) | (available & 0xFFFF).
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t GetNumberOfAvailableStartingPlayers(TeamFileEx *team, int16_t flag) {
    uint8_t *playerList;
    uint16_t totalCount;

    if (team == &_careerTeam) {
        playerList = _currentMatchPlayers;
        totalCount = _careerNumPlayers;
    } else {
        playerList = team->team.playerNumbers;
        totalCount = 16;
    }

    uint16_t disabled = 0;
    uint16_t starters = 0;
    int16_t iter = (int16_t)totalCount - 1;

    for (; iter >= 0; iter--) {
        int16_t idx = (int8_t)playerList[iter];
        PlayerFile *pf = &team->players[idx];

        /* Check if NOT goalkeeper (position != 0) */
        uint16_t position = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT;
        if (position == 0)
            continue;

        starters++;
        if (isPlayerDisabled(pf, flag)) {
            disabled++;
            starters--;
        }
    }

    return ((uint32_t)disabled << 16) | (uint32_t)starters;
}

/* ================================================================
 * GetAgeAdjustedPlayerFormFactor
 * playerBase = team + playerIndex * 38 (shifted pointer).
 * Reads age (price field at +0x6C), form (someFlag at +0x71),
 * position (positionAndFace at +0x66).
 * Returns form / ageDivisor.
 * ================================================================ */

static const uint8_t kPlayerAgeToFormDivisorTable[50] = {
    0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,
    0x04,0x04,0x05,0x05,0x05,0x05,0x05,0x05,0x06,0x06,
    0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x08,
    0x08,0x08,0x08,0x08,0x08,0x09,0x09,0x09,0x0A,0x0A,
    0x0C,0x0C,0x0C,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F
};

__attribute__((used, externally_visible))
int16_t GetAgeAdjustedPlayerFormFactor(uint8_t *playerBase) {
    PlayerFile *pf = (PlayerFile *)(playerBase + 76);
    int8_t age = pf->price;
    int16_t ageDivisor = (int16_t)(uint8_t)kPlayerAgeToFormDivisorTable[(uint8_t)age];

    int16_t formValue;
    uint8_t flagByte = (uint8_t)pf->form;

    if (flagByte == PLAYER_FORM_TRIALIST || flagByte == PLAYER_FORM_RESERVE) {
        /* Special career form: compute player index within careerTeam */
        uint32_t offset = (uintptr_t)((uintptr_t)playerBase - (uintptr_t)&_careerTeam);
        uint16_t playerIdx = offset / 38;
        formValue = (int16_t)(int8_t)_playerFormModifiers[playerIdx];
        formValue <<= 1;  /* * 2 */
    } else {
        formValue = (int16_t)(int8_t)flagByte;
        /* If bit 0 of positionAndFace is set, double the form */
        if (pf->positionAndFace & PF_INFORM) {
            formValue <<= 1;
        }
    }

    /* divs.w d1,d0: signed 32÷16 division */
    int32_t num = (int32_t)formValue;
    int16_t result = (int16_t)(num / ageDivisor);
    return result;
}

/* ================================================================
 * Chairman message constants
 * ================================================================ */
#define CHAIRMAN_BANK_BALANCE_OVERDRAFT      5
#define CHAIRMAN_OVERDRAFT_LAST_WARNING      6
#define CHAIRMAN_CLEAR_OVERDRAFT_IMMEDIATELY 7
#define CHAIRMAN_YOURE_SACKED                8
#define CHAIRMAN_IMPROVE_LEAGUE_POSITION     9
#define CHAIRMAN_IMPROVE_LEAGUE_POSITION_SOON 10
#define CHAIRMAN_THREE_MORE_MATCHES          11
#define CHAIRMAN_YOURE_SACKED_2              12
#define CHAIRMAN_WE_DIDNT_RENEW_YOUR_CONTRACT 38

/* ================================================================
 * Data tables (moved from asm to static const in C)
 * ================================================================ */
static const uint16_t leagueMatchReceiptsMultiplierTable[6] = {
    400, 300, 200, 150, 125, 100
};

static const uint16_t cupMatchReceiptsMultiplierTable[6] = {
    800, 600, 400, 300, 250, 200
};

static const int16_t seasonReviewConfidenceDeltaTable[5] = {
    50, 25, 0, -25, -50
};

static const int16_t nationalSelectorSeasonReviewReputationDeltaTable[5] = {
    5, 2, 0, -2, -5
};

/* ================================================================
 * Group A: Simple utility functions
 * ================================================================ */

/* ClampNewBalanceAndUpdateClubStatus
 * Clamp balance to 100M, set clubBusinessActive via smi, manage overdraftStreak.
 */
__attribute__((used, externally_visible))
void ClampNewBalanceAndUpdateClubStatus(void) {
    if (_newBalance > 100000000)
        _newBalance = 100000000;
    /* smi: set byte to -1 if negative, 0 if positive */
    _clubBusinessActive = (_newBalance < 0) ? -1 : 0;
    if (_newBalance < 0)
        _overdraftStreak++;
    else
        _overdraftStreak = 0;
}

/* ClearOverdraftStateIfRecovered */
__attribute__((used, externally_visible))
void ClearOverdraftStateIfRecovered(void) {
    if (_overdraftWarningState != 0) {
        if (_newBalance >= 0)
            _overdraftWarningState = 0;
    }
}

/* SetOverdraftWarningCooldown — random 4-8 */
static void setOverdraftWarningCooldown(void) {
    uint16_t r = Rand2();
    uint16_t val = (uint16_t)((r * 5) >> 8) + 4;
    _overdraftWarningCooldown = val;
}

/* SetPerformanceWarningCooldown — random 4-8 */
static void setPerformanceWarningCooldown(void) {
    uint16_t r = Rand2();
    uint16_t val = (uint16_t)((r * 5) >> 8) + 4;
    _performanceWarningCooldown = val;
}

/* ================================================================
 * Group B: Tournament check functions
 * ================================================================ */

/* IsMajorInternationalTournament
 * Returns 1 if current/secondary competition is a major international tournament.
 * Sets *outCtx to the appropriate competition context pointer.
 */
__attribute__((used, externally_visible))
uint16_t IsMajorInternationalTournament(uint8_t **outCtx) {
    /* Slot pointers may be NULL (empty slot); 0xFF matches no COMPID, so a NULL
     * slot falls through exactly as the original pointer-compare did. */
    uint8_t curId = _currentCompetitionPtr ? comp_id((void *)_currentCompetitionPtr) : 0xFFu;

    /* Check primary competition: World Cup + 6 continental cups + invitation + tour */
    if (curId == COMPID_worldCup2 ||
        comp_is_continental_cup(curId) ||
        curId == COMPID_invitationTournament ||
        curId == COMPID_tour) {
        *outCtx = (uint8_t *)&_primaryCompetitionBuf;
        return 1;
    }

    /* Check secondary competition: same set minus World Cup */
    uint8_t secId = _secondaryCompetitionPtr ? comp_id((void *)_secondaryCompetitionPtr) : 0xFFu;
    if (comp_is_continental_cup(secId) ||
        secId == COMPID_invitationTournament ||
        secId == COMPID_tour) {
        *outCtx = (uint8_t *)&_competitionContextBuf;
        return 1;
    }

    return 0;
}

/* IsMajorTournamentAllowingRandomStatus
 * Wraps IsMajorInternationalTournament, checks offset 0x44.
 * Returns nonzero if major tournament allows random status.
 */
__attribute__((used, externally_visible))
uint16_t IsMajorTournamentAllowingRandomStatus(void) {
    uint8_t *ctx;
    uint16_t isMajor = IsMajorInternationalTournament(&ctx);
    if (isMajor == 0)
        return 0;
    /* +0x44 = matchStarted in DIYCompetition base */
    return (uint16_t)((DIYCompetition *)ctx)->matchStarted;
}

/* IsMajorTournamentAllowingSquadChanges
 * Wraps IsMajorInternationalTournament, checks offset 0x46.
 */
__attribute__((used, externally_visible))
uint16_t IsMajorTournamentAllowingSquadChanges(void) {
    uint8_t *ctx;
    uint16_t isMajor = IsMajorInternationalTournament(&ctx);
    if (isMajor == 0)
        return 0;
    /* +0x46 = careerTeamInvolved in DIYCompetition base */
    return (uint16_t)((DIYCompetition *)ctx)->careerTeamInvolved;
}

/* IsTournamentAllowingSquadChangesIfNotCareer */
__attribute__((used, externally_visible))
uint16_t IsTournamentAllowingSquadChangesIfNotCareer(void) {
    if (_isCareer != 0)
        return 0;
    return IsMajorTournamentAllowingSquadChanges();
}

/* ShouldSetupPlayers */
__attribute__((used, externally_visible))
uint16_t ShouldSetupPlayers(void) {
    if (_gameType == GAME_TYPE_CAREER) {
        if (_isNationalSelector != 0) {
            uint16_t allows = IsMajorTournamentAllowingSquadChanges();
            if (allows == 0)
                return 0;
        }
    }
    return 1;
}

/* ================================================================
 * Group C: Gate receipt calculations
 * ================================================================ */

/* CalcLeagueMatchGateReceiptsFactor (static)
 * Computes gate receipts factor for league matches from team values.
 * Returns factor in d7. Panics on 16-bit overflow.
 */
static uint16_t calcLeagueMatchGateReceiptsFactor(TeamFileEx *team1, TeamFileEx *team2) {
    uint32_t d0;

    /* team1 value */
    if (team1 == &_careerTeam) {
        d0 = (uint32_t)_totalTeamValue;
    } else {
        d0 = getTeamValueForGateReceipts(team1);
    }
    d0 = (uint16_t)(d0 / _careerParams.gate_value_divisor);
    d0 = (uint32_t)(uint16_t)d0 * _careerParams.gate_league_mult_home;
    uint32_t d7 = d0;

    /* team2 value */
    if (team2 == &_careerTeam) {
        d0 = (uint32_t)_totalTeamValue;
    } else {
        d0 = getTeamValueForGateReceipts(team2);
    }
    d0 = (uint16_t)(d0 / _careerParams.gate_value_divisor);
    d0 = (uint32_t)(uint16_t)d0 * _careerParams.gate_league_mult_away;
    d7 += d0;

    d7 >>= 4;   /* lsr.l #4 */

    /* divu.w teamValueDivisor — check for 16-bit overflow */
    if (_teamValueDivisor == 0 || d7 > 0xFFFFFFFFu) {
        /* bvs → panic */
        _panicReasonCode = 0x9232;
        PanicHaltLoop();
        return 0; /* unreachable */
    }
    uint32_t quotient = d7 / (uint32_t)_teamValueDivisor;
    uint16_t remainder = (uint16_t)(d7 % (uint32_t)_teamValueDivisor);
    /* bvs checks if quotient > 0xFFFF (overflow in 16-bit result) */
    if (quotient > 0xFFFF) {
        _panicReasonCode = 0x9232;
        PanicHaltLoop();
        return 0;
    }
    return (uint16_t)quotient;
}

/* CalcCupMatchGateReceiptsFactor (static)
 * Same as league but with different multipliers.
 */
static uint16_t calcCupMatchGateReceiptsFactor(TeamFileEx *team1, TeamFileEx *team2) {
    uint32_t d0;

    if (team1 == &_careerTeam) {
        d0 = (uint32_t)_totalTeamValue;
    } else {
        d0 = getTeamValueForGateReceipts(team1);
    }
    d0 = (uint16_t)(d0 / _careerParams.gate_value_divisor);
    d0 = (uint32_t)(uint16_t)d0 * _careerParams.gate_cup_mult;
    uint32_t d7 = d0;

    if (team2 == &_careerTeam) {
        d0 = (uint32_t)_totalTeamValue;
    } else {
        d0 = getTeamValueForGateReceipts(team2);
    }
    d0 = (uint16_t)(d0 / _careerParams.gate_value_divisor);
    d0 = (uint32_t)(uint16_t)d0 * _careerParams.gate_cup_mult;
    d7 += d0;

    d7 >>= 4;

    if (_teamValueDivisor == 0 || d7 > 0xFFFFFFFFu) {
        _panicReasonCode = 0x9232;
        PanicHaltLoop();
        return 0;
    }
    uint32_t quotient = d7 / (uint32_t)_teamValueDivisor;
    if (quotient > 0xFFFF) {
        _panicReasonCode = 0x9232;
        PanicHaltLoop();
        return 0;
    }
    return (uint16_t)quotient;
}

/* UpdateCareerFinancesAfterMatch
 * Complex dispatcher that calculates gate receipts based on competition type.
 */
__attribute__((used, externally_visible))
void UpdateCareerFinancesAfterMatch(DIYCompetitionEx *comp, TeamFileEx *team, TeamFileEx *opponent) {
    if (_isNationalSelector != 0)
        return;

    /* The 0x2C word is OVERLOADED: it's named compSlot for its team-selection
     * cache-key role, but in THIS match-finance path it carries the competition
     * TYPE discriminator (0=league, 1/2/4=cup, 3=DIY) — hence reading compSlot
     * into a var called compType. (A future rename pass may give 0x2C a name
     * that captures both roles, or reconcile it with competitionType@0x00.)
     *
     * Read the whole (compSlot, slotId) word: *(uint16_t *) round-trips the
     * value written via *(uint16_t *)&compSlot = slot on both endians.
     * PACK_BE_U16(compSlot, slotId) is wrong on SDL LE because the byte layout
     * is reversed (slot lands in the compSlot byte, not slotId) — that produced
     * compType = slot << 8 and dispatched every match to the panic branch. */
    int16_t compType = (int16_t)*(uint16_t *)&comp->base.compSlot;
    uint32_t d7;

    if (compType == 0) {
        goto league_match;
    } else if (compType == 4) {
        goto cup_match_type4;
    } else if (compType == 1 || compType == 2) {
        goto cup_match_type1or2;
    } else if (compType == 3) {
        goto diy_comp_match;
    } else {
        /* Report the unexpected compType value */
        _panicReasonCode = 0x9200 | (compType & 0xFF);
        PanicHaltLoop();
        return;
    }

league_match:
    /* a1 = the home side, a2 = the opponent (SWOS2.s: CalcLeagueMatchGateReceiptsFactor
     * weighs both clubs; the C used to pass the home team twice). */
    d7 = calcLeagueMatchGateReceiptsFactor(team, opponent);
    {
        int16_t isDiy = comp->base.hasGroups;  /* 0x48 */
        if (isDiy != 0) {
            /* DIY league: check group state via file buffer stride */
            uint16_t groupIdxField = (uint16_t)_diyFileBufferCopy.header.roundComplete;
            DIYTournamentSlot *slot = (DIYTournamentSlot *)((uint8_t *)comp + groupIdxField * 0x76 + 0x15A);
            if (slot->isLeagueFormat != 0)
                goto compute_receipts;
            if (slot->formatFlag16E == 2)
                goto compute_receipts;
            goto increment_count;
        }

        /* Non-DIY league */
        if (comp->base.competitionType != 0) {
            _panicReasonCode = 0x9235;
            PanicHaltLoop();
            return;
        }

        /* Compute gate receipt factor based on division and rank */
        int16_t d3, d4;
        if (_currentDivisionIndex == 0) {
            d3 = 0x52; d4 = 0x7D;
        } else if (_currentDivisionIndex == 1) {
            d3 = 0x55; d4 = 0x81;
        } else if (_currentDivisionIndex == 2) {
            d3 = 0x59; d4 = 0x78;
        } else {
            d3 = 0x58; d4 = 0x71;
        }

        /* Find team rank in league table — search sorted team order for selTeamNumber.
         * ASM: a0 = leagueTeamIds, loop reads teamEntryOffsets,
         * divides by 18 to get team index, looks up in leagueTeamIds.
         * leagueTable is BE-packed (295c9f3); compare via BE_READ_U16. */
        uint16_t *teamEntryOffsets = comp->matchData.league.teamEntryOffsets;
        uint16_t *leagueTable = comp->matchData.league.leagueTeamIds;
        int16_t d1 = 0;
        do {
            int16_t tableEntry = (int16_t)teamEntryOffsets[d1];
            uint16_t teamIdx = (uint16_t)((uint32_t)(int32_t)tableEntry / 18);
            d1++;
        } while (BE_READ_U16(&leagueTable[(uint16_t)((uint32_t)(int32_t)(int16_t)teamEntryOffsets[d1 - 1] / 18)])
                 != _selTeamNumber);
        d1--;  /* convert to 0-based rank */

        int16_t numTeamsInDiv = comp->base.numTeams;  /* 0x32 */
        int16_t rankDelta = numTeamsInDiv - d1 - 1;
        int16_t numTeamsMinus1 = numTeamsInDiv - 1;
        uint16_t pct = (uint16_t)((uint32_t)((uint16_t)rankDelta * 100) / (uint16_t)numTeamsMinus1);
        int16_t range = d4 - d3;
        uint16_t factorFromRange = (uint16_t)((uint32_t)(pct * (uint16_t)range) / 100);
        int16_t receiptIndex = d3 + (int16_t)factorFromRange;
        _ballSpeed = (uint16_t)receiptIndex;

        /* Multiply by crowd factor from avgSkill (0x30), divide by avg player price.
         *
         * The divisor is GetAveragePlayerPrice(careerTeam) itself (SWOS2.s: `lea careerTeam,a0;
         * jsr GetAveragePlayerPrice; divu.w d0,d1`), the squad's average price INDEX -- not
         * the team value CalcGateReceiptsBaseFromTeamValue makes of it, which only the two
         * factor helpers use. Dividing by that value (thousands, not tens) left nothing:
         * league home games paid no gate at all, while cup games did (reports: "no gate
         * receipts for league games, only for cups"; fired for losing money). */
        int8_t crowdFactor = (int8_t)comp->base.avgSkill;  /* 0x30 */
        uint32_t product = (uint32_t)(uint16_t)receiptIndex * (uint16_t)(int16_t)crowdFactor;
        uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
        if (avgPrice == 0) avgPrice = 1; /* safety */
        uint16_t priceRatio = (uint16_t)(product / (uint32_t)avgPrice);
        /* muls.w d1,d7 is a 32-bit product, divs.w #100 then divides it: the product must not
         * be cut to 16 bits first, as it was. */
        int32_t gateProduct = (int32_t)(int16_t)d7 * (int32_t)(int16_t)priceRatio;
        int16_t gateFactorResult = (int16_t)(gateProduct / 100);
        _gateReceiptsFactor = gateFactorResult;
        d7 = (uint32_t)(uint16_t)gateFactorResult;
        goto compute_receipts;
    }

cup_match_type4:
    /* compType 4 falls through to cupMatchType1or2 */
cup_match_type1or2:
    d7 = calcCupMatchGateReceiptsFactor(team, opponent);
    {
        if (compType != 4) {
            /* compType 1 or 2: cup match */
            if (comp->base.competitionType != 1) {
                _panicReasonCode = 0x9236;
                PanicHaltLoop();
                return;
            }
        }
        int16_t totalRounds = comp->base.numRounds;          /* 0x5A */
        int16_t currentRound = comp->base.currentRoundIndex;  /* 0x58 */
        int16_t roundIndex = totalRounds - 1 - currentRound;
        d7 = (uint16_t)((uint32_t)(d7 * leagueMatchReceiptsMultiplierTable[roundIndex]) / 100);

        /* Check match result via roundLegs[currentRound] (0x66 + matchSlot) */
        if (comp->matchData.cup.roundLegs[currentRound] == 2)
            goto compute_receipts;
        goto increment_count;
    }

diy_comp_match:
    d7 = calcCupMatchGateReceiptsFactor(team, opponent);
    {
        int16_t isDiy = comp->base.hasGroups;  /* 0x48 */
        if (isDiy == 0) {
            /* Non-DIY comp type 3 */
            if (comp->base.competitionType == 0)
                goto compute_receipts;
            if (comp->base.competitionType == 1) {
                /* Cup match path */
                int16_t totalRounds = comp->base.numRounds;          /* 0x5A */
                int16_t currentRound = comp->base.currentRoundIndex;  /* 0x58 */
                int16_t roundIndex = totalRounds - 1 - currentRound;
                d7 = (uint16_t)((uint32_t)(d7 * cupMatchReceiptsMultiplierTable[roundIndex]) / 100);
                if (comp->matchData.cup.roundLegs[currentRound] == 2)
                    goto compute_receipts;
                goto increment_count;
            }
            _panicReasonCode = 0x9239;
            PanicHaltLoop();
            return;
        } else {
            /* DIY competition with group stages
             * NOTE: IDA bug at original L28446-28454. The original used
             * lea ($2DAD0).l,a0 which should be diyFileBufferCopy.
             * Also the second currentRound read used a0=cupMatchReceiptsMultiplierTable
             * but should have been diyFileBufferCopy->roundComplete (offset $54).
             */
            uint16_t totalRoundsField = (uint16_t)_diyFileBufferCopy.header.totalMatchesInRound;
            uint16_t curRoundField = (uint16_t)_diyFileBufferCopy.header.roundComplete;
            int16_t roundIndex = (int16_t)(totalRoundsField - 1 - curRoundField);
            d7 = (uint16_t)((uint32_t)(d7 * cupMatchReceiptsMultiplierTable[roundIndex]) / 100);

            /* Use curRoundField as group index to check match state via file buffer stride */
            DIYTournamentSlot *slot = (DIYTournamentSlot *)((uint8_t *)comp + curRoundField * 0x76 + 0x15A);
            if (slot->isLeagueFormat != 0)
                goto compute_receipts;
            if (slot->formatFlag16E == 2)
                goto compute_receipts;
            goto increment_count;
        }
    }

compute_receipts:
    if (team == &_careerTeam) {
        d7 <<= 1;   /* double for career team home matches */
        _seasonMatchReceiptsCount++;
    } else {
        d7 = 0;
        goto final_update;
    }

increment_count:
    _seasonMatchReceiptsCount++;

final_update:
    d7 &= 0xFFFF;
    d7 <<= 4;
    _gateReceipts += (int32_t)d7;
    _newBalance += (int32_t)d7;
    _seasonGateReceiptsAccum2 += (int32_t)d7;
    _seasonGateReceiptsAccum += (int32_t)d7;
    ClampNewBalanceAndUpdateClubStatus();
    ClearOverdraftStateIfRecovered();
}

/* ================================================================
 * Group D: Chairman warning system
 * ================================================================ */

/* MaybeSackManagerAfterSeasonReview */
__attribute__((used, externally_visible))
void MaybeSackManagerAfterSeasonReview(void) {
    if (_stayingInOldClub == 0)
        return;
    if (_wonWorldCup != 0)
        return;
    if (_wonContinentalCup != 0)
        return;
    if (_qualifyingStage1PendingFlag0 != 0)
        return;
    if (_qualifyingStage1PendingFlag1 != 0)
        return;
    if (_seasonReview < 2)
        return;

    uint16_t randVal = Rand();
    uint16_t threshold;
    if (_seasonReview == 2)
        threshold = 0x20;
    else if (_seasonReview == 3)
        threshold = 0x55;
    else
        threshold = 0xAA;

    if (randVal > threshold)
        return;

    _currentMessage = CHAIRMAN_WE_DIDNT_RENEW_YOUR_CONTRACT;
    ShowChairmanMessage();
    _youreSacked = 1;
}

/* Helper: check if secondary/tertiary comps are finished (blocks escalation) */
static uint16_t areActiveCompsFinished(void) {
    if (_secondaryCompetitionPtr != 0) {
        if (_competitionContextBuf.tourn.header.compFinished != 0)
            return 1;
    }
    if (_tertiaryCompetitionPtr != 0) {
        if (_tertiaryCompetitionContextBuf.tourn.header.compFinished != 0)
            return 1;
    }
    return 0;
}

/* UpdateCareerChairmanWarningsAfterMatch */
__attribute__((used, externally_visible))
void UpdateCareerChairmanWarningsAfterMatch(void) {
    if (_isNationalSelector != 0)
        return;

    /* Decrement cooldowns */
    if (_overdraftWarningCooldown != 0)
        _overdraftWarningCooldown--;
    if (_performanceWarningCooldown != 0)
        _performanceWarningCooldown--;

    /* Get team rank in DIY league */
    uint16_t d7 = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);

    /* Compute performance pressure factor */
    int16_t viewPhase = (int16_t)(int8_t)_viewRandomPhase;
    int16_t bias = (int16_t)_careerJobOfferPhaseBias;
    int16_t delta = (int16_t)(viewPhase - bias);
    int16_t pressureFactor = (int16_t)((int32_t)(delta << 2) / (int32_t)_wageScalingDivisor);

    if (pressureFactor == 0)
        goto skip_performance_warning;

    /* Check if league warnings are applicable */
    if (_primaryCompetitionBuf.tourn.header.competitionType != 2) {
        if (_primaryCompetitionBuf.tourn.leagueWarningsEnabled == 0)
            goto skip_performance_warning;
        /* Check if team is in relegation zone */
        uint16_t relegationBoundary = _primaryCompetitionBuf.tourn.header.promotionCount + _primaryCompetitionBuf.tourn.header.relegationCount;
        if (relegationBoundary == 0)
            relegationBoundary = 1;
        uint16_t boundary = _primaryCompetitionBuf.tourn.header.numTeams - relegationBoundary;
        if (d7 < boundary)
            goto check_expected_rank;
    }

    goto performance_warning_active;

check_expected_rank:
    {
        int16_t expRank = (int16_t)_expectedLeagueRankIndex;
        int16_t rankDiff = expRank - (int16_t)d7;
        int32_t pct = ((int32_t)rankDiff * 100) / (int32_t)(int16_t)_primaryCompetitionBuf.tourn.header.numTeams;
        if (pct > -25)
            goto clear_performance_warning;
    }

performance_warning_active:
    {
        uint16_t cooldown = _performanceWarningCooldown;
        if (_performanceWarningState == 0) {
            /* State 0: first warning */
            if (cooldown != 0)
                goto done_performance;
            _performanceWarningState = 1;
            setPerformanceWarningCooldown();
            _currentMessage = CHAIRMAN_IMPROVE_LEAGUE_POSITION;
            ShowChairmanMessage();
            return;
        }
        if (_performanceWarningState == 1) {
            /* State 1: second warning */
            if (cooldown != 0)
                goto done_performance;
            _performanceWarningState = 2;
            goto issue_improve_league_soon;
        }
        if (_performanceWarningState == 2) {
            /* State 2: third warning or escalate */
            if (cooldown != 0)
                goto done_performance;
            if (_overdraftWarningState >= 3)
                goto random_escalate;
            if (d7 == 0)
                goto random_escalate;
            if (_careerTeamLostMatch != 0)
                goto random_escalate;
            /* Check if secondary/tertiary comps block escalation */
            if (_secondaryCompetitionPtr != 0 && _competitionContextBuf.tourn.header.compFinished != 0)
                goto random_escalate;
            if (_tertiaryCompetitionPtr != 0 && _tertiaryCompetitionContextBuf.tourn.header.compFinished != 0)
                goto random_escalate;
            /* Issue 3-more-matches warning */
            _performanceWarningState = 3;
            setPerformanceWarningCooldown();
            _currentMessage = CHAIRMAN_THREE_MORE_MATCHES;
            ShowChairmanMessage();
            return;
        }
        /* State >= 3: countdown to sacking */
        if (d7 == 0) {
            _performanceWarningState = 3;
            goto done_performance;
        }
        _performanceWarningState++;
        if (_performanceWarningState < 6)
            goto done_performance;
        if (_careerTeamLostMatch != 0)
            goto done_performance;
        if (_competitionSlotUpdatedFlag != 0)
            goto done_performance;
        /* Sacked for poor performance */
        _currentMessage = CHAIRMAN_YOURE_SACKED_2;
        ShowChairmanMessage();
        _youreSacked = 1;
        return;
    }

random_escalate:
    /* 50% chance to escalate or re-issue warning */
    if ((_currentGameTick & 0x04) == 0) {
issue_improve_league_soon:
        setPerformanceWarningCooldown();
        _currentMessage = CHAIRMAN_IMPROVE_LEAGUE_POSITION_SOON;
        ShowChairmanMessage();
        return;
    }
    goto done_performance;

clear_performance_warning:
    _performanceWarningState = 0;

skip_performance_warning:
    setPerformanceWarningCooldown();

done_performance:
    /* Overdraft warning section */
    ClearOverdraftStateIfRecovered();

    if (_newBalance >= 0) {
        setOverdraftWarningCooldown();
        return;
    }

    /* Check if league warnings are applicable */
    if (_primaryCompetitionBuf.tourn.header.competitionType != 2) {
        if (_primaryCompetitionBuf.tourn.leagueWarningsEnabled == 0)
            return;
    }

    {
        uint16_t cooldown = _overdraftWarningCooldown;
        if (_overdraftWarningState == 0) {
            if (cooldown != 0)
                return;
            _overdraftWarningState = 1;
            setOverdraftWarningCooldown();
            _currentMessage = CHAIRMAN_BANK_BALANCE_OVERDRAFT;
            ShowChairmanMessage();
            return;
        }
        if (_overdraftWarningState == 1) {
            if (cooldown != 0)
                return;
            goto issue_overdraft_last_warning;
        }
        if (_overdraftWarningState == 2) {
            if (cooldown != 0)
                return;
            if (_performanceWarningState >= 3)
                goto issue_overdraft_last_warning;
            if (d7 == 0)
                goto issue_overdraft_last_warning;
            if (_careerTeamLostMatch != 0)
                goto issue_overdraft_last_warning;
            if (_secondaryCompetitionPtr != 0 && _competitionContextBuf.tourn.header.compFinished != 0)
                goto issue_overdraft_last_warning;
            if (_tertiaryCompetitionPtr != 0 && _tertiaryCompetitionContextBuf.tourn.header.compFinished != 0)
                goto issue_overdraft_last_warning;
            /* Clear overdraft immediately */
            _overdraftWarningState = 3;
            setOverdraftWarningCooldown();
            _currentMessage = CHAIRMAN_CLEAR_OVERDRAFT_IMMEDIATELY;
            ShowChairmanMessage();
            return;
        }
        /* State >= 3: countdown to sacking */
        if (d7 == 0) {
            _overdraftWarningState = 3;
            return;
        }
        _overdraftWarningState++;
        if (_overdraftWarningState < 6)
            return;
        if (_careerTeamLostMatch != 0)
            return;
        if (_competitionSlotUpdatedFlag != 0)
            return;
        _currentMessage = CHAIRMAN_YOURE_SACKED;
        ShowChairmanMessage();
        _youreSacked = 1;
        return;
    }

issue_overdraft_last_warning:
    _overdraftWarningState = 2;
    setOverdraftWarningCooldown();
    _currentMessage = CHAIRMAN_OVERDRAFT_LAST_WARNING;
    ShowChairmanMessage();
}

/* UpdateCareerAfterMatch — orchestrator */
__attribute__((used, externally_visible))
void UpdateCareerAfterMatch(void) {
    ClampNewBalanceAndUpdateClubStatus();
    ClearOverdraftStateIfRecovered();
    UpdateCareerChairmanWarningsAfterMatch();
    if (_removedOfferPlayersFromSelectedTeamCount != 0) {
        _removedOfferPlayersFromSelectedTeamCount--;
        AddCareerTeamPlayer();
    }
    CheckPlayersLeavingComing();
}

/* ================================================================
 * Group E: Weekly/season finances
 * ================================================================ */

/* ApplyWeeklyFinancialDeductions */
__attribute__((used, externally_visible))
void ApplyWeeklyFinancialDeductions(void) {
    uint8_t *listPtr = TeamSelectionKeyCache_GetCurrentListPtr();
    uint8_t currentWeek = listPtr[1];
    uint8_t lastWeek = _lastFinancialWeekIndex;
    int16_t diff = (int16_t)(uint16_t)currentWeek - (int16_t)(uint16_t)lastWeek;
    if (diff < 0)
        goto update_and_clamp;
    if (diff == 0)
        goto save_week_and_clamp;

    /* Deduct for (diff) weeks, counting from 0..diff-1 */
    int16_t weeks = diff - 1;
    for (int16_t i = weeks; i >= 0; i--) {
        int32_t wages = _weeklyPlayerWages;
        _playerWagesBill += wages;
        _newBalance -= wages;
        _seasonWagesAccum += wages;
        int32_t running = _weeklyRunningCosts;
        _generalRunningCosts += running;
        _newBalance -= running;
        _seasonRunningCostsAccum += running;
        _seasonRunningCostsAccum2 += running;
        _seasonWeeksDeducted++;
    }

save_week_and_clamp:
    listPtr = TeamSelectionKeyCache_GetCurrentListPtr();
    _lastFinancialWeekIndex = listPtr[1];

update_and_clamp:
    ClampNewBalanceAndUpdateClubStatus();
    ClearOverdraftStateIfRecovered();
}

/* GetExpectedLeagueRankIndex (static helper)
 * Computes expected league rank from competition context.
 * a6 = competition context buffer, uses d2 from GetMoreExpensiveTeamsCount.
 */
static uint16_t getExpectedLeagueRankIndex(uint8_t *ctx, uint16_t moreExpensiveCount) {
    DIYCompetitionEx *ctxEx = (DIYCompetitionEx *)ctx;
    DIYCompetition *ctxBase = &ctxEx->base;
    uint16_t d3 = (uint16_t)ctxBase->numTeams;
    uint16_t compType = (uint16_t)ctxBase->competitionType;

    if (compType == 0) {
        /* League: (numTeams-1) * playEachTeam */
        uint16_t d7 = d3 - 1;
        d7 = (uint16_t)(d7 * (uint16_t)ctxBase->playEachTeam);
        return d7;
    }

    if (compType == 1) {
        /* Cup: walk through rounds halving d3, counting d7. roundPtr starts
         * at cup roundLegs[0] and advances by stride 2 (so roundPtr[i] iterates
         * through roundLegs[i]). */
        uint16_t d7 = 0;
        uint16_t *roundPtr = ctxEx->matchData.cup.roundLegs;
        for (;;) {
            d7++;
            if (*roundPtr++ == 2)
                d7++;
            d3 >>= 1;
            if (moreExpensiveCount >= d3)
                return d7;
            if (d3 == 1)
                return d7;
        }
    }

    /* DIY (compType >= 2): walk through groups */
    {
        uint16_t numGroups = (uint16_t)ctxBase->totalMatchesInRound;
        uint8_t *groupPtr = ctx;
        uint16_t d7 = 0;

        for (uint16_t i = numGroups; i > 0; i--) {
            DIYTournamentSlot *slot = (DIYTournamentSlot *)(groupPtr + 0x15A);
            if (slot->isLeagueFormat != 0) {
                uint16_t numRounds = slot->teamsPerGroup;
                uint16_t multiplier = slot->playEachTeam;
                uint16_t val = (numRounds - 1) * multiplier;
                d7 += val;
            } else {
                d7++;
                if (slot->formatFlag16E == 2)
                    d7++;
            }
            if (moreExpensiveCount >= slot->divisionFactor)
                return d7;
            groupPtr += 0x76;
        }
        return d7;
    }
}

/* InitializeCareerSeasonFinancials */
__attribute__((used, externally_visible))
void InitializeCareerSeasonFinancials(void) {
    /* Get more expensive teams count and set expected rank */
    uint16_t moreExpensiveCount = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);
    _expectedLeagueRankIndex = moreExpensiveCount;

    /* Calculate team value divisor from all active competitions */
    uint16_t d5 = getExpectedLeagueRankIndex((uint8_t *)&_primaryCompetitionBuf, moreExpensiveCount);

    if (_secondaryCompetitionPtr != 0) {
        uint16_t mec2 = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)&_competitionContextBuf);
        d5 += getExpectedLeagueRankIndex((uint8_t *)&_competitionContextBuf, mec2);
    }
    if (_tertiaryCompetitionPtr != 0) {
        uint16_t mec3 = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)&_tertiaryCompetitionContextBuf);
        d5 += getExpectedLeagueRankIndex((uint8_t *)&_tertiaryCompetitionContextBuf, mec3);
    }
    if (_quaternaryCompetitionPtr != 0) {
        uint16_t mec4 = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)&_euroCupsCompetitionContextBuf);
        d5 += getExpectedLeagueRankIndex((uint8_t *)&_euroCupsCompetitionContextBuf, mec4);
    }

    _teamValueDivisor = d5;

    /* Compute total team value: weeklyPlayerWages * wageScalingDivisor */
    int32_t d6 = 0;
    int32_t wages = _weeklyPlayerWages;
    uint16_t scalingCount = _wageScalingDivisor;
    for (uint16_t i = 0; i < scalingCount; i++)
        d6 += wages;
    _totalTeamValue = d6;

    /* Compute weekly running costs from total team value */
    uint32_t d0 = (uint32_t)d6;
    d0 = (uint16_t)(d0 / 1000);           /* divu #$3E8 */
    d0 = (uint32_t)(uint16_t)d0 * 207;    /* mulu #$CF */
    d0 = (uint16_t)(d0 / 6144);           /* divu #$1800 */
    d0 = (uint32_t)(uint16_t)d0 * 96;     /* mulu #$60 */
    d0 = (uint16_t)(d0 / _wageScalingDivisor); /* divu wageScalingDivisor */
    d0 = (uint32_t)(uint16_t)d0 * 1000;   /* mulu #$3E8 */
    _weeklyRunningCosts = (int32_t)d0;

    /* Clear accumulators */
    _seasonRunningCostsAccum = 0;
    _seasonWagesAccum = 0;
    _seasonGateReceiptsAccum = 0;
    _seasonMatchReceiptsCount = 0;
    _seasonWeeksDeducted = 0;
    _seasonGateReceiptsAccum2 = 0;
    _seasonRunningCostsAccum2 = 0;

    /* Handle staying/leaving club balance adjustments */
    if (_stayingInOldClub == 0) {
        int32_t bal = _startingBalanceCopy + _competitionBonuses;
        _old_balance += bal;
        _startingBalanceCopy = 0;
        _competitionBonuses = 0;
    }

    /* Reset warning states */
    _overdraftStreak = 0;
    _overdraftWarningState = 0;
    _performanceWarningState = 0;
    _overdraftWarningCooldown = 0;
    _performanceWarningCooldown = 0;
}

/* ================================================================
 * Group F: Board confidence system
 * ================================================================ */

/* CalcBoardConfidenceDeltaPercent (static) */
static int16_t calcBoardConfidenceDeltaPercent(void) {
    if (_isNationalSelector != 0) {
        if (_seasonFinished == 0)
            return (int16_t)_boardConfidenceHistory[3];
        return seasonReviewConfidenceDeltaTable[_seasonReview];
    }

    /* Club manager: compute from league rank delta */
    uint16_t teamRank = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);
    int16_t d0 = (int16_t)_expectedLeagueRankIndex - (int16_t)teamRank;
    int16_t numTeams = ((DIYCompetitionEx *)&_primaryCompetitionBuf)->base.numTeams;
    d0 = (int16_t)(((int32_t)d0 * 100) / (int32_t)numTeams);
    return d0;
}

/* GetBoardConfidenceScore */
__attribute__((used, externally_visible))
int16_t GetBoardConfidenceScore(void) {
    int16_t d0 = calcBoardConfidenceDeltaPercent();
    d0 += (int16_t)_boardConfidenceHistory[0];
    d0 += (int16_t)_boardConfidenceHistory[1];
    d0 += (int16_t)_boardConfidenceHistory[2];
    d0 += _boardConfidenceScore;
    return d0;
}

/* UpdateBoardConfidenceScore */
__attribute__((used, externally_visible))
int16_t UpdateBoardConfidenceScore(void) {
    int16_t d0 = _boardConfidenceScore;

    if (_majorTrophyBonusActive != 0)
        d0 += 100;
    if (_cupBonus2Active != 0)
        d0 += 50;
    if (_cupBonus1Active != 0)
        d0 += 50;
    if (_playWinCupAnimation != 0 && _currentDivisionIndex == 0)
        d0 += 50;
    if (_minorTrophyBonusActive != 0)
        d0 += 25;
    if (_wonWorldCup != 0)
        d0 += 150;
    if (_wonContinentalCup != 0)
        d0 += 100;

    if (d0 > 200)
        d0 = 200;

    _boardConfidenceScore = d0;
    return (uint16_t)d0;
}

/* PushBoardConfidenceHistory */
__attribute__((used, externally_visible))
int16_t PushBoardConfidenceHistory(void) {
    _boardConfidenceHistory[3] = _boardConfidenceHistory[2];
    _boardConfidenceHistory[2] = _boardConfidenceHistory[1];
    _boardConfidenceHistory[1] = _boardConfidenceHistory[0];
    int16_t d0 = calcBoardConfidenceDeltaPercent();
    _boardConfidenceHistory[0] = (uint16_t)d0;
    return d0;
}

/* UpdateBoardConfidenceModifiers */
__attribute__((used, externally_visible))
void UpdateBoardConfidenceModifiers(void) {
    _qualifyingStage1PendingFlag0 = 0;
    _qualifyingStage1PendingFlag1 = 0;
    _qualifyingStage2PendingFlag0 = 0;
    _qualifyingStage2PendingFlag1 = 0;

    if (_isNationalSelector == 0)
        goto check_winners;

    /* National selector: check World Cup qualifying */
    if ((_someTeamFlags & 0x40) == 0)
        goto check_ec_qualifying;

    {
        uint16_t team = _selTeamNumber;
        if (team == _worldCupQualifyingTeam || team == _worldCupHostTeam)
            goto set_qual1_flag0;

        uint8_t curId = _currentCompetitionPtr ? comp_id((void *)_currentCompetitionPtr) : 0xFFu;
        if (!comp_is_wc_qualifier(curId))
            goto check_ec_qualifying;

        /* In WC qualifying: find team rank and determine qualifying slots */
        uint16_t teamRank = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);
        uint16_t qualSlots;

        if (curId == COMPID_northAmericaWorldCupQualification)
            qualSlots = 3;
        else if (curId == COMPID_southAmericaWorldCupQualification)
            qualSlots = 4;
        else if (curId == COMPID_africaWorldCupQualification)
            qualSlots = 5;
        else if (curId == COMPID_asiaWorldCupQualification)
            qualSlots = 2;
        else if (curId == COMPID_oceaniaWorldCupQualification)
            qualSlots = 1;
        else if (curId == COMPID_worldCupQualifyingGroup8)
            qualSlots = 1;
        else
            qualSlots = 2;

        if (_diyFoundTeamIndex != 0) {
            goto set_qual2_flag0;
        }
        if (_diyCompHasGroupStage == 0)
            goto check_ec_qualifying;
        if (teamRank >= qualSlots)
            goto set_qual2_flag0;
    }

set_qual1_flag0:
    _qualifyingStage1PendingFlag0 = 1;
    goto check_ec_qualifying;

set_qual2_flag0:
    _qualifyingStage2PendingFlag0 = 1;

check_ec_qualifying:
    if ((_someTeamFlags & 0x01) == 0)
        goto check_winners;

    {
        uint16_t team = _selTeamNumber;
        if (team == _euroChampionshipQualTeam)
            goto set_qual1_flag1;

        uint8_t curId = _currentCompetitionPtr ? comp_id((void *)_currentCompetitionPtr) : 0xFFu;
        if (!comp_is_ec_qualifier(curId))
            goto check_winners;

        uint16_t teamRank = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);
        uint16_t qualSlots;
        if (curId == COMPID_ecQualifyingGroup8)
            qualSlots = 1;
        else
            qualSlots = 2;

        if (_diyFoundTeamIndex != 0)
            goto set_qual2_flag1;
        if (_diyCompHasGroupStage == 0)
            goto check_winners;
        if (teamRank >= qualSlots)
            goto set_qual2_flag1;
    }

    _qualifyingStage1PendingFlag1 = 1;
    goto check_winners;

set_qual1_flag1:
    _qualifyingStage1PendingFlag1 = 1;
    goto check_winners;

set_qual2_flag1:
    _qualifyingStage2PendingFlag1 = 1;

check_winners:
    /* Clear trophy flags */
    _wonWorldCup = 0;
    _wonContinentalCup = 0;
    _wonInvitationTournament = 0;
    _playWinCupAnimation = 0;

    /* Check if we won the primary competition */
    if (_selTeamNumber != _primaryCompetitionBuf.tourn.header.winnerTeam)
        goto check_secondary_winner;
    {
        uint8_t curId = _currentCompetitionPtr ? comp_id((void *)_currentCompetitionPtr) : 0xFFu;
        if (curId == COMPID_friendly)
            goto check_secondary_winner;
        if (curId == COMPID_tour)
            goto check_secondary_winner;
        if (comp_is_ec_qualifier(curId))      /* Skip if EC qualifying */
            goto check_secondary_winner;
        if (comp_is_wc_qualifier(curId))      /* Skip if WC qualifying */
            goto check_secondary_winner;

        /* Determine which trophy was won */
        if (curId == COMPID_invitationTournament) {
            _wonInvitationTournament = 1;
        } else if (curId == COMPID_worldCup2) {
            _wonWorldCup = 1;
        } else if (comp_is_continental_cup(curId)) {
            _wonContinentalCup = 1;
        } else {
            _playWinCupAnimation = 1;
        }
    }

check_secondary_winner:
    _minorTrophyBonusActive = 0;

    if (_secondaryCompetitionPtr == 0)
        goto check_tertiary_winner;
    if (_selTeamNumber != _competitionContextBuf.tourn.header.winnerTeam)
        goto check_tertiary_winner;
    {
        /* _secondaryCompetitionPtr is non-NULL here (guarded above). */
        uint8_t secId = comp_id((void *)_secondaryCompetitionPtr);
        if (secId == COMPID_friendly)
            goto check_tertiary_winner;
        /* NOTE: original tests the PRIMARY ptr against tour here, not the
         * secondary — preserved verbatim (faithful to SWOS2.s). */
        if (_currentCompetitionPtr && comp_id((void *)_currentCompetitionPtr) == COMPID_tour)
            goto check_tertiary_winner;

        if (secId == COMPID_invitationTournament) {
            _wonInvitationTournament = 1;
        } else if (comp_is_continental_cup(secId)) {
            _wonContinentalCup = 1;
        } else {
            _minorTrophyBonusActive = 1;
        }
    }

check_tertiary_winner:
    _leagueTitleBonusActive = 0;

    if (_tertiaryCompetitionPtr != 0 &&
        _selTeamNumber == _tertiaryCompetitionContextBuf.tourn.header.winnerTeam &&
        comp_id((void *)_tertiaryCompetitionPtr) != COMPID_friendly) {  /* non-NULL: guarded by && above */
        _leagueTitleBonusActive = 1;
    }

    /* Check quaternary (Euro cups) */
    _majorTrophyBonusActive = 0;
    _cupBonus1Active = 0;
    _cupBonus2Active = 0;

    if (_quaternaryCompetitionPtr == 0)
        return;
    if (_selTeamNumber != _euroCupsCompetitionContextBuf.tourn.header.winnerTeam)
        return;

    if (_euroCupSlotIndex == 0 || _euroCupSlotIndex == 3) {
        /* European Cup (0) or Copa Libertadores (3): the premier continental club
         * cup -> major trophy bonus. */
        _majorTrophyBonusActive = 1;
    } else if (_euroCupSlotIndex == 2) {
        _cupBonus2Active = 1;
    } else {
        _cupBonus1Active = 1;
    }
}

/* ================================================================
 * Group G: Reputation + starting balance
 * ================================================================ */

/* AddReputationFromBonusValueDiv4 (static) */
static void addReputationFromBonusValueDiv4(int8_t bonusValue) {
    int16_t val = (int16_t)bonusValue;
    val >>= 2;  /* lsr.w #2 - but ext.w d0 first, so this is unsigned shift */
    _careerReputationPoints += val;
}

/* AddReputationFromBonusValueDiv8 (static) */
static void addReputationFromBonusValueDiv8(int8_t bonusValue) {
    int16_t val = (int16_t)bonusValue;
    val >>= 3;
    _careerReputationPoints += val;
}

/* UpdateCareerReputationBonuses */
__attribute__((used, externally_visible))
void UpdateCareerReputationBonuses(void) {
    /* National selector reputation delta */
    if (_isNationalSelector != 0) {
        int16_t delta = nationalSelectorSeasonReviewReputationDeltaTable[_seasonReview];
        _nationalSelectorReputationPoints += delta;
        if (_nationalSelectorReputationPoints < 0)
            _nationalSelectorReputationPoints = 0;
        if (_nationalSelectorReputationPoints > 49)
            _nationalSelectorReputationPoints = 49;
    }

    /* Club reputation from league rank */
    DIYCompetitionEx *cfbEx = (DIYCompetitionEx *)&_primaryCompetitionBuf;
    if (cfbEx->base.competitionType != 2) {
        /* Not a pure cup — compute reputation from rank delta */
        uint16_t avgPrice = getTeamValueForGateReceipts(&_careerTeam);
        int16_t d7 = (int16_t)avgPrice;

        /* Find team rank in league table.
         * leagueTable is BE-packed (295c9f3 convention) — compare via
         * BE_READ_U16 against selTeamNumber, which the rest of the code
         * keeps in (country<<8)|ordinal form. Without this the loop
         * compares the host-native byte-swap of each entry and never
         * matches → hang on FINISH SEASON. */
        uint16_t *leagueTable = cfbEx->matchData.league.leagueTeamIds;
        int16_t *teamEntryOffsets = (int16_t *)cfbEx->matchData.league.teamEntryOffsets;
        int16_t d1 = -1;
        do {
            d1++;
            int16_t entry = teamEntryOffsets[d1];
            int32_t extended = (int32_t)entry;
            uint16_t divided = (uint16_t)((uint32_t)extended / 18);
            (void)divided;
        } while (BE_READ_U16(&leagueTable[(uint16_t)((uint32_t)(int32_t)teamEntryOffsets[d1] / 18)])
                 != _selTeamNumber);
        /* d1 is now the rank (0-based index into teamEntryOffsets) */

        int16_t rankDelta = (int16_t)_expectedLeagueRankIndex - d1;
        int16_t numTeams = cfbEx->base.numTeams;
        int16_t pct = (int16_t)(((int32_t)rankDelta * 100) / (int32_t)numTeams);
        int16_t repDelta = (int16_t)(((int32_t)pct * (int32_t)d7) / 200);
        _careerReputationPoints += repDelta;
    }

    /* Trophy reputation bonuses */
    if (_wonWorldCup != 0)
        addReputationFromBonusValueDiv4((int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue);

    if (_wonContinentalCup != 0) {
        /* Major continental (primary slot) vs minor (secondary context) */
        uint8_t curId = _currentCompetitionPtr ? comp_id((void *)_currentCompetitionPtr) : 0xFFu;
        int8_t bonusVal;
        if (comp_is_continental_cup(curId)) {
            bonusVal = (int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue;
        } else {
            bonusVal = (int8_t)_competitionContextBuf.tourn.header.trophyBonusValue;
        }
        addReputationFromBonusValueDiv4(bonusVal);
    }

    if (_playWinCupAnimation != 0) {
        _winCupBonusCount++;
        addReputationFromBonusValueDiv4((int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue);
    }

    if (_minorTrophyBonusActive != 0) {
        _minorTrophyBonusCount++;
        addReputationFromBonusValueDiv4((int8_t)_competitionContextBuf.tourn.header.trophyBonusValue);
    }

    if (_leagueTitleBonusActive != 0)
        addReputationFromBonusValueDiv8((int8_t)_tertiaryCompetitionContextBuf.tourn.header.trophyBonusValue);

    if (_majorTrophyBonusActive != 0 || _cupBonus1Active != 0 || _cupBonus2Active != 0) {
        _majorTrophyBonusCount++;
        addReputationFromBonusValueDiv4((int8_t)_euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue);
    }
}

/* InitializeStartingBalanceForNewSeason */
__attribute__((used, externally_visible))
void InitializeStartingBalanceForNewSeason(void) {
    /* Get team value base from avg player price */
    uint16_t avgPrice = getTeamValueForGateReceipts(&_careerTeam);
    uint16_t teamBase = GetTeamValueBase(avgPrice);
    uint16_t d2 = teamBase;

    /* Compute adjusted starting balance from expected rank */
    uint16_t teamRank = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);
    int16_t rankDelta = (int16_t)_expectedLeagueRankIndex - (int16_t)teamRank;
    int16_t pct = (int16_t)(((int32_t)rankDelta * 100) / (int32_t)(int16_t)_primaryCompetitionBuf.tourn.header.numTeams);
    pct >>= 1;   /* asr.w #1 */
    pct += 100;   /* addi.w #$64 */
    uint16_t adjustedBase = (uint16_t)(((uint32_t)(uint16_t)pct * (uint32_t)d2) / 100);

    /* Add random factor (90-110%) */
    uint16_t r = Rand2();
    uint16_t randPct = (uint16_t)((r * 20) >> 8) + 90;  /* 90 to ~110 */
    uint16_t finalBase = (uint16_t)(((uint32_t)randPct * (uint32_t)adjustedBase) / 100);
    _startingBalance = (int32_t)((uint32_t)finalBase * 1000);

    /* Cup win bonuses by division */
    if (_playWinCupAnimation != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue;
        if (_currentDivisionIndex == 0) {
            /* Division 0: add twice (once extra) */
            _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * 0xEDC8);
            _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * 0xEDC8);
        } else {
            uint32_t multiplier;
            if (_currentDivisionIndex == 1)
                multiplier = 0xB720;
            else if (_currentDivisionIndex == 2)
                multiplier = 0x7FDD;
            else
                multiplier = 0x61A8;
            _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * multiplier);
        }
    }

    /* Minor trophy bonus */
    if (_minorTrophyBonusActive != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_competitionContextBuf.tourn.header.trophyBonusValue;
        _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * 0xCED0);
    }

    /* League title bonus */
    if (_leagueTitleBonusActive != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_tertiaryCompetitionContextBuf.tourn.header.trophyBonusValue;
        _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * 0x44F1);
    }

    /* Major trophy / Euro cup bonuses */
    if (_majorTrophyBonusActive != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue;
        uint32_t amount = (uint32_t)(uint16_t)bonusVal * 0xF426;
        _startingBalance += (int32_t)amount;
        _startingBalance += (int32_t)amount;  /* added twice in asm */
    }

    if (_cupBonus1Active != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue;
        uint32_t amount = (uint32_t)(uint16_t)bonusVal * 0x7A14;
        _startingBalance += (int32_t)amount;
        _startingBalance += (int32_t)amount;
    }

    if (_cupBonus2Active != 0) {
        int16_t bonusVal = (int16_t)(int8_t)_euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue;
        uint32_t amount = (uint32_t)(uint16_t)bonusVal * 0xB190;
        _startingBalance += (int32_t)amount;
        _startingBalance += (int32_t)amount;
    }

    /* Promoted bonus (only if no cup win) */
    if (_playWinCupAnimation != 0)
        return;
    if (_playPromotedAnimation == 0)
        return;

    {
        int16_t bonusVal = (int16_t)(int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue;
        if (_currentDivisionIndex == 0) {
            /* Cannot be promoted to division 0 — panic */
            _panicReasonCode = 0x1782;
            PanicHaltLoop();
            return;
        }
        uint32_t multiplier;
        if (_currentDivisionIndex == 1)
            multiplier = 0x6DE0;
        else if (_currentDivisionIndex == 2)
            multiplier = 0x4709;
        else
            multiplier = 0x30D4;
        _startingBalance += (int32_t)((uint32_t)(uint16_t)bonusVal * multiplier);
    }
}

/* ================================================================
 * recalcTotalWeeklyWages  (SWOS2.s line 86716, loc_14B3AE)
 *
 * Iterates all career team players and sums their cached
 * weekly wages into weeklyPlayerWages.
 * Shared by Career_UpdatePlayerWeeklyWageCache (fall-through)
 * and Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages.
 * ================================================================ */
static void recalcTotalWeeklyWages(void)
{
    int32_t total = 0;
    int16_t i;
    for (i = (int16_t)(_careerNumPlayers - 1); i >= 0; i--) {
        GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
        uint16_t fileIdx = _cGetPlayerAtIndexFileIdx;
        total += _weeklyWageByPlayerIndex[fileIdx];
    }
    _weeklyPlayerWages = total;
}

/* ================================================================
 * GetAvgTeamValueBaseForCurrentPlayer  (SWOS2.s line 71480)
 *
 * Gets the average team value base for a player.
 * Calls GetPlayer to get player pointer, then GetDisplayedPlayerValue
 * to get the displayed value (1-80), and indexes into avgTeamValueBases.
 *
 * In asm, d0=teamNumber and d1=playerIndex are set by caller.
 * We take playerIndex as a parameter and read selTeamNumber internally,
 * matching the actual calling convention where d0 is always selTeamNumber.
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetAvgTeamValueBaseForCurrentPlayer(uint16_t teamNumber, uint16_t playerIndex)
{
    uint8_t *player = GetPlayer(teamNumber, playerIndex);
    uint16_t displayedValue = GetDisplayedPlayerValue(player);
    displayedValue &= 0xFF;
    return _avgTeamValueBases[displayedValue];
}

/* ================================================================
 * Career_UpdatePlayerWeeklyWageCache  (SWOS2.s line 86678)
 *
 * Calculates and stores the weekly wage for a single player.
 * Steps:
 *   1. Get average team value base for the player
 *   2. Check player status flags for wage reduction
 *      (0x9B = trialist / "TRIAL" → 33%, 0x9A = reserve / "RES" → 66%)
 *   3. Apply scaling: value * 1000 / 768 * 96 / wageScalingDivisor
 *   4. Store in weeklyWageByPlayerIndex[playerIndex]
 *   5. Recalculate total weekly wages
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex)
{
    uint16_t avgBase = GetAvgTeamValueBaseForCurrentPlayer(_selTeamNumber, playerIndex);

    /* Get player pointer to check status flags */
    uint8_t *player = GetPlayer(_selTeamNumber, playerIndex);
    PlayerFile *pf = (PlayerFile *)(player + 76);
    uint8_t someFlag = (uint8_t)pf->form;

    if (someFlag == PLAYER_FORM_TRIALIST) {
        /* Trialist: reduce wage (default 33%) */
        avgBase = (uint16_t)((uint32_t)avgBase * _careerParams.wage_trialist_pct / 100);
    } else if (someFlag == PLAYER_FORM_RESERVE) {
        /* Youth: reduce wage (default 66%) */
        avgBase = (uint16_t)((uint32_t)avgBase * _careerParams.wage_reserve_pct / 100);
    }

    /* Scale: value * 1000 / 768 * scale / wageScalingDivisor */
    uint32_t wage = (uint32_t)avgBase * 1000;
    wage = wage / 768;
    wage = wage * _careerParams.wage_scale;
    wage = wage / (uint32_t)_wageScalingDivisor;
    wage &= 0xFFFF;  /* keep 16-bit quotient (68k divu behavior) */

    _weeklyWageByPlayerIndex[playerIndex] = (int32_t)wage;

    recalcTotalWeeklyWages();
}

/* ================================================================
 * Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages  (SWOS2.s line 86711)
 *
 * Clears the weekly wage entry for a player and recalculates
 * the total weekly wages.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages(uint16_t playerIndex)
{
    _weeklyWageByPlayerIndex[playerIndex] = 0;
    recalcTotalWeeklyWages();
}

/* ================================================================
 * ShowXxxMessage functions  (SWOS2.s lines 38068-38130)
 *
 * Each selects a message ID based on isNationalSelector
 * (national vs club chairman messages), sets currentMessage,
 * then calls ShowChairmanMessage.
 * ================================================================ */

/* ShowLeavingMessage — "Thanks for all your help" */
__attribute__((used, externally_visible))
void ShowLeavingMessage(void)
{
    _currentMessage = _isNationalSelector ? 32 : 18;
    ShowChairmanMessage();
}

/* ShowGreetingMessage — "Welcome, we look forward..." */
__attribute__((used, externally_visible))
void ShowGreetingMessage(void)
{
    _currentMessage = _isNationalSelector ? 33 : 19;
    ShowChairmanMessage();
}

/* ShowLastSeasonMessage — "Enjoy your last season" */
__attribute__((used, externally_visible))
void ShowLastSeasonMessage(void)
{
    _currentMessage = _isNationalSelector ? 34 : 20;
    ShowChairmanMessage();
}

/* ShowWishYouSuccessMessage — "Wish you success" */
__attribute__((used, externally_visible))
void ShowWishYouSuccessMessage(void)
{
    _currentMessage = _isNationalSelector ? 35 : 21;
    ShowChairmanMessage();
}

/* ShowEnjoyLastSeasonMessage — "Enjoy last season before..." */
__attribute__((used, externally_visible))
void ShowEnjoyLastSeasonMessage(void)
{
    _currentMessage = _isNationalSelector ? 36 : 22;
    ShowChairmanMessage();
}

/* ShowRetirementMessage — "Enjoy your retirement" */
__attribute__((used, externally_visible))
void ShowRetirementMessage(void)
{
    _currentMessage = _isNationalSelector ? 37 : 23;
    ShowChairmanMessage();
}

/* ================================================================
 * CalculateStartingBalance  (SWOS2.s line 86092)
 *
 * Calculates career starting balance for a given team.
 * Gets team value base * 3, applies random percentage (80-119%),
 * divides by 100 and multiplies by 1000 to get final balance.
 *
 * Asm: CalculateStartingBalance (d0=teamNumber → d1=balance)
 * ================================================================ */
extern TeamFileEx *GetTeamPointer(uint16_t teamId);
extern uint16_t GetAveragePlayerPrice(TeamFileEx *team);

__attribute__((used, externally_visible))
uint32_t CalculateStartingBalance(uint16_t teamNumber)
{
    TeamFileEx *teamPtr = GetTeamPointer(teamNumber);
    uint16_t avgPrice = GetAveragePlayerPrice(teamPtr);
    uint16_t baseValue = GetTeamValueBase(avgPrice);
    uint16_t base3 = (uint16_t)(baseValue * _careerParams.start_balance_value_mult);
    uint16_t r = Rand2();
    uint16_t randFactor = (uint16_t)((uint16_t)(r * _careerParams.start_balance_pct_range) >> 8);
    uint16_t pct = (uint16_t)(randFactor + _careerParams.start_balance_pct_min);  /* default 80-119 */
    uint32_t product = (uint32_t)pct * (uint32_t)base3;
    uint16_t divided = (uint16_t)(product / 100);                /* divu.w quotient */
    return (uint32_t)divided * _careerParams.start_balance_scale;
}

/* ================================================================
 * Chunk 95 — Chairman message helpers, post-match stat updates,
 *            CheckPlayersLeavingComing, AddTrialPlayer
 * ================================================================ */

/* Message ID constants for chairman messages */
#define MSG_TRIAL_PLAYER_LEFT_CLUB        24
#define MSG_RESERVE_PLAYER_LEFT_CLUB      25
#define MSG_PLAYER_JOINED_CLUB_ON_TRIAL   26

extern uint8_t *_plNameForMessage;
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern void Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
extern void RemovePlayerFromTeam(uint16_t playerOrd);

/* asm bridges for functions not yet ported */
extern void Career_ConvertPlayerToTrialistFromName(uint16_t teamNum, uint16_t playerIdx);
extern void TrainingMatch_SaveSelectedPlayerId(uint16_t value);

/* competitionFileBuffer migrated to primaryCompetitionBuf (slot-0 untangle, S6); buffer extern declared above */


/* ================================================================
 * PlayerJoinedClubOnTrial — show chairman message for player joining on trial.
 * Asm: PlayerJoinedClubOnTrial (a1=name ptr)
 * ================================================================ */
__attribute__((used, externally_visible))
void PlayerJoinedClubOnTrial(uint8_t *name)
{
    _plNameForMessage = name;
    _currentMessage = MSG_PLAYER_JOINED_CLUB_ON_TRIAL;
    ShowChairmanMessage();
}

/* ================================================================
 * TrialPlayerLeftClubMessage — show chairman message for trial player leaving.
 * Asm: TrialPlayerLeftClubMessage (a1=name ptr)
 * ================================================================ */
__attribute__((used, externally_visible))
void TrialPlayerLeftClubMessage(uint8_t *name)
{
    _plNameForMessage = name;
    _currentMessage = MSG_TRIAL_PLAYER_LEFT_CLUB;
    ShowChairmanMessage();
}

/* ================================================================
 * ReservePlayerLeftClubMessage — show chairman message for reserve player leaving.
 * Asm: ReservePlayerLeftClubMessage (a1=name ptr)
 * ================================================================ */
__attribute__((used, externally_visible))
void ReservePlayerLeftClubMessage(uint8_t *name)
{
    _plNameForMessage = name;
    _currentMessage = MSG_RESERVE_PLAYER_LEFT_CLUB;
    ShowChairmanMessage();
}

/* ================================================================
 * Career_UpdateSinglePlayerPostMatchStats — update one player's post-match form.
 * Asm: Career_UpdateSinglePlayerPostMatchStats (a6=player raw ptr within careerTeam)
 *
 * The player pointer is within careerTeam; index = (ptr - careerTeam) / 38.
 * Handles form transitions: $9B→$9A, $9A→restored form (20% chance),
 * in-form players get +2 to form clamped at 100.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_UpdateSinglePlayerPostMatchStats(uint8_t *playerSlotPtr)
{
    if (_isNationalSelector)
        return;

    /* Compute player index from raw offset */
    uint32_t offset = (uint32_t)(playerSlotPtr - (uint8_t *)&_careerTeam);
    uint16_t playerIdx = (uint16_t)(offset / 38);

    PlayerFile *pf = (PlayerFile *)(playerSlotPtr + 76);
    uint8_t positionAndFace = pf->positionAndFace;
    uint8_t form = (uint8_t)pf->form;

    if (!(positionAndFace & PF_INFORM)) {
        /* Player is NOT in form (bit 0 clear) */
        if (form == PLAYER_FORM_TRIALIST) {
            /* Trialist hired → demote to "on trial" ($9A) */
            pf->form = (int8_t)PLAYER_FORM_RESERVE;
            Career_UpdatePlayerWeeklyWageCache(playerIdx);
            return;
        }

        if (form != PLAYER_FORM_RESERVE)
            return;  /* Not trial — nothing to do */

        /* Form = $9A (on trial) — ~20% chance to promote */
        if (Rand() >= 0x33)
            return;

        /* Restore form from saved modifier table and set in-form bit */
        pf->form = (int8_t)_playerFormModifiers[playerIdx];
        pf->positionAndFace = positionAndFace | PF_INFORM;  /* bset #0 */
        Career_UpdatePlayerWeeklyWageCache(playerIdx);
        return;
    }

    /* Player is in form (bit 0 set) */
    form = (uint8_t)(form + 2);
    pf->form = (int8_t)form;

    if ((int8_t)form < 0) {
        /* Overflowed past 127 → clear in-form bit */
        pf->positionAndFace = positionAndFace & (uint8_t)~PF_INFORM;
    }

    /* Clamp form at 100 ($64) */
    if ((int8_t)pf->form > PLAYER_FORM_MAX) {
        pf->form = PLAYER_FORM_MAX;
    }
}

/* ================================================================
 * Career_ApplyPostMatchPlayerStatModsIfCareerTeam — apply post-match
 * stat mods to career team players.
 * Asm: Career_ApplyPostMatchPlayerStatModsIfCareerTeam (a0=team, a1=mods)
 *
 * Only processes if NOT national selector and team IS careerTeam.
 * Iterates 11 players (d7=10..0), uses currentMatchPlayers[d7] as
 * player file index, checks remap[fileIndex] — if negative, 20% chance
 * to use remap value as replacement modifier.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_ApplyPostMatchPlayerStatModsIfCareerTeam(TeamFileEx *team, uint8_t *mods)
{
    if (_isNationalSelector)
        return;
    if (team != &_careerTeam)
        return;

    uint8_t *teamRaw = (uint8_t *)team;
    int16_t d7;
    for (d7 = 0x0A; d7 >= 0; d7--) {
        int8_t d6 = (int8_t)_currentMatchPlayers[d7];
        uint8_t modByte = mods[(uint8_t)d6];

        if (!((int8_t)modByte < 0)) {
            /* modByte is non-negative — check 12.5% chance */
            if (Rand() >= 0x20)
                goto do_update;
            d6 = (int8_t)modByte;
        }

    do_update:
        {
            uint16_t d2 = (uint16_t)(uint8_t)d6;
            d2 = (uint16_t)(d2 * 38);  /* mulu.w #$26 */
            uint8_t *playerSlotPtr = teamRaw + d2;
            Career_UpdateSinglePlayerPostMatchStats(playerSlotPtr);
        }
    }
}

/* ================================================================
 * AddTrialPlayer — generate a random trial player and add to roster.
 * Asm: AddTrialPlayer (no params, result: a3=new player ptr)
 *
 * Picks a random position from trialPlayerPositionTable, then calls
 * TrainingMatch_AddTrialPlayerAtPosition.
 * Returns pointer to the new player (for accessing name at offset 79).
 * ================================================================ */
static const uint8_t trialPlayerPositionTable[] = {3,3,6,6,7,7,0,1,2,4,5,0};

__attribute__((used, externally_visible))
uint8_t *AddTrialPlayer(void)
{
    uint16_t r = Rand();
    uint16_t posIdx = (uint16_t)((uint16_t)(r * 11) >> 8);
    uint16_t position = (uint16_t)trialPlayerPositionTable[posIdx];
    return TrainingMatch_AddTrialPlayerAtPosition(position);
}

/* ================================================================
 * TrainingMatch_AddTrialPlayerAtPosition — add trial player at given position.
 * Asm: TrainingMatch_AddTrialPlayerAtPosition (d0=position)
 *
 * Finds unused player slot in team, sets up as trialist, inserts into
 * currentMatchPlayers array at correct position, assigns random form modifier.
 * Returns pointer to the new player record.
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *TrainingMatch_AddTrialPlayerAtPosition(uint16_t position)
{
    /* Find an unused player index (not in currentMatchPlayers[0..careerNumPlayers-1]) */
    uint16_t freeSlot = 0;
    while (1) {
        uint16_t np = _careerNumPlayers;
        uint16_t found = 0;
        uint16_t i;
        for (i = 0; i < np; i++) {
            if (_currentMatchPlayers[i] == (uint8_t)freeSlot) {
                freeSlot++;
                found = 1;
                break;
            }
        }
        if (!found)
            break;
    }

    /* Get the player record at that slot */
    uint8_t *player = GetPlayer(_selTeamNumber, freeSlot);
    PlayerFile *pf = (PlayerFile *)(player + 76);
    TrainingMatch_SaveSelectedPlayerId(freeSlot);

    /* Set shirtNumber as a flag (used as a marker here) */
    pf->shirtNumber = 0x10;

    /* Store position into positionAndFace (position << 5) */
    pf->positionAndFace = (uint8_t)(position << PF_POSITION_SHIFT);

    /* Convert player to trialist */
    Career_ConvertPlayerToTrialistFromName(_selTeamNumber, freeSlot);

    /* Determine insertion point in currentMatchPlayers */
    uint16_t insertOffset;
    {
        DIYCompetitionEx *compBufEx = (DIYCompetitionEx *)&_primaryCompetitionBuf;
        if (compBufEx->base.competitionType == 2) {
            insertOffset = (uint16_t)compBufEx->base.numTeamsInStage;
        } else {
            insertOffset = compBufEx->matchData.cup.roundLegs[0];
        }
        insertOffset += 0x0B;
    }

    /* Shift entries up to make room at insertOffset.
     * ASM: a0 = arr + careerNumPlayers, copies a0[-1]→a0[0], a0--, repeat.
     * This expands the array by one, shifting elements rightward. */
    {
        uint8_t *arr = _currentMatchPlayers;
        uint16_t endIdx = _careerNumPlayers;
        uint8_t *endPtr = &arr[endIdx];
        uint16_t count = endIdx - insertOffset;
        if (count > 0) {
            uint16_t i;
            for (i = 0; i < count; i++) {
                endPtr[-(int16_t)i] = endPtr[-1 - (int16_t)i];
            }
        }
        arr[insertOffset] = (uint8_t)freeSlot;
    }

    /* Assign random negative form modifier */
    {
        uint16_t r = Rand();
        uint16_t formVal = (uint16_t)((uint16_t)(r * 50) >> 8);  /* 0-49 */
        formVal += 40;   /* 40-89 */
        formVal = (uint16_t)(-(int16_t)formVal);  /* -40 to -89 */
        _playerFormModifiers[freeSlot] = (uint8_t)formVal;
    }

    _careerNumPlayers += 1;
    Career_UpdatePlayerWeeklyWageCache(freeSlot);

    return player;
}

/* ================================================================
 * CheckPlayersLeavingComing — check for random player arrivals/departures.
 * Asm: CheckPlayersLeavingComing (no params)
 *
 * If not national selector and not sacked:
 * - If roster < 30 and 25/256 chance → add trial player, show message
 * - Otherwise scan roster backwards for trialists ($9B form) and
 *   reserves ($9A form) with small random chance to leave
 * ================================================================ */
__attribute__((used, externally_visible))
void CheckPlayersLeavingComing(void)
{
    if (_isNationalSelector)
        return;
    if (_youreSacked)
        return;

    /* Try to add a trial player if roster not full */
    if (_careerNumPlayers < 30) {
        if (Rand() < 25) {
            uint8_t *newPlayer = AddTrialPlayer();
            PlayerFile *npf = (PlayerFile *)(newPlayer + 76);
            PlayerJoinedClubOnTrial((uint8_t *)npf->playerName);
            return;
        }
    }

    /* Scan roster backwards for players that might leave.
     *
     * Asm passes the *file index* (= currentMatchPlayers[d2]) to
     * RemovePlayerFromTeam, not the ordinal d2 — GetPlayerAtIndex
     * returns d1=fileIndex, and the asm uses that d1 directly across
     * the form check and into RemovePlayerFromTeam.
     *
     * Passing d2 (the ordinal) here would try to remove a career-roster
     * slot that may not exist (a "hole" left by previous removals),
     * causing GetPlayerSlotIndexByPlayerNumber to panic searching for
     * the missing value. */
    {
        int16_t d2;
        for (d2 = (int16_t)(_careerNumPlayers - 1); d2 >= 0; d2--) {
            uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)d2);
            uint16_t fileIndex = (uint16_t)_currentMatchPlayers[d2];
            PlayerFile *pf = (PlayerFile *)(player + 76);
            uint8_t form = (uint8_t)pf->form;

            if (form == PLAYER_FORM_TRIALIST) {
                /* Trial player ($9B) — 5/256 chance to leave */
                if (Rand() >= 5)
                    continue;
                TrialPlayerLeftClubMessage((uint8_t *)pf->playerName);
                goto remove;
            }

            if (form == PLAYER_FORM_RESERVE) {
                /* Reserve/loan player ($9A) — 2/256 chance to leave */
                if (Rand() >= 2)
                    continue;
                ReservePlayerLeftClubMessage((uint8_t *)pf->playerName);
                goto remove;
            }

            continue;

        remove:
            RemovePlayerFromTeam(fileIndex);
            return;  /* Only one removal per call */
        }
    }
}

/* ================================================================
 * Chunk 101: Club Business Init & Career Season Finance Init
 * ================================================================ */

/* Additional externs needed for Chunk 101 */
extern MenuEntry _menuEntries[];
extern int32_t _playerSales;
extern int32_t _playerPurchases;
extern uint16_t _contractNegotiationActive;
extern uint16_t _sensisoftPlcSponsorshipFactor;
extern int32_t _sensisoftPlcSponsorship;
extern uint16_t _additionalInvestments;
extern uint16_t _financeTotalsNeedResetFlag;
extern uint8_t _aTotalLoss[];
extern uint8_t _aBalanceAtStart[];
extern uint8_t _aBalanceAtEndOf[];
extern uint8_t _aSponsorshipFor[];
extern uint8_t _aSeasonFromSens[];
extern uint8_t _aAdditionalInve[];

/* Already-ported C functions used by Chunk 101 */
extern uint16_t GetTeamValueBase(uint16_t avgPrice);

/* Helper: menu entry from byte offset */

/* Helper: access bg as the high 16 bits (big-endian BACKANDFRAMECOLOR) */

/* Compute total profit/loss from all financial components */
static inline int32_t computeTotalProfitLoss(void) {
    int32_t total = _startingBalanceCopy;
    total += _competitionBonuses;
    total += _gateReceipts;
    total += _playerSales;
    total -= _playerPurchases;
    total -= _playerWagesBill;
    total -= _generalRunningCosts;
    return total;
}

/* Common: format new balance entry with color/flag adjustments if negative */
static void formatNewBalanceEntry(MenuEntry *e, int32_t balance) {
    if (balance < 0) {
        /* Set blinking flag (bit 13 of stringFlags) */
        e->stringFlags |= (1 << 13);
        /* Change color to RED_12 */
        uint16_t bg = e->bgColor;
        bg = (bg & 0xFFF0) | 12;
        e->bgColor = bg;
    }
    Int2PoundsSigned(balance, (uint8_t *)e->fg);
}

/* ================================================================
 * ClubBusinessInit  (SWOS2.s line 83165)
 *
 * Initializes the Club Business menu display. Three paths:
 * 1) contractNegotiationActive → full financial summary
 * 2) !stayingInOldClub → abbreviated (no gate receipts/costs)
 * 3) stayingInOldClub → extended with sponsorship info
 * ================================================================ */
__attribute__((used, externally_visible))
void ClubBusinessInit(void)
{
    if (_contractNegotiationActive) {
        /* ---- Path 1: Full financial summary ---- */
        /* Old balance */
        Int2PoundsSigned(_old_balance, (uint8_t *)_menuEntries[12].fg);
        /* Gate receipts */
        WriteAccountBalance(_gateReceipts, (uint8_t *)_menuEntries[14].fg);
        /* General running costs (negated) */
        WriteAccountBalance(-_generalRunningCosts, (uint8_t *)_menuEntries[15].fg);
        /* Player sales */
        WriteAccountBalance(_playerSales, (uint8_t *)_menuEntries[17].fg);
        /* Player purchases (negated) */
        WriteAccountBalance(-_playerPurchases, (uint8_t *)_menuEntries[18].fg);
        /* Player wages bill (negated) */
        WriteAccountBalance(-_playerWagesBill, (uint8_t *)_menuEntries[19].fg);

        /* Compute total profit/loss */
        int32_t total = computeTotalProfitLoss();
        if (total < 0) {
            _menuEntries[10].fg = (uintptr_t)_aTotalLoss;
        }

        /* Write total */
        WriteAccountBalance(total, (uint8_t *)_menuEntries[20].fg);

        /* New balance with red color if negative */
        formatNewBalanceEntry(&_menuEntries[21], _newBalance);
        return;
    }

    if (!_stayingInOldClub) {
        /* ---- Path 2: Abbreviated summary (no gate receipts etc.) ---- */
        MenuEntry *e70 = &_menuEntries[2];
        e70->y += 30;
        e70->fg = (uintptr_t)_aBalanceAtStart;

        MenuEntry *e2A0 = &_menuEntries[12];
        e2A0->y += 30;
        Int2PoundsSigned(_old_balance, (uint8_t *)e2A0->fg);

        /* Hide entries: E0, 118, 310, 348 */
        _menuEntries[4].isInvisible = 1;
        _menuEntries[5].isInvisible = 1;
        _menuEntries[14].isInvisible = 1;
        _menuEntries[15].isInvisible = 1;

        /* Player sales & purchases */
        WriteAccountBalance(_playerSales, (uint8_t *)_menuEntries[17].fg);
        WriteAccountBalance(-_playerPurchases, (uint8_t *)_menuEntries[18].fg);

        /* Hide: 1F8, 428 */
        _menuEntries[9].isInvisible = 1;
        _menuEntries[19].isInvisible = 1;

        /* Adjust Y positions */
        _menuEntries[10].y -= 15;

        /* Compute total */
        int32_t total = computeTotalProfitLoss();
        if (total < 0) {
            _menuEntries[10].fg = (uintptr_t)_aTotalLoss;
        }

        /* Write total */
        _menuEntries[20].y -= 15;
        WriteAccountBalance(total, (uint8_t *)_menuEntries[20].fg);

        _menuEntries[11].y -= 15;

        /* New balance with color adjustments */
        MenuEntry *e498 = &_menuEntries[21];
        e498->y -= 15;
        formatNewBalanceEntry(e498, _newBalance);
        return;
    }

    /* ---- Path 3: Extended with sponsorship ---- */
    if (_sensisoftPlcSponsorshipFactor) {
        /* Shift entries 0x70..0x70+9*56 up by 13 each */
        MenuEntry *e = &_menuEntries[2];
        for (int16_t i = 9; i >= 0; i--) {
            e->y -= 13;
            e++;
        }
        e = &_menuEntries[12];
        for (int16_t i = 9; i >= 0; i--) {
            e->y -= 13;
            e++;
        }
    }

    /* Set "BALANCE AT END OF LAST SEASON:" */
    MenuEntry *e70 = &_menuEntries[2];
    e70->fg = (uintptr_t)_aBalanceAtEndOf;
    e70->y += 5;

    MenuEntry *e2A0 = &_menuEntries[12];
    e2A0->y += 5;
    Int2PoundsSigned(_old_balance, (uint8_t *)e2A0->fg);

    /* Show entries: A8, 2D8 */
    _menuEntries[3].isInvisible = 0;
    _menuEntries[13].isInvisible = 0;

    /* Competition bonuses */
    WriteAccountBalance(_competitionBonuses, (uint8_t *)_menuEntries[13].fg);

    /* Shift entries E0..E0+7*56 down by 15 */
    {
        MenuEntry *e = &_menuEntries[4];
        for (int16_t i = 7; i >= 0; i--) {
            e->y += 15;
            e++;
        }
    }
    {
        MenuEntry *e = &_menuEntries[14];
        for (int16_t i = 7; i >= 0; i--) {
            e->y += 15;
            e++;
        }
    }

    /* Set sponsorship text */
    MenuEntry *eE0 = &_menuEntries[4];
    eE0->fg = (uintptr_t)_aSponsorshipFor;
    eE0->y += 5;

    _menuEntries[5].fg = (uintptr_t)_aSeasonFromSens;

    /* Starting balance copy */
    _menuEntries[14].y += 10;
    _menuEntries[15].isInvisible = 1;
    WriteAccountBalance(_startingBalanceCopy, (uint8_t *)_menuEntries[14].fg);

    if (_sensisoftPlcSponsorshipFactor) {
        /* Show sponsorship entries */
        _menuEntries[6].isInvisible = 0;
        _menuEntries[16].isInvisible = 0;
        WriteAccountBalance(_sensisoftPlcSponsorship, (uint8_t *)_menuEntries[16].fg);

        if (_additionalInvestments) {
            _menuEntries[6].fg = (uintptr_t)_aAdditionalInve;
        }

        /* Shift entries 188..188+4*56 down by 15 */
        {
            MenuEntry *e = &_menuEntries[7];
            for (int16_t i = 4; i >= 0; i--) {
                e->y += 15;
                e++;
            }
        }
        {
            MenuEntry *e = &_menuEntries[17];
            for (int16_t i = 4; i >= 0; i--) {
                e->y += 15;
                e++;
            }
        }
    }

    /* Player sales & purchases */
    WriteAccountBalance(_playerSales, (uint8_t *)_menuEntries[17].fg);
    WriteAccountBalance(-_playerPurchases, (uint8_t *)_menuEntries[18].fg);

    /* Hide: 1F8, 428 */
    _menuEntries[9].isInvisible = 1;
    _menuEntries[19].isInvisible = 1;

    /* Adjust and compute total */
    _menuEntries[10].y -= 15;

    int32_t total = computeTotalProfitLoss();
    if (total < 0) {
        _menuEntries[10].fg = (uintptr_t)_aTotalLoss;
    }

    _menuEntries[20].y -= 15;
    WriteAccountBalance(total, (uint8_t *)_menuEntries[20].fg);

    _menuEntries[11].y -= 15;

    MenuEntry *e498 = &_menuEntries[21];
    e498->y -= 15;
    formatNewBalanceEntry(e498, _newBalance);
}

/* ================================================================
 * Career_InitializeNewSeasonFinances  (SWOS2.s line 83433)
 *
 * Initializes financial state for a new season: recalculates wages,
 * resets accumulators, handles sponsorship and competition bonuses.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_InitializeNewSeasonFinances(void)
{
    /* Recalculate all player wages */
    for (int16_t i = (int16_t)(_careerNumPlayers - 1); i >= 0; i--) {
        GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
        Career_UpdatePlayerWeeklyWageCache((uint16_t)i);
    }

    /* Transfer newBalance → old_balance and clear accumulators */
    _old_balance = _newBalance;
    _playerPurchases = 0;
    _playerSales = 0;
    _playerWagesBill = 0;
    _generalRunningCosts = 0;
    _gateReceipts = 0;
    _sensisoftPlcSponsorship = 0;

    if (_stayingInOldClub) {
        _additionalInvestments = 0;
        uint16_t d7 = _sensisoftPlcSponsorshipFactor;

        if (d7 == 0) {
            /* 1/8 chance of getting sponsorship */
            if (Rand2() >= 0x20) {
                goto skipSponsorship;
            }
            d7 = 0x10;
            _additionalInvestments = 1;
        }

        /* Random sponsorship level: 1..32, capped at d7 */
        {
            uint16_t rnd = Rand2();
            rnd = (rnd & 0x1F) + 1;
            _sensisoftPlcSponsorshipFactor = 0;
            if (rnd > d7) {
                goto skipSponsorship;
            }
            _sensisoftPlcSponsorshipFactor = rnd;
            d7 = rnd;
        }

        /* Calculate sponsorship amount based on team value */
        {
            VerifyTeamLoaded(_selTeamNumber);
            uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
            uint32_t d0 = (uint32_t)GetTeamValueBase(avgPrice);
            d0 = d0 * d7;

            if (d0 > 10000) {
                d0 = 10000;
                uint16_t rnd = Rand2() & 0xF;
                rnd = rnd * 100;
                d0 = 10000 - rnd;
            }

            d0 = d0 * 1000;
            _sensisoftPlcSponsorship = (int32_t)d0;
            _newBalance += (int32_t)d0;
        }
    }

skipSponsorship:
    /* Competition bonuses */
    _competitionBonuses = 0;

    if (_stayingInOldClub && _quaternaryCompetitionPtr != 0) {
        int16_t bonus = (int8_t)_euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue;
        uint32_t d0;

        if (_euroCupSlotIndex == 1) {
            d0 = (uint32_t)((uint16_t)bonus) * 62644;
        } else if (_euroCupSlotIndex == 2) {
            d0 = (uint32_t)((uint16_t)bonus) * 41668;
            d0 <<= 1;
        } else {
            /* European Cup (0) or Copa Libertadores (3): euroCup-tier bonus. */
            d0 = (uint32_t)((uint16_t)(bonus << 1)) * 50011;
        }

        d0 = (d0 / 1000) * 1000;
        _competitionBonuses += (int32_t)d0;
    }

    if (_stayingInOldClub) {
        int16_t bonus = (int8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue;
        uint32_t d0;

        if (_currentDivisionIndex == 0) {
            d0 = (uint32_t)((uint16_t)(bonus << 1)) * 0x84D0;
        } else if (_currentDivisionIndex == 1) {
            d0 = (uint32_t)((uint16_t)bonus) * 0x7E61;
        } else if (_currentDivisionIndex == 2) {
            d0 = (uint32_t)((uint16_t)bonus) * 16667;
        } else {
            d0 = (uint32_t)((uint16_t)bonus) * 11112;
        }

        d0 = (d0 / 1000) * 1000;
        _competitionBonuses += (int32_t)d0;
    }

    /* Apply competition bonuses and starting balance */
    _newBalance += _competitionBonuses;
    _startingBalanceCopy = _startingBalance;
    _newBalance += _startingBalanceCopy;

    _financeTotalsNeedResetFlag = 1;
    _clubBusinessActive = 0;
}

/* ================================================================
 * Career_HandleRetirement  (SWOS2.s line 36190)
 *
 * Sets retired flag, then runs end-of-season career updates:
 * board confidence modifiers/score, reputation, job offers, history.
 * ================================================================ */
extern uint16_t _retired;
extern void Career_FillCurrentSeasonInfo(void);
extern void Career_AutoAdvanceCompetitionAndPersistSlotState(uint8_t *compPtr);
extern uint16_t _numJobOffers;
extern uint16_t _numNewJobOffers;

__attribute__((used, externally_visible))
void Career_HandleRetirement(void)
{
    _retired = 1;
    UpdateBoardConfidenceModifiers();
    UpdateCareerReputationBonuses();
    UpdateBoardConfidenceScore();
    Career_MaybeGenerateJobOffer();
    PushBoardConfidenceHistory();
}

/* ================================================================
 * Career_PostMatchUpdate  (SWOS2.s line 35371)
 *
 * Called after each career match. Fills season info, removes expired
 * job offers, generates 1-2 new job offers (up to 100 attempts),
 * then auto-advances all competition slots. Falls through to
 * Career_HandleRetirement at the end.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_PostMatchUpdate(void)
{
    /* Fill current season info (preserves all regs in asm) */
    Career_FillCurrentSeasonInfo();

    /* Remove expired job offers */
    JobOffers_RemoveExpiredOffers();

    /* Decide how many job offers to target: 1 or 2 */
    uint16_t target = (Rand2() & 1) + 1;

    /* Try up to 100 times to generate enough job offers */
    for (int16_t i = 99; i >= 0; i--) {
        uint16_t total = _numJobOffers + _numNewJobOffers;
        if (total >= target)
            break;
        Career_MaybeGenerateJobOffer();
    }

#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate ENTER isNationalSelector=%u "
                      "currentCompPtr=%p secondaryCompPtr=%p tertiaryCompPtr=%p "
                      "quaternaryCompPtr=%p youreSacked=%u",
                      (unsigned)_isNationalSelector,
                      (void *)(uintptr_t)_currentCompetitionPtr,
                      (void *)(uintptr_t)_secondaryCompetitionPtr,
                      (void *)(uintptr_t)_tertiaryCompetitionPtr,
                      (void *)(uintptr_t)_quaternaryCompetitionPtr,
                      (unsigned)_youreSacked);
#endif

    /* If national selector, skip competition auto-advance */
    if (_isNationalSelector != 0) {
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate EXIT (isNationalSelector — no auto-advance)");
#endif
        Career_HandleRetirement();
        return;
    }

    /* Save and temporarily override selTeamNumber and teamControls */
    uint16_t savedSelTeam = _selTeamNumber;
    uint8_t savedControls = _careerTeam.team.teamControls;

    _selTeamNumber = 0xFFFF;
    _careerTeam.team.teamControls = 1;

    /* Auto-advance all competition slots */
#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate -> AutoAdvance(currentCompetitionPtr=%p)",
                      (void *)(uintptr_t)_currentCompetitionPtr);
#endif
    Career_AutoAdvanceCompetitionAndPersistSlotState((uint8_t *)_currentCompetitionPtr);

    if (_secondaryCompetitionPtr != 0) {
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate -> AutoAdvance(secondaryCompetitionPtr=%p)",
                          (void *)(uintptr_t)_secondaryCompetitionPtr);
#endif
        Career_AutoAdvanceCompetitionAndPersistSlotState((uint8_t *)_secondaryCompetitionPtr);
    }

    if (_tertiaryCompetitionPtr != 0) {
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate -> AutoAdvance(tertiaryCompetitionPtr=%p)",
                          (void *)(uintptr_t)_tertiaryCompetitionPtr);
#endif
        Career_AutoAdvanceCompetitionAndPersistSlotState((uint8_t *)_tertiaryCompetitionPtr);
    }

    if (_quaternaryCompetitionPtr != 0) {
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate -> AutoAdvance(quaternaryCompetitionPtr=%p)",
                          (void *)(uintptr_t)_quaternaryCompetitionPtr);
#endif
        Career_AutoAdvanceCompetitionAndPersistSlotState((uint8_t *)_quaternaryCompetitionPtr);
    }

    /* Restore saved values */
    _careerTeam.team.teamControls = savedControls;
    _selTeamNumber = savedSelTeam;

#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug4-cup] PostMatchUpdate EXIT (after all 4 AutoAdvance calls)");
#endif

    /* Fall through to Career_HandleRetirement (bra.w *+4) */
    Career_HandleRetirement();
}

/* --- Chunk 179: Career Auto-Advance Chain --- */

extern uint16_t _careerAutoAdvanceInProgress;
/* Career_AutoAdvanceCompetitionSlots — forward declaration (ported below) */

/* Career_AutoAdvanceCompetitionAndPersistSlotState (SWOS2.s line 35559)
 * Takes a competition pointer, identifies the corresponding context buffer,
 * loads slot state, dispatches to the appropriate auto-advance function
 * based on competition type, then stores the slot state back. */
__attribute__((used, externally_visible))
void Career_AutoAdvanceCompetitionAndPersistSlotState(uint8_t *compPtr)
{
    _careerAutoAdvanceInProgress = 1;

    /* Find the matching competition context buffer */
    uint8_t *ctx = (uint8_t *)&_primaryCompetitionBuf;
    if (compPtr != (uint8_t *)_currentCompetitionPtr) {
        ctx = (uint8_t *)&_competitionContextBuf;
        if (compPtr != (uint8_t *)_secondaryCompetitionPtr) {
            ctx = (uint8_t *)&_tertiaryCompetitionContextBuf;
            if (compPtr != (uint8_t *)_tertiaryCompetitionPtr) {
                ctx = (uint8_t *)&_euroCupsCompetitionContextBuf;
            }
        }
    }

    Career_LoadCompetitionSlotStateFromBuffer(ctx);

    /* Dispatch by competition type. The asm reads `1(a0)` which on m68k
     * BE is the LOW byte of the uint16 type word — i.e. the actual type
     * value (0=league, 1=cup, 2=hybrid). On x86 LE the same byte index
     * picks the HIGH byte instead, which is always zero for legal types,
     * so the previous `ctx[1]` always evaluated to 0 and every comp was
     * mis-dispatched to AutoAdvanceLeagueFixtures. Read as a host-native
     * uint16 and take the low byte — works on both endians. */
    uint8_t compType = (uint8_t)*(uint16_t *)ctx;

    /* Sanity-check the loaded competition state. If numTeams or
     * totalMatchesInRound is out of range, the context buffer was
     * never properly populated for this slot — most likely the
     * competition pointers in this CAR/save reference a competition
     * the user isn't really in (FIRED.CAR repro had spanishLeague /
     * spanishCup pointing here for a manager who never owned a
     * Spanish team). Running the auto-advance with garbage causes
     * either a SIGSEGV inside getCurrentFixtureTeamPtrs (huge
     * totalMatchesInRound makes d5 = subRoundCounter * totalMatches
     * overflow into invalid memory) or an infinite loop in
     * Career_AutoAdvanceLeagueFixtures' main_loop (state never
     * progresses past the loop's break conditions).
     *
     * Skip the dispatch entirely on garbage state — the
     * persist-back at the bottom still runs so we don't corrupt
     * the context further, but the actual sim is bypassed. One-shot
     * log so we know which competition was bad. */
    {
        DIYCompetition *dcs = &_diyCompetitionStart.base;
        int16_t numTeams = dcs->numTeams;
        int16_t totalInRound = dcs->totalMatchesInRound;
        int bail = 0;
        const char *bailReason = 0;
        uint32_t bailTeamSelPtr = 0;

        if (numTeams <= 0 || numTeams > LEAGUE_PHASE_CLUBS) {   /* (a world's league phase: 36) */
            bail = 1;
            bailReason = "numTeams out of range";
        } else if (totalInRound <= 0 || totalInRound > 32) {
            bail = 1;
            bailReason = "totalMatchesInRound out of range";
        } else if (compType == 0) {
            /* Standings-row validation only applies to LEAGUE comps
             * (type=0). For cup (type=1) and hybrid (type=2) comps,
             * `diyCompetitionStart+0x2B4` overlays diyCompetitionStart.cupTeamIds /
             * diyTeamGridPairings / diyTeamShuffleOrder / audioInstrumentBank
             * — see project memory `diycomp_league_table_overlay`. Reading
             * the first 4 bytes of that region as an int32 "teamSelOffset"
             * for a cup yields garbage like 0x0D0C0B0A (sequential team
             * grid indices), which would fail any sane offset range and
             * mis-flag the cup as corrupt. The Coppa Italia and European
             * Cup post-sack auto-advance regressions trace to exactly
             * this false positive: Playaveli's repro showed bail #1
             * compType=0 numTeams=32 bailTeamSelPtr=0x1A151E0B (Coppa)
             * and bail #2 compType=0 numTeams=8 bailTeamSelPtr=0x0D0C0B0A
             * (CL hybrid). Both reports had compType=0 wrongly because
             * of the byte-order bug fixed above; with the real type now
             * dispatched correctly, this guard naturally limits itself
             * to leagues where the standings rows actually exist.
             *
             * Each row is 18 bytes; teamSelOffset at offset 0 is now a team
             * IDENTITY (step-4 phase 3): -1 == careerTeam, >= 0 == selTeamsPtr
             * index (0..numTeams-1). The legacy byte-offset range below
             * [maxNegative, maxPositive] still bounds it correctly — every valid
             * identity (-1 and small positives) sits well inside it — and still
             * catches the MB-class garbage that motivated the validator (a cup
             * comp's standings overlay read as sequential grid indices). Left as
             * the wide legacy range to keep this a pure corruption guard; it
             * could be tightened to [-1, numTeams) now that the field is an id. */
            const int32_t maxPositive = 64 * 684;
            const int32_t maxNegative = -1 * 1024 * 1024;
            uint8_t *rowsBase = (uint8_t *)&_diyCompetitionStart + DIY_STANDINGS_OFFSET;
            for (int16_t i = 0; i < numTeams; i++) {
                int32_t teamSelOff = *(int32_t *)(rowsBase + i * 18);
                if (teamSelOff > maxPositive || teamSelOff < maxNegative) {
                    bail = 1;
                    bailReason = "standings row teamSelOffset out of range";
                    bailTeamSelPtr = (uint32_t)teamSelOff;
                    break;
                }
            }
        }

        if (bail) {
#ifdef SWOS_DEBUG_AUTOSKIP
            /* Log up to 20 bails (not just first) so we can see whether the
             * player's secondary/tertiary cups are bailing silently. */
            static int s_bad_state_count = 0;
            if (s_bad_state_count++ < 20) {
                SWOS_AUTOSKIP_LOG("[bug1] AutoAdvanceCompetition: bail #%d — "
                                  "compPtr=%p ctx=%p compType=%u numTeams=%d "
                                  "totalMatchesInRound=%d reason='%s' "
                                  "bailTeamSelPtr=0x%08X",
                                  s_bad_state_count,
                                  (void *)compPtr, (void *)ctx,
                                  (unsigned)compType,
                                  (int)numTeams, (int)totalInRound,
                                  bailReason ? bailReason : "?",
                                  (unsigned)bailTeamSelPtr);
            }
#endif
            Career_StoreCompetitionSlotStateToBuffer(ctx);
            _careerAutoAdvanceInProgress = 0;
            return;
        }
    }

    if (compType == 1) {
        Career_AutoAdvanceCupMatches();
    } else if (compType == 2) {
        Career_AutoAdvanceCompetitionSlots();
    } else {
        Career_AutoAdvanceLeagueFixtures();
    }

    Career_StoreCompetitionSlotStateToBuffer(ctx);
    _careerAutoAdvanceInProgress = 0;
}

extern uint16_t _careerLoadedFlag;
extern uint16_t _internationalAutoAdvanceFlag;
extern uint16_t _teamSelectionKeyCacheEnd;
/* viewRandomPhase already declared above as uint8_t */
extern uint8_t  _seasonPlaying;
extern uint8_t  _englishCup[];
extern uint8_t  _englishCup_1[];
extern uint8_t  _englishLeagueCup[];
extern uint8_t  _englishLeagueCup_1[];

extern void SaveRand2State(void);
extern void RestoreRand2State(void);
extern void Randomize2(uint16_t _seed);
extern void Career_InitCompetitionTeamSelectionFromTableHeaderSpec(uint8_t *record);
extern void Career_InitCompetitionTeamSelectionFromAltTableHeaderSpec(uint8_t *record);
extern void Career_ApplyPendingForeignMarketPlayersToSelectedTeams(void);
extern void InitDIYCupFromRecord(uint8_t *record);
extern void InitDIYLeagueFromRecord(uint8_t *record);
extern void InitDIYCompetitionFromTableEntry(uint16_t index, uint8_t *record);
extern void InitTeams(void);
extern void Career_InitCompetitionTeamSelectionFromTableEntrySpec(uint8_t *record, uint16_t index);

/* Career_AutoAdvanceCupCompetitionFromRecord (SWOS2.s line 35246)
 * Auto-advances a cup competition: initializes team selection from record,
 * loads competition, runs cup match advancement, then restores RNG state.
 * a0 = competition record pointer. */
__attribute__((used, externally_visible))
void Career_AutoAdvanceCupCompetitionFromRecord(uint8_t *record)
{
    /* Special-case English cup aliases (118/119 → 120/121). */
    uint8_t rid = record ? comp_id(record) : 0xFFu;
    if (rid == COMPID_englishCup)
        record = _englishCup_1;
    else if (rid == COMPID_englishLeagueCup)
        record = _englishLeagueCup_1;

    _careerLoadedFlag = 1;

    if (_seasonUpdateInProgress == 0 && _internationalAutoAdvanceFlag == 0)
        _teamSelectionKeyCacheEnd = 1;

    _teamsLoaded = 0;
    SaveRand2State();

    /* Save viewRandomPhase + seasonPlaying (contiguous bytes, saved as word in asm) */
    uint8_t savedViewRandom = _viewRandomPhase;
    uint8_t savedSeasonPlaying = _seasonPlaying;

    /* Seed RNG from record byte and seasonPlaying */
    uint16_t seed = ((uint16_t)record[0] << 8) | (uint16_t)_seasonPlaying;
    Randomize2(seed);

    _numSelectedTeams = 0;
    Career_InitCompetitionTeamSelectionFromTableHeaderSpec(record);
    Career_ApplyPendingForeignMarketPlayersToSelectedTeams();
    InitDIYCupFromRecord(record);
    InitTeams();
    Career_AutoAdvanceCupMatches();

    _viewRandomPhase = savedViewRandom;
    _seasonPlaying = savedSeasonPlaying;
    RestoreRand2State();
    _careerLoadedFlag = 0;
}

/* Career_AutoAdvanceLeagueCompetitionFromRecord (SWOS2.s line 35291)
 * Same as above but for league competitions. */
__attribute__((used, externally_visible))
void Career_AutoAdvanceLeagueCompetitionFromRecord(uint8_t *record)
{
    _careerLoadedFlag = 1;

    if (_seasonUpdateInProgress == 0 && _internationalAutoAdvanceFlag == 0)
        _teamSelectionKeyCacheEnd = 1;

    _teamsLoaded = 0;
    SaveRand2State();

    uint8_t savedViewRandom = _viewRandomPhase;
    uint8_t savedSeasonPlaying = _seasonPlaying;

    uint16_t seed = ((uint16_t)record[0] << 8) | (uint16_t)_seasonPlaying;
    Randomize2(seed);

    _numSelectedTeams = 0;
    Career_InitCompetitionTeamSelectionFromAltTableHeaderSpec(record);
    Career_ApplyPendingForeignMarketPlayersToSelectedTeams();
    InitDIYLeagueFromRecord(record);
    InitTeams();
    Career_AutoAdvanceCompetitionSlots();

    _viewRandomPhase = savedViewRandom;
    _seasonPlaying = savedSeasonPlaying;
    RestoreRand2State();
    _careerLoadedFlag = 0;
}

/* Career_AutoAdvanceInternationalCompetitionFixtures (SWOS2.s line 35208)
 * Auto-advances an international competition: initializes team selection from
 * table entry spec, loads competition from table entry, runs league fixture
 * advancement, then restores RNG state.
 * a0 = competition record pointer, d0 = table index. */
__attribute__((used, externally_visible))
void Career_AutoAdvanceInternationalCompetitionFixtures(uint8_t *record, uint16_t index)
{
    _careerLoadedFlag = 1;
    if (_seasonUpdateInProgress == 0 && _internationalAutoAdvanceFlag == 0)
        _teamSelectionKeyCacheEnd = 1;
    _teamsLoaded = 0;
    SaveRand2State();
    uint8_t savedViewRandom = _viewRandomPhase;
    uint8_t savedSeasonPlaying = _seasonPlaying;

    uint16_t seed = ((uint16_t)record[0] << 8) | (uint16_t)_seasonPlaying;
    Randomize2(seed);
    _numSelectedTeams = 0;
    Career_InitCompetitionTeamSelectionFromTableEntrySpec(record, index);
    Career_ApplyPendingForeignMarketPlayersToSelectedTeams();
    InitDIYCompetitionFromTableEntry(index, record);
    InitTeams();
    Career_AutoAdvanceLeagueFixtures();

    _viewRandomPhase = savedViewRandom;
    _seasonPlaying = savedSeasonPlaying;
    RestoreRand2State();
    _careerLoadedFlag = 0;
}

/* ================================================================
 * UpdateCareerPlayersFormAfterMatch (SWOS2.s line 27475, Chunk 188)
 *
 * Adjusts career team players' form (someFlag field) after a match.
 * Considers: goals scored, injuries, position type, match performance,
 * comparison of current form vs expected form.
 *
 * ASM register interface:
 *   a0 = competition context (diyCompetitionStart or similar)
 *   a1 = team1 pointer, a2 = team2 pointer
 *   d1 = team1 goals, d2 = team2 goals
 * ================================================================ */
extern uint8_t _team1GoalCounterByPlayer[];   /* 32 bytes: goal count per player slot (team 1) */
extern uint8_t _team2GoalCounterByPlayer[];   /* 32 bytes: goal count per player slot (team 2) */
extern uint16_t _avgPlayerPriceHalfClamped;
extern uint8_t _playerFormDeltaRandomTable[]; /* 144 bytes: 18×8 lookup table */

__attribute__((used, externally_visible))
void UpdateCareerPlayersFormAfterMatch(DIYCompetitionEx *comp, TeamFileEx *team1, TeamFileEx *team2,
                                         uint16_t team1Goals, uint16_t team2Goals)
{
    if (_isNationalSelector)
        return;

    /* Select goal counter based on which team is the career team.
     * ASM: if a1 == careerTeam, use team1GoalCounterByPlayer; else swap d1/d2 and use team2. */
    uint8_t *_goalCounter = _team1GoalCounterByPlayer;
    uint16_t d1 = team1Goals;
    uint16_t d2 = team2Goals;
    if (team1 != &_careerTeam) {
        uint16_t tmp = d1;
        d1 = d2;
        d2 = tmp;
        _goalCounter = _team2GoalCounterByPlayer;
    }

    /* d6 = substitutes count from competition context */
    uint16_t d6 = (uint16_t)comp->base.substitutesCount;  /* 0x3C */

    /* d7 = max match player index.
     * Primary competition (comp == diyCompetitionStart): use pointsForAWin (0x62)
     * Other: use roundLegs[0] (0x66) */
    uint16_t d7;
    if (comp == &_diyCompetitionStart) {
        d7 = comp->pointsForAWin;  /* 0x62 */
    } else {
        d7 = comp->matchData.cup.roundLegs[0];   /* 0x66 */
    }
    d7 += 11;

    /* Get average player price for career team, halve and clamp to 17 */
    uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
    avgPrice >>= 1;
    if (avgPrice > 0x11)
        avgPrice = 0x11;
    _avgPlayerPriceHalfClamped = avgPrice;

    /* Loop through all career players */
    for (uint16_t d3 = 0; d3 < _careerNumPlayers; d3++) {
        int16_t slot = (int8_t)_currentMatchPlayers[d3];
        uint8_t *goalCounterForPlayer = _goalCounter + slot;

        PlayerFile *pl = &_careerTeam.players[slot];
        uint8_t plSomeFlag = (uint8_t)pl->form;

        /* Skip trialist/reserve leaving special states */
        if (plSomeFlag == PLAYER_FORM_TRIALIST || plSomeFlag == PLAYER_FORM_RESERVE)
            goto nextPlayer;

        int8_t d4;  /* form delta to apply */

        /* Check if player is injured */
        if (IsPlayerInjured(pl - 2, (int16_t)d6)) {
            /* IsPlayerInjured takes the TeamFile-relative player base; the
             * Amiga asm passes the slot-base (careerTeam + slot*38, two
             * PlayerFile records before the players[] start). Preserve
             * that calling convention. */
            /* Injured: 33% chance of -1 form */
            if (Rand2() > 0x33)
                goto clampForm;
            d4 = -1;
            goto applyGoalBonus;
        }

        if (d3 == 0) {
            /* Goalkeeper (index 0) */
            if (Rand2() > 0x33)
                goto clampForm;
            if (d2 == 0) {
                d4 = 4;  /* clean sheet: +4 */
            } else {
                /* Goals taken: delta = -(goals taken).
                 * ASM: muls.w #$FFFF,d4 (multiply by -1) */
                d4 = (int8_t)(-((int16_t)d2));
            }
            goto applyGoalBonus;
        } else if (d3 < 11) {
            /* Starting outfield player (index 1-10): form-based adjustment */

            /* Expected form: someFlag/10 + price */
            int8_t sf = (int8_t)plSomeFlag;
            int16_t sfExt = (int16_t)sf;
            int32_t sfLong = (int32_t)sfExt;
            int16_t div10 = (int16_t)(sfLong / 10);  /* divs.w #$A */

            uint8_t price = (uint8_t)pl->price;
            uint8_t expected = (uint8_t)div10 + price;  /* add.b */

            /* formDelta = field_1 - expected */
            uint8_t field1 = (uint8_t)pl->cachedMatchValue;
            int8_t formDelta = (int8_t)(field1 - expected);  /* sub.b */

            int16_t d4s = (int16_t)formDelta;

            if (d4s >= 0) {
                /* Performing above expectations: positive random adjustment */
                if (d4s > 0x11) d4s = 0x11;
                uint16_t r = Rand();
                d4s <<= 3;
                uint16_t rIdx = r & 7;
                d4 = (int8_t)_playerFormDeltaRandomTable[d4s + rIdx];
            } else {
                /* Performing below expectations: negative random adjustment */
                d4s = -d4s;
                if (d4s > 0x11) d4s = 0x11;
                uint16_t r = Rand();
                d4s <<= 3;
                uint16_t rIdx = r & 7;
                d4 = -(int8_t)_playerFormDeltaRandomTable[d4s + rIdx];
            }
            goto applyGoalBonus;
        } else if (d3 < d7) {
            /* Substitute who played (index 11..d7-1) */
            if (Rand2() > 0x33)
                goto clampForm;

            uint8_t posType = (((uint8_t)pl->positionAndFace) & PF_POSITION_MASK) >> 5;
            if (posType == 0) {
                /* GK sub: random ±1 */
                uint16_t r = Rand2();
                d4 = (int8_t)(r & 1);
                d4 = -d4;  /* 0→0 or 1→-1 */
            } else {
                /* Outfield sub: -1 */
                d4 = -1;
            }
            goto applyGoalBonus;
        } else {
            /* Unused substitute (index >= d7): random small form change */
            if (Rand2() > 0x55)
                goto clampForm;
            uint16_t r = Rand2();
            d4 = (int8_t)(r & 1);
            d4 = -d4;
            d4 -= 1;  /* 0→-1 or 1→-2 */
            goto applyGoalBonus;
        }

applyGoalBonus:
        /* Goal scorer bonus */
        {
            uint8_t goals = *goalCounterForPlayer;
            if (goals != 0) {
                d4 += 1;
                if ((Rand2() & 1) == 0)
                    d4 += 1;
                if (goals != 1) {
                    if ((Rand2() & 3) == 0)
                        d4 += 1;
                }
            }
        }

        /* Clamp delta to [-maxDelta..+maxDelta] where maxDelta = random 1 or 2 */
        {
            uint16_t r = Rand2();
            int16_t maxDelta = (int16_t)((r & 1) + 1);
            if ((int8_t)d4 > (int8_t)maxDelta)
                d4 = (int8_t)maxDelta;
            int8_t negMax = (int8_t)(-maxDelta);
            if ((int8_t)d4 < negMax)
                d4 = negMax;
        }

        /* Apply delta to someFlag.
         * ASM has dead code checking d0 against 0x9B/0x9A (impossible values).
         * Replicated as no-op. */
        pl->form = (int8_t)((int8_t)pl->form + d4);

clampForm:
        /* Clamp someFlag to [-100..100] signed (0x9C..0x64 unsigned) */
        if ((int8_t)pl->form < (int8_t)PLAYER_FORM_MIN)
            pl->form = (int8_t)PLAYER_FORM_MIN;
        if ((int8_t)pl->form > (int8_t)PLAYER_FORM_MAX)
            pl->form = (int8_t)PLAYER_FORM_MAX;

nextPlayer:
        ;
    }
}

/* ===================================================================
 * Chunk 189: Career_UpdateStatsAfterMatch  (SWOS2.s line 32584)
 *
 * Updates board confidence modifiers, win/loss/draw/goals stats,
 * and stores match result into the competition slot record.
 *
 * ASM register inputs:
 *   a1 = team1 ptr, a2 = team2 ptr
 *   d0 = competition type (0=league, non-zero=cup)
 *   d4 = packed goals (team1<<8 | team2)
 *   d5 = aggregate score (word)
 *   d6 = penalty score packed (team1<<8 | team2)
 *   d7 = flags (bit 3 = went to penalties; low byte stored to result)
 * =================================================================== */
extern uint8_t _teamSelectionKeyCache[];
extern uint16_t _teamSelectionKeyCacheOffset;
extern uint16_t _boardConfidenceMod0;
extern uint16_t _boardConfidenceMod1;
extern uint16_t _boardConfidenceMod2;
extern uint16_t _boardConfidenceMod3;
extern uint16_t _boardConfidenceMod4;
extern uint16_t _boardConfidenceMod5;
extern uint16_t _boardConfidenceMod6;
extern uint16_t _boardConfidenceMod7;
extern uint16_t _boardConfidenceMod8;
extern uint16_t _boardConfidenceMod9;
extern uint16_t _boardConfidenceValue0;
extern uint16_t _boardConfidenceValue1;
extern uint16_t _gamesPlayed;
extern uint16_t _numWins;
extern uint16_t _numDrawn;
extern uint16_t _numLost;
extern uint16_t _goalsScored;
extern uint16_t _goalsTaken;
extern uint16_t _careerSlotProcessingInProgress;
extern uint32_t _aEuropeanTour;  /* string label — used as address boundary */
extern uint32_t _aOceaniaTour;   /* string label — used as address boundary */

extern uint16_t _seasonNumberOfSquads;
extern uint16_t _seasonSquadTeamNumbers[];

/* Does this fixture belong to a team the PLAYER controls — i.e. should it
 * consume one of the round's keycache match slots?
 *
 * Career (gameType 4): the one career team, selTeamNumber.
 *
 * Season (gameType 3): selTeamNumber is NEVER assigned. SeasonMenu calls
 * InitCareerVariables(GAME_TYPE_SEASON), which sets it to 0xFFFF, and the only
 * writers (Career_InitializeSelectedTeamAndStartingBalance, via InitCareer /
 * Career_ContinueToNextSeasonInitTeamAndFunds) are career-only paths — so it
 * stays 0xFFFF for the whole season. The human-controlled teams live in the
 * seasonSquadTeamNumbers list instead, which InitNewSeason fills from the
 * non-computer selected teams using the same PACK_BE_U16 {country, ordinal}
 * key we compare against here.
 *
 * Getting this wrong is not cosmetic: a bare selTeamNumber test is false for
 * EVERY season match, so the caller's store block never runs — matchFlags /
 * packedGoals are never written (the season fixture list keeps rendering
 * "VALENCIA V SEVILLA" for matches already played) and remainingElements never
 * decrements (Career_PlayMatchMenuLoop then takes its no-advance path, so the
 * list cursor desyncs from the real league fixture index).
 */
static int KeyCacheFixtureIsPlayerControlled(uint16_t t1num, uint16_t t2num)
{
    if (t1num == _selTeamNumber || t2num == _selTeamNumber)
        return 1;

    if (_gameType == GAME_TYPE_SEASON) {
        for (uint16_t i = 0; i < _seasonNumberOfSquads; i++) {
            uint16_t id = _seasonSquadTeamNumbers[i];
            if (id == t1num || id == t2num)
                return 1;
        }
    }
    return 0;
}

__attribute__((used, externally_visible))
void Career_UpdateStatsAfterMatch(TeamFileEx *team1, TeamFileEx *team2, uint16_t compType,
                                    uint16_t packedGoals, uint16_t aggregate,
                                    uint16_t penalty, uint16_t flags)
{
    _teamSelectionKeyCacheEnd = 1;

    if (_youreSacked)
        goto skipStats;

    /* Determine which team is the career team and assign goals accordingly */
    uint8_t team1Goals = (uint8_t)(packedGoals >> 8);
    uint8_t team2Goals = (uint8_t)(packedGoals & 0xFF);
    uint8_t team1Pen = (uint8_t)(penalty >> 8);
    uint8_t team2Pen = (uint8_t)(penalty & 0xFF);

    int16_t result;  /* 1=win, -1=loss, 0=draw */

    if (team1 == &_careerTeam) {
        /* Career team is team1: career goals = high byte, opponent = low byte */
        _boardConfidenceMod0++;
        _gamesPlayed++;
        _boardConfidenceValue0 += team1Goals;
        _goalsScored += team1Goals;
        _boardConfidenceValue1 += team2Goals;
        _goalsTaken += team2Goals;

        if (team1Goals > team2Goals) {
            result = 1;  /* Win */
        } else if (team1Goals < team2Goals) {
            result = -1;  /* Loss */
        } else {
            /* Scores equal — check penalties */
            if (!(flags & 8)) {
                result = 0;  /* Draw (no penalties) */
            } else if (team1Pen > team2Pen) {
                result = 1;  /* Win on penalties */
            } else {
                result = -1;  /* Loss on penalties */
            }
        }
    } else if (team2 == &_careerTeam) {
        /* Career team is team2: career goals = low byte, opponent = high byte */
        _boardConfidenceMod0++;
        _gamesPlayed++;
        _boardConfidenceValue1 += team1Goals;
        _goalsTaken += team1Goals;
        _boardConfidenceValue0 += team2Goals;
        _goalsScored += team2Goals;

        if (team1Goals < team2Goals) {
            result = 1;  /* Win (opponent < career) */
        } else if (team1Goals > team2Goals) {
            result = -1;  /* Loss (opponent > career) */
        } else {
            /* Scores equal — check penalties */
            if (!(flags & 8)) {
                result = 0;  /* Draw */
            } else if (team1Pen > team2Pen) {
                result = -1;  /* Loss on penalties (opponent won) */
            } else {
                result = 1;  /* Win on penalties */
            }
        }
    } else {
        /* Neither team is career team — skip stats */
        goto skipStats;
    }

    /* Apply result to confidence modifiers and stat counters */
    if (result > 0) {
        /* Win */
        _boardConfidenceMod1++;
        _numWins++;
        if (compType == 0) {
            uint8_t *tourPtr = _diyCompetitionStart.base.namePtr;
            if (tourPtr >= (uint8_t *)&_aEuropeanTour && tourPtr <= (uint8_t *)&_aOceaniaTour) {
                _boardConfidenceMod7++;
            } else if (_diyCompetitionStart.matchData.league.hasGroupsDiy) {
                _boardConfidenceMod4++;
            }
        }
    } else if (result < 0) {
        /* Loss */
        _boardConfidenceMod3++;
        _numLost++;
        if (compType == 0) {
            uint8_t *tourPtr = _diyCompetitionStart.base.namePtr;
            if (tourPtr >= (uint8_t *)&_aEuropeanTour && tourPtr <= (uint8_t *)&_aOceaniaTour) {
                _boardConfidenceMod9++;
            } else if (_diyCompetitionStart.matchData.league.hasGroupsDiy) {
                _boardConfidenceMod6++;
            }
        }
    } else {
        /* Draw */
        _boardConfidenceMod2++;
        _numDrawn++;
        if (compType == 0) {
            uint8_t *tourPtr = _diyCompetitionStart.base.namePtr;
            if (tourPtr >= (uint8_t *)&_aEuropeanTour && tourPtr <= (uint8_t *)&_aOceaniaTour) {
                _boardConfidenceMod8++;
            } else if (_diyCompetitionStart.matchData.league.hasGroupsDiy) {
                _boardConfidenceMod5++;
            }
        }
    }

skipStats: ;
    /* Store match result into competition slot record.
     * ASM: a1 is overwritten by GetCurrentSelectedElementPtr (returns in a1).
     * The base cache entry (a0) gets its counter decremented,
     * while the selected element (a1) receives match result data. */
    /* Only the genuine SELECTED-team fixture consumes a keycache slot. During cup
     * auto-advance the code sets selTeamNumber=0xFFFF and the player's other legs
     * are auto-played through a STALE selTeams copy of careerTeam that carries
     * teamControls=2 (the Amiga loads those cup slots as teamControls=COMPUTER, so
     * the whole block below is skipped there). On SDL the copy's ctl=2 let every
     * auto-played leg over-decrement the cursor entry past 0 -> stray match-score
     * writes smashed an entryStride -> DIY_RecordCupBracketProgress panic 0x6666.
     * Gate on the player's team: its number during interactive play, and no
     * match during auto-advance (selTeamNumber is 0xFFFF there). This
     * reproduces the Amiga's net keycache effect regardless of the copy's wrong
     * teamControls. (See panic_0x6666 analysis.)
     *
     * The player-team test is MODE-AWARE — season mode never assigns
     * selTeamNumber, so a bare comparison against it silently disabled this
     * whole block for every season match. See
     * KeyCacheFixtureIsPlayerControlled. */
    uint16_t t1num = PACK_BE_U16(team1->team.countryNumber, team1->team.teamOrdinal);
    uint16_t t2num = PACK_BE_U16(team2->team.countryNumber, team2->team.teamOrdinal);
    if ((team1->team.teamControls != 1 || team2->team.teamControls != 1) &&
        KeyCacheFixtureIsPlayerControlled(t1num, t2num)) {
        /* Asm calls GetCurrentSelectedElementPtr FIRST, THEN decrements
         * remainingElements. Order matters: the slot pointer depends on
         * (totalElements - remainingElements) * 12. */
        TeamSelectionKeyCacheMatchSlot *slot = TeamSelectionKeyCache_GetCurrentMatchSlot();

        /* Base cache entry — decrement remainingElements AFTER getting slot */
        TeamSelectionKeyCacheEntry *base_entry =
            (TeamSelectionKeyCacheEntry *)(_teamSelectionKeyCache + _teamSelectionKeyCacheOffset);
        base_entry->remainingElements -= 1;

        slot->matchFlags    = (uint8_t)flags;
        slot->packedGoals   = packedGoals;
        slot->aggregate     = aggregate;
        slot->penaltyScore  = penalty;
    }

    /* Set career slot processing variable */
    if (compType == 0) {
        _careerSlotProcessingInProgress = _diyCompetitionStart.leagueCurrentSlot;
    } else {
        _careerSlotProcessingInProgress = (uint16_t)_diyCompetitionStart.base.roundComplete;
    }
}

/* ========================================================================
 * Chunk 194 — Career_AutoAdvanceLeagueFixtures, Career_AutoAdvanceCupMatches
 * ======================================================================== */

extern uint8_t _careerLastMatchIndexByCompetitionSlot[];
extern uint16_t _highlightedLeagueTeamId;
extern uint16_t _teamSelectionKeyCacheIndex;
extern uint16_t _teamSelectionKeyCacheCount;

/* Forward declarations for Chunk 194 dependencies */
extern uint8_t *TeamSelectionKeyCache_GetCurrentListPtr(void);
extern void DIY_AdvanceFixtureIndex(uint16_t lo, uint16_t hi);
extern uint16_t DIY_PlayOrSimulateLeagueFixture(int16_t allowSquadChanges, int16_t selector);
extern uint16_t DIY_PlayCupMatchAndAdvance(int16_t allowSquadChanges, int16_t mode);
extern void DIY_UpdateCompetitionProgress(void);
extern void ShowDiyLeaguesTournamentMenu(uint16_t type, uint16_t viewOnly, uint16_t focusOverride);
extern void ShowDIYCupMenuWithParams(uint16_t arg0, uint16_t arg1, uint16_t arg2);

/* Career_AutoAdvanceLeagueFixtures (SWOS2.s line 31608)
 *
 * Auto-simulates all league fixtures for the current competition up to the
 * current game phase. Checks guard flags first, then loops simming matches
 * until the competition phase catches up with the current/next cache phase.
 * When done, optionally shows the league/tournament menu.
 */
__attribute__((used, externally_visible))
void Career_AutoAdvanceLeagueFixtures(void)
{
    uint8_t currentPhase, nextPhase;

    /* Guard checks — if any is active, skip with phases = 0xFF */
    if (_careerAutoAdvanceInProgress != 0 || _seasonUpdateInProgress != 0 ||
        _internationalAutoAdvanceFlag != 0 || _seasonFinished != 0) {
        currentPhase = 0xFF;
        nextPhase = 0xFF;
    } else {
        /* Get phase bytes (sortKey) from TeamSelectionKeyCache */
        TeamSelectionKeyCacheEntry *cacheEntry =
            (TeamSelectionKeyCacheEntry *)TeamSelectionKeyCache_GetCurrentListPtr();
        currentPhase = cacheEntry->sortKey;
        uint16_t nextIdx = _teamSelectionKeyCacheIndex + 1;
        if (nextIdx == _teamSelectionKeyCacheCount) {
            nextPhase = 0xFF;
        } else {
            TeamSelectionKeyCacheEntry *next =
                (TeamSelectionKeyCacheEntry *)((uint8_t *)cacheEntry + cacheEntry->entryStride);
            nextPhase = next->sortKey;
        }
    }

    DIYCompetition *base = &_diyCompetitionStart.base;
    uint8_t *lastMatchSlots = _careerLastMatchIndexByCompetitionSlot;
    uint8_t slotIdx = base->field_2E;
    uint8_t *lastMatchPtr = lastMatchSlots + (uint16_t)slotIdx;
    uint8_t lastMatch = *lastMatchPtr;

#ifdef SWOS_DEBUG_AUTOSKIP
    {
        DIYCompetitionEx *bex = &_diyCompetitionStart;
        SWOS_AUTOSKIP_LOG("[bug3-league] AutoAdvanceLeague ENTER compId=0x%04X slotIdx=%u "
                          "currentPhase=%u nextPhase=%u viewRandomPhase=%u "
                          "lastMatch=%u leagueMatchProgress=%u sectionsLeft(minSubs)=%u "
                          "leagueCurrentSlot=%u totalMatchesPerSectionLive=%d "
                          "totalMatchesInRound=%d subRoundCounter=%d "
                          "seasonFinished=%u",
                          (unsigned)*(uint16_t *)&base->compSlot, (unsigned)slotIdx,
                          (unsigned)currentPhase, (unsigned)nextPhase,
                          (unsigned)base->viewRandomPhase,
                          (unsigned)lastMatch, (unsigned)bex->leagueMatchProgress,
                          (unsigned)bex->minSubstitutes,
                          (unsigned)bex->leagueCurrentSlot,
                          (int)bex->matchData.league.totalMatchesPerSectionLive,
                          (int)base->totalMatchesInRound,
                          (int)base->subRoundCounter,
                          (unsigned)_seasonFinished);
    }
#endif

    /* Inline DIY_AdvanceLeagueFixtureIndex: advance fixture and set d6 */
    uint8_t viewRP = base->viewRandomPhase;
    DIY_AdvanceFixtureIndex(0, (uint16_t)viewRP);
    /* d6 tracking not needed — sim path (selector=0) ignores allowSquadChanges */

    /* Main simulation loop */
main_loop:
    if (currentPhase <= base->viewRandomPhase)
        goto check_next_phase;

    /* Sim league fixtures until phase catches up */
    DIY_PlayOrSimulateLeagueFixture(0, 0); /* allowSquadChanges=0, selector=0 (sim) */

    if (((DIYCompetitionEx *)base)->leagueCurrentSlot == 0)
        goto main_loop; /* more matches pending in current fixture set */

    if (((DIYCompetitionEx *)base)->minSubstitutes != 0) {
        /* Advance fixture index and continue */
        viewRP = base->viewRandomPhase;
        DIY_AdvanceFixtureIndex((uint16_t)(viewRP + 1), (uint16_t)viewRP);
        goto main_loop;
    }

    /* Check groups type */
    if (base->hasGroups == 0)
        goto show_menu_check;
    if (base->hasGroups == 2)
        goto show_menu_check;
    return; /* groups type 1/3 — return silently */

check_next_phase:
    if (nextPhase > base->viewRandomPhase)
        goto catch_up_phase;

show_menu_check:
    if (_internationalAutoAdvanceFlag != 0)
        return;
    if (_careerAutoAdvanceInProgress != 0)
        return;

#ifdef SWOS_DEBUG_AUTOSKIP
    {
        DIYCompetitionEx *bex = &_diyCompetitionStart;
        SWOS_AUTOSKIP_LOG("[bug3-league] AutoAdvanceLeague EXIT(show_menu) compId=0x%04X "
                          "viewRandomPhase=%u leagueMatchProgress=%u sectionsLeft=%u "
                          "leagueCurrentSlot=%u totalMatchesPerSectionLive=%d subRoundCounter=%d",
                          (unsigned)*(uint16_t *)&base->compSlot,
                          (unsigned)base->viewRandomPhase,
                          (unsigned)bex->leagueMatchProgress,
                          (unsigned)bex->minSubstitutes,
                          (unsigned)bex->leagueCurrentSlot,
                          (int)bex->matchData.league.totalMatchesPerSectionLive,
                          (int)base->subRoundCounter);
    }
#endif

    /* Show league menu. SWOS2.s loc_12D754/loc_12D764 calls (1,1,0)
     * unconditionally here — the highlightedLeagueTeamId check belongs
     * only on the catch_up_phase first-time path (loc_12D790).
     * arg1=1 is required so DIYLeagueTournamentInit's seasonUpdate-pre-
     * select-entry-42 isn't overridden by the menuState fallback in
     * UpdateMenuEntryVisibilityAndFocus, which would otherwise drop the
     * controlMask=0xFFFF auto-skip the season transition relies on. */
    ShowDiyLeaguesTournamentMenu(1, 1, 0);
    return;

catch_up_phase:
#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug3-league] AutoAdvanceLeague catch_up_phase compId=0x%04X "
                      "lastMatch=%u viewRandomPhase=%u (eq=%d)",
                      (unsigned)*(uint16_t *)&base->compSlot,
                      (unsigned)lastMatch, (unsigned)base->viewRandomPhase,
                      (int)(lastMatch == base->viewRandomPhase));
#endif
    /* Check if lastMatch matches current viewRandomPhase */
    if (lastMatch == base->viewRandomPhase) {
        /* Catch-up loop: sim until leagueCurrentSlot != 0 */
        do {
            DIY_PlayOrSimulateLeagueFixture(0, 0);
        } while (((DIYCompetitionEx *)base)->leagueCurrentSlot == 0);
        goto show_menu_check;
    }

    /* Store current viewRandomPhase as last match index */
    *lastMatchPtr = base->viewRandomPhase;
    if (_highlightedLeagueTeamId != 0xFFFF) {
        ShowDiyLeaguesTournamentMenu(1, 1, 0);
    } else {
        ShowDiyLeaguesTournamentMenu(1, 0, 0);
    }
}

/* Career_AutoAdvanceCupMatches (SWOS2.s line 31708)
 *
 * Auto-simulates all cup matches for the current competition up to the
 * current game phase. Similar to league version but uses cup-specific
 * functions and checks roundComplete (0x54) and finishedFlag (0x56).
 */
__attribute__((used, externally_visible))
void Career_AutoAdvanceCupMatches(void)
{
    uint8_t currentPhase, nextPhase;

    /* Guard checks */
    if (_careerAutoAdvanceInProgress != 0 || _seasonUpdateInProgress != 0 ||
        _internationalAutoAdvanceFlag != 0 || _seasonFinished != 0) {
        currentPhase = 0xFF;
        nextPhase = 0xFF;
    } else {
        TeamSelectionKeyCacheEntry *cacheEntry =
            (TeamSelectionKeyCacheEntry *)TeamSelectionKeyCache_GetCurrentListPtr();
        currentPhase = cacheEntry->sortKey;
        uint16_t nextIdx = _teamSelectionKeyCacheIndex + 1;
        if (nextIdx == _teamSelectionKeyCacheCount) {
            nextPhase = 0xFF;
        } else {
            TeamSelectionKeyCacheEntry *next =
                (TeamSelectionKeyCacheEntry *)((uint8_t *)cacheEntry + cacheEntry->entryStride);
            nextPhase = next->sortKey;
        }
    }

    DIYCompetition *base = &_diyCompetitionStart.base;
    uint8_t *lastMatchSlots = _careerLastMatchIndexByCompetitionSlot;
    uint8_t slotIdx = base->field_2E;
    uint8_t *lastMatchPtr = lastMatchSlots + (uint16_t)slotIdx;
    uint8_t lastMatch = *lastMatchPtr;

#ifdef SWOS_DEBUG_AUTOSKIP
    int debug_cup_iters = 0;
    SWOS_AUTOSKIP_LOG("[bug4-cup] AutoAdvanceCupMatches ENTER compId=0x%04X "
                      "currentPhase=%u nextPhase=%u viewRandomPhase=%u lastMatch=%u "
                      "currentRoundIdx=%d/%d matchIdx=%d/%d roundComplete=%d "
                      "competitionFinished=%d hasGroups=%d",
                      (unsigned)*(uint16_t *)&base->compSlot,
                      (unsigned)currentPhase, (unsigned)nextPhase,
                      (unsigned)base->viewRandomPhase, (unsigned)lastMatch,
                      (int)base->currentRoundIndex, (int)base->numRounds,
                      (int)_diyCompetitionStart.matchData.cup.currentMatchIndex,
                      (int)base->totalMatchesInRound,
                      (int)base->roundComplete,
                      (int)base->competitionFinished,
                      (int)base->hasGroups);
#endif

    /* Inline DIY_AdvanceCupFixtureIndex */
    uint8_t viewRP = base->viewRandomPhase;
    DIY_AdvanceFixtureIndex(0, (uint16_t)viewRP);

    /* First check: if currentPhase <= viewRandomPhase, skip main loop */
    if (currentPhase <= base->viewRandomPhase)
        goto check_next_phase_cup;

    /* Main cup sim loop */
cup_main_loop:
#ifdef SWOS_DEBUG_AUTOSKIP
    if (++debug_cup_iters > 500) {
        SWOS_AUTOSKIP_LOG("[bug4-cup] AutoAdvanceCupMatches EMERGENCY BAIL "
                          "iter>%d compId=0x%04X viewRandomPhase=%u currentPhase=%u "
                          "roundComplete=%d competitionFinished=%d",
                          debug_cup_iters,
                          (unsigned)*(uint16_t *)&base->compSlot,
                          (unsigned)base->viewRandomPhase, (unsigned)currentPhase,
                          (int)base->roundComplete,
                          (int)base->competitionFinished);
        return;
    }
#endif
    DIY_PlayCupMatchAndAdvance(0, 0); /* allowSquadChanges=0, mode=0 (sim) */

    if (base->roundComplete == 0)
        goto cup_main_loop; /* roundComplete == 0, keep going */

    if (base->competitionFinished != 0)
        goto cup_finished; /* finishedFlag != 0 */

    if (currentPhase <= base->viewRandomPhase)
        goto check_next_phase_cup;

    /* Update competition progress and advance fixture */
    DIY_UpdateCompetitionProgress();
    viewRP = base->viewRandomPhase;
    DIY_AdvanceFixtureIndex(0, (uint16_t)viewRP);
    goto cup_main_loop;

cup_finished:
#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug4-cup] AutoAdvanceCupMatches cup_finished compId=0x%04X "
                      "iters=%d hasGroups=%d viewRandomPhase=%u",
                      (unsigned)*(uint16_t *)&base->compSlot,
                      debug_cup_iters, (int)base->hasGroups,
                      (unsigned)base->viewRandomPhase);
#endif
    if (base->hasGroups == 0)
        goto cup_show_menu_check;
    if (base->hasGroups == 2)
        goto cup_show_menu_check;
    return; /* groups type 1/3 — return silently */

check_next_phase_cup:
    if (nextPhase > base->viewRandomPhase)
        goto cup_catch_up;

cup_show_menu_check:
    if (_internationalAutoAdvanceFlag != 0)
        return;
    if (_careerAutoAdvanceInProgress != 0)
        return;

#ifdef SWOS_DEBUG_AUTOSKIP
    SWOS_AUTOSKIP_LOG("[bug4-cup] AutoAdvanceCupMatches EXIT (show_menu) compId=0x%04X "
                      "iters=%d currentRoundIdx=%d viewRandomPhase=%u "
                      "roundComplete=%d competitionFinished=%d",
                      (unsigned)*(uint16_t *)&base->compSlot,
                      debug_cup_iters, (int)base->currentRoundIndex,
                      (unsigned)base->viewRandomPhase,
                      (int)base->roundComplete,
                      (int)base->competitionFinished);
#endif

    /* Show cup menu. SWOS2.s loc_12D884/loc_12D894 calls (1,1,0)
     * unconditionally here — the highlightedLeagueTeamId check belongs
     * only on the catch_up_phase first-time path (loc_12D8D6).
     * arg1=1 is required so DIYCupInit's seasonUpdate-pre-select-entry-
     * 66 isn't overridden by the menuState fallback in
     * DesignDIYCup_UpdateMenuEntriesVisibilityAndSelection, which would
     * otherwise drop the controlMask=0xFFFF auto-skip the season
     * transition relies on. */
    ShowDIYCupMenuWithParams(1, 1, 0);
    return;

cup_catch_up:
    /* If roundComplete, update progress and advance fixture */
    if (base->roundComplete != 0) {
        DIY_UpdateCompetitionProgress();
        viewRP = base->viewRandomPhase;
        DIY_AdvanceFixtureIndex(0, (uint16_t)viewRP);
    }

    if (lastMatch == base->viewRandomPhase) {
        /* Catch-up loop */
        do {
            DIY_PlayCupMatchAndAdvance(0, 0);
        } while (base->roundComplete == 0);
        goto cup_show_menu_check;
    }

    /* Store current viewRandomPhase as last match index */
    *lastMatchPtr = base->viewRandomPhase;
    if (_highlightedLeagueTeamId != 0xFFFF) {
        ShowDIYCupMenuWithParams(1, 1, 0);
    } else {
        ShowDIYCupMenuWithParams(1, 0, 0);
    }
}

extern void DIY_SortLeagueTableByStandings(void);
extern void DIY_AdvanceToNextCompetitionSlot(void);

/* Career_AutoAdvanceCompetitionSlots (SWOS2.s line 31635)
 *
 * Iterates through competition slots in diyFileBufferCopy, dispatching each
 * to league or cup auto-advance based on the slot type flag at offset 0x15E.
 * Sets hasGroups (0x48) to indicate slot position (1=normal, 2=last).
 * Loops via tail-call recursion until all slots are processed.
 */
__attribute__((used, externally_visible))
void Career_AutoAdvanceCompetitionSlots(void)
{
restart:
    ;
    uint8_t *bufCopy = (uint8_t *)&_diyFileBufferCopy;
    DIYCompetition *bufCopyBase = (DIYCompetition *)bufCopy;
    uint16_t slotIndex = (uint16_t)bufCopyBase->roundComplete;
    DIYTournamentSlot *slotRec = (DIYTournamentSlot *)(bufCopy + (uint32_t)slotIndex * 0x76 + 0x15A);
    DIYCompetitionEx *compEx = &_diyCompetitionStart;

    /* Determine hasGroups value based on slot position */
    uint16_t hasGroups = 1;
    if (slotIndex + 1 == (uint16_t)bufCopyBase->totalMatchesInRound)
        hasGroups = 2; /* last slot */

    if (slotRec->isLeagueFormat != 0) {
        /* League path */
        compEx->base.hasGroups = (int16_t)hasGroups;
        Career_AutoAdvanceLeagueFixtures();
        if (compEx->base.hasGroups < 0)
            return; /* hasGroups went negative → done */
        DIY_SortLeagueTableByStandings();
        DIY_AdvanceToNextCompetitionSlot();
        if (bufCopyBase->currentRoundIndex == 0)
            goto restart; /* more slots remain */
    } else {
        /* Cup path */
        compEx->base.hasGroups = (int16_t)hasGroups;
        Career_AutoAdvanceCupMatches();
        if (compEx->base.hasGroups < 0)
            return; /* hasGroups went negative → done */
        DIY_AdvanceToNextCompetitionSlot();
        if (bufCopyBase->currentRoundIndex == 0)
            goto restart; /* more slots remain */
    }
}


/* ================================================================
 * Chunk 196 — Career_SetCurrentCompetitionSlot
 * ================================================================ */

extern uint8_t *_careerCompetitionSlotPtrTable[];
extern volatile uint16_t _isNationalSelector;
extern void Career_ApplyForeignMarketPlayersToSelectedTeams(void);
extern void CopySelectedTeamNumbers(uint16_t count, uint8_t *dest);
extern void InitCompetitionFromRecord(uint16_t index, uint8_t *record);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *src);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *dst);
extern void Career_InitCompetitionTeamSelection_Dispatch(uint8_t *spec, uint16_t idx);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);
extern void TeamSelectionKeyCache_BuildFromCompetitionBlock(uint8_t *blk);

/* ================================================================
 * Career_SetCurrentCompetitionSlot
 * (SWOS2.s line 31086)
 *
 * Sets up a competition slot for career mode:
 * 1) Stores comp pointer in currentCompetitionPtr[slot]
 * 2) Loads slot state from buffer, writes slot index to state
 * 3) Validates comp type byte against slot index (panics on mismatch)
 * 4) For slot 4 (DIY): fills diyCompetitionStart.cupTeamIds with career team id
 * 5) For other slots with hasSelectedTeams: copies team selections
 * 6) For other slots without: dispatches team selection + foreign market
 * 7) Initializes competition from record, stores state, builds key cache
 * 8) Unless slot 4: loads state again and updates competition progress
 *
 * Parameters:
 *   comp — pointer to competition data (a0)
 *   slot — competition slot index 0-4 (d0)
 *   divisionIndex — division index passed through to InitCompetitionFromRecord (d1, hidden param)
 *   hasSelectedTeams — flag indicating teams are pre-selected (d2)
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex, uint16_t hasSelectedTeams)
{
    /* Store comp pointer in the named per-slot global. On Amiga
     * `currentCompetitionPtr` and its 4 siblings were 5 adjacent
     * uint32 BSS labels, so the original code walked them as
     * `((uint32_t *)&currentCompetitionPtr)[slot]`. We extracted them
     * into typed standalone pointers (Phase 6) — they are no longer
     * guaranteed to be adjacent, and the linker is free to put them
     * anywhere. Address each by name instead. */
    extern uint8_t *_secondaryCompetitionPtr;
    extern uint8_t *_tertiaryCompetitionPtr;
    extern uint8_t *_quaternaryCompetitionPtr;
    extern uint8_t *_diyCompetitionPtr;
    switch (slot) {
        case 0: _currentCompetitionPtr    = comp; break;
        case 1: _secondaryCompetitionPtr  = comp; break;
        case 2: _tertiaryCompetitionPtr   = comp; break;
        case 3: _quaternaryCompetitionPtr = comp; break;
        case 4: _diyCompetitionPtr        = comp; break;
        default:
            _panicReasonCode = 0x4459;  /* unknown slot index */
            PanicHaltLoop();
    }

    /* Get the slot state pointer from careerCompetitionSlotPtrTable.
     * +0x2C = compSlot/slotId word in DIYCompetition base. */
    uint8_t *slotState = _careerCompetitionSlotPtrTable[slot];
    *(uint16_t *)&((DIYCompetition *)slotState)->compSlot = slot;

    /* Load competition slot state from buffer */
    /* asm: exg a0,a1 → a0=slotState for the call, then exg back */
    Career_LoadCompetitionSlotStateFromBuffer(slotState);

    /* Validate comp type vs slot — panic if mismatch */
    const Competition *compHdr = (const Competition *)comp;
    int8_t compType = compHdr->type;
    if (slot == 0) {
        /* Slot 0: compType must NOT be 1 */
        if (compType == 1)
            goto panic_mismatch;
        goto validated;
    } else if (slot == 4) {
        /* Slot 4: compType must be 1 */
        if (compType != 1)
            goto panic_mismatch;
        goto validated;
    } else if (slot == 3) {
        goto validated;
    } else {
        /* Slots 1, 2: check isNationalSelector */
        if (_isNationalSelector) {
            if (compType == 0 || compType == 2)
                goto validated;
        }
        /* Default: compType must be 1 */
        if (compType != 1)
            goto panic_mismatch;
        goto validated;
    }

panic_mismatch:
    _panicReasonCode = 0x4459;  /* 'DY' */
    PanicHaltLoop();

validated:
    /* Slot 4 special case: fill diyCompetitionStart.cupTeamIds with career team id */
    if (slot == 4) {
        uint16_t teamId = PACK_BE_U16(_careerTeam.team.countryNumber, _careerTeam.team.teamOrdinal);
        /* Slot 4 is validated as compType==1 (cup) above, so comp is a
         * CompetitionCup — byte 0xA = CompetitionCup.teams. */
        int16_t count = (int16_t)((const CompCupHeader *)comp)->teams;
        int16_t i = count - 1;
        while (i >= 0) {
            /* cupTeamIds is BE-packed {country, ordinal} (295c9f3 convention);
             * every reader uses BE_READ_U16 (e.g. DIY_BuildTeamSelectionPointerCache).
             * The Amiga `move.w d0,(a0)+` stored a big-endian word; a host-native
             * `cupTeamIds[i] = teamId` on an LE host stores the bytes swapped, so
             * the BE read came back byte-swapped (careerTeam 0x084d -> 0x4d08) and
             * VerifyTeamLoaded panicked while building the slot-4 (playoff) cup
             * bracket during season transition. Store BE to match every reader. */
            BE_WRITE_U16(&_diyCompetitionStart.cupTeamIds[i], teamId);
            i--;
        }
        goto init_competition;
    }

    /* Non-slot-4: handle team selection */
    if (hasSelectedTeams) {
        /* Copy team numbers from selection to appropriate team ID list */
        uint16_t *teamIdList;
        if (compType == 0) {
            teamIdList = _diyCompetitionStart.matchData.league.leagueTeamIds;
        } else if (compType == 1) {
            teamIdList = _diyCompetitionStart.cupTeamIds;
        } else {
            teamIdList = _diyFileBufferCopy.teamIds;
        }
        CopySelectedTeamNumbers(_numSelectedTeams, (uint8_t *)teamIdList);
    } else {
        /* Initialize team selection from competition spec
         * asm: jsr Career_InitCompetitionTeamSelection_Dispatch with d1=divisionIndex, a0=comp */
        Career_InitCompetitionTeamSelection_Dispatch(comp, divisionIndex);
        Career_ApplyForeignMarketPlayersToSelectedTeams();
    }

init_competition:
    /* Initialize competition from record — original asm does exg d0,d1 to pass
     * divisionIndex (originally in d1) as the first param to InitCompetitionFromRecord,
     * then exg back to restore d0=slot */
    InitCompetitionFromRecord(divisionIndex, comp);

    /* Store slot index to both diyFileBufferCopy and diyCompetitionStart.
     * +0x2C = compSlot/slotId word in DIYCompetition base. */
    *(uint16_t *)&_diyFileBufferCopy.header.compSlot = slot;
    *(uint16_t *)&_diyCompetitionStart.base.compSlot = slot;  /* writes compId+slotId as word */

    /* Write comp type to slot state header (sign-extended from int8). */
    int16_t typeWord = (int16_t)compType;
    *(uint16_t *)slotState = (uint16_t)typeWord;

    /* Store competition slot state to buffer */
    Career_StoreCompetitionSlotStateToBuffer(slotState);

    /* Build team selection key cache */
    TeamSelectionKeyCache_BuildFromCompetitionBlock(slotState);

    /* Slot 4 doesn't do the final load+progress step */
    if (slot == 4)
        return;

    /* Reload state and update competition progress */
    Career_LoadCompetitionSlotStateFromBuffer(slotState);
    if (compType == 1) {
        /* Stage-3 band-aid removal (d16ad5b, TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md
         * §4). The teamOffsetTable rebuild before the season keycache fill is
         * REMOVED. It guarded the selTeams-index staleness — "the cup slot load
         * restores bracketScratch.teamOffsetTable (selTeams INDICES that go stale
         * when the careerFileBuffer overlay reorders selTeams), so the fill
         * resolves a slot to a duplicate team → two fixtures in one 1-game
         * keycache entry → walker desync → VerifyTeamLoaded garbage-id panic" —
         * but Part B retired that overlay, killing the staleness root (same as the
         * 8c660f4 removal). The §4 probe confirmed it a no-op 14/14 across many
         * cup/euro seasons, INCLUDING roundIterator (this site never save/restored
         * it, but the loaded slot's value already equals the Part-2 recompute, so
         * that was redundant too). Production no longer rebuilds; debug keeps a
         * NON-WRITING check (run the rebuild, diff teamOffsetTable + roundIterator,
         * restore both so debug == production; log if it ever WOULD change). */
#ifdef SWOS_DEBUG_AUTOSKIP
        extern void DIY_BuildTeamSelectionPointerCache(void);
        {
            int16_t ri_before = _diyCompetitionStart.base.roundIterator;
            int32_t tot_before[64];
            for (int i = 0; i < 64; i++)
                tot_before[i] = _diyCompetitionStart.bracketScratch.teamOffsetTable[i];
            DIY_BuildTeamSelectionPointerCache();
            int m = 0, first = -1;
            long new_first = 0;
            for (int i = 0; i < 64; i++)
                if (_diyCompetitionStart.bracketScratch.teamOffsetTable[i] != tot_before[i]) {
                    if (first < 0) {
                        first = i;
                        new_first = (long)_diyCompetitionStart.bracketScratch.teamOffsetTable[i];
                    }
                    m++;
                }
            int ri_new = _diyCompetitionStart.base.roundIterator;
            int ri_chg = (ri_new != ri_before);
            for (int i = 0; i < 64; i++)
                _diyCompetitionStart.bracketScratch.teamOffsetTable[i] = tot_before[i];
            _diyCompetitionStart.base.roundIterator = ri_before;
            if (m || ri_chg)
                SWOS_AUTOSKIP_LOG("[tot-rebuild-d16ad5b] REMOVED but rebuild WOULD CHANGE tot=%d/64 "
                                  "(idx %d %ld->%ld) roundIter(%d->%d) -> STALENESS BACK, revisit",
                                  m, first, (long)(first >= 0 ? tot_before[first] : 0), new_first,
                                  ri_before, ri_new);
            else
                SWOS_AUTOSKIP_LOG("[tot-rebuild-d16ad5b] band-aid REMOVED, no-rebuild state stays correct");
        }
#endif
    }
    DIY_UpdateCompetitionProgressOrAdvanceSlot(slotState);
}
