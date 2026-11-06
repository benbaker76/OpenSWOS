/*
 * car_rebuild.c -- a career from an ORIGINAL SWOS save (.CAR), rebuilt: the Amiga's 96/97, or
 * the PC game's -- SWOS 96/97 on DOS, and SWOS 2020, which saves careers as the PC game does.
 *
 * OpenSWOS reads only its own JSON careers; an original save is a raw dump of the game's
 * careerFileBuffer (the Amiga's 98,476 bytes of header, the PC's 95,151; then numSelectedTeams
 * and 684 bytes a team), full of that machine's addresses. Rather than translate the whole dump, this starts a NEW
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
 * Offsets: the labels of careerFileBuffer, counted from careerFileBuffer -- data/SWOS2.s's for
 * the Amiga, the PC game's swos.asm for the PC (the same fields; most 2 bytes further on, the
 * manager's name 3, the year elsewhere: CarLayout below). Byte order: the Amiga's file is
 * big-endian, the PC's little-endian for its numbers -- but a team's number is a (country,
 * ordinal) byte pair on both, read as such. A file is told by its shape (header + 2 + 684 a
 * team, the count where its header ends), so the right layout needs no option.
 *
 * SWOS 2020 careers are of its own team database (a .DB beside the .CAR): the saved team is
 * looked up by NAME in its country, so it is the same club in this game's database (its
 * number there may differ); the squad as saved comes with it whatever the database.
 */
#include "game_types.h"
#include "endian_helpers.h"
#include "team_db.h"           /* this_games_team */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_SI_SIZE              106u    /* SeasonInformations, on both */
#define OC_SI_COUNT             20u
#define OC_CAREER_TEAM_SIZE     1292u   /* TeamFileEx + 16 more players */

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

/* The management record: each a u16 (RECORDS entries, in CarLayout.record's order). */
static const struct { uint16_t *dst; const char *name; } s_record[] = {
    { &_gamesPlayed,             "games" },
    { &_numWins,                 "wins" },
    { &_numDrawn,                "drawn" },
    { &_numLost,                 "lost" },
    { &_goalsScored,             "goals for" },
    { &_goalsTaken,              "goals against" },
    { &_careerReputationPoints,  "reputation" },
    { &_careerSeasonsCompleted,  "seasons completed" },
    { &_winCupBonusCount,        "cup bonuses" },
    { &_minorTrophyBonusCount,   "minor trophies" },
    { &_majorTrophyBonusCount,   "major trophies" },
    { &_wonWorldCup,             "won World Cup" },
    { &_wonContinentalCup,       "won continental cup" },
    { &_wonInvitationTournament, "won invitation" },
    { &_boardConfidenceScore,    "board confidence" },
};
#define RECORDS ((int32_t)(sizeof s_record / sizeof s_record[0]))

/* A game's name strings: where each is in that game's memory -> its text. A save's history
 * rows point at them, so the rows' four competition names are found by the one alignment
 * that puts every such pointer exactly on a name. */
typedef struct { int32_t at; const char *name; } OrigName;
/* The Amiga's: byte offset from aPremierLeague (generated from data/SWOS2.s). */
static const OrigName s_amigaNames[] = {
#include "car_rebuild_names.inc"
};
/* The PC game's: address in its data segment (generated from its swos.asm). */
static const OrigName s_pcNames[] = {
#include "car_rebuild_names_dos.inc"
};

/* Where a version keeps each field (careerFileBuffer offsets). */
typedef struct {
    const char     *what;
    int32_t         littleEndian;           /* its numbers (a team's number never) */
    uint32_t        header;                 /* to numSelectedTeams */
    uint32_t        currentSeason, newBalance, oldBalance, boardConfHist, selTeamNumber,
                    coachOrPlayer, mrOrMs, managerName, managerSurname, unavailLookup,
                    unavailNames, currentMatchPlayers, careerTeam, careerNumPlayers,
                    seasonInfos, seasonPlaying;
    uint32_t        record[15];             /* s_record's, in its order */
    const OrigName *names;
    int32_t         numNames;
} CarLayout;

static const CarLayout s_layouts[] = {
    {   "an original Amiga career", 0, 98476u,
        /* data/SWOS2.s */
        54722u, 54746u, 54750u, 54980u, 55112u, 55114u, 55126u, 55132u, 55141u, 55738u,
        55818u, 56158u, 56190u, 57482u, 57488u, 96209u,
        { 55022u, 55024u, 55026u, 55028u, 55030u, 55032u, 55162u, 55410u, 55156u, 55158u,
          55160u, 55004u, 55006u, 55008u, 54988u },
        s_amigaNames, (int32_t)(sizeof s_amigaNames / sizeof s_amigaNames[0]) },
    {   "a PC (DOS / SWOS 2020) career", 1, 95151u,
        /* the PC game's swos.asm: most fields 2 bytes on from the Amiga's (its manager's name
         * 3: managerSexBuffer is a byte longer), the year (seasonPlaying) elsewhere -- the
         * market before it is laid out differently */
        54724u, 54748u, 54752u, 54982u, 55114u, 55116u, 55128u, 55135u, 55144u, 55740u,
        55820u, 56160u, 56192u, 57484u, 57490u, 92861u,
        { 55024u, 55026u, 55028u, 55030u, 55032u, 55034u, 55164u, 55412u, 55158u, 55160u,
          55162u, 55006u, 55008u, 55010u, 54990u },
        s_pcNames, (int32_t)(sizeof s_pcNames / sizeof s_pcNames[0]) },
};

static const CarLayout *s_lay;      /* the file's */

static uint16_t rd16(const uint8_t *b, uint32_t o)
{
    return s_lay->littleEndian ? LE_READ_U16(b + o) : BE_READ_U16(b + o);
}
static int32_t  rd32(const uint8_t *b, uint32_t o)
{
    return (int32_t)(s_lay->littleEndian ? LE_READ_U32(b + o) : BE_READ_U32(b + o));
}
/* a team's number: (country, ordinal) bytes on both */
static uint16_t rd_team(const uint8_t *b, uint32_t o) { return BE_READ_U16(b + o); }

static const char *orig_name_at(int32_t at)
{
    int32_t lo = 0, hi = s_lay->numNames - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        if (s_lay->names[mid].at == at) return s_lay->names[mid].name;
        if (s_lay->names[mid].at < at) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* The game's own string when the history screen tells it by ADDRESS (statistics.c:
 * FRIENDLY, and the tours), else the name's text. */
static uint8_t *game_name(const char *text)
{
    extern uint8_t _aFriendly[], _aEuropeanTour[], _aAfricanTour[], _aSouthAmericanT[],
                   _aNorthAmericanT[], _aAsianTour[], _aOceaniaTour[];
    uint8_t *const own[] = { _aFriendly, _aEuropeanTour, _aAfricanTour, _aSouthAmericanT,
                             _aNorthAmericanT, _aAsianTour, _aOceaniaTour };
    for (uint32_t i = 0; i < sizeof own / sizeof own[0]; i++)
        if (strcmp(text, (const char *)own[i]) == 0)
            return own[i];
    return (uint8_t *)text;
}

/* The base address (in the writer's machine) that puts the most of `ptr[0..n)` on a
 * name; 0 when none puts them all. */
static uint32_t orig_names_base(const uint32_t *ptr, int32_t n)
{
    uint32_t best = 0;
    int32_t bestHits = 0;
    if (n == 0) return 0;
    for (int32_t k = 0; k < s_lay->numNames; k++) {
        uint32_t base = ptr[0] - (uint32_t)s_lay->names[k].at;
        int32_t hits = 0;
        for (int32_t j = 0; j < n; j++)
            if (orig_name_at((int32_t)(ptr[j] - base))) hits++;
        if (hits > bestHits) { bestHits = hits; best = base; }
        if (hits == n) break;
    }
    return bestHits == n ? best : 0;
}

/* The saved team in THIS game's database: the same number if it is the same club, else the
 * club of that name in its country (a SWOS 2020 career is of its own database). */
static uint16_t this_games_team(uint16_t teamNo, const TeamFileEx *saved)
{
    char want[18];
    memcpy(want, saved->team.teamName, 17);
    want[17] = 0;
    const int32_t country = teamNo >> 8;
    const TeamDbEntry *e = TeamDb_Team(country, teamNo & 0xFF);
    if (e && strncmp((const char *)e->team.team.teamName, want, 17) == 0)
        return teamNo;
    for (int32_t i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++)
        if (strncmp((const char *)e->team.team.teamName, want, 17) == 0) {
            const uint16_t found = PACK_BE_U16(e->team.team.countryNumber, e->team.team.teamOrdinal);
            printf("[rebuild]   %s is %04x in this database (%04x in the save's)\n", want,
                   (unsigned)found, (unsigned)teamNo);
            return found;
        }
    e = TeamDb_Team(country, teamNo & 0xFF);
    printf("[rebuild]   %s is not in this database's country %d by name: its number, %04x, is %.17s here\n",
           want, (int)country, (unsigned)teamNo, e ? (const char *)e->team.team.teamName : "no team");
    return teamNo;
}

/* One finished season's history row, field by field (on 64-bit the struct holds 8-byte
 * pointers, so it is not the file's 106 bytes). */
static void read_season_row(SeasonInformations *si, const uint8_t *r)
{
    memset(si, 0, sizeof *si);
    si->clubNo = BE_READ_U16(r + 0x00);     /* (a team's number: bytes) */
    memcpy(si->clubName, r + 0x02, sizeof si->clubName);
    si->trophyFlags = (uint16_t)rd16(r, 0x14);
    /* 0x16-0x25: four pointers into the original machine's memory -- left NULL */
    si->bestScorerNumGoals = (uint16_t)rd16(r, 0x26);
    memcpy(si->bestScorerName, r + 0x28, sizeof si->bestScorerName);
    memcpy(si->secondScorerName, r + 0x3F, sizeof si->secondScorerName);
    si->comp1Result = (int8_t)r[0x56];
    si->comp2Result = (int8_t)r[0x57];
    si->comp3Result = (int8_t)r[0x58];
    si->comp4Result = (int8_t)r[0x59];
    si->totalGoalsScored = (uint16_t)rd16(r, 0x5A);
    si->totalGoalsTaken  = (uint16_t)rd16(r, 0x5C);
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

    /* An original career: the header, numSelectedTeams, then that many 684-byte teams --
     * the Amiga's header or the PC's, its count in that version's byte order. */
    s_lay = NULL;
    uint32_t teams = 0;
    for (uint32_t k = 0; k < sizeof s_layouts / sizeof s_layouts[0] && !s_lay && b[0] != '{'; k++) {
        const CarLayout *L = &s_layouts[k];
        if ((uint32_t)size < L->header + 2u)
            continue;
        s_lay = L;
        teams = (uint32_t)rd16(b, L->header);
        if ((uint32_t)size != L->header + 2u + teams * 684u)
            s_lay = NULL;
    }
    if (!s_lay) {
        printf("[rebuild] %s is not an original Amiga or PC career (%ld bytes)\n", path, size);
        free(b);
        return 1;
    }
    printf("[rebuild] %s: %s, %u teams\n", path, s_lay->what, (unsigned)teams);

    const uint16_t teamNo = this_games_team(rd_team(b, s_lay->selTeamNumber),
                                            (const TeamFileEx *)(b + s_lay->careerTeam));
    const uint16_t season = rd16(b, s_lay->currentSeason);
    const uint8_t  year   = b[s_lay->seasonPlaying];
    if (season >= OC_SI_COUNT || year >= 100) {
        printf("[rebuild] season %u / year %u out of range\n", (unsigned)season, (unsigned)year);
        free(b);
        return 1;
    }

    /* The manager, before the career starts (as the CAREER menu sets them). */
    _coachOrPlayer = rd16(b, s_lay->coachOrPlayer);
    _mrOrMs = rd16(b, s_lay->mrOrMs);
    memcpy(_managerNameBuffer, b + s_lay->managerName, 9);
    memcpy(_managerSurnameBuffer, b + s_lay->managerSurname, 13);

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
    memcpy(_careerTeam, b + s_lay->careerTeam, OC_CAREER_TEAM_SIZE);
    ct->team.globalTeamNumber = globalNo;
    memcpy(&_selectedTeams[0], _careerTeam, sizeof(TeamFileEx));
    /* Which slots ARE the squad, in squad order (all 32): a slot not in it is a player who
     * left -- sold or released, his name stamped "*ERR* " (teams.c) -- and must stay out. */
    memcpy(_currentMatchPlayers, b + s_lay->currentMatchPlayers, 32);
    _careerNumPlayers = rd16(b, s_lay->careerNumPlayers);
    /* A national squad's players each come from a club: per squad slot the club's team
     * number and the player's index in it ({u16, u16}, host order here) and the club's
     * name (17 bytes) -- the squad screen's CLUBS view and the call-ups read them. */
    for (int32_t i = 0; i < 40; i++)    /* {team: bytes, index: a number} */
        _careerUnavailablePlayerLookupTable[i] = (i & 1) ? rd16(b, s_lay->unavailLookup + 2u * (uint32_t)i)
                                                         : rd_team(b, s_lay->unavailLookup + 2u * (uint32_t)i);
    memcpy(_careerUnavailablePlayerNames, b + s_lay->unavailNames, 20 * 17);
    for (uint16_t i = 0; i < _careerNumPlayers; i++)   /* each wage, then the week's total */
        Career_UpdatePlayerWeeklyWageCache(i);

    /* The management record, the board, and the money -- exact. */
    for (int32_t i = 0; i < RECORDS; i++)
        *s_record[i].dst = rd16(b, s_lay->record[i]);
    for (int32_t i = 0; i < 4; i++)
        _boardConfidenceHistory[i] = rd16(b, s_lay->boardConfHist + 2u * (uint32_t)i);
    _newBalance = rd32(b, s_lay->newBalance);
    _old_balance = rd32(b, s_lay->oldBalance);
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
            uint32_t p = (uint32_t)rd32(b, s_lay->seasonInfos + (uint32_t)i * OC_SI_SIZE + 0x16 + 4 * s);
            if (p) ptrs[nptr++] = p;
        }
    const uint32_t base = orig_names_base(ptrs, nptr);
    for (uint16_t i = 0; i < season; i++) {
        const uint8_t *r = b + s_lay->seasonInfos + (uint32_t)i * OC_SI_SIZE;
        uint8_t **slot[4] = { &si[i].leagueStrPtr, &si[i].strPtr1, &si[i].strPtr2, &si[i].strPtr3 };
        read_season_row(&si[i], r);
        for (uint32_t s = 0; s < 4; s++) {
            uint32_t p = (uint32_t)rd32(r, 0x16 + 4 * s);
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
    for (int32_t i = 0; i < RECORDS; i++)
        printf("[rebuild]   %-20s %u\n", s_record[i].name, (unsigned)*s_record[i].dst);
    for (uint16_t i = 0; i < season; i++)
        printf("[rebuild]   history %u: %.18s trophies %04x  %s / %s / %s / %s\n", (unsigned)i,
               (const char *)si[i].clubName, (unsigned)si[i].trophyFlags,
               si[i].leagueStrPtr ? (const char *)si[i].leagueStrPtr : "-", si[i].strPtr1 ? (const char *)si[i].strPtr1 : "-",
               si[i].strPtr2 ? (const char *)si[i].strPtr2 : "-", si[i].strPtr3 ? (const char *)si[i].strPtr3 : "-");
    free(b);
    return 0;
}
