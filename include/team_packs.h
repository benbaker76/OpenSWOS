/*
 * team_packs.h -- teams a mod brings (docs/TEAM_IDS.md "Team packs", docs/MODS.md §19). SDL builds.
 *
 * A team pack is a mod's data_json/teampacks/<name>.json, in the database's own team format (the
 * header of bin/data_json/teams/<COUNTRY>.json: "type": "team", "country": <its number>, ...), each
 * team with its uuid and checksum (scripts/make_team_pack.py fills them in). Its number is its own,
 * in TEAMPACK_FIRST..TEAMPACK_LAST, outside every country and world file's: the team database
 * (src/team_db.c), match loading (LoadTeamFile) and the online team rule (CustomTeam_Allowed) take
 * its teams as they take the database's. A mod of packs is "changes": "data": both players of an
 * online match need it alike (Mods_OnlineSignature), and the server knows its teams from the list
 * the script writes beside it.
 */
#ifndef TEAM_PACKS_H
#define TEAM_PACKS_H

#include <stdint.h>

#define TEAMPACK_FIRST 210
#define TEAMPACK_LAST  249

#ifdef SWOS_SDL_BUILD
/* The pack file (a path through the mods' overlay, "data_json/teampacks/<name>.json") of that
 * country number, or NULL. */
const char *TeamPacks_File(int country);

/* The packs found (in number order), for the screens that offer them: CHOOSE TEAMS' TEAM PACKS
 * row (src/menus.c) and the mods (ModApi_PackCount / ModApi_PackInfo). Name: the file's
 * "country_name"; description: its optional "description". */
int32_t     TeamPacks_Count(void);
int32_t     TeamPacks_Info(int32_t i, int32_t *number, const char **name, const char **description,
                           int32_t *teams);

/* Once at start-up (src/main.c, after the mods): each pack a country name and a league of its
 * own at its number (_countriesTable, _competitionsTable: a copy of the customs league, the
 * pack's number its team file), so the game's team lists show it as they show ORIGINAL CUSTOM
 * TEAMS -- straight to its teams. */
void        TeamPacks_Install(void);

/* The pack CHOOSE TEAMS goes to (its TEAM PACKS row, ModApi_ChoosePackTeam), 0 none. */
void        TeamPacks_SetChosen(int32_t number);
int32_t     TeamPacks_Chosen(void);
#endif

#endif /* TEAM_PACKS_H */
