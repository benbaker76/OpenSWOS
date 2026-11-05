/*
 * competition_data.h — Extern declarations for competition data
 */
#ifndef COMPETITION_DATA_H
#define COMPETITION_DATA_H

#include "game_types.h"

/* ======================================================================
 * Major International Cups
 * ====================================================================== */

extern CompetitionTournament _euroCup;
extern CompetitionCup _cupWinnersCup;
extern CompetitionCup _uefaCup;

/* World Cup */
extern CompetitionTournament _worldCup2;
extern CompetitionTournament _worldCup;

/* World Cup Qualifying Groups */
extern CompetitionCup _worldCupQualifyingGroup1;
extern CompetitionCup _worldCupQualifyingGroup2;
extern CompetitionCup _worldCupQualifyingGroup3;
extern CompetitionCup _worldCupQualifyingGroup4;
extern CompetitionCup _worldCupQualifyingGroup5;
extern CompetitionCup _worldCupQualifyingGroup6;
extern CompetitionCup _worldCupQualifyingGroup7;
extern CompetitionCup _worldCupQualifyingGroup8;

/* World Cup Regional Qualifications */
extern CompetitionTournament _northAmericaWorldCupQualification;
extern CompetitionTournament _southAmericaWorldCupQualification;
extern CompetitionTournament _africaWorldCupQualification;
extern CompetitionTournament _asiaWorldCupQualification;
extern CompetitionCup _oceaniaWorldCupQualification;

/* Invitation / Tour / Friendly */
extern CompetitionCup _invitationTournament;
extern CompetitionCup _tour;
extern CompetitionCup _friendly;

/* ======================================================================
 * European Championships
 * ====================================================================== */

extern CompetitionTournament _europeanChampionships2;
extern CompetitionTournament _europeanChampionships;

/* EC Qualifying Groups */
extern CompetitionCup _ecQualifyingGroup1;
extern CompetitionCup _ecQualifyingGroup2;
extern CompetitionCup _ecQualifyingGroup3;
extern CompetitionCup _ecQualifyingGroup4;
extern CompetitionCup _ecQualifyingGroup5;
extern CompetitionCup _ecQualifyingGroup6;
extern CompetitionCup _ecQualifyingGroup7;
extern CompetitionCup _ecQualifyingGroup8;

/* ======================================================================
 * Continental Cups
 * ====================================================================== */

extern CompetitionTournament _copaAmerica;
extern CompetitionTournament _africanNationsCup;
extern CompetitionTournament _northAmericanCup;
extern CompetitionTournament _asianCup;
extern CompetitionCup _oceaniaCup;

/* ======================================================================
 * Customs League
 * ====================================================================== */

extern CompetitionLeague _customsLeague;

/* ======================================================================
 * National Leagues (1-division)
 * ====================================================================== */

extern CompetitionLeague _albanianLeague;
extern CompetitionLeague _austrianLeague;
extern CompetitionLeague _belarussiaLleague;
extern CompetitionLeague _belgianLeague;
extern CompetitionLeague _bulgarianLeague;
extern CompetitionLeague _croatianLeague;
extern CompetitionLeague _cypriotLeague;
extern CompetitionLeague _czechLeague;
extern CompetitionLeague _danishLeague;
extern CompetitionLeague _estonianLeague;
extern CompetitionLeague _faroeIslesLeague;
extern CompetitionLeague _finnishLeague;
extern CompetitionLeague _greekLeague;
extern CompetitionLeague _hungarianLeague;
extern CompetitionLeague _icelandicLeague;
extern CompetitionLeague _israeliLeague;
extern CompetitionLeague _latvianLeague;
extern CompetitionLeague _lithuanianLeague;
extern CompetitionLeague _luxembourgLeague;
extern CompetitionLeague _malteseLeague;
extern CompetitionLeague _northernIrishLeague;
extern CompetitionLeague _norwegianLeague;
extern CompetitionLeague _polishLeague;
extern CompetitionLeague _portugeseLeague;
extern CompetitionLeague _irishLeague;
extern CompetitionLeague _romanianLeague;
extern CompetitionLeague _russianLeague;
extern CompetitionLeague _sanMarinoLeague;
extern CompetitionLeague _slovakianLeague;
extern CompetitionLeague _slovenianLeague;
extern CompetitionLeague _swedishLeague;
extern CompetitionLeague _swissLeague;
extern CompetitionLeague _turkishLeague;
extern CompetitionLeague _ukrainianLeague;
extern CompetitionLeague _welshLeague;
extern CompetitionLeague _yugoslavianLeague;

/* Non-European 1-division leagues */
extern CompetitionLeague _algerianLeague;
extern CompetitionLeague _bolivianLeague;
extern CompetitionLeague _brasilianLeague;
extern CompetitionLeague _chileanLeague;
extern CompetitionLeague _colombianLeague;
extern CompetitionLeague _ecuadorLeague;
extern CompetitionLeague _indianLeague;
extern CompetitionLeague _paraguayanLeague;
extern CompetitionLeague _peruvianLeague;
extern CompetitionLeague _southAfricanLeague;
extern CompetitionLeague _surinamLeague;
extern CompetitionLeague _taiwaneseLeague;
extern CompetitionLeague _americanLeague;
extern CompetitionLeague _venezuelanLeague;
extern CompetitionLeague _ghanaLeague;

/* ======================================================================
 * Multi-division leagues
 * ====================================================================== */

extern CompetitionLeague _frenchLeague;
extern CompetitionLeague _germanLeague;
extern CompetitionLeague _italianLeague;
extern CompetitionLeague _japaneseLeague;
extern CompetitionLeague _argentinianLeague;
extern CompetitionLeague _mexicanLeague;
extern CompetitionLeague _elSalvadorLeague;
extern CompetitionLeague _australianLeague;
extern CompetitionLeague _newZealandLeague;

/* ======================================================================
 * Complex leagues with playoffs
 * ====================================================================== */

extern Playoffs _div1Playoffs;
extern Playoffs _div2Playoffs;
extern Playoffs _div3Playoffs;

/* England's leagues — standalone per-country globals (consolidated from the old
 * EnglishLeaguesBlock 2026-06-18 so England is uniform with every other country).
 * englishLeague = the 4-division league; englishLeagues1/2/3 = the per-division
 * promotion/relegation link blocks. Forward-declared here so their mutual
 * cross-references resolve inside competition_data.c. */
extern CompetitionLeague _englishLeague;
extern Leagues _englishLeagues1;
extern Leagues _englishLeagues2;
extern Leagues _englishLeagues3;

extern CompetitionLeague _dutchLeague;
extern Leagues _dutchLeagues1;
extern Playoffs _dutchPlayoffs;

extern CompetitionLeague _scottishLeague;
extern Leagues _scottishLeague1;
extern Playoffs _scottishPlayoffs;

extern CompetitionLeague _spanishLeague;
extern Leagues _spanishLeague1;
extern Playoffs _spanishPlayoffs;

extern CompetitionLeague _uruguayanLeague;
extern Leagues _uruguayanLeague1;
extern Playoffs _uruguayanPlayoffs;

/* ======================================================================
 * National Cups
 * ====================================================================== */

extern CompetitionCup _albanianCup;
extern CompetitionCup _austrianCup;
extern CompetitionCup _belarusianCup;
extern CompetitionCup _belgianCup;
extern CompetitionCup _bulgarianCup;
extern CompetitionCup _croatianCup;
extern CompetitionCup _cypriotCup;
extern CompetitionCup _czechCup;
extern CompetitionCup _danishCup;
extern CompetitionCup _estonianCup;
extern CompetitionCup _faroeIslesCup;
extern CompetitionCup _finnishCup;
extern CompetitionCup _greekCup;
extern CompetitionCup _hungarianCup;
extern CompetitionCup _icelandicCup;
extern CompetitionCup _irishCup;
extern CompetitionCup _israeliCup;
extern CompetitionCup _latvianCup;
extern CompetitionCup _lithuanianCup;
extern CompetitionCup _luxembourgCup;
extern CompetitionCup _malteseCup;
extern CompetitionCup _northernIrishCup;
extern CompetitionCup _norwegianCup;
extern CompetitionCup _polishCup;
extern CompetitionCup _portugeseCup;
extern CompetitionCup _romanianCup;
extern CompetitionCup _russianCup;
extern CompetitionCup _sanMarinoCup;
extern CompetitionCup _slovakianCup;
extern CompetitionCup _slovenianCup;
extern CompetitionCup _swedishCup;
extern CompetitionCup _swissCup;
extern CompetitionCup _turkishCup;
extern CompetitionCup _ukrainianCup;
extern CompetitionCup _welshCup;
extern CompetitionCup _yugoslavianCup;
extern CompetitionCup _algerianCup;
extern CompetitionCup _southAfricanCup;
extern CompetitionCup _taiwaneseCup;

/* Cups with names */
extern CompetitionCup _englishCup;
extern CompetitionCup _englishLeagueCup;
extern CompetitionCup _englishCup_1;
extern CompetitionCup _englishLeagueCup_1;
extern CompetitionCup _frenchCup;
extern CompetitionCup _germanCup;
extern CompetitionCup _italianCup;
extern CompetitionCup _scottishCup;
extern CompetitionCup _scottishLeagueCup;
extern CompetitionCup _spanishCup;
extern CompetitionCup _dutchCup;
extern CompetitionCup _australianCup;
extern CompetitionCup _japaneseCup;
extern CompetitionCup _japaneseEmperorsCup;

/* ======================================================================
 * Competition "Copy" variables (BSS, used at runtime for career/season)
 * ====================================================================== */

extern CompetitionTournament _euroCupCopy;
extern CompetitionCup _cupWinnersCupCopy;
extern CompetitionCup _uefaCupCopy;
extern CompetitionCup _invitationTournamentCopy;
extern CompetitionCup _tourCopy;
extern CompetitionCup _friendlyCopy;
extern CompetitionTournament _europeanChampionships2Copy;
extern CompetitionCup _ecQualifyingGroup1Copy;
extern CompetitionCup _ecQualifyingGroup2Copy;
extern CompetitionCup _ecQualifyingGroup3Copy;
extern CompetitionCup _ecQualifyingGroup4Copy;
extern CompetitionCup _ecQualifyingGroup5Copy;
extern CompetitionCup _ecQualifyingGroup6Copy;
extern CompetitionCup _ecQualifyingGroup7Copy;
extern CompetitionCup _ecQualifyingGroup8Copy;
extern CompetitionTournament _copaAmericaCopy;
extern CompetitionTournament _africanNationsCupCopy;
extern CompetitionTournament _northAmericanCupCopy;
extern CompetitionTournament _asianCupCopy;
extern CompetitionCup _oceaniaCupCopy;
extern CompetitionTournament _worldCup2Copy;
extern CompetitionCup _worldCupQualifyingGroup1Copy;
extern CompetitionCup _worldCupQualifyingGroup2Copy;
extern CompetitionCup _worldCupQualifyingGroup3Copy;
extern CompetitionCup _worldCupQualifyingGroup4Copy;
extern CompetitionCup _worldCupQualifyingGroup5Copy;
extern CompetitionCup _worldCupQualifyingGroup6Copy;
extern CompetitionCup _worldCupQualifyingGroup7Copy;
extern CompetitionCup _worldCupQualifyingGroup8Copy;
extern CompetitionTournament _northAmericaWorldCupQualificationCopy;
extern CompetitionTournament _southAmericaWorldCupQualificationCopy;
extern CompetitionTournament _africaWorldCupQualificationCopy;
extern CompetitionTournament _asiaWorldCupQualificationCopy;
extern CompetitionCup _oceaniaWorldCupQualificationCopy;

/* ======================================================================
 * Career Contests
 * ====================================================================== */

/* 34 compiled entries + sentinel + spare slots for runtime-appended
 * (dynamically-loaded) competitions — roadmap step 5/6. */
#define CAREER_CONTESTS_CAP 48
extern CompetitionCareer _careerContests[CAREER_CONTESTS_CAP];

/* Append a competition's {template, working-copy, sizeof(template)} triple to
 * _careerContests at runtime (used by the dynamic-competition loader). Writes the
 * entry over the current 0xFFFFFFFF sentinel and re-plants the sentinel one slot
 * later, so the sentinel-terminated InitCareer fill + save-reconstruct walks pick
 * it up. Returns 0 on success, -1 if the table is full. */
int Comp_AppendCareerContest(Competition *competitionTemplate,
                             Competition *competitionCopy, uint16_t templateSize);

/* Runtime compId -> Competition* registry (roadmap step 6/7). Dynamically-
 * loaded competitions register their heap record; Comp_ById resolves a saved
 * compId back to a live record (returns NULL if unregistered). */
void Comp_Register(Competition *c);
Competition *Comp_ById(uint8_t compId);

/* ======================================================================
 * Master Competition Pointer Arrays
 * ====================================================================== */

extern Competition *_nationalTeamCompetitionsAndQualifications[39];
extern Competition *_continentalCups[7];

/* ======================================================================
 * Qualifying Groups and Region Tables
 * ====================================================================== */

extern QualifyingGroup _worldCupQualifyingGroups;
extern QualifyingGroup _ecQualifyingGroups;
extern QualifyingGroup _customsTable;
extern QualifyingGroup _worldTable;
extern QualifyingGroup _europeTable;
extern QualifyingGroup _africaTable;
extern QualifyingGroup _southAmericaTable;
extern QualifyingGroup _northAmericaTable;
extern QualifyingGroup _asiaTable;
extern QualifyingGroup _oceaniaTable;

/* ======================================================================
 * Country Competition Tables
 * ====================================================================== */

extern Competition *_albaniaTable[4];
extern Competition *_austriaTable[4];
extern Competition *_belgiumTable[4];
extern Competition *_belarusTable[4];
extern Competition *_bulgariaTable[4];
extern Competition *_croatiaTable[4];
extern Competition *_cyprusTable[4];
extern Competition *_czechRepublicTable[4];
extern Competition *_denmarkTable[4];
extern Competition *_englandTable[5];
extern Competition *_estoniaTable[4];
extern Competition *_faroeIslandsTable[4];
extern Competition *_finlandTable[4];
extern Competition *_franceTable[4];
extern Competition *_germanyTable[4];
extern Competition *_greeceTable[4];
extern Competition *_hungaryTable[4];
extern Competition *_icelandTable[4];
extern Competition *_republicIrelandTable[4];
extern Competition *_israelTable[4];
extern Competition *_italyTable[5];
extern Competition *_latviaTable[4];
extern Competition *_lithuaniaTable[4];
extern Competition *_luxembourgTable[4];
extern Competition *_maltaTable[4];
extern Competition *_hollandTable[4];
extern Competition *_northernIrelandTable[4];
extern Competition *_norwayTable[4];
extern Competition *_polandTable[4];
extern Competition *_portugalTable[4];
extern Competition *_romaniaTable[4];
extern Competition *_russiaTable[4];
extern Competition *_sanMarinoTable[4];
extern Competition *_scotlandTable[5];
extern Competition *_slovakiaTable[4];
extern Competition *_sloveniaTable[4];
extern Competition *_spainTable[4];
extern Competition *_swedenTable[4];
extern Competition *_switzerlandTable[4];
extern Competition *_turkeyTable[4];
extern Competition *_ukraineTable[4];
extern Competition *_walesTable[4];
extern Competition *_yugoslaviaTable[4];
extern Competition *_algeriaTable[4];
extern Competition *_argentinaTable[3];
extern Competition *_australiaTable[4];
extern Competition *_boliviaTable[3];
extern Competition *_brasilTable[3];
extern Competition *_chileTable[3];
extern Competition *_colombiaTable[3];
extern Competition *_ecuadorTable[3];
extern Competition *_elSalvadorTable[3];
extern Competition *_indiaTable[3];
extern Competition *_japanTable[5];
extern Competition *_mexicoTable[3];
extern Competition *_newZealandTable[3];
extern Competition *_paraguayTable[3];
extern Competition *_peruTable[3];
extern Competition *_southAfricaTable[4];
extern Competition *_surinamTable[3];
extern Competition *_taiwanTable[4];
extern Competition *_uruguayTable[3];
extern Competition *_usaTable[3];
extern Competition *_venezuelaTable[3];
extern Competition *_ghanaTable[3];

/* ======================================================================
 * Season flags and scheduling data
 * ====================================================================== */

extern int16_t _seasonCycleNationalFlagsA[5];
extern uint16_t _seasonCycleNationalFlagsB[5];
extern uint8_t _careerTeamFlagsBySeason0[20];
extern uint8_t _careerTeamFlagsBySeason1[20];
extern uint32_t _nationalSelectorSeasonMaskA;
extern uint32_t _nationalSelectorSeasonMaskB;

/* ======================================================================
 * Countries list and career remap
 * ====================================================================== */

extern CountryNumbers _allCountriesList[66];
extern uint8_t _careerCompetitionIdRemapTable[66];

/* ======================================================================
 * Euro Cup Qualifying Seed Lists
 * ====================================================================== */

extern uint8_t _euroCupQualSeedList4[4];
extern uint8_t _euroCupQualSeedList6[6];
extern uint8_t _euroCupQualSeedList11[11];
extern uint8_t _euroCupQualSeedList22[23];
extern uint8_t _euroCupQualSeedListReduced4[4];
extern uint8_t _euroCupQualSeedListReduced6[6];
extern uint8_t _euroCupQualSeedListReduced11[11];
extern uint8_t _euroCupQualSeedListReduced22[23];
/* euroCupQualSeedBucket15/18/22/22Full: untangled into EuroCupQualSeedBucketBlock
 * fields (.bucket15/.bucket18/.bucket22/.bucket22Full) — typedef in game_types.h,
 * definition in competition_data.c. */

/* ======================================================================
 * Cup Pick Tables (per-region competition selection for career)
 * ====================================================================== */

extern Competition *_cupPickOceaniaInviteFriendly[5];
extern Competition *_cupPickOceaniaOceaniaCupFriendly[5];
extern Competition *_cupPickOceaniaWorldCupFriendly[5];
extern Competition *_cupPickOceaniaWorldCupInvite[5];
extern Competition *_cupPickOceaniaOceaniaCupNonFinalist[5];
extern Competition *_cupPickOceaniaOceaniaCupFinalist[5];

extern Competition *_cupPickNAmericaInviteFriendly[5];
extern Competition *_cupPickNAmericaNaCupFriendly[5];
extern Competition *_cupPickNAmericaWorldCupFriendly[5];
extern Competition *_cupPickNAmericaWorldCupInvite[5];
extern Competition *_cupPickNAmericaNaCupNonFinalist[5];
extern Competition *_cupPickNAmericaNaCupFinalist[5];

extern Competition *_cupPickAsiaInviteFriendly[5];
extern Competition *_cupPickAsiaAsianCupFriendly[5];
extern Competition *_cupPickAsiaWorldCupFriendly[5];
extern Competition *_cupPickAsiaWorldCupInvite[5];
extern Competition *_cupPickAsiaAsianCupNonFinalist[5];
extern Competition *_cupPickAsiaAsianCupFinalist[5];

extern Competition *_cupPickAfricaInviteFriendly[5];
extern Competition *_cupPickAfricaAfricanCupFriendly[5];
extern Competition *_cupPickAfricaWorldCupFriendly[5];
extern Competition *_cupPickAfricaWorldCupInvite[5];
extern Competition *_cupPickAfricaAfricanCupNonFinalist[5];
extern Competition *_cupPickAfricaAfricanCupFinalist[5];

extern Competition *_cupPickSAmericaInviteFriendly[5];
extern Competition *_cupPickSAmericaCopaAmericaFriendly[5];
extern Competition *_cupPickSAmericaWorldCupFriendly[5];
extern Competition *_cupPickSAmericaWorldCupInvite[5];
extern Competition *_cupPickSAmericaCopaAmericaNonFinalist[5];
extern Competition *_cupPickSAmericaCopaAmericaFinalist[5];

extern Competition *_ecQualifyingGroupsTable[8];
extern Competition *_cupPickEuropeNoCupFriendly[5];
extern Competition *_cupPickListEuropeInviteTournamentFriendly[5];
extern Competition *_cupPickLisEuropeEuroChampsFriendly[5];
extern Competition *_cupPickListEuropeEuroChampsInviteFriendly[5];
extern Competition *_cupPickListEuropeWorldCupFriendly[5];
extern Competition *_cupPickLisEuropeWorldCupInviteFriendly[5];
extern Competition *_cupPickEuropeNoCupFriendlyAlt[5];
extern Competition *_cupPickListEuropeInviteTournamentFriendlyAlt[5];
extern Competition *_worldCupQualifyingGroupsTable[8];

/* Tour string table */
extern char *_tourStringTable[6];

/* Career region weights */
extern uint8_t _careerRegionPickWeightsTable[32];
extern uint8_t _careerRegionPickWeightsTable2[32];

/* ======================================================================
 * DIY Cup Choice Variables (zero-init)
 * ====================================================================== */

extern uint16_t _diyCupLegsChoice0;
extern uint16_t _diyCupExtraTimeChoice0;
extern uint16_t _diyCupPenaltiesChoice0;
extern uint16_t _diyCupLegsChoice1;
extern uint16_t _diyCupExtraTimeChoice1;
extern uint16_t _diyCupPenaltiesChoice1;
extern uint16_t _diyCupLegsChoice2;
extern uint16_t _diyCupExtraTimeChoice2;
extern uint16_t _diyCupPenaltiesChoice2;
extern uint16_t _diyCupLegsChoice3;
extern uint16_t _diyCupExtraTimeChoice3;
extern uint16_t _diyCupPenaltiesChoice3;
extern uint16_t _diyCupLegsChoice4;
extern uint16_t _diyCupExtraTimeChoice4;
extern uint16_t _diyCupPenaltiesChoice4;
extern uint16_t _diyCupLegsChoice5;
extern uint16_t _diyCupExtraTimeChoice5;
extern uint16_t _diyCupPenaltiesChoice5;

extern uint16_t _diyEditParamB;
extern uint16_t _diyEditParamC;

#endif /* COMPETITION_DATA_H */
