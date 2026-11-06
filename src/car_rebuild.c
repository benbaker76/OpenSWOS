/*
 * car_rebuild.c -- a career from an ORIGINAL Amiga SWOS 96/97 save (.CAR), rebuilt.
 *
 * OpenSWOS reads only its own JSON careers; an original save is a raw dump of the Amiga's
 * careerFileBuffer (98,476 bytes of header, numSelectedTeams, then 684 bytes a team), full
 * of that machine's addresses. Rather than translate the whole dump, this starts a NEW
 * career for the saved team through the game's own InitCareer -- at the saved season and
 * year, so the season's competitions are that year's -- and puts back what makes the career
 * the player's:
 *
 *   - the squad as saved: all 32 slots, reserves too (the 1,292-byte careerTeam record;
 *     a PlayerFile is all bytes, so it copies as it is) and WHICH of them are the squad, in
 *     order (currentMatchPlayers), its wages worked out again
 *   - the exact bank balance (newBalance) and last season's (old_balance)
 *   - the manager: name, surname, Mr/Ms, coach or player-coach
 *   - the management record: games, wins, draws, losses, goals, reputation, trophy-bonus
 *     counts, the World Cup / continental cup / invitation wins, the board's confidence
 *   - the history: every finished season's row (club, trophies, results, scorers)
 *
 * Lost: the season in progress restarts (tables, fixtures, cup runs), the transfer market,
 * job offers and other clubs' squads start fresh, and the finished seasons' four
 * competition names (pointers into the original machine's memory) are blank.
 *
 * Offsets: the labels of careerFileBuffer in data/SWOS2.s, counted from careerFileBuffer.
 * The file is big-endian: BE_READ_* throughout, so this is right on both targets.
 */
#include "game_types.h"
#include "endian_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* careerFileBuffer offsets (data/SWOS2.s labels) */
#define OC_HEADER_SIZE          98476u  /* &numSelectedTeams - &careerFileBuffer */
#define OC_CURRENT_SEASON       54722u  /* currentSeasonShowing (u16) */
#define OC_NEW_BALANCE          54746u  /* newBalance (s32): the bank */
#define OC_OLD_BALANCE          54750u  /* old_balance (s32) */
#define OC_BOARD_CONF_HIST      54980u  /* boardConfidenceHistory0..3 (u16 each) */
#define OC_SEL_TEAM_NUMBER      55112u
#define OC_COACH_OR_PLAYER      55114u
#define OC_MR_OR_MS             55126u
#define OC_MANAGER_NAME         55132u  /* 9 bytes */
#define OC_MANAGER_SURNAME      55141u  /* 13 bytes */
#define OC_UNAVAIL_LOOKUP       55738u  /* careerUnavailablePlayerLookupTable: 20 x {u16 team, u16 index} */
#define OC_UNAVAIL_NAMES        55818u  /* careerUnavailablePlayerNames: 20 x 17-byte club names */
#define OC_CURRENT_MATCH_PLAYERS 56158u /* 32 bytes: the squad's slots, in squad order */
#define OC_CAREER_TEAM          56190u  /* 1,292 bytes: TeamFileEx + 16 more players */
#define OC_CAREER_NUM_PLAYERS   57482u
#define OC_SEASON_INFOS         57488u  /* 20 x 106 */
#define OC_SEASON_PLAYING       96209u  /* (u8) the year, last two digits */
#define OC_SI_SIZE              106u
#define OC_SI_COUNT             20u
#define OC_CAREER_TEAM_SIZE     1292u

extern int32_t  _careerStartSeason, _careerStartYear;     /* career_init.c */
extern void     InitCareer(uint16_t teamNo);
extern void     Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
extern uint8_t  _careerTeam[];
extern uint8_t  _currentMatchPlayers[];
extern uint8_t  _managerNameBuffer[], _managerSurnameBuffer[];
extern uint16_t _coachOrPlayer, _mrOrMs, _careerNumPlayers;
extern uint16_t _gamesPlayed, _numWins, _numDrawn, _numLost, _goalsScored, _goalsTaken;
extern uint16_t _careerReputationPoints, _careerSeasonsCompleted;
extern uint16_t _winCupBonusCount, _minorTrophyBonusCount, _majorTrophyBonusCount;
extern uint16_t _wonWorldCup, _wonContinentalCup, _wonInvitationTournament;
extern uint16_t _boardConfidenceScore, _boardConfidenceHistory[4];
extern int32_t  _newBalance, _old_balance, _startingBalance, _startingBalanceCopy;
extern int32_t  _competitionBonuses;
extern uint8_t  _seasonInformationsArray[];
extern uint16_t _careerUnavailablePlayerLookupTable[40];
extern uint8_t  _careerUnavailablePlayerNames[340];

/* The management record: each a u16, by its SWOS2.s label's offset. */
static const struct { uint32_t off; uint16_t *dst; const char *name; } s_record[] = {
    { 55022u, &_gamesPlayed,             "games" },
    { 55024u, &_numWins,                 "wins" },
    { 55026u, &_numDrawn,                "drawn" },
    { 55028u, &_numLost,                 "lost" },
    { 55030u, &_goalsScored,             "goals for" },
    { 55032u, &_goalsTaken,              "goals against" },
    { 55162u, &_careerReputationPoints,  "reputation" },
    { 55410u, &_careerSeasonsCompleted,  "seasons completed" },
    { 55156u, &_winCupBonusCount,        "cup bonuses" },
    { 55158u, &_minorTrophyBonusCount,   "minor trophies" },
    { 55160u, &_majorTrophyBonusCount,   "major trophies" },
    { 55004u, &_wonWorldCup,             "won World Cup" },
    { 55006u, &_wonContinentalCup,       "won continental cup" },
    { 55008u, &_wonInvitationTournament, "won invitation" },
    { 54988u, &_boardConfidenceScore,    "board confidence" },
};

/* The original game's name strings, by byte offset from aPremierLeague (generated
 * from data/SWOS2.s). A save's history rows point at them -- addresses in the
 * machine that wrote it -- so the rows' four competition names are found by the
 * one alignment that puts every such pointer exactly on a name. */
static const struct { int32_t off; const char *name; } s_origNames[] = {
#include "car_rebuild_names.inc"
};
#define ORIG_NAMES ((int32_t)(sizeof s_origNames / sizeof s_origNames[0]))

static const char *orig_name_at(int32_t off)
{
    int32_t lo = 0, hi = ORIG_NAMES - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        if (s_origNames[mid].off == off) return s_origNames[mid].name;
        if (s_origNames[mid].off < off) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* The game's own string when the history screen tells it by ADDRESS (statistics.c:
 * FRIENDLY, and the tours' range), else the name's text. */
static uint8_t *game_name(const char *text)
{
    extern uint8_t _aFriendly[], _aEuropeanTour[], _aOceaniaTour[];
    if (strcmp(text, "FRIENDLY") == 0) return _aFriendly;
    if (strcmp(text, "EUROPEAN TOUR") == 0) return _aEuropeanTour;
    if (strcmp(text, "OCEANIA TOUR") == 0) return _aOceaniaTour;
    return (uint8_t *)text;
}

/* The base address (aPremierLeague's, in the writer's machine) that puts the most
 * of `ptr[0..n)` on a name; 0 when none puts them all. */
static uint32_t orig_names_base(const uint32_t *ptr, int32_t n)
{
    uint32_t best = 0;
    int32_t bestHits = 0;
    if (n == 0) return 0;
    for (int32_t k = 0; k < ORIG_NAMES; k++) {
        uint32_t base = ptr[0] - (uint32_t)s_origNames[k].off;
        int32_t hits = 0;
        for (int32_t j = 0; j < n; j++)
            if (orig_name_at((int32_t)(ptr[j] - base))) hits++;
        if (hits > bestHits) { bestHits = hits; best = base; }
        if (hits == n) break;
    }
    return bestHits == n ? best : 0;
}

static uint16_t rd16(const uint8_t *b, uint32_t o) { return (uint16_t)BE_READ_U16(b + o); }
static int32_t  rd32(const uint8_t *b, uint32_t o) { return (int32_t)BE_READ_U32(b + o); }

/* One finished season's history row, field by field (on 64-bit the struct holds 8-byte
 * pointers, so it is not the file's 106 bytes). */
static void read_season_row(SeasonInformations *si, const uint8_t *r)
{
    memset(si, 0, sizeof *si);
    si->clubNo = (uint16_t)BE_READ_U16(r + 0x00);
    memcpy(si->clubName, r + 0x02, sizeof si->clubName);
    si->trophyFlags = (uint16_t)BE_READ_U16(r + 0x14);
    /* 0x16-0x25: four pointers into the original machine's memory -- left NULL */
    si->bestScorerNumGoals = (uint16_t)BE_READ_U16(r + 0x26);
    memcpy(si->bestScorerName, r + 0x28, sizeof si->bestScorerName);
    memcpy(si->secondScorerName, r + 0x3F, sizeof si->secondScorerName);
    si->comp1Result = (int8_t)r[0x56];
    si->comp2Result = (int8_t)r[0x57];
    si->comp3Result = (int8_t)r[0x58];
    si->comp4Result = (int8_t)r[0x59];
    si->totalGoalsScored = (uint16_t)BE_READ_U16(r + 0x5A);
    si->totalGoalsTaken  = (uint16_t)BE_READ_U16(r + 0x5C);
    si->totalGames    = r[0x5E];
    si->totalWins     = r[0x5F];
    si->totalDrawn    = r[0x60];
    si->totalLost     = r[0x61];
    si->friendlyWins  = r[0x62];
    si->friendlyDraws = r[0x63];
    si->friendlyLost  = r[0x64];
    si->tourWins      = r[0x65];
    si->tourDraws     = r[0x66];
    si->tourLost      = r[0x67];
    si->tournamentStatus = r[0x68];
    si->managerStatus    = (int8_t)r[0x69];
}

/* Start the rebuilt career from `path` (an original .CAR). 0 when the career is set up;
 * the caller saves it (SaveJson_WriteCareer) and plays it. */
int32_t CarRebuild_Start(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[rebuild] cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc(size > 0 ? (size_t)size : 1);
    if (!b || size <= 0 || fread(b, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(b);
        printf("[rebuild] cannot read %s\n", path);
        return 1;
    }
    fclose(f);

    /* An original career: the header, numSelectedTeams, then that many 684-byte teams. */
    const uint32_t teams = (uint32_t)rd16(b, OC_HEADER_SIZE);
    if ((uint32_t)size != OC_HEADER_SIZE + 2u + teams * 684u || b[0] == '{') {
        printf("[rebuild] %s is not an original Amiga career (%ld bytes, %u teams)\n",
               path, size, (unsigned)teams);
        free(b);
        return 1;
    }

    const uint16_t teamNo = rd16(b, OC_SEL_TEAM_NUMBER);
    const uint16_t season = rd16(b, OC_CURRENT_SEASON);
    const uint8_t  year   = b[OC_SEASON_PLAYING];
    if (season >= OC_SI_COUNT || year >= 100) {
        printf("[rebuild] season %u / year %u out of range\n", (unsigned)season, (unsigned)year);
        free(b);
        return 1;
    }

    /* The manager, before the career starts (as the CAREER menu sets them). */
    _coachOrPlayer = rd16(b, OC_COACH_OR_PLAYER);
    _mrOrMs = rd16(b, OC_MR_OR_MS);
    memcpy(_managerNameBuffer, b + OC_MANAGER_NAME, 9);
    memcpy(_managerSurnameBuffer, b + OC_MANAGER_SURNAME, 13);

    /* A new career for the saved team, at the saved season and year. */
    _careerStartSeason = season;
    _careerStartYear = year;
    InitCareer(teamNo);
    _careerStartSeason = -1;
    _careerStartYear = -1;

    /* The squad as saved: every player, reserves too. The one word in the record
     * (globalTeamNumber) is the new career's, in host order, for today's database. */
    TeamFileEx *ct = (TeamFileEx *)_careerTeam;
    const int16_t globalNo = ct->team.globalTeamNumber;
    memcpy(_careerTeam, b + OC_CAREER_TEAM, OC_CAREER_TEAM_SIZE);
    ct->team.globalTeamNumber = globalNo;
    memcpy(&_selectedTeams[0], _careerTeam, sizeof(TeamFileEx));
    /* Which slots ARE the squad, in squad order (all 32): a slot not in it is a player who
     * left -- sold or released, his name stamped "*ERR* " (teams.c) -- and must stay out. */
    memcpy(_currentMatchPlayers, b + OC_CURRENT_MATCH_PLAYERS, 32);
    _careerNumPlayers = rd16(b, OC_CAREER_NUM_PLAYERS);
    /* A national squad's players each come from a club: per squad slot the club's team
     * number and the player's index in it ({u16, u16}, host order here) and the club's
     * name (17 bytes) -- the squad screen's CLUBS view and the call-ups read them. */
    for (int32_t i = 0; i < 40; i++)
        _careerUnavailablePlayerLookupTable[i] = rd16(b, OC_UNAVAIL_LOOKUP + 2u * (uint32_t)i);
    memcpy(_careerUnavailablePlayerNames, b + OC_UNAVAIL_NAMES, 20 * 17);
    for (uint16_t i = 0; i < _careerNumPlayers; i++)   /* each wage, then the week's total */
        Career_UpdatePlayerWeeklyWageCache(i);

    /* The management record, the board, and the money -- exact. */
    for (uint32_t i = 0; i < sizeof s_record / sizeof s_record[0]; i++)
        *s_record[i].dst = rd16(b, s_record[i].off);
    for (int32_t i = 0; i < 4; i++)
        _boardConfidenceHistory[i] = rd16(b, OC_BOARD_CONF_HIST + 2u * (uint32_t)i);
    _newBalance = rd32(b, OC_NEW_BALANCE);
    _old_balance = rd32(b, OC_OLD_BALANCE);
    _startingBalance = _newBalance;         /* this season starts with what the bank holds */
    _startingBalanceCopy = _newBalance;
    _competitionBonuses = 0;

    /* The finished seasons' history rows (the current one InitCareer has just made),
     * with their competition names resolved from the original pointers. */
    SeasonInformations *si = (SeasonInformations *)_seasonInformationsArray;
    uint32_t ptrs[OC_SI_COUNT * 4];
    int32_t nptr = 0;
    for (uint16_t i = 0; i < season; i++)
        for (uint32_t s = 0; s < 4; s++) {
            uint32_t p = BE_READ_U32(b + OC_SEASON_INFOS + (uint32_t)i * OC_SI_SIZE + 0x16 + 4 * s);
            if (p) ptrs[nptr++] = p;
        }
    const uint32_t base = orig_names_base(ptrs, nptr);
    for (uint16_t i = 0; i < season; i++) {
        const uint8_t *r = b + OC_SEASON_INFOS + (uint32_t)i * OC_SI_SIZE;
        uint8_t **slot[4] = { &si[i].leagueStrPtr, &si[i].strPtr1, &si[i].strPtr2, &si[i].strPtr3 };
        read_season_row(&si[i], r);
        for (uint32_t s = 0; s < 4; s++) {
            uint32_t p = BE_READ_U32(r + 0x16 + 4 * s);
            const char *t = (p && base) ? orig_name_at((int32_t)(p - base)) : NULL;
            *slot[s] = t ? game_name(t) : NULL;
        }
    }
    printf("[rebuild]   history names: %d pointers, %s\n", (int)nptr,
           nptr == 0 ? "none" : base ? "all resolved" : "NOT resolved (left blank)");

    printf("[rebuild] %s: team %04x %.17s, manager %.9s %.13s, season %u (year %02u), "
           "squad %u, balance %d\n", path, (unsigned)teamNo, (const char *)ct->team.teamName,
           (const char *)_managerNameBuffer, (const char *)_managerSurnameBuffer,
           (unsigned)season, (unsigned)year, (unsigned)_careerNumPlayers, (int)_newBalance);
    for (uint32_t i = 0; i < sizeof s_record / sizeof s_record[0]; i++)
        printf("[rebuild]   %-20s %u\n", s_record[i].name, (unsigned)*s_record[i].dst);
    for (uint16_t i = 0; i < season; i++)
        printf("[rebuild]   history %u: %.18s trophies %04x  %s / %s / %s / %s\n", (unsigned)i,
               (const char *)si[i].clubName, (unsigned)si[i].trophyFlags,
               si[i].leagueStrPtr ? (const char *)si[i].leagueStrPtr : "-", si[i].strPtr1 ? (const char *)si[i].strPtr1 : "-",
               si[i].strPtr2 ? (const char *)si[i].strPtr2 : "-", si[i].strPtr3 ? (const char *)si[i].strPtr3 : "-");
    free(b);
    return 0;
}
