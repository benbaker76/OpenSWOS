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

#define TEAMPACK_FIRST 210
#define TEAMPACK_LAST  249

#ifdef SWOS_SDL_BUILD
/* The pack file (a path through the mods' overlay, "data_json/teampacks/<name>.json") of that
 * country number, or NULL. */
const char *TeamPacks_File(int country);
#endif

#endif /* TEAM_PACKS_H */
