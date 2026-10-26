/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "menu_data.h"

extern void MultiplayerMenuSelected(void);
extern void CreditsMenuSelected(void);
extern void SetExitMenuFlag(void);
extern void TopGoalScorersMenu_0(void);
extern void SetupDIYCompetitionStatsMenu(void);
extern DIYCompetitionEx _diyCompetitionStart;
extern DIYTournamentState _diyFileBufferCopy;  /* header field was the diyTournamentCompetition symbol */

/* ---- Stub functions (trivial wrappers removed from SWOS2.s) ---- */
void GlobalRetn(void) { }
void NationalTeamAddPlayerInit(void) { }
void locret_139AE2(void) { }
void ExitPlayerMarketMenu(void) { SetExitMenuFlag(); }
void DesignDIYTournamentOnSelect91(void) { SetExitMenuFlag(); }
void TopGoalScorersMenu(void) { TopGoalScorersMenu_0(); }
void DIYCupOnSelect9(void) { SetupDIYCompetitionStatsMenu(); }

/* ---- External function declarations ---- */
extern void AbortChooseTeamsFriendly(void);
extern void AbortEditTactics(void);
extern void AbortSaveFile(void);
extern void AbortSelected(void);
extern void AcceptJobOffer(void);
extern void AcceptOffer(void);
extern void AskForMore(void);
extern void BuyOtherDomesticPlayer(void);
extern void BuyOtherForeignPlayer(void);
extern void BuyOtherPlayersAfterDrawCommon(void);
extern void BuyOtherPlayersCommon(void);
extern void BuyOtherPlayersFinish(void);
extern void BuyOtherPlayersInit(void);
extern void BuyPlayerExit(void);
extern void BuyPlayerInit(void);
extern void BuyPlayerMenu(void);
extern void BuyPlayerViewSquad(void);
extern void CareerGameListCommon(void);
extern void CareerMenu(void);
extern void CareerMenuFinish(void);
extern void CareerMenuInit(void);
extern void CareerOverFinish(void);
extern void CareerOverInit(void);
extern void ChairmanMemosContinue(void);
extern void ChairmanMemosInit(void);
extern void ChairmanMemosOnReturn(void);
extern void ChangeAllPlTeamsEqual(void);
extern void ChangeAutoReplays(void);
extern void ChangeAutoSaveHighlights(void);
extern void ChangeGameLength(void);
extern void ChangeMenuMusic(void);
extern void ChangeGameplayStyle(void);
extern void ChangeCpuCards(void);
extern void ShowControlsMenu(void);
extern void ChangePitchStyle(void);
extern void ChangeNationality(void);
extern void ChangePitchType(void);
extern void ChangeSex(void);
extern void ChangeTeams(void);
extern void ChooseTacticsInit(void);
extern void ChooseTeamAndStartCareer(void);
extern void ChooseTeamsFriendlyCommon(void);
extern void ChooseTeamsFriendlyCommon2(void);
extern void ChooseTeamsFriendlyInit(void);
extern void ChooseTeamsFriendlyInitFinish(void);
extern void ChooseTeamsFriendlyOnSelect76(void);
extern void ChooseTeamsInit(void);
extern void ChooseTeamsMenu(void);
extern void ChooseTeamsPlayGame(void);
extern void ChooseTeamsPlayMatchSelected(void);
extern void ClubBusinessInit(void);
extern void ClubBusinessMenu(void);
extern void ClubTeams(void);
extern void CoachOrPlayerCoach(void);
extern void CompetitionInfo(void);
extern void CompetitionTopGoalScorersExit(void);
extern void CompetitionTopGoalScorersInit(void);
extern void ConsiderJobOffer(void);
extern void ConsiderOffer(void);
extern void ContestMenuInit(void);
extern void ContestMenuOnReturn(void);
extern void ContestMenuScrollDown(void);
extern void ContestMenuScrollUp(void);
extern void ContinueAbortInit(void);
extern void ContinueCareer(void);
extern void ContinueInit(void);
extern void ContinueMenuContinueSelected(void);
extern void ContinueSelected(void);
extern void CopyTactics(void);
extern void DIYCompetitionMenu(void);
extern void DIYCupFinish(void);
extern void DIYCupInit(void);
extern void DIYCupOnSelect1(void);
extern void DIYCupOnSelect10(void);
extern void DIYCupOnSelect16(void);
extern void DIYCupOnSelect17(void);
extern void DIYCupOnSelect3(void);
extern void DIYCupOnSelect4(void);
extern void DIYCupOnSelect5(void);
extern void DIYCupOnSelect6(void);
extern void DIYCupOnSelect7(void);
extern void DIYCupOnSelect8(void);
extern void DIYLeagueChangePitchTypeOrSeason(void);
extern void DIYLeagueTournament16BeforeDraw(void);
extern void DIYLeagueTournamentExit(void);
extern void DIYLeagueTournamentInit(void);
extern void DIYLeagueTournamentMatch(void);
extern void DIYLeagueTournamentNextMatch(void);
extern void DIYLeagueTournamentOnRestore(void);
extern void DIYLeagueTournamentOnSelect4(void);
extern void DIYLeagueTournamentResult(void);
extern void DIYLeagueTournamentScrollDown(void);
extern void DIYLeagueTournamentScrollUp(void);
extern void DIYLeagueTournamentStats(void);
extern void DIYLeagueTournamentViewHighlights(void);
extern void DIYLeagueTournamentViewMatch(void);
extern void DIYLeagueTournamentViewResult(void);
extern void DesignDIYCupInit(void);
extern void DesignDIYCupOK(void);
extern void DesignDIYCupOnDraw(void);
extern void DesignDIYCupOnSelect11(void);
extern void DesignDIYCupOnSelect13(void);
extern void DesignDIYCupOnSelect15(void);
extern void DesignDIYCupOnSelect2(void);
extern void DesignDIYCupOnSelect20(void);
extern void DesignDIYCupOnSelect21(void);
extern void DesignDIYCupOnSelect22(void);
extern void DesignDIYCupOnSelect3(void);
extern void DesignDIYCupOnSelect4(void);
extern void DesignDIYCupOnSelect48(void);
extern void DesignDIYCupOnSelect5(void);
extern void DesignDIYCupOnSelect7(void);
extern void DesignDIYCupOnSelect9(void);
extern void DesignDIYCupReinit(void);
extern void DesignDIYLeagueChangeEndMonth(void);
extern void DesignDIYLeagueChangeMaxSubstitutes(void);
extern void DesignDIYLeagueChangeMinSubstitutes(void);
extern void DesignDIYLeagueChangeNumPointsForWin(void);
extern void DesignDIYLeagueChangeNumberOfTeams(void);
extern void DesignDIYLeagueChangePitchType(void);
extern void DesignDIYLeagueChangePlayEachTeam(void);
extern void DesignDIYLeagueChangeStartMonth(void);
extern void DesignDIYLeagueChooseTeams(void);
extern void DesignDIYLeagueFinish(void);
extern void DesignDIYLeagueInit(void);
extern void DesignDIYLeagueOK(void);
extern void DesignDIYLeagueSetName(void);
extern void DesignDIYTournamentInit(void);
extern void DesignDIYTournamentOK(void);
extern void DesignDIYTournamentOnDraw(void);
extern void DesignDIYTournamentOnSelect11(void);
extern void DesignDIYTournamentOnSelect13(void);
extern void DesignDIYTournamentOnSelect2(void);
extern void DesignDIYTournamentOnSelect20(void);
extern void DesignDIYTournamentOnSelect21(void);
extern void DesignDIYTournamentOnSelect23(void);
extern void DesignDIYTournamentOnSelect24(void);
extern void DesignDIYTournamentOnSelect25(void);
extern void DesignDIYTournamentOnSelect26(void);
extern void DesignDIYTournamentOnSelect28(void);
extern void DesignDIYTournamentOnSelect3(void);
extern void DesignDIYTournamentOnSelect4(void);
extern void DesignDIYTournamentOnSelect5(void);
extern void DesignDIYTournamentOnSelect7(void);
extern void DesignDIYTournamentOnSelect89(void);
extern void DesignDIYTournamentOnSelect9(void);
extern void DesignDIYTournamentReinit(void);
extern void DesignDIYTournament_AdjustStageQualifiers(void);
extern void DrawPlayerEntries(void);
extern void EditCustomTeamMenu(void);
extern void EditMenuSelected(void);
extern void EditTacicsMovePlayersOrBall(void);
extern void EditTactics28AfterDraw(void);
extern void EditTactics2AfterDraw(void);
extern void EditTacticsAfterDrawPlayerFace(void);
extern void EditTacticsAfterDrawPlayerNames(void);
extern void EditTacticsCommon(void);
extern void EditTacticsInPlayMatchMenu(void);
extern void EditTacticsInit(void);
extern void EditTacticsMenu(void);
extern void EditTacticsOnRestore(void);
extern void EditTacticsOnSelect28(void);
extern void EditTacticsPlayerNamesCommon(void);
extern void EditTeamsChangePlayerFace(void);
extern void EditTeamsChangePlayerName(void);
extern void EditTeamsChangeShirtsPrimary(void);
extern void EditTeamsChangeShirtsSecondary(void);
extern void EditTeamsChangeShortsPrimary(void);
extern void EditTeamsChangeShortsSecondary(void);
extern void EditTeamsChangeSocksPrimary(void);
extern void EditTeamsChangeSocksSecondary(void);
extern void EditTeamsFinish(void);
extern void EditTeamsInit(void);
extern void EditTeamsOK(void);
extern void EditTeamsPlayersBeforeDraw(void);
extern void EditTeamsSelectCoachName(void);
extern void EditTeamsSelectPrimaryShirtType(void);
extern void EditTeamsSelectSecondaryShirtType(void);
extern void EditTeamsSelectTeamName(void);
extern void EditTeamsUndo(void);
extern void EditedCustomTeams(void);
extern void ExitCareerMenu(void);
extern void ExitChooseTacticsMenu(void);
extern void ExitChooseTeams(void);
extern void ExitChooseTeams2(void);
extern void ExitHighlights(void);
extern void ExitImportTactics(void);
extern void ExitOptions(void);
extern void ExitPlayMatch(void);
extern void ExitSelectTeams(void);
extern void FillManagementRecordInfo(void);
extern void FlipOnOff(void);
extern void FriendlyMenu(void);
extern void FriendlyMenuInit(void);
extern void FriendlyMenuReinit(void);
extern void FriendlyOK(void);
extern void FriendlyPitchTypeProc(void);
extern void GiveFreeTransfer(void);
extern void GotJobOfferFinish(void);
extern void GotJobOfferInit(void);
extern void HighlightsMenu(void);
extern void HighlightsMenuInit(void);
extern void HighlightsMenuOnReturn(void);
extern void ImportTactics(void);
extern void ImportTacticsInit(void);
extern void ImportTacticsSelectTactics(void);
extern void ImportTeamsMenu(void);
extern void JmpShowHighlights(void);
extern void _JobOffer(void);
extern void JobOffersAfterDrawCommon(void);
extern void JobOffersCommon(void);
extern void JobOffersExit(void);
extern void JobOffersFinish(void);
extern void JobOffersInit(void);
extern void JobOffersMenu(void);
extern void JobOffersMenuRetired(void);
extern void JobOffers_DrawOldOfferLine(void);
extern void LoadHighlightsMenu(void);
extern void LoadLiverpool(void);
extern void LoadOldCompetitionMenu(void);
extern void LoadTactics(void);
extern void ManagementArrowDownSelect(void);
extern void ManagementArrowUpSelect(void);
extern void ManagementRecordInit(void);
extern void ManagementRecordMenu(void);
extern void MaxSubstitutes(void);
extern void MinSubstitutes(void);
extern void NationalTeamAddPlayer(void);
extern void NationalTeamAddPlayerAbroadOnSelect(void);
extern void NationalTeamAddPlayerExit(void);
extern void NationalTeamAddPlayerHomeOnSelect(void);
extern void NationalTeamRemovePlayers(void);
extern void NationalTeams(void);
extern void NextMatch(void);
extern void OffersAfterDrawCommon(void);
extern void OffersCommon(void);
extern void OffersInit(void);
extern void OldJobOffersCommon(void);
extern void OnSwapPlayer(void);
extern void OpenDiyFromFile(void);
extern void OpenDiyPreset1(void);
extern void OpenDiyPreset2(void);
extern void OpenDiyPreset3(void);
extern void OptionsMenuBeforeDraw(void);
extern void OptionsMenuSelected(void);
extern void OriginalCustomTeams(void);
extern void PitchTypeOrSeasonProc(void);
extern void PlayMatch30AfterDraw(void);
extern void PlayMatchChangeTactics(void);
extern void PlayMatchMenu(void);
extern void PlayMatchMenuInit(void);
extern void PlayMatchMenuReinit(void);
extern void PlayMatchOnSelect30(void);
extern void PlayMatchSelected(void);
extern void PlayerMarketFilterAgeGroupOnSelect(void);
extern void PlayerMarketFilterPositionOnSelect(void);
extern void PlayerMarketItemBeforeDraw(void);
extern void PlayerMarketItemOnSelect(void);
extern void PlayerMarketScrollOnSelect(void);
extern void PlayerMarketViewSquadOnSelect(void);
extern void PlayerOffersAdjustOfferAmount(void);
extern void PlayerOffersAfterDrawCommon(void);
extern void PlayerOffersInit(void);
extern void PlayerOffersOnSelect10(void);
extern void PlayerOffersOnSelect11(void);
extern void PlayerOffersOnSelect12(void);
extern void PlayerOffersOnSelect13(void);
extern void PlayerOffersOnSelect8(void);
extern void PresetCompetitionMenu(void);
extern void PutPlayerOnMarketMenu(void);
extern void RejectJobOffer(void);
extern void RejectOffer(void);
extern void ReturnFromViewOpponent(void);
extern void SWOSMainMenuInit(void);
extern void SaveAndExitEditTactics(void);
extern void SaveCareer(void);
extern void SaveContest(void);
extern void SaveDiskFilingMenu(void);
extern void ChallengesMenu(void);
extern void SaveHighlightsMenu(void);
extern void SeasonMenu(void);
extern void SelFilesDrawItemDescription(void);
extern void SelFilesOnSelectCommon(void);
extern void SelTeamForTacticsCommon(void);
extern void SelTeamForTacticsExit(void);
extern void SelTeamForTacticsInit(void);
extern void SelectCopyTactics(void);
extern void SelectFilenameToSaveAndExit(void);
extern void SelectFilesInit(void);
extern void SelectFilesInputSaveFilenameOnSelect(void);
extern void SelectFilesOnReturn(void);
extern void SelectGoals(void);
extern void SelectLeaguesCommon(void);
extern void SelectNationalTeam25AfterDraw(void);
extern void SelectNationalTeamAbort(void);
extern void SelectNationalTeamAfterDrawCommon(void);
extern void SelectNationalTeamCommon(void);
extern void SelectNationalTeamExit(void);
extern void SelectNationalTeamFinish(void);
extern void SelectNationalTeamInit(void);
extern void SelectNationalTeamOnSelect25(void);
extern void SelectTeamsCommon(void);
extern void SelectTeamsInit(void);
extern void SelectTeamsReinit(void);
extern void SelectTraining(void);
extern void SetName(void);
extern void SetSurname(void);
extern void ShowPlayerMarket(void);
extern void ShowPlayerMarketInit(void);
extern void ShowStadiumInit(void);
extern void Squad45AfterDraw(void);
extern void SquadAfterDrawCommon(void);
extern void SquadCommon(void);
extern void SquadFinish(void);
extern void SquadInit(void);
extern void SquadMenu(void);
extern void SquadOnSelect11(void);
extern void SquadOnSelect45(void);
extern void StadiumBeforeDrawLeftTeam(void);
extern void StadiumBeforeDrawRightTeam(void);
extern void StartContest(void);
extern void StatsExit(void);
extern void StatsHighestScorerList(void);
extern void StatsViewSquads(void);
extern void TakeOffMarketMenu(void);
extern void _TeamTactics(void);
extern void ThirdPartyOfferMenuAfterDrawCommon(void);
extern void ThirdPartyOfferMenuExit(void);
extern void ThirdPartyOfferMenuInit(void);
extern void ThirdPartyOfferViewSquad(void);
extern void TogglePlayerCoach(void);
extern void TopGoalScorersBeforeDrawCommon(void);
extern void TopGoalScorersExit(void);
extern void TopGoalScorersInit(void);
extern void TransferMarketAfterDrawCommon(void);
extern void TransferMarketCommon(void);
extern void TransferMarketFinish(void);
extern void TransferMarketInit(void);
extern void TransferPlayerAfterDrawCommon(void);
extern void TransferPlayerInit(void);
extern void TransferPlayerOnSelect10(void);
extern void TransferPlayerOnSelect11(void);
extern void TransferPlayerOnSelect12(void);
extern void TransfersMenu(void);
extern void UndoTactics(void);
extern void UseMatchTeamSelect(void);
extern void UniformSelected(void);
extern void ShootingPracticeMenu(void);
extern void ShootingPracticeSetupInit(void);
extern void ChangePracticeDefenders(void);
extern void ChangePracticeMidfield(void);
extern void ShootingPracticeContinue(void);
extern void PenaltyShootoutMenu(void);
extern void PracticeMenuSelected(void);
extern void User_A_Tactics(void);
extern void User_B_Tactics(void);
extern void User_C_Tactics(void);
extern void User_D_Tactics(void);
extern void User_E_Tactics(void);
extern void User_F_Tactics(void);
extern void ViewCompetitionsInit(void);
extern void ViewCompetitionsMenu(void);
extern void ViewCompetitionsMenuRetired(void);
extern void ViewCompetitionsOnReturn(void);
extern void ViewDomesticPlayerMarket(void);
extern void ViewForeignPlayerMarket(void);
extern void ViewFutureTeam(void);
extern void ViewFutureTeamLeague(void);
extern void ViewOffersMenu(void);
extern void ViewOpponentsSelect(void);
extern void ViewSelected(void);
extern void ViewSelectedTeams(void);
extern void ViewSquad21AfterDraw(void);
extern void ViewSquadAfterDrawCommon(void);
extern void ViewSquadCommon(void);
extern void ViewSquadExitContinue(void);
extern void ViewSquadInit(void);
extern void ViewSquadOnSelect21(void);
extern void ViewWorldMenu(void);

/* ---- External string constants ---- */
extern char _a343[];
extern char _a352[];
extern char _a424[];
extern char _a433[];
extern char _a442[];
extern char _a451[];
extern char _a523[];
extern char _a532[];
extern char _a541[];
extern char _aA_0[];
extern char _aAbort[];
extern char _aAccept[];
extern char _aAcceptOffer[];
extern char _aAddInternation[];
extern char _aAddInternation_0[];
extern char _aAddPlayer[];
extern char _aAdditionalInve_0[];
extern char _aAllMatches[];
extern char _aAllPlayerTeams[];
extern char _aAskForMore[];
extern char _aAttack[];
extern char _aAutoReplays[];
extern char _aAutoSaveHighli[];
extern char _aAwayGoals[];
extern char _aBankStatement[];
extern char _aBestRegards[];
extern char _aBuyOtherDomest[];
extern char _aBuyOtherForeig[];
extern char _aBuyPlayer[];
extern char _aCareerTotal[];
extern char _aCareer_1[];
extern char _aChooseTeam[];
extern char _aChooseTeams[];
extern char _aClubBusiness[];
extern char _aClubTeams[];
extern char _aCoach[];
extern char _aCoach_3[];
extern char _aCoach_4[];
extern char _aCompetitionBon[];
extern char _aCompetitionInf[];
extern char _aComputer[];
extern char _aConfirmOffer[];
extern char _aConsider[];
extern char _aContinue[];
extern char _aContinueCareer[];
extern char _aCopy[];
extern char _aCriteria[];
extern char _aCup1[];
extern char _aCup2[];
extern char _aCup_3[];
extern char _aCups[];
extern char _aD[];
extern char _aDearSirMadam[];
extern char _aDefend[];
extern char _aDescription[];
extern char _aDesignDiyCup[];
extern char _aDesignDiyLeagu[];
extern char _aDesignDiyTourn[];
extern char _aDiyCompetition_0[];
extern char _aEdit[];
extern char _aEditCustomTeam[];
extern char _aEditTactics[];
extern char _aEditedCustomTe_0[];
extern char _aExPlayerGoals[];
extern char _aExit[];
extern char _aF[];
extern char _aFilename[];
extern char _aFirstKit[];
extern char _aFixPitchType[];
extern char _aForm[];
extern char _aFriendlyDiyGam[];
extern char _aFriendly_1[];
extern char _aFrom[];
extern char _aGateReceipts[];
extern char _aGeneralRunning[];
extern char _aGiveFreeTransf[];
extern char _aGoals[];
extern char _aHighestScorerL[];
extern char _aHighlights[];
extern char _aHighlights_0[];
extern char _aHyphen[];
extern char _aImport[];
extern char _aImportTactics[];
extern char _aJobOffer[];
extern char _aJobOffers[];
extern char _aJobOffersBeing[];
extern char _aL[];
extern char _aLeadingCompeti[];
extern char _aLge[];
extern char _aLge_0[];
extern char _aList[];
extern char _aLoad[];
extern char _aLoadHighlights[];
extern char _aLoadLiverpool[];
extern char _aLoadOldCompeti[];
extern char _aMakeImprovedOf[];
extern char _aMakeOffer[];
extern char _aManagementReco_0[];
extern char _aMatch[];
extern char _aMemoFromTheClu[];
extern char _aMenuMusic[];
extern char _aGameplay[];
extern char _aCpuCards[];
extern char _aControls[];
extern char _aRedefineKeys[];
extern char _aPitches[];
extern char _aMultiplayer[];
extern char _aCredits[];
extern char _aName[];
extern char _aNat[];
extern char _aNationalTeams[];
extern char _aNationality[];
extern char _aNewBalance[];
extern char _aNextMatch[];
extern char _aNumberOfTeams[];
extern char _aOffers[];
extern char _aOk[];
extern char _aOldBalance[];
extern char _aOptions[];
extern char _aOriginalCustom[];
extern char _aOwnGoals[];
extern char _aPl[];
extern char _aPlayEachTeam[];
extern char _aPlayMatch[];
extern char _aPlayerCoach[];
extern char _aPlayerPurchase[];
extern char _aPlayerSales[];
extern char _aPlayerWagesBil[];
extern char _aPlayersCurrent[];
extern char _aPlus[];
extern char _aPointsForAWin[];
extern char _aPresetCompetit_0[];
extern char _aPts[];
extern char _aPutPlayerOnTra_0[];
extern char _aReject[];
extern char _aRejectOffer[];
extern char _aRemovePlayers[];
extern char _aResponseToOffe[];
extern char _aResult[];
extern char _aRounds[];
extern char _aSacked[];
extern char _aSave[];
extern char _aSaveCareer[];
extern char _aSaveDiskFiling[];
extern char _aChallenges[];
extern char _aSaveExit[];
extern char _aSaveHighlights[];
extern char _aSeason_1[];
extern char _aSecondKit[];
extern char _aSeeded[];
extern char _aShootingPractice[];
extern char _aPenaltyShootout[];
extern char _aPractice[];
extern char _aSkills[];
extern char _aSquad[];
extern char _aStar[];
extern char _aStartCareerAs[];
extern char _aStatistics[];
extern char _aStats[];
extern char _aSubstitutes[];
extern char _aSweep[];
extern char _aSwosVersion152[];
extern char _aTakeOffMarket[];
extern char _aTeams[];
extern char _aThirdPartyOffe[];
extern char _aToReplace[];
extern char _aTopGoalScorers[];
extern char _aTot[];
extern char _aTot_0[];
extern char _aTotalProfit[];
extern char _aTraining[];
extern char _aTransferMarket[];
extern char _aTransfers[];
extern char _aType[];
extern char _aUndo[];
extern char _aUniform[];
extern char _aUseMatchTeam[];
extern char _aV[];
extern char _aViewCompetitio[];
extern char _aViewDomesticPl[];
extern char _aViewForeignPla[];
extern char _aViewHighlights[];
extern char _aViewHighlights_0[];
extern char _aViewLeague[];
extern char _aViewMatch[];
extern char _aViewOffers[];
extern char _aViewOppo[];
extern char _aViewResult[];
extern char _aViewSelectedTe[];
extern char _aViewSquad[];
extern char _aViewSquads[];
extern char _aViewTeam[];
extern char _aViewWorld[];
extern char _aW[];
extern char _aYourCareerIsOv[];
extern char _aYourChairman[];
extern char _asc_131FF2[];
extern char _asc_135D96[];

/* ---- External string tables ---- */
extern char* _allPlTeamsEqualStrTable[];
extern char* _practiceDefendersStrTable[];
extern char* _practiceMidfieldStrTable[];
extern char* _autoReplaysStrTable[];
extern char* _autoSaveHlStrTable[];
extern char* _diyAwayGoalsStrTab[];
extern char* _diyCupAwayGoalsStringTable[];
extern char* _diyCupEndMonthStringTable[];
extern char* _diyCupExtraTimeStringTable0[];
extern char* _diyCupExtraTimeStringTable1[];
extern char* _diyCupExtraTimeStringTable2[];
extern char* _diyCupExtraTimeStringTable3[];
extern char* _diyCupExtraTimeStringTable4[];
extern char* _diyCupExtraTimeStringTable5[];
extern char* _diyCupLegsStringTable0[];
extern char* _diyCupLegsStringTable1[];
extern char* _diyCupLegsStringTable2[];
extern char* _diyCupLegsStringTable3[];
extern char* _diyCupLegsStringTable4[];
extern char* _diyCupLegsStringTable5[];
extern char* _diyCupPenaltiesStringTable0[];
extern char* _diyCupPenaltiesStringTable1[];
extern char* _diyCupPenaltiesStringTable2[];
extern char* _diyCupPenaltiesStringTable3[];
extern char* _diyCupPenaltiesStringTable4[];
extern char* _diyCupPenaltiesStringTable5[];
extern char* _diyCupPitchTypeOrSeasonStringTable[];
extern char* _diyCupPitchTypeStringTable[];
extern char* _diyCupStartMonthStringTable[];
extern char* _diyEndMonthStrTab[];
extern char* _diyLeagueEndingMonthStrTable[];
extern char* _diyLeagueMonthsStrTable[];
extern char* _diyLeaguePitchTypeStrTable[];
extern char* _diyPitchSeasonStrTab[];
extern char* _diyPitchTypeOrSeasonStrTable[];
extern char* _diyPitchTypeStringTable[];
extern char* _diyStage1ExtraTimeStrTab[];
extern char* _diyStage1LegsStrTab[];
extern char* _diyStage1PenaltiesStrTab[];
extern char* _diyStage1PlayCountStrTab[];
extern char* _diyStage1PtsForWinStrTab[];
extern char* _diyStage2ExtraTimeStrTab[];
extern char* _diyStage2LegsStrTab[];
extern char* _diyStage2PenaltiesStrTab[];
extern char* _diyStage2PlayCountStrTab[];
extern char* _diyStage2PtsForWinStrTab[];
extern char* _diyStage3ExtraTimeStrTab[];
extern char* _diyStage3LegsStrTab[];
extern char* _diyStage3PenaltiesStrTab[];
extern char* _diyStage3PlayCountStrTab[];
extern char* _diyStage3PtsForWinStrTab[];
extern char* _diyStage4ExtraTimeStrTab[];
extern char* _diyStage4LegsStrTab[];
extern char* _diyStage4PenaltiesStrTab[];
extern char* _diyStage4PlayCountStrTab[];
extern char* _diyStage4PtsForWinStrTab[];
extern char* _diyStage5ExtraTimeStrTab[];
extern char* _diyStage5LegsStrTab[];
extern char* _diyStage5PenaltiesStrTab[];
extern char* _diyStage5PlayCountStrTab[];
extern char* _diyStage5PtsForWinStrTab[];
extern char* _diyStage6ExtraTimeStrTab[];
extern char* _diyStage6LegsStrTab[];
extern char* _diyStage6PenaltiesStrTab[];
extern char* _diyStage6PlayCountStrTab[];
extern char* _diyStage6PtsForWinStrTab[];
extern char* _diyStartMonthStrTab[];
extern char* _flipStrTable[];
extern char* _friendlyPitchTypeTable[];
extern char* _gameLengthStrTable[];
extern char* _menuMusicStrTable[];
extern char* _cpuCardsStrTable[];
extern char* _gameplayStyleStrTable[];
extern char* _pitchStyleStrTable[];
extern char* _pitchTypeOrSeasonTable[];
extern char* _pitchTypeStrTable[];
extern char* _positionFilterStringTable[];
extern char* _skillFilterStringTable[];

/* ---- External data (char type for pointer compatibility) ---- */
extern char _managerNameBuffer[];
extern char _managerSexBuffer[];
extern char _managerSurnameBuffer[];
extern char _offsetCoachOrPlayer[];
extern char _saveFileTitle[];
extern EditTeamsSpriteCopy _stadiumMenuLittlePlayerSpriteCopy;
extern char _teamTacticsUSERA[];
extern char _teamTacticsUSERB[];
extern char _teamTacticsUSERC[];
extern char _teamTacticsUSERD[];
extern char _teamTacticsUSERE[];
extern char _teamTacticsUSERF[];

/* ---- External data (uint8_t type) ---- */
extern EditTeamsSpriteCopy _editTeamsPrimaryShirts;
extern EditTeamsSpriteCopy _editTeamsSecondaryShirt;
extern EditTeamsSpriteCopy _editTeamsSecondaryShorts;
extern EditTeamsSpriteCopy _editTeamsSecondarySocks;
extern EditTeamsSpriteCopy _editTeamsShorts;
extern EditTeamsSpriteCopy _editTeamsSocks;

Menu_ChooseTeamsFriendly _chooseTeamsFriendlyMenu = {
    { ChooseTeamsFriendlyInitFinish, ChooseTeamsFriendlyInitFinish, 0, 23 },
    { 2, 0, 300, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        {
            { 20, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xA },
            MENU_CMD_END
        },
        {
            { 120, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xB },
            MENU_CMD_END
        },
        {
            { -36, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xD },
            MENU_CMD_END
        }
    },
    {
        {
            { 45, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aComputer },
            MENU_CMD_END
        },
        {
            { -111, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aPlayerCoach },
            MENU_CMD_END
        },
        {
            { -11, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aCoach },
            MENU_CMD_END
        }
    },
    { MENU_MARK_START_MENU, 0, 16 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x6 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, ChooseTeamsFriendlyCommon2 },
    MENU_CMD_END,
    {
        { 1, 21, 100, 9 },
        { MENU_CMD_NAV, -1, 23, -1, 8 },
        { MENU_CMD_LEFT_SKIP, 38, 2 },
        MENU_CMD_END
    },
    {
        {
            { 1, 31, 100, 9 },
            { MENU_CMD_NAV, -1, 24, 7, 9 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 41, 100, 9 },
            { MENU_CMD_NAV, -1, 25, 8, 10 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 51, 100, 9 },
            { MENU_CMD_NAV, -1, 26, 9, 11 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 61, 100, 9 },
            { MENU_CMD_NAV, -1, 27, 10, 12 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 71, 100, 9 },
            { MENU_CMD_NAV, -1, 28, 11, 13 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 81, 100, 9 },
            { MENU_CMD_NAV, -1, 29, 12, 14 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 91, 100, 9 },
            { MENU_CMD_NAV, -1, 30, 13, 15 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 101, 100, 9 },
            { MENU_CMD_NAV, -1, 31, 14, 16 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 111, 100, 9 },
            { MENU_CMD_NAV, -1, 32, 15, 17 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 121, 100, 9 },
            { MENU_CMD_NAV, -1, 33, 16, 18 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -125, 100, 9 },
            { MENU_CMD_NAV, -1, 34, 17, 19 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -115, 100, 9 },
            { MENU_CMD_NAV, -1, 35, 18, 20 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -105, 100, 9 },
            { MENU_CMD_NAV, -1, 36, 19, 21 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -95, 100, 9 },
            { MENU_CMD_NAV, -1, 37, 20, 22 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -85, 100, 9 },
            { MENU_CMD_NAV, -1, 38, 21, 55 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        }
    },
    {
        {
            { 102, 21, 100, 9 },
            { MENU_CMD_NAV, 7, 39, -1, 24 },
            MENU_CMD_END
        },
        {
            { 102, 31, 100, 9 },
            { MENU_CMD_NAV, 8, 40, 23, 25 },
            MENU_CMD_END
        },
        {
            { 102, 41, 100, 9 },
            { MENU_CMD_NAV, 9, 41, 24, 26 },
            MENU_CMD_END
        },
        {
            { 102, 51, 100, 9 },
            { MENU_CMD_NAV, 10, 42, 25, 27 },
            MENU_CMD_END
        },
        {
            { 102, 61, 100, 9 },
            { MENU_CMD_NAV, 11, 43, 26, 28 },
            MENU_CMD_END
        },
        {
            { 102, 71, 100, 9 },
            { MENU_CMD_NAV, 12, 44, 27, 29 },
            MENU_CMD_END
        },
        {
            { 102, 81, 100, 9 },
            { MENU_CMD_NAV, 13, 45, 28, 30 },
            MENU_CMD_END
        },
        {
            { 102, 91, 100, 9 },
            { MENU_CMD_NAV, 14, 46, 29, 31 },
            MENU_CMD_END
        },
        {
            { 102, 101, 100, 9 },
            { MENU_CMD_NAV, 15, 47, 30, 32 },
            MENU_CMD_END
        },
        {
            { 102, 111, 100, 9 },
            { MENU_CMD_NAV, 16, 48, 31, 33 },
            MENU_CMD_END
        },
        {
            { 102, 121, 100, 9 },
            { MENU_CMD_NAV, 17, 49, 32, 34 },
            MENU_CMD_END
        },
        {
            { 102, -125, 100, 9 },
            { MENU_CMD_NAV, 18, 50, 33, 35 },
            MENU_CMD_END
        },
        {
            { 102, -115, 100, 9 },
            { MENU_CMD_NAV, 19, 51, 34, 36 },
            MENU_CMD_END
        },
        {
            { 102, -105, 100, 9 },
            { MENU_CMD_NAV, 20, 52, 35, 37 },
            MENU_CMD_END
        },
        {
            { 102, -95, 100, 9 },
            { MENU_CMD_NAV, 21, 53, 36, 38 },
            MENU_CMD_END
        },
        {
            { 102, -85, 100, 9 },
            { MENU_CMD_NAV, 22, 54, 37, 56 },
            MENU_CMD_END
        }
    },
    {
        { -53, 21, 100, 9 },
        { MENU_CMD_NAV, 23, -1, -1, 40 },
        { MENU_CMD_LEFT_SKIP, 38, 2 },
        MENU_CMD_END
    },
    {
        {
            { -53, 31, 100, 9 },
            { MENU_CMD_NAV, 24, -1, 39, 41 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 41, 100, 9 },
            { MENU_CMD_NAV, 25, -1, 40, 42 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 51, 100, 9 },
            { MENU_CMD_NAV, 26, -1, 41, 43 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 61, 100, 9 },
            { MENU_CMD_NAV, 27, -1, 42, 44 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 71, 100, 9 },
            { MENU_CMD_NAV, 28, -1, 43, 45 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 81, 100, 9 },
            { MENU_CMD_NAV, 29, -1, 44, 46 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 91, 100, 9 },
            { MENU_CMD_NAV, 30, -1, 45, 47 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 101, 100, 9 },
            { MENU_CMD_NAV, 31, -1, 46, 48 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 111, 100, 9 },
            { MENU_CMD_NAV, 32, -1, 47, 49 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, 121, 100, 9 },
            { MENU_CMD_NAV, 33, -1, 48, 50 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, -125, 100, 9 },
            { MENU_CMD_NAV, 34, -1, 49, 51 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, -115, 100, 9 },
            { MENU_CMD_NAV, 35, -1, 50, 52 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, -105, 100, 9 },
            { MENU_CMD_NAV, 36, -1, 51, 53 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, -95, 100, 9 },
            { MENU_CMD_NAV, 37, -1, 52, 54 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        },
        {
            { -53, -85, 100, 9 },
            { MENU_CMD_NAV, 38, -1, 53, 57 },
            { MENU_CMD_DOWN_SKIP, 54, 2 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, -71, 120, 15 },
    { MENU_CMD_NAV, -1, 56, 22, -1 },
    {
        { MENU_CMD_UP_SKIP, 55, 2 },
        { MENU_CMD_RIGHT_SKIP, 55, 1 }
    },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewSelectedTe },
    { MENU_CMD_ONSELECT_FIRE, ViewSelected },
    MENU_CMD_END,
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 55, 57, 38, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitChooseTeams2 },
        MENU_CMD_END
    },
    { -72, -71, 120, 15 },
    { MENU_CMD_NAV, 56, -1, 54, -1 },
    {
        { MENU_CMD_DOWN_SKIP, 57, 2 },
        { MENU_CMD_RIGHT_SKIP, 57, 0 }
    },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, ChooseTeamsPlayMatchSelected },
    MENU_CMD_END,
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ChooseTeamsFriendly2 _chooseTeamsFriendlyMenu2 = {
    { ChooseTeamsFriendlyInit, 0, 0, 30 },
    { 2, 0, 300, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 20, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_NUMBER, 0x0, 0 },
    MENU_CMD_END,
    {
        {
            { 20, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xA },
            MENU_CMD_END
        },
        {
            { 120, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xB },
            MENU_CMD_END
        },
        {
            { -36, 35, 19, 9 },
            { MENU_CMD_COLORS, 0xD },
            MENU_CMD_END
        }
    },
    {
        {
            { 45, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aComputer },
            MENU_CMD_END
        },
        {
            { -111, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aPlayerCoach },
            MENU_CMD_END
        },
        {
            { -11, 35, 0, 11 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aCoach },
            MENU_CMD_END
        }
    },
    { MENU_MARK_START_MENU, 0, -1 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x6 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, ChooseTeamsFriendlyCommon },
    MENU_CMD_END,
    {
        { 1, 21, 100, 8 },
        { MENU_CMD_NAV, -1, 30, -1, 9 },
        { MENU_CMD_LEFT_SKIP, 51, 2 },
        MENU_CMD_END
    },
    {
        {
            { 1, 30, 100, 8 },
            { MENU_CMD_NAV, -1, 31, 8, 10 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 39, 100, 8 },
            { MENU_CMD_NAV, -1, 32, 9, 11 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 48, 100, 8 },
            { MENU_CMD_NAV, -1, 33, 10, 12 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 57, 100, 8 },
            { MENU_CMD_NAV, -1, 34, 11, 13 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 66, 100, 8 },
            { MENU_CMD_NAV, -1, 35, 12, 14 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 75, 100, 8 },
            { MENU_CMD_NAV, -1, 36, 13, 15 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 84, 100, 8 },
            { MENU_CMD_NAV, -1, 37, 14, 16 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 93, 100, 8 },
            { MENU_CMD_NAV, -1, 38, 15, 17 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 102, 100, 8 },
            { MENU_CMD_NAV, -1, 39, 16, 18 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 111, 100, 8 },
            { MENU_CMD_NAV, -1, 40, 17, 19 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, 120, 100, 8 },
            { MENU_CMD_NAV, -1, 41, 18, 20 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -127, 100, 8 },
            { MENU_CMD_NAV, -1, 42, 19, 21 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -118, 100, 8 },
            { MENU_CMD_NAV, -1, 43, 20, 22 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -109, 100, 8 },
            { MENU_CMD_NAV, -1, 44, 21, 23 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -100, 100, 8 },
            { MENU_CMD_NAV, -1, 45, 22, 24 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -91, 100, 8 },
            { MENU_CMD_NAV, -1, 46, 23, 25 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -82, 100, 8 },
            { MENU_CMD_NAV, -1, 47, 24, 26 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -73, 100, 8 },
            { MENU_CMD_NAV, -1, 48, 25, 27 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -64, 100, 8 },
            { MENU_CMD_NAV, -1, 49, 26, 28 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -55, 100, 8 },
            { MENU_CMD_NAV, -1, 50, 27, 29 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        },
        {
            { 1, -46, 100, 8 },
            { MENU_CMD_NAV, -1, 51, 28, 74 },
            { MENU_CMD_UP_SKIP, 29, 2 },
            MENU_CMD_END
        }
    },
    {
        {
            { 102, 21, 100, 8 },
            { MENU_CMD_NAV, 8, 52, -1, 31 },
            MENU_CMD_END
        },
        {
            { 102, 30, 100, 8 },
            { MENU_CMD_NAV, 9, 53, 30, 32 },
            MENU_CMD_END
        },
        {
            { 102, 39, 100, 8 },
            { MENU_CMD_NAV, 10, 54, 31, 33 },
            MENU_CMD_END
        },
        {
            { 102, 48, 100, 8 },
            { MENU_CMD_NAV, 11, 55, 32, 34 },
            MENU_CMD_END
        },
        {
            { 102, 57, 100, 8 },
            { MENU_CMD_NAV, 12, 56, 33, 35 },
            MENU_CMD_END
        },
        {
            { 102, 66, 100, 8 },
            { MENU_CMD_NAV, 13, 57, 34, 36 },
            MENU_CMD_END
        },
        {
            { 102, 75, 100, 8 },
            { MENU_CMD_NAV, 14, 58, 35, 37 },
            MENU_CMD_END
        },
        {
            { 102, 84, 100, 8 },
            { MENU_CMD_NAV, 15, 59, 36, 38 },
            MENU_CMD_END
        },
        {
            { 102, 93, 100, 8 },
            { MENU_CMD_NAV, 16, 60, 37, 39 },
            MENU_CMD_END
        },
        {
            { 102, 102, 100, 8 },
            { MENU_CMD_NAV, 17, 61, 38, 40 },
            MENU_CMD_END
        },
        {
            { 102, 111, 100, 8 },
            { MENU_CMD_NAV, 18, 62, 39, 41 },
            MENU_CMD_END
        },
        {
            { 102, 120, 100, 8 },
            { MENU_CMD_NAV, 19, 63, 40, 42 },
            MENU_CMD_END
        },
        {
            { 102, -127, 100, 8 },
            { MENU_CMD_NAV, 20, 64, 41, 43 },
            MENU_CMD_END
        },
        {
            { 102, -118, 100, 8 },
            { MENU_CMD_NAV, 21, 65, 42, 44 },
            MENU_CMD_END
        },
        {
            { 102, -109, 100, 8 },
            { MENU_CMD_NAV, 22, 66, 43, 45 },
            MENU_CMD_END
        },
        {
            { 102, -100, 100, 8 },
            { MENU_CMD_NAV, 23, 67, 44, 46 },
            MENU_CMD_END
        },
        {
            { 102, -91, 100, 8 },
            { MENU_CMD_NAV, 24, 68, 45, 47 },
            MENU_CMD_END
        },
        {
            { 102, -82, 100, 8 },
            { MENU_CMD_NAV, 25, 69, 46, 48 },
            MENU_CMD_END
        },
        {
            { 102, -73, 100, 8 },
            { MENU_CMD_NAV, 26, 70, 47, 49 },
            MENU_CMD_END
        },
        {
            { 102, -64, 100, 8 },
            { MENU_CMD_NAV, 27, 71, 48, 50 },
            MENU_CMD_END
        },
        {
            { 102, -55, 100, 8 },
            { MENU_CMD_NAV, 28, 72, 49, 51 },
            MENU_CMD_END
        },
        {
            { 102, -46, 100, 8 },
            { MENU_CMD_NAV, 29, 73, 50, 75 },
            MENU_CMD_END
        }
    },
    {
        { -53, 21, 100, 8 },
        { MENU_CMD_NAV, 30, -1, -1, 53 },
        { MENU_CMD_LEFT_SKIP, 51, 2 },
        MENU_CMD_END
    },
    {
        {
            { -53, 30, 100, 8 },
            { MENU_CMD_NAV, 31, -1, 52, 54 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 39, 100, 8 },
            { MENU_CMD_NAV, 32, -1, 53, 55 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 48, 100, 8 },
            { MENU_CMD_NAV, 33, -1, 54, 56 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 57, 100, 8 },
            { MENU_CMD_NAV, 34, -1, 55, 57 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 66, 100, 8 },
            { MENU_CMD_NAV, 35, -1, 56, 58 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 75, 100, 8 },
            { MENU_CMD_NAV, 36, -1, 57, 59 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 84, 100, 8 },
            { MENU_CMD_NAV, 37, -1, 58, 60 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 93, 100, 8 },
            { MENU_CMD_NAV, 38, -1, 59, 61 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 102, 100, 8 },
            { MENU_CMD_NAV, 39, -1, 60, 62 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 111, 100, 8 },
            { MENU_CMD_NAV, 40, -1, 61, 63 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, 120, 100, 8 },
            { MENU_CMD_NAV, 41, -1, 62, 64 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -127, 100, 8 },
            { MENU_CMD_NAV, 42, -1, 63, 65 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -118, 100, 8 },
            { MENU_CMD_NAV, 43, -1, 64, 66 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -109, 100, 8 },
            { MENU_CMD_NAV, 44, -1, 65, 67 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -100, 100, 8 },
            { MENU_CMD_NAV, 45, -1, 66, 68 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -91, 100, 8 },
            { MENU_CMD_NAV, 46, -1, 67, 69 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -82, 100, 8 },
            { MENU_CMD_NAV, 47, -1, 68, 70 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -73, 100, 8 },
            { MENU_CMD_NAV, 48, -1, 69, 71 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -64, 100, 8 },
            { MENU_CMD_NAV, 49, -1, 70, 72 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -55, 100, 8 },
            { MENU_CMD_NAV, 50, -1, 71, 73 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        },
        {
            { -53, -46, 100, 8 },
            { MENU_CMD_NAV, 51, -1, 72, 76 },
            { MENU_CMD_DOWN_SKIP, 73, 2 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, -71, 120, 15 },
    { MENU_CMD_NAV, -1, 75, 29, -1 },
    {
        { MENU_CMD_UP_SKIP, 74, 2 },
        { MENU_CMD_RIGHT_SKIP, 74, 1 }
    },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, ChangeTeams },
    MENU_CMD_END,
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 74, 76, 51, -1 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x0, _aAbort },
        { MENU_CMD_ONSELECT_FIRE, AbortChooseTeamsFriendly },
        MENU_CMD_END
    },
    { -72, -71, 120, 15 },
    { MENU_CMD_NAV, 75, -1, 73, -1 },
    {
        { MENU_CMD_DOWN_SKIP, 76, 2 },
        { MENU_CMD_RIGHT_SKIP, 76, 0 }
    },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, ChooseTeamsFriendlyOnSelect76 },
    MENU_CMD_END,
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_DesignDIYLeague _designDiyLeagueMenu = {
    { DesignDIYLeagueInit, DesignDIYLeagueFinish, 0, 13 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aDesignDiyLeagu },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyCompetitionStart.base.competitionName },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 8 },
    MENU_MARK_BYTE_COORDS,
    {
        { 31, 20, -15, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 9 },
        { MENU_CMD_COLORS, 0x1D },
        { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyCompetitionStart.base.competitionName },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueSetName },
        MENU_CMD_END
    },
    {
        {
            { 31, 60, 120, 15 },
            { MENU_CMD_COLORS, 0x9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aNumberOfTeams },
            MENU_CMD_END
        },
        {
            { 31, 80, 120, 15 },
            { MENU_CMD_COLORS, 0x9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlayEachTeam },
            MENU_CMD_END
        },
        {
            { 31, 100, 120, 15 },
            { MENU_CMD_COLORS, 0x9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPointsForAWin },
            MENU_CMD_END
        },
        {
            { 31, 120, 120, 15 },
            { MENU_CMD_COLORS, 0x9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aSubstitutes },
            MENU_CMD_END
        },
        {
            { -63, 120, 38, 15 },
            { MENU_CMD_COLORS, 0x9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aFrom },
            MENU_CMD_END
        }
    },
    {
        {
            { 31, 40, 120, 15 },
            { MENU_CMD_NAV, -1, 9, 2, 13 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyPitchTypeOrSeasonStrTable },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueChangePitchTypeOrSeason },
            MENU_CMD_END
        },
        {
            { -104, 40, 120, 15 },
            { MENU_CMD_NAV, 8, 10, 2, 10 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyLeaguePitchTypeStrTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangePitchType },
            MENU_CMD_END
        },
        {
            { -104, 40, 50, 15 },
            { MENU_CMD_NAV, 9, 12, 9, 13 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyLeagueMonthsStrTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeStartMonth },
            MENU_CMD_END
        }
    },
    {
        { -53, 40, 18, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _asc_131FF2 },
        MENU_CMD_END
    },
    {
        { -34, 40, 50, 15 },
        { MENU_CMD_NAV, 10, -1, 9, 13 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _diyLeagueEndingMonthStrTable },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeEndMonth },
        MENU_CMD_END
    },
    { -104, 60, 120, 15 },
    { MENU_CMD_NAV, -1, -1, 10, 14 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_NUMBER, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeNumberOfTeams },
    MENU_CMD_END,
    { -104, 80, 120, 15 },
    { MENU_CMD_NAV, -1, -1, 13, 15 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangePlayEachTeam },
    MENU_CMD_END,
    { -104, 100, 120, 15 },
    { MENU_CMD_NAV, -1, -1, 14, 16 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_NUMBER, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeNumPointsForWin },
    MENU_CMD_END,
    { -104, 120, 40, 15 },
    { MENU_CMD_NAV, -1, 17, 15, 21 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeMinSubstitutes },
    MENU_CMD_END,
    { -24, 120, 40, 15 },
    { MENU_CMD_NAV, 16, -1, 15, 21 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChangeMaxSubstitutes },
    MENU_CMD_END,
    { 31, -116, -15, 12 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { 31, -101, -15, 12 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 0 },
    {
        {
            { 0, -71, 120, 15 },
            { MENU_CMD_NAV, -1, 22, 16, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aChooseTeams },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueChooseTeams },
            MENU_CMD_END
        },
        {
            { 121, -91, 62, 15 },
            { MENU_CMD_NAV, -1, -1, 16, 22 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aOk },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYLeagueOK },
            MENU_CMD_END
        },
        {
            { 121, -71, 62, 15 },
            { MENU_CMD_NAV, 20, -1, 21, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
            MENU_CMD_END
        }
    },
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_DIYLeagueTournament _diyLeagueTournamentMenu = {
    { DIYLeagueTournamentInit, DIYLeagueTournamentOnRestore, 0, 1 },
    { 0, 0, 304, 14 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, -68, -54, 12 },
    { MENU_CMD_NAV, -1, 2, 41, -1 },
    { MENU_CMD_RIGHT_SKIP, 1, 1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentNextMatch },
    MENU_CMD_END,
    { 0, -68, -54, 12 },
    { MENU_CMD_NAV, 1, 3, 41, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aPlayMatch },
    MENU_CMD_END,
    {
        { 0, -68, 100, 12 },
        { MENU_CMD_NAV, 2, 4, 41, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aMatch },
        { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentMatch },
        MENU_CMD_END
    },
    { 77, -68, 80, 12 },
    { MENU_CMD_NAV, 3, 5, 41, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewHighlights_0 },
    { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentOnSelect4 },
    MENU_CMD_END,
    {
        {
            { 101, -68, 101, 12 },
            { MENU_CMD_NAV, 4, 6, 41, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aResult },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentResult },
            MENU_CMD_END
        },
        {
            { 0, -68, 100, 12 },
            { MENU_CMD_NAV, 5, 7, 41, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aViewMatch },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentViewMatch },
            MENU_CMD_END
        }
    },
    { 61, -68, 90, 12 },
    { MENU_CMD_NAV, 6, 8, 41, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewHighlights_0 },
    { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentViewHighlights },
    MENU_CMD_END,
    {
        {
            { 101, -68, 101, 12 },
            { MENU_CMD_NAV, 7, 9, 41, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aViewResult },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentViewResult },
            MENU_CMD_END
        },
        {
            { -53, -68, 50, 12 },
            { MENU_CMD_NAV, 8, 10, 41, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aStats },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentStats },
            MENU_CMD_END
        },
        {
            { -2, -68, 50, 12 },
            { MENU_CMD_NAV, 9, -1, 41, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentExit },
            MENU_CMD_END
        }
    },
    {
        {
            { -107, -77, 6, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x10, 0 },
            MENU_CMD_END
        },
        {
            { 0, -77, -127, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x4010, 0 },
            MENU_CMD_END
        },
        {
            { -83, -77, -127, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x8010, 0 },
            MENU_CMD_END
        }
    },
    {
        {
            { -127, -77, 18, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_NUMBER, 0x4010, 0 },
            MENU_CMD_END
        },
        {
            { -101, -77, 18, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_NUMBER, 0x8010, 0 },
            MENU_CMD_END
        }
    },
    {
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        },
        {
            { 25, 0, 100, 8 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            { MENU_CMD_BEFORE_DRAW, DIYLeagueTournament16BeforeDraw },
            MENU_CMD_END
        }
    },
    {
        {
            { 126, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPl },
            MENU_CMD_END
        },
        {
            { -105, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aW },
            MENU_CMD_END
        },
        {
            { -80, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aD },
            MENU_CMD_END
        },
        {
            { -55, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aL },
            MENU_CMD_END
        },
        {
            { -30, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aF },
            MENU_CMD_END
        },
        {
            { -5, 10, 24, 8 },
            { MENU_CMD_INVISIBLE },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aA_0 },
            MENU_CMD_END
        }
    },
    MENU_MARK_WIDE_COORDS,
    { 276, 10, 24, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_PTR, 0x0, _aPts },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 6, 0, 22, 6 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 25, 0, 100, 6 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        {
            { 126, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { -105, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { -80, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { -55, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { -30, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { -5, 0, 24, 6 },
            { MENU_CMD_COLORS, 0x7 },
            { MENU_CMD_NUMBER, 0x0, 0 },
            MENU_CMD_END
        }
    },
    MENU_MARK_WIDE_COORDS,
    { 276, 0, 24, 6 },
    { MENU_CMD_COLORS, 0x7 },
    { MENU_CMD_NUMBER, 0x0, 0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, 30, 8, 14 },
    { MENU_CMD_NAV, -1, 1, -1, 41 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 183 },
    { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentScrollUp },
    MENU_CMD_END,
    { 0, -98, 8, 14 },
    { MENU_CMD_NAV, -1, 1, 40, 1 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 184 },
    { MENU_CMD_ONSELECT_FIRE, DIYLeagueTournamentScrollDown },
    MENU_CMD_END,
    { 0, 0, 0, 0 },
    { MENU_CMD_INPUT_FUNC, 0xFFFF, SetExitMenuFlag },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_DesignDIYCup _designDiyCupMenu = {
    { DesignDIYCupInit, DesignDIYCupReinit, DesignDIYCupOnDraw, 11 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aDesignDiyCup },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyCompetitionStart.base.competitionName },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 15 },
    { 0, 5, 304, 15 },
    { MENU_CMD_NAV, -1, -1, -1, 3 },
    { MENU_CMD_COLORS, 0x1D },
    { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyCompetitionStart.base.competitionName },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect2 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        {
            { 0, 25, 75, 15 },
            { MENU_CMD_NAV, -1, 4, 2, 11 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyCupPitchTypeOrSeasonStringTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect3 },
            MENU_CMD_END
        },
        {
            { 76, 25, 56, 15 },
            { MENU_CMD_NAV, 3, 5, 2, 11 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyCupPitchTypeStringTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect4 },
            MENU_CMD_END
        },
        {
            { 76, 25, 22, 15 },
            { MENU_CMD_NAV, 4, 7, 4, 11 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyCupStartMonthStringTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect5 },
            MENU_CMD_END
        }
    },
    {
        { 99, 25, 10, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aHyphen },
        MENU_CMD_END
    },
    {
        { 110, 25, 22, 15 },
        { MENU_CMD_NAV, 5, 9, 4, 11 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _diyCupEndMonthStringTable },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect7 },
        MENU_CMD_END
    },
    {
        { -123, 25, 70, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAwayGoals },
        MENU_CMD_END
    },
    {
        { -52, 25, 100, 15 },
        { MENU_CMD_NAV, 7, -1, 2, 13 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _diyCupAwayGoalsStringTable },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect9 },
        MENU_CMD_END
    },
    {
        { 0, 45, 64, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aRounds },
        MENU_CMD_END
    },
    { 65, 45, 44, 15 },
    { MENU_CMD_NAV, -1, 13, 5, 20 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_NUMBER, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect11 },
    MENU_CMD_END,
    {
        { 113, 45, 70, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aSubstitutes },
        MENU_CMD_END
    },
    { -72, 45, 40, 15 },
    { MENU_CMD_NAV, 11, 15, 9, 21 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect13 },
    MENU_CMD_END,
    {
        { -31, 45, 38, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFrom },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 264, 45, 40, 15 },
    { MENU_CMD_NAV, 13, -1, 9, 22 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect15 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        {
            { 58, 66, 80, 9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aTeams },
            MENU_CMD_END
        },
        {
            { -127, 66, 120, 9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aDescription },
            MENU_CMD_END
        }
    },
    {
        {
            {
                { 0, 78, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, 78, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, 78, 38, 9 },
                { MENU_CMD_NAV, -1, 21, 11, 25 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable0 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, 78, 72, 9 },
                { MENU_CMD_NAV, 20, 22, 13, 26 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable0 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, 78, 79, 9 },
                { MENU_CMD_NAV, 21, -1, 15, 27 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable0 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        },
        {
            {
                { 0, 88, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, 88, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, 88, 38, 9 },
                { MENU_CMD_NAV, -1, 26, 20, 30 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable1 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, 88, 72, 9 },
                { MENU_CMD_NAV, 25, 27, 21, 31 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable1 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, 88, 79, 9 },
                { MENU_CMD_NAV, 26, -1, 22, 32 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable1 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        },
        {
            {
                { 0, 98, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, 98, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, 98, 38, 9 },
                { MENU_CMD_NAV, -1, 31, 25, 35 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable2 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, 98, 72, 9 },
                { MENU_CMD_NAV, 30, 32, 26, 36 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable2 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, 98, 79, 9 },
                { MENU_CMD_NAV, 31, -1, 27, 37 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable2 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        },
        {
            {
                { 0, 108, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, 108, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, 108, 38, 9 },
                { MENU_CMD_NAV, -1, 36, 30, 40 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable3 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, 108, 72, 9 },
                { MENU_CMD_NAV, 35, 37, 31, 41 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable3 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, 108, 79, 9 },
                { MENU_CMD_NAV, 36, -1, 32, 42 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable3 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        },
        {
            {
                { 0, 118, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, 118, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, 118, 38, 9 },
                { MENU_CMD_NAV, -1, 41, 35, 45 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable4 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, 118, 72, 9 },
                { MENU_CMD_NAV, 40, 42, 36, 46 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable4 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, 118, 79, 9 },
                { MENU_CMD_NAV, 41, -1, 37, 47 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable4 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        },
        {
            {
                { 0, -128, 80, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_TEXT_PTR, 0x8000, 0 },
                MENU_CMD_END
            },
            {
                { 88, -128, 18, 9 },
                { MENU_CMD_COLORS, 0x9 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { 113, -128, 38, 9 },
                { MENU_CMD_NAV, -1, 46, 40, 49 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupLegsStringTable5 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect20 },
                MENU_CMD_END
            },
            {
                { -104, -128, 72, 9 },
                { MENU_CMD_NAV, 45, 47, 41, 49 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupExtraTimeStringTable5 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect21 },
                MENU_CMD_END
            },
            {
                { -31, -128, 79, 9 },
                { MENU_CMD_NAV, 46, -1, 42, 49 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyCupPenaltiesStringTable5 },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect22 },
                MENU_CMD_END
            }
        }
    },
    { MENU_MARK_START_MENU, 0, 0 },
    {
        {
            { 0, -71, 120, 15 },
            { MENU_CMD_NAV, -1, 50, 45, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aChooseTeams },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOnSelect48 },
            MENU_CMD_END
        },
        {
            { 121, -91, 62, 15 },
            { MENU_CMD_NAV, -1, -1, 45, 50 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aOk },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYCupOK },
            MENU_CMD_END
        },
        {
            { 121, -71, 62, 15 },
            { MENU_CMD_NAV, 48, -1, 49, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
            MENU_CMD_END
        }
    },
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_DIYCup _diyCupMenu = {
    { DIYCupInit, DIYCupFinish, 0, 1 },
    { 0, 0, 304, 14 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, -68, -54, 12 },
    { MENU_CMD_NAV, -1, 2, 17, -1 },
    { MENU_CMD_RIGHT_SKIP, 1, 1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect1 },
    MENU_CMD_END,
    { 0, -68, -54, 12 },
    { MENU_CMD_NAV, 1, 3, 17, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aPlayMatch },
    MENU_CMD_END,
    {
        { 0, -68, 100, 12 },
        { MENU_CMD_NAV, 2, 4, 17, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aMatch },
        { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect3 },
        MENU_CMD_END
    },
    { 77, -68, 80, 12 },
    { MENU_CMD_NAV, 3, 5, 17, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewHighlights_0 },
    { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect4 },
    MENU_CMD_END,
    {
        {
            { 101, -68, 101, 12 },
            { MENU_CMD_NAV, 4, 6, 17, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aResult },
            { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect5 },
            MENU_CMD_END
        },
        {
            { 0, -68, 100, 12 },
            { MENU_CMD_NAV, 5, 7, 17, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aViewMatch },
            { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect6 },
            MENU_CMD_END
        }
    },
    { 61, -68, 90, 12 },
    { MENU_CMD_NAV, 6, 8, 17, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewHighlights_0 },
    { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect7 },
    MENU_CMD_END,
    {
        {
            { 101, -68, 101, 12 },
            { MENU_CMD_NAV, 7, 9, 17, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aViewResult },
            { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect8 },
            MENU_CMD_END
        },
        {
            { -53, -68, 50, 12 },
            { MENU_CMD_NAV, 8, 10, 17, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aStats },
            { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect9 },
            MENU_CMD_END
        },
        {
            { -2, -68, 50, 12 },
            { MENU_CMD_NAV, 9, -1, 17, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect10 },
            MENU_CMD_END
        }
    },
    {
        {
            { -107, -76, 6, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x10, 0 },
            MENU_CMD_END
        },
        {
            { 0, -76, -127, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x4010, 0 },
            MENU_CMD_END
        },
        {
            { -83, -76, -127, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_PTR, 0x8010, 0 },
            MENU_CMD_END
        }
    },
    {
        {
            { -127, -76, 18, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_NUMBER, 0x4010, 0 },
            MENU_CMD_END
        },
        {
            { -101, -76, 18, 8 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_NUMBER, 0x8010, 0 },
            MENU_CMD_END
        }
    },
    { 0, 30, 8, 14 },
    { MENU_CMD_NAV, -1, 1, -1, 17 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 183 },
    { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect16 },
    MENU_CMD_END,
    { 0, -91, 8, 14 },
    { MENU_CMD_NAV, -1, 1, 16, 1 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 184 },
    { MENU_CMD_ONSELECT_FIRE, DIYCupOnSelect17 },
    MENU_CMD_END,
    {
        {
            { MENU_MARK_START_MENU, 0, 25 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 52 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 79 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 106 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 133 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 160 },
            {
                { -104, 0, 0, 0 },
                { MENU_CMD_TEXT_PTR, 0x0, 0 },
                MENU_CMD_END
            },
            {
                {
                    { -121, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -121, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
                    MENU_CMD_END
                },
                {
                    { -87, 0, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                },
                {
                    { -87, 10, 0, 0 },
                    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
                    MENU_CMD_END
                }
            },
            {
                { -113, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -95, 0, 0, 0 },
                { MENU_CMD_NUMBER, 0x0, 0 },
                MENU_CMD_END
            },
            {
                { -104, 10, 0, 0 },
                { MENU_CMD_TEXT_NO_PTR, 0xE },
                MENU_CMD_END
            }
        }
    },
    { 0, 0, 0, 0 },
    { MENU_CMD_INPUT_FUNC, 0xFFFF, SetExitMenuFlag },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_Options _optionsMenu = {
    { OptionsMenuBeforeDraw, 0, 0, 19 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aOptions },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    {
        { 10, 64, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFriendlyDiyGam },
        MENU_CMD_END
    },
    {
        { -102, 64, -116, 11 },
        { MENU_CMD_NAV, -1, -1, -1, 4 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _gameLengthStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeGameLength },
        MENU_CMD_END
    },
    {
        { 10, 76, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFixPitchType },
        MENU_CMD_END
    },
    {
        { -102, 76, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 2, 6 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _pitchTypeStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangePitchType },
        MENU_CMD_END
    },
    {
        { 10, 88, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAutoReplays },
        MENU_CMD_END
    },
    {
        { -102, 88, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 4, 8 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _autoReplaysStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeAutoReplays },
        MENU_CMD_END
    },
    {
        { 10, 100, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAllPlayerTeams },
        MENU_CMD_END
    },
    {
        { -102, 100, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 6, 10 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _allPlTeamsEqualStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeAllPlTeamsEqual },
        MENU_CMD_END
    },
    {
        { 10, 112, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAutoSaveHighli },
        MENU_CMD_END
    },
    {
        { -102, 112, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 8, 12 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _autoSaveHlStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeAutoSaveHighlights },
        MENU_CMD_END
    },
    {
        { 10, 124, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aMenuMusic },
        MENU_CMD_END
    },
    {
        { -102, 124, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 10, 14 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _menuMusicStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeMenuMusic },
        MENU_CMD_END
    },
    {
        { 10, 136, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aGameplay },
        MENU_CMD_END
    },
    {
        { -102, 136, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 12, 16 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _gameplayStyleStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeGameplayStyle },
        MENU_CMD_END
    },
    {
        { 10, 148, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aControls },
        MENU_CMD_END
    },
    {
        { -102, 148, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 14, 22 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _aRedefineKeys },
        { MENU_CMD_ONSELECT_FIRE, ShowControlsMenu },
        MENU_CMD_END
    },
    {
        { 10, 148, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aPitches },
        MENU_CMD_END
    },
    {
        { -102, 148, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 14, 22 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _pitchStyleStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangePitchStyle },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    { 102, -71, 100, 15 },
    { MENU_CMD_NAV, -1, -1, 22, -1 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_INPUT_FUNC, 0x30, ExitOptions },
    MENU_CMD_END,
    { -104, -48, 0, 6 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x0, _aSwosVersion152 },
    MENU_CMD_END,
    {
        { 10, 160, -116, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aCpuCards },
        MENU_CMD_END
    },
    {
        { -102, 160, -116, 11 },
        { MENU_CMD_NAV, -1, -1, 16, 19 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _cpuCardsStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangeCpuCards },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_DesignDiyTournament _designDiyTournamentMenu = {
    { DesignDIYTournamentInit, DesignDIYTournamentReinit, DesignDIYTournamentOnDraw, 19 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aDesignDiyTourn },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyFileBufferCopy.header.competitionName },
    MENU_CMD_END,
    { 0, 17, 304, 15 },
    { MENU_CMD_NAV, -1, -1, -1, 4 },
    { MENU_CMD_COLORS, 0x1D },
    { MENU_CMD_TEXT_PTR, 0x0, (char *)&_diyFileBufferCopy.header.competitionName },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect2 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        {
            { 0, 34, 76, 12 },
            { MENU_CMD_NAV, -1, 4, 2, 19 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyPitchSeasonStrTab },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect3 },
            MENU_CMD_END
        },
        {
            { 77, 34, 56, 12 },
            { MENU_CMD_NAV, 3, 5, 2, 5 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyPitchTypeStringTable },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect4 },
            MENU_CMD_END
        },
        {
            { 77, 34, 22, 12 },
            { MENU_CMD_NAV, 4, 7, 4, 20 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyStartMonthStrTab },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect5 },
            MENU_CMD_END
        }
    },
    {
        { 100, 34, 10, 12 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _asc_135D96 },
        MENU_CMD_END
    },
    {
        { 111, 34, 22, 12 },
        { MENU_CMD_NAV, 5, 9, 4, 11 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _diyEndMonthStrTab },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect7 },
        MENU_CMD_END
    },
    {
        { -118, 34, 64, 12 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAwayGoals },
        MENU_CMD_END
    },
    {
        { -54, 34, 102, 12 },
        { MENU_CMD_NAV, 7, -1, 2, 13 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _diyAwayGoalsStrTab },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect9 },
        MENU_CMD_END
    },
    {
        { 53, 47, 70, 12 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aSubstitutes },
        MENU_CMD_END
    },
    { 124, 47, 40, 12 },
    { MENU_CMD_NAV, -1, 13, 7, 23 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect11 },
    MENU_CMD_END,
    {
        { -91, 47, 38, 12 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFrom },
        MENU_CMD_END
    },
    { -52, 47, 40, 12 },
    { MENU_CMD_NAV, 11, -1, 9, 25 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect13 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 33 },
    {
        {
            { 1, 29, 34, 9 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aTeams },
            MENU_CMD_END
        },
        {
            { -126, 29, 120, 9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aDescription },
            MENU_CMD_END
        },
        {
            { 83, 29, 50, 9 },
            { MENU_CMD_TEXT_PTR, 0x0, _aSeeded },
            MENU_CMD_END
        }
    },
    {
        { MENU_MARK_START_MENU, 0, 71 },
        {
            { 0, 0, 6, 9 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 8, 10, 16, 8 },
            { MENU_CMD_SPRITE_FG, 185 },
            MENU_CMD_END
        },
        { 8, 0, 16, 9 },
        { MENU_CMD_NAV, -1, 20, 3, 31 },
        { MENU_CMD_COLORS, 0xB },
        { MENU_CMD_NUMBER, 0x0, 0 },
        { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
        MENU_CMD_END,
        { 25, 0, 78, 9 },
        { MENU_CMD_NAV, 19, 21, 5, 32 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
        MENU_CMD_END,
        {
            { 104, 0, 8, 9 },
            { MENU_CMD_NAV, 20, 23, 11, 33 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_PTR, 0x0, _aStar },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
            MENU_CMD_END
        },
        MENU_MARK_WIDE_COORDS,
        { 25, 10, 279, 9 },
        { MENU_CMD_COLORS, 0x0 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END,
        MENU_MARK_BYTE_COORDS,
        {
            {
                { 113, 0, 38, 9 },
                { MENU_CMD_NAV, 21, 24, 11, 26 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage1LegsStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                MENU_CMD_END
            },
            {
                { -104, 0, 72, 9 },
                { MENU_CMD_NAV, 23, 25, 11, 27 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage1ExtraTimeStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                MENU_CMD_END
            },
            {
                { -31, 0, 79, 9 },
                { MENU_CMD_NAV, 24, 26, 13, 28 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage1PenaltiesStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                MENU_CMD_END
            },
            {
                { 113, 0, 84, 9 },
                { MENU_CMD_NAV, 25, 27, 23, 35 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage1PtsForWinStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                MENU_CMD_END
            }
        },
        { 0, 0, 0, 0 },
        { MENU_CMD_NAV, 26, 28, 24, 36 },
        { MENU_CMD_INVISIBLE },
        MENU_CMD_END,
        {
            { -58, 0, 106, 9 },
            { MENU_CMD_NAV, 26, -1, 25, 37 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyStage1PlayCountStrTab },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
            MENU_CMD_END
        }
    },
    {
        { MENU_MARK_START_MENU, 0, 90 },
        {
            { 0, 0, 6, 9 },
            { MENU_CMD_NUMBER, 0x0, 2 },
            MENU_CMD_END
        },
        {
            { 8, 10, 16, 8 },
            { MENU_CMD_SPRITE_FG, 185 },
            MENU_CMD_END
        },
        { 8, 0, 16, 9 },
        { MENU_CMD_NAV, -1, 32, 19, 43 },
        { MENU_CMD_COLORS, 0xB },
        { MENU_CMD_NUMBER, 0x0, 0 },
        { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
        MENU_CMD_END,
        { 25, 0, 78, 9 },
        { MENU_CMD_NAV, 31, 33, 20, 44 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
        MENU_CMD_END,
        {
            { 104, 0, 8, 9 },
            { MENU_CMD_NAV, 32, 35, 21, 45 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_PTR, 0x0, _aStar },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
            MENU_CMD_END
        },
        MENU_MARK_WIDE_COORDS,
        { 25, 10, 279, 9 },
        { MENU_CMD_COLORS, 0x0 },
        { MENU_CMD_TEXT_PTR, 0x0, (char *)4 },
        MENU_CMD_END,
        MENU_MARK_BYTE_COORDS,
        {
            {
                { 113, 0, 38, 9 },
                { MENU_CMD_NAV, 33, 36, 26, 38 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage2LegsStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                MENU_CMD_END
            },
            {
                { -104, 0, 72, 9 },
                { MENU_CMD_NAV, 35, 37, 27, 39 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage2ExtraTimeStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                MENU_CMD_END
            },
            {
                { -31, 0, 79, 9 },
                { MENU_CMD_NAV, 36, 38, 28, 40 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage2PenaltiesStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                MENU_CMD_END
            },
            {
                { 113, 0, 84, 9 },
                { MENU_CMD_NAV, 37, 39, 35, 47 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage2PtsForWinStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                MENU_CMD_END
            }
        },
        { 0, 0, 0, 0 },
        { MENU_CMD_NAV, 38, 40, 36, 48 },
        { MENU_CMD_INVISIBLE },
        MENU_CMD_END,
        {
            { -58, 0, 106, 9 },
            { MENU_CMD_NAV, 38, -1, 37, 49 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_STRING_TABLE, 0x0, _diyStage2PlayCountStrTab },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
            MENU_CMD_END
        }
    },
    {
        {
            { MENU_MARK_START_MENU, 0, 109 },
            {
                { 0, 0, 6, 9 },
                { MENU_CMD_NUMBER, 0x0, 3 },
                MENU_CMD_END
            },
            {
                { 8, 10, 16, 8 },
                { MENU_CMD_SPRITE_FG, 185 },
                MENU_CMD_END
            },
            { 8, 0, 16, 9 },
            { MENU_CMD_NAV, -1, 44, 31, 55 },
            { MENU_CMD_COLORS, 0xB },
            { MENU_CMD_NUMBER, 0x0, 0 },
            { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
            MENU_CMD_END,
            { 25, 0, 78, 9 },
            { MENU_CMD_NAV, 43, 45, 32, 56 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
            MENU_CMD_END,
            {
                { 104, 0, 8, 9 },
                { MENU_CMD_NAV, 44, 47, 33, 57 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_TEXT_PTR, 0x0, _aStar },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
                MENU_CMD_END
            },
            MENU_MARK_WIDE_COORDS,
            { 25, 10, 279, 9 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            MENU_CMD_END,
            MENU_MARK_BYTE_COORDS,
            {
                {
                    { 113, 0, 38, 9 },
                    { MENU_CMD_NAV, 45, 48, 38, 50 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage3LegsStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                    MENU_CMD_END
                },
                {
                    { -104, 0, 72, 9 },
                    { MENU_CMD_NAV, 47, 49, 39, 51 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage3ExtraTimeStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                    MENU_CMD_END
                },
                {
                    { -31, 0, 79, 9 },
                    { MENU_CMD_NAV, 48, 50, 40, 52 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage3PenaltiesStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                    MENU_CMD_END
                },
                {
                    { 113, 0, 84, 9 },
                    { MENU_CMD_NAV, 49, 51, 47, 59 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage3PtsForWinStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                    MENU_CMD_END
                }
            },
            { 0, 0, 0, 0 },
            { MENU_CMD_NAV, 50, 52, 48, 60 },
            { MENU_CMD_INVISIBLE },
            MENU_CMD_END,
            {
                { -58, 0, 106, 9 },
                { MENU_CMD_NAV, 50, -1, 49, 61 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage3PlayCountStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 128 },
            {
                { 0, 0, 6, 9 },
                { MENU_CMD_NUMBER, 0x0, 4 },
                MENU_CMD_END
            },
            {
                { 8, 10, 16, 8 },
                { MENU_CMD_SPRITE_FG, 185 },
                MENU_CMD_END
            },
            { 8, 0, 16, 9 },
            { MENU_CMD_NAV, -1, 56, 43, 67 },
            { MENU_CMD_COLORS, 0xB },
            { MENU_CMD_NUMBER, 0x0, 0 },
            { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
            MENU_CMD_END,
            { 25, 0, 78, 9 },
            { MENU_CMD_NAV, 55, 57, 44, 68 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
            MENU_CMD_END,
            {
                { 104, 0, 8, 9 },
                { MENU_CMD_NAV, 56, 59, 45, 69 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_TEXT_PTR, 0x0, _aStar },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
                MENU_CMD_END
            },
            MENU_MARK_WIDE_COORDS,
            { 25, 10, 279, 9 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            MENU_CMD_END,
            MENU_MARK_BYTE_COORDS,
            {
                {
                    { 113, 0, 38, 9 },
                    { MENU_CMD_NAV, 57, 60, 50, 62 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage4LegsStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                    MENU_CMD_END
                },
                {
                    { -104, 0, 72, 9 },
                    { MENU_CMD_NAV, 59, 61, 51, 63 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage4ExtraTimeStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                    MENU_CMD_END
                },
                {
                    { -31, 0, 79, 9 },
                    { MENU_CMD_NAV, 60, 62, 52, 64 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage4PenaltiesStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                    MENU_CMD_END
                },
                {
                    { 113, 0, 84, 9 },
                    { MENU_CMD_NAV, 61, 63, 59, 71 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage4PtsForWinStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                    MENU_CMD_END
                }
            },
            { 0, 0, 0, 0 },
            { MENU_CMD_NAV, 62, 64, 60, 72 },
            { MENU_CMD_INVISIBLE },
            MENU_CMD_END,
            {
                { -58, 0, 106, 9 },
                { MENU_CMD_NAV, 62, -1, 61, 73 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage4PlayCountStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 147 },
            {
                { 0, 0, 6, 9 },
                { MENU_CMD_NUMBER, 0x0, 5 },
                MENU_CMD_END
            },
            {
                { 8, 10, 16, 8 },
                { MENU_CMD_SPRITE_FG, 185 },
                MENU_CMD_END
            },
            { 8, 0, 16, 9 },
            { MENU_CMD_NAV, -1, 68, 55, 79 },
            { MENU_CMD_COLORS, 0xB },
            { MENU_CMD_NUMBER, 0x0, 0 },
            { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
            MENU_CMD_END,
            { 25, 0, 78, 9 },
            { MENU_CMD_NAV, 67, 69, 56, 80 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
            MENU_CMD_END,
            {
                { 104, 0, 8, 9 },
                { MENU_CMD_NAV, 68, 71, 57, 81 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_TEXT_PTR, 0x0, _aStar },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
                MENU_CMD_END
            },
            MENU_MARK_WIDE_COORDS,
            { 25, 10, 279, 9 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            MENU_CMD_END,
            MENU_MARK_BYTE_COORDS,
            {
                {
                    { 113, 0, 38, 9 },
                    { MENU_CMD_NAV, 69, 72, 62, 74 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage5LegsStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                    MENU_CMD_END
                },
                {
                    { -104, 0, 72, 9 },
                    { MENU_CMD_NAV, 71, 73, 63, 75 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage5ExtraTimeStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                    MENU_CMD_END
                },
                {
                    { -31, 0, 79, 9 },
                    { MENU_CMD_NAV, 72, 74, 64, 76 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage5PenaltiesStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                    MENU_CMD_END
                },
                {
                    { 113, 0, 84, 9 },
                    { MENU_CMD_NAV, 73, 75, 71, 83 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage5PtsForWinStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                    MENU_CMD_END
                }
            },
            { 0, 0, 0, 0 },
            { MENU_CMD_NAV, 74, 76, 72, 84 },
            { MENU_CMD_INVISIBLE },
            MENU_CMD_END,
            {
                { -58, 0, 106, 9 },
                { MENU_CMD_NAV, 74, -1, 73, 85 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage5PlayCountStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
                MENU_CMD_END
            }
        },
        {
            { MENU_MARK_START_MENU, 0, 166 },
            {
                { 0, 0, 6, 9 },
                { MENU_CMD_NUMBER, 0x0, 6 },
                MENU_CMD_END
            },
            {
                { 8, 10, 16, 8 },
                { MENU_CMD_SPRITE_FG, 185 },
                MENU_CMD_END
            },
            { 8, 0, 16, 9 },
            { MENU_CMD_NAV, -1, 80, 67, 90 },
            { MENU_CMD_COLORS, 0xB },
            { MENU_CMD_NUMBER, 0x0, 0 },
            { MENU_CMD_INPUT_FUNC, 0x3C1, DesignDIYTournament_AdjustStageQualifiers },
            MENU_CMD_END,
            { 25, 0, 78, 9 },
            { MENU_CMD_NAV, 79, 81, 68, 90 },
            { MENU_CMD_COLORS, 0xD },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect20 },
            MENU_CMD_END,
            {
                { 104, 0, 8, 9 },
                { MENU_CMD_NAV, 80, 83, 69, 90 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_TEXT_PTR, 0x0, _aStar },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect21 },
                MENU_CMD_END
            },
            MENU_MARK_WIDE_COORDS,
            { 25, 10, 279, 9 },
            { MENU_CMD_COLORS, 0x0 },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            MENU_CMD_END,
            MENU_MARK_BYTE_COORDS,
            {
                {
                    { 113, 0, 38, 9 },
                    { MENU_CMD_NAV, 81, 84, 74, 86 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage6LegsStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect23 },
                    MENU_CMD_END
                },
                {
                    { -104, 0, 72, 9 },
                    { MENU_CMD_NAV, 83, 85, 75, 87 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage6ExtraTimeStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect24 },
                    MENU_CMD_END
                },
                {
                    { -31, 0, 79, 9 },
                    { MENU_CMD_NAV, 84, 86, 76, 88 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage6PenaltiesStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect25 },
                    MENU_CMD_END
                },
                {
                    { 113, 0, 84, 9 },
                    { MENU_CMD_NAV, 85, 87, 83, 90 },
                    { MENU_CMD_COLORS, 0xD },
                    { MENU_CMD_STRING_TABLE, 0x0, _diyStage6PtsForWinStrTab },
                    { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect26 },
                    MENU_CMD_END
                }
            },
            { 0, 0, 0, 0 },
            { MENU_CMD_NAV, 86, 88, 84, 91 },
            { MENU_CMD_INVISIBLE },
            MENU_CMD_END,
            {
                { -58, 0, 106, 9 },
                { MENU_CMD_NAV, 86, -1, 85, 91 },
                { MENU_CMD_COLORS, 0xD },
                { MENU_CMD_STRING_TABLE, 0x0, _diyStage6PlayCountStrTab },
                { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect28 },
                MENU_CMD_END
            }
        }
    },
    { MENU_MARK_START_MENU, 0, 0 },
    {
        {
            { 0, -71, 120, 15 },
            { MENU_CMD_NAV, -1, 90, 79, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aChooseTeams },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect89 },
            MENU_CMD_END
        },
        {
            { 89, -71, 62, 15 },
            { MENU_CMD_NAV, 89, 91, 86, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aOk },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOK },
            MENU_CMD_END
        },
        {
            { 121, -71, 62, 15 },
            { MENU_CMD_NAV, 90, -1, 87, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, DesignDIYTournamentOnSelect91 },
            MENU_CMD_END
        }
    },
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_FriendlyGame _friendlyGameMenu = {
    { FriendlyMenuInit, FriendlyMenuReinit, 0, 8 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFriendly_1 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 10 },
    {
        { 31, 70, 120, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aSubstitutes },
        MENU_CMD_END
    },
    {
        { -63, 70, 38, 15 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFrom },
        MENU_CMD_END
    },
    {
        { 31, 50, 120, 15 },
        { MENU_CMD_NAV, -1, 4, -1, 5 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _pitchTypeOrSeasonTable },
        { MENU_CMD_ONSELECT_FIRE, PitchTypeOrSeasonProc },
        MENU_CMD_END
    },
    {
        { -104, 50, 120, 15 },
        { MENU_CMD_NAV, 3, -1, -1, 5 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _friendlyPitchTypeTable },
        { MENU_CMD_ONSELECT_FIRE, FriendlyPitchTypeProc },
        MENU_CMD_END
    },
    { -104, 70, 40, 15 },
    { MENU_CMD_NAV, -1, 6, 4, 8 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, MinSubstitutes },
    MENU_CMD_END,
    { -24, 70, 40, 15 },
    { MENU_CMD_NAV, 5, -1, 4, 8 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, MaxSubstitutes },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 0 },
    {
        { 0, -71, 120, 15 },
        { MENU_CMD_NAV, -1, 9, 5, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aChooseTeams },
        { MENU_CMD_ONSELECT_FIRE, ChooseTeamsMenu },
        MENU_CMD_END
    },
    {
        { 121, -91, 62, 15 },
        { MENU_CMD_NAV, -1, -1, 5, 9 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aOk },
        { MENU_CMD_ONSELECT_FIRE, FriendlyOK },
        MENU_CMD_END
    },
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 7, -1, 8, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_EditTeams _editTeamsMenu = {
    { EditTeamsInit, EditTeamsFinish, 0, 0 },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, 0, -63, 12 },
        { MENU_CMD_NAV, -1, 5, -1, 39 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x10, 0 },
        { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectTeamName },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 274, 0, 30, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_NUMBER, 0x0, 0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 0, -74, 55, 10 },
        { MENU_CMD_TEXT_PTR, 0x8000, _aCoach_3 },
        MENU_CMD_END
    },
    {
        { 0, -66, -114, 10 },
        { MENU_CMD_NAV, -1, 20, 54, -1 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectCoachName },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 211, 21 },
    {
        { 23, 0, 0, 6 },
        { MENU_CMD_TEXT_PTR, 0x0, _aFirstKit },
        MENU_CMD_END
    },
    { 0, 7, 46, 33 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsPrimaryShirts },
    { MENU_CMD_NAV, 41, 13, 0, 6 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeShirtsPrimary },
    MENU_CMD_END,
    { 0, 40, 46, 19 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsShorts },
    { MENU_CMD_NAV, 43, 14, 5, 7 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeShortsPrimary },
    MENU_CMD_END,
    { 0, 59, 46, 19 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsSocks },
    { MENU_CMD_NAV, 45, 15, 6, 9 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeSocksPrimary },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 211, 101 },
    {
        { 23, 0, 0, 6 },
        { MENU_CMD_TEXT_PTR, 0x0, _aSecondKit },
        MENU_CMD_END
    },
    { 0, 7, 46, 33 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsSecondaryShirt },
    { MENU_CMD_NAV, 49, 17, 7, 10 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeShirtsSecondary },
    MENU_CMD_END,
    { 0, 40, 46, 19 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsSecondaryShorts },
    { MENU_CMD_NAV, 51, 18, 9, 11 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeShortsSecondary },
    MENU_CMD_END,
    { 0, 59, 46, 19 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_COPY, &_editTeamsSecondarySocks },
    { MENU_CMD_NAV, 53, 19, 10, 22 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, EditTeamsChangeSocksSecondary },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 272, 24 },
    { 0, 0, 24, 18 },
    { MENU_CMD_NAV, 5, -1, 0, 13 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 199 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectPrimaryShirtType },
    MENU_CMD_END,
    { 0, 19, 24, 18 },
    { MENU_CMD_NAV, 5, -1, 12, 14 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 200 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectPrimaryShirtType },
    MENU_CMD_END,
    { 0, 38, 24, 18 },
    { MENU_CMD_NAV, 6, -1, 13, 15 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 201 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectPrimaryShirtType },
    MENU_CMD_END,
    { 0, 57, 24, 18 },
    { MENU_CMD_NAV, 7, -1, 14, 16 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 202 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectPrimaryShirtType },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 272, 104 },
    { 0, 0, 24, 18 },
    { MENU_CMD_NAV, 9, -1, 15, 17 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 199 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectSecondaryShirtType },
    MENU_CMD_END,
    { 0, 19, 24, 18 },
    { MENU_CMD_NAV, 9, -1, 16, 18 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 200 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectSecondaryShirtType },
    MENU_CMD_END,
    { 0, 38, 24, 18 },
    { MENU_CMD_NAV, 10, -1, 17, 19 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 201 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectSecondaryShirtType },
    MENU_CMD_END,
    { 0, 57, 24, 18 },
    { MENU_CMD_NAV, 11, -1, 18, 21 },
    { MENU_CMD_COLORS, 0x9 },
    { MENU_CMD_SPRITE_FG, 202 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsSelectSecondaryShirtType },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 0 },
    {
        { -92, -71, 40, 15 },
        { MENU_CMD_NAV, 3, 22, 54, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aImport },
        { MENU_CMD_ONSELECT_FIRE, ImportTeamsMenu },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 264, 185, 40, 15 },
    { MENU_CMD_NAV, 22, -1, 19, -1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aOk },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsOK },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { -42, -71, 40, 15 },
        { MENU_CMD_NAV, 20, 21, 11, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aUndo },
        { MENU_CMD_ONSELECT_FIRE, EditTeamsUndo },
        MENU_CMD_END
    },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_BEFORE_DRAW, EditTeamsPlayersBeforeDraw },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 22 },
    { 0, 0, 10, 8 },
    { MENU_CMD_NAV, -1, 39, 0, 24 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 10, 10, 8 },
    { MENU_CMD_NAV, -1, 40, 23, 25 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 20, 10, 8 },
    { MENU_CMD_NAV, -1, 41, 24, 26 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 30, 10, 8 },
    { MENU_CMD_NAV, -1, 42, 25, 27 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 40, 10, 8 },
    { MENU_CMD_NAV, -1, 43, 26, 28 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 50, 10, 8 },
    { MENU_CMD_NAV, -1, 44, 27, 29 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 60, 10, 8 },
    { MENU_CMD_NAV, -1, 45, 28, 30 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 70, 10, 8 },
    { MENU_CMD_NAV, -1, 46, 29, 31 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 80, 10, 8 },
    { MENU_CMD_NAV, -1, 47, 30, 32 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 90, 10, 8 },
    { MENU_CMD_NAV, -1, 48, 31, 33 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 100, 10, 8 },
    { MENU_CMD_NAV, -1, 49, 32, 34 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 110, 10, 8 },
    { MENU_CMD_NAV, -1, 50, 33, 35 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, 120, 10, 8 },
    { MENU_CMD_NAV, -1, 51, 34, 36 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, -126, 10, 8 },
    { MENU_CMD_NAV, -1, 52, 35, 37 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, -116, 10, 8 },
    { MENU_CMD_NAV, -1, 53, 36, 38 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    { 0, -106, 10, 8 },
    { MENU_CMD_NAV, -1, 54, 37, 3 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerFace },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 22 },
    {
        {
            { 22, 0, -105, 8 },
            { MENU_CMD_NAV, 23, 5, 0, 40 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 10, -105, 8 },
            { MENU_CMD_NAV, 24, 5, 39, 41 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 20, -105, 8 },
            { MENU_CMD_NAV, 25, 5, 40, 42 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 30, -105, 8 },
            { MENU_CMD_NAV, 26, 5, 41, 43 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 40, -105, 8 },
            { MENU_CMD_NAV, 27, 6, 42, 44 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 50, -105, 8 },
            { MENU_CMD_NAV, 28, 6, 43, 45 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 60, -105, 8 },
            { MENU_CMD_NAV, 29, 7, 44, 46 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 70, -105, 8 },
            { MENU_CMD_NAV, 30, 7, 45, 47 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 80, -105, 8 },
            { MENU_CMD_NAV, 31, 9, 46, 48 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 90, -105, 8 },
            { MENU_CMD_NAV, 32, 9, 47, 49 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 100, -105, 8 },
            { MENU_CMD_NAV, 33, 9, 48, 50 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 110, -105, 8 },
            { MENU_CMD_NAV, 34, 9, 49, 51 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, 120, -105, 8 },
            { MENU_CMD_NAV, 35, 10, 50, 52 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, -126, -105, 8 },
            { MENU_CMD_NAV, 36, 10, 51, 53 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, -116, -105, 8 },
            { MENU_CMD_NAV, 37, 11, 52, 54 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        },
        {
            { 22, -106, -105, 8 },
            { MENU_CMD_NAV, 38, 11, 53, 3 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            { MENU_CMD_ONSELECT_FIRE, EditTeamsChangePlayerName },
            MENU_CMD_END
        }
    },
    {
        { 11, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -92, 0, 9, 8 },
        { MENU_CMD_COLORS, 0xA },
        MENU_CMD_END
    },
    {
        { -82, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        {
            { -68, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -62, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -91, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    MENU_MARK_END_MENU
};
Menu_CompetitionTopGoalScorers _competitionTopGoalScorersMenu = {
    { CompetitionTopGoalScorersInit, 0, 0, 2 },
    { 0, 0, 304, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 52, 16, -56, 13 },
        { MENU_CMD_COLORS, 0x79 },
        { MENU_CMD_TEXT_PTR, 0x0, _aLeadingCompeti },
        MENU_CMD_END
    },
    { 102, -71, 100, 15 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, CompetitionTopGoalScorersExit },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 14, 32 },
    {
        { 0, 0, 12, 8 },
        { MENU_CMD_NUMBER, 0x0, 1 },
        MENU_CMD_END
    },
    {
        {
            { 20, 0, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 0, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 0, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 10, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 2 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 10, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 10, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 10, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 20, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 3 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 20, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 20, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 20, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 30, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 4 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 30, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 30, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 30, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 40, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 5 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 40, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 40, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 40, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 50, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 6 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 50, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 50, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 50, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 60, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 7 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 60, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 60, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 60, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 70, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 8 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 70, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 70, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 70, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 80, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 9 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 80, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 80, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 80, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 90, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 10 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 90, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 90, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 90, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 100, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 11 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 100, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 100, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 100, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 110, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 12 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 110, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 110, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 110, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 120, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 13 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, 120, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, 120, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, 120, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, -126, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 14 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, -126, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, -126, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        {
            { -1, -126, 30, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, -116, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 15 },
            MENU_CMD_END
        }
    },
    {
        {
            { 20, -116, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -101, -116, 90, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        { -1, -116, 30, 8 },
        { MENU_CMD_NUMBER, 0x0, 1 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_TopGoalScorers _topGoalScorersMenu = {
    { TopGoalScorersInit, 0, 0, 5 },
    MENU_MARK_BYTE_COORDS,
    {
        { 52, 0, -56, 13 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aTopGoalScorers },
        MENU_CMD_END
    },
    { -9, 18, 12, 24 },
    { MENU_CMD_MULTILINE, 0x0, _aLge_0 },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 262, 18, 12, 24 },
            { MENU_CMD_MULTILINE, 0x0, _aCup1 },
            MENU_CMD_END
        },
        {
            { 277, 18, 12, 24 },
            { MENU_CMD_MULTILINE, 0x0, _aCup2 },
            MENU_CMD_END
        },
        {
            { 292, 18, 12, 24 },
            { MENU_CMD_MULTILINE, 0x0, _aTot_0 },
            MENU_CMD_END
        }
    },
    MENU_MARK_BYTE_COORDS,
    { 102, -71, 100, 15 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, TopGoalScorersExit },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 45 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_BEFORE_DRAW, TopGoalScorersBeforeDrawCommon },
    MENU_CMD_END,
    {
        {
            { 0, 0, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 1 },
            MENU_CMD_END
        },
        {
            { 0, 9, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 2 },
            MENU_CMD_END
        },
        {
            { 0, 18, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 3 },
            MENU_CMD_END
        },
        {
            { 0, 27, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 4 },
            MENU_CMD_END
        },
        {
            { 0, 36, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 5 },
            MENU_CMD_END
        },
        {
            { 0, 45, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 6 },
            MENU_CMD_END
        },
        {
            { 0, 54, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 7 },
            MENU_CMD_END
        },
        {
            { 0, 63, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 8 },
            MENU_CMD_END
        },
        {
            { 0, 72, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 9 },
            MENU_CMD_END
        },
        {
            { 0, 81, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 10 },
            MENU_CMD_END
        },
        {
            { 0, 90, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 11 },
            MENU_CMD_END
        },
        {
            { 0, 99, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 12 },
            MENU_CMD_END
        },
        {
            { 0, 108, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 13 },
            MENU_CMD_END
        },
        {
            { 0, 117, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 14 },
            MENU_CMD_END
        },
        {
            { 0, 126, 8, 8 },
            { MENU_CMD_NUMBER, 0x0, 15 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    {
        { 12, 0, 96, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -117, 0, 100, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    {
        { -9, 0, 12, 8 },
        { MENU_CMD_NUMBER, 0x0, 22 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 262, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 33 },
            MENU_CMD_END
        },
        {
            { 277, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 44 },
            MENU_CMD_END
        },
        {
            { 292, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 77 },
            MENU_CMD_END
        }
    },
    MENU_MARK_END_MENU
};
Menu_Squad _squadMenu = {
    { SquadInit, SquadFinish, 0, 9 },
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 20, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_NUMBER, 0x0, 0 },
    MENU_CMD_END,
    { 3, 6, 16, 16 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    {
        { 22, 0, -121, 11 },
        { MENU_CMD_COLORS, 0x0 },
        { MENU_CMD_TEXT_PTR, 0x10, 0 },
        MENU_CMD_END
    },
    {
        { 89, 14, 0, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    {
        {
            { 0, -64, 55, 8 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aCoach_4 },
            MENU_CMD_END
        },
        {
            { 44, -64, -114, 8 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        }
    },
    { -12, -71, 60, 15 },
    { MENU_CMD_NAV, 8, -1, 45, 9 },
    { MENU_CMD_DOWN_SKIP, 6, 3 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aTraining },
    { MENU_CMD_ONSELECT_FIRE, SelectTraining },
    MENU_CMD_END,
    { 0, 0, 0, 0 },
    { MENU_CMD_NAV, -1, -1, 6, -1 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    { -74, -71, 60, 15 },
    { MENU_CMD_NAV, 44, 6, 45, 9 },
    { MENU_CMD_LEFT_SKIP, 7, 2 },
    { MENU_CMD_RIGHT_SKIP, 8, 1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aGoals },
    { MENU_CMD_ONSELECT_FIRE, SelectGoals },
    MENU_CMD_END,
    {
        { -74, -55, 122, 15 },
        { MENU_CMD_NAV, 44, -1, 8, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    { -43, -71, 60, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
    MENU_CMD_END,
    { 0, 0, 0, 0 },
    { MENU_CMD_ONSELECT_FIRE, SquadOnSelect11 },
    MENU_CMD_END,
    {
        { -19, 2, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    {
        {
            { -41, 14, 60, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aGoals },
            MENU_CMD_END
        },
        {
            { -83, 14, 60, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aNat },
            MENU_CMD_END
        },
        {
            { -38, 14, 48, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aForm },
            MENU_CMD_END
        }
    },
    {
        { -84, 3, -126, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    {
        {
            { -22, 14, 25, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aCups },
            MENU_CMD_END
        },
        {
            { -51, 14, 20, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aLge },
            MENU_CMD_END
        },
        {
            { -31, 14, 20, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aCup_3 },
            MENU_CMD_END
        }
    },
    { -11, 14, 20, 6 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 265, 14, 20, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, 0 },
            MENU_CMD_END
        },
        {
            { 285, 14, 18, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _aTot },
            MENU_CMD_END
        }
    },
    {
        { 0, 172, 304, 9 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    {
        { 274, 183, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aOwnGoals },
        MENU_CMD_END
    },
    {
        { 289, 183, 12, 8 },
        { MENU_CMD_NUMBER, 0x0, 222 },
        MENU_CMD_END
    },
    {
        { 274, 192, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aExPlayerGoals },
        MENU_CMD_END
    },
    {
        { 289, 192, 12, 8 },
        { MENU_CMD_NUMBER, 0x0, 333 },
        MENU_CMD_END
    },
    MENU_MARK_START_TEMPLATE,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, SquadAfterDrawCommon },
    { MENU_CMD_ONSELECT_FIRE, SquadCommon },
    MENU_CMD_END,
    {
        {
            { 23, 24, -122, 8 },
            { MENU_CMD_NAV, -1, 8, -1, 29 },
            MENU_CMD_END
        },
        {
            { 23, 33, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 28, 30 },
            MENU_CMD_END
        },
        {
            { 23, 42, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 29, 31 },
            MENU_CMD_END
        },
        {
            { 23, 51, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 30, 32 },
            MENU_CMD_END
        },
        {
            { 23, 60, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 31, 33 },
            MENU_CMD_END
        },
        {
            { 23, 69, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 32, 34 },
            MENU_CMD_END
        },
        {
            { 23, 78, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 33, 35 },
            MENU_CMD_END
        },
        {
            { 23, 87, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 34, 36 },
            MENU_CMD_END
        },
        {
            { 23, 96, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 35, 37 },
            MENU_CMD_END
        },
        {
            { 23, 105, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 36, 38 },
            MENU_CMD_END
        },
        {
            { 23, 114, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 37, 39 },
            MENU_CMD_END
        },
        {
            { 23, 125, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 38, 40 },
            MENU_CMD_END
        },
        {
            { 23, -122, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 39, 41 },
            MENU_CMD_END
        },
        {
            { 23, -113, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 40, 42 },
            MENU_CMD_END
        },
        {
            { 23, -104, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 41, 43 },
            MENU_CMD_END
        },
        {
            { 23, -95, -122, 8 },
            { MENU_CMD_NAV, -1, 8, 42, 44 },
            MENU_CMD_END
        }
    },
    { 23, -84, -122, 8 },
    { MENU_CMD_NAV, 45, 8, 43, 8 },
    { MENU_CMD_UP_SKIP, 44, 2 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { 0, -84, 12, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, -1, 44, 43, 8 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, SquadOnSelect45 },
    { MENU_CMD_AFTER_DRAW, Squad45AfterDraw },
    MENU_CMD_END,
    { 0, 0, 12, 8 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 23, 0, -122, 8 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -98, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -75, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -84, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -77, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -107, 0, 8, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -68, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    {
        { -26, 0, 30, 8 },
        { MENU_CMD_NUMBER, 0x0, 333 },
        MENU_CMD_END
    },
    { -38, 0, 30, 8 },
    { MENU_CMD_FG_FUNC, locret_139AE2 },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        { 272, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { -61, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    {
        {
            { -47, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 22 },
            MENU_CMD_END
        },
        {
            { -27, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 33 },
            MENU_CMD_END
        },
        {
            { -7, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 44 },
            MENU_CMD_END
        }
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 269, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 55 },
            MENU_CMD_END
        },
        {
            { 289, 0, 12, 8 },
            { MENU_CMD_NUMBER, 0x0, 77 },
            MENU_CMD_END
        }
    },
    MENU_MARK_END_MENU
};
Menu_CompetitionStats _competitionStatsMenu = {
    { 0, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aStatistics },
        MENU_CMD_END
    },
    {
        { 92, 40, 120, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aHighestScorerL },
        { MENU_CMD_ONSELECT_FIRE, StatsHighestScorerList },
        MENU_CMD_END
    },
    {
        { 92, 65, 120, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aCompetitionInf },
        { MENU_CMD_ONSELECT_FIRE, CompetitionInfo },
        MENU_CMD_END
    },
    {
        { 92, 90, 120, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewSquads },
        { MENU_CMD_ONSELECT_FIRE, StatsViewSquads },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 3, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, StatsExit },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_CareerOver _careerOverMenu = {
    { CareerOverInit, CareerOverFinish, 0, 4 },
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 90, 14 },
    { MENU_CMD_COLORS, 0x79 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { 91, 0, 122, 14 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { -42, 0, 90, 14 },
    { MENU_CMD_COLORS, 0x79 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { 82, 25, -116, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x0, _aYourCareerIsOv },
    MENU_CMD_END,
    {
        { 92, 50, 120, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 5 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewCompetitio },
        { MENU_CMD_ONSELECT_FIRE, ViewCompetitionsMenuRetired },
        MENU_CMD_END
    },
    {
        { 92, 70, 120, 15 },
        { MENU_CMD_NAV, -1, -1, 4, 6 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aJobOffers },
        { MENU_CMD_ONSELECT_FIRE, JobOffersMenuRetired },
        MENU_CMD_END
    },
    {
        { 82, 118, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 5, 7 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aContinueCareer },
        { MENU_CMD_ONSELECT_FIRE, ContinueCareer },
        MENU_CMD_END
    },
    {
        { 102, -91, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 6, 8 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aSaveCareer },
        { MENU_CMD_ONSELECT_FIRE, SaveCareer },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 7, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitCareerMenu },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Career _careerMenu = {
    { CareerMenuInit, CareerMenuFinish, 0, 3 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aCareer_1 },
        MENU_CMD_END
    },
    { 92, 20, 120, 10 },
    { MENU_CMD_NAV, -1, -1, -1, 4 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_PTR, 0x0, _aLoadLiverpool },
    { MENU_CMD_ONSELECT_FIRE, LoadLiverpool },
    MENU_CMD_END,
    {
        { 30, 65, 60, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aName },
        MENU_CMD_END
    },
    {
        { 94, 65, 20, 11 },
        { MENU_CMD_NAV, -1, 4, 1, 7 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _managerSexBuffer },
        { MENU_CMD_ONSELECT_FIRE, ChangeSex },
        MENU_CMD_END
    },
    {
        { 115, 65, 58, 11 },
        { MENU_CMD_NAV, 3, 5, 1, 7 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _managerNameBuffer },
        { MENU_CMD_ONSELECT_FIRE, SetName },
        MENU_CMD_END
    },
    {
        { -82, 65, 82, 11 },
        { MENU_CMD_NAV, 4, -1, 1, 7 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _managerSurnameBuffer },
        { MENU_CMD_ONSELECT_FIRE, SetSurname },
        MENU_CMD_END
    },
    {
        { 30, 85, 80, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aNationality },
        MENU_CMD_END
    },
    { 114, 85, 40, 11 },
    { MENU_CMD_NAV, -1, -1, 4, 9 },
    { MENU_CMD_COLORS, 0xB },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, ChangeNationality },
    MENU_CMD_END,
    {
        { 30, 105, 120, 11 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aStartCareerAs },
        MENU_CMD_END
    },
    {
        { -102, 105, 120, 11 },
        { MENU_CMD_NAV, -1, -1, 7, 10 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _offsetCoachOrPlayer },
        { MENU_CMD_ONSELECT_FIRE, CoachOrPlayerCoach },
        MENU_CMD_END
    },
    {
        { 102, -91, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 9, 11 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aChooseTeam },
        { MENU_CMD_ONSELECT_FIRE, ChooseTeamAndStartCareer },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 10, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_ViewCompetitions _viewCompetitionsMenu = {
    { ViewCompetitionsInit, ViewCompetitionsOnReturn, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewCompetitio },
        MENU_CMD_END
    },
    {
        { 47, 30, -46, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, OpenDiyFromFile },
        MENU_CMD_END
    },
    {
        { 47, 55, -46, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, OpenDiyPreset1 },
        MENU_CMD_END
    },
    {
        { 47, 80, -46, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, OpenDiyPreset2 },
        MENU_CMD_END
    },
    {
        { 47, 105, -46, 15 },
        { MENU_CMD_NAV, -1, -1, 3, 5 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, OpenDiyPreset3 },
        MENU_CMD_END
    },
    {
        { 47, -126, -46, 15 },
        { MENU_CMD_NAV, -1, -1, 4, 6 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aTopGoalScorers },
        { MENU_CMD_ONSELECT_FIRE, TopGoalScorersMenu },
        MENU_CMD_END
    },
    {
        { 47, -101, -46, 15 },
        { MENU_CMD_NAV, -1, -1, 5, 7 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aManagementReco_0 },
        { MENU_CMD_ONSELECT_FIRE, ManagementRecordMenu },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 6, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Contest _contestMenu = {
    { ContestMenuInit, ContestMenuOnReturn, 0, 7 },
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 90, 14 },
    { MENU_CMD_COLORS, 0x79 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { 91, 0, 122, 14 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { -42, 0, 90, 14 },
    { MENU_CMD_COLORS, 0x79 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, _aSeason_1 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, -94, 100, 12 },
    { MENU_CMD_NAV, -1, 7, 17, 5 },
    { MENU_CMD_UP_SKIP, 4, 2 },
    { MENU_CMD_COLORS, 0x8 },
    { MENU_CMD_TEXT_PTR, 0x0, _aTransfers },
    { MENU_CMD_ONSELECT_FIRE, TransfersMenu },
    MENU_CMD_END,
    { 0, -81, 100, 12 },
    { MENU_CMD_NAV, -1, 8, 4, 6 },
    { MENU_CMD_UP_SKIP, 5, 2 },
    { MENU_CMD_RIGHT_SKIP, 6, 1 },
    { MENU_CMD_COLORS, 0xB },
    { MENU_CMD_TEXT_PTR, 0x0, _aJobOffers },
    { MENU_CMD_ONSELECT_FIRE, JobOffersMenu },
    MENU_CMD_END,
    { 0, -68, 100, 12 },
    { MENU_CMD_NAV, -1, 9, 5, -1 },
    { MENU_CMD_RIGHT_SKIP, 6, 1 },
    { MENU_CMD_UP_SKIP, 6, 2 },
    { MENU_CMD_COLORS, 0x8 },
    { MENU_CMD_TEXT_PTR, 0x0, _aClubBusiness },
    { MENU_CMD_ONSELECT_FIRE, ClubBusinessMenu },
    MENU_CMD_END,
    { 101, -100, 100, 18 },
    { MENU_CMD_NAV, 4, 10, 17, 8 },
    { MENU_CMD_UP_SKIP, 7, 3 },
    { MENU_CMD_DOWN_SKIP, 7, 3 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aPlayMatch },
    { MENU_CMD_ONSELECT_FIRE, PlayMatchMenu },
    MENU_CMD_END,
    { 101, -81, 100, 12 },
    { MENU_CMD_NAV, 5, 11, 7, 13 },
    { MENU_CMD_LEFT_SKIP, 8, 0 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aSquad },
    { MENU_CMD_ONSELECT_FIRE, SquadMenu },
    MENU_CMD_END,
    {
        { 101, -68, 100, 12 },
        { MENU_CMD_NAV, 6, 12, 8, -1 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    { -54, -94, 100, 12 },
    { MENU_CMD_NAV, 7, -1, 17, 11 },
    { MENU_CMD_DOWN_SKIP, 10, 3 },
    { MENU_CMD_COLORS, 0x4 },
    { MENU_CMD_TEXT_PTR, 0x0, _aNextMatch },
    { MENU_CMD_ONSELECT_FIRE, NextMatch },
    MENU_CMD_END,
    {
        { -54, -81, 100, 12 },
        { MENU_CMD_NAV, 8, -1, 10, 12 },
        { MENU_CMD_COLORS, 0x5 },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewCompetitio },
        { MENU_CMD_ONSELECT_FIRE, ViewCompetitionsMenu },
        MENU_CMD_END
    },
    { -54, -68, 100, 12 },
    { MENU_CMD_NAV, 9, -1, 11, -1 },
    { MENU_CMD_RIGHT_SKIP, 9, 3 },
    { MENU_CMD_COLORS, 0x4 },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewWorld },
    { MENU_CMD_ONSELECT_FIRE, ViewWorldMenu },
    MENU_CMD_END,
    { 101, -68, 100, 11 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, 17, -1, 8, 14 },
    { MENU_CMD_COLORS, 0x4 },
    { MENU_CMD_TEXT_PTR, 0x0, _aNextMatch },
    { MENU_CMD_ONSELECT_FIRE, NextMatch },
    MENU_CMD_END,
    { 101, -55, 100, 11 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, 17, -1, 13, 15 },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewCompetitio },
    { MENU_CMD_ONSELECT_FIRE, ViewCompetitionsMenu },
    MENU_CMD_END,
    { 101, -42, 100, 11 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, 17, -1, 14, 9 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
    MENU_CMD_END,
    { 0, 16, 8, 14 },
    { MENU_CMD_NAV, -1, 7, -1, 17 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 183 },
    { MENU_CMD_ONSELECT_FIRE, ContestMenuScrollUp },
    MENU_CMD_END,
    { 0, -109, 8, 14 },
    { MENU_CMD_NAV, -1, 7, 16, 4 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 184 },
    { MENU_CMD_ONSELECT_FIRE, ContestMenuScrollDown },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        { 0, 31, 304, 116 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 113 },
    MENU_MARK_START_TEMPLATE,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 0, 0 },
    { MENU_CMD_BEFORE_DRAW, CareerGameListCommon },
    MENU_CMD_END,
    {
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        },
        {
            { 0, 0, 0, 0 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    MENU_MARK_WIDE_COORDS,
    {
        { 0, 0, 304, 8 },
        { MENU_CMD_COLORS, 0xA },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, 0, 62, 6 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { 0, 8, 62, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    {
        { 70, 0, 12, 6 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 84, 0, -37, 8 },
        { MENU_CMD_COLORS, 0xB },
        MENU_CMD_END
    },
    {
        { 84, 0, 96, 6 },
        { MENU_CMD_TEXT_PTR, 0x4000, 0 },
        MENU_CMD_END
    },
    {
        { -76, 0, 12, 6 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -65, 0, 6, 6 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -60, 0, 12, 6 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -48, 0, 96, 6 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { 84, 8, -36, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0x1 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_ManagementRecord _managementRecordMenu = {
    { ManagementRecordInit, 0, 0, 3 },
    { 27, -25, 250, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_BEFORE_DRAW, FillManagementRecordInfo },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 8, 14 },
    { MENU_CMD_NAV, -1, 3, -1, 2 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 183 },
    { MENU_CMD_ONSELECT_FIRE, ManagementArrowUpSelect },
    MENU_CMD_END,
    { 0, -70, 8, 14 },
    { MENU_CMD_NAV, -1, 3, 1, 3 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 184 },
    { MENU_CMD_ONSELECT_FIRE, ManagementArrowDownSelect },
    MENU_CMD_END,
    {
        { 102, -46, 100, 15 },
        { MENU_CMD_NAV, 2, -1, 2, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    {
        {
            { -104, 0, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x10 },
            MENU_CMD_END
        },
        {
            { -104, 0, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x7 },
            MENU_CMD_END
        },
        {
            { -104, 0, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x11 },
            MENU_CMD_END
        },
        {
            { 0, 0, -112, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4001 },
            MENU_CMD_END
        },
        {
            { -96, 0, -112, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 6 },
    { MENU_CMD_TEXT_PTR, 0xA, _aSacked },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { -104, 0, 0, 6 },
        { MENU_CMD_TEXT_PTR, 0x7, _aAllMatches },
        MENU_CMD_END
    },
    {
        { -104, 0, 0, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 6 },
    { MENU_CMD_TEXT_PTR, 0x7, 0 },
    MENU_CMD_END,
    {
        {
            { 0, 0, 304, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x1 },
            MENU_CMD_END
        },
        {
            { 0, 0, 304, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x1 },
            MENU_CMD_END
        }
    },
    MENU_MARK_BYTE_COORDS,
    {
        { -104, -68, 0, 6 },
        { MENU_CMD_TEXT_PTR, 0x1D, _aCareerTotal },
        MENU_CMD_END
    },
    {
        { -104, -58, 0, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0xD },
        MENU_CMD_END
    },
    {
        {
            { -104, -122, 20, 12 },
            { MENU_CMD_SPRITE_FG, 0 },
            MENU_CMD_END
        },
        {
            { -104, -122, 20, 12 },
            { MENU_CMD_SPRITE_FG, 0 },
            MENU_CMD_END
        },
        {
            { -104, -122, 20, 12 },
            { MENU_CMD_SPRITE_FG, 0 },
            MENU_CMD_END
        },
        {
            { -104, -122, 20, 12 },
            { MENU_CMD_SPRITE_FG, 0 },
            MENU_CMD_END
        }
    },
    MENU_MARK_END_MENU
};
Menu_SelectTeamsForTactics _selectTeamsForTacticsMenu = {
    { SelTeamForTacticsInit, SelTeamForTacticsInit, 0, 23 },
    MENU_MARK_BYTE_COORDS,
    {
        { 77, 0, -106, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, -1 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, SelTeamForTacticsCommon },
    MENU_CMD_END,
    {
        {
            { 1, 21, 100, 8 },
            { MENU_CMD_NAV, -1, 23, -1, 2 },
            { MENU_CMD_LEFT_SKIP, 44, 2 },
            MENU_CMD_END
        },
        {
            { 1, 30, 100, 8 },
            { MENU_CMD_NAV, -1, 24, 1, 3 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 39, 100, 8 },
            { MENU_CMD_NAV, -1, 25, 2, 4 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 48, 100, 8 },
            { MENU_CMD_NAV, -1, 26, 3, 5 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 57, 100, 8 },
            { MENU_CMD_NAV, -1, 27, 4, 6 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 66, 100, 8 },
            { MENU_CMD_NAV, -1, 28, 5, 7 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 75, 100, 8 },
            { MENU_CMD_NAV, -1, 29, 6, 8 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 84, 100, 8 },
            { MENU_CMD_NAV, -1, 30, 7, 9 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 93, 100, 8 },
            { MENU_CMD_NAV, -1, 31, 8, 10 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 102, 100, 8 },
            { MENU_CMD_NAV, -1, 32, 9, 11 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 111, 100, 8 },
            { MENU_CMD_NAV, -1, 33, 10, 12 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, 120, 100, 8 },
            { MENU_CMD_NAV, -1, 34, 11, 13 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -127, 100, 8 },
            { MENU_CMD_NAV, -1, 35, 12, 14 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -118, 100, 8 },
            { MENU_CMD_NAV, -1, 36, 13, 15 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -109, 100, 8 },
            { MENU_CMD_NAV, -1, 37, 14, 16 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -100, 100, 8 },
            { MENU_CMD_NAV, -1, 38, 15, 17 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -91, 100, 8 },
            { MENU_CMD_NAV, -1, 39, 16, 18 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -82, 100, 8 },
            { MENU_CMD_NAV, -1, 40, 17, 19 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -73, 100, 8 },
            { MENU_CMD_NAV, -1, 41, 18, 20 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -64, 100, 8 },
            { MENU_CMD_NAV, -1, 42, 19, 21 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -55, 100, 8 },
            { MENU_CMD_NAV, -1, 43, 20, 22 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        },
        {
            { 1, -46, 100, 8 },
            { MENU_CMD_NAV, -1, 44, 21, 67 },
            { MENU_CMD_UP_SKIP, 22, 2 },
            MENU_CMD_END
        }
    },
    {
        {
            { 102, 21, 100, 8 },
            { MENU_CMD_NAV, 1, 45, -1, 24 },
            MENU_CMD_END
        },
        {
            { 102, 30, 100, 8 },
            { MENU_CMD_NAV, 2, 46, 23, 25 },
            MENU_CMD_END
        },
        {
            { 102, 39, 100, 8 },
            { MENU_CMD_NAV, 3, 47, 24, 26 },
            MENU_CMD_END
        },
        {
            { 102, 48, 100, 8 },
            { MENU_CMD_NAV, 4, 48, 25, 27 },
            MENU_CMD_END
        },
        {
            { 102, 57, 100, 8 },
            { MENU_CMD_NAV, 5, 49, 26, 28 },
            MENU_CMD_END
        },
        {
            { 102, 66, 100, 8 },
            { MENU_CMD_NAV, 6, 50, 27, 29 },
            MENU_CMD_END
        },
        {
            { 102, 75, 100, 8 },
            { MENU_CMD_NAV, 7, 51, 28, 30 },
            MENU_CMD_END
        },
        {
            { 102, 84, 100, 8 },
            { MENU_CMD_NAV, 8, 52, 29, 31 },
            MENU_CMD_END
        },
        {
            { 102, 93, 100, 8 },
            { MENU_CMD_NAV, 9, 53, 30, 32 },
            MENU_CMD_END
        },
        {
            { 102, 102, 100, 8 },
            { MENU_CMD_NAV, 10, 54, 31, 33 },
            MENU_CMD_END
        },
        {
            { 102, 111, 100, 8 },
            { MENU_CMD_NAV, 11, 55, 32, 34 },
            MENU_CMD_END
        },
        {
            { 102, 120, 100, 8 },
            { MENU_CMD_NAV, 12, 56, 33, 35 },
            MENU_CMD_END
        },
        {
            { 102, -127, 100, 8 },
            { MENU_CMD_NAV, 13, 57, 34, 36 },
            MENU_CMD_END
        },
        {
            { 102, -118, 100, 8 },
            { MENU_CMD_NAV, 14, 58, 35, 37 },
            MENU_CMD_END
        },
        {
            { 102, -109, 100, 8 },
            { MENU_CMD_NAV, 15, 59, 36, 38 },
            MENU_CMD_END
        },
        {
            { 102, -100, 100, 8 },
            { MENU_CMD_NAV, 16, 60, 37, 39 },
            MENU_CMD_END
        },
        {
            { 102, -91, 100, 8 },
            { MENU_CMD_NAV, 17, 61, 38, 40 },
            MENU_CMD_END
        },
        {
            { 102, -82, 100, 8 },
            { MENU_CMD_NAV, 18, 62, 39, 41 },
            MENU_CMD_END
        },
        {
            { 102, -73, 100, 8 },
            { MENU_CMD_NAV, 19, 63, 40, 42 },
            MENU_CMD_END
        },
        {
            { 102, -64, 100, 8 },
            { MENU_CMD_NAV, 20, 64, 41, 43 },
            MENU_CMD_END
        },
        {
            { 102, -55, 100, 8 },
            { MENU_CMD_NAV, 21, 65, 42, 44 },
            MENU_CMD_END
        },
        {
            { 102, -46, 100, 8 },
            { MENU_CMD_NAV, 22, 66, 43, 67 },
            MENU_CMD_END
        }
    },
    {
        {
            { -53, 21, 100, 8 },
            { MENU_CMD_NAV, 23, -1, -1, 46 },
            { MENU_CMD_LEFT_SKIP, 44, 2 },
            MENU_CMD_END
        },
        {
            { -53, 30, 100, 8 },
            { MENU_CMD_NAV, 24, -1, 45, 47 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 39, 100, 8 },
            { MENU_CMD_NAV, 25, -1, 46, 48 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 48, 100, 8 },
            { MENU_CMD_NAV, 26, -1, 47, 49 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 57, 100, 8 },
            { MENU_CMD_NAV, 27, -1, 48, 50 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 66, 100, 8 },
            { MENU_CMD_NAV, 28, -1, 49, 51 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 75, 100, 8 },
            { MENU_CMD_NAV, 29, -1, 50, 52 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 84, 100, 8 },
            { MENU_CMD_NAV, 30, -1, 51, 53 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 93, 100, 8 },
            { MENU_CMD_NAV, 31, -1, 52, 54 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 102, 100, 8 },
            { MENU_CMD_NAV, 32, -1, 53, 55 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 111, 100, 8 },
            { MENU_CMD_NAV, 33, -1, 54, 56 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, 120, 100, 8 },
            { MENU_CMD_NAV, 34, -1, 55, 57 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -127, 100, 8 },
            { MENU_CMD_NAV, 35, -1, 56, 58 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -118, 100, 8 },
            { MENU_CMD_NAV, 36, -1, 57, 59 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -109, 100, 8 },
            { MENU_CMD_NAV, 37, -1, 58, 60 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -100, 100, 8 },
            { MENU_CMD_NAV, 38, -1, 59, 61 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -91, 100, 8 },
            { MENU_CMD_NAV, 39, -1, 60, 62 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -82, 100, 8 },
            { MENU_CMD_NAV, 40, -1, 61, 63 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -73, 100, 8 },
            { MENU_CMD_NAV, 41, -1, 62, 64 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -64, 100, 8 },
            { MENU_CMD_NAV, 42, -1, 63, 65 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -55, 100, 8 },
            { MENU_CMD_NAV, 43, -1, 64, 66 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        },
        {
            { -53, -46, 100, 8 },
            { MENU_CMD_NAV, 44, -1, 65, 67 },
            { MENU_CMD_DOWN_SKIP, 66, 2 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 22, 66, 44, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SelTeamForTacticsExit },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_SelectTeams _selectTeamsMenu = {
    { SelectTeamsInit, SelectTeamsReinit, 0, 43 },
    { 2, 0, 300, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_START_TEMPLATE,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_ONSELECT_FIRE, SelectLeaguesCommon },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 30 },
    {
        {
            { 1, 0, -106, 8 },
            { MENU_CMD_NAV, -1, 2, 20, 3 },
            MENU_CMD_END
        },
        {
            { -104, 0, -105, 8 },
            { MENU_CMD_NAV, 1, -1, 20, 4 },
            MENU_CMD_END
        },
        {
            { 1, 9, -106, 8 },
            { MENU_CMD_NAV, -1, 4, 1, 5 },
            MENU_CMD_END
        },
        {
            { -104, 9, -105, 8 },
            { MENU_CMD_NAV, 3, -1, 2, 6 },
            MENU_CMD_END
        },
        {
            { 1, 18, -106, 8 },
            { MENU_CMD_NAV, -1, 6, 3, 7 },
            MENU_CMD_END
        },
        {
            { -104, 18, -105, 8 },
            { MENU_CMD_NAV, 5, -1, 4, 8 },
            MENU_CMD_END
        },
        {
            { 1, 27, -106, 8 },
            { MENU_CMD_NAV, -1, 8, 5, 9 },
            MENU_CMD_END
        },
        {
            { -104, 27, -105, 8 },
            { MENU_CMD_NAV, 7, -1, 6, 10 },
            MENU_CMD_END
        },
        {
            { 1, 36, -106, 8 },
            { MENU_CMD_NAV, -1, 10, 7, 21 },
            MENU_CMD_END
        },
        {
            { -104, 36, -105, 8 },
            { MENU_CMD_NAV, 9, -1, 8, 65 },
            MENU_CMD_END
        }
    },
    { MENU_MARK_START_MENU, 0, 30 },
    {
        {
            { 67, 0, -86, 8 },
            { MENU_CMD_NAV, -1, -1, -1, 12 },
            MENU_CMD_END
        },
        {
            { 67, 9, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 67, 18, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 67, 27, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 67, 36, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 67, 45, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 67, 54, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 67, 63, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 17, 19 },
            MENU_CMD_END
        },
        {
            { 67, 72, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 18, 20 },
            MENU_CMD_END
        },
        {
            { 67, 81, -86, 8 },
            { MENU_CMD_NAV, -1, -1, 19, 43 },
            MENU_CMD_END
        }
    },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x4 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, SelectTeamsCommon },
    MENU_CMD_END,
    { 1, 21, 100, 8 },
    { MENU_CMD_NAV, -1, 43, 9, 22 },
    { MENU_CMD_LEFT_SKIP, 64, 2 },
    { MENU_CMD_RIGHT_SKIP, 43, 3 },
    MENU_CMD_END,
    { 1, 30, 100, 8 },
    { MENU_CMD_NAV, -1, 44, 21, 23 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 39, 100, 8 },
    { MENU_CMD_NAV, -1, 45, 22, 24 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 48, 100, 8 },
    { MENU_CMD_NAV, -1, 46, 23, 25 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 57, 100, 8 },
    { MENU_CMD_NAV, -1, 47, 24, 26 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 66, 100, 8 },
    { MENU_CMD_NAV, -1, 48, 25, 27 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 75, 100, 8 },
    { MENU_CMD_NAV, -1, 49, 26, 28 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 84, 100, 8 },
    { MENU_CMD_NAV, -1, 50, 27, 29 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 93, 100, 8 },
    { MENU_CMD_NAV, -1, 51, 28, 30 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 102, 100, 8 },
    { MENU_CMD_NAV, -1, 52, 29, 31 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 111, 100, 8 },
    { MENU_CMD_NAV, -1, 53, 30, 32 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, 120, 100, 8 },
    { MENU_CMD_NAV, -1, 54, 31, 33 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -127, 100, 8 },
    { MENU_CMD_NAV, -1, 55, 32, 34 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -118, 100, 8 },
    { MENU_CMD_NAV, -1, 56, 33, 35 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -109, 100, 8 },
    { MENU_CMD_NAV, -1, 57, 34, 36 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -100, 100, 8 },
    { MENU_CMD_NAV, -1, 58, 35, 37 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -91, 100, 8 },
    { MENU_CMD_NAV, -1, 59, 36, 38 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -82, 100, 8 },
    { MENU_CMD_NAV, -1, 60, 37, 39 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -73, 100, 8 },
    { MENU_CMD_NAV, -1, 61, 38, 40 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -64, 100, 8 },
    { MENU_CMD_NAV, -1, 62, 39, 41 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -55, 100, 8 },
    { MENU_CMD_NAV, -1, 63, 40, 42 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    { 1, -46, 100, 8 },
    { MENU_CMD_NAV, -1, 64, 41, 87 },
    { MENU_CMD_UP_SKIP, 42, 2 },
    MENU_CMD_END,
    {
        {
            { 102, 21, 100, 8 },
            { MENU_CMD_NAV, 21, 65, 9, 44 },
            MENU_CMD_END
        },
        {
            { 102, 30, 100, 8 },
            { MENU_CMD_NAV, 22, 66, 43, 45 },
            MENU_CMD_END
        },
        {
            { 102, 39, 100, 8 },
            { MENU_CMD_NAV, 23, 67, 44, 46 },
            MENU_CMD_END
        },
        {
            { 102, 48, 100, 8 },
            { MENU_CMD_NAV, 24, 68, 45, 47 },
            MENU_CMD_END
        },
        {
            { 102, 57, 100, 8 },
            { MENU_CMD_NAV, 25, 69, 46, 48 },
            MENU_CMD_END
        },
        {
            { 102, 66, 100, 8 },
            { MENU_CMD_NAV, 26, 70, 47, 49 },
            MENU_CMD_END
        },
        {
            { 102, 75, 100, 8 },
            { MENU_CMD_NAV, 27, 71, 48, 50 },
            MENU_CMD_END
        },
        {
            { 102, 84, 100, 8 },
            { MENU_CMD_NAV, 28, 72, 49, 51 },
            MENU_CMD_END
        },
        {
            { 102, 93, 100, 8 },
            { MENU_CMD_NAV, 29, 73, 50, 52 },
            MENU_CMD_END
        },
        {
            { 102, 102, 100, 8 },
            { MENU_CMD_NAV, 30, 74, 51, 53 },
            MENU_CMD_END
        },
        {
            { 102, 111, 100, 8 },
            { MENU_CMD_NAV, 31, 75, 52, 54 },
            MENU_CMD_END
        },
        {
            { 102, 120, 100, 8 },
            { MENU_CMD_NAV, 32, 76, 53, 55 },
            MENU_CMD_END
        },
        {
            { 102, -127, 100, 8 },
            { MENU_CMD_NAV, 33, 77, 54, 56 },
            MENU_CMD_END
        },
        {
            { 102, -118, 100, 8 },
            { MENU_CMD_NAV, 34, 78, 55, 57 },
            MENU_CMD_END
        },
        {
            { 102, -109, 100, 8 },
            { MENU_CMD_NAV, 35, 79, 56, 58 },
            MENU_CMD_END
        },
        {
            { 102, -100, 100, 8 },
            { MENU_CMD_NAV, 36, 80, 57, 59 },
            MENU_CMD_END
        },
        {
            { 102, -91, 100, 8 },
            { MENU_CMD_NAV, 37, 81, 58, 60 },
            MENU_CMD_END
        },
        {
            { 102, -82, 100, 8 },
            { MENU_CMD_NAV, 38, 82, 59, 61 },
            MENU_CMD_END
        },
        {
            { 102, -73, 100, 8 },
            { MENU_CMD_NAV, 39, 83, 60, 62 },
            MENU_CMD_END
        },
        {
            { 102, -64, 100, 8 },
            { MENU_CMD_NAV, 40, 84, 61, 63 },
            MENU_CMD_END
        },
        {
            { 102, -55, 100, 8 },
            { MENU_CMD_NAV, 41, 85, 62, 64 },
            MENU_CMD_END
        },
        {
            { 102, -46, 100, 8 },
            { MENU_CMD_NAV, 42, 86, 63, 88 },
            MENU_CMD_END
        }
    },
    { -53, 21, 100, 8 },
    { MENU_CMD_NAV, 43, -1, 10, 66 },
    { MENU_CMD_LEFT_SKIP, 64, 2 },
    { MENU_CMD_RIGHT_SKIP, 43, 3 },
    MENU_CMD_END,
    { -53, 30, 100, 8 },
    { MENU_CMD_NAV, 44, -1, 65, 67 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 39, 100, 8 },
    { MENU_CMD_NAV, 45, -1, 66, 68 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 48, 100, 8 },
    { MENU_CMD_NAV, 46, -1, 67, 69 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 57, 100, 8 },
    { MENU_CMD_NAV, 47, -1, 68, 70 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 66, 100, 8 },
    { MENU_CMD_NAV, 48, -1, 69, 71 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 75, 100, 8 },
    { MENU_CMD_NAV, 49, -1, 70, 72 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 84, 100, 8 },
    { MENU_CMD_NAV, 50, -1, 71, 73 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 93, 100, 8 },
    { MENU_CMD_NAV, 51, -1, 72, 74 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 102, 100, 8 },
    { MENU_CMD_NAV, 52, -1, 73, 75 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 111, 100, 8 },
    { MENU_CMD_NAV, 53, -1, 74, 76 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, 120, 100, 8 },
    { MENU_CMD_NAV, 54, -1, 75, 77 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -127, 100, 8 },
    { MENU_CMD_NAV, 55, -1, 76, 78 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -118, 100, 8 },
    { MENU_CMD_NAV, 56, -1, 77, 79 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -109, 100, 8 },
    { MENU_CMD_NAV, 57, -1, 78, 80 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -100, 100, 8 },
    { MENU_CMD_NAV, 58, -1, 79, 81 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -91, 100, 8 },
    { MENU_CMD_NAV, 59, -1, 80, 82 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -82, 100, 8 },
    { MENU_CMD_NAV, 60, -1, 81, 83 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -73, 100, 8 },
    { MENU_CMD_NAV, 61, -1, 82, 84 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -64, 100, 8 },
    { MENU_CMD_NAV, 62, -1, 83, 85 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -55, 100, 8 },
    { MENU_CMD_NAV, 63, -1, 84, 86 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    { -53, -46, 100, 8 },
    { MENU_CMD_NAV, 64, -1, 85, 89 },
    { MENU_CMD_DOWN_SKIP, 86, 2 },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, -71, 120, 15 },
    { MENU_CMD_NAV, -1, 88, 42, -1 },
    { MENU_CMD_UP_SKIP, 87, 2 },
    { MENU_CMD_RIGHT_SKIP, 87, 1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aViewSelectedTe },
    { MENU_CMD_ONSELECT_FIRE, ViewSelectedTeams },
    MENU_CMD_END,
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 87, 89, 64, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitSelectTeams },
        MENU_CMD_END
    },
    { -72, -71, 120, 15 },
    { MENU_CMD_NAV, 88, -1, 86, -1 },
    { MENU_CMD_DOWN_SKIP, 89, 2 },
    { MENU_CMD_RIGHT_SKIP, 89, 0 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, ChooseTeamsPlayGame },
    MENU_CMD_END,
    { -72, -68, 120, 9 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ChooseTeams _chooseTeamsMenu = {
    { ChooseTeamsInit, ChooseTeamsInit, 0, 0 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 35, 120, 25 },
        { MENU_CMD_NAV, -1, -1, -1, 1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aClubTeams },
        { MENU_CMD_ONSELECT_FIRE, ClubTeams },
        MENU_CMD_END
    },
    {
        { 92, 70, 120, 25 },
        { MENU_CMD_NAV, -1, -1, 0, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aNationalTeams },
        { MENU_CMD_ONSELECT_FIRE, NationalTeams },
        MENU_CMD_END
    },
    {
        { 92, 105, 120, 25 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aOriginalCustom },
        { MENU_CMD_ONSELECT_FIRE, OriginalCustomTeams },
        MENU_CMD_END
    },
    {
        { 92, -116, 120, 25 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aEditedCustomTe_0 },
        { MENU_CMD_ONSELECT_FIRE, EditedCustomTeams },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 3, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitChooseTeams },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_SelectFiles _selectFilesMenu = {
    { SelectFilesInit, SelectFilesOnReturn, 0, 16 },
    { 2, 0, 300, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, -1 },
    MENU_MARK_START_TEMPLATE,
    MENU_MARK_BYTE_COORDS,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x6 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, SelFilesOnSelectCommon },
    { MENU_CMD_BEFORE_DRAW, SelFilesDrawItemDescription },
    MENU_CMD_END,
    {
        { 0, 21, 56, 8 },
        { MENU_CMD_NAV, -1, 16, -1, 2 },
        MENU_CMD_END
    },
    { 0, 30, 56, 8 },
    { MENU_CMD_NAV, -1, 17, 1, 3 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 39, 56, 8 },
    { MENU_CMD_NAV, -1, 18, 2, 4 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 48, 56, 8 },
    { MENU_CMD_NAV, -1, 19, 3, 5 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 57, 56, 8 },
    { MENU_CMD_NAV, -1, 20, 4, 6 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 66, 56, 8 },
    { MENU_CMD_NAV, -1, 21, 5, 7 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 75, 56, 8 },
    { MENU_CMD_NAV, -1, 22, 6, 8 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 84, 56, 8 },
    { MENU_CMD_NAV, -1, 23, 7, 9 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 93, 56, 8 },
    { MENU_CMD_NAV, -1, 24, 8, 10 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 102, 56, 8 },
    { MENU_CMD_NAV, -1, 25, 9, 11 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 111, 56, 8 },
    { MENU_CMD_NAV, -1, 26, 10, 12 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, 120, 56, 8 },
    { MENU_CMD_NAV, -1, 27, 11, 13 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, -127, 56, 8 },
    { MENU_CMD_NAV, -1, 28, 12, 14 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, -118, 56, 8 },
    { MENU_CMD_NAV, -1, 29, 13, 15 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    { 0, -109, 56, 8 },
    { MENU_CMD_NAV, -1, 30, 14, 48 },
    { MENU_CMD_UP_SKIP, 15, 2 },
    MENU_CMD_END,
    {
        {
            { 104, 21, 56, 8 },
            { MENU_CMD_NAV, 1, 31, -1, 17 },
            MENU_CMD_END
        },
        {
            { 104, 30, 56, 8 },
            { MENU_CMD_NAV, 2, 32, 16, 18 },
            MENU_CMD_END
        },
        {
            { 104, 39, 56, 8 },
            { MENU_CMD_NAV, 3, 33, 17, 19 },
            MENU_CMD_END
        },
        {
            { 104, 48, 56, 8 },
            { MENU_CMD_NAV, 4, 34, 18, 20 },
            MENU_CMD_END
        },
        {
            { 104, 57, 56, 8 },
            { MENU_CMD_NAV, 5, 35, 19, 21 },
            MENU_CMD_END
        },
        {
            { 104, 66, 56, 8 },
            { MENU_CMD_NAV, 6, 36, 20, 22 },
            MENU_CMD_END
        },
        {
            { 104, 75, 56, 8 },
            { MENU_CMD_NAV, 7, 37, 21, 23 },
            MENU_CMD_END
        },
        {
            { 104, 84, 56, 8 },
            { MENU_CMD_NAV, 8, 38, 22, 24 },
            MENU_CMD_END
        },
        {
            { 104, 93, 56, 8 },
            { MENU_CMD_NAV, 9, 39, 23, 25 },
            MENU_CMD_END
        },
        {
            { 104, 102, 56, 8 },
            { MENU_CMD_NAV, 10, 40, 24, 26 },
            MENU_CMD_END
        },
        {
            { 104, 111, 56, 8 },
            { MENU_CMD_NAV, 11, 41, 25, 27 },
            MENU_CMD_END
        },
        {
            { 104, 120, 56, 8 },
            { MENU_CMD_NAV, 12, 42, 26, 28 },
            MENU_CMD_END
        },
        {
            { 104, -127, 56, 8 },
            { MENU_CMD_NAV, 13, 43, 27, 29 },
            MENU_CMD_END
        },
        {
            { 104, -118, 56, 8 },
            { MENU_CMD_NAV, 14, 44, 28, 30 },
            MENU_CMD_END
        },
        {
            { 104, -109, 56, 8 },
            { MENU_CMD_NAV, 15, 45, 29, 48 },
            MENU_CMD_END
        },
        {
            { -48, 21, 56, 8 },
            { MENU_CMD_NAV, 16, -1, -1, 32 },
            MENU_CMD_END
        }
    },
    { -48, 30, 56, 8 },
    { MENU_CMD_NAV, 17, -1, 31, 33 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 39, 56, 8 },
    { MENU_CMD_NAV, 18, -1, 32, 34 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 48, 56, 8 },
    { MENU_CMD_NAV, 19, -1, 33, 35 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 57, 56, 8 },
    { MENU_CMD_NAV, 20, -1, 34, 36 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 66, 56, 8 },
    { MENU_CMD_NAV, 21, -1, 35, 37 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 75, 56, 8 },
    { MENU_CMD_NAV, 22, -1, 36, 38 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 84, 56, 8 },
    { MENU_CMD_NAV, 23, -1, 37, 39 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 93, 56, 8 },
    { MENU_CMD_NAV, 24, -1, 38, 40 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 102, 56, 8 },
    { MENU_CMD_NAV, 25, -1, 39, 41 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 111, 56, 8 },
    { MENU_CMD_NAV, 26, -1, 40, 42 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, 120, 56, 8 },
    { MENU_CMD_NAV, 27, -1, 41, 43 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, -127, 56, 8 },
    { MENU_CMD_NAV, 28, -1, 42, 44 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, -118, 56, 8 },
    { MENU_CMD_NAV, 29, -1, 43, 45 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    { -48, -109, 56, 8 },
    { MENU_CMD_NAV, 30, -1, 44, 48 },
    { MENU_CMD_DOWN_SKIP, 45, 2 },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, 0, 39, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    MENU_CMD_END,
    { 111, -104, 0, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x4000, _aFilename },
    MENU_CMD_END,
    { 112, -104, 80, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, -1, -1, 30, 49 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, SelectFilesInputSaveFilenameOnSelect },
    MENU_CMD_END,
    { 121, -91, 62, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, 15, 45, 48, 50 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aSave },
    { MENU_CMD_ONSELECT_FIRE, SelectFilenameToSaveAndExit },
    MENU_CMD_END,
    {
        { 121, -71, 62, 15 },
        { MENU_CMD_NAV, 15, 45, 49, -1 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x0, _aAbort },
        { MENU_CMD_ONSELECT_FIRE, AbortSaveFile },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Continue _continueMenu = {
    { ContinueInit, 0, 0, 1 },
    { 2, 40, 300, 40 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x10, 0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 102, -71, 100, 15 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
    { MENU_CMD_ONSELECT_FIRE, ContinueMenuContinueSelected },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ContinueAbort _continueAbortMenu = {
    { ContinueAbortInit, 0, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { -104, 0, 0, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    { 52, 50, -56, 40 },
    { MENU_CMD_COLORS, 0x4 },
    { MENU_CMD_MULTILINE, 0x0, 0 },
    MENU_CMD_END,
    {
        { 102, -91, 100, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, ContinueSelected },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 2, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, AbortSelected },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
#ifndef SWOS_SDL_BUILD   /* the Amiga's; shown on AGA only (menus.c) */
/* The Amiga AGA CREDITS screen. The SDL builds run SWOS 2020's credits.mnu from JSON
 * (online_menus.c); the Amiga has no JSON menus, so this is the same screen as a native
 * one. Coordinates are bytes: 140 = -116, 200 = -56. */
extern void CreditsAgaInit(void);
extern void CreditsAgaDraw(void);
char _aCreditsAgaHeader[] = "OPENSWOS - CREDITS";
char _aCreditsAgaExit[]   = "AMIGA FOREVER!";
Menu_CreditsAga _creditsAgaMenu = {
    { CreditsAgaInit, 0, CreditsAgaDraw, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 82, 0, -116, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aCreditsAgaHeader },
        MENU_CMD_END
    },
    {
        { 82, -56, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, -1 },
        { MENU_CMD_COLORS, 0x4 },
        { MENU_CMD_TEXT_PTR, 0x0, _aCreditsAgaExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
#endif

Menu_SWOSMainMenu _swosMainMenu = {
    { SWOSMainMenuInit, GlobalRetn, 0, 6 },
    { 88, -24, 128, 76 },
    { MENU_CMD_SPRITE_FG, 224 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, 62, -106, 16 },
        { MENU_CMD_NAV, -1, 6, -1, 2 },
        { MENU_CMD_COLORS, 0x3 },
        { MENU_CMD_TEXT_PTR, 0x10, _aEdit },
        { MENU_CMD_ONSELECT_FIRE, EditMenuSelected },
        MENU_CMD_END
    },
    {
        { 0, 80, -106, 16 },
        { MENU_CMD_NAV, -1, 7, 1, 3 },
        { MENU_CMD_COLORS, 0x5 },
        { MENU_CMD_TEXT_PTR, 0x10, _aPractice },
        { MENU_CMD_ONSELECT_FIRE, PracticeMenuSelected },
        MENU_CMD_END
    },
    {
        { 0, 98, -106, 16 },
        { MENU_CMD_NAV, -1, 8, 2, 4 },
        { MENU_CMD_COLORS, 0xB },
        { MENU_CMD_TEXT_PTR, 0x10, _aHighlights_0 },
        { MENU_CMD_ONSELECT_FIRE, HighlightsMenu },
        MENU_CMD_END
    },
    {
        { 0, 116, -106, 16 },
        { MENU_CMD_NAV, -1, 9, 3, 5 },
        { MENU_CMD_COLORS, 0x8 },
        { MENU_CMD_TEXT_PTR, 0x10, _aOptions },
        { MENU_CMD_ONSELECT_FIRE, OptionsMenuSelected },
        MENU_CMD_END
    },
    {
        { 0, -122, -106, 16 },
        { MENU_CMD_NAV, -1, 10, 4, 14 },
        { MENU_CMD_COLORS, 0x3 },
        { MENU_CMD_TEXT_PTR, 0x10, _aChallenges },
        { MENU_CMD_ONSELECT_FIRE, ChallengesMenu },
        MENU_CMD_END
    },
    {
        { -102, 62, -106, 16 },
        { MENU_CMD_NAV, 1, -1, -1, 7 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x10, _aFriendly_1 },
        { MENU_CMD_ONSELECT_FIRE, FriendlyMenu },
        MENU_CMD_END
    },
    {
        { -102, 80, -106, 16 },
        { MENU_CMD_NAV, 2, -1, 6, 8 },
        { MENU_CMD_COLORS, 0x4 },
        { MENU_CMD_TEXT_PTR, 0x10, _aDiyCompetition_0 },
        { MENU_CMD_ONSELECT_FIRE, DIYCompetitionMenu },
        MENU_CMD_END
    },
    {
        { -102, 98, -106, 16 },
        { MENU_CMD_NAV, 3, -1, 7, 9 },
        { MENU_CMD_COLORS, 0x5 },
        { MENU_CMD_TEXT_PTR, 0x10, _aPresetCompetit_0 },
        { MENU_CMD_ONSELECT_FIRE, PresetCompetitionMenu },
        MENU_CMD_END
    },
    {
        { -102, 116, -106, 16 },
        { MENU_CMD_NAV, 4, -1, 8, 10 },
        { MENU_CMD_COLORS, 0x4 },
        { MENU_CMD_TEXT_PTR, 0x10, _aSeason_1 },
        { MENU_CMD_ONSELECT_FIRE, SeasonMenu },
        MENU_CMD_END
    },
    {
        { -102, -122, -106, 16 },
        { MENU_CMD_NAV, 5, -1, 9, 14 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x10, _aCareer_1 },
        { MENU_CMD_ONSELECT_FIRE, CareerMenu },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    { 27, -84, -6, 14 },
    { MENU_CMD_NAV, -1, -1, 14, 12 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, StartContest },
    MENU_CMD_END,
    {
        { 27, -67, -6, 14 },
        { MENU_CMD_NAV, -1, -1, 11, 13 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x0, _saveFileTitle },
        { MENU_CMD_ONSELECT_FIRE, SaveContest },
        MENU_CMD_END
    },
    {
        { 27, -50, -6, 14 },
        { MENU_CMD_NAV, -1, -1, 12, 15 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aLoadOldCompeti },
        { MENU_CMD_ONSELECT_FIRE, LoadOldCompetitionMenu },
        MENU_CMD_END
    },
    {
        { 27, -103, -6, 16 },
        { MENU_CMD_NAV, -1, -1, 10, 11 },
        { MENU_CMD_COLORS, 0xB },
        { MENU_CMD_TEXT_PTR, 0x10, _aMultiplayer },
        { MENU_CMD_ONSELECT_FIRE, MultiplayerMenuSelected },
        MENU_CMD_END
    },
    {
        { -12, -36, 60, 14 },
        { MENU_CMD_NAV, -1, -1, 13, -1 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _aCredits },
        { MENU_CMD_ONSELECT_FIRE, CreditsMenuSelected },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_END_MENU
};

/* ---- editMenu (EDIT) — OpenSWOS, no SWOS2.s counterpart -------------
 * The main menu's EDIT button (menus.c EditMenuSelected). EDIT TACTICS and EDIT
 * CUSTOM TEAMS had a main-menu row each until 2026-10-23; grouping them here is
 * what freed the row SHOOTING PRACTICE took. Hand-written like diskMenu below:
 * scripts/emit_menus.py splices VALUES into the braces that are already here, so
 * the shape of a new menu has to exist in C first (docs/MENUS.md section 12). */
Menu_EditMenu _editMenu = {
    { 0, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aEdit },
        MENU_CMD_END
    },
    {
        { 82, 60, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aEditTactics },
        { MENU_CMD_ONSELECT_FIRE, EditTacticsMenu },
        MENU_CMD_END
    },
    {
        { 82, 90, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aEditCustomTeam },
        { MENU_CMD_ONSELECT_FIRE, EditCustomTeamMenu },
        MENU_CMD_END
    },
    {
        { 82, 120, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aSaveDiskFiling },
        { MENU_CMD_ONSELECT_FIRE, SaveDiskFilingMenu },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 3, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};

/* ---- practiceMenu (PRACTICE) — OpenSWOS, no SWOS2.s counterpart -------------
 * The main menu's PRACTICE button (menus.c PracticeMenuSelected). SHOOTING
 * PRACTICE had that row to itself until 2026-10-26, when PENALTY SHOOTOUT
 * (src/penalty_shootout.c) joined it under this submenu. Hand-written like
 * _editMenu above: scripts/emit_menus.py splices VALUES into the braces that
 * are already here, so the shape of a new menu has to exist in C first
 * (docs/MENUS.md section 12). */
Menu_PracticeMenu _practiceMenu = {
    { 0, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aPractice },
        MENU_CMD_END
    },
    {
        { 82, 60, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aShootingPractice },
        { MENU_CMD_ONSELECT_FIRE, ShootingPracticeMenu },
        MENU_CMD_END
    },
    {
        { 82, 90, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aPenaltyShootout },
        { MENU_CMD_ONSELECT_FIRE, PenaltyShootoutMenu },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 2, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};

/* ---- shootingPracticeSetupMenu (SHOOTING PRACTICE) — OpenSWOS, no SWOS2.s
 * counterpart ----------------------------------------------------------------
 * The setup screen the PRACTICE submenu's SHOOTING PRACTICE button opens
 * (shooting_practice.c ShootingPracticeMenu): DEFENDERS OFF/ON and MIDFIELD
 * OFF/ON (string-table rows over g_shootingPracticeDefenders /
 * g_shootingPracticeMidfield, recoloured green/yellow by
 * ShootingPracticeSetupInit / ChangePracticeDefenders / ChangePracticeMidfield),
 * CONTINUE (ShootingPracticeContinue runs the friendly flow with the mode flag
 * set), EXIT. Hand-written like _practiceMenu above: scripts/emit_menus.py
 * splices VALUES into the braces that are already here, so the shape of a new
 * menu has to exist in C first (docs/MENUS.md section 12). */
Menu_ShootingPracticeSetup _shootingPracticeSetupMenu = {
    { ShootingPracticeSetupInit, ShootingPracticeSetupInit, 0, 3 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aShootingPractice },
        MENU_CMD_END
    },
    {
        { 82, 50, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_STRING_TABLE, 0x0, _practiceDefendersStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangePracticeDefenders },
        MENU_CMD_END
    },
    {
        { 82, 68, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_STRING_TABLE, 0x0, _practiceMidfieldStrTable },
        { MENU_CMD_ONSELECT_FIRE, ChangePracticeMidfield },
        MENU_CMD_END
    },
    {
        { 82, 96, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
        { MENU_CMD_ONSELECT_FIRE, ShootingPracticeContinue },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 3, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Offers _offersMenu = {
    { OffersInit, OffersInit, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aOffers },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, -1, 6, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 30 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, OffersCommon },
    { MENU_CMD_AFTER_DRAW, OffersAfterDrawCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 21 },
            { MENU_CMD_NAV, -1, -1, -1, 3 },
            MENU_CMD_END
        },
        {
            { 0, 30, 304, 21 },
            { MENU_CMD_NAV, -1, -1, 2, 4 },
            MENU_CMD_END
        },
        {
            { 0, 60, 304, 21 },
            { MENU_CMD_NAV, -1, -1, 3, 5 },
            MENU_CMD_END
        },
        {
            { 0, 90, 304, 21 },
            { MENU_CMD_NAV, -1, -1, 4, 6 },
            MENU_CMD_END
        },
        {
            { 0, 120, 304, 21 },
            { MENU_CMD_NAV, -1, -1, 5, 1 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, 0, 304, 21 },
    { MENU_CMD_COLORS, 0x0 },
    MENU_CMD_END,
    {
        {
            { 15, 0, 289, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 15, 10, 289, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, 0, 15, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_PlayerTransfer _playerTransferMenu = {
    { TransferPlayerInit, TransferPlayerInit, 0, 11 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aResponseToOffe },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 32, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 42, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 89, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    { 0, 156, 304, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
    MENU_CMD_END,
    { 0, 166, 304, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        {
            { -104, 65, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, 110, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, -126, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        }
    },
    {
        { 112, 100, 80, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    {
        {
            { 31, -71, 120, 15 },
            { MENU_CMD_NAV, -1, 11, -1, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aMakeImprovedOf },
            { MENU_CMD_ONSELECT_FIRE, TransferPlayerOnSelect10 },
            MENU_CMD_END
        },
        {
            { -104, -71, 120, 15 },
            { MENU_CMD_NAV, 10, -1, -1, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, TransferPlayerOnSelect11 },
            MENU_CMD_END
        }
    },
    { 92, -71, 120, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
    { MENU_CMD_ONSELECT_FIRE, TransferPlayerOnSelect12 },
    MENU_CMD_END,
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, TransferPlayerAfterDrawCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 55, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 75, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 120, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 140, 304, 8 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    {
        { 0, 0, 304, 8 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 272, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_ThirdPartyOffer _thirdPartyOfferMenu = {
    { ThirdPartyOfferMenuInit, ThirdPartyOfferMenuInit, 0, 13 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aThirdPartyOffe },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 32, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 42, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    { 0, 42, 304, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_NO_PTR, 0x8000 },
    MENU_CMD_END,
    {
        { 0, 89, 304, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        {
            { -104, 65, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, 110, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, -126, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        }
    },
    {
        { 112, 100, 80, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    {
        {
            { 102, -103, 100, 15 },
            { MENU_CMD_NAV, 10, 12, -1, 11 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aViewSquad },
            { MENU_CMD_ONSELECT_FIRE, ThirdPartyOfferViewSquad },
            MENU_CMD_END
        },
        {
            { 0, -87, 101, 15 },
            { MENU_CMD_NAV, -1, 11, 9, 13 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aAcceptOffer },
            { MENU_CMD_ONSELECT_FIRE, AcceptOffer },
            MENU_CMD_END
        }
    },
    { 102, -87, 100, 15 },
    { MENU_CMD_NAV, 10, 12, 9, 13 },
    { MENU_CMD_RIGHT_SKIP, 11, 0 },
    { MENU_CMD_LEFT_SKIP, 11, 0 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aAskForMore },
    { MENU_CMD_ONSELECT_FIRE, AskForMore },
    MENU_CMD_END,
    {
        {
            { -53, -87, 101, 15 },
            { MENU_CMD_NAV, 11, -1, 9, 13 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aRejectOffer },
            { MENU_CMD_ONSELECT_FIRE, RejectOffer },
            MENU_CMD_END
        },
        {
            { 102, -71, 100, 15 },
            { MENU_CMD_NAV, 10, 12, 11, -1 },
            { MENU_CMD_COLORS, 0x5 },
            { MENU_CMD_TEXT_PTR, 0x0, _aConsider },
            { MENU_CMD_ONSELECT_FIRE, ConsiderOffer },
            MENU_CMD_END
        }
    },
    { 122, -71, 60, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, ThirdPartyOfferMenuExit },
    MENU_CMD_END,
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, ThirdPartyOfferMenuAfterDrawCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 55, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 75, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 120, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 140, 304, 8 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    {
        { 0, 0, 304, 8 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 272, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_PlayerOffers _playerOffersMenu = {
    { PlayerOffersInit, PlayerOffersInit, 0, 9 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aMakeOffer },
        MENU_CMD_END
    },
    {
        { 92, 20, 120, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 0, 32, 304, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, _aDearSirMadam },
        MENU_CMD_END
    },
    {
        {
            { 0, 42, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 89, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    MENU_MARK_BYTE_COORDS,
    {
        {
            { -104, 65, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, 110, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        },
        {
            { -104, -126, 0, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            MENU_CMD_END
        }
    },
    {
        { 0, 65, 14, 8 },
        { MENU_CMD_NAV, -1, 9, -1, 9 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
        { MENU_CMD_ONSELECT_FIRE, PlayerOffersOnSelect8 },
        MENU_CMD_END
    },
    { 112, 100, 80, 8 },
    { MENU_CMD_NAV, 8, 10, 8, 11 },
    { MENU_CMD_COLORS, 0xB },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    { MENU_CMD_INPUT_FUNC, 0x3C1, PlayerOffersAdjustOfferAmount },
    MENU_CMD_END,
    {
        {
            { -54, 100, 14, 8 },
            { MENU_CMD_NAV, 9, -1, 8, 11 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            { MENU_CMD_ONSELECT_FIRE, PlayerOffersOnSelect10 },
            MENU_CMD_END
        },
        {
            { 0, -126, 14, 8 },
            { MENU_CMD_NAV, -1, -1, 9, 12 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aPlus },
            { MENU_CMD_ONSELECT_FIRE, PlayerOffersOnSelect11 },
            MENU_CMD_END
        },
        {
            { 102, -91, 100, 15 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aConfirmOffer },
            { MENU_CMD_ONSELECT_FIRE, PlayerOffersOnSelect12 },
            MENU_CMD_END
        },
        {
            { 102, -71, 100, 15 },
            { MENU_CMD_NAV, -1, -1, 12, -1 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, PlayerOffersOnSelect13 },
            MENU_CMD_END
        }
    },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, PlayerOffersAfterDrawCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 55, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 75, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 120, 304, 8 },
            MENU_CMD_END
        },
        {
            { 0, 140, 304, 8 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    {
        { 0, 0, 304, 8 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 272, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_ViewSquad _viewSquadMenu = {
    { ViewSquadInit, 0, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 57, 0, -66, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 82, 16, -116, 10 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x10, 0 },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, 3, 20, 3 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ViewSquadExitContinue },
        MENU_CMD_END
    },
    { 122, -71, 60, 15 },
    { MENU_CMD_NAV, -1, -1, 2, -1 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
    { MENU_CMD_ONSELECT_FIRE, ViewSquadExitContinue },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 27 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, ViewSquadAfterDrawCommon },
    { MENU_CMD_ONSELECT_FIRE, ViewSquadCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 8 },
            { MENU_CMD_NAV, -1, -1, -1, 5 },
            MENU_CMD_END
        },
        {
            { 0, 9, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 4, 6 },
            MENU_CMD_END
        },
        {
            { 0, 18, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 5, 7 },
            MENU_CMD_END
        },
        {
            { 0, 27, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 6, 8 },
            MENU_CMD_END
        },
        {
            { 0, 36, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 7, 9 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 8, 10 },
            MENU_CMD_END
        },
        {
            { 0, 54, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 9, 11 },
            MENU_CMD_END
        },
        {
            { 0, 63, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        },
        {
            { 0, 72, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 0, 81, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 0, 90, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 0, 101, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 0, 110, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 0, 119, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 0, 128, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 17, 19 },
            MENU_CMD_END
        },
        {
            { 0, 137, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 18, 20 },
            MENU_CMD_END
        }
    },
    { 23, 148, 281, 8 },
    { MENU_CMD_NAV, 21, -1, 19, 2 },
    { MENU_CMD_LEFT_SKIP, 22, 2 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    MENU_MARK_BYTE_COORDS,
    { 13, -108, 10, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, -1, 20, 19, 2 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, ViewSquadOnSelect21 },
    { MENU_CMD_AFTER_DRAW, ViewSquad21AfterDraw },
    MENU_CMD_END,
    {
        { 0, 0, 0, 0 },
        { MENU_CMD_INVISIBLE },
        { MENU_CMD_NAV, -1, -1, 21, -1 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_WIDE_COORDS,
    { 0, 0, 304, 8 },
    { MENU_CMD_COLORS, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 272, 0, 32, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, 0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_BuyOtherPlayers _buyOtherPlayersMenu = {
    { BuyOtherPlayersInit, BuyOtherPlayersFinish, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 67, 0, -86, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 82, 16, -116, 10 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x10, 0 },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, -1, 18, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 27 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, BuyOtherPlayersAfterDrawCommon },
    { MENU_CMD_ONSELECT_FIRE, BuyOtherPlayersCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 8 },
            { MENU_CMD_NAV, -1, -1, -1, 4 },
            MENU_CMD_END
        },
        {
            { 0, 9, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 3, 5 },
            MENU_CMD_END
        },
        {
            { 0, 18, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 4, 6 },
            MENU_CMD_END
        },
        {
            { 0, 27, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 5, 7 },
            MENU_CMD_END
        },
        {
            { 0, 36, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 6, 8 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 7, 9 },
            MENU_CMD_END
        },
        {
            { 0, 54, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 8, 10 },
            MENU_CMD_END
        },
        {
            { 0, 63, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 9, 11 },
            MENU_CMD_END
        },
        {
            { 0, 72, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        },
        {
            { 0, 81, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 0, 90, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 0, 101, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 0, 110, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 0, 119, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 0, 128, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 0, 137, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 17, 2 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, 0, 304, 8 },
    { MENU_CMD_COLORS, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 272, 0, 32, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, 0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ShowPlayerMarket _showPlayerMarketMenu = {
    { ShowPlayerMarketInit, ShowPlayerMarket, 0, 7 },
    MENU_MARK_BYTE_COORDS,
    {
        { 62, 0, -76, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 92, 20, 120, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    {
        { -104, 32, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, _aCriteria },
        MENU_CMD_END
    },
    {
        { 8, 44, 59, 10 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aType },
        MENU_CMD_END
    },
    {
        { 68, 44, 80, 10 },
        { MENU_CMD_NAV, -1, 6, -1, 7 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_STRING_TABLE, 0x0, _positionFilterStringTable },
        { MENU_CMD_ONSELECT_FIRE, PlayerMarketFilterPositionOnSelect },
        MENU_CMD_END
    },
    {
        { -98, 44, 59, 10 },
        { MENU_CMD_COLORS, 0x9 },
        { MENU_CMD_TEXT_PTR, 0x0, _aSkills },
        MENU_CMD_END
    },
    { -38, 44, 80, 10 },
    { MENU_CMD_NAV, 4, -1, -1, 7 },
    { MENU_CMD_UP_SKIP, 6, 2 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_STRING_TABLE, 0x0, _skillFilterStringTable },
    { MENU_CMD_ONSELECT_FIRE, PlayerMarketFilterAgeGroupOnSelect },
    MENU_CMD_END,
    {
        { 102, 59, 100, 10 },
        { MENU_CMD_NAV, 4, 6, 4, 10 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aList },
        { MENU_CMD_ONSELECT_FIRE, PlayerMarketScrollOnSelect },
        MENU_CMD_END
    },
    {
        { 112, -90, 80, 15 },
        { MENU_CMD_NAV, -1, -1, 19, 9 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewSquad },
        { MENU_CMD_ONSELECT_FIRE, PlayerMarketViewSquadOnSelect },
        MENU_CMD_END
    },
    {
        { 112, -71, 80, 15 },
        { MENU_CMD_NAV, -1, -1, 8, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitPlayerMarketMenu },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 73 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, PlayerMarketItemBeforeDraw },
    { MENU_CMD_ONSELECT_FIRE, PlayerMarketItemOnSelect },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 7, 11 },
            MENU_CMD_END
        },
        {
            { 0, 9, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        },
        {
            { 0, 18, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 0, 27, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 0, 36, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 0, 54, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 0, 63, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 0, 72, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 17, 19 },
            MENU_CMD_END
        },
        {
            { 0, 81, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 18, 8 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, 0, 304, 8 },
    { MENU_CMD_COLORS, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 272, 0, 32, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, 0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_BuyPlayer _buyPlayerMenu = {
    { BuyPlayerInit, BuyPlayerInit, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aBuyPlayer },
        MENU_CMD_END
    },
    {
        { 92, 32, 120, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    {
        { 57, 55, -66, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewDomesticPl },
        { MENU_CMD_ONSELECT_FIRE, ViewDomesticPlayerMarket },
        MENU_CMD_END
    },
    {
        { 57, 80, -66, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewForeignPla },
        { MENU_CMD_ONSELECT_FIRE, ViewForeignPlayerMarket },
        MENU_CMD_END
    },
    {
        { 57, 105, -66, 15 },
        { MENU_CMD_NAV, -1, -1, 3, 5 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aBuyOtherDomest },
        { MENU_CMD_ONSELECT_FIRE, BuyOtherDomesticPlayer },
        MENU_CMD_END
    },
    {
        { 57, -126, -66, 15 },
        { MENU_CMD_NAV, -1, -1, 4, 6 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aBuyOtherForeig },
        { MENU_CMD_ONSELECT_FIRE, BuyOtherForeignPlayer },
        MENU_CMD_END
    },
    {
        { 102, -101, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 5, 7 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewSquad },
        { MENU_CMD_ONSELECT_FIRE, BuyPlayerViewSquad },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, -1, 6, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, BuyPlayerExit },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_TransferMarket _transferMarketMenu = {
    { TransferMarketInit, TransferMarketFinish, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aTransferMarket },
        MENU_CMD_END
    },
    {
        { 92, 24, 120, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x10 },
        MENU_CMD_END
    },
    {
        { 102, 40, 100, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aBuyPlayer },
        { MENU_CMD_ONSELECT_FIRE, BuyPlayerMenu },
        MENU_CMD_END
    },
    {
        { 102, 65, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewOffers },
        { MENU_CMD_ONSELECT_FIRE, ViewOffersMenu },
        MENU_CMD_END
    },
    {
        { 62, 90, -76, 15 },
        { MENU_CMD_NAV, -1, -1, 3, 9 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aPutPlayerOnTra_0 },
        { MENU_CMD_ONSELECT_FIRE, PutPlayerOnMarketMenu },
        MENU_CMD_END
    },
    {
        { 0, -71, 121, 15 },
        { MENU_CMD_NAV, -1, 6, 13, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aGiveFreeTransf },
        { MENU_CMD_ONSELECT_FIRE, GiveFreeTransfer },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, 5, 7, 13, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    {
        { -73, -71, 121, 15 },
        { MENU_CMD_NAV, 6, -1, 13, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aTakeOffMarket },
        { MENU_CMD_ONSELECT_FIRE, TakeOffMarketMenu },
        MENU_CMD_END
    },
    {
        { -104, 115, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, _aPlayersCurrent },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 130 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, TransferMarketAfterDrawCommon },
    { MENU_CMD_ONSELECT_FIRE, TransferMarketCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 4, 10 },
            MENU_CMD_END
        },
        {
            { 0, 9, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 9, 11 },
            MENU_CMD_END
        },
        {
            { 0, 18, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        },
        {
            { 0, 27, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 0, 36, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 6 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { 0, 0, 304, 8 },
    { MENU_CMD_COLORS, 0x0 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    { 272, 0, 32, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, 0 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_SelectNationalTeam _selectNationalTeamMenu = {
    { SelectNationalTeamInit, SelectNationalTeamFinish, 0, 4 },
    { 57, -10, 190, 15 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x10 },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 32, 11, -16, 10 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    {
        { -104, 16, 0, 6 },
        { MENU_CMD_TEXT_PTR, 0x0, _aToReplace },
        MENU_CMD_END
    },
    {
        {
            { 0, -71, 121, 15 },
            { MENU_CMD_NAV, -1, 4, 24, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aAddPlayer },
            { MENU_CMD_ONSELECT_FIRE, NationalTeamAddPlayer },
            MENU_CMD_END
        },
        {
            { 122, -71, 60, 15 },
            { MENU_CMD_NAV, 3, 5, 24, 5 },
            { MENU_CMD_COLORS, 0xC },
            { MENU_CMD_TEXT_PTR, 0x0, _aExit },
            { MENU_CMD_ONSELECT_FIRE, SelectNationalTeamExit },
            MENU_CMD_END
        }
    },
    { 122, -71, 60, 15 },
    { MENU_CMD_NAV, 4, 6, 4, -1 },
    { MENU_CMD_RIGHT_SKIP, 5, 0 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_TEXT_PTR, 0x0, _aAbort },
    { MENU_CMD_ONSELECT_FIRE, SelectNationalTeamAbort },
    MENU_CMD_END,
    {
        { -73, -71, 121, 15 },
        { MENU_CMD_NAV, 5, -1, 24, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aRemovePlayers },
        { MENU_CMD_ONSELECT_FIRE, NationalTeamRemovePlayers },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 27 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, SelectNationalTeamAfterDrawCommon },
    { MENU_CMD_ONSELECT_FIRE, SelectNationalTeamCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        { 0, -24, 1, 8 },
        MENU_CMD_END
    },
    {
        {
            { 0, 0, 304, 8 },
            { MENU_CMD_NAV, -1, -1, -1, 9 },
            MENU_CMD_END
        },
        {
            { 0, 9, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 8, 10 },
            MENU_CMD_END
        },
        {
            { 0, 18, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 9, 11 },
            MENU_CMD_END
        },
        {
            { 0, 27, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        },
        {
            { 0, 36, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 0, 54, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 0, 63, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 0, 72, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 0, 81, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 0, 90, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 17, 19 },
            MENU_CMD_END
        },
        {
            { 0, 101, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 18, 20 },
            MENU_CMD_END
        },
        {
            { 0, 110, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 19, 21 },
            MENU_CMD_END
        },
        {
            { 0, 119, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 20, 22 },
            MENU_CMD_END
        },
        {
            { 0, 128, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 21, 23 },
            MENU_CMD_END
        },
        {
            { 0, 137, 304, 8 },
            { MENU_CMD_NAV, -1, -1, 22, 24 },
            MENU_CMD_END
        }
    },
    { 23, 148, 281, 8 },
    { MENU_CMD_NAV, 25, -1, 23, 4 },
    { MENU_CMD_LEFT_SKIP, 26, 2 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    MENU_MARK_BYTE_COORDS,
    { 13, -108, 10, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, -1, 24, 23, 4 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, SelectNationalTeamOnSelect25 },
    { MENU_CMD_AFTER_DRAW, SelectNationalTeam25AfterDraw },
    MENU_CMD_END,
    {
        { 0, 0, 0, 0 },
        { MENU_CMD_INVISIBLE },
        { MENU_CMD_NAV, -1, -1, 25, -1 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_WIDE_COORDS,
    {
        { 0, 0, 304, 8 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 2, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_SPRITE_FG, 0 },
        MENU_CMD_END
    },
    {
        { 24, 0, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { -47, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -45, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -21, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        {
            { -24, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 181 },
            MENU_CMD_END
        },
        {
            { -17, 0, 7, 7 },
            { MENU_CMD_SPRITE_FG, 182 },
            MENU_CMD_END
        },
        {
            { -45, 0, 9, 8 },
            { MENU_CMD_SPRITE_FG, 189 },
            MENU_CMD_END
        }
    },
    {
        { -13, 0, 30, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 272, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_NationalTeamAddPlayer _nationalTeamAddPlayerMenu = {
    { NationalTeamAddPlayerInit, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aAddPlayer },
        MENU_CMD_END
    },
    {
        { 57, 70, -66, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aAddInternation },
        { MENU_CMD_ONSELECT_FIRE, NationalTeamAddPlayerHomeOnSelect },
        MENU_CMD_END
    },
    {
        { 57, 95, -66, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aAddInternation_0 },
        { MENU_CMD_ONSELECT_FIRE, NationalTeamAddPlayerAbroadOnSelect },
        MENU_CMD_END
    },
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, -1, 2, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, NationalTeamAddPlayerExit },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_PlayMatch _playMatchMenu = {
    { PlayMatchMenuInit, PlayMatchMenuReinit, 0, 9 },
    { MENU_MARK_START_MENU, 0, -20 },
    MENU_MARK_BYTE_COORDS,
    { 3, 6, 16, 16 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    {
        { 82, 0, -116, 12 },
        { MENU_CMD_COLORS, 0x0 },
        { MENU_CMD_TEXT_PTR, 0x10, 0 },
        MENU_CMD_END
    },
    {
        { -104, 14, 0, 6 },
        { MENU_CMD_TEXT_NO_PTR, 0x0 },
        MENU_CMD_END
    },
    {
        {
            { 0, -44, 55, 8 },
            { MENU_CMD_TEXT_PTR, 0x8000, _aCoach_4 },
            MENU_CMD_END
        },
        {
            { 44, -44, -114, 8 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        }
    },
    {
        { 0, -99, 105, 10 },
        { MENU_CMD_NAV, -1, 8, 29, 6 },
        { MENU_CMD_COLORS, 0x8 },
        { MENU_CMD_TEXT_PTR, 0x0, _aUseMatchTeam },
        { MENU_CMD_ONSELECT_FIRE, UseMatchTeamSelect },
        MENU_CMD_END
    },
    { 0, -73, 42, 18 },
    { MENU_CMD_NAV, -1, 50, 5, 9 },
    { MENU_CMD_UP_SKIP, 6, 2 },
    { MENU_CMD_RIGHT_SKIP, 9, 3 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_MULTILINE, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, TogglePlayerCoach },
    MENU_CMD_END,
    {
        { -84, -34, 62, 15 },
        { MENU_CMD_NAV, 8, 10, 54, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewOppo },
        { MENU_CMD_ONSELECT_FIRE, ViewOpponentsSelect },
        MENU_CMD_END
    },
    { 113, -34, 52, 15 },
    { MENU_CMD_NAV, 9, 7, 6, -1 },
    { MENU_CMD_RIGHT_SKIP, 9, 0 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aExit },
    { MENU_CMD_ONSELECT_FIRE, ExitPlayMatch },
    MENU_CMD_END,
    { 0, -38, 106, 23 },
    { MENU_CMD_NAV, -1, 8, 6, -1 },
    { MENU_CMD_UP_SKIP, 9, 2 },
    { MENU_CMD_RIGHT_SKIP, 9, 1 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, 0 },
    { MENU_CMD_ONSELECT_FIRE, PlayMatchSelected },
    MENU_CMD_END,
    {
        { -21, -34, 68, 15 },
        { MENU_CMD_NAV, 7, -1, 55, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aEditTactics },
        { MENU_CMD_ONSELECT_FIRE, EditTacticsInPlayMatchMenu },
        MENU_CMD_END
    },
    { 0, 0, 0, 0 },
    { MENU_CMD_ONSELECT_FIRE, ReturnFromViewOpponent },
    MENU_CMD_END,
    {
        { 0, -84, -84, 9 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, DrawPlayerEntries },
    { MENU_CMD_ONSELECT_FIRE, OnSwapPlayer },
    MENU_CMD_END,
    {
        {
            { 23, 24, 82, 8 },
            { MENU_CMD_NAV, -1, 38, -1, 14 },
            MENU_CMD_END
        },
        {
            { 23, 33, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 13, 15 },
            MENU_CMD_END
        },
        {
            { 23, 42, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 14, 16 },
            MENU_CMD_END
        },
        {
            { 23, 51, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 15, 17 },
            MENU_CMD_END
        },
        {
            { 23, 60, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 16, 18 },
            MENU_CMD_END
        },
        {
            { 23, 69, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 17, 19 },
            MENU_CMD_END
        },
        {
            { 23, 78, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 18, 20 },
            MENU_CMD_END
        },
        {
            { 23, 87, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 19, 21 },
            MENU_CMD_END
        },
        {
            { 23, 96, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 20, 22 },
            MENU_CMD_END
        },
        {
            { 23, 105, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 21, 23 },
            MENU_CMD_END
        },
        {
            { 23, 114, 82, 8 },
            { MENU_CMD_NAV, -1, 38, 22, 24 },
            MENU_CMD_END
        }
    },
    { 23, 125, 82, 8 },
    { MENU_CMD_NAV, -1, 38, 23, 25 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    MENU_CMD_END,
    { 23, -122, 82, 8 },
    { MENU_CMD_NAV, -1, 38, 24, 26 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    MENU_CMD_END,
    { 23, -113, 82, 8 },
    { MENU_CMD_NAV, -1, 38, 25, 27 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    MENU_CMD_END,
    { 23, -104, 82, 8 },
    { MENU_CMD_NAV, -1, 38, 26, 28 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    MENU_CMD_END,
    { 23, -95, 82, 8 },
    { MENU_CMD_NAV, -1, 41, 27, 29 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    MENU_CMD_END,
    { 23, -84, 82, 8 },
    { MENU_CMD_NAV, 30, 44, 28, 5 },
    { MENU_CMD_UP_SKIP, 29, 2 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { 0, -84, 12, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, -1, 29, 28, 5 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, PlayMatchOnSelect30 },
    { MENU_CMD_AFTER_DRAW, PlayMatch30AfterDraw },
    MENU_CMD_END,
    { 0, 0, 12, 8 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 23, 0, 82, 8 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { 106, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -127, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        { -117, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    { 0, 0, 12, 8 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 172, 131 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_ONSELECT_FIRE, PlayMatchChangeTactics },
    MENU_CMD_END,
    { 0, 0, 43, 9 },
    { MENU_CMD_NAV, 27, 39, -1, 41 },
    { MENU_CMD_TEXT_PTR, 0x0, _a442 },
    MENU_CMD_END,
    { 44, 0, 43, 9 },
    { MENU_CMD_NAV, 38, 40, -1, 42 },
    { MENU_CMD_TEXT_PTR, 0x0, _a541 },
    MENU_CMD_END,
    { 88, 0, 43, 9 },
    { MENU_CMD_NAV, 39, -1, -1, 43 },
    { MENU_CMD_TEXT_PTR, 0x0, _a451 },
    MENU_CMD_END,
    { 0, 10, 43, 9 },
    { MENU_CMD_NAV, 27, 42, 38, 44 },
    { MENU_CMD_TEXT_PTR, 0x0, _a532 },
    MENU_CMD_END,
    { 44, 10, 43, 9 },
    { MENU_CMD_NAV, 41, 43, 39, 45 },
    { MENU_CMD_TEXT_PTR, 0x0, _a352 },
    MENU_CMD_END,
    { 88, 10, 43, 9 },
    { MENU_CMD_NAV, 42, -1, 40, 46 },
    { MENU_CMD_TEXT_PTR, 0x0, _a433 },
    MENU_CMD_END,
    { 0, 20, 43, 9 },
    { MENU_CMD_NAV, 28, 45, 41, 47 },
    { MENU_CMD_TEXT_PTR, 0x0, _a424 },
    MENU_CMD_END,
    { 44, 20, 43, 9 },
    { MENU_CMD_NAV, 44, 46, 42, 48 },
    { MENU_CMD_TEXT_PTR, 0x0, _a343 },
    MENU_CMD_END,
    { 88, 20, 43, 9 },
    { MENU_CMD_NAV, 45, -1, 43, 49 },
    { MENU_CMD_TEXT_PTR, 0x0, _aSweep },
    MENU_CMD_END,
    { 0, 30, 43, 9 },
    { MENU_CMD_NAV, 29, 48, 44, 50 },
    { MENU_CMD_TEXT_PTR, 0x0, _a523 },
    MENU_CMD_END,
    { 44, 30, 43, 9 },
    { MENU_CMD_NAV, 47, 49, 45, 50 },
    { MENU_CMD_TEXT_PTR, 0x0, _aAttack },
    MENU_CMD_END,
    { 88, 30, 43, 9 },
    { MENU_CMD_NAV, 48, -1, 46, 51 },
    { MENU_CMD_TEXT_PTR, 0x0, _aDefend },
    MENU_CMD_END,
    { 0, 40, 65, 9 },
    { MENU_CMD_NAV, 6, 51, 47, 52 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERA },
    MENU_CMD_END,
    { 66, 40, 65, 9 },
    { MENU_CMD_NAV, 50, -1, 49, 53 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERB },
    MENU_CMD_END,
    { 0, 50, 65, 9 },
    { MENU_CMD_NAV, 6, 53, 50, 54 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERC },
    MENU_CMD_END,
    { 66, 50, 65, 9 },
    { MENU_CMD_NAV, 52, -1, 51, 55 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERD },
    MENU_CMD_END,
    { 0, 60, 65, 9 },
    { MENU_CMD_NAV, 6, 55, 52, 7 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERE },
    MENU_CMD_END,
    { 66, 60, 65, 9 },
    { MENU_CMD_NAV, 54, -1, 53, 10 },
    { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERF },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 172, 4 },
    {
        { 0, 0, -124, 125 },
        { MENU_CMD_SPRITE_FG, 225 },
        MENU_CMD_END
    },
    {
        {
            { 66, 12, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x0 },
            MENU_CMD_END
        },
        {
            { 3, 25, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 63, 35, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4000 },
            MENU_CMD_END
        },
        {
            { 69, 35, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -127, 25, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4000 },
            MENU_CMD_END
        },
        {
            { 3, 55, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 63, 65, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4000 },
            MENU_CMD_END
        },
        {
            { 69, 65, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { -127, 55, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4000 },
            MENU_CMD_END
        },
        {
            { 63, 85, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x4000 },
            MENU_CMD_END
        },
        {
            { 69, 85, 0, 6 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    {
        { 63, -50, 68, 11 },
        { MENU_CMD_NAV, 7, -1, 10, -1 },
        { MENU_CMD_COLORS, 0xD },
        { MENU_CMD_TEXT_PTR, 0x0, _aUniform },
        { MENU_CMD_ONSELECT_FIRE, UniformSelected },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_JobOffers _jobOffersMenu = {
    { JobOffersInit, JobOffersFinish, 0, 12 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aJobOffers },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 22 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, JobOffersCommon },
    { MENU_CMD_AFTER_DRAW, JobOffersAfterDrawCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 12 },
            { MENU_CMD_NAV, -1, -1, -1, 2 },
            MENU_CMD_END
        },
        {
            { 0, 15, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 1, 3 },
            MENU_CMD_END
        },
        {
            { 0, 30, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 2, 4 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 3, 5 },
            MENU_CMD_END
        },
        {
            { 0, 60, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 4, 7 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_BYTE_COORDS,
    {
        { -104, 94, 0, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, _aJobOffersBeing },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 109 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, JobOffers_DrawOldOfferLine },
    { MENU_CMD_ONSELECT_FIRE, OldJobOffersCommon },
    MENU_CMD_END,
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 0, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 5, 8 },
            MENU_CMD_END
        },
        {
            { 0, 15, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 7, 9 },
            MENU_CMD_END
        },
        {
            { 0, 30, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 8, 10 },
            MENU_CMD_END
        },
        {
            { 0, 45, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 9, 11 },
            MENU_CMD_END
        },
        {
            { 0, 60, 304, 12 },
            { MENU_CMD_NAV, -1, -1, 10, 12 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_BYTE_COORDS,
    {
        { 122, -71, 60, 15 },
        { MENU_CMD_NAV, -1, -1, 11, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, JobOffersExit },
        MENU_CMD_END
    },
    {
        { 0, 0, 15, 12 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 15, 0, 289, 12 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, 0, 15, 12 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        { 15, 0, 289, 12 },
        { MENU_CMD_TEXT_NO_PTR, 0x8000 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_GotJobOffer _gotJobOfferMenu = {
    { GotJobOfferInit, GotJobOfferFinish, 0, 10 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aJobOffer },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 42, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 57, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 67, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 82, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 92, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 102, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 117, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, -71, 57, 15 },
        { MENU_CMD_NAV, -1, 9, -1, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aReject },
        { MENU_CMD_ONSELECT_FIRE, RejectJobOffer },
        MENU_CMD_END
    },
    {
        { 58, -71, 64, 15 },
        { MENU_CMD_NAV, 8, 10, -1, -1 },
        { MENU_CMD_COLORS, 0x5 },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewLeague },
        { MENU_CMD_ONSELECT_FIRE, ViewFutureTeamLeague },
        MENU_CMD_END
    },
    {
        { 123, -71, 58, 15 },
        { MENU_CMD_NAV, 9, 11, -1, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aConsider },
        { MENU_CMD_ONSELECT_FIRE, ConsiderJobOffer },
        MENU_CMD_END
    },
    {
        { 123, -71, 58, 15 },
        { MENU_CMD_NAV, 10, 12, -1, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    {
        { -74, -71, 64, 15 },
        { MENU_CMD_NAV, 11, 13, -1, -1 },
        { MENU_CMD_COLORS, 0x5 },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewTeam },
        { MENU_CMD_ONSELECT_FIRE, ViewFutureTeam },
        MENU_CMD_END
    },
    {
        { -9, -71, 57, 15 },
        { MENU_CMD_NAV, 12, -1, -1, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aAccept },
        { MENU_CMD_ONSELECT_FIRE, AcceptJobOffer },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_ChairmanMemos _chairmanMemosMenu = {
    { ChairmanMemosInit, ChairmanMemosOnReturn, 0, 6 },
    MENU_MARK_BYTE_COORDS,
    {
        { 52, 0, -56, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aMemoFromTheClu },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        {
            { 0, 40, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 60, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        },
        {
            { 0, 70, 304, 8 },
            { MENU_CMD_TEXT_NO_PTR, 0x8000 },
            MENU_CMD_END
        }
    },
    { 0, 100, 304, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, _aBestRegards },
    MENU_CMD_END,
    { 0, 115, 304, 8 },
    { MENU_CMD_TEXT_PTR, 0x8000, _aYourChairman },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 102, -71, 100, 15 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
    { MENU_CMD_ONSELECT_FIRE, ChairmanMemosContinue },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ClubBusiness _clubBusinessMenu = {
    { ClubBusinessInit, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aBankStatement },
        MENU_CMD_END
    },
    { 102, -71, 100, 15 },
    { MENU_CMD_COLORS, 0xC },
    { MENU_CMD_TEXT_PTR, 0x0, _aContinue },
    { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, 0, 42 },
    MENU_MARK_WIDE_COORDS,
    { 0, -5, 229, 8 },
    { MENU_CMD_TEXT_PTR, 0x4000, _aOldBalance },
    MENU_CMD_END,
    MENU_MARK_BYTE_COORDS,
    { 0, 20, -27, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x4000, _aCompetitionBon },
    MENU_CMD_END,
    {
        { 0, 15, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aGateReceipts },
        MENU_CMD_END
    },
    {
        { 0, 30, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aGeneralRunning },
        MENU_CMD_END
    },
    { 0, 45, -27, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_PTR, 0x4000, _aAdditionalInve_0 },
    MENU_CMD_END,
    {
        { 0, 45, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aPlayerSales },
        MENU_CMD_END
    },
    {
        { 0, 60, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aPlayerPurchase },
        MENU_CMD_END
    },
    {
        { 0, 75, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aPlayerWagesBil },
        MENU_CMD_END
    },
    {
        { 0, 95, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aTotalProfit },
        MENU_CMD_END
    },
    {
        { 0, 115, -27, 8 },
        { MENU_CMD_TEXT_PTR, 0x4000, _aNewBalance },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 42 },
    MENU_MARK_WIDE_COORDS,
    {
        { 229, -5, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    MENU_MARK_BYTE_COORDS,
    { -27, 20, 75, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
    MENU_CMD_END,
    {
        { -27, 15, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -27, 30, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    { -27, 45, 75, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
    MENU_CMD_END,
    {
        { -27, 45, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -27, 60, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -27, 75, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    {
        { -27, 95, 75, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0x4000 },
        MENU_CMD_END
    },
    { -22, 114, 70, 10 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_TEXT_NO_PTR, 0x4000 },
    MENU_CMD_END,
    MENU_MARK_END_MENU
};
Menu_ChooseTactics _chooseTacticsMenu = {
    { ChooseTacticsInit, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aEditTactics },
        MENU_CMD_END
    },
    {
        { 82, 40, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_A_Tactics },
        MENU_CMD_END
    },
    {
        { 82, 60, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_B_Tactics },
        MENU_CMD_END
    },
    {
        { 82, 80, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_C_Tactics },
        MENU_CMD_END
    },
    {
        { 82, 100, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 3, 5 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_D_Tactics },
        MENU_CMD_END
    },
    {
        { 82, 120, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 4, 6 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_E_Tactics },
        MENU_CMD_END
    },
    {
        { 82, -116, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 5, 7 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        { MENU_CMD_ONSELECT_FIRE, User_F_Tactics },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 6, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitChooseTacticsMenu },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_EditTactics _editTacticsMenu = {
    { EditTacticsInit, EditTacticsOnRestore, 0, 8 },
    MENU_MARK_BYTE_COORDS,
    { -121, 0, 13, 16 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    { -102, 0, -106, 12 },
    { MENU_CMD_COLORS, 0x17 },
    { MENU_CMD_TEXT_NO_PTR, 0x0 },
    MENU_CMD_END,
    { 0, 0, 0, 0 },
    { MENU_CMD_INPUT_FUNC, 0xFFFF, EditTacicsMovePlayersOrBall },
    { MENU_CMD_AFTER_DRAW, EditTactics2AfterDraw },
    MENU_CMD_END,
    { 0, 0, 0, 0 },
    { MENU_CMD_INPUT_FUNC, 0xFFFF, CopyTactics },
    { MENU_CMD_AFTER_DRAW, EditTactics2AfterDraw },
    MENU_CMD_END,
    {
        {
            { -125, -75, 57, 9 },
            { MENU_CMD_NAV, 2, 5, 27, 7 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aCopy },
            { MENU_CMD_ONSELECT_FIRE, SelectCopyTactics },
            MENU_CMD_END
        },
        {
            { -67, -75, 57, 9 },
            { MENU_CMD_NAV, 4, 6, 27, 8 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aUndo },
            { MENU_CMD_ONSELECT_FIRE, UndoTactics },
            MENU_CMD_END
        }
    },
    { -9, -75, 57, 9 },
    { MENU_CMD_NAV, 5, -1, 27, 9 },
    { MENU_CMD_COLORS, 0xD },
    { MENU_CMD_STRING_TABLE, 0x0, _flipStrTable },
    { MENU_CMD_ONSELECT_FIRE, FlipOnOff },
    MENU_CMD_END,
    {
        {
            { -125, -65, 57, 9 },
            { MENU_CMD_NAV, 2, 8, 4, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aImport },
            { MENU_CMD_ONSELECT_FIRE, ImportTactics },
            MENU_CMD_END
        },
        {
            { -67, -65, 57, 9 },
            { MENU_CMD_NAV, 7, 9, 5, -1 },
            { MENU_CMD_COLORS, 0xE },
            { MENU_CMD_TEXT_PTR, 0x0, _aSaveExit },
            { MENU_CMD_ONSELECT_FIRE, SaveAndExitEditTactics },
            MENU_CMD_END
        },
        {
            { -9, -65, 57, 9 },
            { MENU_CMD_NAV, 8, -1, 6, -1 },
            { MENU_CMD_COLORS, 0xA },
            { MENU_CMD_TEXT_PTR, 0x0, _aAbort },
            { MENU_CMD_ONSELECT_FIRE, AbortEditTactics },
            MENU_CMD_END
        }
    },
    { MENU_MARK_START_MENU, 131, 18 },
    {
        { 0, -108, -83, 9 },
        { MENU_CMD_COLORS, 0x0 },
        MENU_CMD_END
    },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, EditTacticsAfterDrawPlayerNames },
    { MENU_CMD_ONSELECT_FIRE, EditTacticsPlayerNamesCommon },
    MENU_CMD_END,
    {
        {
            { 23, 0, 82, 8 },
            { MENU_CMD_NAV, 2, -1, -1, 12 },
            MENU_CMD_END
        },
        {
            { 23, 9, 82, 8 },
            { MENU_CMD_NAV, 29, -1, 11, 13 },
            MENU_CMD_END
        },
        {
            { 23, 18, 82, 8 },
            { MENU_CMD_NAV, 30, -1, 12, 14 },
            MENU_CMD_END
        },
        {
            { 23, 27, 82, 8 },
            { MENU_CMD_NAV, 31, -1, 13, 15 },
            MENU_CMD_END
        },
        {
            { 23, 36, 82, 8 },
            { MENU_CMD_NAV, 32, -1, 14, 16 },
            MENU_CMD_END
        },
        {
            { 23, 45, 82, 8 },
            { MENU_CMD_NAV, 33, -1, 15, 17 },
            MENU_CMD_END
        },
        {
            { 23, 54, 82, 8 },
            { MENU_CMD_NAV, 34, -1, 16, 18 },
            MENU_CMD_END
        },
        {
            { 23, 63, 82, 8 },
            { MENU_CMD_NAV, 35, -1, 17, 19 },
            MENU_CMD_END
        },
        {
            { 23, 72, 82, 8 },
            { MENU_CMD_NAV, 36, -1, 18, 20 },
            MENU_CMD_END
        },
        {
            { 23, 81, 82, 8 },
            { MENU_CMD_NAV, 37, -1, 19, 21 },
            MENU_CMD_END
        },
        {
            { 23, 90, 82, 8 },
            { MENU_CMD_NAV, 38, -1, 20, 22 },
            MENU_CMD_END
        },
        {
            { 23, 101, 82, 8 },
            { MENU_CMD_NAV, 2, -1, 21, 23 },
            MENU_CMD_END
        },
        {
            { 23, 110, 82, 8 },
            { MENU_CMD_NAV, 2, -1, 22, 24 },
            MENU_CMD_END
        },
        {
            { 23, 119, 82, 8 },
            { MENU_CMD_NAV, 2, -1, 23, 25 },
            MENU_CMD_END
        },
        {
            { 23, -128, 82, 8 },
            { MENU_CMD_NAV, 2, -1, 24, 26 },
            MENU_CMD_END
        }
    },
    { 23, -119, 82, 8 },
    { MENU_CMD_NAV, 2, -1, 25, 27 },
    { MENU_CMD_UP_SKIP, 27, 2 },
    MENU_CMD_END,
    { 23, -108, 82, 8 },
    { MENU_CMD_NAV, 28, -1, 26, 5 },
    { MENU_CMD_INVISIBLE },
    MENU_CMD_END,
    { 0, -108, 12, 8 },
    { MENU_CMD_INVISIBLE },
    { MENU_CMD_NAV, 2, 27, 38, 4 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_ONSELECT_FIRE, EditTacticsOnSelect28 },
    { MENU_CMD_AFTER_DRAW, EditTactics28AfterDraw },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0x0 },
    { MENU_CMD_AFTER_DRAW, EditTacticsAfterDrawPlayerFace },
    { MENU_CMD_ONSELECT_FIRE, EditTacticsCommon },
    MENU_CMD_END,
    {
        {
            { 0, 9, 12, 8 },
            { MENU_CMD_NAV, 2, 12, -1, 30 },
            MENU_CMD_END
        },
        {
            { 0, 18, 12, 8 },
            { MENU_CMD_NAV, 2, 13, 29, 31 },
            MENU_CMD_END
        },
        {
            { 0, 27, 12, 8 },
            { MENU_CMD_NAV, 2, 14, 30, 32 },
            MENU_CMD_END
        },
        {
            { 0, 36, 12, 8 },
            { MENU_CMD_NAV, 2, 15, 31, 33 },
            MENU_CMD_END
        },
        {
            { 0, 45, 12, 8 },
            { MENU_CMD_NAV, 2, 16, 32, 34 },
            MENU_CMD_END
        },
        {
            { 0, 54, 12, 8 },
            { MENU_CMD_NAV, 2, 17, 33, 35 },
            MENU_CMD_END
        },
        {
            { 0, 63, 12, 8 },
            { MENU_CMD_NAV, 2, 18, 34, 36 },
            MENU_CMD_END
        },
        {
            { 0, 72, 12, 8 },
            { MENU_CMD_NAV, 2, 19, 35, 37 },
            MENU_CMD_END
        },
        {
            { 0, 81, 12, 8 },
            { MENU_CMD_NAV, 2, 20, 36, 38 },
            MENU_CMD_END
        },
        {
            { 0, 90, 12, 8 },
            { MENU_CMD_NAV, 2, 21, 37, 28 },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 0 },
    { MENU_MARK_START_MENU, 131, 0 },
    { 0, 0, 12, 8 },
    { MENU_CMD_COLORS, 0xA },
    { MENU_CMD_SPRITE_FG, 0 },
    MENU_CMD_END,
    {
        { 13, 0, 10, 8 },
        { MENU_CMD_NUMBER, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { 23, 0, 82, 8 },
        { MENU_CMD_COLORS, 0xA },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    {
        { 106, 0, 14, 8 },
        { MENU_CMD_TEXT_PTR, 0x0, 0 },
        MENU_CMD_END
    },
    {
        { -127, 0, 0, 8 },
        { MENU_CMD_TEXT_NO_PTR, 0xF },
        MENU_CMD_END
    },
    {
        { -117, 0, 32, 8 },
        { MENU_CMD_TEXT_PTR, 0x8000, 0 },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    MENU_MARK_END_MENU
};
Menu_ImportTactics _importTacticsMenu = {
    { ImportTacticsInit, 0, 0, 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aImportTactics },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 85, 60 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_COLORS, 0xE },
    { MENU_CMD_ONSELECT_FIRE, ImportTacticsSelectTactics },
    MENU_CMD_END,
    {
        {
            { 0, 0, 43, 9 },
            { MENU_CMD_NAV, -1, 2, -1, 4 },
            { MENU_CMD_TEXT_PTR, 0x0, _a442 },
            MENU_CMD_END
        },
        {
            { 45, 0, 43, 9 },
            { MENU_CMD_NAV, 1, 3, -1, 5 },
            { MENU_CMD_TEXT_PTR, 0x0, _a541 },
            MENU_CMD_END
        },
        {
            { 90, 0, 43, 9 },
            { MENU_CMD_NAV, 2, -1, -1, 6 },
            { MENU_CMD_TEXT_PTR, 0x0, _a451 },
            MENU_CMD_END
        },
        {
            { 0, 11, 43, 9 },
            { MENU_CMD_NAV, -1, 5, 1, 7 },
            { MENU_CMD_TEXT_PTR, 0x0, _a532 },
            MENU_CMD_END
        },
        {
            { 45, 11, 43, 9 },
            { MENU_CMD_NAV, 4, 6, 2, 8 },
            { MENU_CMD_TEXT_PTR, 0x0, _a352 },
            MENU_CMD_END
        },
        {
            { 90, 11, 43, 9 },
            { MENU_CMD_NAV, 5, -1, 3, 9 },
            { MENU_CMD_TEXT_PTR, 0x0, _a433 },
            MENU_CMD_END
        },
        {
            { 0, 22, 43, 9 },
            { MENU_CMD_NAV, -1, 8, 4, 10 },
            { MENU_CMD_TEXT_PTR, 0x0, _a424 },
            MENU_CMD_END
        },
        {
            { 45, 22, 43, 9 },
            { MENU_CMD_NAV, 7, 9, 5, 11 },
            { MENU_CMD_TEXT_PTR, 0x0, _a343 },
            MENU_CMD_END
        },
        {
            { 90, 22, 43, 9 },
            { MENU_CMD_NAV, 8, -1, 6, 12 },
            { MENU_CMD_TEXT_PTR, 0x0, _aSweep },
            MENU_CMD_END
        },
        {
            { 0, 33, 43, 9 },
            { MENU_CMD_NAV, -1, 11, 7, 13 },
            { MENU_CMD_TEXT_PTR, 0x0, _a523 },
            MENU_CMD_END
        },
        {
            { 45, 33, 43, 9 },
            { MENU_CMD_NAV, 10, 12, 8, 13 },
            { MENU_CMD_TEXT_PTR, 0x0, _aAttack },
            MENU_CMD_END
        },
        {
            { 90, 33, 43, 9 },
            { MENU_CMD_NAV, 11, -1, 9, 14 },
            { MENU_CMD_TEXT_PTR, 0x0, _aDefend },
            MENU_CMD_END
        },
        {
            { 0, 44, 65, 9 },
            { MENU_CMD_NAV, -1, 14, 10, 15 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERA },
            MENU_CMD_END
        },
        {
            { 67, 44, 66, 9 },
            { MENU_CMD_NAV, 13, -1, 12, 16 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERB },
            MENU_CMD_END
        },
        {
            { 0, 55, 65, 9 },
            { MENU_CMD_NAV, -1, 16, 13, 17 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERC },
            MENU_CMD_END
        },
        {
            { 67, 55, 66, 9 },
            { MENU_CMD_NAV, 15, -1, 14, 18 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERD },
            MENU_CMD_END
        },
        {
            { 0, 66, 65, 9 },
            { MENU_CMD_NAV, -1, 18, 15, 19 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERE },
            MENU_CMD_END
        },
        {
            { 67, 66, 66, 9 },
            { MENU_CMD_NAV, 17, -1, 16, 19 },
            { MENU_CMD_TEXT_PTR, 0x0, _teamTacticsUSERF },
            MENU_CMD_END
        }
    },
    MENU_MARK_INIT_ENTRY,
    {
        { 0, 78, -124, 9 },
        { MENU_CMD_NAV, -1, -1, 17, 20 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aLoad },
        { MENU_CMD_ONSELECT_FIRE, LoadTactics },
        MENU_CMD_END
    },
    { MENU_MARK_START_MENU, 0, 0 },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 19, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitImportTactics },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Stadium _stadiumMenu = {
    { ShowStadiumInit, 0, 0, 59 },
    { MENU_MARK_START_MENU, 0, -15 },
    MENU_MARK_BYTE_COORDS,
    {
        {
            { -104, 0, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x10, 0 },
            MENU_CMD_END
        },
        {
            { -104, 12, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x10, 0 },
            MENU_CMD_END
        }
    },
    { 0, 28, -119, 12 },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_NO_PTR, 0x4010 },
    MENU_CMD_END,
    {
        { -114, 28, 20, 12 },
        { MENU_CMD_TEXT_PTR, 0x10, _aV },
        MENU_CMD_END
    },
    { -89, 28, -119, 12 },
    { MENU_CMD_COLORS, 0x5 },
    { MENU_CMD_TEXT_NO_PTR, 0x8010 },
    MENU_CMD_END,
    { MENU_MARK_START_MENU, -1, 35 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_BEFORE_DRAW, StadiumBeforeDrawLeftTeam },
    MENU_CMD_END,
    { -114, 0, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 13, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 26, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 39, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 52, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 65, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 78, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 91, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 104, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, 117, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -114, -126, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 30 },
    {
        {
            { -124, 0, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 13, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 26, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 39, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 52, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 65, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 78, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 91, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 104, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, 117, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, -126, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4000, 0 },
            MENU_CMD_END
        },
        {
            { -124, -111, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4001, 0 },
            MENU_CMD_END
        },
        {
            { -124, -102, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4001, 0 },
            MENU_CMD_END
        },
        {
            { -124, -93, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4001, 0 },
            MENU_CMD_END
        },
        {
            { -124, -84, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4001, 0 },
            MENU_CMD_END
        },
        {
            { -124, -75, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x4001, 0 },
            MENU_CMD_END
        }
    },
    { MENU_MARK_START_MENU, 0, 35 },
    MENU_MARK_START_TEMPLATE,
    { 0, 0, 0, 0 },
    { MENU_CMD_BEFORE_DRAW, StadiumBeforeDrawRightTeam },
    MENU_CMD_END,
    { -94, 0, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 13, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 26, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 39, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 52, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 65, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 78, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 91, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 104, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, 117, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    { -94, -126, 0, 0 },
    { MENU_CMD_SPRITE_COPY, &_stadiumMenuLittlePlayerSpriteCopy },
    MENU_CMD_END,
    MENU_MARK_INIT_ENTRY,
    { MENU_MARK_START_MENU, 0, 30 },
    {
        {
            { -84, 0, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 13, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 26, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 39, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 52, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 65, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 78, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 91, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 104, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, 117, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, -126, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8000, 0 },
            MENU_CMD_END
        },
        {
            { -84, -111, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8001, 0 },
            MENU_CMD_END
        },
        {
            { -84, -102, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8001, 0 },
            MENU_CMD_END
        },
        {
            { -84, -93, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8001, 0 },
            MENU_CMD_END
        },
        {
            { -84, -84, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8001, 0 },
            MENU_CMD_END
        },
        {
            { -84, -75, 0, 10 },
            { MENU_CMD_TEXT_PTR, 0x8001, 0 },
            MENU_CMD_END
        }
    },
    {
        { 0, 0, 0, 0 },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};
Menu_Highlights _highlightsMenu = {
    { HighlightsMenuInit, HighlightsMenuOnReturn, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, _aHighlights },
        MENU_CMD_END
    },
    {
        { 82, 60, -116, 15 },
        { MENU_CMD_NAV, -1, -1, -1, 2 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aViewHighlights },
        { MENU_CMD_ONSELECT_FIRE, JmpShowHighlights },
        MENU_CMD_END
    },
    {
        { 82, 90, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 1, 3 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aLoadHighlights },
        { MENU_CMD_ONSELECT_FIRE, LoadHighlightsMenu },
        MENU_CMD_END
    },
    {
        { 82, 120, -116, 15 },
        { MENU_CMD_NAV, -1, -1, 2, 4 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, _aSaveHighlights },
        { MENU_CMD_ONSELECT_FIRE, SaveHighlightsMenu },
        MENU_CMD_END
    },
    {
        { 102, -71, 100, 15 },
        { MENU_CMD_NAV, -1, -1, 3, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, _aExit },
        { MENU_CMD_ONSELECT_FIRE, ExitHighlights },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};

/* ---- diskMenu (SAVE DISK FILING) — SWOS2.s line 94175 ----------------
 * Hand-defined here because the auto-extractor mis-classified diskMenu
 * as a uint32_t scalar (= the SWOS2.s file offset 1312736) instead of a
 * packed-menu blob. The previous stub was 4 bytes; UnpackMenu walked
 * off the end and SIGSEGV'd. */
extern void DiskMenuRet(void);
extern void FormatSaveDiskMenu(void);
extern void DeleteFileMenu(void);
extern void ExitDisk(void);
extern char _aSaveDiskFiling[];
extern char _aFormatSaveDisk[];
extern char _aDeleteFile[];
extern char _aExit[];

#pragma pack(push, 1)
struct {
    MenuHeader hdr;
    MenuMark   markByteCoords;
    MenuEntry_Byte_Colors_TextPtr               entry0;  /* SAVE DISK FILING title */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry1;  /* FORMAT SAVE DISK */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry2;  /* DELETE FILE */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry3;  /* EXIT */
    MenuMark   markEndMenu;
} _diskMenu = {
    { DiskMenuRet, DiskMenuRet, 0, 3 },
    MENU_MARK_BYTE_COORDS,
    { { 0x5C, 0x00,  0x78, 0x0F },
      { MENU_CMD_COLORS, 0x17 },
      { MENU_CMD_TEXT_PTR, 0x0, _aSaveDiskFiling },
      MENU_CMD_END },
    { { 0x52, 0x3C, -116, 0x0F },
      { MENU_CMD_NAV, -1, -1, -1, 2 },
      { MENU_CMD_COLORS, 0xE },
      { MENU_CMD_TEXT_PTR, 0x0, _aFormatSaveDisk },
      { MENU_CMD_ONSELECT_FIRE, FormatSaveDiskMenu },
      MENU_CMD_END },
    { { 0x52, 0x50, -116, 0x0F },
      { MENU_CMD_NAV, -1, -1, 1, 3 },
      { MENU_CMD_COLORS, 0xE },
      { MENU_CMD_TEXT_PTR, 0x0, _aDeleteFile },
      { MENU_CMD_ONSELECT_FIRE, DeleteFileMenu },
      MENU_CMD_END },
    { { 0x66, -71,  0x64, 0x0F },
      { MENU_CMD_NAV, -1, -1, 2, -1 },
      { MENU_CMD_COLORS, 0xC },
      { MENU_CMD_TEXT_PTR, 0x0, _aExit },
      { MENU_CMD_ONSELECT_FIRE, ExitDisk },
      MENU_CMD_END },
    MENU_MARK_END_MENU,
};
#pragma pack(pop)

/* ---- diyCompetitionMenu (DIY COMPETITION) — SWOS2.s line 76567 -------
 * Same auto-extractor bug as diskMenu — was emitted as `uint32_t = 0`. */
extern void SelectDiyLeagueMenu(void);
extern void SelectDiyCupMenu(void);
extern void SelectDiyTournamentMenu(void);
extern void SelectDiyExitMenu(void);
extern char _aSelectDiyCompe[];
extern char _aLeague[];
extern char _aCup[];
extern char _aTournament[];

#pragma pack(push, 1)
struct {
    MenuHeader hdr;
    MenuMark   markByteCoords;
    MenuEntry_Byte_Colors_TextPtr               entry0;  /* SELECT DIY COMPETITION TYPE title */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry1;  /* LEAGUE */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry2;  /* CUP */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry3;  /* TOURNAMENT */
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect  entry4;  /* EXIT */
    MenuMark   markEndMenu;
} _diyCompetitionMenu = {
    { 0, 0, 0, 1 },
    MENU_MARK_BYTE_COORDS,
    { { 0x3E, 0x00, -76,  0x0F },
      { MENU_CMD_COLORS, 0x17 },
      { MENU_CMD_TEXT_PTR, 0x0, _aSelectDiyCompe },
      MENU_CMD_END },
    { { 0x5C, 0x28, 0x78, 0x0F },
      { MENU_CMD_NAV, -1, -1, -1, 2 },
      { MENU_CMD_COLORS, 0xE },
      { MENU_CMD_TEXT_PTR, 0x0, _aLeague },
      { MENU_CMD_ONSELECT_FIRE, SelectDiyLeagueMenu },
      MENU_CMD_END },
    { { 0x5C, 0x41, 0x78, 0x0F },
      { MENU_CMD_NAV, -1, -1, 1, 3 },
      { MENU_CMD_COLORS, 0xE },
      { MENU_CMD_TEXT_PTR, 0x0, _aCup },
      { MENU_CMD_ONSELECT_FIRE, SelectDiyCupMenu },
      MENU_CMD_END },
    { { 0x5C, 0x5A, 0x78, 0x0F },
      { MENU_CMD_NAV, -1, -1, 2, 4 },
      { MENU_CMD_COLORS, 0xE },
      { MENU_CMD_TEXT_PTR, 0x0, _aTournament },
      { MENU_CMD_ONSELECT_FIRE, SelectDiyTournamentMenu },
      MENU_CMD_END },
    { { 0x66, -71,  0x64, 0x0F },
      { MENU_CMD_NAV, -1, -1, 3, -1 },
      { MENU_CMD_COLORS, 0xC },
      { MENU_CMD_TEXT_PTR, 0x0, _aExit },
      { MENU_CMD_ONSELECT_FIRE, SelectDiyExitMenu },
      MENU_CMD_END },
    MENU_MARK_END_MENU,
};
#pragma pack(pop)