/*
 * tweaks.c -- [tweaks] (include/tweaks.h, docs/MODS.md §4).
 */
#include "tweaks.h"

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#endif

static uint32_t s_mask;            /* the player's ([tweaks] in SWOS.ini) */
static uint32_t s_modMask;         /* switched on by a mod */
static char     s_modBy[32][24];   /* ... by which, per bit */

/* key, bit, and whether a mod may switch it: every tweak is OFFLINE-ONLY today. A tweak that
 * comes to travel in the host's rules online (MODS.md §4) is never a mod's: give it 0. */
static const struct { const char *key; uint32_t bit; uint8_t byMod; } kTweaks[] = {
    { "cpu_cards",                TWEAK_CPU_CARDS,                1 },
    { "no_teammate_tackles",      TWEAK_NO_TEAMMATE_TACKLES,      1 },
    { "tackle_controlled_player", TWEAK_TACKLE_CONTROLLED_PLAYER, 1 },
    { "more_fouls",               TWEAK_MORE_FOULS,               1 },
    { "ai_injury_subs",           TWEAK_AI_INJURY_SUBS,           1 },
};

void Tweaks_Set(uint32_t mask) { s_mask = mask; }
uint32_t Tweaks_Mask(void) { return s_mask; }

/* (no libc on the Amiga) */
static int32_t same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

uint32_t Tweaks_Bit(const char *key)
{
    for (uint32_t i = 0; key && i < sizeof kTweaks / sizeof kTweaks[0]; i++)
        if (same(key, kTweaks[i].key))
            return kTweaks[i].bit;
    return 0;
}

static int32_t bit_index(uint32_t tweak)
{
    for (int32_t b = 0; b < 32; b++)
        if (tweak == (1u << b))
            return b;
    return -1;
}

int32_t Tweaks_SetByMod(uint32_t tweak, int32_t on, const char *who)
{
    const int32_t b = bit_index(tweak);
    uint32_t i;
    for (i = 0; i < sizeof kTweaks / sizeof kTweaks[0]; i++)
        if (kTweaks[i].bit == tweak)
            break;
    if (b < 0 || i == sizeof kTweaks / sizeof kTweaks[0] || !kTweaks[i].byMod)
        return 1;
    if (on) {
        s_modMask |= tweak;
        const char *src = who ? who : "a mod";
        uint32_t n = 0;
        while (src[n] && n < sizeof s_modBy[b] - 1) { s_modBy[b][n] = src[n]; n++; }
        s_modBy[b][n] = '\0';
    } else if (!who || !s_modBy[b][0] || same(s_modBy[b], who)) {
        s_modMask &= ~tweak;       /* (only the mod that switched it, or anyone when unnamed) */
        s_modBy[b][0] = '\0';
    }
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[tweaks] %s %s by mod %s\n", kTweaks[i].key, on ? "ON" : "off",
            who ? who : "?");
#endif
    return 0;
}

const char *Tweaks_ByMod(uint32_t tweak)
{
    const int32_t b = bit_index(tweak);
    return (b >= 0 && (s_modMask & tweak) && s_modBy[b][0]) ? s_modBy[b] : (const char *)0;
}

#if SWOS_TWEAKS
#ifdef SWOS_SDL_BUILD
#include "replay_test.h"    /* ReplayTest_Running */
#include "srp_resim.h"      /* SrpResim_Armed */
#include "rollback_rig.h"   /* Rollback_Enabled */
extern int32_t NetplayMatch_Enabled(void);
extern int32_t CareerAutopilot_Active(void);
#endif

int32_t Tweaks_On(uint32_t tweak)
{
    if (!((s_mask | s_modMask) & tweak))
        return 0;
#ifdef SWOS_SDL_BUILD
    /* Online the other player would play the original; the rigs prove the original. */
    if (NetplayMatch_Enabled() || ReplayTest_Running() || SrpResim_Armed() || Rollback_Enabled() ||
        CareerAutopilot_Active())
        return 0;
#endif
    return 1;
}
#endif
