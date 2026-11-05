/* team_db.c -- the team database as canonical data. SDL only. docs/TEAM_IDS.md.
 *
 * Every team and every player has an id (a UUID), and every team a checksum of what
 * its id stands for. A custom team's file links to them instead of carrying skills
 * (src/custom_team.c); a downloadable team will be known by them.
 *
 *   AN ID NAMES FIXED DATA. A team whose players, positions, skills or values change
 *   is a new team with a new id. That is what lets a file, or the other player, or the
 *   server, say "team X" and mean one exact set of numbers.
 *
 *   THE CHECKSUM is those numbers: 16 hex digits of the SHA-1 of nine bytes a player --
 *   position, passing, velocity, heading, tackling, control, speed, finishing, price --
 *   for the sixteen in order. Names, faces and kits are not in it. The database states
 *   it beside the id and it is recomputed here on load; a team where the two differ
 *   (a skill edited, the id kept) is not canonical, and is not offered as what its id
 *   stands for.
 *
 *   THE IDS OF THE 96/97 DATABASE ARE DERIVED (uuid5, below), so every copy agrees on
 *   them untold, and the binary TEAM.nnn files -- which have nowhere to keep one --
 *   have them too. The JSON database states them all the same (scripts/
 *   assign_team_uuids.py), and what a file states is what is used: a team added later
 *   has an id of its own that no formula gives.
 *
 * What is NOT here is authority. A player who edits a skill and the checksum beside it
 * has a database that agrees with itself. Only a list kept somewhere else -- the
 * server's, of id and checksum -- can say that team is not the canonical one. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "team_db.h"
#include "endian_helpers.h"
#include "swos_io.h"

extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern int     DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);
extern int     DataJson_LoadTeamIdsForCountry(int countryNum, struct TeamDbIds *out, int maxTeams);

enum {
    kTeamRecord = 684,
    kMaxTeamsInFile = TEAM_FILE_MAX_TEAMS,  /* what the game's own team buffer holds (game_types.h) */
    kCountries = 256,
    kOwnCustoms = 100,              /* CUSTOMS.EDT, the player's own edited teams: not the database */
};

/* (SHA-1, uuid5 and the derived 96/97 ids: src/team_uuid.c, both targets) */

void TeamDb_Checksum(const TeamFileEx *team, char out[TEAM_CHECKSUM_TEXT])
{
    TeamSha1 s;
    uint8_t digest[20];
    int32_t i;

    TeamSha1_Start(&s);
    for (i = 0; i < TEAM_PLAYERS; i++) {
        const PlayerFile *p = &team->players[i];
        const uint8_t plays[9] = {
            (uint8_t)((p->positionAndFace & PF_POSITION_MASK) >> 5),
            (uint8_t)(p->passing & 15),
            (uint8_t)((p->velocityHeading >> 4) & 15), (uint8_t)(p->velocityHeading & 15),
            (uint8_t)((p->tacklingControl >> 4) & 15), (uint8_t)(p->tacklingControl & 15),
            (uint8_t)((p->speedFinishing >> 4) & 15),  (uint8_t)(p->speedFinishing & 15),
            (uint8_t)p->price,
        };
        TeamSha1_Add(&s, plays, sizeof plays);
    }
    TeamSha1_End(&s, digest);
    TeamUuid_Hex(digest, (TEAM_CHECKSUM_TEXT - 1) / 2, out);
}

/* ---- the database, a country at a time ---- */

static struct Country {
    TeamDbEntry *entries;
    int32_t      count;
    int32_t      absent;
} s_country[kCountries];

/* One country's teams as LoadTeamFile reads them -- the JSON database unless it is
 * turned off or has no such country, then the binary TEAM.nnn (big-endian count) --
 * into `records` (a count word, then the records); the ids the JSON states into `ids`,
 * zeroed where it states none. Returns the number of teams, 0 if there is no file. */
static int32_t read_country(int32_t country, uint8_t *records, uint32_t size, TeamDbIds *ids)
{
    const char *json = getenv("SWOS_DATA_JSON");
    char name[32];
    uint32_t got = 0;
    int32_t count;

    memset(ids, 0, sizeof *ids * kMaxTeamsInFile);
    if (!(json && json[0] == '0' && json[1] == '\0') &&
        (count = DataJson_LoadTeamFileForCountry(country, records, kMaxTeamsInFile)) > 0) {
        if (DataJson_LoadTeamIdsForCountry(country, ids, kMaxTeamsInFile) != count)
            memset(ids, 0, sizeof *ids * kMaxTeamsInFile);
        return count;
    }
    snprintf(name, sizeof name, "data/team.%03d", (int)country);
    {
        /* LoadFileEx asks for the disk again, for ever, when a file is not there --
         * and a country number from the other player's team may be any byte. */
        char anchored[1024];
        FILE *f = fopen(swos_data_path(name, anchored, (uint32_t)sizeof anchored), "rb");
        if (!f)
            return 0;
        fclose(f);
    }
    if (LoadFileEx((uint8_t *)name, records, &got) != 0 || got < 2 || got > size)
        return 0;
    count = BE_READ_U16(records);
    return (uint32_t)count * kTeamRecord + 2 <= got ? count : 0;
}

static struct Country *country_of(int32_t country)
{
    static uint8_t   records[2 + kMaxTeamsInFile * kTeamRecord];
    static TeamDbIds ids[kMaxTeamsInFile];
    struct Country *c;
    int32_t i, p;

    if (country < 0 || country >= kCountries || country == kOwnCustoms)
        return NULL;
    c = &s_country[country];
    if (c->absent)
        return NULL;
    if (c->entries)
        return c;
    c->count = read_country(country, records, (uint32_t)sizeof records, ids);
    c->entries = c->count > 0 ? (TeamDbEntry *)calloc((size_t)c->count, sizeof *c->entries) : NULL;
    if (!c->entries) {
        c->absent = 1;
        c->count = 0;
        return NULL;
    }
    for (i = 0; i < c->count; i++) {
        TeamDbEntry *e = &c->entries[i];
        memcpy(&e->team, records + 2 + (size_t)i * kTeamRecord, sizeof e->team);
        e->ids = ids[i];
        /* What the file does not state is the 96/97 database's, by the formula. */
        if (!e->ids.uuid[0])
            TeamDb_DerivedTeamUuid(e->team.team.countryNumber, e->team.team.teamOrdinal, e->ids.uuid);
        for (p = 0; p < TEAM_PLAYERS; p++)
            if (!e->ids.player[p][0])
                TeamDb_DerivedPlayerUuid(e->team.team.countryNumber, e->team.team.teamOrdinal, p,
                                         e->ids.player[p]);
        TeamDb_Checksum(&e->team, e->computed);
        if (!e->ids.checksum[0])
            snprintf(e->ids.checksum, sizeof e->ids.checksum, "%s", e->computed);
        e->canonical = strcmp(e->ids.checksum, e->computed) == 0;
        if (!e->canonical)
            fprintf(stderr, "[team-db] %.16s (%s): the database says its checksum is %s and its players make %s -- "
                            "its positions, skills or values were changed and its id was not; it is not used as "
                            "that team\n", (const char *)e->team.team.teamName, e->ids.uuid,
                    e->ids.checksum, e->computed);
    }
    return c;
}

const TeamDbEntry *TeamDb_Entry(int32_t country, int32_t index)
{
    const struct Country *c = country_of(country);
    return c && index >= 0 && index < c->count ? &c->entries[index] : NULL;
}

const TeamDbEntry *TeamDb_Team(int32_t country, int32_t ordinal)
{
    const struct Country *c = country_of(country);
    int32_t i;

    for (i = 0; c && i < c->count; i++)
        if (c->entries[i].team.team.countryNumber == country && c->entries[i].team.team.teamOrdinal == ordinal)
            return &c->entries[i];
    return NULL;
}

const TeamDbEntry *TeamDb_ByUuid(const char *uuid)
{
    int32_t country, i;

    for (country = 0; country < kCountries; country++) {
        const struct Country *c = country_of(country);
        for (i = 0; c && i < c->count; i++)
            if (strcmp(c->entries[i].ids.uuid, uuid) == 0)
                return &c->entries[i];
    }
    return NULL;
}

int32_t TeamDb_SelfTest(void)
{
    /* The formula itself, against Python's uuid.uuid5 (scripts/assign_team_uuids.py). */
    static const struct { int32_t country, ordinal, player; const char *uuid; } k_known[] = {
        { 0, 0, -1, "ed3fd1fc-88e8-5409-81e0-9ca58216bae5" },      /* ALBPETROL PATOS */
        { 82, 2, -1, "30e26617-4001-56c3-a068-8d88849c8398" },     /* BRAZIL, the national side */
        { 0, 0, 0, "d00a3fd0-9352-5a70-9ec6-48e46e1df67c" },       /* its first player */
    };
    int32_t country, i, p, teams = 0, ids = 0, bad = 0;
    char want[TEAM_UUID_TEXT];

    for (i = 0; i < (int32_t)(sizeof k_known / sizeof k_known[0]); i++) {
        if (k_known[i].player < 0)
            TeamDb_DerivedTeamUuid(k_known[i].country, k_known[i].ordinal, want);
        else
            TeamDb_DerivedPlayerUuid(k_known[i].country, k_known[i].ordinal, k_known[i].player, want);
        if (strcmp(want, k_known[i].uuid) != 0) {
            fprintf(stderr, "[team-db] self-test: uuid5 gives %s, expected %s\n", want, k_known[i].uuid);
            bad++;
        }
    }
    for (country = 0; country < kCountries; country++) {
        const TeamDbEntry *e;
        for (i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++) {
            if (e->team.team.countryNumber != country)
                continue;                       /* a record filed under another country's number */
            teams++;
            ids += 1 + TEAM_PLAYERS;
            TeamDb_DerivedTeamUuid(country, e->team.team.teamOrdinal, want);
            if (strcmp(want, e->ids.uuid) != 0 || !e->canonical || TeamDb_ByUuid(e->ids.uuid) != e)
                bad++;
            for (p = 0; p < TEAM_PLAYERS; p++) {
                TeamDb_DerivedPlayerUuid(country, e->team.team.teamOrdinal, p, want);
                if (strcmp(want, e->ids.player[p]) != 0)
                    bad++;
            }
        }
    }
    fprintf(stderr, "[team-db] self-test: %d teams, %d ids, %d wrong\n", (int)teams, (int)ids, (int)bad);
    return teams > 0 && bad == 0;
}
