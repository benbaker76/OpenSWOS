/*
 * world_baked.c -- the worlds' baked tables, by the world being played (include/world.h).
 *
 * Each world's tables are one generated file (scripts/world_import.py: src/world_data.c for World
 * 2026-27, src/world_data_<tag>.c for the others), whose functions are WorldData<tag>_*; the game
 * asks World_* and this answers with the current world's. 96/97 has none of its own: what a world
 * function answers then is here (a mod's European league phases while 96/97 is the world, its
 * file numbers its own). A new world: its tag in src/world.c, a line in WORLDS below.
 */
#include "game_types.h"
#include "competition_data.h"
#include "world.h"
#include "league_groups.h"
#include "league_playoffs.h"
#include "team_pool.h"
#include "world_cups.h"
#include "world_nationals.h"
#include "world_cupd.h"
#include "league_split.h"
#include "league_phase.h"
#include "uefa_coef.h"
#include "world_europe.h"
#include "world_supercup.h"

/* (X: one world's tag, as its generated functions are named) */
#define WORLD_BAKED_DECLARE(T)                                                                     \
    const uint8_t *WorldData##T##_ContinentCountries(uint8_t region);                               \
    const uint8_t *WorldData##T##_SeasonCountries(void);                                            \
    const char *WorldData##T##_CountryTeamFile(uint8_t country);                                    \
    uint8_t WorldData##T##_SecondFile(uint8_t country);                                             \
    uint8_t WorldData##T##_FileCountry(uint8_t file);                                               \
    void WorldData##T##_InstallBaked(void);                                                         \
    const LeagueGroups *WorldData##T##_LeagueGroups(int32_t *count);                                \
    const LeaguePlayoffs *WorldData##T##_LeaguePlayoffs(int32_t *count);                            \
    const TeamPool *WorldData##T##_TeamPools(int32_t *count);                                       \
    const WorldCup *WorldData##T##_Cups(int32_t *count);                                            \
    CompetitionLeaguePhase *WorldData##T##_LeaguePhaseCups(const uint8_t (**places)[4], int32_t *count); \
    const char *WorldData##T##_DataFingerprint(void);                                               \
    int32_t WorldData##T##_SquadNumbers(void);                                                      \
    const uint8_t *WorldData##T##_ContiSpec(void);                                                  \
    const WorldStates *WorldData##T##_States(uint8_t country);                                      \
    const WorldPromedio *WorldData##T##_Promedio(uint8_t country, int32_t *count);                  \
    const WorldContiCup *WorldData##T##_ContiCups(int32_t *count);                                  \
    const WorldUefaSeeds *WorldData##T##_UefaSeeds(void);                                           \
    const LeagueSplit *WorldData##T##_LeagueSplits(int32_t *count);                                 \
    const WorldCupD *WorldData##T##_CupD(void);                                                     \
    const WorldEuropeKO *WorldData##T##_EuropeKnockouts(void);                                      \
    const WorldSuperCup *WorldData##T##_SuperCups(int32_t *count);                                  \
    const LeagueGroups *WorldData##T##_CupClubs(uint16_t country);                                  \
    const WorldNationals *WorldData##T##_Nationals(void);

#if SWOS_WORLDS
WORLD_BAKED_DECLARE(2627)
WORLD_BAKED_DECLARE(8485)

/* The current world's function, else (96/97) `otherwise`. */
#define WORLD_CALL(call, otherwise)                                                                \
    switch (World_Current()) {                                                                     \
    case WORLD_2627:                                                                               \
        return WorldData2627_##call;                                                               \
    case WORLD_8485:                                                                               \
        return WorldData8485_##call;                                                               \
    default:                                                                                       \
        otherwise;                                                                                 \
    }
#else
#define WORLD_CALL(call, otherwise) otherwise;
#endif

const uint8_t *World_ContinentCountries(uint8_t region)
{
    WORLD_CALL(ContinentCountries(region), return 0)
}

const uint8_t *World_SeasonCountries(void)
{
    WORLD_CALL(SeasonCountries(), return 0)
}

const char *World_CountryTeamFile(uint8_t country)
{
    WORLD_CALL(CountryTeamFile(country), return 0)
}

#if SWOS_WORLDS
uint8_t World_SecondFile(uint8_t country)
{
    WORLD_CALL(SecondFile(country), return 0)
}

uint8_t World_FileCountry(uint8_t file)
{
    WORLD_CALL(FileCountry(file), return file)
}
#endif

void World_InstallBaked(void)
{
#if SWOS_WORLDS
    switch (World_Current()) {
    case WORLD_2627:
        WorldData2627_InstallBaked();
        return;
    case WORLD_8485:
        WorldData8485_InstallBaked();
#ifndef SWOS_SDL_BUILD
        WorldNationals_Install();       /* (its national competitions: world_nationals.c; the SDL's after its JSON) */
#endif
        return;
    default:
        break;
    }
#endif
    {   /* 96/97 with a mod's European cups as league phases (LeaguePhase_SetModEurope) */
        const uint8_t (*places)[4];
        int32_t n;
        CompetitionLeaguePhase *cups = LeaguePhase_ModCups(&places, &n);
        if (cups)
            LeaguePhase_InstallCups(cups);
    }
}

const LeagueGroups *World_LeagueGroups(int32_t *count)
{
    WORLD_CALL(LeagueGroups(count), *count = 0; return 0)
}

const WorldNationals *World_Nationals(void)
{
    WORLD_CALL(Nationals(), return 0)
}

const LeagueGroups *World_CupClubs(uint16_t country)
{
    WORLD_CALL(CupClubs(country), return 0)
}

const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count)
{
    WORLD_CALL(LeaguePlayoffs(count), *count = 0; return 0)
}

const TeamPool *World_TeamPools(int32_t *count)
{
    WORLD_CALL(TeamPools(count), *count = 0; return 0)
}

const WorldCup *World_Cups(int32_t *count)
{
    WORLD_CALL(Cups(count), *count = 0; return 0)
}

CompetitionLeaguePhase *World_LeaguePhaseCups(const uint8_t (**places)[4], int32_t *count)
{
    WORLD_CALL(LeaguePhaseCups(places, count), return LeaguePhase_ModCups(places, count))
}

const char *World_DataFingerprint(void)
{
    WORLD_CALL(DataFingerprint(), return "")
}

int32_t World_SquadNumbers(void)
{
    WORLD_CALL(SquadNumbers(), return 0)
}

const uint8_t *World_ContiSpec(void)
{
    WORLD_CALL(ContiSpec(), return 0)
}

const WorldStates *World_States(uint8_t country)
{
    WORLD_CALL(States(country), return 0)
}

const WorldPromedio *World_Promedio(uint8_t country, int32_t *count)
{
    WORLD_CALL(Promedio(country, count), *count = 0; return 0)
}

const WorldContiCup *World_ContiCups(int32_t *count)
{
    WORLD_CALL(ContiCups(count), *count = 0; return 0)
}

const WorldUefaSeeds *World_UefaSeeds(void)
{
    WORLD_CALL(UefaSeeds(), return (const WorldUefaSeeds *)LeaguePhase_ModSeeds())
}

const LeagueSplit *World_LeagueSplits(int32_t *count)
{
    WORLD_CALL(LeagueSplits(count), *count = 0; return 0)
}

const WorldCupD *World_CupD(void)
{
    WORLD_CALL(CupD(), return 0)
}

const WorldEuropeKO *World_EuropeKnockouts(void)
{
    WORLD_CALL(EuropeKnockouts(), return 0)
}

const WorldSuperCup *World_SuperCups(int32_t *count)
{
    WORLD_CALL(SuperCups(count), *count = 0; return 0)
}
