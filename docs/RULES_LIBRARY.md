# RULES_LIBRARY.md — the shared rules library: interface draft (for the freeze after phase 4)

Draft, 2026-10-28, for Davide Lorigliola (SWOS 96/97 Mod) and OpenSWOS. Nothing here is
built yet: this is what OpenSWOS proposes to move into the library and the interface it
would have, written now that phase 4 (a world's league rules) is done
(docs/WORLD_2026_27.md §6).

## 1. What was agreed (2026-10-27)

- **One C library**, its own repository, pulled into both projects. MIT, both credited.
- **Davide owns the rules and their tests** (`dpt.c`, his harness scenarios as golden cases);
  **OpenSWOS keeps the C portable and the interface clean**.
- **Input**: the final tables and the rules as the Mod's `compile_spec` makes them.
  **Output**: each club's move. **No engine in it**: the game around it (calendars, menus,
  contests, saves) stays each project's own.
- **The interface freezes after phase 4** — now.
- **The DOS build's limits** (the Mod's `tools/cobj.py`: clang `-target i686`, freestanding,
  `-Os -march=i486`, no SSE, cdecl, linked into the exe):
  - no undefined symbols but declared game externs: **no libc**;
  - **no compiler-generated `memcpy` / `memset`** (large struct copies, zero-inits) and no
    64-bit multiply / divide helpers — avoided, or tiny ones shipped with the library;
  - **few absolute references** (globals, pointer tables, switch jump tables: each one a
    fixup, and the LE header's room is small) — so **one caller-owned state struct passed by
    pointer**, not file statics;
  - size is no issue (`gplay.c` is ~55 KB as an object).

## 2. What OpenSWOS has, module by module

| Module | The Mod's source | OpenSWOS | How it is checked | Into the library |
|---|---|---|---|---|
| Season-end play-offs / play-outs (Italian rules) | `dplay.c` `dp_season` | `src/league_playoffs.c` | `scripts/league_playoffs_check.sh`: the Mod's C and ours on the same random tables, 20,000 seasons, every move equal | **yes** |
| The pack's knockouts (promotion, play-out, placement) | `gplay.c` `gp_season` | `src/league_playoffs.c` | the same check (the Mod walks its whole spec, ours its compiled part) | **yes** |
| B teams | `gplay.c` `sec_post` | `src/league_playoffs.c` | built cases + 20,000 random ones vs the Mod's | **yes** |
| The Coppa Italia Serie C winner's play-off place | `dplay.c` `cup_place` | `src/league_playoffs.c` | the play-off check, with a random cup winner each season | **yes** (part of the above) |
| Cups' clubs from the 2nd season (4.0.1) | `gplay.c` `cf_build`, `tools/cupsf.py` | `src/world_cups.c` `build` | 20,000 random seasons (moves, places, career clubs) vs the Mod's `cf_build`, `CF_SEED` 1 | **yes** |
| The Coppa Italia Serie D: table, decisions, later seasons' entry order | `tools/cupd.py`, `gplay.c` `cd_*` | `scripts/world_import.py` `cupd_table`, `src/world_cupd.c` | the table byte for byte vs `compile_table`; `cd_rank`/`cd_season`/`cd_make` over 20,000 random season ends | **yes**: its pure part (see §4.4) |
| Parallel groups: a mover's group by its area, sizes kept | `pgroups.py` (**nasm**, no C) | `src/league_groups.c` (our own C) | the pyramid holds its sizes every season; **never compared with the Mod's** | **yes, if aligned**: the only C version — to be checked against `pgroups` |
| The off-file pool's queue | `gplay.c` RI section (`ri_*`) | `src/team_pool.c` (our own C, same identities encoding) | every one of the 298 clubs in exactly one place each season; **not compared** | **yes, if aligned** |
| Odd leagues' real rounds | `oddtables.py` | `src/fixture_rounds.c` | every club once a match day | **open**: it is data more than rules |
| The career club's play-off / cup ties as contests | `dp_lend`, `gp_lend`, `cd_ko`, CDS1 | `src/world_playoff.c`, `src/world_cupd.c` | golden careers (forced places) | **no**: engine glue. Its bracket arithmetic (`dp_perm_c`) could be |

## 3. Interface (draft)

One header, `swosrules.h`; fixed-width types only (`<stdint.h>` is the one include, it has
no code). Every function takes the caller's state first; nothing is global.

```c
#define SWOSRULES_VERSION 1

/* A club as both games name it: the Mod's team word (team file | ordinal << 8), and its
 * global number (the league table's index). */
typedef struct { uint16_t team, glob, won, gf, ga, pts; } sr_row;

#define SR_DIVS  16                 /* (gplay.c GD) */
#define SR_CLUBS 32                 /* (gplay.c GT) */

/* A league's final tables, place order (row 0 the top). */
typedef struct {
    uint8_t divisions;
    uint8_t n[SR_DIVS];
    sr_row  t[SR_DIVS][SR_CLUBS];
} sr_tables;

/* A league's rules: the pack's, compiled by the Mod's tools. */
typedef struct {
    uint8_t        divisions;
    const uint8_t *promoted, *relegated;    /* per division (the record's counts) */
    uint8_t        italy, serie_b, group_first, groups;    /* dplay.c's */
    const uint8_t *spec;                    /* compile_spec's bytes, 0-ended */
    uint8_t        seconds;                 /* B teams: (ordinal, first team's or FFh) */
    const uint8_t (*second)[2];
    uint8_t        second_top, second_flags;
} sr_league;

/* What the career club played itself (0: simulated), and the cups' results the rules use. */
typedef struct {
    uint16_t po_win, out_win, cpo_win;      /* its Serie B play-off, play-out, Serie C play-off */
    uint16_t gp_a, gp_b, gp_w;              /* its group tie (gplay's pl_w) */
    uint16_t cup_winner;                    /* the Coppa Italia Serie C winner, 0 unknown */
} sr_played;

/* A move: a club's division delta (one level), the rule that made it. */
typedef struct { uint16_t glob; int8_t delta; uint8_t rule; } sr_move;

/* The library's working state (its generators, scratch tables): the caller's memory. */
typedef struct sr_state sr_state;
uint32_t sr_state_size(void);

/* The season end: every move the play-offs, play-outs and B-team rules make (dp_season,
 * gp_season, sec_post), in the Mod's order; the count, or -1 if out holds too few. */
int32_t sr_season_end(sr_state *st, const sr_league *lg, const sr_tables *t,
                      const sr_played *pl, sr_move *out, int32_t cap);
```

### 3.1 Cups from the second season (4.0.1)

```c
typedef struct { uint8_t division, clubs, byes; } sr_quota;     /* from the real list */

/* A cup's clubs this season (cf_build, CF_SEED 1): division[o] this season's division of
 * ordinal o, place[o] last season's (place | level << 5, 0 none) or 0 if not kept, level[d]
 * a division's level; the career club first when its division takes part. The list in
 * order (round 1's clubs, then the byes), its count, or 0: keep the real list. */
int32_t sr_cup_entrants(const sr_quota *q, int32_t nq, int32_t n, uint8_t file,
                        const uint8_t *division, const uint8_t *place, const uint8_t *level,
                        int32_t career_ordinal, uint16_t *out);
```

### 3.2 The Coppa Italia Serie D

The pure part — the table walk (`cd_side`, `cd_raw` with generated rounds), the hosts, the
simulation (`cd_str`, `cd_sim`), a round's draw, the season's entry order and generated
rounds (`cd_rank`, `cd_season`, `cd_make`) — with the career club's tie reported to the
caller instead of built:

```c
typedef struct sr_cupd sr_cupd;         /* its state: results, hosts, the generated rounds */

void    sr_cupd_new_season(sr_state *st, sr_cupd *cd, const uint8_t *table, int32_t first_season,
                           uint8_t season, const uint8_t *division /* this season's */,
                           const uint8_t *last_division, const uint8_t *place_from_bottom,
                           const uint8_t *arrivals /* bits */);
/* The calendar has reached week w: the rounds that have come are decided; 1 when the
 * career club's tie is due (*tie: its index, sides, host, weeks, legs), 0 otherwise. */
int32_t sr_cupd_step(sr_state *st, sr_cupd *cd, const uint8_t *table, uint16_t week,
                     uint16_t career, int32_t first_season, sr_cupd_tie *tie);
void    sr_cupd_result(sr_cupd *cd, int32_t tie, int32_t winner /* 1 a, 2 b */);
```

### 3.3 Groups and pool (if aligned)

```c
/* After the season's one-division moves: each mover's group at its new level by its area,
 * the groups' sizes kept by occupancy (pgroups). */
void    sr_groups_place(sr_state *st, const sr_groups *g, const int8_t *table, int8_t *scratch);
/* A slot relegated out of the lowest division takes the queue's next identity. */
int32_t sr_pool_swap(sr_pool *pool, uint8_t ordinal);
```

## 4. Notes for the port

1. **State.** `league_playoffs.c` keeps today's file statics (`DP_IN`, the moves, the
   generators, `lot[]`); they become `sr_state` fields. The tables are copied in by the
   caller (no struct copies inside: fields are written one by one, or with a tiny `sr_copy`).
2. **Generators.** Each module keeps its own LCG, seeded as the Mod seeds it (`dp_rng`,
   `gp_rng`, the Coppa D's `GP_SEAS * 7919 + r * 131 + first`), so a season replays the same
   in both games given the same tables.
3. **64-bit.** `cd_below` multiplies a 32-bit value by a 32-bit count into 64 bits: on i686
   that is one `mul`, no helper — to be checked in `cobj.py`'s output. No divisions by
   variables in 64 bits anywhere.
4. **The Coppa D's engine part stays out**: when the calendar steps, how the career club's tie
   becomes a contest (slot 4, then slot 2 from the round of 64), its dates.
5. **The team word**: both games pass the Mod's `file | ordinal << 8`; a file is never 255
   (the table's references are `index << 8 | FFh`).

## 5. Found on the way (for Davide)

- **The Coppa Italia Serie D's drawn round (a bug, also in the Mod).** `cd_side` reads a
  drawn round's ties (the semi-finals) from the draw `sd` by the first tie of the round
  **under way**. Once the final is under way, its sides (the semi-finals' winners) are
  read from `sd[2 * (t - first(final)) + sd]` — a negative index: `CD_ST.home[164..167]`,
  past the table's 161 ties, zeros — so both semi-finals read as tie 0 of the preliminary
  round, and its two clubs play the final. Whenever nobody plays those rounds (every career whose club is out of the cup) the
  same preliminary-round club wins it every season. Fixed in OpenSWOS by indexing from the
  drawn round's own first tie (`src/world_cupd.c` `cd_side`).
- **The career club's Coppa D tie is built earlier in OpenSWOS** (as soon as both clubs are
  known, at its round's weeks) — not a rule: our calendar's entries would put the first
  round (week 63) after the first league match (64). The library's `sr_cupd_step` leaves the
  choice to the caller.
- **A club in no final table**: OpenSWOS's Coppa D entry order gives it "unknown" (40h), as
  the Mod does; never seen in a real season.

## 6. Proposed steps

1. A repository (`swos-rules`?), MIT, both names; its `README` the scope above.
2. Move in, one module at a time, the checked ports first (`dplay`, `gplay`'s season end,
   `sec_post`, `cf_build`, the Coppa D's pure part), each with the comparison it already has
   against the Mod's C — those become the library's own tests, beside Davide's `dpt.c` and
   his harness scenarios as golden cases.
3. Both games call it; OpenSWOS's golden careers and the Mod's suite must not move.
4. Then the groups and the pool, once checked against `pgroups` and the RI section.
5. Version 1 frozen: `SWOSRULES_VERSION`, the structs' layouts; a change after that is a
   version 2 both sides agree to.

## 7. Davide's answers (2026-10-29)

1. **Scope**: season end, the cups' entry rule (4.0.1) and the Coppa Italia Serie D's pure
   part, all three: they are rules, ported and checked, and both games stay identical.
2. **Parallel groups**: the library takes OpenSWOS's C (`src/league_groups.c`) once it has been
   compared with the Mod's `pgroups` on random seasons; the Mod then replaces its nasm with the
   library's C, as for the other modules.
3. **The career club's results**: passed as today (`po_win`, ...) in v1, as the Mod works; the
   whole played bracket can come in a later version.
4. **Repository**: under Davide's account -- `aminta/swos-competition-engine` (MIT, already
   holding the schema and `swcc`), so the shared parts live in one place; OpenSWOS gets write
   access.
5. **The interface**: approved; its limits fit the whole world (at most 30 clubs a division:
   `SR_CLUBS` 32). Step 2 of §6 (moving the checked ports in, one at a time, each with its
   comparison against the Mod's C) is agreed.

Also confirmed: the Coppa Italia Serie D's drawn-round bug (§5) is in the Mod's `gplay.c` and
is fixed in its 4.0.3.

## 8. The questions as they were sent

1. Is the scope right: should the cups' entry rule and the Coppa D be in, or season end only?
2. `pgroups` is nasm: would you take OpenSWOS's C version into the library, once we have
   compared the two on random seasons?
3. Do you want the career club's results passed in as today (`po_win`, ...), or the whole
   played bracket?
4. Repository: under your account, ours, or a shared organisation?
