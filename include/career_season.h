/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_season.h — Career season management, team list building, qualification. */
#pragma once
#include "game_types.h"

/* --- Random Team List Filling --- */
void Career_FillRandomTeamListExcludingSelected(uint16_t excludeCount,
                                                  uint8_t *table, uint8_t *compIds);

/* --- European Cup Qualification State --- */
void Career_PrepareEuropeanCupQualificationState(void);

/* --- Competition abbreviation (Chunk 145) --- */
uint8_t *GetCompetitionAbbrevStr(uint8_t *competition);

/* --- Chunk 161: Season opinion --- */
void ShowSeasonOpinion(void);

/* --- Career_UpdateLastNationalityCall (Chunk 170) --- */
uint16_t Career_UpdateLastNationalityCall(void);

/* --- Chunk 174: Season Review functions --- */
void SeasonFinished_ComputeSeasonReview(void);
void ShowChairmanOpinionAboutTheSeason(void);

/* --- Chunk 181: Contract Negotiation --- */
void BeginContractNegotiationSequence(DIYCompetitionEx *comp, TeamFileEx *team, TeamFileEx *opponent);

/* --- Chunk 182: Competition Season Processing --- */
void Career_ProcessSecondaryCompetitionForNextSeason(uint8_t *competition);
void Career_ProcessTertiaryCompetitionForNextSeason(uint8_t *competition);
void Career_ProcessCompetitionForNextSeason(uint16_t compIndex);

/* --- Chunk 185: Cup Pick List System --- */
void Career_SetCupPickList_ForSelectedRegion(uint16_t *teamList);

/* --- Chunk 186: Season Update Functions --- */
void Career_AdvanceToNextSeason_UpdateCompetitions(void);

/* Persistent season champions, independent of manager-only SeasonInformations.
 * kind: 0 league division, 1 primary cup, 2 secondary cup, 3 tertiary cup.
 * The season index is zero-based; competition id is the original country code.
 * Entries are only written after results have been generated. */
#define CAREER_WINNER_SEASONS 35
#define CAREER_WINNERS_PER_SEASON 512

typedef struct CareerWinnerRecord {
    uint16_t teamId;
    uint8_t competitionId;
    uint8_t division;
    uint8_t kind;
} CareerWinnerRecord;
extern CareerWinnerRecord _careerWinners[CAREER_WINNER_SEASONS][CAREER_WINNERS_PER_SEASON];
extern uint16_t _careerWinnerCounts[CAREER_WINNER_SEASONS];
void CareerWinners_Clear(void);
void CareerWinners_Record(uint16_t season, uint8_t competitionId,
                          uint8_t kind, uint8_t division, uint16_t teamId);
