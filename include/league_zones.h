/*
 * league_zones.h -- a career's league tables in their zones' colours (src/league_zones.c; the SWOS 96/97
 * Mod's tools/zones.py, Davide Lorigliola, MIT): each row's background tells where the club ends the
 * season -- light blue (0Dh) the best way up (Champions League, promotion), green (0Eh) the second
 * (Europa League, a play-off), dark blue (08h) the Conference League, orange (0Ch) a play-out, gray (02h)
 * relegation -- a computer club's name in its row's colour (the human club's keeps its own), and a legend
 * of the colours used under the title.
 *
 * The zones are bin/data_json/zones/<world tag>.json (scripts/world_zones.py, from the Mod's DATA\ZONEN5.DAT),
 * per league (comp id) and division; a top flight of a league-phase world takes its European places from
 * its association's UEFA rank and the 2026/27 access list (UefaCoef_AssociationRank,
 * UefaAccess_PlaceColours) as the Mod's. Shown from mid-season: once the leaders have played half the
 * season's matches (Playa, 2026-11-16; the Mod's from the first match day). SDL builds (JSON).
 */
#ifndef LEAGUE_ZONES_H
#define LEAGUE_ZONES_H

#include <stdint.h>

/* The table being drawn (DIYLeagueTournament16BeforeDraw, each group): 1 when it has zones to show --
 * a career's league table of a world with them, its leaders at least half way through the season.
 * `mostPlayed`: the most matches a club of the table has played; `perGroup`: its clubs. */
int32_t  LeagueZones_Begin(uint16_t mostPlayed, uint16_t perGroup);
/* Place 1..'s zone colour, 0 the game's own (after LeagueZones_Begin gave 1). */
uint16_t LeagueZones_RowColour(int32_t place);
/* The legend: up to 6 boxes, each its colour and label (after LeagueZones_Begin gave 1). */
int32_t  LeagueZones_Legend(uint16_t *colour, const char **label, int32_t max);

#endif
