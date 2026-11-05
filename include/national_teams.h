#ifndef NATIONAL_TEAMS_H
#define NATIONAL_TEAMS_H

/* The national teams' records (_nationalTeams: NationalTeamRecord, game_types.h; _numNationalTeams
 * of them counted) and the career manager's nationalities (_nationalityOrdinalsArray, OTHER =
 * 0xFF last). Compiled: the original's 63 records (of the 144 its table holds: the rest have no
 * nationality or team file) and 63 nationalities + OTHER. On SDL a mod may bring its own,
 * data_json/national_teams.json through the overlay (DataJson_LoadNationalTeams,
 * src/save_format_json.c; docs/DATA_PIPELINE.md): then the counts are its lengths. Without one,
 * the compiled tables, byte for byte. */

#ifdef SWOS_SDL_BUILD
#define NATIONAL_TEAMS_MAX   256u   /* records */
#define NATIONALITIES_MAX    128u   /* the career's nationalities, OTHER included */
#else
#define NATIONAL_TEAMS_MAX   144u   /* the original's table */
#define NATIONALITIES_MAX    64u
#endif

/* A record's team file when the nation has no club league in the game: its national selector's
 * ADD INTERNATIONAL PLAYER has no HOME (src/menus.c NationalTeamAddPlayerInit). */
#define NATIONAL_TEAM_NO_FILE 0xFFu

/* SDL: the mod's table, when there is one (frontend.c, at start-up, after the mods). */
void DataJson_LoadNationalTeams(void);

#endif
