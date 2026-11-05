# CAREER_PLUS.md — porting benefactorr's Career+ fork to one mod

Status (2026-11-05): **converted.** Career+ is `mods/careerplus` plus `src/mod_careerplus*.c`
(listed through `src/fork_mods.inc`), on OpenSWOS main's hooks and APIs; the fork's engine
edits left in the merge of 9316b349. Every engine change it needed went to OpenSWOS main as a
patch (§3, §5, and the gaps found while writing it: a mod's bigger team file, 16-letter team
names, the career calls of docs/MODS.md §31, the parked selection in the save). Branch
`career-plus-mod` of github.com/benbaker76/OpenSWOS; the PR goes back to
benefactorr/OpenSWOS:main, so the fork keeps its history and ends up as OpenSWOS + one mod.

**Where it differs from the fork** (on purpose):
- Its random numbers are its own, seeded from the career and saved with it: a career repeats
  under the autopilot (the fork drew on the clock).
- Retirements use the real season (the fork's counter only moved on a job change, so its
  retirement draws and RETIREMENT NEWS's season repeated while you stayed at a club).
- A goalkeeper youth's price stays within 49 (the fork's 127 made a youth of a 48-49 keeper
  "no player" to the transfers, retirements and line-up tidy).
- A player with no age in the data is 25 and ages (the fork showed 25 for ever).
- SEARCH NATIONAL PLAYERS calls the player up (the fork opened his club's squad to pick
  again); AI TRANSFERS says how many transfers the clubs made.
- BUDUCNOST VALJEVO is B. VALJEVO: a team name is 16 letters.
- The joke teams (TEAM.072) are not clubs: the fork's searches and AI transfers left them out
  by a list, OpenSWOS by CN_CUSTOM.
- Not carried over: the fork's diagnostics (logs, CSVs, dry runs, journals) and its test knobs
  (`SWOS_AI_WORLD_MAX_CLUBS`, `SWOS_SEASON_REVIEW_FRAMES`); its crash guards (§2: none of
  them reproduces with the mod in eight five-season careers); ReconcileNationalMatchStatus (a
  national selector's career, which the autopilot cannot reach -- untested).

**Checked:** with the mod off the branch's golden careers equal OpenSWOS main's record; with it
on, the seven golden careers and a Yugoslav one play five seasons each. A season-3 save is
about 8.4 MB (the career's foreign-market pool: about 20,000 claims after the AI transfers,
levelling off near 24,000) and writes in about 30 ms, loads in about 250 ms (desktop SDL).

The mod system is docs/MODS.md. Career+ is named there as one of the first code mods to port
(§9, §11 item 5).

---

## 1. What the mod is

| | |
|---|---|
| slug | `careerplus` |
| kind | a built-in **code** mod (`src/mod_careerplus*.c`, a `ModDef`) plus data (`bin/mods/careerplus/`) |
| `changes` | `career` (only between matches) -- see §5 for the two match changes |
| targets | `sdl` (the identity registry, the 50,000-player pool and the search screens need megabytes; the Amiga keeps the original) |
| default | off |

With the mod **off**, the game must be OpenSWOS exactly: `scripts/career_golden_test.sh`
equal to the record, the replay corpus unchanged, the Amiga building.

**Settings** (mod.json `"settings"`, shown on its page). Each replaces an environment variable
of the fork:

| key | choices | was |
|---|---|---|
| `retirements` | on / off | always on (`SWOS_*RETIREMENT*`) |
| `ai_transfers` | on / off | always on |
| `ai_transfers_max_clubs` | all / 64 / 128 / ... | `SWOS_AI_WORLD_MAX_CLUBS` |
| `season_review` | fast / original | 2 frames, `SWOS_SEASON_REVIEW_FRAMES` |

---

## 2. The features, and what each becomes

The features are Career+'s README's; the call sites are this branch's HEAD.

| # | Feature | Becomes |
|---|---|---|
| F1 | AI transfers between clubs once a season + AI line-up tidy; TRANSFERS menu button | mod code at **careerSeasonEnd**; button at a **MODPOINT** |
| F2 | ages, ageing, retirements at 35+, youth replacements, RETIREMENT NEWS screen | mod code at **careerNew / careerSeasonEnd / playerMoved / playerCreated**; screen = mod JSON menu shown at **careerBeforeContinue**; the 23,264-row seed `src/swos_identity_seed.h` becomes the mod's data file `player_ages.json` |
| F3 | PAST WINNERS: every title of a 35-season career | mod code at **competitionFinished**; its screen a mod JSON menu from a MODPOINT in COMPETITION STATS (see Q2) |
| F4 | WORLD PLAYER SEARCH (from BUY PLAYER) | mod JSON menu + `careerplus.*` handlers; MODPOINT in BUY PLAYER; API to make an offer |
| F5 | SEARCH NATIONAL PLAYERS (national team, add player) | mod JSON menu; MODPOINT in NATIONAL ADD PLAYER; API to call a player up |
| F6 | squad screen: 7 ratings + total + age; GOALS view: nationality; PLAY MATCH: skill total | **playerRowDrawn** display hook |
| F7 | finances: wages 40%, value x2.5, running costs (progressive brackets, 45%), league gate x100, guaranteed sponsorship by division, annual league distribution, sponsorship in the profit/loss | **career params** (new fields); the two formulas that are not a constant (sponsorship, the brackets) as params with a mode switch |
| F8 | transfers: no seasonal quota (256, 128/128), bigger foreign market (x3 a refill, cap 45), more bids for listed players (11->36/256), domestic share 97->75%, domestic buyer eligibility, trialists 5-10 below squad peers | **career params**; trialists at **playerCreated** |
| F9 | job offers: 20 queued with NEXT PAGE, never expire, from week 20, pass rate 42->220, reputation 55-125%, selectors get club offers, waves 6/12/20 | **career params** + capacities (§3.2); NEXT PAGE by the engine once the queue is longer than a page |
| F10 | 35-season careers | `num_seasons` 35 in the mod's `career_params.json` (the base already allows 35) |
| F11 | Yugoslavia 1A/1B (2 x 12) and its national team | data: the mod's `leagues.json` (already overlays) + `TEAM.041`; the national-team record needs §3.3 |
| F12 | 145 national teams, 66 nationalities | data: needs §3.3 |
| F13 | AI injury substitutions | **gameplay**: see §5 |
| F14 | AI straight reds | already the base's `[tweaks] cpu_cards`; the mod asks for it on (§5) |
| F15 | "more fouls" (1 in 16 borderline sideways tackles) | **gameplay**: see §5 |
| F16 | menu music off by default | dropped (a preference: OPTIONS has it) |

**Dropped, as diagnostics:** every `fopen` log and CSV (`swos_ai_debug.log`,
`swos_division_debug.log`, `swos_euro_cup_crash.log`, `swos_euro_qualification_debug.log`,
`swos_age_identity_debug.log`, `world_player_search.log`, `national_player_search.log`,
`swos_finance_debug.log`, the retirement-balance and world-quality CSVs), `EURO_TRACE`,
`SEASON_STEP`, `VALIDTRACE`/`MENUTRACE`/`CUPTRACE`/`DIVTRACE`, `SWOS_AISwapSmokeTest`,
`SWOS_AIStressTest`, the retirement dry run, the `[SWOS FINANCE]`/`[SWOS YUGO OFFER]` prints, the
`menu-json` skip list (the rows come from the mod instead), `#include <sys/stat.h>` in
`swos_io.h`, the Yugoslavia/Canada forced picks in `job_offers.c` `searchNational` (test hacks).

**Dropped, as guards over a crash** (CLAUDE.md: "NULL-deref crashes are a symptom"):
`Career_ViewOrEditCompetitionSlot`'s silent return on `sel >= divisionsCount`
(career_init.c:4901), `Career_AutoAdvanceLeagueFixtures`' skip on an invalid cache / return on
`slotIdx >= 162` (transfer_market.c:4157, :4177), the dead `Career_RebuildNationalCupPointersAfterLoad`.
Each is reproduced with the mod on; a real cause is fixed in the base.

---

## 3. What the engine needs (requested from OpenSWOS main)

In the order that unblocks most. "Fires" lines are this branch's HEAD; the same code is in main.

### 3.1 Base fixes (bugs of the port, for everyone, with the mod or without)

1. **`num_seasons - 1`, not 19**: the last-season checks hard-code `0x13` -- `statistics.c:1726`,
   `job_offers.c` (offers stop), `menus.c` `CareerOverInit` / `CareerOverUpdateJobOffersEntry` /
   `CareerOverFinish`, `career_init.c:4335/:4341`. With `num_seasons` 35 a career says "last
   season" at season 20.
2. **`NationalTeamRecord.skillOrPrice` is big-endian, read host-order** in `job_offers.c`
   `searchNational` and `career_season.c:1123`: on SDL the skill is 0x1300, so a national job
   offer never passes the reputation window. `BE_READ_U16` (game_types.h:1413 already suspects it).
3. **The trialist roster check ignores its flag**: `TrainingMatch_AdjustSelectedTeamRosterForTrialists`
   (transfer_market.c:5161) names its first parameter `teamNum`; in the asm it is the
   availability flag, `playMatchExcludeFlags` (SWOS2.s:106631), handed to `GetNumberOfAvailable*`
   (SWOS2.s:96958-96963). Passing 0 counts suspended players as available.
4. **17-byte team names copied as strings**: `StringCopy` of a `teamName[17]` with no NUL runs
   into the next field (the own-goals byte). Career+'s `SWOS_CopyTeamName17/18`
   (`include/swos_fixed_team_name.h`) at `InitInGameTeamStructure`, `ShowStadiumInit`,
   `PrintNameAndSeason`, `ViewSquadInit`, `CareerGameListCommon`, the transfer and DIY name
   copies. Only data that fills 17 bytes shows it -- a mod's can.
5. **`players_on_transfer_market` is saved as raw host-order words** (`FLD_BYTES`): not portable
   between byte orders (CLAUDE.md, the save rules).
6. `GetRemainingOfferSwapSlots` can underflow (unsigned).

### 3.2 Capacities as settings (career params, saved with the career)

A mod cannot grow an array. Career+ grew these; each becomes a base capacity whose default is the
original's, a career param sets it (SDL), and the save writes the count it used (and still reads
an original-sized one):

| what | original | Career+ |
|---|---|---|
| career foreign-market pool `_careerForeignMarketPlayers` | 432 | 50,000 |
| foreign market listing `_foreignMarketPlayers` | 60 B | 180 B |
| your transfer list | 5 | 10 (+ five TRANSFER MARKET rows) |
| job offer queues `new_job_offers` / `job_offers` | 5 | 20 (+ NEXT PAGE) |

The transfer list's save key `players_on_transfer_market_ids` is sized in BYTES (`FA`, 2 per
entry): the capacity scales its storage and its length together, with a static assert that they
match. (Until then this branch writes 20.)

With these, one rule param: `trialist_count_mode` -- 0 = the caller's availability flag, as
SWOS2.s (`d4 -> d0` before each `GetNumberOfAvailable*`); 1 = Career+'s (injured AND suspended in
every non-training match, so an injury plus a suspension still brings a trialist).
`TrainingMatch_AdjustSelectedTeamRosterForTrialists`, transfer_market.c. Not a base fix: main is
the original. This branch keeps the fork's code until the param lands.

### 3.3 National teams and nationalities from data

`_nationalTeams` (63 records), `_numNationalTeams` and `_nationalityOrdinalsArray` (64) are
compiled. Career+ edited them to 145 / 66 and added Yugoslavia (record 144: 0x50:0x30,
nationality 0x25, team file 0x29). Wanted: the table loaded from JSON through the overlay (SDL),
its counts used where `63` / `64` / `66` are written (`menus.c` `career_nationalities`,
`GetNationalTeamNumber*`), so the data lives in the mod. **Career+'s edits to records 24 and 117
and to 63-141 are unverified** -- check them against the TEAM files before the mod ships them.

### 3.4 Career events (MODS.md §8.5)

Add-only unless it says otherwise. None fires on the Amiga (an SDL mod).

| hook | fires | args | what the mod does there |
|---|---|---|---|
| `careerNew()` | `InitCareer`, after the career is set up, before `InitializeNewSeason` (career_init.c:3493) | -- | seed ages and identities; reset its records |
| `careerSeasonEnd(season)` | `ContinueCareerNextSeason`, after `InitializeNewSeason` in both job-change branches (career_init.c:3642-3664) | season just ended | ageing, retirements, then AI transfers (in that order) |
| `careerBeforeContinue()` | `ContinueCareer`, before `ActivateMenu(_contestMenu)` (career_init.c:336) | -- | may show a mod menu (modal): RETIREMENT NEWS |
| `competitionFinished(compId, kind, division, winnerTeamId)` | career_season.c:908, 918, 939, 944 (secondary/tertiary), 1807 (primary cup), 1859 (each division, table settled, before `DIY_UpdatePromotionRelegationScratchCounts`); and the European holders after `Career_UpdateUefaCupCompetitionSlot` (career_season.c:1943) | the title | PAST WINNERS |
| `playerMoved(fromTeam, fromSlot, toTeam, toSlot)` | every human buy and sell (transfer_market.c:4242, :4443), and slot copies | | identities follow the player |
| `playerCreated(team, slot, kind, PlayerFile *)` | a youth (transfer_market.c:4500), a trialist or reserve (:4875, :4880, :4897; career_status.c:2725) | may rewrite the new player | trialists relative to the squad; age 17 |

**The mod's section of the career save**: `mods.careerplus` in the career's JSON, written and
read through the mod (`save(out)` / `load(json, len)` in its ModDef), optional on load (a career
without it starts the mod's records empty), never required. The mod's own fields are named
objects (not the fork's bare tuples): `ages`, `identities`, `retired`, `winners`, `ai_transfers`
(its cooldown, its random state). Career+'s schema 4 is not carried on: it refused every main
save (see Q4).

### 3.5 Entry points and screens

- **MODPOINTs** (as `MODPOINT_MODES`): BUY PLAYER, TRANSFER MARKET, NATIONAL TEAM ADD PLAYER,
  COMPETITION STATS. A point is a row at the end of the original menu that opens the list of the
  mods' entries -- or, if only one mod has one, its entry directly. Or MENUS.md §1e's
  call-site repoint so a mod's JSON menu may be larger than the compiled one; either works for
  Career+.
- **`playerRowDrawn(menu, team, slot, const PlayerFile *, row)`**: at the end of a row's fill in
  `SquadAfterDrawCommon` (menus.c:1852), `DrawPlayerEntries` (play_match_menu.c:1336) and
  `BuildTransferPlayerMenuEntry` (transfer_market.c:2551). Display only; the mod writes the row's
  cells through `ModApi_MenuText`.
- **APIs**: `ModApi_OfferForPlayer(team, slot)` -> transfer completed or not (wraps
  `ShowPlayerOfferMenuForPlayer` + `_transferCompleted`); `ModApi_CallUpPlayer(team, slot)` inside
  the add-player flow (`Career_AddInternationalPlayerCommon` with a chosen index); a read of a
  club's players by team number that follows the career's live copies (`_selectedTeams`,
  `_tmdFileBuffer`, the market pool) without `LoadTeamFile`'s side effects; and a write of one
  slot that keeps those copies in step (Career+'s `SWOS_AI_P3SyncLiveSlot`).

### 3.6 Career params (new fields; defaults = the original)

Wages %, team-value factor, running-cost % and a `running_cost_mode` (original / brackets),
league gate multiplier, a `sponsorship_mode` (original random / guaranteed by division) with its
weeks per division, a league distribution (weeks per division; 0 = none), whether sponsorship
counts in the profit/loss; transfer quota (original formula / fixed N), foreign-market refill
multiplier and cap, listed-player bid chance, domestic bid share, domestic buyer eligibility;
job offers: expiry on/off, first week, pass rate, reputation window, selectors' club offers,
target waves. Each is one line in the mod's `career_params.json`.

---

## 4. Random numbers

The mod's career code uses its own generator, seeded from the career (its state saved in
`mods.careerplus`), so a career with the mod is reproducible -- the fork seeded from `time()`.
Where Career+ replaced a `Rand2` draw (sponsorship, trialists), the mod on gives another career,
as it should; the mod off draws exactly as the original.

## 5. The match changes (decided: the owner said yes, 2026-11-04)

Career+ changes matches in three places, and all three are **built-in `[tweaks]` the mod switches
on**. Nothing in the mod writes a match. The rules (OpenSWOS docs/MODS.md §4, a61f73b2):

- **AI straight reds** = `[tweaks] cpu_cards` (already on main).
- **More fouls** (`tackle_foul.c:569`, a new `Rand()` draw: every later roll moves) = a new tweak.
- **AI injury subs** (`bench_subs.c:431`: swaps players, sub counts, tactics) = a new tweak.

1. All three are built-in `[tweaks]` keys, off by default and marked OFFLINE-ONLY. The game owns
   them; a mod can only switch one on, through one call, `ModApi_SetTweak`. The main session
   builds that call and the two new tweaks together, in OpenSWOS. Before writing either tweak it
   checks SWOS2.s: if the original already does what is asked, it becomes a base bug fix instead.
2. Online always wins: a match against another player plays without them. The rigs and the golden
   careers run without them too. Career+ stays a `career` mod, so it never has to match between
   two players.
3. The player can see it: the tweak's OPTIONS row shows "ON (CAREER+)", and turning the mod off
   restores the player's own setting.
4. A recording stores the tweaks in force, so a replay or `--srp-resim` plays the same match.

The mod calls `ModApi_SetTweak("careerplus", key, on)` (main 304254f7; 0 = done, 1 = unknown key
or not switchable) for each of the three, each behind one of its own settings (default on), from
`init` and again from `matchStart`, so a settings change applies without a restart. Only the mod
that switched a tweak on can switch it off; a mod's switch is never written to SWOS.ini. The keys:
`cpu_cards`, `more_fouls`, `ai_injury_subs` (the last two on main since 0461374a: one sideways
tackle in 16 a foul, its `Rand()` drawn only with the tweak on; the fork's bench_subs.c code, the
managed club excluded, the swapped-teams branch fixed).

## 6. Open questions

- Q1. Yugoslavia's `rounds`: `leagues.json` says 3, the compiled fallback (competition_data.c:868)
  2. And the 24-club league overlaps Algeria's scratch-validation window [928, 944)
  (career_season.c's capture code shows it).
- Q2. **Decided (2026-11-05): PAST WINNERS stays the mod's own screen.** The base's WINNERS
  (world_winners.c) is the worlds' titles page, from the SWOS 96/97 Mod, and stays as it is; Career+
  keeps its whole-career record from `competitionFinished` (§3.4), saves it in
  `mods.careerplus.winners`, and shows it from a MODPOINT in COMPETITION STATS (§3.5).
- Q3. Do the EURO team-id repair (`EuroReconcileGroupTeamIds`, diy_competition.c:4123;
  `EuroCheckCupFixtureIdentity` :11292) and the national fixture rebind (career_status.c:4087)
  happen on main with stock data, or only with the 145-team table? If on main, base fixes.
- Q4. **Decided (2026-11-05): no converter.** Career+ is new enough that its schema-4 saves need
  not carry over; a Career+ career starts again with the mod.
- Q5. Departing a club keeps the displayed XI and squad slots 16-31 (transfer_market.c:4069):
  does main lose signings in 16-31? Then a base fix, else the mod's.
