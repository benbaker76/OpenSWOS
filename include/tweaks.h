/*
 * tweaks.h -- [tweaks] in SWOS.ini: small, optional changes to how the original behaves where it
 * annoys (docs/MODS.md §4). Each is OFF by default but TWEAKS_DEFAULT_ON's, and checked against the original
 * first (data/SWOS2.s): a tweak changes the original's behaviour; a difference only OpenSWOS
 * has is a bug, fixed for everyone instead.
 *
 * Offline only, for now: in an online match, under the test rigs (replay, rollback, golden
 * careers, --srp-resim) every tweak is off, so they play -- and prove -- the original. (Carrying
 * them in the host's rules, as the lobby's switches travel, is MODS.md §4's next step.) The
 * Amiga reads no SWOS.ini: no tweaks there. SWOS_TWEAKS=0 (default: PROJECT_TWEAKS in
 * include/project_identity.h) compiles them out: no [tweaks] in SWOS.ini, no tweak a mod can set.
 */
#ifndef TWEAKS_H
#define TWEAKS_H

#include "swos_types.h"
#include "project_identity.h"   /* PROJECT_TWEAKS */

#ifndef SWOS_TWEAKS
#define SWOS_TWEAKS PROJECT_TWEAKS
#endif
/* The stronger keepers alone (project_identity.h PROJECT_TWEAK_STRONGER_GK): built with the others,
 * or by itself in a build without them. SWOS_TWEAKS_ANY: any tweak at all is built. */
#ifndef SWOS_TWEAK_STRONGER_GK
#define SWOS_TWEAK_STRONGER_GK (SWOS_TWEAKS || PROJECT_TWEAK_STRONGER_GK)
#endif
/* The on-by-default ones (TWEAKS_DEFAULT_ON) alone: project_identity.h PROJECT_TWEAK_FIXES. */
#ifndef SWOS_TWEAK_FIXES
#define SWOS_TWEAK_FIXES (SWOS_TWEAKS || PROJECT_TWEAK_FIXES)
#endif
#define SWOS_TWEAKS_ANY (SWOS_TWEAKS || SWOS_TWEAK_STRONGER_GK || SWOS_TWEAK_FIXES)

enum {
    TWEAK_CPU_CARDS                = 1u << 0,   /* the computer's players are booked and sent off too */
    TWEAK_NO_TEAMMATE_TACKLES      = 1u << 1,   /* a team-mate leaves your player on the ball alone */
    TWEAK_TACKLE_CONTROLLED_PLAYER = 1u << 2,   /* the slide is made by the player you control */
    TWEAK_MORE_FOULS               = 1u << 3,   /* one borderline (sideways) tackle in 16 is a foul */
    TWEAK_AI_INJURY_SUBS           = 1u << 4,   /* the computer replaces its injured players */
    /* ---- SWOS 2020 port (agent D) ---- */
    TWEAK_STRONGER_GOALKEEPER_AI   = 1u << 5,   /* SWOS 2020's: the Amiga keepers reach a step further */
    TWEAK_UNLIMITED_TRANSFERS      = 1u << 6,   /* SWOS 2020's: buying is not held to the season's quota */
    /* ---- ON by default (TWEAKS_DEFAULT_ON; 2026-11-10, the owner) ---- */
    TWEAK_CONTEST_OWN_RATINGS      = 1u << 7,   /* a contested ball rates each side by the man at it
                                                 * (both originals rate one side by a team-mate:
                                                 * docs/BALL_CONTEST.md) */
    TWEAK_LEAGUE_CRASH_FIXES       = 1u << 8,   /* SWOS 2020's career fixes 1-3: a league fitted to
                                                 * its teams (sizes, a longer season, fewer meetings) */
    TWEAK_LEAGUE_RULES_2020        = 1u << 9,   /* its fixes 4-7: Greece from October, Israel twice,
                                                 * Italy 3 down / 3 up, Scotland three times */
    TWEAK_THREE_POINTS             = 1u << 10,  /* every league 3 points for a win */
    TWEAK_FIVE_SUBSTITUTES         = 1u << 11,  /* every league and cup: 5 substitutes from a bench of 5 */
};

/* The tweaks a new SWOS.ini starts with (main.c): SWOS 2020's, which its launcher always passed,
 * and the contest fix. What is not built (Tweaks_Built) is left out. */
#define TWEAKS_DEFAULT_ON (TWEAK_CONTEST_OWN_RATINGS | TWEAK_LEAGUE_CRASH_FIXES | TWEAK_LEAGUE_RULES_2020 | \
                           TWEAK_THREE_POINTS | TWEAK_FIVE_SUBSTITUTES)
/* The tweaks of `mask` this build has. */
uint32_t Tweaks_Built(uint32_t mask);

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

#if SWOS_TWEAKS_ANY
/* Whether `tweak` is on now: set in SWOS.ini or by a mod, and not online or under a rig. */
int32_t Tweaks_On(uint32_t tweak);
#else
#define Tweaks_On(tweak) 0
#endif

#endif /* TWEAKS_H */
