/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* diy_competition.h — DIY competition/tournament management, player pricing */
#pragma once
#include "game_types.h"

/* --- Helpers that replace the historical `comp + 0xN` byte arithmetic.
 *
 * On 64-bit builds the embedded DIYCompetition base grows from 0x60 to
 * 0x68 (one pointer field + tail alignment), so every Amiga byte
 * literal (`+0x2B4`, `+0xEE`, etc.) is wrong by 8 bytes. Express the
 * accesses via the typed struct fields instead — `offsetof` /
 * field-address forms adjust automatically with the struct layout.
 *
 * `diy_standings_row(comp, idx)` returns the DIYTeamStats row for team
 * `idx` (0..63) in the per-competition standings table. UNTANGLE (roadmap
 * step 3): the standings used to overlay `cupTeamIds[51]` (Amiga
 * `diyCompetitionStart + 0x2B4`), which let a league (sub-)slot's
 * standings write zero a cup (sub-)slot's teamGridPairings /
 * teamOffsetTable. They are now the explicit trailing `leagueStandings[64]`
 * field, clear of the cup scratch — see
 * docs/TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md. */
static inline DIYTeamStats *diy_standings_row(DIYCompetitionEx *comp, int idx) {
    return &comp->leagueStandings[idx];
}
static inline DIYTeamStats *diy_standings_row_from_raw(uint8_t *compRaw, int idx) {
    return diy_standings_row((DIYCompetitionEx *)compRaw, idx);
}

/* Byte offset from a DIYCompetitionEx base to its standings table.
 * Used by call sites that already hold a `comp_base + grid_idx*18`
 * pointer ("team position pointer") and just need to add this offset
 * to reach the corresponding standings row. Now the offset of the
 * relocated `leagueStandings` field (was `cupTeamIds + 51*2` while the
 * standings overlaid the cup scratch); still offsetof-derived so it
 * adjusts automatically for the 32/64-bit struct growth. */
#include "swos_types.h"
#define DIY_STANDINGS_OFFSET  (offsetof(DIYCompetitionEx, leagueStandings))

/* --- National team competition check (Chunk 151) --- */
uint16_t IsNationalTeamCompetitionOrQualification(uint32_t key);

/* --- Player Price Utilities --- */
uint16_t GetAveragePlayerPrice(TeamFileEx *team);
uint32_t GetLowestAndHighestPlayerPrice(void);
uint16_t CountNearnessFactor(uint16_t curBallQuadrant, uint16_t dstBallQuadrant,
                              uint16_t price, const uint8_t *curQuadDistFactor,
                              const uint8_t *_playerPositions, const uint8_t *coordToQuad);

/* --- DIY Team Shuffling --- */
void DIY_ShuffleTeams(uint8_t *diy, uint8_t *slot);
void DIY_SortAndShuffleTeamsByValue(uint8_t *diy, uint8_t *slot);
void DIY_ShuffleLeagueTeamOrder(void);

/* --- DIY Initialization --- */
void InitDIYTournament(void);
void DIY_InitCompetitionDefaults(void);
void DIY_ResetCompetitionProgressAndShuffle(void);
void DIYTournament_RecalculateMatchesPerTeam(void);

/* --- DIY Team Management --- */
void DIY_CacheSelectedTeamPointers(void);
uint16_t DIY_CountUnselectedTeams(void);

/* --- DIY League Table --- */
void DIY_SortLeagueTableByStandings(void);
void DIY_UpdateLeagueQualifiers(void);
int16_t DIY_RestoreLeagueQualifiersAfterLoad(void);

/* --- DIY Strength/Bracket --- */
uint32_t DIY_ComputeAverageTeamStrengthTier(uint16_t count, uint8_t *table);
void DIY_InitCupBracketProgressionTable(void);
void DIYTournament_BuildMatchesPerTeamProgressionTable(void);

/* --- Cup Round String Utilities --- */
uint16_t GetNextCupRoundStringIndex(uint16_t cur, uint16_t n, uint16_t add);
const char *GetNextCupRoundString(uint16_t cur, uint16_t n, uint16_t add);
uint16_t GetCupRoundStringIndex(uint16_t cur, uint16_t flag, uint16_t idx,
                                  uint16_t stage, uint16_t opt);
const char *GetCupRoundString(uint16_t cur, uint16_t flag, uint16_t idx,
                                uint16_t stage, uint16_t opt);

/* --- DIY League Scratch Buffer --- */
void DIY_LoadLeagueScratchFromTable(void);
void DIY_SaveLeagueScratchToTable(void);

/* --- DIY Cup Init & Helpers --- */
void InitDIYCup(void);
void InitCompetitionFromRecord(uint16_t index, uint8_t *record);
void InitDIYCompetitionFromTableEntry(uint16_t idx, uint8_t *table);
void InitDIYCupFromRecord(uint8_t *cupRec);
void InitDIYLeagueFromRecord(uint8_t *leagueRec);
void DIY_ShuffleTeamOrderList(void);
void DIY_ShuffleTeamGridPairs(void);
void DIY_SyncProgressToDiyFileBufferCopy(void);
void DIY_ValidateLeagueScratchBuffer(void);

/* --- DIY Cup Bracket & Team Grid --- */
void DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs(void);
void DIY_ReorderTeamGridByOrderList(void);
void DIY_RecountMissingTeamsInTeamGrid(void);
void DIY_BuildTeamSelectionPointerCache(void);
void DIY_InitCupBracketState(void);

/* --- DIY Buffer Copy Operations --- */
void SaveDIYTournament(void);
void RestoreDIYTournament(void);
void DIY_CopyCompetitionFileBufferToDiyFileBufferCopy(void);
void DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer(void);
void DIY_CopyCompetitionFileBufferToCompetitionStart(void);
void DIY_CopyCompetitionStartToCompetitionFileBuffer(void);
void DIY_CopyCompetitionFileBufferToCompetitionStart_Alt(void);
void DIY_CopyCompetitionStartToCompetitionFileBuffer_Alt(void);
void SaveDIYLeague(void);
void RestoreDIYLeague(void);
void DIY_SaveCompetitionFileBufferToDIYCompetition(void);
void DIY_RestoreCompetitionFileBufferFromDIYCompetition(void);

/* --- Competition Derived Fields & Fixture Helpers --- */
void DIY_RecomputeCompetitionDerivedFields(void);
void DIY_GetCurrentFixtureTeamPtrs(void);
void DIY_AdvanceLeagueProgressAfterMatch(uint8_t *team1, uint8_t *team2);
uint16_t DIY_GetCurrentFixtureTeamClass(void);

/* --- Qualified Team List Building --- */
void DIY_BuildQualifiedTeamListFromSortedTable(uint16_t count, uint8_t *out);
void DIY_BuildQualifiedTeamListFromDiyFileBuffer(uint16_t count, uint8_t *out);
void DIY_SyncCompetitionTeamGridOrBuildBracket(void);

/* --- Competition Team Index & Promotion/Relegation --- */
/* Returns advanced cursor; caller chains across per-division calls so
 * promotion lists accumulate rather than overwrite. See definition. */
uint8_t *DIY_CopyCompetitionTeamIndexTable(uint8_t *dst);
void DIY_ApplyPromotionRelegationWeightsToScratch(const uint8_t *weights);
void DIY_UpdatePromotionRelegationScratchCounts(void);

/* --- Career Team Search (Chunk 89) --- */
uint32_t Career_IsTeamIdNotInListOrReturnSentinel(uint16_t teamId, uint16_t *endPtr);
uint32_t Career_IsTeamIdNotInListOrReturnSentinel2(uint16_t teamId, uint16_t *endPtr);

/* --- Chunk 96: DIY Competition Slot Editing & Progress --- */
void DIY_EditCompetitionSlot(uint16_t paramA, uint16_t paramB, uint16_t paramC);
void DIY_ReorderTeamsBySelectionFlags(void);
void DIY_UpdateCompetitionProgress(void);
void DIY_RecordCupBracketProgress(void);
void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);
void DIY_RecordLeagueFixtureResultAndAdvanceProgress(void);
void DIY_RebuildCompetitionAndTeamSelectionCaches(void);

/* --- Career post-load dispatch (loc_12D4B0) --- */
void DIY_PostLoadCompetitionDispatch(void);

/* --- Chunk 97: DIY Competition Setup Functions --- */
void DIY_LoadCurrentCompetitionSlot(void);
void DIY_ResetAndLoadCurrentCompetitionSlot(void);
void DIY_InitCupFromTeamGridAndValidateTeams(void);
void DIY_ShuffleTeamsAndBuildCompTeamList(uint8_t *diy, uint8_t *slot,
                                            uint8_t *comp, uint8_t *comp5);
void DIY_BuildTeamGridAndIndexMaps(uint8_t *diy, uint8_t *slot,
                                     uint8_t *comp4, uint8_t *comp);
void DIY_CopyTeamGridFromCompToDIY(uint8_t *diy, uint8_t *slot,
                                     uint8_t *comp, uint8_t *comp5);
void DIY_BuildSeededBracketAndUpdateScores(uint8_t *diy, uint8_t *slot,
                                             uint8_t *comp4, uint8_t *comp);
void DIY_AdjustWorldCupBracketScores(uint8_t *diy, uint8_t *slot, uint8_t *comp);

/* ---- DIY selection menu callbacks (Chunk 108) ---- */
void SelectDiyLeagueMenu(void);
void SelectDiyCupMenu(void);
void SelectDiyTournamentMenu(void);
void SelectDiyExitMenu(void);

/* ---- DIY Design Menu functions (Chunk 120) ---- */
void ShowDesignDIYLeagueMenu(void);
void ShowLeagueInfo(void);
void DesignDIYLeagueOK(void);
void SetNumberOfTeamsToSelect(void);
void ShowDesignDIYCupMenu(void);
void ShowCupInfo(void);
void DesignDIYCupOK(void);
void DesignDIYCup_UpdateMinSubstitutesAndPointsStrings(void);
void DesignDIYCup_UpdateSelectTeamsPrompt(void);
void ShowDesignDIYTournamentMenu(void);
void ShowTournamentInfo(void);
void DesignDIYTournamentOK(void);
void DesignDIYTournament_UpdateTwoNumericFieldsText(void);
void DesignDIYTournament_UpdateSelectTeamsPrompt(void);

/* ---- DesignDIYLeague Change Callbacks (Chunk 126) ---- */
#include "menu_system.h"
void DesignDIYLeagueInit(void);
void DesignDIYLeagueSetName(MenuEntry *entry);
void DesignDIYLeagueChangePitchType(MenuEntry *entry);
void DesignDIYLeagueChangeStartMonth(MenuEntry *entry);
void DesignDIYLeagueChangeEndMonth(MenuEntry *entry);
void DesignDIYLeagueChangeNumberOfTeams(MenuEntry *entry);
void SetPlayEachTeamString(MenuEntry *entry);
void DesignDIYLeagueChangePlayEachTeam(MenuEntry *entry);
void DesignDIYLeagueChangeNumPointsForWin(MenuEntry *entry);
void ConvertMinMaxSubstitutesNumbers(void);
void DesignDIYLeagueChangeMinSubstitutes(MenuEntry *entry);
void DesignDIYLeagueChangeMaxSubstitutes(MenuEntry *entry);

/* ---- DesignDIYCup Callbacks & Helpers (Chunk 127) ---- */
void DesignDIYCup_UpdateStartEndMonthIndices(void);
void DesignDIYCup_UpdatePitchTypeOrSeasonSelectionVisibility(void);
void DesignDIYCup_SetupTeamsSelectionUI(void);
void DesignDIYCupReinit(void);
void DesignDIYCupInit(void);
void DesignDIYCupOnSelect2(MenuEntry *entry);
void DesignDIYCupOnSelect3(MenuEntry *entry);
void DesignDIYCupOnSelect4(MenuEntry *entry);
void DesignDIYCupOnSelect5(MenuEntry *entry);
void DesignDIYCupOnSelect7(MenuEntry *entry);
void DesignDIYCupOnSelect9(MenuEntry *entry);
void DesignDIYCupOnSelect11(MenuEntry *entry);
void DesignDIYCupOnSelect13(MenuEntry *entry);
void DesignDIYCupOnSelect15(MenuEntry *entry);
void DesignDIYCupOnSelect20(MenuEntry *entry);
void DesignDIYCupOnSelect21(MenuEntry *entry);
void DesignDIYCupOnSelect22(MenuEntry *entry);

/* ---- DesignDIYTournament Callbacks & Helpers (Chunk 128) ---- */
void DesignDIYTournament_UpdateMonthPageIndices(void);
void DesignDIYTournament_ApplyInactiveMenuColors(void);
void DesignDIYTournament_UpdatePitchTypeSeasonalUI(void);
void DesignDIYTournamentReinit(void);
void DesignDIYTournamentInit(void);
void DesignDIYTournamentOnSelect2(MenuEntry *entry);
void DesignDIYTournamentOnSelect3(MenuEntry *entry);
void DesignDIYTournamentOnSelect4(MenuEntry *entry);
void DesignDIYTournamentOnSelect5(MenuEntry *entry);
void DesignDIYTournamentOnSelect7(MenuEntry *entry);
void DesignDIYTournamentOnSelect9(MenuEntry *entry);
void DesignDIYTournamentOnSelect11(MenuEntry *entry);
void DesignDIYTournamentOnSelect13(MenuEntry *entry);
void DesignDIYTournamentOnSelect21(MenuEntry *entry);
void DesignDIYTournamentOnSelect24(MenuEntry *entry);
void DesignDIYTournamentOnSelect25(MenuEntry *entry);
void DesignDIYTournamentOnSelect26(MenuEntry *entry);
void DesignDIYTournamentOnSelect28(MenuEntry *entry);

/* ---- DesignDIYTournament Remaining (Chunk 129) ---- */
void DesignDIYTournamentOnDraw(void);
void DesignDIYTournamentOnSelect23(MenuEntry *entry);
void DesignDIYTournamentOnSelect20(MenuEntry *entry);
void DesignDIYTournament_AdjustStageQualifiers(MenuEntry *entry);
void DesignDIYTournament_CacheStageSelection(MenuEntry *entry);
void DesignDIYTournament_RedrawStageItemsFromBlock(uint8_t *blk, uint16_t count);
void DIYTournament_RecalculateNumStages(void);

/* ---- DesignDIYCup Draw & Helpers (Chunk 130) ---- */
void DesignDIYCup_BuildMatchListEntryStrings(void);
void DesignDIYCup_UpdateMenuEntriesVisibilityAndSelection(void);
void DesignDIYCupOnDraw(void);
void DesignDIYCupOnSelect48(void);
void DesignDIYCup_UpdateRoundOptionsVisibility(void);
void DesignDIYCup_BuildMenuTitleString(void);
void DesignDIYCup_UpdateCurrentMatchTeamEntryStrings(void);
void DesignDIYCup_DrawCurrentMatchTeamEntries(void);
void DesignDIYCup_HideCurrentMatchTeamEntries(void);
int16_t DesignDIYCup_UpdateMatchListScrollAndRebuildStrings(void);
void DesignDIYCup_UpdateMatchListAndRedrawIfNeeded(void);
void DesignDIYCup_UpdateCurrentMatchResultHeaderEntries(void);

/* ---- DesignDIYLeague remaining + DIYCup menu helpers (Chunk 131) ---- */
void DIYSetMonthsOrPitchTypeEntries(void);
void DIYLeagueSetVariousFields(void);
void DIYLeagueOKSelected(void);
void DesignDIYLeagueUpdatePromotionRelegationText(void);
void DesignDIYLeagueFinish(void);
void DesignDIYLeagueChooseTeams(void);
void ShowDIYCupMenuWithParams(uint16_t arg0, uint16_t arg1, uint16_t arg2);
void DIYCupFinish(void);
void DIYCupInit(void);
void DIYCupOnSelect10(void);
void DIYCupOnSelect16(void);
void DIYCupOnSelect17(void);

/* ---- DIYCup/League/Tournament select callbacks (Chunk 132) ---- */
void DIYLeagueChangePitchTypeOrSeason(void);
void DIYCupOnSelect1(void);
void DIYCupOnSelect3(MenuEntry *entry);
void DIYCupOnSelect4(MenuEntry *entry);
void DIYCupOnSelect5(MenuEntry *entry);
void DIYCupOnSelect6(MenuEntry *entry);
void DIYCupOnSelect7(MenuEntry *entry);
void DIYCupOnSelect8(MenuEntry *entry);
void DesignDIYTournamentOnSelect89(void);

/* ---- DIY Competition Stats Menu (Chunk 141) ---- */
void SetupDIYCompetitionStatsMenu(void);

/* ---- OpenDiy* + ShowDiyCompetitionForSource (Chunk 116) ---- */
void ShowDiyCompetitionForSource(uint8_t *header);
void OpenDiyFromFile(void);
void OpenDiyPreset1(void);
void OpenDiyPreset2(void);
void OpenDiyPreset3(void);

/* --- Choose teams helpers (Chunk 148) --- */
void ChooseTeamsSetHeader(void);
void CheckNumTeamsAllowed(void);
void Bureaucracy(void);

/* --- DIY League/Tournament Menu (Chunk 150) --- */
void DIYLeagueTournamentStats(void);
void DIYLeagueTournamentExit(void);
void DIYLeagueTournament_UpdateViewOptionsAvailability(void);
void DIYSetTeamNamesAndResults(void);
void DIYLeagueTournamentScrollUp(void);
void DIYLeagueTournamentScrollDown(void);
void DIYLeagueTournament_ScrollFixtureListToCurrentFixture(void);
uint16_t DIYLeagueTournament_EnsureCurrentFixtureVisible(void);
void DIYLeagueTournament_SetupCurrentFixtureTeamNames(void);
void DIYLeagueTournament_DrawPlayMatchAndFixtureHeader(void);
void DIYLeagueTournamentNextMatch(void);
void DIYLeagueTournamentInit(void);

/* --- DIY League/Tournament Match Callbacks & Visibility (Chunk 152) --- */
void DIYLeagueTournamentMatch(MenuEntry *entry);
void DIYLeagueTournamentOnSelect4(MenuEntry *entry);
void DIYLeagueTournamentResult(MenuEntry *entry);
void DIYLeagueTournamentViewMatch(MenuEntry *entry);
void DIYLeagueTournamentViewHighlights(MenuEntry *entry);
void DIYLeagueTournamentViewResult(MenuEntry *entry);
void DIYLeagueTournamentMatchCommon(uint16_t action);
void DIYLeagueTournament_UpdateMenuEntryVisibilityAndFocus(void);

/* --- DIYLeagueTournamentOnRestore + DIYLeagueTournament16BeforeDraw (Chunk 229) --- */
void DIYLeagueTournamentOnRestore(void);
void DIYLeagueTournament16BeforeDraw(MenuEntry *entry);

/* --- ShowDiyLeaguesTournamentMenu (Chunk 153) --- */
void ShowDiyLeaguesTournamentMenu(uint16_t type, uint16_t viewOnly, uint16_t focusOverride);

/* --- GetMoreExpensiveTeamsCount (Chunk 171) --- */
uint16_t GetMoreExpensiveTeamsCount(uint16_t teamNo, DIYCompetition *comp);

/* --- InitializeNewSeason_BuildTeamBudgetRankTable (Chunk 172) --- */
void InitializeNewSeason_BuildTeamBudgetRankTable(void);

/* --- FindTeamInDIY (Chunk 173) --- */
uint16_t FindTeamInDIY(uint16_t teamNo, DIYCompetition *comp);

/* --- DIY_RecomputeCompetitionDerivedProgressionTable (Chunk 175) --- */
void DIY_RecomputeCompetitionDerivedProgressionTable(void);

/* --- DIY_GetCurrentMatchTeamSlots, DIY_CompareCurrentMatchTeamCategories (Chunk 191) --- */
void DIY_GetCurrentMatchTeamSlots(uint8_t **outSlot1, uint8_t **outSlot2);
uint16_t DIY_CompareCurrentMatchTeamCategories(void);

/* --- Match Play & Result Caching --- */
uint16_t PlayDIYLeagueMatchAndCacheResult(TeamFileEx *team1, TeamFileEx *team2,
                                            int16_t allowSquadChanges, int16_t mode);
uint16_t PlayDIYCupFixtureAndCacheResult(TeamFileEx *team1, TeamFileEx *team2,
                                           int16_t allowSquadChanges, int16_t mode);

/* --- Match Orchestration (Chunk 193) --- */
uint16_t DIY_PlayCupMatchAndAdvance(int16_t allowSquadChanges, int16_t mode);
uint16_t DIY_PlayOrSimulateLeagueFixture(int16_t allowSquadChanges, int16_t selector);

/* --- DIY team-selection resolvers (step-4 phase 2) ---
 * Resolve a DIY team-selection value (DIYTeamStats.teamSelOffset /
 * bracketScratch.teamOffsetTable[]) to the team record / team-name address.
 * See docs/CAREERTEAM_STEP4_REWORK.md. */
uint8_t *diy_resolve_team(int32_t teamSel);
uint8_t *diy_resolve_team_name(int32_t teamSel);
int32_t  diy_team_to_identity(const uint8_t *teamPtr);

/* --- Match Result Processing (Chunk 190) --- */
void ProcessDIYLeagueMatchResultAndCareerUpdates(TeamFileEx *team1, TeamFileEx *team2,
    uint16_t *outGoals1, uint16_t *outGoals2, uint16_t *outFlags);
void ProcessDIYKnockoutMatchResultAndCareerUpdates(TeamFileEx *team1, TeamFileEx *team2,
    uint16_t inFirstLegTeam1, uint16_t inFirstLegTeam2,
    uint16_t *outPackedGoals, uint16_t *outPackedAggregate,
    uint16_t *outPackedPenalty, uint16_t *outFlags);
