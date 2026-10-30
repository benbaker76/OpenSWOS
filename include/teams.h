/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef TEAMS_H
#define TEAMS_H

#include "menu_system.h"
#include "game_types.h"

/* ---- Team file loading functions ---- */
int16_t LoadTeamFile(uint16_t extNumber);
/* A club outside its country's league (was league == 4; teams.c). */
int32_t Team_IsOutsideLeague(const TeamFile *t);
void SetTeamGlobalNumbers(uint16_t numTeams, uint8_t *firstTeam);

/* ---- Team identification ---- */
void CheckIsTeamNational(void);
uint16_t IsTeamNational(TeamFileEx *team);

/* ---- Team value / price utilities ---- */
uint16_t GetTeamValueBase(uint16_t avgPrice);
uint16_t CalcGateReceiptsBaseFromTeamValue(uint16_t avgPrice);
uint16_t GetAveragePlayerPriceInSelectedTeams(void);

/* ---- Player display value utilities ---- */
uint16_t GetPlayerRandomNum(PlayerFile *playerFile);
uint16_t GetDisplayedPlayerValue(uint8_t *ptr);
void InitPlayerValueDisplayStruct(uint8_t *playerEntry, uint8_t *dst);

/* ---- Value snapping utilities ---- */
uint16_t SnapToNearestAvgTeamValueBase(uint16_t value);
uint16_t SnapUpToAvgTeamValueBase(uint16_t value);

/* ---- Player rotation ---- */
void RotatePlayersLeft(TeamFileEx *team, uint16_t maxSubs);

/* ---- Team buffer management ---- */
TeamFileEx *AddTeamToBuffer(TeamFileEx *team);
TeamFileEx *FindTeam(TeamFileEx *teamToFind, uint16_t *outRemainingCount);
void DeselectTeam(TeamFileEx *teamToRemove);

/* ---- Player value/status text formatting (Chunk 151) ---- */
void FormatPlayerValueOrStatusText(uint8_t *base, uint8_t *dst);

/* ---- Player record lookup (Chunk 91) ---- */
uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
uint16_t GetPlayerSlotIndexByPlayerNumber(uint16_t teamNumber, uint16_t playerNumber);

/* ---- SetBenchPlayersNumbers, Career_RemovePlayerFromSelectedTeamRoster (Chunk 95) ---- */
void SetBenchPlayersNumbers(TeamFileEx *team);
/* Real squad numbers (a world's squad_numbers, in a career): on; a new player of the managed club
 * (its slot) keeps his number or takes the lowest free. */
int32_t SquadNumbers_On(void);
void SquadNumbers_Free(uint16_t slot);
void Career_RemovePlayerFromSelectedTeamRoster(uint16_t index);
void RemovePlayerFromTeam(uint16_t playerOrd);

/* ---- ChooseTeams layout & selection (Chunks 219-221) ---- */
void ChooseTeamsFriendly_UpdateMenuEntryYLayout(void);
void ViewSelTeamsSetColumns(void);
void SetTeamEntriesStringsAndColors(void);
void SetChooseChangeTeamsEntries(void);
void ChooseTeamsFriendly_DrawChangeTeamsPrompt(MenuEntry *savedA5);
void ChooseTeamsFriendlyCommon(MenuEntry *entry);
void ChooseTeamsFriendlyCommon2(MenuEntry *entry);
void ChooseTeamsFriendlyInit(void);
void ChooseTeamsFriendlyInitFinish(void);
void SetTeamsCoordinates(void);
void SelectTeamsReinit(void);
void SelectTeamsInit(void);
void SelTeamForTacticsInit(void);
void ChooseTeamsInit(void);

/* ---- Team selection menu callbacks (Chunks 107-108) ---- */
void DeselectAllTeams(void);
void ChangeTeams(void);
void AbortChooseTeamsFriendly(void);
void ChooseTeamsFriendlyOnSelect76(void);
void ViewSelected(void);
void ExitChooseTeams2(void);
void ChooseTeamsPlayMatchSelected(void);

/* ---- Team navigation stack & contest checking (Chunk 109) ---- */
uint8_t *GetCountryName(uint16_t countryId);
void PushContinent(uint16_t countryId);
uint16_t PopContinent(void);
void SetSelectedTeamsStackTop(void);
void PushLastSelectedTeam(void);
void GetSelectedTeamsStackTop(void);
void MarkSelectedTeamsStackLocation(void);
void IsContestContinentalCup(void);
uint16_t IsClubTeamsContest(uintptr_t contest);
void SetTeamsTypeForSelectedContest(void);

/* ---- SelectTeamsFinalMenu ---- */
void SelectTeamsFinalMenu(uint16_t mode);

/* ---- National team selector utilities (Chunk 91) ---- */
uint8_t *GetNationalityRecordPtr(uint16_t teamFileAndOrdinal);
int16_t GetNationalTeamNumber(uint16_t nationality);
int16_t GetNationalTeamNumberFromTeamFileId(uint16_t teamFileId);
uint32_t GetNationalityAndTeamFile(uint16_t teamFileAndOrdinal);
int16_t IsTeamCodeOkForNationalSelector(uint16_t code);
int16_t IsPlayerOkForNationalSelector(void);

#endif
