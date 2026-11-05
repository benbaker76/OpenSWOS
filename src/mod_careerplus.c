/*
 * mod_careerplus.c -- Career+ (benefactorr), one mod (docs/mods/CAREER_PLUS.md): what its fork
 * changed in the engine, on OpenSWOS's career hooks and APIs (docs/MODS.md §27-§31) and its
 * data (bin/mods/careerplus). The SDL build's: its data (career_params.json and the rest) is.
 *
 * This file: the mod's registration, its settings, its random numbers and its section of the
 * career save. Each feature is a file of its own, mod_careerplus_<feature>.c, through
 * include/mod_careerplus.h.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "mjson.h"

#include <stdio.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

/* ---- the match changes (CAREER_PLUS.md §5): the game's own tweaks, each behind a setting ---- */

static const char *const kTweaks[] = { "cpu_cards", "more_fouls", "ai_injury_subs" };

static void apply_tweaks(void)
{
    for (int32_t i = 0; i < (int32_t)(sizeof kTweaks / sizeof kTweaks[0]); i++)
        if (ModApi_SetTweak(CP_SLUG, kTweaks[i], ModApi_SettingInt(CP_SLUG, kTweaks[i], 1)) != 0)
            fprintf(stderr, "[careerplus] the game has no tweak %s to switch\n", kTweaks[i]);
}

/* ---- random numbers (CAREER_PLUS.md §4): the mod's own, so the game's draws are the game's.
 * Seeded when a career starts, from the game's own seeds and clock (so a run of the autopilot
 * repeats), and kept in the career save. xorshift32. ---- */

static uint32_t s_rng = 0x2545F491u;

uint32_t CP_Rand(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s_rng = x;
}

int32_t CP_RandBelow(int32_t n)
{
    return n > 0 ? (int32_t)(CP_Rand() % (uint32_t)n) : 0;
}

static void seed_rng(void)
{
    extern uint8_t _seed, _randXorIndex, _seed2, _randXorIndex2;
    extern volatile uint16_t _currentTick;
    s_rng = 0x9E3779B9u ^ ((uint32_t)_seed << 24 | (uint32_t)_randXorIndex << 16 |
                           (uint32_t)_seed2 << 8 | _randXorIndex2) ^ (_currentTick * 2654435761u);
    if (s_rng == 0)
        s_rng = 1;
}

/* ---- the hooks: each feature's, in turn ---- */

static void cp_init(void)
{
    apply_tweaks();
    CP_Winners_Init();
}

static void cp_matchStart(void)
{
    apply_tweaks();         /* (a setting changed in OPTIONS is seen at the next match) */
}

static void cp_careerNew(void)
{
    seed_rng();
    CP_Winners_Clear();
}

static void cp_competitionFinished(const ModCompetitionResult *result)
{
    CP_Winners_Record(result);
}

static void cp_careerSave(ModJsonWriter *out)
{
    ModJson_Printf(out, "\"rng\":%u", (unsigned)s_rng);
    CP_Winners_Save(out);
}

static void cp_careerLoad(const char *json, int32_t len)
{
    double v;
    if (json && mjson_get_number(json, len, "$.rng", &v) && v >= 1 && v <= 4294967295.0)
        s_rng = (uint32_t)v;
    else
        seed_rng();         /* (a career Career+ did not save: from here on, the mod's) */
    CP_Winners_Load(json, len);
}

const ModDef ModDef_careerplus = {
    .slug = CP_SLUG,
    .init = cp_init,
    .matchStart = cp_matchStart,
    .careerNew = cp_careerNew,
    .competitionFinished = cp_competitionFinished,
    .careerSave = cp_careerSave,
    .careerLoad = cp_careerLoad,
};

#endif /* SWOS_SDL_BUILD */
