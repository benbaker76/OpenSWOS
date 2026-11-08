/* team_db.h -- the team database as CANONICAL data: every team and player by its id,
 * and what each id stands for (src/team_db.c, docs/TEAM_IDS.md). SDL only.
 *
 * A copy of the database of its own, read once per country: the game's team buffer
 * belongs to whatever menu the player is in, and this is also asked from the lobby's
 * polling. */
#ifndef TEAM_DB_H
#define TEAM_DB_H

#include "game_types.h"

#include "team_uuid.h"     /* TEAM_UUID_TEXT, TEAM_CHECKSUM_TEXT; the derived 96/97 ids */
#define TEAM_PLAYERS        16

/* The ids a database file states for one team ("" where it states none). */
typedef struct TeamDbIds {
    char uuid[TEAM_UUID_TEXT];
    char checksum[TEAM_CHECKSUM_TEXT];
    char player[TEAM_PLAYERS][TEAM_UUID_TEXT];
} TeamDbIds;

typedef struct TeamDbEntry {
    TeamFileEx team;                /* as the database has it */
    TeamDbIds  ids;                 /* stated by the database, else derived (96/97) */
    char       computed[TEAM_CHECKSUM_TEXT];    /* the checksum of `team` as it stands */
    int32_t    canonical;           /* the stated checksum is the computed one */
    int32_t    world;               /* the database it is in: a WorldId (include/world.h) */
} TeamDbEntry;

/* TWO DATABASES. A world (World 2026-27) replaces a country's file in what the game reads, so
 * the team with a number is the world's while it is played -- and 96/97's team with that number
 * is still a team, which a custom team may have been made from. So: the world being played
 * (TeamDb_Entry, TeamDb_Team: what the game's lists show), and 96/97's under it (the *9697
 * calls; the same as the others while 96/97 is played). Each entry says which it is in. */

/* The teams filed under a country's number (the number of its TEAM.nnn), in the file's
 * order; NULL past the last, or where there is no such file. The world being played. */
const TeamDbEntry *TeamDb_Entry(int32_t country, int32_t index);
/* The team with this number, in the file of its country. The world being played. */
const TeamDbEntry *TeamDb_Team(int32_t country, int32_t ordinal);
/* The same in 96/97's database, whatever world is played. */
const TeamDbEntry *TeamDb_Entry9697(int32_t country, int32_t index);
const TeamDbEntry *TeamDb_Team9697(int32_t country, int32_t ordinal);
/* The team with this id, wherever it is: the world being played, then 96/97. */
const TeamDbEntry *TeamDb_ByUuid(const char *uuid);

/* What a team's id stands for: 16 hex digits of the SHA-1 of its sixteen players'
 * position, seven skills and value -- everything a match plays it by, nothing else. */
void TeamDb_Checksum(const TeamFileEx *team, char out[TEAM_CHECKSUM_TEXT]);

/* (TeamDb_DerivedTeamUuid / TeamDb_DerivedPlayerUuid: include/team_uuid.h) */

/* Every team of the database: its stated ids are the derived ones, its stated checksum
 * the computed one, and no id is used twice. Prints one line; 1 = all of that holds. */
int32_t TeamDb_SelfTest(void);

#endif /* TEAM_DB_H */
