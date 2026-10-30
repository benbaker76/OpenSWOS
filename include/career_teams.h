/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_teams.h — Team cache lookup, save/restore selected teams */
#pragma once
#include "game_types.h"

/* --- Team league lookup (Chunk 151) --- */
int16_t GetTeamLeague(uint16_t teamNumber);

/* --- Team lookup --- */
TeamFileEx *GetTeamPointer(uint16_t teamId);
TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);
void CopySelectedTeamNumbers(uint16_t count, uint8_t *dest);

/* --- Save/Restore --- */
void SaveSelectedTeams(void);
void RestoreSelectedTeams(void);

/* --- Key cache (Chunk 76) --- */
uint8_t *TeamSelectionKeyCache_InsertSpaceSorted(uint16_t key, uint16_t insertionBytes);

/* --- TMD team pool management (Chunk 77) --- */
int16_t SearchTeamInTmdFile(uint16_t teamNo);
void Career_RemoveTeamFromTmdTeamPool(uint16_t teamNo);
void Career_AppendTeamToTmdTeamPool(TeamFileEx *team);
void Career_RetireSlotCalendarEntries(uint8_t slot);   /* (before a slot takes another contest mid-season) */

/* --- Career competition team selection (Chunk 90) --- */
void Career_SortTeamFilePtrsByAvgPlayerPrice(uint16_t extNumber);
void LoadSomeEuroCup(uint8_t *whichCup);
void AppendTeamsToSelectionFromTmdOrTeamFile(uint16_t *out, uint16_t *teamIds, uint16_t count);
void Career_TryAddRandomTeamByRegionToLists(void);
void FatalError(void);
void Career_InitCompetitionTeamSelectionFromTableEntrySpec(uint8_t *spec, uint16_t idx);
void Career_InitCompetitionTeamSelectionFromTableHeaderSpec(uint8_t *spec);
void Career_InitCompetitionTeamSelectionFromAltTableHeaderSpec(uint8_t *spec);
void Career_InitCompetitionTeamSelection_Dispatch(uint8_t *spec, uint16_t idx);

/* --- Competition state buffer copy (Chunk 92) --- */
void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *src);
void Career_StoreCompetitionSlotStateToBuffer(uint8_t *dst);
void Career_CopyCompetitionFileBufferToOrFromActive(void);
void Career_CopyCompetitionContextToOrFromActive(void);
void Career_CopyTertiaryCompetitionContextToOrFromActive(void);
void Career_CopyEuroCupsCompetitionContextToOrFromActive(void);

/* --- Team selection key cache navigation (Chunk 92) --- */
uint8_t *TeamSelectionKeyCache_GetListPtr(uint16_t listIndex);
uint8_t *TeamSelectionKeyCache_GetListElementPtr(uint16_t listIndex, uint16_t elementIndex);
uint8_t *TeamSelectionKeyCache_GetCurrentListPtr(void);
uint8_t *TeamSelectionKeyCache_GetCurrentSelectedElementPtr(void);

/* --- Career state management (Chunk 92) --- */
void Career_UpdateCareerFinishedFlag(void);
void Career_AdvanceToNextTeamSelectionKeyCacheEntry(void);

/* --- Career match flow (Chunk 93) --- */
void CareerPlayMatch(void);
void Career_PlayMatchMenuLoop(void);
void Career_NextMatch(void);
void Career_OpenCompetitionSlotMenuAndApplyChanges(uint16_t d2);
void Career_ProcessCurrentCompetitionSlotForMatch(void);
void Career_ProcessCurrentCompetitionSlotAfterMatch(void);

/* --- Career auto-simulate & competition slot advancement (Chunk 94) --- */
void Career_AutoSimulateLeagueFixturesUntilHumanTeam(void);
void Career_AutoSimulateCupMatchesUntilHumanTeam(void);
void Career_AutoSimulatePendingMatchUntilHumanTeam(void);
void Career_AutoPlayOrSimulateLeagueFixtureLoop(void);
void Career_AutoPlayCupMatchAndAdvanceLoop(void);
void Career_UpdateHybridCompetitionProgressAndMaybeAdvanceSlot(void);
void DIY_AdvanceToNextCompetitionSlot(void);

/* --- NationalTeamAddPlayer callbacks (Chunk 108) --- */
void NationalTeamAddPlayerHomeOnSelect(void);
void NationalTeamAddPlayerAbroadOnSelect(void);
void NationalTeamAddPlayerExit(void);

/* --- National team menu update helpers (Chunk 148) --- */
void Career_UpdateNationalTeamMenuEntries(void);
void Career_UpdateRemovePlayersMenuEntry(void);

/* --- Career_CopyUnavailablePlayerToCareerTeamSlot (Chunk 170) --- */
void Career_CopyUnavailablePlayerToCareerTeamSlot(uint8_t *srcPlayer, uint8_t *dstTeam,
    uint8_t *srcName, uint16_t srcTeamNumber, uint16_t srcPlayerIndex, uint16_t dstSlotIndex);

/* --- GetPlayerPositionFromCurrent / Career_RemoveInternationalPlayersByMask (Chunk 171) --- */
uint16_t GetPlayerPositionFromCurrent(uint16_t playerIndex);
void Career_RemoveInternationalPlayersByMask(uint32_t removeMask);

/* --- Career_InitInternationalSquadAndLookupTablesForNewSeason (Chunk 172) --- */
void Career_InitInternationalSquadAndLookupTablesForNewSeason(TeamFileEx *teamPtr);

/* --- TeamSelectionKeyCache builders (Chunk 196) --- */
void TeamSelectionKeyCache_BuildFromCompetitionBlock(uint8_t *blk);
