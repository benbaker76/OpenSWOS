/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef MENUS_H
#define MENUS_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;

struct TeamFile;
typedef struct TeamFile TeamFile;
struct PlayerFile;
typedef struct PlayerFile PlayerFile;

void PrepareMenuBackground(void);
void InitMainMenuStuff(void);
void ZeroOutStars(void);
void SetDefaultOptions(void);
void SetDefaultNameAndSurname(void);
void InitCareerVariables(uint16_t gameTypeParam);
void InitUserTactics(void);
void InitializeTacticsPositions(void);
void SaveOptions(void);
void RestoreOptions(void);
void ShowErrorMenu(uint8_t *errorTextStr);
int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing, uint8_t *contText, uint8_t *abortTextStr, uint16_t defaultEntry);
void ContinueAbortInit(void);
void ContinueSelected(void);
void AbortSelected(void);
void ContinueInit(void);
void ContinueMenuContinueSelected(void);

/* --- Stadium menu (Chunk 89) --- */
struct TeamGame;
void ShowStadiumInit(void);
void StadiumPlayerSpritesBeforeDraw(struct TeamGame *team, uint16_t playerIndex);
void StadiumBeforeDrawLeftTeam(void *entry);
void StadiumBeforeDrawRightTeam(void *entry);

/* --- Squad menu callbacks (Chunk 111) --- */
void SelectTraining(void);
void SelectGoals(void);
void SquadOnSelect11(void);
void SquadOnSelect45(void);
void Squad45AfterDraw(void);

/* --- Squad operations (Chunk 112) --- */
void UpdateBigCheckMark(void);

/* --- Squad menu functions (Chunk 208) --- */
void SquadUpdateGoalsMenuText(void);
void SquadCommon(void *entryPtr);
void SquadRedrawPlayerListAndCheckMark(void);
void SquadMenuSavePlayerPositions(void);
void SquadCommitPositionsAndUpdateSkill(void);

/* --- Squad Init/Finish (Chunk 209) --- */
void SquadInit(void);
void SquadFinish(void);

/* --- SquadUpdateMenuState (Chunk 210) --- */
void SquadUpdateMenuState(void);

/* --- SquadAfterDrawCommon (Chunk 211) --- */
void SquadAfterDrawCommon(void *entryPtr);

/* --- Options menu callbacks (Chunk 113) --- */
#include "menu_system.h"
void OptionsMenuBeforeDraw(void);
void ChangePitchType(MenuEntry *entry);
void ChangeGameLength(MenuEntry *entry);
void ChangeAutoReplays(MenuEntry *entry);
void ChangeAllPlTeamsEqual(MenuEntry *entry);
void ChangeAutoSaveHighlights(MenuEntry *entry);
void ChangeMenuMusic(MenuEntry *entry);

/* --- Simple menu exit/draw callbacks (Chunk 114) --- */
void CompetitionTopGoalScorersExit(void);
void TopGoalScorersExit(void);
void BuyPlayerExit(void);
void ViewSquadExitContinue(void);
void ViewSquad21AfterDraw(void);
void SelectNationalTeamExit(void);
void SelectNationalTeam25AfterDraw(void);
void SelTeamForTacticsExit(void);
void AbortSaveFile(void);

/* --- CareerOver + ViewSquad + National callbacks (Chunk 115) --- */
void CareerOverInit(void);
void ViewSquadOnSelect21(void);
void SelectNationalTeamOnSelect25(void);

/* --- Menu navigation callbacks (Chunk 116) --- */
void SWOSMainMenuInit(void);
void ManagementRecordMenu(void);
void ViewCompetitionsMenuRetired(void);

/* --- CommonMenuExit (Chunk 118) --- */
void CommonMenuExit(void);

/* --- Menu callbacks + friendly/national (Chunk 119) --- */
void FriendlyMenu(void);
void EditCustomTeamMenu(void);
void EditTacticsMenu(void);
void HighlightsMenu(void);
void OptionsMenuSelected(void);
void SaveDiskFilingMenu(void);
void ShowFriendlyMenu(void);
uint16_t ShowSelectRepresentationMenu(void);
void SetSelectTwoTeamsText(void);

/* --- Simple menu callbacks (Chunk 121) --- */
void FriendlyOK(void);
void ViewSelectedTeams(void);
void ContestMenuScrollUp(void);
void ContestMenuScrollDown(void);

/* --- Contest menu helpers (Chunk 122) --- */
void ScrollUpInGameList(void);
void ScrollDownInGameList(void);
void RestoreContestScrollPosition(void);
void SelectActiveContestAndRound(void);
void CheckClubBusiness(void);
void CheckJobOffers(void);
void ContestMenuOnReturn_SetButtonsAndDefaultSelection(MenuEntry *a5);
void SeasonLowerButtons(void);
void CheckTransfers(void);
void BuildGameListArrayFromTeamSelectionKeyCache(void);
void PrintNameAndSeason(void);
void SetNationalSelectorButtons(void);
void ContestSetButtons(void);
void ContestMenuInit(void);
void ContestMenuOnReturn(void);

/* --- Contest menu action handlers (Chunk 123) --- */
void PlayMatchMenu(MenuEntry *callerEntry);
void NextMatch(MenuEntry *callerEntry);
void ViewCompetitionsMenu(void);
void ClubBusinessMenu(void);
void JobOffersMenu(void);
void SeasonFinished(void);
void TransfersMenu(void);
void ViewWorldMenu(void);

/* --- Choose/select team menu functions (Chunk 125) --- */
void ChooseCompetitionMenu(void);
void SelectTeamsForPresetCompetition(void);
void ChooseSeasonMenu(void);
void GoGetTeamsForPlay(uint8_t *gameTypeStrParam, uint8_t *playWhatStrParam,
                        uint16_t numTeams, uint16_t showClub,
                        uint16_t showNational, uint16_t showOrigCustom);

/* --- Chunk 201: ChooseTeamsDialog --- */
void ChooseTeamsDialog(uint16_t showClub, uint16_t showNational, uint16_t showOrigCustom,
                        uint8_t *title, void *onSelect);
void ChooseTeamsDialogWithNationality(uint16_t nationality, uint16_t showClub,
                                       uint16_t showNational, uint16_t showOrigCustom,
                                       uint8_t *title, void *onSelect);

/* --- Chunk 202: SelectTeamToManage --- */
void SelectTeamToManage(void);

/* --- Chunk 204: Career_ShowAddInternationalPlayerDialog --- */
void Career_ShowAddInternationalPlayerDialog(uint16_t homeOrAbroadFlag);

/* --- Chunk 205: Career_AddInternationalPlayerCommon + Home/Abroad --- */
void Career_AddInternationalPlayerCommon(uint8_t *selectedTeam, uint8_t *title);
void AddInternationalPlayerHome(uint8_t *selectedTeam);
void AddInternationalPlayerAbroad(uint8_t *selectedTeam);

/* --- Chunk 206: National Team Selection family --- */
void SelectNationalTeamInit(void);
void SelectNationalTeamFinish(void);
void Career_RebuildInternationalPlayerSelectionMasks(void);
void NationalTeamAddPlayer(void);
void NationalTeamRemovePlayers(void);
void SelectNationalTeamAbort(void);
void SelectNationalTeamCommon(void *entryPtr);
void SelectNationalTeamAfterDrawCommon(void *entryPtr);

/* --- Competition menu functions (Chunk 124) --- */
void DIYCompetitionMenu(void);
void PresetCompetitionMenu(void);
void SeasonMenu(void);
void CareerMenu(void);
void StartContest(void);
void SaveContest(void);
void LoadOldCompetitionMenu(void);

/* Chunk 139 — Team selection type handlers */
void ClubTeams(void);
void NationalTeams(void);
void OriginalCustomTeams(void);
void EditedCustomTeams(void);
void ExitChooseTeams(void);
void SetLeagueNames(void);
void SetCountryNames(void);

/* ---- Menu Entry Utilities (Chunk 141) ---- */
#include "menu_system.h"
void SetMenuEntryPlayerFace(MenuEntry *entry, PlayerFile *playerFile);

/* --- SaveCareer (Chunk 142) --- */
void SaveCareer(void);

/* --- ExitOptions + SquadMenuStatusSprite (Chunk 143) --- */
void ExitOptions(void);
void SetSquadMenuEntryStatusSprite(MenuEntry *entry, PlayerFile *playerFile, uint16_t flag);
void SetSquadMenuEntryStatusSprite_AfterSim(MenuEntry *entry, PlayerFile *playerFile);

/* --- Friendly pitch/season menu callbacks (Chunk 147) --- */
void SetFriendlyPitchOrSeason(void);
void PitchTypeOrSeasonProc(void);
void FriendlyPitchTypeProc(MenuEntry *entry);

/* --- Friendly menu cluster + RandomlySwapTeams (Chunk 149) --- */
void Friendly(void);
void DrawNumbersOfSubstitutes(void);
void DisableEntries(void);
void FriendlyMenuInit(void);
void FriendlyMenuReinit(void);
void ChooseTeamsMenu(void);
void CheckNumberOfTeams(void);
void MinSubstitutes(void);
void MaxSubstitutes(void);
void RandomlySwapTeams(void);

/* Set by a chooseTeamsFunc callback that has DONE its job and wants the club
 * list left open instead of closed. ChooseTeamsFriendlyCommon2 consumes and
 * clears it; nothing else sets it.
 *
 * The original closes the list after any selection the callback reports
 * (SWOS2.s:65118 — `tst.w careerSquadScreenDidSelectPlayerFlag / bne` ->
 * abortSelectTeams + SetExitMenuFlag). For adding international players that
 * means one club per visit: add a player, get returned to the squad screen, and
 * walk ADD PLAYER -> HOME/ABROAD -> league -> club again for the next one.
 * Reported by a player, and changed deliberately. */
extern uint16_t chooseTeamsKeepListOpen;

/* --- ShowSquad (Chunk 164) --- */
int16_t ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo,
                    uint32_t maskHi, uint16_t arg2, uint16_t arg3);

/* --- ViewSquad Init/Common/AfterDraw (Chunk 163) --- */
void ViewSquadInit(void);
void ViewSquadCommon(void *entry);
void ViewSquadAfterDrawCommon(void *entry);

/* --- Chunk 217: SelectAndShowSquadToView, StatsViewSquads, SquadMenu --- */
void SelectAndShowSquadToView(uint16_t *teamNumbers, uint16_t numTeams, uint16_t rowMode);
void SelectAndShowSquadToView_Direct(uint16_t rowMode);
void StatsViewSquads(void);
void SquadMenu(void);

/* --- Chunk 230: CareerGameListCommon (called from asm only) --- */

/* A career screen's row for the mods (docs/MODS.md §28, include/mod_api.h MODPOINT_BUY_PLAYER ...):
 * ModRow_Show, from the screen's init, shows menu entry `entry` with the point's label and gives
 * 1 -- or hides it and gives 0 (no mod's entry there; always on the Amiga). The four rows' onSelect
 * (BuyPlayerModRow ...) run the entry or list them. */
enum { MODROW_BUY_PLAYER, MODROW_TRANSFER_MARKET, MODROW_NATIONAL_ADD_PLAYER, MODROW_COMPETITION_STATS };
int32_t ModRow_Show(uint16_t entry, int32_t row);
void BuyPlayerModRow(void);
void TransferMarketModRow(void);
void NationalTeamAddPlayerModRow(void);
void CompetitionStatsModRow(void);

#endif
