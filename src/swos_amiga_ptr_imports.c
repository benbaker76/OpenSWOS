/*
 * src/swos_amiga_ptr_imports.c — auto-generated pointer tables.
 * Each entry maps an enum/index to a struct or string already defined
 * elsewhere in the codebase. Without these, callers read NULL and
 * segfault on the first dereference (same bug as _positionsTable,
 * _sidePitchSurnamePositionsTable, _teamTacticsTable etc.).
 *
 * Generated from SWOS2.s; only tables whose targets are all defined
 * in our build are emitted here. Six tables (_bottomBallOutOfPlayPositions,
 * _topBallOutOfPlayPositions, _kGoalieSkillTables, _diskMenu,
 * _diyStage1LegsStrTab, _savedRandStatePtr) reference symbols not yet
 * ported and stay as zero stubs in swos_amiga_data_stubs.c.
 */
#include "swos_types.h"
#include "game_types.h"   /* DIYTournamentState (_diyFileBufferCopy typed struct) */

/* _topSkillLettersEven/Odd — pointer tables that use base+offset syntax
 * in SWOS2.s (e.g. `dc.l _aTspvhcfstpvhcf+$7`). The master string
 * _aTspvhcfstpvhcf packs all per-position-group skill letters into one
 * contiguous buffer. The auto-extractor doesn't handle base+offset, so
 * these are hand-emitted. Without them FormTopSkillsString reads NULL
 * and segfaults at letters[i]. */
extern uint16_t _bigNumbersColorConversionTable[];
/* _skinAndHairColorConversionTable: pointer table indexed by skin
 * color (0-2). Entries are byte-offsets ($60/$7A/$94) into the
 * _bigNumbersColorConversionTable int16 array — i.e. int16 indices
 * 48/61/74. Without this FillSkinColorConversionTable reads NULL
 * and segfaults. */
void *_skinAndHairColorConversionTable[3] = {
    (_bigNumbersColorConversionTable + 48),
    (_bigNumbersColorConversionTable + 61),
    (_bigNumbersColorConversionTable + 74),
};

extern const char _aTspvhcfstpvhcf[];
void *_topSkillLettersEven[7] = {
    ((void *)_aTspvhcfstpvhcf + 0x00),
    ((void *)_aTspvhcfstpvhcf + 0x0E),
    ((void *)_aTspvhcfstpvhcf + 0x1C),
    ((void *)_aTspvhcfstpvhcf + 0x2A),
    ((void *)_aTspvhcfstpvhcf + 0x38),
    ((void *)_aTspvhcfstpvhcf + 0x46),
    ((void *)_aTspvhcfstpvhcf + 0x54),
};
void *_topSkillLettersOdd[7] = {
    ((void *)_aTspvhcfstpvhcf + 0x07),
    ((void *)_aTspvhcfstpvhcf + 0x15),
    ((void *)_aTspvhcfstpvhcf + 0x23),
    ((void *)_aTspvhcfstpvhcf + 0x31),
    ((void *)_aTspvhcfstpvhcf + 0x3F),
    ((void *)_aTspvhcfstpvhcf + 0x4D),
    ((void *)_aTspvhcfstpvhcf + 0x5B),
};

extern char _a2ndReplay[];
extern char _a2ndReplays[];
extern char _a343[];
extern char _a352[];
extern char _a3MatchesToTurn[];
extern char _a3rdPlayOff[];
extern char _a3rdReplay[];
extern char _a3rdReplays[];
extern char _a3rd[];
extern char _a424[];
extern char _a433[];
extern char _a442[];
extern char _a451[];
extern char _a523[];
extern char _a532[];
extern char _a541[];
extern char _aAGoodSeasonAWe[];
extern char _aAGoodSeasonAWe_0[];
extern char _aAVeryDisappoin[];
extern char _aAVeryDisappoin_0[];
extern char _aA_1[];
extern char _aAfr[];
extern char _aAfr_0[];
extern char _aAnUpAndDownSea[];
extern char _aAnUpAndDownSea_0[];
extern char _aAnyFurtherPlay[];
extern char _aAnyFurtherPlay_0[];
extern char _aAsi[];
extern char _aAsi_0[];
extern char _aAttack[];
extern char _aBHasLeftTheClu[];
extern char _aBHasLeftTheClu_0[];
extern char _aBeforeHandingT[];
extern char _aBeforeHandingT_0[];
extern char _aBoxall[];
extern char _aCanImproveOnTh[];
extern char _aCanImproveOnTh_0[];
extern char _aChapman[];
extern char _aChris[];
extern char _aClearTheOverdr[];
extern char _aComeOnAWeCanno[];
extern char _aD_0[];
extern char _aDavid[];
extern char _aDavid_0[];
extern char _aDearsley[];
extern char _aDecidedToRelie[];
extern char _aDecidedToRelie_0[];
extern char _aDefend[];
extern char _aDemandsMuchBet[];
extern char _aDistinguishedC[];
extern char _aDistinguishedC_0[];
extern char _aDiv1[];
extern char _aDiv2[];
extern char _aDiv3[];
extern char _aDivision1_0[];
extern char _aDivision2_0[];
extern char _aDivision3_0[];
extern char _aEnsureThatItIs[];
extern char _aEnsureThatWeCl[];
extern char _aEur[];
extern char _aEuro[];
extern char _aFifthRound[];
extern char _aFinalGroup[];
extern char _aFinalGroup_0[];
extern char _aFinalGroups[];
extern char _aFinalGroups_0[];
extern char _aFinal[];
extern char _aFinal_0[];
extern char _aFinal_1[];
extern char _aFinal_2[];
extern char _aFinal_3[];
extern char _aFirstRound[];
extern char _aFollowingACris[];
extern char _aFollowingAVote[];
extern char _aFourthRound[];
extern char _aG[];
extern char _aGraeme[];
extern char _aGrafsCjcteam1R[];
extern char _aGrafsCjcteam2R[];
extern char _aGrafsCjcteam3R[];
extern char _aGrafsSwcpich1M[];
extern char _aGrafsSwcpich2M[];
extern char _aGrafsSwcpich3M[];
extern char _aGrafsSwcpich4M[];
extern char _aGrafsSwcpich5M[];
extern char _aGrafsSwcpich6M[];
extern char _aGroupA[];
extern char _aGroupB[];
extern char _aGroupC[];
extern char _aGroupD[];
extern char _aGroupE[];
extern char _aGroupF[];
extern char _aGroupG[];
extern char _aGroupH[];
extern char _aHare[];
extern char _aHasJoinedTheCl[];
extern char _aImproveOnOurPe[];
extern char _aImproveTheTeam[];
extern char _aItIsWithGreatR[];
extern char _aItIsWithGreatR_0[];
extern char _aItIsWithGreatR_1[];
extern char _aJon[];
extern char _aJustANoteToWis[];
extern char _aJustANoteToWis_0[];
extern char _aJustAReminderT[];
extern char _aLb[];
extern char _aLw[];
extern char _aM[];
extern char _aMark[];
extern char _aMoreTimeToNego[];
extern char _aMuchBetterFrom[];
extern char _aMuchSuccessFro[];
extern char _aNam[];
extern char _aNam_0[];
extern char _aNewSeason[];
extern char _aNewSeason_0[];
extern char _aNotAVeryGoodSe[];
extern char _aNotAVeryGoodSe_0[];
extern char _aNottingham[];
extern char _aOce[];
extern char _aOce_0[];
extern char _aOrTheWageBillW[];
extern char _aOrWeWillHaveTo[];
extern char _aPleaseNoteATha[];
extern char _aPleaseNoteATha_0[];
extern char _aPleaseNoteATha_1[];
extern char _aPleaseNoteATha_2[];
extern char _aPleaseTakeNote[];
extern char _aPleaseTakeNote_0[];
extern char _aPleasedWithYou[];
extern char _aPleasedWithYou_0[];
extern char _aPremDiv[];
extern char _aPremierDivisio[];
extern char _aQF[];
extern char _aQFinal[];
extern char _aQuarterFinal[];
extern char _aRb[];
extern char _aRememberUnless[];
extern char _aRenewYourContr[];
extern char _aReplay[];
extern char _aReplays[];
extern char _aRnd1[];
extern char _aRnd2[];
extern char _aRnd3[];
extern char _aRnd4[];
extern char _aRnd5[];
extern char _aRnd6[];
extern char _aRound1[];
extern char _aRound2[];
extern char _aRound3[];
extern char _aRound4[];
extern char _aRound5[];
extern char _aRound6[];
extern char _aRw[];
extern char _aSF[];
extern char _aSFinal[];
extern char _aSam[];
extern char _aSam_0[];
extern char _aSeasonInManage[];
extern char _aSeasonInManage_0[];
extern char _aSecondRound[];
extern char _aSemiFinal[];
extern char _aSixthRound[];
extern char _aSorryAAreUnwil[];
extern char _aSorryAAreUnwil_0[];
extern char _aSorryAButTheCl[];
extern char _aSorryAButTheDi[];
extern char _aSuccessFromOur[];
extern char _aSweep[];
extern char _aThanksForAllYo[];
extern char _aThanksForAllYo_0[];
extern char _aThirdPlacePlay[];
extern char _aThirdRound[];
extern char _aThisSeasonAndW[];
extern char _aThisSeasonAndW_0[];
extern char _aUSER_A[];
extern char _aUSER_B[];
extern char _aUSER_C[];
extern char _aUSER_D[];
extern char _aUSER_E[];
extern char _aUSER_F[];
extern char _aWeAllHopeYouEn[];
extern char _aWeAllHopeYouEn_0[];
extern char _aWeAllWishYouTh[];
extern char _aWeAllWishYouTh_0[];
extern char _aWeHereAtAAreHa[];
extern char _aWeHereAtAAreHa_0[];
extern char _aWeWillHaveToRe[];
extern char _aWelcomeAWeHope[];
extern char _aWelcomeAWeLook[];
extern char _aWelcomeToOurCl[];
extern char _aWelcomeToOurCl_0[];
extern char _aWellDoneAYouDi[];
extern char _aWellDoneAYouDi_0[];
extern char _aWilkin[];
extern char _aYouWellInYourN[];
extern char _aYouWellInYourN_0[];
extern char _africaWorldCupQualificationCopy[];
extern char _africanNationsCupCopy[];
extern char _africanNationsCup[];
extern char _allPlayerTeamsEqual[];
extern char _asiaWorldCupQualificationCopy[];
extern char _asianCupCopy[];
extern char _asianCup[];
extern char _autoReplays[];
extern char _autoSaveHighlights[];
extern char _blackPalette[];
extern char _careerTeamFlagsBySeason0[];
extern char _careerTeamFlagsBySeason1[];
extern char _coachOrPlayer[];
extern char _copaAmericaCopy[];
extern char _copaAmerica[];
extern char _countriesStack[];
extern char _cupWinnersCupCopy[];
extern char _cupWinnersCup[];
extern char _diyCupEndMonthIndex[];
extern char _diyCupExtraTimeChoice0[];
extern char _diyCupExtraTimeChoice1[];
extern char _diyCupExtraTimeChoice2[];
extern char _diyCupExtraTimeChoice3[];
extern char _diyCupExtraTimeChoice4[];
extern char _diyCupExtraTimeChoice5[];
extern char _diyCupLegsChoice0[];
extern char _diyCupLegsChoice1[];
extern char _diyCupLegsChoice2[];
extern char _diyCupLegsChoice3[];
extern char _diyCupLegsChoice4[];
extern char _diyCupLegsChoice5[];
extern char _diyCupPenaltiesChoice0[];
extern char _diyCupPenaltiesChoice1[];
extern char _diyCupPenaltiesChoice2[];
extern char _diyCupPenaltiesChoice3[];
extern char _diyCupPenaltiesChoice4[];
extern char _diyCupPenaltiesChoice5[];
extern char _diyCupPitchTypeOrSeasonSelection[];
extern char _diyCupStartMonthIndex[];
extern char _diyLeagueEndingMonth[];
extern char _diyLeagueStartingMonth[];
extern char _diyPitchTypeOrSeason_1[];
extern char _diyTournamentEndMonthPage[];
extern char _diyTournamentStartMonthPage[];
extern char _diyTournamentUseSeasonalPitchEffective[];
extern uint16_t _dryPalette[3];
extern char _ecQualifyingGroup1Copy[];
extern char _ecQualifyingGroup2Copy[];
extern char _ecQualifyingGroup3Copy[];
extern char _ecQualifyingGroup4Copy[];
extern char _ecQualifyingGroup5Copy[];
extern char _ecQualifyingGroup6Copy[];
extern char _ecQualifyingGroup7Copy[];
extern char _ecQualifyingGroup8Copy[];
extern char _euroCupCopy[];
/* euroCupQualSeedBucket15/18/22/22Full: now _euroCupQualSeedBucketBlock.bucketN
 * (EuroCupQualSeedBucketBlock in game_types.h, included above). */
extern char _euroCupQualSeedList4[];
extern char _euroCupQualSeedList6[];
extern char _euroCupQualSeedList11[];
extern char _euroCupQualSeedList22[];
extern char _euroCupQualSeedListReduced4[];
extern char _euroCupQualSeedListReduced6[];
extern char _euroCupQualSeedListReduced11[];
extern char _euroCupQualSeedListReduced22[];
extern char _euroCup[];
extern char _europeanChampionships2Copy[];
extern char _europeanChampionships[];
extern char _fixtureSchedule10Teams[];
extern char _fixtureSchedule11Teams[];
extern char _fixtureSchedule12Teams[];
extern char _fixtureSchedule13Teams[];
extern char _fixtureSchedule14Teams[];
extern char _fixtureSchedule15Teams[];
extern char _fixtureSchedule16Teams[];
extern char _fixtureSchedule17Teams[];
extern char _fixtureSchedule18Teams[];
extern char _fixtureSchedule19Teams[];
extern char _fixtureSchedule20Teams[];
extern char _fixtureSchedule21Teams[];
extern char _fixtureSchedule22Teams[];
extern char _fixtureSchedule23Teams[];
extern char _fixtureSchedule24Teams[];
extern char _fixtureSchedule2Teams[];
extern char _fixtureSchedule3Teams[];
extern char _fixtureSchedule4Teams[];
extern char _fixtureSchedule5Teams[];
extern char _fixtureSchedule6Teams[];
extern char _fixtureSchedule7Teams[];
extern char _fixtureSchedule8Teams[];
extern char _fixtureSchedule9Teams[];
extern char _flipOnOff[];
extern char _friendlyCopy[];
extern char _friendly[];
extern uint16_t _frozenPalette[3];
extern char _gameLength[];
extern char _gamePalette[];
extern char _goalScorerDist343[];
extern char _goalScorerDist352[];
extern char _goalScorerDist424[];
extern char _goalScorerDist433[];
extern char _goalScorerDist442[];
extern char _goalScorerDist451[];
extern char _goalScorerDist523[];
extern char _goalScorerDist532[];
extern char _goalScorerDist541[];
extern char _goalScorerDistAttack[];
extern char _goalScorerDistDefend[];
extern char _goalScorerDistSweep[];
extern uint16_t _hardPalette[3];
extern char _invitationTournamentCopy[];
extern char _menuMusic[];
extern char _menuPalette[];
extern uint16_t _muddyPalette[3];
extern uint16_t _normalPalette[3];
extern char _northAmericaWorldCupQualificationCopy[];
extern char _northAmericanCupCopy[];
extern char _northAmericanCup[];
extern char _oceaniaCupCopy[];
extern char _oceaniaCup[];
extern char _oceaniaWorldCupQualificationCopy[];
extern char _panicPalette[];
extern char _pitchType[];
extern char _playerCardChances10min[];
extern char _playerCardChances3min[];
extern char _playerCardChances5min[];
extern char _playerCardChances7min[];
extern char _playerMarketFilterAgeGroup[];
extern char _playerMarketFilterPosition[];
extern char _positionGroupAttacker[];
extern char _positionGroupDefender[];
extern char _positionGroupGoalkeeper[];
extern char _positionGroupMidfielder[];
extern char _positions343[];
extern char _positions352[];
extern char _positions424[];
extern char _positions433[];
extern char _positions451[];
extern char _positions523[];
extern char _positions532[];
extern char _positions541[];
extern char _positionsAttack[];
extern char _positionsDefault[];
extern char _positionsDefend[];
extern char _positionsSweep[];
extern char _sidePitchPositionsAttack[];
extern char _sidePitchPositionsDefault[];
extern char _sidePitchPositionsDefend[];
extern char _sidePitchPositionsSweep[];
extern char _sidePitchPositions_3_4_3[];
extern char _sidePitchPositions_3_5_2[];
extern char _sidePitchPositions_4_2_4[];
extern char _sidePitchPositions_4_3_3[];
extern char _sidePitchPositions_4_5_1[];
extern char _sidePitchPositions_5_2_3[];
extern char _sidePitchPositions_5_3_2[];
extern char _sidePitchPositions_5_4_1[];
extern char _simExtraGoalProbThreshA[];
extern char _simExtraGoalProbThreshB[];
extern char _simExtraGoalProbThreshC[];
extern char _simExtraGoalProbThreshD[];
extern char _simExtraGoalProbThreshE[];
extern char _simGoalProbThreshA[];
extern char _simGoalProbThreshB[];
extern char _simGoalProbThreshC[];
extern char _simGoalProbThreshD[];
extern char _simGoalProbThreshE[];
extern char _skillBitRotEven1[];
extern char _skillBitRotEven2[];
extern char _skillBitRotEven3[];
extern char _skillBitRotEven4[];
extern char _skillBitRotEven5[];
extern char _skillBitRotEven6[];
extern char _skillBitRotEven7[];
extern char _skillBitRotOdd1[];
extern char _skillBitRotOdd2[];
extern char _skillBitRotOdd3[];
extern char _skillBitRotOdd4[];
extern char _skillBitRotOdd5[];
extern char _skillBitRotOdd6[];
extern char _skillBitRotOdd7[];
extern uint16_t _softPalette[3];
extern char _southAmericaWorldCupQualificationCopy[];
extern char _tacticPosMap0[];
extern char _tacticPosMap10[];
extern char _tacticPosMap11[];
extern char _tacticPosMap1[];
extern char _tacticPosMap2[];
extern char _tacticPosMap3[];
extern char _tacticPosMap4[];
extern char _tacticPosMap5[];
extern char _tacticPosMap6[];
extern char _tacticPosMap7[];
extern char _tacticPosMap8[];
extern char _tacticPosMap9[];
extern char _titleLogoPalette[];
extern char _titleScreenPalette[];
extern char _titleTextPalette[];
extern char _tourCopy[];
extern char _trainingPosOrder0[];
extern char _trainingPosOrder1[];
extern char _trainingPosOrder2[];
extern char _trainingPosOrder3[];
extern char _trainingPosOrder4[];
extern char _trainingPosOrder5[];
extern char _trainingPosOrder6[];
extern char _trainingPosOrder7[];
extern char _uefaCupCopy[];
extern char _uefaCup[];
extern uint16_t _wetPalette[3];
extern char _worldCup2Copy[];
extern char _worldCupQualifyingGroup1Copy[];
extern char _worldCupQualifyingGroup2Copy[];
extern char _worldCupQualifyingGroup3Copy[];
extern char _worldCupQualifyingGroup4Copy[];
extern char _worldCupQualifyingGroup5Copy[];
extern char _worldCupQualifyingGroup6Copy[];
extern char _worldCupQualifyingGroup7Copy[];
extern char _worldCupQualifyingGroup8Copy[];
extern char _worldCup[];

const uint16_t *_pitchPalettePtrs[7] = {
    _frozenPalette,
    _muddyPalette,
    _wetPalette,
    _softPalette,
    _normalPalette,
    _dryPalette,
    _hardPalette,
};
void *_swcPitchMapFiles[6] = {
    _aGrafsSwcpich1M,
    _aGrafsSwcpich2M,
    _aGrafsSwcpich3M,
    _aGrafsSwcpich4M,
    _aGrafsSwcpich5M,
    _aGrafsSwcpich6M,
};
void *_paletteTable[7] = {
    _menuPalette,
    _blackPalette,
    _gamePalette,
    _titleScreenPalette,
    _panicPalette,
    _titleTextPalette,
    _titleLogoPalette,
};
void *_playerCardChancesPerGameLength[4] = {
    _playerCardChances3min,
    _playerCardChances5min,
    _playerCardChances7min,
    _playerCardChances10min,
};
void *_defaultNamesTable[6] = {
    _aChris,
    _aJon,
    _aGraeme,
    _aMark,
    _aDavid,
    _aDavid_0,
};
void *_defaultSurnamesTable[6] = {
    _aChapman,
    _aHare,
    _aBoxall,
    _aDearsley,
    _aWilkin,
    _aNottingham,
};
void *_simGoalProbByTacticPtrs[18] = {
    _simGoalProbThreshA,
    _simGoalProbThreshA,
    _simGoalProbThreshB,
    _simGoalProbThreshB,
    _simGoalProbThreshC,
    _simGoalProbThreshC,
    _simGoalProbThreshC,
    _simGoalProbThreshD,
    _simGoalProbThreshD,
    _simGoalProbThreshD,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
    _simGoalProbThreshE,
};
void *_simExtraGoalProbByTacticPtrs[16] = {
    _simExtraGoalProbThreshA,
    _simExtraGoalProbThreshA,
    _simExtraGoalProbThreshB,
    _simExtraGoalProbThreshB,
    _simExtraGoalProbThreshC,
    _simExtraGoalProbThreshC,
    _simExtraGoalProbThreshC,
    _simExtraGoalProbThreshD,
    _simExtraGoalProbThreshD,
    _simExtraGoalProbThreshD,
    _simExtraGoalProbThreshE,
    _simExtraGoalProbThreshE,
    _simExtraGoalProbThreshE,
    _simExtraGoalProbThreshE,
    _simExtraGoalProbThreshE,
    _simExtraGoalProbThreshE,
};
void *_goalScorerDistributionByBallOutTactic[18] = {
    _goalScorerDist442,
    _goalScorerDist541,
    _goalScorerDist451,
    _goalScorerDist532,
    _goalScorerDist352,
    _goalScorerDist433,
    _goalScorerDist424,
    _goalScorerDist343,
    _goalScorerDistSweep,
    _goalScorerDist523,
    _goalScorerDistAttack,
    _goalScorerDistDefend,
    _goalScorerDist442,
    _goalScorerDist442,
    _goalScorerDist442,
    _goalScorerDist442,
    _goalScorerDist442,
    _goalScorerDist442,
};
void *_careerTeamFlagsByRegionTable[6] = {
    _careerTeamFlagsBySeason0,
    _careerTeamFlagsBySeason1,
    _careerTeamFlagsBySeason1,
    _careerTeamFlagsBySeason1,
    _careerTeamFlagsBySeason1,
    _careerTeamFlagsBySeason1,
};
void *_trainingPosOrderTables[8] = {
    _trainingPosOrder0,
    _trainingPosOrder1,
    _trainingPosOrder2,
    _trainingPosOrder3,
    _trainingPosOrder4,
    _trainingPosOrder5,
    _trainingPosOrder6,
    _trainingPosOrder7,
};
/* ---- String tables for MENU_CMD_STRING_TABLE entries ----------------
 * Layout (per SWOS2.s and the C unpacker in DrawMenuItem):
 *   +0  uint32_t  indexPtr     ; pointer to a uint16_t holding current index
 *   +4  uint16_t  base         ; subtracted from current to get list offset
 *   +6  uint32_t  entries[N]   ; pointers to the strings to display
 *
 * The auto-extractor that originally produced this file emitted only the
 * first 4-byte field, so every STRING_TABLE-typed menu entry crashed in
 * DrawMenuItem when it tried to follow the (out-of-bounds) entries[]
 * array. Hand-rebuild each one from SWOS2.s with the exact same byte
 * layout via a packed struct. */
extern const char _aJan[], _aFeb[], _aMar[], _aApr[], _aMay[], _aJun[];
extern const char _aJul[], _aAug[], _aSep[], _aOct[], _aNov[], _aDec[];

/* These are already extern'd as `char[]` earlier in this file by the
 * auto-extractor. We just take their addresses below — the unpacker
 * reads 2 bytes at the address as a uint16 index, regardless of the
 * declared element type. */
extern uint8_t  _diyCompetitionStart[];
extern const char _aPitchType[], _aSeason_1[];
extern const char _aRandom[], _aFrozen[], _aMuddy[], _aWet[], _aSoft[];
extern const char _aNormal[], _aDry[], _aHard[];

#define MONTHS \
    (void *)_aJan, (void *)_aFeb, \
    (void *)_aMar, (void *)_aApr, \
    (void *)_aMay, (void *)_aJun, \
    (void *)_aJul, (void *)_aAug, \
    (void *)_aSep, (void *)_aOct, \
    (void *)_aNov, (void *)_aDec

extern char _diyPitchTypeOrSeason_1[];
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _diyPitchTypeOrSeasonStrTable = {
    _diyPitchTypeOrSeason_1, 0,
    { (void *)_aPitchType, (void *)_aSeason_1 }
};

/* _diyLeaguePitchTypeStrTable: index var is `_diyCompetitionStart.base.pitchType`
 * (asm `_diyCompetitionStart + 0x36`; the raw 32-bit offset read numTeams on
 * 64-bit, giving an out-of-bounds entries[] read — typed field is arch-safe).
 * base = -1 (so index 0 → entries[1] = "FROZEN" while index -1 → "RANDOM"). */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[8];
} _diyLeaguePitchTypeStrTable = {
    (void *)&((DIYCompetitionEx *)_diyCompetitionStart)->base.pitchType, (uint16_t)-1,
    {
        (void *)_aRandom, (void *)_aFrozen,
        (void *)_aMuddy,  (void *)_aWet,
        (void *)_aSoft,   (void *)_aNormal,
        (void *)_aDry,    (void *)_aHard,
    }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyLeagueMonthsStrTable = {
    _diyLeagueStartingMonth, 0,
    { MONTHS }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyLeagueEndingMonthStrTable = {
    _diyLeagueEndingMonth, 0,
    { MONTHS }
};

/* DIY CUP / Tournament — same auto-extractor truncation as the
 * other StringTable structs, recovered from SWOS2.s lines 70279,
 * 70320, 75328. */
extern DIYTournamentState _diyFileBufferCopy;
extern const char _aAfter90Mins[], _aAfterExtraTime[];
extern const char _aOff[];
extern const char _aRandom[], _aFrozen[], _aMuddy[], _aWet[];
extern const char _aSoft[], _aNormal[], _aDry[], _aHard[];

#define PITCH_8 \
    (void *)_aRandom, (void *)_aFrozen, \
    (void *)_aMuddy,  (void *)_aWet, \
    (void *)_aSoft,   (void *)_aNormal, \
    (void *)_aDry,    (void *)_aHard

/* _diyCupPitchTypeStringTable: indexPtr = `_diyCompetitionStart + 0x36`
 * = `&_diyCompetitionStart.base.pitchType`; base = -1; 8 entries. */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[8];
} _diyCupPitchTypeStringTable = {
    (void *)&((DIYCompetitionEx *)_diyCompetitionStart)->base.pitchType, (uint16_t)-1,
    { PITCH_8 }
};

/* _diyCupAwayGoalsStringTable: indexPtr = `_diyCompetitionStart + 0x5E`
 * (which our struct names `playEachTeam` — Amiga reuses that field
 * for AWAY-GOALS-MODE in cup configurations); base = 0; 3 entries. */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[3];
} _diyCupAwayGoalsStringTable = {
    (void *)&((DIYCompetitionEx *)_diyCompetitionStart)->base.playEachTeam, 0,
    {
        (void *)_aOff,
        (void *)_aAfter90Mins,
        (void *)_aAfterExtraTime,
    }
};

/* _diyPitchTypeStringTable: indexPtr = `_diyFileBufferCopy + 0x36`. */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[8];
} _diyPitchTypeStringTable = {
    ((void *)&_diyFileBufferCopy.header.pitchType), (uint16_t)-1,
    { PITCH_8 }
};

#undef PITCH_8

void *_groupStringTable[8] = {
    _aGroupA,
    _aGroupB,
    _aGroupC,
    _aGroupD,
    _aGroupE,
    _aGroupF,
    _aGroupG,
    _aGroupH,
};
/* ---- DIY Cup string tables (SWOS2.s lines 70274..70433) ----
 * All 21 of these were previously emitted as 4-byte stubs holding
 * just the indexPtr; the unpacker reads `tablePtr + 6 + idx*4` and
 * walked off the end into unmapped memory. Recovered with proper
 * packed structs below. */
extern char _diyCupPitchTypeOrSeasonSelection[];
extern char _diyCupStartMonthIndex[];
extern char _diyCupEndMonthIndex[];
extern char _diyCupLegsChoice0[], _diyCupLegsChoice1[], _diyCupLegsChoice2[];
extern char _diyCupLegsChoice3[], _diyCupLegsChoice4[], _diyCupLegsChoice5[];
extern char _diyCupExtraTimeChoice0[], _diyCupExtraTimeChoice1[], _diyCupExtraTimeChoice2[];
extern char _diyCupExtraTimeChoice3[], _diyCupExtraTimeChoice4[], _diyCupExtraTimeChoice5[];
extern char _diyCupPenaltiesChoice0[], _diyCupPenaltiesChoice1[], _diyCupPenaltiesChoice2[];
extern char _diyCupPenaltiesChoice3[], _diyCupPenaltiesChoice4[], _diyCupPenaltiesChoice5[];
extern const char _a1Leg[], _a2Legs[];
extern const char _aNoExtraTime[], _aExtraTime[], _aETIfReplay[];
extern const char _aNoPenalties[], _aPenalties[], _aPensIfReplay[];
extern const char _aPitchType[], _aSeason_1[];

#define MONTHS \
    (void *)_aJan, (void *)_aFeb, \
    (void *)_aMar, (void *)_aApr, \
    (void *)_aMay, (void *)_aJun, \
    (void *)_aJul, (void *)_aAug, \
    (void *)_aSep, (void *)_aOct, \
    (void *)_aNov, (void *)_aDec
#define LEGS_3 \
    (void *)_a1Leg, (void *)_a1Leg, (void *)_a2Legs
#define EXTRATIME_3 \
    (void *)_aNoExtraTime, (void *)_aExtraTime, \
    (void *)_aETIfReplay
#define PENALTIES_3 \
    (void *)_aNoPenalties, (void *)_aPenalties, \
    (void *)_aPensIfReplay

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _diyCupPitchTypeOrSeasonStringTable = {
    _diyCupPitchTypeOrSeasonSelection, 0,
    { (void *)_aPitchType, (void *)_aSeason_1 }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyCupStartMonthStringTable = {
    _diyCupStartMonthIndex, 0, { MONTHS }
};
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyCupEndMonthStringTable = {
    _diyCupEndMonthIndex, 0, { MONTHS }
};

#define DEFINE_LEGS(N) \
struct __attribute__((packed)) { \
    void    *indexPtr; uint16_t base; void    *entries[3]; \
} _diyCupLegsStringTable##N = { _diyCupLegsChoice##N, 0, { LEGS_3 } }
DEFINE_LEGS(0); DEFINE_LEGS(1); DEFINE_LEGS(2);
DEFINE_LEGS(3); DEFINE_LEGS(4); DEFINE_LEGS(5);
#undef DEFINE_LEGS

#define DEFINE_ET(N) \
struct __attribute__((packed)) { \
    void    *indexPtr; uint16_t base; void    *entries[3]; \
} _diyCupExtraTimeStringTable##N = { _diyCupExtraTimeChoice##N, 0, { EXTRATIME_3 } }
DEFINE_ET(0); DEFINE_ET(1); DEFINE_ET(2);
DEFINE_ET(3); DEFINE_ET(4); DEFINE_ET(5);
#undef DEFINE_ET

#define DEFINE_PEN(N) \
struct __attribute__((packed)) { \
    void    *indexPtr; uint16_t base; void    *entries[3]; \
} _diyCupPenaltiesStringTable##N = { _diyCupPenaltiesChoice##N, 0, { PENALTIES_3 } }
DEFINE_PEN(0); DEFINE_PEN(1); DEFINE_PEN(2);
DEFINE_PEN(3); DEFINE_PEN(4); DEFINE_PEN(5);
#undef DEFINE_PEN

#undef MONTHS
#undef LEGS_3
#undef EXTRATIME_3
#undef PENALTIES_3
/* ---- options-menu string tables ----
 * The earlier extern block in this file declared several of these as
 * `extern char foo[]` from the auto-extractor. STRING_TABLE indexPtr
 * is a 4-byte pointer regardless of the variable's element type, so
 * we just take the address — the C unpacker reads 2 bytes at that
 * address as a uint16_t index. Don't redeclare the type here. */
extern const char _aSeasonal[];
extern const char _a3Mins[], _a5Mins[], _a7Mins[], _a10Mins[];
extern const char _aOff[], _aOn[];

#define ON_OFF_TABLE(NAME, VAR) \
struct __attribute__((packed)) { \
    void    *indexPtr; uint16_t base; void    *entries[2]; \
} NAME = { \
    (VAR), 0, \
    { (void *)_aOff, (void *)_aOn } \
}

/* _pitchType menu: base = -2 so index -2 → "SEASONAL", -1 → "RANDOM",
 * 0..6 → FROZEN..HARD (matches SWOS2.s line 73164). */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[9];
} _pitchTypeStrTable = {
    _pitchType, (uint16_t)-2,
    {
        (void *)_aSeasonal, (void *)_aRandom,
        (void *)_aFrozen,   (void *)_aMuddy,
        (void *)_aWet,      (void *)_aSoft,
        (void *)_aNormal,   (void *)_aDry,
        (void *)_aHard,
    }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[4];
} _gameLengthStrTable = {
    _gameLength, 0,
    {
        (void *)_a3Mins,  (void *)_a5Mins,
        (void *)_a7Mins,  (void *)_a10Mins,
    }
};

ON_OFF_TABLE(_autoReplaysStrTable,     _autoReplays);
ON_OFF_TABLE(_allPlTeamsEqualStrTable, _allPlayerTeamsEqual);
ON_OFF_TABLE(_autoSaveHlStrTable,      _autoSaveHighlights);
ON_OFF_TABLE(_menuMusicStrTable,       _menuMusic);

/* SHOOTING PRACTICE setup screen's DEFENDERS and MIDFIELD rows: OFF / ON,
 * indexed by their flags (shooting_practice.c). Same shape as the ON/OFF
 * tables above; spelled out because the entries name their line too. */
extern char _aDefendersOff[], _aDefendersOn[];
extern char _aMidfieldOff[], _aMidfieldOn[];
extern uint16_t g_shootingPracticeDefenders;
extern uint16_t g_shootingPracticeMidfield;
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _practiceDefendersStrTable = {
    (&g_shootingPracticeDefenders), 0,
    { (void *)_aDefendersOff, (void *)_aDefendersOn }
};
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _practiceMidfieldStrTable = {
    (&g_shootingPracticeMidfield), 0,
    { (void *)_aMidfieldOff, (void *)_aMidfieldOn }
};

extern char _aPracticeAttackUp[], _aPracticeAttackDown[];
extern uint16_t g_shootingPracticeAttackDown;
struct __attribute__((packed)) {
    void *indexPtr; uint16_t base; void *entries[2];
} _practiceDirectionStrTable = {
    (&g_shootingPracticeAttackDown), 0,
    { (void *)_aPracticeAttackUp, (void *)_aPracticeAttackDown }
};

/* OPTIONS -> GAMEPLAY: AMIGA / DOS, indexed by _gameplayStyleOption
 * (dos_gameplay.c). Same shape as the ON/OFF tables above; spelled out because
 * the entries are not _aOff/_aOn. */
extern char _aAmigaStyle[], _aDosStyle[];
extern uint16_t _gameplayStyleOption;
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _gameplayStyleStrTable = {
    (&_gameplayStyleOption), 0,
    { (void *)_aAmigaStyle, (void *)_aDosStyle }
};

/* OPTIONS -> PITCHES: DOS / 2020, indexed by _pitchStyleOption (pitch.c). */
extern char _a2020Style[];
extern uint16_t _pitchStyleOption;
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _pitchStyleStrTable = {
    (&_pitchStyleOption), 0,
    { (void *)_aDosStyle, (void *)_a2020Style }
};

/* OPTIONS -> CPU CARDS: OFF / ON, indexed by _cpuCardsOption (tackle_foul.c),
 * the uint16_t mirror of _gCpuTeamsGetCards -- [gameplay] cpu_cards. */
extern uint16_t _cpuCardsOption;
ON_OFF_TABLE(_cpuCardsStrTable, (&_cpuCardsOption));

#undef ON_OFF_TABLE
#undef MONTHS
/* ---- DIY Tournament string tables (SWOS2.s lines 75323..75554) ----
 * 34 packed StringTable structs that the auto-extractor truncated to
 * 4-byte stubs (3 here as `[1]` arrays, 31 as scalars in
 * swos_data_typed_stubs.c). Each table is indexPtr + base + entries[]
 * the way the unpacker expects (see project_string_table_truncation.md). */
/* Note: the auto-extracted block earlier in this file already extern'd
 * _diyTournamentUseSeasonalPitchEffective / StartMonthPage / EndMonthPage
 * as `char[]`. The address-of works regardless of element type, so just
 * reuse those forward declarations (no re-extern here). */
extern uint8_t  _diyTournamentStages[];   /* DIYStage[6] = 60 bytes */
extern const char _a2PtsForAWin[], _a3PtsForAWin[];
extern const char _aPlayEachTeamX1[], _aPlayEachTeamX2[], _aPlayEachTeamX3[], _aPlayEachTeamX4[];

#define MONTHS \
    (void *)_aJan, (void *)_aFeb, \
    (void *)_aMar, (void *)_aApr, \
    (void *)_aMay, (void *)_aJun, \
    (void *)_aJul, (void *)_aAug, \
    (void *)_aSep, (void *)_aOct, \
    (void *)_aNov, (void *)_aDec
#define LEGS_3 \
    (void *)_a1Leg, (void *)_a1Leg, (void *)_a2Legs
#define EXTRATIME_3 \
    (void *)_aNoExtraTime, (void *)_aExtraTime, (void *)_aETIfReplay
#define PENALTIES_3 \
    (void *)_aNoPenalties, (void *)_aPenalties, (void *)_aPensIfReplay
#define PTS_2 \
    (void *)_a2PtsForAWin, (void *)_a3PtsForAWin
#define PLAYCOUNT_4 \
    (void *)_aPlayEachTeamX1, (void *)_aPlayEachTeamX2, \
    (void *)_aPlayEachTeamX3, (void *)_aPlayEachTeamX4

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _diyPitchSeasonStrTab = {
    _diyTournamentUseSeasonalPitchEffective, 0,
    { (void *)_aPitchType, (void *)_aSeason_1 }
};
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyStartMonthStrTab = {
    _diyTournamentStartMonthPage, 0, { MONTHS }
};
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[12];
} _diyEndMonthStrTab = {
    _diyTournamentEndMonthPage, 0, { MONTHS }
};
/* _diyAwayGoalsStrTab: indexPtr = _diyFileBufferCopy + 0x4A. */
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[3];
} _diyAwayGoalsStrTab = {
    ((void *)&_diyFileBufferCopy.header.menuState), 0,
    {
        (void *)_aOff,
        (void *)_aAfter90Mins,
        (void *)_aAfterExtraTime,
    }
};

/* Six-stage tournament tables. Each stage is 10 bytes (DIYStage):
 *   +0 legs, +2 extraTime, +4 penalties, +6 ptsForWin, +8 playCount.
 * (NB our DIYStage struct names ptsForWin/playCount in the OPPOSITE
 * order — the byte offsets here match the Amiga layout regardless.) */
#define STAGE_PTR(N, OFF)  ((uint8_t *)_diyTournamentStages + (N) * 0xA + (OFF))

#define DEFINE_LEGS(N) \
struct __attribute__((packed)) { void    *indexPtr; uint16_t base; void    *entries[3]; } \
_diyStage##N##LegsStrTab = { STAGE_PTR((N) - 1, 0), 0, { LEGS_3 } }
DEFINE_LEGS(1); DEFINE_LEGS(2); DEFINE_LEGS(3);
DEFINE_LEGS(4); DEFINE_LEGS(5); DEFINE_LEGS(6);
#undef DEFINE_LEGS

#define DEFINE_ET(N) \
struct __attribute__((packed)) { void    *indexPtr; uint16_t base; void    *entries[3]; } \
_diyStage##N##ExtraTimeStrTab = { STAGE_PTR((N) - 1, 2), 0, { EXTRATIME_3 } }
DEFINE_ET(1); DEFINE_ET(2); DEFINE_ET(3);
DEFINE_ET(4); DEFINE_ET(5); DEFINE_ET(6);
#undef DEFINE_ET

#define DEFINE_PEN(N) \
struct __attribute__((packed)) { void    *indexPtr; uint16_t base; void    *entries[3]; } \
_diyStage##N##PenaltiesStrTab = { STAGE_PTR((N) - 1, 4), 0, { PENALTIES_3 } }
DEFINE_PEN(1); DEFINE_PEN(2); DEFINE_PEN(3);
DEFINE_PEN(4); DEFINE_PEN(5); DEFINE_PEN(6);
#undef DEFINE_PEN

/* PtsForWin: indexPtr at stage offset +6, base = 2 (index 2 → "2 PTS"). */
#define DEFINE_PTS(N) \
struct __attribute__((packed)) { void    *indexPtr; uint16_t base; void    *entries[2]; } \
_diyStage##N##PtsForWinStrTab = { STAGE_PTR((N) - 1, 6), 2, { PTS_2 } }
DEFINE_PTS(1); DEFINE_PTS(2); DEFINE_PTS(3);
DEFINE_PTS(4); DEFINE_PTS(5); DEFINE_PTS(6);
#undef DEFINE_PTS

/* PlayCount: indexPtr at stage offset +8, base = 1, 4 entries. */
#define DEFINE_PLAY(N) \
struct __attribute__((packed)) { void    *indexPtr; uint16_t base; void    *entries[4]; } \
_diyStage##N##PlayCountStrTab = { STAGE_PTR((N) - 1, 8), 1, { PLAYCOUNT_4 } }
DEFINE_PLAY(1); DEFINE_PLAY(2); DEFINE_PLAY(3);
DEFINE_PLAY(4); DEFINE_PLAY(5); DEFINE_PLAY(6);
#undef DEFINE_PLAY

#undef STAGE_PTR
#undef MONTHS
#undef LEGS_3
#undef EXTRATIME_3
#undef PENALTIES_3
#undef PTS_2
#undef PLAYCOUNT_4
void *_topSkillBitRotEven[7] = {
    _skillBitRotEven1,
    _skillBitRotEven2,
    _skillBitRotEven3,
    _skillBitRotEven4,
    _skillBitRotEven5,
    _skillBitRotEven6,
    _skillBitRotEven7,
};
void *_topSkillBitRotOdd[7] = {
    _skillBitRotOdd1,
    _skillBitRotOdd2,
    _skillBitRotOdd3,
    _skillBitRotOdd4,
    _skillBitRotOdd5,
    _skillBitRotOdd6,
    _skillBitRotOdd7,
};
void *_tacticPosMapPtrs[18] = {
    _tacticPosMap0,
    _tacticPosMap1,
    _tacticPosMap2,
    _tacticPosMap3,
    _tacticPosMap4,
    _tacticPosMap5,
    _tacticPosMap6,
    _tacticPosMap7,
    _tacticPosMap8,
    _tacticPosMap9,
    _tacticPosMap10,
    _tacticPosMap11,
    _tacticPosMap0,
    _tacticPosMap0,
    _tacticPosMap0,
    _tacticPosMap0,
    _tacticPosMap0,
    _tacticPosMap0,
};
/* _offsetCoachOrPlayer: career-menu COACH / PLAYER-COACH selector
 * (SWOS2.s:85269). Auto-extractor emitted only the indexPtr; the
 * STRING_TABLE layout actually needs `{ indexPtr, base, entries[N] }`
 * packed, otherwise the menu reads past the indexPtr into adjacent
 * globals when picking a string. */
extern const char _aCoach_0[], _aPlayerCoach_0[];
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[2];
} _offsetCoachOrPlayer = {
    _coachOrPlayer, 0,
    {
        (void *)_aCoach_0,
        (void *)_aPlayerCoach_0,
    }
};
void *_countryStackPtr[1] = {
    _countriesStack,
};
void *_sellExchangeStrings[4] = {
    _aWeHereAtAAreHa,
    _aWeHereAtAAreHa_0,
    _aSorryAAreUnwil,
    _aSorryAAreUnwil_0,
};
/* _positionFilterStringTable / _skillFilterStringTable / _countryFilterStringTable
 * — same auto-extractor truncation as the diyCup* tables: only the
 * first 4 bytes (the indexPtr) were emitted, but the menu engine
 * (menu_system.c:632 ENTRY_STRING_TABLE) reads at +0 (indexPtr),
 * +4 (base, i16), and +6 (4-byte string pointers indexed by
 * `*indexPtr - base`). With the truncated form, the +6 read landed
 * in adjacent BSS and produced NULL/garbage — DrawMenuTextCentered
 * (entry 4 of the player-market menu) crashed on the position
 * filter when ShowPlayerMarketInit ran.
 *
 * Layouts recovered from SWOS2.s line 102593-102620. The country
 * filter table immediately follows _skillFilterStringTable in the
 * original (no separate label) — we expose it as its own symbol
 * since nothing in our menu_data.c references it via stride. */
/* _playerMarketFilterPosition / _playerMarketFilterAgeGroup are
 * already extern-declared earlier in this file as `char[]`; the
 * actual definition is `uint16_t[1]` in swos_amiga_data_imports.c.
 * We just need their address — the menu engine reads through it as
 * `*(uint16_t *)indexPtr` regardless of declared type. */
extern char _playerMarketFilterCountry[];
extern const char _aAny[];
extern const char _aGoalkeeper[], _aDefender[], _aMidfielder[], _aAttacker[];
extern const char _aPassing[], _aShooting[], _aHeading[], _aTackling[];
extern const char _aBallControl[], _aSpeed[], _aFinishing[];
extern const char _aEuropean[], _aNorthAmerican[], _aSouthAmerican[];
extern const char _aAfrican[], _aAsian[], _aOceanian[];

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[5];
} _positionFilterStringTable = {
    _playerMarketFilterPosition, (uint16_t)-1,
    {
        (void *)_aAny,
        (void *)_aGoalkeeper,
        (void *)_aDefender,
        (void *)_aMidfielder,
        (void *)_aAttacker,
    }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[8];
} _skillFilterStringTable = {
    _playerMarketFilterAgeGroup, (uint16_t)-1,
    {
        (void *)_aAny,
        (void *)_aPassing,
        (void *)_aShooting,
        (void *)_aHeading,
        (void *)_aTackling,
        (void *)_aBallControl,
        (void *)_aSpeed,
        (void *)_aFinishing,
    }
};

struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[7];
} _countryFilterStringTable = {
    _playerMarketFilterCountry, 0,
    {
        (void *)_aEuropean,
        (void *)_aNorthAmerican,
        (void *)_aSouthAmerican,
        (void *)_aAfrican,
        (void *)_aAsian,
        (void *)_aOceanian,
        (void *)_aAny,
    }
};
void *_positionGroupTable[4] = {
    _positionGroupGoalkeeper,
    _positionGroupDefender,
    _positionGroupMidfielder,
    _positionGroupAttacker,
};
void *_sidePitchSurnamePositionsTable[18] = {
    _sidePitchPositionsDefault,
    _sidePitchPositions_5_4_1,
    _sidePitchPositions_4_5_1,
    _sidePitchPositions_5_3_2,
    _sidePitchPositions_3_5_2,
    _sidePitchPositions_4_3_3,
    _sidePitchPositions_4_2_4,
    _sidePitchPositions_3_4_3,
    _sidePitchPositionsSweep,
    _sidePitchPositions_5_2_3,
    _sidePitchPositionsAttack,
    _sidePitchPositionsDefend,
    _sidePitchPositionsDefault,
    _sidePitchPositionsDefault,
    _sidePitchPositionsDefault,
    _sidePitchPositionsDefault,
    _sidePitchPositionsDefault,
    _sidePitchPositionsDefault,
};
void *_clubMessagesTable[76] = {
    _aSorryAButTheCl,
    _aAnyFurtherPlay,
    _aComeOnAWeCanno,
    _aOrTheWageBillW,
    _aSorryAButTheDi,
    _aMoreTimeToNego,
    _aPleaseNoteATha_2,
    _aAnyFurtherPlay_0,
    _aPleaseTakeNote,
    _aEnsureThatWeCl,
    _aJustAReminderT,
    _aOrWeWillHaveTo,
    _aFollowingACris,
    _aClearTheOverdr,
    _aItIsWithGreatR_0,
    _aDecidedToRelie,
    _aPleaseTakeNote_0,
    _aEnsureThatItIs,
    _aRememberUnless,
    _aWeWillHaveToRe,
    _aFollowingAVote,
    _a3MatchesToTurn,
    _aItIsWithGreatR_1,
    _aDecidedToRelie_0,
    _aWellDoneAYouDi_0,
    _aThisSeasonAndW_0,
    _aAGoodSeasonAWe_0,
    _aPleasedWithYou_0,
    _aAnUpAndDownSea_0,
    _aCanImproveOnTh_0,
    _aNotAVeryGoodSe_0,
    _aImproveTheTeam,
    _aAVeryDisappoin_0,
    _aDemandsMuchBet,
    _aThanksForAllYo_0,
    _aYouWellInYourN_0,
    _aWelcomeToOurCl,
    _aMuchSuccessFro,
    _aWelcomeToOurCl_0,
    _aSeasonInManage_0,
    _aJustANoteToWis_0,
    _aNewSeason_0,
    _aWeAllHopeYouEn_0,
    _aBeforeHandingT_0,
    _aWeAllWishYouTh_0,
    _aDistinguishedC_0,
    _aPleaseNoteATha,
    _aBHasLeftTheClu,
    _aPleaseNoteATha_0,
    _aBHasLeftTheClu_0,
    _aPleaseNoteATha_1,
    _aHasJoinedTheCl,
    _aWellDoneAYouDi,
    _aThisSeasonAndW,
    _aAGoodSeasonAWe,
    _aPleasedWithYou,
    _aAnUpAndDownSea,
    _aCanImproveOnTh,
    _aNotAVeryGoodSe,
    _aImproveOnOurPe,
    _aAVeryDisappoin,
    _aMuchBetterFrom,
    _aThanksForAllYo,
    _aYouWellInYourN,
    _aWelcomeAWeLook,
    _aSuccessFromOur,
    _aWelcomeAWeHope,
    _aSeasonInManage,
    _aJustANoteToWis,
    _aNewSeason,
    _aWeAllHopeYouEn,
    _aBeforeHandingT,
    _aWeAllWishYouTh,
    _aDistinguishedC,
    _aItIsWithGreatR,
    _aRenewYourContr,
};
/* _flipStrTable: edit-tactics tactic-side flip selector. SWOS2.s:112717
 * (_flipOnOff already extern'd above as char[]; just take its address.) */
extern const char _aFlipOff[], _aFlipOn[];
extern char _aUSER_A[], _aUSER_B[], _aUSER_C[];
struct __attribute__((packed)) {
    void    *indexPtr; uint16_t base; void    *entries[5];
} _flipStrTable = {
    _flipOnOff, 0,
    {
        (void *)_aFlipOff, (void *)_aFlipOn,
        _aUSER_A,  _aUSER_B,
        _aUSER_C,
    }
};
void *_europeanCupsandNationalTeamCompetitionsAndQualifications[46] = {
    _worldCup,
    _europeanChampionships,
    _copaAmerica,
    _euroCup,
    _euroCupCopy,
    _cupWinnersCup,
    _cupWinnersCupCopy,
    _uefaCup,
    _uefaCupCopy,
    _copaAmericaCopy,
    _africanNationsCup,
    _africanNationsCupCopy,
    _northAmericanCup,
    _northAmericanCupCopy,
    _asianCup,
    _asianCupCopy,
    _oceaniaCup,
    _oceaniaCupCopy,
    _europeanChampionships2Copy,
    _ecQualifyingGroup1Copy,
    _ecQualifyingGroup2Copy,
    _ecQualifyingGroup3Copy,
    _ecQualifyingGroup4Copy,
    _ecQualifyingGroup5Copy,
    _ecQualifyingGroup6Copy,
    _ecQualifyingGroup7Copy,
    _ecQualifyingGroup8Copy,
    _worldCup2Copy,
    _worldCupQualifyingGroup1Copy,
    _worldCupQualifyingGroup2Copy,
    _worldCupQualifyingGroup3Copy,
    _worldCupQualifyingGroup4Copy,
    _worldCupQualifyingGroup5Copy,
    _worldCupQualifyingGroup6Copy,
    _worldCupQualifyingGroup7Copy,
    _worldCupQualifyingGroup8Copy,
    _northAmericaWorldCupQualificationCopy,
    _southAmericaWorldCupQualificationCopy,
    _africaWorldCupQualificationCopy,
    _asiaWorldCupQualificationCopy,
    _oceaniaWorldCupQualificationCopy,
    _invitationTournamentCopy,
    _tourCopy,
    _friendly,
    _friendlyCopy,
    (void *)(uintptr_t)0xFFFFFFFFu,
};
void *_nationalTeamCompetitionsAndQualifications1[39] = {
    _worldCup,
    _worldCup2Copy,
    _europeanChampionships,
    _europeanChampionships2Copy,
    _copaAmerica,
    _copaAmericaCopy,
    _africanNationsCup,
    _africanNationsCupCopy,
    _northAmericanCup,
    _northAmericanCupCopy,
    _asianCup,
    _asianCupCopy,
    _oceaniaCup,
    _oceaniaCupCopy,
    _friendlyCopy,
    _ecQualifyingGroup1Copy,
    _ecQualifyingGroup2Copy,
    _ecQualifyingGroup3Copy,
    _ecQualifyingGroup4Copy,
    _ecQualifyingGroup5Copy,
    _ecQualifyingGroup6Copy,
    _ecQualifyingGroup7Copy,
    _ecQualifyingGroup8Copy,
    _worldCupQualifyingGroup1Copy,
    _worldCupQualifyingGroup2Copy,
    _worldCupQualifyingGroup3Copy,
    _worldCupQualifyingGroup4Copy,
    _worldCupQualifyingGroup5Copy,
    _worldCupQualifyingGroup6Copy,
    _worldCupQualifyingGroup7Copy,
    _worldCupQualifyingGroup8Copy,
    _northAmericaWorldCupQualificationCopy,
    _southAmericaWorldCupQualificationCopy,
    _africaWorldCupQualificationCopy,
    _asiaWorldCupQualificationCopy,
    _oceaniaWorldCupQualificationCopy,
    _invitationTournamentCopy,
    _tourCopy,
    (void *)(uintptr_t)0xFFFFFFFFu,
};
/* Each Amiga euroCupQual* "group _seed list" table is a sequence of
 * 8-byte entries plus a 4-byte 0xFFFFFFFF terminator. Per SWOS2.s
 * (line 125058 etc.):
 *
 *   dc.l <seedListPtr>     ; 4 bytes BE pointer
 *   dc.b 0,sz,0,cnt        ; _size and copy-count as 16-bit BE words
 *   ... repeated ...
 *   dc.b $FF,$FF,$FF,$FF   ; terminator
 *
 * Career_FillRandomTeamListExcludingSelected reads (ptr, _size, count)
 * tuples and breaks on 0xFFFFFFFF. The previous SDL stub had only the
 * first ptr and no metadata or terminator, so the function read garbage
 * _size/count words, ran the inner shuffle on bogus indices, and SIGSEGV'd
 * walking off the end. Reconstruct the table as a packed C struct array
 * with terminator. Host-order writes work directly because the function
 * uses host uint16_t / uint32_t reads. */
typedef struct __attribute__((packed)) SeedListGroupEntry {
    void    *ptr;
    uint16_t _size;
    uint16_t count;
} SeedListGroupEntry;

SeedListGroupEntry _euroCupQualGroupSeedLists_4_6_11[5] = {
    { _euroCupQualSeedList4,  4,  4 },
    { _euroCupQualSeedList6,  6,  5 },
    { _euroCupQualSeedList11, 11, 5 },
    { _euroCupQualSeedList22, 22, 2 },
    { (void *)(uintptr_t)-1, 0, 0 },
};

SeedListGroupEntry _euroCupQualGroupSeedListsReduced_4_6_11[5] = {
    { _euroCupQualSeedListReduced4,  4,  4  },
    { _euroCupQualSeedListReduced6,  6,  6  },
    { _euroCupQualSeedListReduced11, 11, 11 },
    { _euroCupQualSeedListReduced22, 22, 11 },
    { (void *)(uintptr_t)-1, 0, 0 },
};

SeedListGroupEntry _euroCupQualGroupSeedBuckets_15_18_22[5] = {
    { _euroCupQualSeedBucketBlock.bucket15,     15, 10 },
    { _euroCupQualSeedBucketBlock.bucket18,     18, 10 },
    { _euroCupQualSeedBucketBlock.bucket22,     22, 8  },
    { _euroCupQualSeedBucketBlock.bucket22Full, 22, 4  },
    { (void *)(uintptr_t)-1, 0, 0 },
};
void *_positionsTable[18] = {
    _positionsDefault,
    _positions541,
    _positions451,
    _positions532,
    _positions352,
    _positions433,
    _positions424,
    _positions343,
    _positionsSweep,
    _positions523,
    _positionsAttack,
    _positionsDefend,
    _positionsDefault,
    _positionsDefault,
    _positionsDefault,
    _positionsDefault,
    _positionsDefault,
    _positionsDefault,
};
void *_divisionStringTable[4] = {
    _aPremierDivisio,
    _aDivision1_0,
    _aDivision2_0,
    _aDivision3_0,
};
void *_divStringTable[4] = {
    _aPremDiv,
    _aDiv1,
    _aDiv2,
    _aDiv3,
};
void *_cupRoundStringTable[12] = {
    _aFirstRound,
    _aSecondRound,
    _aThirdRound,
    _aFourthRound,
    _aFifthRound,
    _aSixthRound,
    _aQuarterFinal,
    _aSemiFinal,
    _aFinal_2,
    _aFinalGroup,
    _aThirdPlacePlay,
    _aFinalGroups,
};
void *_roundStringTable[12] = {
    _aRound1,
    _aRound2,
    _aRound3,
    _aRound4,
    _aRound5,
    _aRound6,
    _aQFinal,
    _aSFinal,
    _aFinal_3,
    _aFinalGroup_0,
    _a3rdPlayOff,
    _aFinalGroups_0,
};
void *_rndStringTable[15] = {
    _aRnd1,
    _aRnd2,
    _aRnd3,
    _aRnd4,
    _aRnd5,
    _aRnd6,
    _aQF,
    _aSF,
    _aFinal,
    _aFinal_0,
    _a3rd,
    _aFinal_1,
    _aReplay,
    _a2ndReplay,
    _a3rdReplay,
};
void *_replayStringTable[3] = {
    _aReplays,
    _a2ndReplays,
    _a3rdReplays,
};
void *_playerPositionsStringTable[20] = {
    _aG,
    _aRb,
    _aLb,
    _aD_0,
    _aRw,
    _aLw,
    _aM,
    _aA_1,
    _aEur,
    _aNam,
    _aSam,
    _aAsi,
    _aOce,
    _aAfr,
    _aEuro,
    _aNam_0,
    _aSam_0,
    _aAsi_0,
    _aOce_0,
    _aAfr_0,
};
void *_tacticsStringTable[18] = {
    _a442,
    _a541,
    _a451,
    _a532,
    _a352,
    _a433,
    _a424,
    _a343,
    _aSweep,
    _a523,
    _aAttack,
    _aDefend,
    _aUSER_A,
    _aUSER_B,
    _aUSER_C,
    _aUSER_D,
    _aUSER_E,
    _aUSER_F,
};
const uint8_t *_fixtureSchedulePtrTable[23] = {
    _fixtureSchedule2Teams,
    _fixtureSchedule3Teams,
    _fixtureSchedule4Teams,
    _fixtureSchedule5Teams,
    _fixtureSchedule6Teams,
    _fixtureSchedule7Teams,
    _fixtureSchedule8Teams,
    _fixtureSchedule9Teams,
    _fixtureSchedule10Teams,
    _fixtureSchedule11Teams,
    _fixtureSchedule12Teams,
    _fixtureSchedule13Teams,
    _fixtureSchedule14Teams,
    _fixtureSchedule15Teams,
    _fixtureSchedule16Teams,
    _fixtureSchedule17Teams,
    _fixtureSchedule18Teams,
    _fixtureSchedule19Teams,
    _fixtureSchedule20Teams,
    _fixtureSchedule21Teams,
    _fixtureSchedule22Teams,
    _fixtureSchedule23Teams,
    _fixtureSchedule24Teams,
};
void *_teamFilenamesTable[4] = {
    _aGrafsCjcteam1R,
    _aGrafsCjcteam2R,
    _aGrafsCjcteam1R,
    _aGrafsCjcteam3R,
};
