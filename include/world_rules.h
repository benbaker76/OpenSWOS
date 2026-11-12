/*
 * world_rules.h -- the SWOS 96/97 Mod 4.1's match and bench rules, played in a world other than
 * 96/97 (src/world_rules.c; docs/WORLD_2026_27.md, "The Mod 4.1's match rules").
 *
 * Each is a setting of the world's mod (its mod.json "settings", SWOS.ini [mod.world<tag>], the
 * mod's page in OPTIONS -> MODS), with the Mod's default. In 96/97, and on a SWOS_WORLDS=0 build,
 * every one is off: the original game plays as it always did.
 *
 *   stoppage_time        on   added minutes at the end of each half (src/stoppage.c)
 *   cpu_red_cards        on   the computer's players are sent off, and suspended like yours
 *   cpu_injuries         on   the computer's injured players miss the following matches
 *   goalkeeper_injuries  on   after each match a keeper is injured 1 time in 25
 *   bench_info           on   the bench menus show a substitute's value and three best skills
 *   opponent_squad       on   the bench menu's OPPONENT row: the other team's squad
 *
 * CPU RED CARDS, CPU INJURIES and GOALKEEPER INJURIES are kept with a career, as the Mod keeps
 * them (its OPTIONS rows, the career items 'CC' and 'GI'): a career's own values are put back in
 * the settings when it loads (an older career: ON), and the settings are what a new career and
 * every other match play by.
 */
#ifndef WORLD_RULES_H
#define WORLD_RULES_H

#include "swos_types.h"

enum {
    WRULE_STOPPAGE      = 0,
    WRULE_CPU_CARDS,
    WRULE_CPU_INJURIES,
    WRULE_GK_INJURIES,
    WRULE_BENCH_INFO,
    WRULE_OPPONENT_SQUAD,
    WRULE_COUNT
};

/* A world other than 96/97 is played. */
int32_t     WorldRules_World(void);
/* Rule `r` is on: a world is played and its setting is on. (The setting is read when asked: a
 * change on the mod's page is seen at once. Never from inside a match frame on the Amiga.) */
int32_t     WorldRules_On(int32_t r);
/* The setting's key ("stoppage_time"). */
const char *WorldRules_Key(int32_t r);

/* ---- the career's copies (save_format_json.c, "world_rules") ---- */
/* What a career saves: bit WRULE_x set = on, for the three career rules. */
uint32_t    WorldRules_CareerBits(void);
/* A career loaded: its rules into the settings (`have` 0: an older save, all ON). */
void        WorldRules_CareerLoaded(int32_t have, uint32_t bits);

/* ---- CPU RED CARDS / CPU INJURIES (the Mod's cpucards.py) ----
 * The original keeps a computer club's cards and injuries out of its records: it never sends one
 * of its players off (tackle_foul.c: the cpu_cards tweak's code), and every countdown and record
 * write skips it. With the rule on, those guards open for the kind (cards: bits 1Fh of
 * cardsInjuries, injuries: E0h). */
/* A guard on writing kind `r` (WRULE_CPU_CARDS / WRULE_CPU_INJURIES) into a computer club's
 * records opens. */
int32_t     WorldRules_CpuOpen(int32_t r);
/* Either kind is on. */
int32_t     WorldRules_CpuAny(void);
/* A computer club's countdown of kind `r` runs: on a career's or season's calendar day
 * (WorldRules_CpuDay), and after each match of a DIY or preset competition, which has no day. */
int32_t     WorldRules_CpuCountdown(int32_t r);
/* A calendar day (Career_AdvanceToNextTeamSelectionKeyCacheEntry): every cached computer club's
 * countdowns, once. */
void        WorldRules_CpuDay(void);
/* A match (played or simulated) begins: a computer club's starters that are injured or suspended
 * change places with available substitutes, the same role first (keeper or not), for this match
 * only -- End puts its order back. `mode` is the match's card mode (plgD3Param). `which` 0: a
 * played match, 1: a simulated result. */
struct TeamFileEx;
void        WorldRules_CpuLineupsBegin(int32_t which, struct TeamFileEx *a, struct TeamFileEx *b, int16_t mode);
void        WorldRules_CpuLineupsEnd(int32_t which);

/* ---- goalkeeper injuries (src/world_rules.c) ----
 * After a match played for real, each team's keeper on the pitch at the final whistle; after a
 * simulated result, each team's first-choice keeper: 1 in 25 to be out of the next one or two
 * matches. A human's keeper always, the computer's in a competition (not a friendly) while CPU
 * INJURIES is on too. Its own generator, so the game's Rand is not shifted: seeded from the
 * game's two generators' states at its first roll (read, not drawn), kept with the career. */
struct TeamGame;
void        WorldRules_GkAfterMatch(struct TeamGame *tg, struct TeamFileEx *team);
void        WorldRules_GkAfterSim(struct TeamFileEx *team, const uint8_t *remap);
extern uint32_t g_gkInjurySeed;                 /* (the career's, "world_rules" "gk_seed") */
void        WorldRules_NewCareer(void);         /* (the generator anew) */

#endif /* WORLD_RULES_H */
