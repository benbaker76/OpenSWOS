/*
 * tweaks.h -- [tweaks] in SWOS.ini: small, optional changes to how the original behaves where it
 * annoys (docs/MODS.md §4). Every one is OFF by default and checked against the original
 * first (data/SWOS2.s): a tweak changes the original's behaviour; a difference only OpenSWOS
 * has is a bug, fixed for everyone instead.
 *
 * Offline only, for now: in an online match, under the test rigs (replay, rollback, golden
 * careers, --srp-resim) every tweak is off, so they play -- and prove -- the original. (Carrying
 * them in the host's rules, as the lobby's switches travel, is MODS.md §4's next step.) The
 * Amiga reads no SWOS.ini: no tweaks there. SWOS_TWEAKS=0 compiles them out.
 */
#ifndef TWEAKS_H
#define TWEAKS_H

#include "swos_types.h"

#ifndef SWOS_TWEAKS
#define SWOS_TWEAKS 1
#endif

enum {
    TWEAK_CPU_CARDS                = 1u << 0,   /* the computer's players are booked and sent off too */
    TWEAK_NO_TEAMMATE_TACKLES      = 1u << 1,   /* a team-mate leaves your player on the ball alone */
    TWEAK_TACKLE_CONTROLLED_PLAYER = 1u << 2,   /* the slide is made by the player you control */
    TWEAK_MORE_FOULS               = 1u << 3,   /* one borderline (sideways) tackle in 16 is a foul */
    TWEAK_AI_INJURY_SUBS           = 1u << 4,   /* the computer replaces its injured players */
};

/* The tweaks SWOS.ini switched on (main.c, after the settings). */
void    Tweaks_Set(uint32_t mask);
uint32_t Tweaks_Mask(void);

/* A tweak by its [tweaks] key ("cpu_cards"), or 0 when there is none of that name. */
uint32_t Tweaks_Bit(const char *key);

/* A tweak a MOD switched on or off (ModApi_SetTweak; docs/MODS.md §4): kept apart from the
 * player's own, so the mod being off leaves the player's setting as it was. Only an
 * OFFLINE-ONLY tweak may be switched by a mod -- today every tweak is (Tweaks_On is 0 online).
 * `who` names the mod for the OPTIONS row and the log. 0 = done, 1 = no such tweak / not a
 * mod's to switch. */
int32_t Tweaks_SetByMod(uint32_t tweak, int32_t on, const char *who);
/* The mod that switched `tweak` on, or NULL (the player's, or off). */
const char *Tweaks_ByMod(uint32_t tweak);

#if SWOS_TWEAKS
/* Whether `tweak` is on now: set in SWOS.ini or by a mod, and not online or under a rig. */
int32_t Tweaks_On(uint32_t tweak);
#else
#define Tweaks_On(tweak) 0
#endif

#endif /* TWEAKS_H */
