/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_status.h — Player status decay, availability, team init, career finances,
 * chairman warnings, board confidence, reputation bonuses. */
#pragma once
#include "game_types.h"

/* --- Team initialization for match (decay status + cycle flags) --- */
void InitInGameTeams_PlayerControlled(TeamFileEx *team1, TeamFileEx *team2, uint16_t mode);
void InitInGameTeams_WithMatchParam(TeamFileEx *team1, TeamFileEx *team2);

/* --- Career-specific utilities --- */
void BuildCareerTeamUnavailablePlayerList(void);
void ClearCareerMatchPlayerStatusBucket(void);

/* --- Status checks --- */
uint16_t ShouldApplyRandomPlayerStatusAfterSim(void);
uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);
/* DISPLAY-only: the real status even under the testers' rule (test_rules.h; see career_status.c). */
uint16_t IsPlayerInjuredForDisplay(PlayerFile *playerFile, int16_t mode);

/* --- Player count utilities --- */
uint16_t GetNumberOfSubstitutes(TeamFileEx *team, uint16_t numPl);
uint16_t GetNumberOfSubstitutesInSelectionList(TeamFileEx *team, uint16_t extraCount);
uint32_t GetNumberOfAvailablePlayers(TeamFileEx *team, int16_t excludeCardsFlag, uint16_t numPl);
uint32_t GetNumberOfAvailableSubstitutes(TeamFileEx *team, int16_t flag);
uint32_t GetNumberOfAvailableStartingPlayers(TeamFileEx *team, int16_t flag);
int16_t GetAgeAdjustedPlayerFormFactor(uint8_t *playerBase);

/* --- Financial management --- */
void ClampNewBalanceAndUpdateClubStatus(void);
void ClearOverdraftStateIfRecovered(void);
void UpdateCareerFinancesAfterMatch(DIYCompetitionEx *comp, TeamFileEx *team, TeamFileEx *opponent);
void ApplyWeeklyFinancialDeductions(void);
void InitializeCareerSeasonFinancials(void);

/* --- Chairman warnings --- */
void MaybeSackManagerAfterSeasonReview(void);
void UpdateCareerChairmanWarningsAfterMatch(void);
void UpdateCareerAfterMatch(void);

/* --- Tournament checks --- */
uint16_t IsMajorInternationalTournament(uint8_t **outCtx);
uint16_t IsMajorTournamentAllowingRandomStatus(void);
uint16_t IsMajorTournamentAllowingSquadChanges(void);
uint16_t IsTournamentAllowingSquadChangesIfNotCareer(void);
uint16_t ShouldSetupPlayers(void);

/* --- Board confidence --- */
int16_t GetBoardConfidenceScore(void);
int16_t UpdateBoardConfidenceScore(void);
int16_t PushBoardConfidenceHistory(void);
void UpdateBoardConfidenceModifiers(void);

/* --- Reputation & starting balance --- */
void UpdateCareerReputationBonuses(void);
void InitializeStartingBalanceForNewSeason(void);
uint32_t CalculateStartingBalance(uint16_t teamNumber);

/* --- Wage management (Chunk 76) --- */
uint16_t GetAvgTeamValueBaseForCurrentPlayer(uint16_t teamNumber, uint16_t playerIndex);
void Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
void Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages(uint16_t playerIndex);

/* --- Post-match stats & player movement (Chunk 95) --- */
void PlayerJoinedClubOnTrial(uint8_t *name);
void TrialPlayerLeftClubMessage(uint8_t *name);
void ReservePlayerLeftClubMessage(uint8_t *name);
void Career_UpdateSinglePlayerPostMatchStats(uint8_t *playerSlotPtr);
void Career_ApplyPostMatchPlayerStatModsIfCareerTeam(TeamFileEx *team, uint8_t *mods);
uint8_t *AddTrialPlayer(void);
uint8_t *TrainingMatch_AddTrialPlayerAtPosition(uint16_t position);
void CheckPlayersLeavingComing(void);

/* --- Chairman messages (Chunk 77) --- */
void ShowLeavingMessage(void);
void ShowGreetingMessage(void);
void ShowLastSeasonMessage(void);
void ShowWishYouSuccessMessage(void);
void ShowEnjoyLastSeasonMessage(void);
void ShowRetirementMessage(void);

/* --- Club Business & Season Finance Init (Chunk 101) --- */
void ClubBusinessInit(void);
void Career_InitializeNewSeasonFinances(void);

/* --- Career retirement (Chunk 119) --- */
void Career_HandleRetirement(void);

/* --- Career post-match update (Chunk 175) --- */
void Career_PostMatchUpdate(void);

/* --- Career auto-advance chain (Chunk 179) --- */
void Career_AutoAdvanceCompetitionAndPersistSlotState(uint8_t *compPtr);
void Career_AutoAdvanceCupCompetitionFromRecord(uint8_t *record);
void Career_AutoAdvanceLeagueCompetitionFromRecord(uint8_t *record);
void Career_AutoAdvanceInternationalCompetitionFixtures(uint8_t *record, uint16_t index);

/* --- Career auto-advance match sim (Chunk 194/195) --- */
void Career_AutoAdvanceLeagueFixtures(void);
void Career_AutoAdvanceCupMatches(void);
void Career_AutoAdvanceCompetitionSlots(void);

/* --- Career_SetCurrentCompetitionSlot (Chunk 196) --- */
void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex, uint16_t hasSelectedTeams);

/* --- Career player form update (Chunk 188) --- */
void UpdateCareerPlayersFormAfterMatch(DIYCompetitionEx *comp, TeamFileEx *team1, TeamFileEx *team2,
                                         uint16_t team1Goals, uint16_t team2Goals);

/* --- Career stats update after match (Chunk 189) --- */
void Career_UpdateStatsAfterMatch(TeamFileEx *team1, TeamFileEx *team2, uint16_t compType,
                                    uint16_t packedGoals, uint16_t aggregate,
                                    uint16_t _penalty, uint16_t flags);
