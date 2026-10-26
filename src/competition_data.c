/*
 * data/competition_data.c — Competition data for SWOS C port
 *
 * All competition definitions extracted from data/Data.c.
 * This file is NOT part of the build yet — it lives in data/ for preparation.
 * When ready, move to src/ and add to the Makefile.
 *
 * String pointers (e.g., aEuropeanChampi_1) are extern references to
 * string constants defined elsewhere. They will need proper extern
 * declarations or a shared header when this file joins the build.
 */
#include "game_types.h"
#include "swos_types.h"
#include "swos_platform.h"  /* _ASM_PFX */
#include "competition_data.h"
#include "comp_identity.h"  /* comp_is_type0_cup — type-0 cup name-table routing */

/* ======================================================================
 * Polymorphic competition accessors (untangle Step 4b — declared in game_types.h).
 * Switch on comp.type (+ comp.isPlayoff to split the two type==1 layouts,
 * CompetitionCup vs Playoffs) and return the concrete struct's field directly.
 * Typed field access is automatically pointer-width-correct on 32/64-bit — no
 * self-offset bytes, no name[2]-growth delta math. The former nameTableOffset /
 * teamListOffset bytes (Competition byte 5 / 7) are now inert.
 * ====================================================================== */
char **comp_name_table(Competition *c)
{
    char **nm;
    switch (c->type) {
    case 0:
        /* Real leagues use CompetitionLeague.name (offset 38). BUT invitation/
         * tour/friendly are authored type==0 while physically CompetitionCup
         * (name[] at offset 20, teamSeedList at 36) — reading them as a league
         * lands name[] on teamSeedList[1], which is a valid team-id-as-pointer
         * once the national-coach season fills the draw list -> StringCopy
         * SIGSEGV. Route those three to the cup name field. */
        nm = comp_is_type0_cup(c->compId) ? ((CompetitionCup *)c)->name
                                          : ((CompetitionLeague *)c)->name;
        break;
    case 1:  nm = c->isPlayoff ? ((Playoffs *)c)->name
                               : ((CompetitionCup *)c)->name; break;      /* playoff / cup */
    case 2:  nm = ((CompetitionTournament *)c)->name; break;             /* tournament */
    default: return (char **)0;
    }
    /* An empty table (name[0] == NULL) means the record uses default names;
       callers substitute divisionStringTable / aCup_0 / aLeague_1. */
    return nm[0] ? nm : (char **)0;
}

TeamId *comp_team_seed_list(Competition *c)
{
    switch (c->type) {
    case 1:  return c->isPlayoff ? (TeamId *)((Playoffs *)c)->qualifiedTeamIds
                                 : ((CompetitionCup *)c)->teamSeedList;   /* playoff / cup */
    case 2:  return ((CompetitionTournament *)c)->teamSeedList;          /* tournament */
    default: return (TeamId *)0;     /* league (type 0): no embedded draw list */
    }
}

/* ======================================================================
 * Forward declarations — strings defined in assembly/Data.c
 * These will need proper externs when integrated into the build.
 * ====================================================================== */

/* Competition name strings */
extern char _aEuropeanChampi_1[];
extern char _aEuropeanCup[];
extern char _aEuropeanCupWin[];
extern char _aEuroCwCup[];
extern char _aEufaCup[];
extern char _aEufaCup_0[];
extern char _aWorldCup[];
extern char _aWorldCup_0[];
extern char _aWorldCupQualif_1[];
extern char _aWorldCupQualif_2[];
extern char _aWorldCupQualif_3[];
extern char _aWorldCupQualif_4[];
extern char _aWorldCupQualif_5[];
extern char _aWorldCupQualif_6[];
extern char _aWorldCupQualif_7[];
extern char _aWorldCupQualif_8[];
extern char _aWorldCupQualif_9[];
extern char _aWCQualifier_0[];
extern char _aInvitationTour[];
extern char _aInvTournament[];
extern char _aTour[];
extern char _aFriendly[];
extern char _aFriendly_0[];
extern char _aEuropeanChampi_0[];
extern char _aECFinals[];
extern char _aECQualifyingGr[];
extern char _aECQualifyingGr_0[];
extern char _aECQualifyingGr_1[];
extern char _aECQualifyingGr_2[];
extern char _aECQualifyingGr_3[];
extern char _aECQualifyingGr_4[];
extern char _aECQualifyingGr_5[];
extern char _aECQualifyingGr_6[];
extern char _aECQualifier_0[];
extern char _aCopaAmerica[];
extern char _aCopaAmerica_0[];
extern char _aAfricanNations[];
extern char _aAfricanCup[];
extern char _aNorthAmericanC[];
extern char _aNAmericanCup[];
extern char _aAsianCup[];
extern char _aAsianCup_0[];
extern char _aOceaniaCup[];
extern char _aOceaniaCup_0[];
extern char _aEuropeanTour[];
extern char _aAfricanTour[];
extern char _aSouthAmericanT[];
extern char _aNorthAmericanT[];
extern char _aAsianTour[];
extern char _aOceaniaTour[];

/* League division name strings */
extern char _aDivision1[];
extern char _aDivision2[];
extern char _aDivision3[];
extern char _aPremierLeague[];
extern char _aPremier[];
extern char _aDivisionOne[];
extern char _aDivisionTwo[];
extern char _aDivisionThree[];
extern char _aPremiereDivisi[];
extern char _aPremiere[];
extern char _aDeuxiemeDivisi[];
extern char _aDeuxieme[];
extern char _a1Liga[];
extern char _a2Liga[];
extern char _aSerieA[];
extern char _aSerieB[];
extern char _aPrimeraDivisio[];
extern char _aPrimera[];
extern char _aSegundaDivisio[];
extern char _aSegunda[];
extern char _a1stDivision[];
extern char _aSouthDivision[];
extern char _aSouthDivision_0[];
extern char _aNswDivision[];
extern char _aNsw[];
extern char _aQueenslandDivi[];
extern char _aQueensland[];
extern char _aJLeague[];
extern char _aJLeague_0[];
extern char _aLeague_2[];
extern char _aLeague_3[];
extern char _aNorthernRegion[];
extern char _aNorthern[];
extern char _aCentralRegion[];
extern char _aCentral[];
extern char _aSouthernRegion[];
extern char _aSouthern[];

/* Playoff/cup name strings */
extern char _aDiv1Playoffs[];
extern char _aDiv2Playoffs[];
extern char _aDiv3Playoffs[];
extern char _aPlayoffs[];
extern char _aCup_4[];
extern char _aCup_5[];
extern char _aLeagueCup[];
extern char _aLeagueCup_0[];
extern char _aCup_6[];
extern char _aCup_7[];
extern char _aLeagueCup_1[];
extern char _aLeagueCup_2[];
extern char _aCoupeDeFrance[];
extern char _aCoupe[];
extern char _aDFBPokal[];
extern char _aPokal[];
extern char _aCoppaItalia[];
extern char _aCoppa[];
extern char _aCopa[];
extern char _aCopa_0[];
extern char _aCup_8[];
extern char _aCup_9[];
extern char _aEmperorSCup[];
extern char _aEmpCup[];

/* TMD file paths */
extern const char _aDataEurocupTmd[];
extern const char _aDataEurocwcTmd[];
extern const char _aDataUefacupTmd[];

#if COMPETITION_DATA_C

/* Forward declarations for variables used before definition */
extern Playoffs _div1Playoffs, _div2Playoffs, _div3Playoffs;
/* englishLeague + englishLeagues1/2/3: standalone per-country globals (extern in
 * competition_data.h), consolidated from the old EnglishLeaguesBlock. */
extern Playoffs _dutchPlayoffs;
extern Leagues _dutchLeagues1;
extern Leagues _scottishLeague1;
extern Playoffs _scottishPlayoffs;
extern Leagues _spanishLeague1;
extern Playoffs _spanishPlayoffs;
extern Leagues _uruguayanLeague1;
extern Playoffs _uruguayanPlayoffs;

/* ======================================================================
 * Major International Cups
 * ====================================================================== */

CompetitionTournament _euroCup = {
    { { 0, 2, -1, 64, 32, 35, 0, 41, 1 }, 1, 1, 2, 3, 53, 4,
      { { 16, 4, 4 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 0, 0 }, { 0, 0, 0 } }, 0 },
    { 0, -108, -108, 20 },
    { _aEuropeanChampi_1, _aEuropeanCup },
    {
        { CN_FRANCE, 1 /* AUXERRE */ },
        { CN_HOLLAND, 0 /* AJAX */ },
        { CN_SWITZERLAND, 5 /* GRASSHOPPERS */ },
        { CN_SCOTLAND, 33 /* RANGERS */ },
        { CN_SPAIN, 6 /* ATLETICO MADRID */ },
        { CN_ROMANIA, 18 /* STEAUA BUCHAREST */ },
        { CN_GERMANY, 4 /* BOR. DORTMUND */ },
        { CN_POLAND, 23 /* WIDZEW LODZ */ },
        { CN_AUSTRIA, 5 /* RAPID VIENNA */ },
        { CN_TURKEY, 9 /* FENERBAHCE */ },
        { CN_ITALY, 22 /* JUVENTUS */ },
        { CN_ENGLAND, 20 /* MANCHESTER UTD */ },
        { CN_SWEDEN, 8 /* IFK GOTHENBURG */ },
        { CN_NORWAY, 11 /* ROSENBORG */ },
        { CN_ITALY, 0 /* AC MILAN */ },
        { CN_PORTUGAL, 12 /* FC PORTO */ },
    }
};

CompetitionCup _cupWinnersCup = {
    { { 1, 1, -1, 64, 32, 15, 0, 21, 1 }, 0, 32, 1, 53, 1 },
    { -108, -108, -108, -108, 20, 0 },
    { _aEuropeanCupWin, _aEuroCwCup },
    {
        { CN_GREECE, 0 /* AEK ATHENS */ },
        { CN_SLOVAKIA, 2 /* CHEMLON HUMENNE */ },
        { CN_DENMARK, 2 /* AGF */ },
        { CN_SLOVENIA, 11 /* OLIMPIJA L'JANA */ },
        { CN_SPAIN, 8 /* BARCELONA */ },
        { CN_CYPRUS, 0 /* AEK LARNACA */ },
        { CN_PORTUGAL, 2 /* BENFICA */ },
        { CN_WALES, 16 /* LLANSANTFFRAID */ },
        { CN_ICELAND, 8 /* KR */ },
        { CN_SWEDEN, 0 /* AIK */ },
        { CN_BELGIUM, 2 /* CERCLE BRUGES */ },
        { CN_NORWAY, 1 /* BRANN */ },
        { CN_ISRAEL, 15 /* RISHON LEZION */ },
        { CN_TURKEY, 10 /* GALATASARAY */ },
        { CN_FAROE_ISLANDS, 4 /* HB */ },
        { CN_HOLLAND, 21 /* PSV */ },
        { CN_ROMANIA, 8 /* GLORIA BISTRITA */ },
        { CN_ITALY, 18 /* FIORENTINA */ },
        { CN_GERMANY, 22 /* KAISERSLAUTERN */ },
        { CN_YUGOSLAVIA, 7 /* RED STAR B'GRADE */ },
        { CN_RUSSIA, 10 /* LOKOMOTIV MOSCOW */ },
        { CN_CROATIA, 11 /* VARTEKS VARAZDIN */ },
        { CN_FINLAND, 8 /* MYPA */ },
        { CN_ENGLAND, 18 /* LIVERPOOL */ },
        { CN_BULGARIA, 6 /* LEVSKI SOFIA */ },
        { CN_HUNGARY, 8 /* KISPEST-HONVED */ },
        { CN_SWITZERLAND, 12 /* SION */ },
        { CN_UKRAINE, 10 /* NIVA VINNITSA */ },
        { CN_AUSTRIA, 7 /* STURM GRAZ */ },
        { CN_CZECH_REPUBLIC, 10 /* SPARTA PRAGUE */ },
        { CN_LATVIA, 6 /* RAF YELGAVA */ },
        { CN_FRANCE, 30 /* PARIS ST-GERMAIN */ },
    }
};

CompetitionCup _uefaCup = {
    { { 2, 1, -1, 64, 32, 15, 0, 21, 1 }, 0, 32, 1, 53, 1 },
    { -108, -108, -108, -108, 20, 0 },
    { _aEufaCup, _aEufaCup_0 },
    {
        { CN_RUSSIA, 19 /* TORPEDO MOSCOW */ },
        { CN_PORTUGAL, 3 /* BOAVISTA */ },
        { CN_POLAND, 7 /* LEGIA WARSAW */ },
        { CN_TURKEY, 5 /* BESIKTAS */ },
        { CN_CZECH_REPUBLIC, 8 /* SLAVIA PRAGUE */ },
        { CN_SPAIN, 44 /* VALENCIA */ },
        { CN_PORTUGAL, 24 /* VIT. GUIMARAES */ },
        { CN_BELGIUM, 0 /* ANDERLECHT */ },
        { CN_GERMANY, 23 /* KARLSRUHE */ },
        { CN_ITALY, 40 /* ROMA */ },
        { CN_SWEDEN, 7 /* HELSINGBORG */ },
        { CN_SWITZERLAND, 10 /* NEUCHATEL XAMAX */ },
        { CN_SPAIN, 19 /* ESPANYOL */ },
        { CN_HOLLAND, 10 /* FEYENOORD */ },
        { CN_BELGIUM, 4 /* CLUB BRUGES */ },
        { CN_ROMANIA, 10 /* NATIONAL */ },
        { CN_ITALY, 21 /* INTER MILAN */ },
        { CN_AUSTRIA, 2 /* GRAZER AK */ },
        { CN_SCOTLAND, 0 /* ABERDEEN */ },
        { CN_DENMARK, 3 /* BRONDBY */ },
        { CN_HUNGARY, 5 /* FERENCVAROS */ },
        { CN_ENGLAND, 22 /* NEWCASTLE UNITED */ },
        { CN_GERMANY, 30 /* SCHALKE */ },
        { CN_TURKEY, 21 /* TRABZONSPOR */ },
        { CN_ITALY, 23 /* LAZIO */ },
        { CN_SPAIN, 42 /* TENERIFE */ },
        { CN_GERMANY, 17 /* HAMBURG */ },
        { CN_RUSSIA, 16 /* SPARTAK MOSCOW */ },
        { CN_FRANCE, 22 /* METZ */ },
        { CN_PORTUGAL, 20 /* SPORTING LISBON */ },
        { CN_FRANCE, 23 /* MONACO */ },
        { CN_GERMANY, 25 /* MONCHENGLADBACH */ },
    }
};

/* ======================================================================
 * World Cup
 * ====================================================================== */

CompetitionTournament _worldCup2 = {
    { { 3, 2, -1, -120, -112, 35, 0, 41, 0 }, 1, 0, 1, 3, 53, 6,
      { { 32, 8, 4 }, { 16, 0, 16 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, -1, 2 }, { 2, 0, 2 } }, 1 },
    { 0, 20, 20, 20, 20, 20 },
    { _aWorldCup, _aWorldCup_0 },
    { }
};

CompetitionTournament _worldCup = {
    { { 4, 2, -1, -120, -112, 35, 0, 41, 1 }, 0, 0, 1, 3, 53, 6,
      { { 24, 6, 4 }, { 16, 0, 16 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, -1, 2 }, { 2, 0, 2 } }, 1 },
    { 0, 20, 20, 20, 20, 20 },
    { _aWorldCup, _aWorldCup_0 },
    {
        { CN_EUROPE, 36 /* ROMANIA */ },
        { CN_SOUTH_AMERICA, 4 /* COLOMBIA */ },
        { CN_NORTH_AMERICA, 13 /* TRINIDAD-TOBAGO */ },
        { CN_EUROPE, 44 /* SWITZERLAND */ },
        { CN_SOUTH_AMERICA, 2 /* BRAZIL */ },
        { CN_EUROPE, 37 /* RUSSIA */ },
        { CN_AFRICA, 6 /* CAMEROON */ },
        { CN_EUROPE, 43 /* SWEDEN */ },
        { CN_EUROPE, 17 /* GERMANY */ },
        { CN_SOUTH_AMERICA, 1 /* BOLIVIA */ },
        { CN_EUROPE, 42 /* SPAIN */ },
        { CN_ASIA, 24 /* SOUTH KOREA */ },
        { CN_SOUTH_AMERICA, 0 /* ARGENTINA */ },
        { CN_EUROPE, 18 /* GREECE */ },
        { CN_AFRICA, 22 /* NIGERIA */ },
        { CN_EUROPE, 6 /* BULGARIA */ },
        { CN_EUROPE, 23 /* ITALY */ },
        { CN_EUROPE, 35 /* REPUBLIC IRELAND */ },
        { CN_EUROPE, 32 /* NORWAY */ },
        { CN_NORTH_AMERICA, 8 /* MEXICO */ },
        { CN_EUROPE, 5 /* BELGIUM */ },
        { CN_AFRICA, 19 /* MOROCCO */ },
        { CN_EUROPE, 19 /* HOLLAND */ },
        { CN_ASIA, 22 /* SAUDI ARABIA */ },
    }
};

/* ======================================================================
 * World Cup Qualifying Groups
 * ====================================================================== */

CompetitionCup _worldCupQualifyingGroup1 = {
    { { 5, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_2, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup2 = {
    { { 6, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_3, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup3 = {
    { { 7, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_4, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup4 = {
    { { 8, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_5, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup5 = {
    { { 9, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_6, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup6 = {
    { { 10, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_7, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup7 = {
    { { 11, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_8, _aWCQualifier_0 },
    { }
};

CompetitionCup _worldCupQualifyingGroup8 = {
    { { 12, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_9, _aWCQualifier_0 },
    { }
};

/* ======================================================================
 * World Cup Regional Qualifications
 * ====================================================================== */

CompetitionTournament _northAmericaWorldCupQualification = {
    { { 13, 2, 83, 72, 16, 35, 0, 41, 0 }, 1, 0, 2, 3, 53, 2,
      { { 12, 0, 12 }, { 6, 1, 6 }, { 3, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { -108, 20, 20, 20, 20, 20 },
    { _aWorldCupQualif_1, _aWCQualifier_0 },
    { }
};

CompetitionTournament _southAmericaWorldCupQualification = {
    { { 14, 2, 82, 72, 16, 35, 0, 41, 0 }, 1, 0, 2, 3, 53, 1,
      { { 8, 1, 8 }, { 4, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { 20, 20, 20, 20, 20, 20 },
    { _aWorldCupQualif_1, _aWCQualifier_0 },
    { }
};

CompetitionTournament _africaWorldCupQualification = {
    { { 15, 2, 81, 72, 16, 35, 0, 41, 0 }, 1, 0, 2, 3, 53, 3,
      { { 32, 0, 32 }, { 16, 0, 16 }, { 8, 1, 8 }, { 5, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { -108, -108, 20, 20, 20, 20 },
    { _aWorldCupQualif_1, _aWCQualifier_0 },
    { }
};

CompetitionTournament _asiaWorldCupQualification = {
    { { 16, 2, 84, 72, 16, 35, 0, 41, 0 }, 1, 0, 2, 3, 53, 3,
      { { 24, 0, 24 }, { 12, 0, 12 }, { 6, 1, 6 }, { 2, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { -108, -108, 20, 20, 20, 20 },
    { _aWorldCupQualif_1, _aWCQualifier_0 },
    { }
};

CompetitionCup _oceaniaWorldCupQualification = {
    { { 17, 0, 85, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 4 },
    { 1, 0, 0, 0, 0, 0 },
    { _aWorldCupQualif_1, _aWCQualifier_0 },
    { }
};

/* ======================================================================
 * Invitation / Tour / Friendly
 * ====================================================================== */

CompetitionCup _invitationTournament = {
    { { 18, 0, -1, -104, -104, 15, 0, 21, 0 }, 1, 1, 3, 53, 4 },
    { 0 },
    { _aInvitationTour, _aInvTournament },
    { }
};

CompetitionCup _tour = {
    { { 19, 0, -1, -104, -104, 15, 0, 21, 0 }, 1, -1, 3, 53, 4 },
    { 0 },
    { 0, _aTour },
    { }
};

CompetitionCup _friendly = {
    { { 20, 0, -1, 56, 0, 15, 0, 21, 0 }, 1, -1, 3, 53, 6 },
    { 0 },
    { _aFriendly, _aFriendly_0 },
    { }
};

/* ======================================================================
 * European Championships
 * ====================================================================== */

CompetitionTournament _europeanChampionships2 = {
    { { 21, 2, 80, -120, -120, 35, 0, 41, 0 }, 1, 0, 1, 3, 53, 4,
      { { 16, 4, 4 }, { 8, 0, 0 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { 0, 20, 20, 20 },
    { _aEuropeanChampi_0, _aECFinals },
    { }
};

CompetitionTournament _europeanChampionships = {
    { { 22, 2, 80, -120, -120, 35, 0, 41, 1 }, 1, 0, 1, 3, 53, 4,
      { { 16, 4, 4 }, { 8, 0, 0 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { 0, 20, 20, 20 },
    { _aEuropeanChampi_0, _aECFinals },
    {
        { CN_EUROPE, 11 /* ENGLAND */ },
        { CN_EUROPE, 44 /* SWITZERLAND */ },
        { CN_EUROPE, 19 /* HOLLAND */ },
        { CN_EUROPE, 39 /* SCOTLAND */ },
        { CN_EUROPE, 42 /* SPAIN */ },
        { CN_EUROPE, 6 /* BULGARIA */ },
        { CN_EUROPE, 36 /* ROMANIA */ },
        { CN_EUROPE, 15 /* FRANCE */ },
        { CN_EUROPE, 17 /* GERMANY */ },
        { CN_EUROPE, 9 /* CZECH REPUBLIC */ },
        { CN_EUROPE, 23 /* ITALY */ },
        { CN_EUROPE, 37 /* RUSSIA */ },
        { CN_EUROPE, 10 /* DENMARK */ },
        { CN_EUROPE, 34 /* PORTUGAL */ },
        { CN_EUROPE, 45 /* TURKEY */ },
        { CN_EUROPE, 7 /* CROATIA */ },
    }
};

/* ======================================================================
 * EC Qualifying Groups
 * ====================================================================== */

CompetitionCup _ecQualifyingGroup1 = {
    { { 23, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup2 = {
    { { 24, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_0, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup3 = {
    { { 25, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_1, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup4 = {
    { { 26, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_2, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup5 = {
    { { 27, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_3, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup6 = {
    { { 28, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_4, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup7 = {
    { { 29, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 6 },
    { 2, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_5, _aECQualifier_0 },
    { }
};

CompetitionCup _ecQualifyingGroup8 = {
    { { 30, 0, 80, 72, 16, 15, 0, 21, 0 }, 1, 2, 3, 53, 4 },
    { 1, 0, 0, 0, 0, 0 },
    { _aECQualifyingGr_6, _aECQualifier_0 },
    { }
};

/* ======================================================================
 * Continental Cups
 * ====================================================================== */

CompetitionTournament _copaAmerica = {
    { { 31, 2, 82, -120, -112, 35, 0, 41, 0 }, 1, 0, 1, 3, 53, 4,
      { { 12, 3, 4 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { 0, 4, 4, 4 },
    { _aCopaAmerica, _aCopaAmerica_0 },
    {
        { CN_NORTH_AMERICA, 8 /* MEXICO */ },
        { CN_NORTH_AMERICA, 13 /* TRINIDAD-TOBAGO */ },
        { CN_SOUTH_AMERICA, 8 /* URUGUAY */ },
        { CN_SOUTH_AMERICA, 9 /* VENEZUELA */ },
        { CN_SOUTH_AMERICA, 0 /* ARGENTINA */ },
        { CN_SOUTH_AMERICA, 1 /* BOLIVIA */ },
        { CN_SOUTH_AMERICA, 2 /* BRAZIL */ },
        { CN_SOUTH_AMERICA, 3 /* CHILE */ },
        { CN_SOUTH_AMERICA, 4 /* COLOMBIA */ },
        { CN_SOUTH_AMERICA, 5 /* ECUADOR */ },
        { CN_SOUTH_AMERICA, 6 /* PARAGUAY */ },
        { CN_SOUTH_AMERICA, 7 /* PERU */ },
    }
};

CompetitionTournament _africanNationsCup = {
    { { 32, 2, 81, -120, -112, 35, 0, 0, 0 }, 1, 0, 1, 3, 53, 5,
      { { 32, 8, 4 }, { 16, 0, 16 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 } }, 1 },
    { 0, 20, 20, 20, 20, 0 },
    { _aAfricanNations, _aAfricanCup },
    { }
};

CompetitionTournament _northAmericanCup = {
    { { 33, 2, 83, -120, -112, 35, 0, 0, 0 }, 1, 0, 1, 3, 53, 4,
      { { 12, 3, 4 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 }, { 1, 1, 1 } }, 1 },
    { 0, 20, 20, 20 },
    { _aNorthAmericanC, _aNAmericanCup },
    { }
};

CompetitionTournament _asianCup = {
    { { 34, 2, 84, -120, -112, 35, 0, 0, 0 }, 1, 0, 1, 3, 53, 5,
      { { 30, 5, 6 }, { 16, 0, 16 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 }, { 1, 1, 1 } }, 1 },
    { 0, 20, 20, 20, 20, 0 },
    { _aAsianCup, _aAsianCup_0 },
    { }
};

CompetitionCup _oceaniaCup = {
    { { 35, 0, 85, -120, -112, 15, 0, 0, 0 }, 1, 1, 3, 53, 4 },
    { 0 },
    { _aOceaniaCup, _aOceaniaCup_0 },
    { }
};

/* ======================================================================
 * Customs League
 * ====================================================================== */

CompetitionLeague _customsLeague = {
    { { 36, 0, 72, 64, 32, 33, 0, 0, 0 }, 3, 2, 3, 53 },
    { { 20, 0, 0, 0, 0, 0 }, { 22, 0, 0, 0, 0, 0 }, { 22, 0, 0, 0, 0, 0 } },
    0,
    { _aDivision1, _aDivision1, _aDivision2, _aDivision2, _aDivision3, _aDivision3 }
};

/* ======================================================================
 * National Leagues (1-division)
 * ====================================================================== */

CompetitionLeague _albanianLeague = {
    { { 37, 0, 0, 64, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _austrianLeague = {
    { { 38, 0, 1, 56, 40, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _belarussiaLleague = {
    { { 39, 0, 76, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 69 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _belgianLeague = {
    { { 40, 0, 2, 56, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _bulgarianLeague = {
    { { 41, 0, 3, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _croatianLeague = {
    { { 42, 0, 4, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _cypriotLeague = {
    { { 43, 0, 5, 64, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 14, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _czechLeague = {
    { { 44, 0, 6, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _danishLeague = {
    { { 45, 0, 7, 56, 40, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _estonianLeague = {
    { { 50, 0, 10, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _faroeIslesLeague = {
    { { 51, 0, 11, 24, 64, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _finnishLeague = {
    { { 52, 0, 12, 24, 72, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _greekLeague = {
    { { 55, 0, 15, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _hungarianLeague = {
    { { 58, 0, 16, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _icelandicLeague = {
    { { 59, 0, 17, 32, 64, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _israeliLeague = {
    { { 60, 0, 19, 64, 32, 0, 0, 0, 0 }, 1, 3, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _latvianLeague = {
    { { 62, 0, 21, 32, 64, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _lithuanianLeague = {
    { { 63, 0, 22, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _luxembourgLeague = {
    { { 64, 0, 23, 56, 40, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _malteseLeague = {
    { { 65, 0, 24, 64, 24, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _northernIrishLeague = {
    { { 66, 0, 26, 64, 24, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _norwegianLeague = {
    { { 67, 0, 27, 24, 72, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 14, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _polishLeague = {
    { { 68, 0, 28, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _portugeseLeague = {
    { { 69, 0, 29, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _irishLeague = {
    { { 70, 0, 18, 56, 24, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _romanianLeague = {
    { { 71, 0, 30, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _russianLeague = {
    { { 72, 0, 31, 16, 80, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _sanMarinoLeague = {
    { { 73, 0, 32, 72, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _slovakianLeague = {
    { { 76, 0, 78, 56, 40, 0, 0, 0, 0 }, 1, 2, 2, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _slovenianLeague = {
    { { 77, 0, 34, 56, 40, 0, 0, 0, 0 }, 1, 2, 2, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _swedishLeague = {
    { { 80, 0, 36, 24, 72, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 14, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _swissLeague = {
    { { 81, 0, 37, 48, 40, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _turkishLeague = {
    { { 82, 0, 38, 56, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _ukrainianLeague = {
    { { 83, 0, 39, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 18, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _welshLeague = {
    { { 84, 0, 40, 56, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 20, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _yugoslavianLeague = {
    { { 85, 0, 41, 64, 40, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};

/* Non-European 1-division leagues */
CompetitionLeague _algerianLeague = {
    { { 86, 0, 42, 64, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _bolivianLeague = {
    { { 89, 0, 45, 32, 88, 0, 0, 0, 0 }, 1, 2, 2, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _brasilianLeague = {
    { { 90, 0, 46, 0, 40, 0, 0, 0, 0 }, 1, 2, 2, 53 },
    { { 24, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _chileanLeague = {
    { { 91, 0, 48, 24, 88, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _colombianLeague = {
    { { 92, 0, 49, 24, 88, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _ecuadorLeague = {
    { { 93, 0, 50, 24, 88, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _indianLeague = {
    { { 95, 0, 75, 0, 24, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 6, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _paraguayanLeague = {
    { { 99, 0, 64, 64, 32, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _peruvianLeague = {
    { { 100, 0, 65, 24, 88, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _southAfricanLeague = {
    { { 101, 0, 69, 0, 80, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 16, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _surinamLeague = {
    { { 102, 0, 66, 24, 72, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _taiwaneseLeague = {
    { { 103, 0, 67, 64, 32, 0, 0, 0, 0 }, 1, 4, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _americanLeague = {
    { { 106, 0, 73, 24, 72, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _venezuelanLeague = {
    { { 107, 0, 77, 24, 88, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 22, 0, 0, 0, 0, 0 } },
    0,
    {  }
};
CompetitionLeague _ghanaLeague = {
    { { 108, 0, 79, 56, 40, 0, 0, 0, 0 }, 1, 2, 3, 53 },
    { { 8, 0, 0, 0, 0, 0 } },
    0,
    {  }
};

/* ======================================================================
 * Multi-division leagues
 * ====================================================================== */

CompetitionLeague _frenchLeague = {
    { { 53, 0, 13, 56, 40, 33, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 20, 0, 0, 3, 0, 0 }, { 22, 3, 0, 0, 0, 0 } },
    0,
    { _aPremiereDivisi, _aPremiere, _aDeuxiemeDivisi, _aDeuxieme }
};

CompetitionLeague _germanLeague = {
    { { 54, 0, 14, 56, 40, 33, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 18, 0, 0, 3, 0, 0 }, { 18, 3, 0, 0, 0, 0 } },
    0,
    { _a1Liga, _a1Liga, _a2Liga, _a2Liga }
};

CompetitionLeague _italianLeague = {
    { { 61, 0, 20, 64, 32, 33, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 18, 0, 0, 4, 0, 0 }, { 20, 4, 0, 0, 0, 0 } },
    0,
    { _aSerieA, _aSerieA, _aSerieB, _aSerieB }
};

CompetitionLeague _japaneseLeague = {
    { { 96, 0, 55, 16, 80, 33, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 14, 0, 0, 0, 0, 0 }, { 14, 0, 0, 0, 0, 0 } },
    0,
    { _aJLeague, _aJLeague_0, _aLeague_2, _aLeague_3 }
};

CompetitionLeague _argentinianLeague = {
    { { 87, 0, 43, 64, 32, 0, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 20, 0, 0, 2, 0, 0 }, { 20, 2, 0, 0, 0, 0 } },
    0,
    {  }
};

CompetitionLeague _mexicanLeague = {
    { { 97, 0, 60, 56, 32, 0, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 20, 0, 0, 2, 0, 0 }, { 20, 2, 0, 0, 0, 0 } },
    0,
    {  }
};

CompetitionLeague _elSalvadorLeague = {
    { { 94, 0, 51, 56, 32, 0, 0, 0, 0 }, 4, 2, 3, 53 },
    { { 10, 0, 0, 1, 0, 0 }, { 12, 1, 0, 1, 0, 0 }, { 12, 1, 0, 1, 0, 0 }, { 12, 1, 0, 0, 0, 0 } },
    0,
    {  }
};

CompetitionLeague _australianLeague = {
    { { 88, 0, 44, 16, 80, 33, 0, 0, 0 }, 4, 2, 3, 53 },
    { { 12, 0, 0, 0, 0, 0 }, { 12, 0, 0, 0, 0, 0 }, { 14, 0, 0, 0, 0, 0 }, { 12, 0, 0, 0, 0, 0 } },
    0,
    { _a1stDivision, _a1stDivision, _aSouthDivision, _aSouthDivision_0, _aNswDivision, _aNsw, _aQueenslandDivi, _aQueensland }
};

CompetitionLeague _newZealandLeague = {
    { { 98, 0, 62, 16, 72, 33, 0, 0, 0 }, 3, 2, 3, 53 },
    { { 10, 0, 0, 0, 0, 0 }, { 10, 0, 0, 0, 0, 0 }, { 10, 0, 0, 0, 0, 0 } },
    0,
    { _aNorthernRegion, _aNorthern, _aCentralRegion, _aCentral, _aSouthernRegion, _aSouthern }
};

/* ======================================================================
 * Complex leagues with playoffs
 * ====================================================================== */

/* On Amiga, the four English-league structs sit consecutively in BSS
 * (_main 102 + _div1 80 + _div2 80 + _div3 80 bytes after the Leagues merge —
 * _div3 was 48 as a bare Leagues_4, now carries the zeroed playoffs/nextGroup/
 * qualifierIndexPairs tail like the others; it is the block's last member so the
 * growth only extends the block).
 * processDiyLeagueQualificationEntry walks past the end of _main using
 * the per-division qualification offset in `divisions[i].qualifierLinkOffset`,
 * expecting to land inside _div1 or _div2 (whose `playoffs` field at
 * +48 supplies the qualification competition pointer). Four separate
 * linker-scattered globals broke that walk → qualComp garbage → SIGSEGV.
 *
 * CONSOLIDATED 2026-06-18 (EnglishLeaguesBlock removed): England's leagues are
 * now standalone per-country globals — englishLeague (CompetitionLeague) +
 * englishLeagues1/2/3 (Leagues) — exactly like every other multi-division
 * country (dutchLeague + dutchLeagues1, …). No England-only struct: any country
 * can now be customized to the full 4-division + playoff structure. The mutual
 * cross-references resolve via the forward externs in competition_data.h
 * (included above); the data is byte-identical to the old bundled block. */
CompetitionLeague _englishLeague = {
    { { 46, 0, 8, 56, 32, 33, 0, 0, 0 }, 4, 2, 3, 51 },
    { { 20, 0, 0, 3, 0, 0 }, { 24, 2, 4, 4, 0, 46 }, { 24, 3, 4, 4, 0, 120 }, { 24, 3, 4, 0, 0, -62 } },
    0,
    { _aPremierLeague, _aPremier, _aDivisionOne, _aDivisionOne, _aDivisionTwo, _aDivisionTwo, _aDivisionThree, _aDivisionThree },
    { (char *)&_div1Playoffs, (char *)&_englishLeagues1 },
    { 0, 0, 0, 3, 0, 1, 0, 2, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};
Leagues _englishLeagues1 = {
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 1, 0, 0, 0, 0 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 1, 0, 0, 0, 3 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 1, 0, 0, 0, 1 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 1, 0, 0, 0, 2 },
    &_div2Playoffs, (Leagues *)&_englishLeagues2,
    { 0, 4, 0, 7, 0, 5, 0, 6, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};
Leagues _englishLeagues2 = {
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 2, 0, 0, 0, 0 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 2, 0, 0, 0, 3 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 2, 0, 0, 0, 1 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 2, 0, 0, 0, 2 },
    &_div3Playoffs, (Leagues *)&_englishLeagues3,
    { 0, 8, 0, 11, 0, 9, 0, 10, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};
Leagues _englishLeagues3 = {
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 3, 0, 0, 0, 0 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 3, 0, 0, 0, 3 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 3, 0, 0, 0, 1 },
    (CompetitionLeague *)&_englishLeague, { 0, 0, 0, 3, 0, 0, 0, 2 }
};

/* (the englishLeagues / englishLeagues1..3 .equ aliases are gone — all
 * access is via englishLeague/._div1/._div2/._div3 now) */

Playoffs _div1Playoffs = { { 47, 1, 8, -128, -124, 11, 1, 17, 1 }, 0, 4, 1, 51, 1, { -108, 20 }, { _aDiv1Playoffs, _aPlayoffs }, { 0 } };
Playoffs _div2Playoffs = { { 48, 1, 8, -128, -124, 11, 1, 17, 1 }, 0, 4, 1, 51, 1, { -108, 20 }, { _aDiv2Playoffs, _aPlayoffs }, { 0 } };
Playoffs _div3Playoffs = { { 49, 1, 8, -128, -124, 11, 1, 17, 1 }, 0, 4, 1, 51, 1, { -108, 20 }, { _aDiv3Playoffs, _aPlayoffs }, { 0 } };

/* Dutch league */
CompetitionLeague _dutchLeague = {
    { { 56, 0, 25, 56, 32, 0, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 18, 0, 0, 1, 2, 8 }, { 18, 1, 2, 0, 0, 2 } },
    0,
    { 0 },
    { (char *)&_dutchPlayoffs, (char *)&_dutchLeagues1 },
    { 0, 0, 0, 3, 0, 1, 0, 2, 0, -1, 0, -1, 0, -1, 0, -1, 1, 0, 1, 0, 1, 0, 1, 0 }
};

Leagues _dutchLeagues1 = {
    (CompetitionLeague *)&_dutchLeague, { 0 },
    (CompetitionLeague *)&_dutchLeague, { 0, 0, 0, 1, 0, 0, 0, 1 },
    (CompetitionLeague *)&_dutchLeague, { 0, 0, 0, 0, 0, 0, 0, 1 },
    (CompetitionLeague *)&_dutchLeague, { 0, 0, 0, 1, 0, 0, 0, 0 }
};

Playoffs _dutchPlayoffs = { { 57, 1, 25, -128, -128, 11, 1, 17, 1 }, 1, 4, 1, 53, 1, { -108, -108 }, { _aPlayoffs, _aPlayoffs }, { 0 } };

/* Scottish league */
CompetitionLeague _scottishLeague = {
    { { 74, 0, 33, 56, 32, 0, 0, 0, 0 }, 4, 4, 3, 51 },
    { { 10, 0, 0, 1, 1, 20 }, { 10, 1, 1, 2, 0, 14 }, { 10, 2, 0, 2, 0, 0 }, { 10, 2, 0, 0, 0, 0 } },
    0,
    { 0 },
    { (char *)&_scottishPlayoffs, (char *)&_scottishLeague1 },
    { 0, 0, 0, 1, 0, -1, 1, 0 }
};

Leagues _scottishLeague1 = {
    (CompetitionLeague *)&_scottishLeague, { 0 },
    (CompetitionLeague *)&_scottishLeague, { 0, 0, 0, 1, 0, 0, 0, 0 }
};

Playoffs _scottishPlayoffs = { { 75, 1, 33, -128, -128, 11, 1, 17, 1 }, 0, 2, 1, 51, 1, { 20, 0 }, { _aPlayoffs, _aPlayoffs }, { 0 } };

/* Spanish league */
CompetitionLeague _spanishLeague = {
    { { 78, 0, 35, 64, 40, 33, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 22, 0, 0, 2, 2, 24 }, { 20, 2, 2, 0, 0, 18 } },
    0,
    { _aPrimeraDivisio, _aPrimera, _aSegundaDivisio, _aSegunda },
    { (char *)&_spanishPlayoffs, (char *)&_spanishLeague1 },
    { 0, 0, 0, 3, 0, 1, 0, 2, 0, -1, 0, -1, 0, -1, 0, -1, 1, 0, 1, 0, 1, 0, 1, 0 }
};

Leagues _spanishLeague1 = {
    (CompetitionLeague *)&_spanishLeague, { 0 },
    (CompetitionLeague *)&_spanishLeague, { 0, 0, 0, 1, 0, 0, 0, 1 },
    (CompetitionLeague *)&_spanishLeague, { 0, 0, 0, 0, 0, 0, 0, 1 },
    (CompetitionLeague *)&_spanishLeague, { 0, 0, 0, 1, 0, 0, 0, 0 }
};

Playoffs _spanishPlayoffs = { { 79, 1, 35, -120, -120, 11, 1, 17, 1 }, 1, 4, 1, 53, 1, { -108, -108 }, { _aPlayoffs, _aPlayoffs }, { 0 } };

/* Uruguayan league */
CompetitionLeague _uruguayanLeague = {
    { { 104, 0, 71, 24, 88, 0, 0, 0, 0 }, 2, 2, 3, 53 },
    { { 12, 0, 0, 1, 1, 8 }, { 14, 1, 1, 0, 0, 2 } },
    0,
    { 0 },
    { (char *)&_uruguayanPlayoffs, (char *)&_uruguayanLeague1 },
    { 0, 0, 0, 1, 0, -1, 1, 0 }
};

Leagues _uruguayanLeague1 = {
    (CompetitionLeague *)&_uruguayanLeague, { 0 },
    (CompetitionLeague *)&_uruguayanLeague, { 0, 0, 0, 1, 0, 0, 0, 0 }
};

Playoffs _uruguayanPlayoffs = { { 105, 1, 71, 88, 88, 11, 1, 17, 1 }, 0, 2, 1, 53, 1, { -108, 0 }, { _aPlayoffs, _aPlayoffs }, { 0 } };

/* ======================================================================
 * National Cups
 * ====================================================================== */

CompetitionCup _albanianCup = {
    { { 109, 1, 0, 88, 32, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { 84, 84, -108, 20 },
    {  },
    { }
};
CompetitionCup _austrianCup = {
    { { 110, 1, 1, -128, -120, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _belarusianCup = {
    { { 111, 1, 76, 72, 40, 0, 0, 0, 0 }, 0, 16, 0, 53, 1 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _belgianCup = {
    { { 112, 1, 2, 80, 32, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { 84, 84, -108, 20 },
    {  },
    { }
};
CompetitionCup _bulgarianCup = {
    { { 113, 1, 3, 80, 32, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { -108, -108, -108, 20 },
    {  },
    { }
};
CompetitionCup _croatianCup = {
    { { 114, 1, 4, 80, 40, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { -108, -108, -108, -108 },
    {  },
    { }
};
CompetitionCup _cypriotCup = {
    { { 115, 1, 5, 104, -128, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { -108, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _czechCup = {
    { { 116, 1, 6, 72, 40, 0, 0, 0, 0 }, 0, 16, 0, 53, 1 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _danishCup = {
    { { 117, 1, 7, 112, -120, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { 84, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _estonianCup = {
    { { 122, 1, 10, -128, -120, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { 84, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _faroeIslesCup = {
    { { 123, 1, 11, 56, 64, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { 84, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _finnishCup = {
    { { 124, 1, 12, 32, 40, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _greekCup = {
    { { 127, 1, 15, 64, 40, 0, 0, 0, 0 }, 0, 16, 1, 53, 2 },
    { -108, -108, -108, 20 },
    {  },
    { }
};
CompetitionCup _hungarianCup = {
    { { -128, 1, 16, 104, 40, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { -108, -108, -108, -108 },
    {  },
    { }
};
CompetitionCup _icelandicCup = {
    { { -127, 1, 17, 56, 64, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _irishCup = {
    { { -126, 1, 18, 112, -128, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 104, 104, 40, 0 },
    {  },
    { }
};
CompetitionCup _israeliCup = {
    { { -125, 1, 19, 120, -128, 0, 0, 0, 0 }, 0, 16, 0, 53, 1 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _latvianCup = {
    { { -123, 1, 21, 56, 64, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { -108, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _lithuanianCup = {
    { { -122, 1, 22, -128, -120, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { -108, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _luxembourgCup = {
    { { -121, 1, 23, 120, -120, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _malteseCup = {
    { { -120, 1, 24, -128, -120, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _northernIrishCup = {
    { { -118, 1, 26, 104, -128, 0, 0, 0, 0 }, 0, 16, 0, 53, 6 },
    { 104, 104, 104, 40 },
    {  },
    { }
};
CompetitionCup _norwegianCup = {
    { { -117, 1, 27, 64, 80, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _polishCup = {
    { { -116, 1, 28, 80, 40, 0, 0, 0, 0 }, 0, 16, 1, 53, 5 },
    { 84, -108, -108, 20 },
    {  },
    { }
};
CompetitionCup _portugeseCup = {
    { { -115, 1, 29, 96, -120, 0, 0, 0, 0 }, 0, 16, 0, 53, 6 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _romanianCup = {
    { { -114, 1, 30, 112, -120, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { 84, -108, -108, 20 },
    {  },
    { }
};
CompetitionCup _russianCup = {
    { { -113, 1, 31, 16, 40, 0, 0, 0, 0 }, 0, 16, 0, 53, 3 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _sanMarinoCup = {
    { { -112, 1, 32, 120, -128, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _slovakianCup = {
    { { -109, 1, 78, -128, -120, 0, 0, 0, 0 }, 0, 8, 1, 53, 1 },
    { 84, -108, 20, 0 },
    {  },
    { }
};
CompetitionCup _slovenianCup = {
    { { -108, 1, 34, 120, -120, 0, 0, 0, 0 }, 0, 16, 0, 53, 2 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _swedishCup = {
    { { -106, 1, 36, 32, 40, 0, 0, 0, 0 }, 0, 16, 0, 53, 5 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _swissCup = {
    { { -105, 1, 37, 120, -120, 0, 0, 0, 0 }, 0, 8, 0, 53, 1 },
    { 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _turkishCup = {
    { { -104, 1, 38, 88, 24, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { 84, 84, -108, -108 },
    {  },
    { }
};
CompetitionCup _ukrainianCup = {
    { { -103, 1, 39, 120, -120, 0, 0, 0, 0 }, 0, 16, 1, 53, 1 },
    { -108, -108, -108, 20 },
    {  },
    { }
};
CompetitionCup _welshCup = {
    { { -102, 1, 40, 88, 32, 0, 0, 0, 0 }, 0, 16, 1, 53, 4 },
    { 104, 104, -108, 40 },
    {  },
    { }
};
CompetitionCup _yugoslavianCup = {
    { { -101, 1, 41, 80, 32, 0, 0, 0, 0 }, 0, 8, 1, 53, 2 },
    { -108, -108, -108, 0 },
    {  },
    { }
};
CompetitionCup _algerianCup = {
    { { -100, 1, 42, 96, -128, 0, 0, 0, 0 }, 0, 16, 1, 53, 2 },
    { 84, 84, -108, 20 },
    {  },
    { }
};
CompetitionCup _southAfricanCup = {
    { { -96, 1, 69, 56, 80, 0, 0, 0, 0 }, 0, 16, 1, 53, 2 },
    { 84, 84, 84, 20 },
    {  },
    { }
};
CompetitionCup _taiwaneseCup = {
    { { -95, 1, 67, 96, 120, 0, 0, 0, 0 }, 0, 8, 1, 53, 2 },
    { 84, 84, 20, 0 },
    {  },
    { }
};

/* Cups with names */
CompetitionCup _englishCup = {
    { { 118, 1, 8, 96, -128, 15, 0, 0, 0 }, 0, 64, 0, 51, 3 },
    { 104, 104, 104, 104, 40, 40 },
    { _aCup_4, _aCup_5 },
    { }
};
CompetitionCup _englishLeagueCup = {
    { { 119, 1, 8, 64, 16, 15, 0, 0, 0 }, 0, 64, 2, 51, 2 },
    { -108, 104, 104, 104, -108, 40 },
    { _aLeagueCup, _aLeagueCup_0 },
    { }
};
CompetitionCup _englishCup_1 = {
    { { 120, 1, 8, 96, -128, 15, 0, 0, 0 }, 0, 32, 0, 51, 4 },
    { 104, 104, 104, 40, 40, 0 },
    { _aCup_4, _aCup_5 },
    { }
};
CompetitionCup _englishLeagueCup_1 = {
    { { 121, 1, 8, 64, 16, 15, 0, 0, 0 }, 0, 32, 2, 51, 3 },
    { 104, 104, 104, -108, 40, 0 },
    { _aLeagueCup, _aLeagueCup_0 },
    { }
};

CompetitionCup _frenchCup = {
    { { 125, 1, 13, 104, -120, 15, 0, 0, 0 }, 0, 32, 0, 53, 1 },
    { 84, 84, 84, 84, 20, 0 },
    { _aCoupeDeFrance, _aCoupe },
    { }
};
CompetitionCup _germanCup = {
    { { 126, 1, 14, 64, 40, 15, 0, 0, 0 }, 0, 32, 0, 53, 3 },
    { 84, 84, 84, 84, 20, 0 },
    { _aDFBPokal, _aPokal },
    { }
};
CompetitionCup _italianCup = {
    { { -124, 1, 20, 56, 32, 15, 0, 0, 0 }, 0, 32, 1, 53, 2 },
    { -108, -108, -108, -108, 20, 0 },
    { _aCoppaItalia, _aCoppa },
    { }
};
CompetitionCup _scottishCup = {
    { { -111, 1, 33, 96, -128, 15, 0, 0, 0 }, 0, 32, 0, 51, 3 },
    { 104, 104, 104, 104, 40, 0 },
    { _aCup_6, _aCup_7 },
    { }
};
CompetitionCup _scottishLeagueCup = {
    { { -110, 1, 33, 56, 72, 15, 0, 0, 0 }, 0, 32, 0, 51, 2 },
    { 104, 104, 104, 40, 40, 0 },
    { _aLeagueCup_1, _aLeagueCup_2 },
    { }
};
CompetitionCup _spanishCup = {
    { { -107, 1, 35, 88, 40, 15, 0, 0, 0 }, 0, 32, 1, 53, 5 },
    { -108, -108, -108, -108, 20, 0 },
    { _aCopa, _aCopa_0 },
    { }
};

CompetitionCup _dutchCup = {
    { { -119, 1, 25, 88, 32, 0, 0, 0, 0 }, 0, 32, 0, 53, 3 },
    { 84, 84, 84, 84, 20, 0 },
    {  },
    { }
};
CompetitionCup _australianCup = {
    { { -99, 1, 44, 40, 80, 0, 0, 0, 0 }, 0, 32, 1, 53, 2 },
    { 84, 84, 84, 84, 20, 0 },
    {  },
    { }
};

CompetitionCup _japaneseCup = {
    { { -98, 1, 55, 48, 56, 15, 0, 0, 0 }, 0, 8, 1, 53, 2 },
    { 84, 84, 20, 0 },
    { _aCup_8, _aCup_9 },
    { }
};
CompetitionCup _japaneseEmperorsCup = {
    { { -97, 1, 55, 88, 0, 15, 0, 0, 0 }, 0, 16, 1, 53, 2 },
    { 84, 84, 84, 20 },
    { _aEmperorSCup, _aEmpCup },
    { }
};

/* ======================================================================
 * Competition "Copy" variables (used at runtime for career/season)
 * ====================================================================== */

/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): euroCupCopy */
extern CompetitionTournament _euroCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): cupWinnersCupCopy */
extern CompetitionCup _cupWinnersCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): uefaCupCopy */
extern CompetitionCup _uefaCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): invitationTournamentCopy */
extern CompetitionCup _invitationTournamentCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): tourCopy */
extern CompetitionCup _tourCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): friendlyCopy */
extern CompetitionCup _friendlyCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): europeanChampionships2Copy */
extern CompetitionTournament _europeanChampionships2Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup1Copy */
extern CompetitionCup _ecQualifyingGroup1Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup2Copy */
extern CompetitionCup _ecQualifyingGroup2Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup3Copy */
extern CompetitionCup _ecQualifyingGroup3Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup4Copy */
extern CompetitionCup _ecQualifyingGroup4Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup5Copy */
extern CompetitionCup _ecQualifyingGroup5Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup6Copy */
extern CompetitionCup _ecQualifyingGroup6Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup7Copy */
extern CompetitionCup _ecQualifyingGroup7Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): ecQualifyingGroup8Copy */
extern CompetitionCup _ecQualifyingGroup8Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): copaAmericaCopy */
extern CompetitionTournament _copaAmericaCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): africanNationsCupCopy */
extern CompetitionTournament _africanNationsCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): northAmericanCupCopy */
extern CompetitionTournament _northAmericanCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): asianCupCopy */
extern CompetitionTournament _asianCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): oceaniaCupCopy */
extern CompetitionCup _oceaniaCupCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCup2Copy */
extern CompetitionTournament _worldCup2Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup1Copy */
extern CompetitionCup _worldCupQualifyingGroup1Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup2Copy */
extern CompetitionCup _worldCupQualifyingGroup2Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup3Copy */
extern CompetitionCup _worldCupQualifyingGroup3Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup4Copy */
extern CompetitionCup _worldCupQualifyingGroup4Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup5Copy */
extern CompetitionCup _worldCupQualifyingGroup5Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup6Copy */
extern CompetitionCup _worldCupQualifyingGroup6Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup7Copy */
extern CompetitionCup _worldCupQualifyingGroup7Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): worldCupQualifyingGroup8Copy */
extern CompetitionCup _worldCupQualifyingGroup8Copy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): northAmericaWorldCupQualificationCopy */
extern CompetitionTournament _northAmericaWorldCupQualificationCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): southAmericaWorldCupQualificationCopy */
extern CompetitionTournament _southAmericaWorldCupQualificationCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): africaWorldCupQualificationCopy */
extern CompetitionTournament _africaWorldCupQualificationCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): asiaWorldCupQualificationCopy */
extern CompetitionTournament _asiaWorldCupQualificationCopy;  /* aliased into careerFileBufferBuf */
/* MOVED to careerFileBufferBuf overlay (see swos_globals_career_overlay.c): oceaniaWorldCupQualificationCopy */
extern CompetitionCup _oceaniaWorldCupQualificationCopy;  /* aliased into careerFileBufferBuf */

/* ======================================================================
 * Career Contests
 * ====================================================================== */

/* 34 real entries + a 0xFFFFFFFF sentinel as the loop terminator
 * (matches `dc.l -1` after the last entry in SWOS2.s:118137).
 *
 * Roadmap step 5 (runtime-fill): over-sized to CAREER_CONTESTS_CAP so a
 * dynamically-loaded competition (step 6) can append its {template, copy, size}
 * triple via Comp_AppendCareerContest — the spare [35..CAP-1] slots are zero-
 * filled and never reached until an append moves the sentinel. The compiled 34
 * entries are unchanged, so every sentinel-terminated walk (InitCareer fill,
 * save reconstruction) behaves identically until something appends. */
CompetitionCareer _careerContests[CAREER_CONTESTS_CAP] = {
    { (Competition *)&_euroCup, (Competition *)&_euroCupCopy, sizeof(_euroCup) },
    { (Competition *)&_cupWinnersCup, (Competition *)&_cupWinnersCupCopy, sizeof(_cupWinnersCup) },
    { (Competition *)&_uefaCup, (Competition *)&_uefaCupCopy, sizeof(_uefaCup) },
    { (Competition *)&_invitationTournament, (Competition *)&_invitationTournamentCopy, sizeof(_invitationTournament) },
    { (Competition *)&_tour, (Competition *)&_tourCopy, sizeof(_tour) },
    { (Competition *)&_friendly, (Competition *)&_friendlyCopy, sizeof(_friendly) },
    { (Competition *)&_europeanChampionships2, (Competition *)&_europeanChampionships2Copy, sizeof(_europeanChampionships2) },
    { (Competition *)&_ecQualifyingGroup1, (Competition *)&_ecQualifyingGroup1Copy, sizeof(_ecQualifyingGroup1) },
    { (Competition *)&_ecQualifyingGroup2, (Competition *)&_ecQualifyingGroup2Copy, sizeof(_ecQualifyingGroup2) },
    { (Competition *)&_ecQualifyingGroup3, (Competition *)&_ecQualifyingGroup3Copy, sizeof(_ecQualifyingGroup3) },
    { (Competition *)&_ecQualifyingGroup4, (Competition *)&_ecQualifyingGroup4Copy, sizeof(_ecQualifyingGroup4) },
    { (Competition *)&_ecQualifyingGroup5, (Competition *)&_ecQualifyingGroup5Copy, sizeof(_ecQualifyingGroup5) },
    { (Competition *)&_ecQualifyingGroup6, (Competition *)&_ecQualifyingGroup6Copy, sizeof(_ecQualifyingGroup6) },
    { (Competition *)&_ecQualifyingGroup7, (Competition *)&_ecQualifyingGroup7Copy, sizeof(_ecQualifyingGroup7) },
    { (Competition *)&_ecQualifyingGroup8, (Competition *)&_ecQualifyingGroup8Copy, sizeof(_ecQualifyingGroup8) },
    { (Competition *)&_copaAmerica, (Competition *)&_copaAmericaCopy, sizeof(_copaAmerica) },
    { (Competition *)&_africanNationsCup, (Competition *)&_africanNationsCupCopy, sizeof(_africanNationsCup) },
    { (Competition *)&_northAmericanCup, (Competition *)&_northAmericanCupCopy, sizeof(_northAmericanCup) },
    { (Competition *)&_asianCup, (Competition *)&_asianCupCopy, sizeof(_asianCup) },
    { (Competition *)&_oceaniaCup, (Competition *)&_oceaniaCupCopy, sizeof(_oceaniaCup) },
    { (Competition *)&_worldCup2, (Competition *)&_worldCup2Copy, sizeof(_worldCup2) },
    { (Competition *)&_worldCupQualifyingGroup1, (Competition *)&_worldCupQualifyingGroup1Copy, sizeof(_worldCupQualifyingGroup1) },
    { (Competition *)&_worldCupQualifyingGroup2, (Competition *)&_worldCupQualifyingGroup2Copy, sizeof(_worldCupQualifyingGroup2) },
    { (Competition *)&_worldCupQualifyingGroup3, (Competition *)&_worldCupQualifyingGroup3Copy, sizeof(_worldCupQualifyingGroup3) },
    { (Competition *)&_worldCupQualifyingGroup4, (Competition *)&_worldCupQualifyingGroup4Copy, sizeof(_worldCupQualifyingGroup4) },
    { (Competition *)&_worldCupQualifyingGroup5, (Competition *)&_worldCupQualifyingGroup5Copy, sizeof(_worldCupQualifyingGroup5) },
    { (Competition *)&_worldCupQualifyingGroup6, (Competition *)&_worldCupQualifyingGroup6Copy, sizeof(_worldCupQualifyingGroup6) },
    { (Competition *)&_worldCupQualifyingGroup7, (Competition *)&_worldCupQualifyingGroup7Copy, sizeof(_worldCupQualifyingGroup7) },
    { (Competition *)&_worldCupQualifyingGroup8, (Competition *)&_worldCupQualifyingGroup8Copy, sizeof(_worldCupQualifyingGroup8) },
    { (Competition *)&_northAmericaWorldCupQualification, (Competition *)&_northAmericaWorldCupQualificationCopy, sizeof(_northAmericaWorldCupQualification) },
    { (Competition *)&_southAmericaWorldCupQualification, (Competition *)&_southAmericaWorldCupQualificationCopy, sizeof(_southAmericaWorldCupQualification) },
    { (Competition *)&_africaWorldCupQualification, (Competition *)&_africaWorldCupQualificationCopy, sizeof(_africaWorldCupQualification) },
    { (Competition *)&_asiaWorldCupQualification, (Competition *)&_asiaWorldCupQualificationCopy, sizeof(_asiaWorldCupQualification) },
    { (Competition *)&_oceaniaWorldCupQualification, (Competition *)&_oceaniaWorldCupQualificationCopy, sizeof(_oceaniaWorldCupQualification) },
    { (Competition *)0xFFFFFFFFu, (Competition *)0, 0 },  /* sentinel (index 34) */
    /* [35 .. CAREER_CONTESTS_CAP-1]: spare, zero-filled, for runtime appends. */
};

/* Runtime count of real entries preceding the sentinel (34 compiled). */
static int s_careerContestsCount = 34;

int Comp_AppendCareerContest(Competition *competitionTemplate,
                             Competition *competitionCopy, uint16_t templateSize)
{
    /* Need room for the new entry AND a trailing sentinel. */
    if (s_careerContestsCount + 1 >= CAREER_CONTESTS_CAP)
        return -1;
    int i = s_careerContestsCount;
    _careerContests[i].competition     = competitionTemplate;
    _careerContests[i].competitionCopy = competitionCopy;
    _careerContests[i].templateSize    = templateSize;
    _careerContests[i + 1].competition     = (Competition *)0xFFFFFFFFu;  /* move sentinel */
    _careerContests[i + 1].competitionCopy = (Competition *)0;
    _careerContests[i + 1].templateSize    = 0;
    s_careerContestsCount = i + 1;
    return 0;
}

/* Runtime competition registry by compId (roadmap step 6). A dynamically-
 * loaded competition (no compiled global) registers its heap record here so
 * save/load pointer re-derivation (step 7) can resolve a saved compId back
 * to a live record. Compiled competitions don't need this — their slot pointers
 * re-derive via competitionsTable — but registering them too is harmless and
 * keeps the map uniform. Indexed 0..255 (compId is a single byte). */
static Competition *s_compById[256];

void Comp_Register(Competition *c)
{
    if (c)
        s_compById[(uint8_t)c->compId] = c;
}

Competition *Comp_ById(uint8_t compId)
{
    return s_compById[compId];
}

/* ======================================================================
 * Master Competition Pointer Arrays
 * ====================================================================== */

Competition *_nationalTeamCompetitionsAndQualifications[39] = {
    (Competition *)&_worldCup, (Competition *)&_europeanChampionships,
    (Competition *)&_copaAmerica, (Competition *)&_northAmericanCup,
    (Competition *)&_northAmericanCupCopy, (Competition *)&_africanNationsCup,
    (Competition *)&_africanNationsCupCopy, (Competition *)&_asianCup,
    (Competition *)&_asianCupCopy, (Competition *)&_oceaniaCup,
    (Competition *)&_oceaniaCupCopy, (Competition *)&_friendlyCopy,
    (Competition *)&_invitationTournamentCopy, (Competition *)&_tourCopy,
    (Competition *)&_copaAmericaCopy, (Competition *)&_europeanChampionships2Copy,
    (Competition *)&_ecQualifyingGroup1Copy, (Competition *)&_ecQualifyingGroup2Copy,
    (Competition *)&_ecQualifyingGroup3Copy, (Competition *)&_ecQualifyingGroup4Copy,
    (Competition *)&_ecQualifyingGroup5Copy, (Competition *)&_ecQualifyingGroup6Copy,
    (Competition *)&_ecQualifyingGroup7Copy, (Competition *)&_ecQualifyingGroup8Copy,
    (Competition *)&_worldCup2Copy,
    (Competition *)&_worldCupQualifyingGroup1Copy, (Competition *)&_worldCupQualifyingGroup2Copy,
    (Competition *)&_worldCupQualifyingGroup3Copy, (Competition *)&_worldCupQualifyingGroup4Copy,
    (Competition *)&_worldCupQualifyingGroup5Copy, (Competition *)&_worldCupQualifyingGroup6Copy,
    (Competition *)&_worldCupQualifyingGroup7Copy, (Competition *)&_worldCupQualifyingGroup8Copy,
    (Competition *)&_northAmericaWorldCupQualificationCopy,
    (Competition *)&_southAmericaWorldCupQualificationCopy,
    (Competition *)&_africaWorldCupQualificationCopy,
    (Competition *)&_asiaWorldCupQualificationCopy,
    (Competition *)&_oceaniaWorldCupQualificationCopy,
    (Competition *)-1
};

Competition *_continentalCups[7] = {
    (Competition *)&_europeanChampionships, (Competition *)&_copaAmerica,
    (Competition *)&_northAmericanCup, (Competition *)&_africanNationsCup,
    (Competition *)&_asianCup, (Competition *)&_oceaniaCup, (Competition *)-1
};

/* ======================================================================
 * Qualifying Groups and Region Tables
 * ====================================================================== */

QualifyingGroup _worldCupQualifyingGroups = {
    { (Competition *)&_worldCupQualifyingGroup1, (Competition *)&_worldCupQualifyingGroup2, (Competition *)&_worldCupQualifyingGroup3, (Competition *)&_worldCupQualifyingGroup4, (Competition *)&_worldCupQualifyingGroup5, (Competition *)&_worldCupQualifyingGroup6, (Competition *)&_worldCupQualifyingGroup7, (Competition *)&_worldCupQualifyingGroup8, (Competition *)-1 },
    { 255 }
};

QualifyingGroup _ecQualifyingGroups = {
    { (Competition *)&_ecQualifyingGroup1, (Competition *)&_ecQualifyingGroup2, (Competition *)&_ecQualifyingGroup3, (Competition *)&_ecQualifyingGroup4, (Competition *)&_ecQualifyingGroup5, (Competition *)&_ecQualifyingGroup6, (Competition *)&_ecQualifyingGroup7, (Competition *)&_ecQualifyingGroup8, (Competition *)-1 },
    { 255 }
};

QualifyingGroup _customsTable = {
    { (Competition *)&_customsLeague, (Competition *)-2, (Competition *)-1 },
    {  }
};

QualifyingGroup _worldTable = {
    { (Competition *)&_worldCup, (Competition *)-1 },
    { 80, 83, 82, 81, 84, 85, 255 }
};

QualifyingGroup _europeTable = {
    /* uefaCup before cupWinnersCup since the modern renames (2026-10-26): the
     * preset chooser lists this order, and the tiers read CHAMPIONS LEAGUE >
     * EUROPA LEAGUE > CONFERENCE LEAGUE. Must match qualifying.json's europe
     * comp_cup list (the loader verifies region tables byte-identical). */
    { (Competition *)252, (Competition *)&_europeanChampionships, (Competition *)&_euroCup, (Competition *)&_uefaCup, (Competition *)&_cupWinnersCup, (Competition *)-1 },
    { 0, 1, 76, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 25, 16, 17, 19, 20, 21, 22, 23, 24, 26, 27, 28, 29, 18, 30, 31, 32, 33, 78, 34, 35, 36, 37, 38, 39, 40, 41, 255 }
};

QualifyingGroup _africaTable = {
    { (Competition *)&_africaWorldCupQualification, (Competition *)&_africanNationsCup, (Competition *)-1 },
    { 42, 79, 69, 255 }
};

QualifyingGroup _southAmericaTable = {
    { (Competition *)&_southAmericaWorldCupQualification, (Competition *)&_copaAmerica, (Competition *)-1 },
    { 43, 45, 46, 48, 49, 50, 64, 65, 66, 71, 77, 255 }
};

QualifyingGroup _northAmericaTable = {
    { (Competition *)&_northAmericaWorldCupQualification, (Competition *)&_northAmericanCup, (Competition *)-1 },
    { 51, 60, 73, 255 }
};

QualifyingGroup _asiaTable = {
    { (Competition *)&_asiaWorldCupQualification, (Competition *)&_asianCup, (Competition *)-1 },
    { 75, 55, 67, 255 }
};

QualifyingGroup _oceaniaTable = {
    { (Competition *)&_oceaniaWorldCupQualification, (Competition *)&_oceaniaCup, (Competition *)-1 },
    { 44, 62, 255 }
};

/* ======================================================================
 * Country Competition Tables
 * ====================================================================== */

Competition *_albaniaTable[4] = { (Competition *)&_albanianLeague, (Competition *)-2, (Competition *)&_albanianCup, (Competition *)-1 };
Competition *_austriaTable[4] = { (Competition *)&_austrianLeague, (Competition *)-2, (Competition *)&_austrianCup, (Competition *)-1 };
Competition *_belgiumTable[4] = { (Competition *)&_belgianLeague, (Competition *)-2, (Competition *)&_belgianCup, (Competition *)-1 };
Competition *_belarusTable[4] = { (Competition *)&_belarussiaLleague, (Competition *)-2, (Competition *)&_belarusianCup, (Competition *)-1 };
Competition *_bulgariaTable[4] = { (Competition *)&_bulgarianLeague, (Competition *)-2, (Competition *)&_bulgarianCup, (Competition *)-1 };
Competition *_croatiaTable[4] = { (Competition *)&_croatianLeague, (Competition *)-2, (Competition *)&_croatianCup, (Competition *)-1 };
Competition *_cyprusTable[4] = { (Competition *)&_cypriotLeague, (Competition *)-2, (Competition *)&_cypriotCup, (Competition *)-1 };
Competition *_czechRepublicTable[4] = { (Competition *)&_czechLeague, (Competition *)-2, (Competition *)&_czechCup, (Competition *)-1 };
Competition *_denmarkTable[4] = { (Competition *)&_danishLeague, (Competition *)-2, (Competition *)&_danishCup, (Competition *)-1 };
Competition *_englandTable[5] = { (Competition *)&_englishLeague, (Competition *)-2, (Competition *)&_englishCup, (Competition *)&_englishLeagueCup, (Competition *)-1 };
Competition *_estoniaTable[4] = { (Competition *)&_estonianLeague, (Competition *)-2, (Competition *)&_estonianCup, (Competition *)-1 };
Competition *_faroeIslandsTable[4] = { (Competition *)&_faroeIslesLeague, (Competition *)-2, (Competition *)&_faroeIslesCup, (Competition *)-1 };
Competition *_finlandTable[4] = { (Competition *)&_finnishLeague, (Competition *)-2, (Competition *)&_finnishCup, (Competition *)-1 };
Competition *_franceTable[4] = { (Competition *)&_frenchLeague, (Competition *)-2, (Competition *)&_frenchCup, (Competition *)-1 };
Competition *_germanyTable[4] = { (Competition *)&_germanLeague, (Competition *)-2, (Competition *)&_germanCup, (Competition *)-1 };
Competition *_greeceTable[4] = { (Competition *)&_greekLeague, (Competition *)-2, (Competition *)&_greekCup, (Competition *)-1 };
Competition *_hungaryTable[4] = { (Competition *)&_hungarianLeague, (Competition *)-2, (Competition *)&_hungarianCup, (Competition *)-1 };
Competition *_icelandTable[4] = { (Competition *)&_icelandicLeague, (Competition *)-2, (Competition *)&_icelandicCup, (Competition *)-1 };
Competition *_republicIrelandTable[4] = { (Competition *)&_irishLeague, (Competition *)-2, (Competition *)&_irishCup, (Competition *)-1 };
Competition *_israelTable[4] = { (Competition *)&_israeliLeague, (Competition *)-2, (Competition *)&_israeliCup, (Competition *)-1 };
Competition *_italyTable[4] = { (Competition *)&_italianLeague, (Competition *)-2, (Competition *)&_italianCup, (Competition *)-1 };
Competition *_latviaTable[4] = { (Competition *)&_latvianLeague, (Competition *)-2, (Competition *)&_latvianCup, (Competition *)-1 };
Competition *_lithuaniaTable[4] = { (Competition *)&_lithuanianLeague, (Competition *)-2, (Competition *)&_lithuanianCup, (Competition *)-1 };
Competition *_luxembourgTable[4] = { (Competition *)&_luxembourgLeague, (Competition *)-2, (Competition *)&_luxembourgCup, (Competition *)-1 };
Competition *_maltaTable[4] = { (Competition *)&_malteseLeague, (Competition *)-2, (Competition *)&_malteseCup, (Competition *)-1 };
Competition *_hollandTable[4] = { (Competition *)&_dutchLeague, (Competition *)-2, (Competition *)&_dutchCup, (Competition *)-1 };
Competition *_northernIrelandTable[4] = { (Competition *)&_northernIrishLeague, (Competition *)-2, (Competition *)&_northernIrishCup, (Competition *)-1 };
Competition *_norwayTable[4] = { (Competition *)&_norwegianLeague, (Competition *)-2, (Competition *)&_norwegianCup, (Competition *)-1 };
Competition *_polandTable[4] = { (Competition *)&_polishLeague, (Competition *)-2, (Competition *)&_polishCup, (Competition *)-1 };
Competition *_portugalTable[4] = { (Competition *)&_portugeseLeague, (Competition *)-2, (Competition *)&_portugeseCup, (Competition *)-1 };
Competition *_romaniaTable[4] = { (Competition *)&_romanianLeague, (Competition *)-2, (Competition *)&_romanianCup, (Competition *)-1 };
Competition *_russiaTable[4] = { (Competition *)&_russianLeague, (Competition *)-2, (Competition *)&_russianCup, (Competition *)-1 };
Competition *_sanMarinoTable[4] = { (Competition *)&_sanMarinoLeague, (Competition *)-2, (Competition *)&_sanMarinoCup, (Competition *)-1 };
Competition *_scotlandTable[5] = { (Competition *)&_scottishLeague, (Competition *)-2, (Competition *)&_scottishCup, (Competition *)&_scottishLeagueCup, (Competition *)-1 };
Competition *_slovakiaTable[4] = { (Competition *)&_slovakianLeague, (Competition *)-2, (Competition *)&_slovakianCup, (Competition *)-1 };
Competition *_sloveniaTable[4] = { (Competition *)&_slovenianLeague, (Competition *)-2, (Competition *)&_slovenianCup, (Competition *)-1 };
Competition *_spainTable[4] = { (Competition *)&_spanishLeague, (Competition *)-2, (Competition *)&_spanishCup, (Competition *)-1 };
Competition *_swedenTable[4] = { (Competition *)&_swedishLeague, (Competition *)-2, (Competition *)&_swedishCup, (Competition *)-1 };
Competition *_switzerlandTable[4] = { (Competition *)&_swissLeague, (Competition *)-2, (Competition *)&_swissCup, (Competition *)-1 };
Competition *_turkeyTable[4] = { (Competition *)&_turkishLeague, (Competition *)-2, (Competition *)&_turkishCup, (Competition *)-1 };
Competition *_ukraineTable[4] = { (Competition *)&_ukrainianLeague, (Competition *)-2, (Competition *)&_ukrainianCup, (Competition *)-1 };
Competition *_walesTable[4] = { (Competition *)&_welshLeague, (Competition *)-2, (Competition *)&_welshCup, (Competition *)-1 };
Competition *_yugoslaviaTable[4] = { (Competition *)&_yugoslavianLeague, (Competition *)-2, (Competition *)&_yugoslavianCup, (Competition *)-1 };
Competition *_algeriaTable[4] = { (Competition *)&_algerianLeague, (Competition *)-2, (Competition *)&_algerianCup, (Competition *)-1 };
Competition *_argentinaTable[3] = { (Competition *)&_argentinianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_australiaTable[4] = { (Competition *)&_australianLeague, (Competition *)-2, (Competition *)&_australianCup, (Competition *)-1 };
Competition *_boliviaTable[3] = { (Competition *)&_bolivianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_brasilTable[3] = { (Competition *)&_brasilianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_chileTable[3] = { (Competition *)&_chileanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_colombiaTable[3] = { (Competition *)&_colombianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_ecuadorTable[3] = { (Competition *)&_ecuadorLeague, (Competition *)-2, (Competition *)-1 };
Competition *_elSalvadorTable[3] = { (Competition *)&_elSalvadorLeague, (Competition *)-2, (Competition *)-1 };
Competition *_indiaTable[3] = { (Competition *)&_indianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_japanTable[5] = { (Competition *)&_japaneseLeague, (Competition *)-2, (Competition *)&_japaneseCup, (Competition *)&_japaneseEmperorsCup, (Competition *)-1 };
Competition *_mexicoTable[3] = { (Competition *)&_mexicanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_newZealandTable[3] = { (Competition *)&_newZealandLeague, (Competition *)-2, (Competition *)-1 };
Competition *_paraguayTable[3] = { (Competition *)&_paraguayanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_peruTable[3] = { (Competition *)&_peruvianLeague, (Competition *)-2, (Competition *)-1 };
Competition *_southAfricaTable[4] = { (Competition *)&_southAfricanLeague, (Competition *)-2, (Competition *)&_southAfricanCup, (Competition *)-1 };
Competition *_surinamTable[3] = { (Competition *)&_surinamLeague, (Competition *)-2, (Competition *)-1 };
Competition *_taiwanTable[4] = { (Competition *)&_taiwaneseLeague, (Competition *)-2, (Competition *)&_taiwaneseCup, (Competition *)-1 };
Competition *_uruguayTable[3] = { (Competition *)&_uruguayanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_usaTable[3] = { (Competition *)&_americanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_venezuelaTable[3] = { (Competition *)&_venezuelanLeague, (Competition *)-2, (Competition *)-1 };
Competition *_ghanaTable[3] = { (Competition *)&_ghanaLeague, (Competition *)-2, (Competition *)-1 };

/* ======================================================================
 * Season flags and scheduling data
 * ====================================================================== */

int16_t _seasonCycleNationalFlagsA[5] = { -1, -1, -1, -1, -1 };
uint16_t _seasonCycleNationalFlagsB[5] = { 0x5005, 0x5025, 0x5022, 0x5011, 0x500B };

uint8_t _careerTeamFlagsBySeason0[20] = { 64, 128, 1, 2, 64, 128, 1, 2, 64, 128, 1, 2, 64, 128, 1, 2, 64, 128, 1, 2 };
uint8_t _careerTeamFlagsBySeason1[20] = { 66, 128, 2, 0, 66, 128, 2, 0, 66, 128, 2, 0, 66, 128, 2, 0, 66, 128, 2, 0 };

uint32_t _nationalSelectorSeasonMaskA = 0x2AAAA;
uint32_t _nationalSelectorSeasonMaskB = 0x2EEEE;

/* ======================================================================
 * Countries list and career remap
 * ====================================================================== */

CountryNumbers _allCountriesList[66] = {
    CN_BRAZIL, CN_ALBANIA, CN_AUSTRIA, CN_BELGIUM, CN_BELARUS, CN_BULGARIA,
    CN_CROATIA, CN_CYPRUS, CN_CZECH_REPUBLIC, CN_DENMARK, CN_ENGLAND, CN_ESTONIA,
    CN_FAROE_ISLANDS, CN_FINLAND, CN_FRANCE, CN_GERMANY, CN_GREECE, CN_HUNGARY,
    CN_ICELAND, CN_REPUBLIC_IRELAND, CN_ISRAEL, CN_ITALY, CN_LATVIA, CN_LITHUANIA,
    CN_LUXEMBOURG, CN_MALTA, CN_HOLLAND, CN_NORTHERN_IRELAND, CN_NORWAY, CN_POLAND,
    CN_PORTUGAL, CN_ROMANIA, CN_RUSSIA, CN_SAN_MARINO, CN_SCOTLAND, CN_SLOVAKIA,
    CN_SLOVENIA, CN_SPAIN, CN_SWEDEN, CN_SWITZERLAND, CN_TURKEY, CN_UKRAINE,
    CN_WALES, CN_YUGOSLAVIA, CN_ALGERIA, CN_SOUTH_AFRICA, CN_GHANA, CN_ARGENTINA,
    CN_BOLIVIA, CN_BRAZIL, CN_CHILE, CN_COLOMBIA, CN_ECUADOR, CN_PARAGUAY,
    CN_PERU, CN_SURINAM, CN_URUGUAY, CN_VENEZUELA, CN_EL_SALVADOR, CN_MEXICO,
    CN_USA, CN_JAPAN, CN_TAIWAN, CN_INDIA, CN_AUSTRALIA, CN_NEW_ZEALAND
};

uint8_t _careerCompetitionIdRemapTable[66] = {
    0, 1, 76, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 78, 34, 35, 36, 37, 38, 39, 40, 41, 42, 79, 69, 43, 45,
    46, 48, 49, 50, 64, 65, 66, 71, 77, 51, 60, 73, 75, 55, 67, 44,
    62, 255
};

/* ======================================================================
 * Euro Cup Qualifying Seed Lists
 * ====================================================================== */

uint8_t _euroCupQualSeedList4[4]  = { 14, 20, 35, 8 };
uint8_t _euroCupQualSeedList6[6]  = { 2, 13, 29, 30, 31, 41 };
uint8_t _euroCupQualSeedList11[11] = { 1, 3, 6, 7, 15, 25, 28, 33, 36, 37, 38 };
uint8_t _euroCupQualSeedList22[23] = { 16, 12, 0, 17, 5, 27, 26, 24, 18, 23, 4, 10, 11, 19, 21, 22, 32, 34, 39, 40, 76, 78, 0 };

uint8_t _euroCupQualSeedListReduced4[4]  = { 14, 20, 35, 8 };
uint8_t _euroCupQualSeedListReduced6[6]  = { 2, 13, 29, 30, 31, 41 };
uint8_t _euroCupQualSeedListReduced11[11] = { 1, 3, 6, 7, 15, 25, 28, 33, 36, 37, 38 };
uint8_t _euroCupQualSeedListReduced22[23] = { 16, 12, 0, 17, 5, 27, 26, 24, 18, 23, 4, 10, 11, 19, 21, 22, 32, 34, 39, 40, 76, 78, 0 };

/* Pack all four buckets into one contiguous block. The bucket-walk in
 * Career_PrepareEuropeanCupQualificationState scans
 * `euroCupQualGroupSeedBuckets_15_18_22[0].ptr` (= bucket15) byte-by-byte
 * looking for the user's country code, terminating only at the 0xFF
 * sentinel at the end of bucket22Full. On Amiga the four buckets sit
 * consecutively in BSS (15+18+22+23 = 78 bytes ending in 0xFF), so the
 * scan walks the full set. On SDL the linker scattered them — bucket15
 * actually came LAST in memory layout — so the scan walked off the end
 * of bucket15 into unrelated globals, hitting some random 0xFF and
 * miscounting the user's country occurrences. Result: for non-European
 * careers like Ghana the replacement loop accidentally wrote one
 * Ghana-coded entry into uefaCupEligibleCompetitionIds, no European
 * country ever consumed it, UEFA Cup ended up 1 short of 32, and
 * Career_AdvanceToNextSeason_UpdateCompetitions's careerFileBuffer ==
 * 0x50 invariant tripped panic 0x8999. Pack into one struct; consumers
 * use the named bucket15/18/22/22Full fields.
 *
 * UNTANGLED (.equ -> typed field): the EuroCupQualSeedBucketBlock typedef
 * + extern live in game_types.h (shared by competition_data.c and the
 * consumer table in swos_amiga_ptr_imports.c). The old
 * `.equ euroCupQualSeedBucketN, ...block + <literal>` aliases are gone —
 * all uint8 buckets so the literal offsets (15/33/55) were already
 * 64-bit-safe, but typed field access removes the .equ entirely. */
EuroCupQualSeedBucketBlock _euroCupQualSeedBucketBlock = {
    .bucket15     = { 14, 14, 14, 14, 20, 20, 20, 20, 35, 35, 35, 35, 8, 8, 8 },
    .bucket18     = { 2, 2, 2, 13, 13, 13, 29, 29, 29, 30, 30, 30, 31, 31, 31, 41, 41, 41 },
    .bucket22     = { 1, 1, 3, 3, 6, 6, 7, 7, 15, 15, 25, 25, 28, 28, 33, 33, 36, 36, 37, 37, 38, 38 },
    .bucket22Full = { 16, 12, 0, 17, 5, 27, 26, 24, 18, 23, 4, 10, 11, 19, 21, 22, 32, 34, 39, 40, 76, 78, 255 }
};

/* ======================================================================
 * Cup Pick Tables (per-region competition selection for career)
 * ====================================================================== */

Competition *_cupPickOceaniaInviteFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickOceaniaOceaniaCupFriendly[5] = { (Competition *)&_oceaniaCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickOceaniaWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickOceaniaWorldCupInvite[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickOceaniaOceaniaCupNonFinalist[5] = { 0, (Competition *)-2, (Competition *)&_oceaniaCupCopy, (Competition *)-1, 0 };
Competition *_cupPickOceaniaOceaniaCupFinalist[5] = { (Competition *)&_oceaniaCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_cupPickNAmericaInviteFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickNAmericaNaCupFriendly[5] = { (Competition *)&_northAmericanCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickNAmericaWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickNAmericaWorldCupInvite[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickNAmericaNaCupNonFinalist[5] = { 0, (Competition *)-2, (Competition *)&_northAmericanCupCopy, (Competition *)-1, 0 };
Competition *_cupPickNAmericaNaCupFinalist[5] = { (Competition *)&_northAmericanCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_cupPickAsiaInviteFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAsiaAsianCupFriendly[5] = { (Competition *)&_asianCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAsiaWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAsiaWorldCupInvite[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickAsiaAsianCupNonFinalist[5] = { 0, (Competition *)-2, (Competition *)&_asianCupCopy, (Competition *)-1, 0 };
Competition *_cupPickAsiaAsianCupFinalist[5] = { (Competition *)&_asianCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_cupPickAfricaInviteFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAfricaAfricanCupFriendly[5] = { (Competition *)&_africanNationsCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAfricaWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickAfricaWorldCupInvite[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickAfricaAfricanCupNonFinalist[5] = { 0, (Competition *)-2, (Competition *)&_africanNationsCupCopy, (Competition *)-1, 0 };
Competition *_cupPickAfricaAfricanCupFinalist[5] = { (Competition *)&_africanNationsCupCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_cupPickSAmericaInviteFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickSAmericaCopaAmericaFriendly[5] = { (Competition *)&_copaAmericaCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickSAmericaWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickSAmericaWorldCupInvite[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickSAmericaCopaAmericaNonFinalist[5] = { 0, (Competition *)-2, (Competition *)&_copaAmericaCopy, (Competition *)-1, 0 };
Competition *_cupPickSAmericaCopaAmericaFinalist[5] = { (Competition *)&_copaAmericaCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_ecQualifyingGroupsTable[8] = {
    (Competition *)&_ecQualifyingGroup1Copy, (Competition *)&_ecQualifyingGroup2Copy,
    (Competition *)&_ecQualifyingGroup3Copy, (Competition *)&_ecQualifyingGroup4Copy,
    (Competition *)&_ecQualifyingGroup5Copy, (Competition *)&_ecQualifyingGroup6Copy,
    (Competition *)&_ecQualifyingGroup7Copy, (Competition *)&_ecQualifyingGroup8Copy
};

Competition *_cupPickEuropeNoCupFriendly[5] = { 0, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickListEuropeInviteTournamentFriendly[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickLisEuropeEuroChampsFriendly[5] = { (Competition *)&_europeanChampionships2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickListEuropeEuroChampsInviteFriendly[5] = { (Competition *)&_europeanChampionships2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickListEuropeWorldCupFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickLisEuropeWorldCupInviteFriendly[5] = { (Competition *)&_worldCup2Copy, (Competition *)-2, (Competition *)&_invitationTournamentCopy, (Competition *)&_friendlyCopy, (Competition *)-1 };
Competition *_cupPickEuropeNoCupFriendlyAlt[5] = { 0, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };
Competition *_cupPickListEuropeInviteTournamentFriendlyAlt[5] = { (Competition *)&_invitationTournamentCopy, (Competition *)-2, (Competition *)&_friendlyCopy, (Competition *)-1, 0 };

Competition *_worldCupQualifyingGroupsTable[8] = {
    (Competition *)&_worldCupQualifyingGroup1Copy, (Competition *)&_worldCupQualifyingGroup2Copy,
    (Competition *)&_worldCupQualifyingGroup3Copy, (Competition *)&_worldCupQualifyingGroup4Copy,
    (Competition *)&_worldCupQualifyingGroup5Copy, (Competition *)&_worldCupQualifyingGroup6Copy,
    (Competition *)&_worldCupQualifyingGroup7Copy, (Competition *)&_worldCupQualifyingGroup8Copy
};

/* Tour string table */
extern char _aEuropeanTour[];
extern char _aAfricanTour[];
extern char _aSouthAmericanT[];
extern char _aNorthAmericanT[];
extern char _aAsianTour[];
extern char _aOceaniaTour[];
char *_tourStringTable[6] = { _aEuropeanTour, _aAfricanTour, _aSouthAmericanT, _aNorthAmericanT, _aAsianTour, _aOceaniaTour };

/* Career region weights */
uint8_t _careerRegionPickWeightsTable[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4 };
uint8_t _careerRegionPickWeightsTable2[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4 };

/* ======================================================================
 * competitionsTable[256] — country-code → country competition list.
 *
 * Indexed by team-file country byte (CN_*). Most slots are zero (no
 * country at that index). The special entries are 72 (customsTable for
 * ORIGINAL_CUSTOM_TEAMS), 100 (customsTable for EDITED_CUSTOM_TEAMS),
 * 80–85 (region tables), 252–254 (qualifying-group / world tables).
 *
 * Without this, SelectTeamsFinalMenu reads competitionsTable[254]=0 for
 * the CLUB_TEAMS path, panics with code 0x8632, and falls into
 * PanicHaltLoop → FadeOutToBlack → spin forever waiting for both
 * players' fire buttons (looks like a black screen on SDL). Layout
 * transcribed verbatim from SWOS2.s lines 124186–124443. */
void *_competitionsTable[256] = {
    [0] = _albaniaTable,
    [1] = _austriaTable,
    [2] = _belgiumTable,
    [3] = _bulgariaTable,
    [4] = _croatiaTable,
    [5] = _cyprusTable,
    [6] = _czechRepublicTable,
    [7] = _denmarkTable,
    [8] = _englandTable,
    [10] = _estoniaTable,
    [11] = _faroeIslandsTable,
    [12] = _finlandTable,
    [13] = _franceTable,
    [14] = _germanyTable,
    [15] = _greeceTable,
    [16] = _hungaryTable,
    [17] = _icelandTable,
    [18] = _republicIrelandTable,
    [19] = _israelTable,
    [20] = _italyTable,
    [21] = _latviaTable,
    [22] = _lithuaniaTable,
    [23] = _luxembourgTable,
    [24] = _maltaTable,
    [25] = _hollandTable,
    [26] = _northernIrelandTable,
    [27] = _norwayTable,
    [28] = _polandTable,
    [29] = _portugalTable,
    [30] = _romaniaTable,
    [31] = _russiaTable,
    [32] = _sanMarinoTable,
    [33] = _scotlandTable,
    [34] = _sloveniaTable,
    [35] = _spainTable,
    [36] = _swedenTable,
    [37] = _switzerlandTable,
    [38] = _turkeyTable,
    [39] = _ukraineTable,
    [40] = _walesTable,
    [41] = _yugoslaviaTable,
    [42] = _algeriaTable,
    [43] = _argentinaTable,
    [44] = _australiaTable,
    [45] = _boliviaTable,
    [46] = _brasilTable,
    [48] = _chileTable,
    [49] = _colombiaTable,
    [50] = _ecuadorTable,
    [51] = _elSalvadorTable,
    [55] = _japanTable,
    [60] = _mexicoTable,
    [62] = _newZealandTable,
    [64] = _paraguayTable,
    [65] = _peruTable,
    [66] = _surinamTable,
    [67] = _taiwanTable,
    [69] = _southAfricaTable,
    [71] = _uruguayTable,
    [72] = &_customsTable,
    [73] = _usaTable,
    [75] = _indiaTable,
    [76] = _belarusTable,
    [77] = _venezuelaTable,
    [78] = _slovakiaTable,
    [79] = _ghanaTable,
    [80] = &_europeTable,
    [81] = &_africaTable,
    [82] = &_southAmericaTable,
    [83] = &_northAmericaTable,
    [84] = &_asiaTable,
    [85] = &_oceaniaTable,
    [100] = &_customsTable,
    [252] = &_worldCupQualifyingGroups,
    [253] = &_ecQualifyingGroups,
    [254] = &_worldTable,
};

/* ======================================================================
 * DIY Cup Choice Variables (zero-init)
 * ====================================================================== */

uint16_t _diyCupLegsChoice0 = 0;
uint16_t _diyCupExtraTimeChoice0 = 0;
uint16_t _diyCupPenaltiesChoice0 = 0;
uint16_t _diyCupLegsChoice1 = 0;
uint16_t _diyCupExtraTimeChoice1 = 0;
uint16_t _diyCupPenaltiesChoice1 = 0;
uint16_t _diyCupLegsChoice2 = 0;
uint16_t _diyCupExtraTimeChoice2 = 0;
uint16_t _diyCupPenaltiesChoice2 = 0;
uint16_t _diyCupLegsChoice3 = 0;
uint16_t _diyCupExtraTimeChoice3 = 0;
uint16_t _diyCupPenaltiesChoice3 = 0;
uint16_t _diyCupLegsChoice4 = 0;
uint16_t _diyCupExtraTimeChoice4 = 0;
uint16_t _diyCupPenaltiesChoice4 = 0;
uint16_t _diyCupLegsChoice5 = 0;
uint16_t _diyCupExtraTimeChoice5 = 0;
uint16_t _diyCupPenaltiesChoice5 = 0;

uint16_t _diyEditParamB = 0;
uint16_t _diyEditParamC = 0;

#endif /* COMPETITION_DATA_C */
