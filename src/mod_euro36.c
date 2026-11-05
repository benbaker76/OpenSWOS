/* mod_euro36.c -- CHAMPIONS / EUROPA / CONFERENCE LEAGUE in 96/97, a mod (bin/mods/euro36;
 * docs/MODS.md §22).
 *
 * petarku's 36-team european cups (his fork's src/champions36.c), moved onto OpenSWOS's league
 * phases (src/league_phase.c: pots, country rules, UEFA tie-breaks, the play-offs, the round of
 * 16): the mod is only their data, given to the engine at start-up (ModApi_SetLeaguePhases) and
 * played whenever 96/97 is the world -- a world's own cups win.
 *
 *   the clubs     petarku's curated tiers: the Champions League the top 4 of Italy, England,
 *                 Germany, Spain and France, the top 2 of Portugal and Holland, and the 12
 *                 strongest champions of the rest; the Europa and Conference League the
 *                 strongest left, at most 3 a country
 *   the places    from the second season, each country's clubs in each tier
 *   the seeds     the UEFA coefficients a career's qualifying rounds draw on, derived from the
 *                 clubs' strength (the game's own metric, as petarku ranked them), not real ones
 *
 * The data is src/mod_euro36_data.inc, written by scripts/make_euro36.py.
 * "changes": "career": careers and seasons; a normal match is untouched.
 */
#include "mod_api.h"


#include <stdio.h>
#include <string.h>

#include "game_types.h"
#include "uefa_coef.h"

#include "mod_euro36_data.inc"

static char kNames[3][2][24] = {
    { "UEFA CHAMPIONS LEAGUE", "CHAMPIONS LEAGUE" },
    { "UEFA EUROPA LEAGUE", "EUROPA LEAGUE" },
    { "UEFA CONFERENCE LEAGUE", "CONF. LEAGUE" },
};

/* The three records, as World 2026-27's (src/world_data.c kWorld2627LeaguePhases): type 2, six
 * stages -- 36 in 2 groups of 18, the play-offs 16, then 16, 8, 4, 2 -- the Champions League in
 * the Champions Cup's slot (comp 0), the Europa League the Cup Winners' Cup's (1), the
 * Conference League the UEFA Cup's (2). Only the comp id, the names and the clubs differ. */
static CompetitionLeaguePhase s_cups[3];

static const WorldUefaSeeds kSeeds = {
    kCountries, (int32_t)(sizeof kCountries / sizeof kCountries[0]),
    kClubs, (int32_t)(sizeof kClubs / sizeof kClubs[0]),
    0,                                  /* (no association with a cup and no league) */
};

static void euro36_init(void)
{
    static const CompetitionLeaguePhase kTemplate = {
        { { 0, 2, -1, 64, 32, 35, 0, 41, 1 }, 0, 0, 1, 3, 53, 6,
          { { 36, 2, 18 }, { 16, 0, 16 }, { 16, 0, 16 }, { 8, 0, 8 }, { 4, 0, 4 }, { 2, 0, 2 } }, 1 },
        { 0, -108, -108, -108, -108, 20 },
        { 0, 0 },
        { { 0, 0 } },
    };
    const TeamId *const seeds[3] = { kChampionsSeeds, kEuropaSeeds, kConferenceSeeds };

    for (int32_t c = 0; c < 3; c++) {
        s_cups[c] = kTemplate;
        s_cups[c].header.comp.compId = (uint8_t)c;
        s_cups[c].name[0] = kNames[c][0];
        s_cups[c].name[1] = kNames[c][1];
        memcpy(s_cups[c].teamSeedList, seeds[c], sizeof s_cups[c].teamSeedList);
    }
    if (ModApi_SetLeaguePhases("euro36", s_cups, kPlaces, (int32_t)(sizeof kPlaces / sizeof kPlaces[0]),
                               &kSeeds) != 0)
        fprintf(stderr, "[euro36] the league phases were not taken\n");
}

const ModDef ModDef_euro36 = {
    .slug = "euro36",
    .init = euro36_init,
};

