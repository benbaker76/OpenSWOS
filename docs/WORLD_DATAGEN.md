# WORLD_DATAGEN.md — generating World 2026-27's data ourselves

**Status (2026-11-13): phase 1 done** — the Mod's derivation is mapped and measured, and
`scripts/worldgen/` generates a sample of leagues (England's Premier League, Championship and
League Two, Serie A, the Scottish Premiership, Malta: 112 clubs, 3,027 players) from the original
sources in the Mod's own pack format. Phase 2 (the whole world) waits on the source decisions in
§6.

The goal (the owner, 2026-11-13): OpenSWOS makes World 2026-27's data **itself, from the original
sources**, instead of taking it from Davide Lorigliola's SWOS 96/97 Mod (docs/WORLD_2026_27.md).
The Mod's stated sources: Transfermarkt (squads, market values, coaches, the 2026-27 participants,
the 2025-26 tables), EA SPORTS FC ratings (player attributes), Wikipedia (kits, CC BY-SA 4.0) via
Wikidata (CC0), kassiesa.net (UEFA coefficients), the competitions' regulations (formats). "The
game files only contain derived data."

## 1. What the Mod does (read from its public notes)

The squad generator itself is private (the Mod's `internal/`), but its notes describe it:
`notes/world-2025-26.md` ("Data: Transfermarkt 2026-27", "Squads", "Skill model", "EA SPORTS FC
attributes, faces, 25-man squads") and `notes/world-2025-26-squads.md` (tag v4.1.2). Its output is
`bin/data_json/worlds/2627/mod/packs/<country>/` (gitignored; the 4.0.5-era inputs, which equal
the installed 4.1.1's players: checked for England, 2,475 of 2,475 players identical). Summarised:

1. **Snapshot**: Transfermarkt pages at the end of the summer 2026 window (~2026-10-15): the club
   lists, the previous tables, each club's squad 'plus' view (name, number, sub-position, birth
   date, nationalities, height, value) and staff page (the coach). ~3,100 pages, 4-7 s apart.
2. **Level S** (0-49): a quantile mapping of the age-adjusted log market value onto the original
   game's price distribution (adjusted = ln(value) − 0.1 per year under 25, at most 4, + 0.15 per
   year over 28, at most 7); where EA rates the player, S = ½ that + ½ the EA overall mapped onto
   the same scale. Missing values: 70% of the club's lowest known value, else 60% of the division
   median.
3. **Calibration**: per division, each club's strength (the first 11's S, the keeper's twice) moved
   toward the strength of its blended rank (0.65 × its 2025-26 position + 0.35 × its squad rank),
   the ladder at least 70 wide, every player of the club by the same step (|step| ≤ 5).
4. **Skills**: S split over the 7 skills — by the EA attributes (weights (attribute − 25)^1.6, key
   skills the 2 best) or by the sub-position's profile (one point moved between two non-key skills
   by a hash of the Transfermarkt id; ±1 heading by height); clamped at 7, the sum kept. Price = S
   (the original game's own rule for outfield players). Keepers: skills 0, price = S.
5. **Squad**: 16 in record order G RB LB D D RW LW M M A A G D M A + the best remaining, each slot
   the most valuable player of its position, then of the near positions; then up to 14 extras by
   value, each capped at the weakest of the 16 (the Mod's `mkseason._xtra_cap`).
6. **Faces**: dark when any of the player's nationalities is in a dark-nation list; blond hair from
   EA's portraits; a hand list. **Kits**: the infobox colours, the pattern image read in the torso
   box. **Names**: the club's press name (BBC Sport, Gazzetta, Marca ...), else the plain name.

## 2. Sources, and how worldgen reads them

Everything goes through `scripts/worldgen/fetch.py`: one request at a time, ≥ 3 s apart per host
(4-7 s for Transfermarkt), each page cached once (gzipped, with a manifest: url, date, status,
size, sha1) under **`bin/data_json/worldgen-cache/`** — ignored, like all of `bin/data_json/*`,
and never committed. A build reads the cache only (`--offline`), so the same cache gives the same
output. Browser request properties (a desktop User-Agent, Accept, Accept-Language en-GB, gzip, one
cookie jar) for every host except Wikimedia's, which refuse a browser string from a script (403,
their User-Agent policy) and get the tool's own: `OpenSWOS-worldgen/1.0 (project URL)`. No
personal data in any header.

| source | what | how | requests (phase 1) |
|---|---|---|---|
| **Transfermarkt** (transfermarkt.com) | squads, values, sub-positions, coaches, tables | **blocked**: every page answers a script with an AWS WAF *Human Verification* CAPTCHA (HTTP 405, `x-amzn-waf-action: captcha`), whatever the headers. A CAPTCHA is an access control: worldgen does not get past it (§6, decision 1) | 1 URL, 3 tries |
| **transfermarkt-datasets** (github.com/dcaribou/transfermarkt-datasets, CC0, data from Transfermarkt) | players (value, sub-position, birth date, citizenship, height, club), clubs (coach), valuations | `tmdata.py`: 3 gzipped CSVs from its public bucket. **Frozen at 2026-07-06** (before the window closed; the project is paused) and **32 top flights only** (no Championship, no Serie B ...) | 3 |
| **EA SPORTS FC ratings** (drop-api.ea.com, the data of ea.com's ratings pages) | 16,228 men's players: every attribute, overall, birth date, position | `ea.py`: the whole feed (it takes no league filter), 100 a page. Still the FC 26-era rosters the Mod used (17,873 items, the same count) | 179 |
| **Wikidata** (CC0) | Transfermarkt club id (P7223) → en.wikipedia article | `wiki.articles`: SPARQL, 80 ids a query | 8 |
| **Wikipedia** (CC BY-SA 4.0) | the club article: current squad (`{{Fs player}}`: number, FIFA nation, GK/DF/MF/FW, name), infobox kits and manager | `wiki.wikitext`: MediaWiki API, 50 articles a request | 10 |
| **Wikimedia Commons** | kit pattern images (`Kit_body_<pattern>.png`) | `wiki.pattern_image` + a small PNG decoder | 211 |

The squads are **Wikipedia's** (current: November 2026), joined by name to the dataset (value,
sub-position, birth date, height) and by birth date + a name token to EA (the Mod's own match
rule); the dataset's club list is the fallback where an article has no squad (3 of 111).

## 3. The rules and how exactly they reproduce the Mod

Two measurements. **Rule alone**: the rule applied to the Mod's own inputs where they are known
(e.g. its S), over the whole world's matched players. **Generator**: `gen.py` from the sources,
compared with the Mod by `compare.py` over the sample (players paired by name within a club; S =
an outfield player's skill sum, a keeper's price).

### Skills (`model.split`, `model.ea_skills`, `model.profile_skills`)

| rule | rule alone | generator |
|---|---|---|
| **EA split**: pa = short passing + vision, ve = shot power + long shots, he = heading + jumping, ta = standing + sliding tackle + interceptions, co = ball control + dribbling, sp = sprint speed + acceleration, fi = finishing (means); weights (a − 25)^1.6; S in proportion, a skill over 7 clamped and its excess shared in proportion, rounded down with the leftover points to the largest remainders | **92.0% of 7,432 players all 7 skills exact**; per skill 93-95% exact, 96-99% within 1. The exponent and the 25 are sharp (1.0 → 22%, 2.0 → 42%; 20 → 60%, 30 → 54%). The rest is mostly wrong matches of common names (Argentina) | with the generator's own S: all 7 exact 78% of the players whose S is equal |
| **key skills** = the 2 best attribute means | 93% (same mismatches) | 93% |
| **profile split** (EA lacks the player): weights per Transfermarkt sub-position (`curves.json` `profiles`, fitted) | within one moved point: Centre-Back 94%, Centre-Forward 90%, Defensive Mid 82%, Central Mid 80%, full-backs 80%, wingers 78%, Attacking Mid 72%. Exact only ~20-39%: the Mod moves a point by a hash of the Transfermarkt id we cannot know; ours moves one by a CRC of the id (same effect, other players). The height ±1 was not found (adding it lowered the fit) | — |
| price = skill sum (outfield); keepers: skills 0, no key skills; outfield: exactly 2 key skills | 100% of 53,849 / 6,831 | by construction |

### Level S (`model.level`, `curves.json` `level`, `model.calibrate`)

The Mod's quantile mapping needs the whole world's values (its own snapshot); we fit its result
instead: `fit.py` backfits two isotonic curves (age-adjusted ln value → S, EA overall → S, blended
½-½ as the Mod) and one step per club (each division's steps summing to zero), on 11,127 outfield
players and 1,620 keepers the dataset values. The curve: outfield S ≈ 9 at €0.1m, 17 at €1m, 27 at
€10m, 39 at €100m; EA overall 60 → 14, 70 → 23, 80 → 37.

| measurement | outfield | keepers |
|---|---|---|
| rule alone, the Mod's club steps fitted as unknowns | 32% exact, 72% within 1 | 26% / 74% |
| (same, steps unconstrained) | 42% / 76% | 53% / 74% |
| EA overall alone (EA-rated players, steps fitted) | 41% / 73% | 72% / 81% |
| market value alone (same players) | 35% / 62% | 68% / 81% |
| **generator, the 16** (our calibration) | **29% exact, 64% within 1, 89% within 3** | **21% / 53% / 89%** |
| generator, extras (17-30) | 9% exact, 43% within 3 | |
| club strength order (Spearman of the 16's 'skills11', ours vs the Mod's) | PL 0.98, Serie A 0.99, Scotland 0.94, Championship 0.91, Malta 0.91, League Two 0.63 | |

What limits S: (1) the dataset's values are July's, the Mod's October's (the summer's moves and
TM's value updates in between); (2) the Mod's quantiles are over its whole world, ours a fitted
curve; (3) our calibration follows the Mod's description, not its code (ladder, blend and rounding
details unknown); (4) Championship / League Two / Malta players have no dataset value (estimated as
the Mod does) and Malta none in EA either. EA overall alone is as good a level as the value (row 3
vs 4), which matters for §6.

### Squads, players, clubs

| field | rule | generator vs the Mod |
|---|---|---|
| squad found | Wikipedia's first-team list (the dataset's where the article has none) | 90.5% of the Mod's players found (PL 93%, Serie A 95%, Malta 72%) |
| the 16 | slots G RB LB D D RW LW M M A A G D M A (+ the best remaining) by value, near positions next; players a source rates first (Wikipedia lists academy players TM leaves out) | **rows 1-15 by slot: 100% of the Mod's 2,226 clubs**; the Mod's 16 in ours 75.7% (PL 85%); same row 62% |
| position | slot's for rows 1-15, own for the rest; TM sub-position → G RB LB D RW LW M A (Defensive / Central / Attacking Midfield → M, Right Midfield → RW, Second Striker → A) | 90.6% (PL 98.5%); role (G/D/M/A) 94% |
| shirt number | Wikipedia's; missing / taken / > 99 → the lowest free | 97.2% |
| nationality | FIFA code → game number (`curves.json` `nat_fifa`, 130 codes, from the sample), else TM citizenship (`nat_tm`, 174), else the game's own 3-letter table | 99.0% |
| face | dark when the nation is in the dark list (`curves.json` `dark`: 64 nations, from the Mod's faces) | 83.0%: no second nationality (the Mod reads TM's second: Saka, Timber), no blond hair (EA portraits), no hand list |
| extras | rows 17-30 by value (a spare keeper among them), each capped at the weakest of the 16 (`modport.xtra_cap`) | see S extras |
| player name | TM's name when matched, else Wikipedia's; upper ASCII, 22 max, 'J. SURNAME' when longer (`modport.swos_name`) | (pairing is by name: 90.5% found) |
| coach | infobox manager (parenthesis dropped), else the dataset's | 85.7% (Wikipedia is a month later: Udinese, West Ham ...) |
| club name | the article title without legal forms (FC, AC, Calcio ...) and founding years, ≤ 16 | 86.6%; the rest are press names (MAN UTD, QPR, WOLVES, HEARTS, NOTT'M FOREST) — a hand list |
| kit | infobox colours → the nearest of the game's 11 (reference RGBs fitted to the Mod's kits: the menu palette's own sends navy to purple); the body pattern image composed over the body colour, torso box: a second colour ≥ 20% → stripes (2) or hoops (3) by the direction of the colour changes; arms ≠ body → sleeves (1) | home kit **type 90%, whole kit 60%** of the 88 clubs whose Mod kit is from Wikipedia (PL: type 100%, whole 65%). The Mod's League Two kits are generated (a plain hashed colour, white shorts), not Wikipedia's: 0% there, as expected |

## 4. The generator: `scripts/worldgen/`

```
python3 -I scripts/worldgen/ea.py          # fetch EA's feed once (~10 min)
python3 -I scripts/worldgen/tmdata.py      # fetch the dataset once
python3 -I scripts/worldgen/fit.py         # (needs the Mod's packs) -> curves.json
python3 -I scripts/worldgen/gen.py [--offline] [--out DIR]   # -> bin/data_json/worldgen/2627/<country>/
python3 -I scripts/worldgen/compare.py [--clubs]             # vs the Mod's packs
```

| file | what |
|---|---|
| `fetch.py` | the polite cached fetcher (§2) |
| `ea.py`, `tmdata.py`, `wiki.py` | the sources; `wiki.py` also parses `{{Fs player}}` squads, infobox kits and kit PNGs |
| `join.py` | the Mod's players → their source records (for `fit.py`) |
| `model.py` | every rule of §3 |
| `fit.py` → `curves.json` | our scales fitted to the Mod's output: level curves, sub-position profiles, nation tables, the dark list (numbers of our own making, no squad data) |
| `sample_2627.csv` | the sample: country, division, Transfermarkt club id, its 2025-26 level and place, in that table's order |
| `gen.py` | the generator |
| `modport.py` | `swos_name` and `_xtra_cap` from the Mod's `tools/` (MIT, its notice kept) |
| `compare.py` | the club-by-club comparison |

**Output format: the Mod's pack CSVs** (`clubs.csv` key, name, coach, country, file, division,
tactic, kit1, kit2, lineup, source; `players.csv` club, name, role, position, number, nat, skin, 7
skills with `*` on a key skill, price — `modpack.py`). Why: it is exactly what Davide's generator
writes and his mkseason compiles, so the comparison is field for field, and the step to our team
JSON (`bin/data_json/worlds/2627/mod/teams/`, what `scripts/world_import.py` reads) is a plain
format writer for phase 2 — while the competitions (`pack.json`) stay the importer's. The source
column records the squad's origin and the club's calibration step. The output is real people's
data: it stays in the ignored `bin/data_json/worldgen/`.

## 5. What stays manual

- **Formats, splits, play-offs, cups' draws, European places, B teams** (`pack.json`): from the
  regulations and Wikipedia season pages, by hand, as the Mod does them (its
  `notes/world-2025-26.md`, formats spec). Our importer keeps taking these from the Mod's packs.
- **Which clubs play which division in 2026-27, and the 2025-26 tables** (`sample_2627.csv` holds
  them for the sample). Phase 2 can read both from Wikipedia's season articles
  (`{{Sports table}}` modules) or the dataset's games for its 32 leagues.
- **Press names** of clubs (≤ 16), the face hand list, coach corrections after a mid-season change.
- **UEFA coefficients** (kassiesa.net: one page each for associations and clubs; not fetched here).
- **The calibration's ladder** is our reading of the Mod's description; its exact code is private.

## 6. Phase 2: the whole world (91 countries, ~2,400 clubs, ~24,000 extras)

**Decisions for the owner first:**

1. **Market values.** Transfermarkt cannot be read by a script (the CAPTCHA). Options:
   (a) the owner opens the pages in his own browser (a human session passes the check) and saves
   them, or drives that browser — ~3,100 pages, the Mod's count; (b) values only where the frozen
   dataset has them (32 top flights, July 2026), EA overall elsewhere (as good a level where EA
   rates the player: §3) and the division's level below that; (c) ask Transfermarkt for written
   consent / a data feed. (b) needs nothing new; (a) or (c) for a faithful copy of the Mod.
2. **Squads**: Wikipedia (current, CC BY-SA, every club with an article: 108 of 111 here) or
   Transfermarkt (as the Mod). Lower divisions and non-English countries need the other language
   Wikipedias, whose squad templates differ (it: `{{Calciatore in rosa}}`, de, es ...): one parser
   each.
3. **EA's feed** is EA's data, read from its public JSON; the shipped data is derived only (as the
   Mod). When FC 27's ratings replace FC 26's in the feed, everything re-derives from it.
4. **Faces**: a second nationality (Wikidata P27 / P1532 per player: one SPARQL query per ~200
   players) recovers most of the 17% missed; blond hair needs EA's portraits (the Mod's method,
   which its author once stopped as a guess about real people) or a hand list.

**Work (sessions, as the Mod's own estimate counts them):**

| part | sessions |
|---|---|
| club lists and 2025-26 tables for 91 countries (Wikipedia season articles / the dataset; the Mod's `world_tm` division codes as a map) | 2 |
| other-language Wikipedia squad parsers (it, es, de, fr, pt, nl ...) + Wikidata second nationality, birth date, height | 2 |
| values per decision 1; the level without a value (EA, the division's ladder) refitted | 1-2 |
| writer to our team JSON (`mod/teams` format) + the extras; run `world_import.py` on it | 1 |
| strength check: every division's rho vs its 2025-26 table, simulated seasons (`--career-golden` style) | 1-2 |
| kits for every club (~4,800 pattern images, ~4 h at 3 s) and the press-name list | 1 |
| **total** | **8-10**, then ~1 a season (a new snapshot, rerun, a diff report) |

**Risks**: values from two dates (dataset vs a later snapshot) skew a league; Wikipedia squads lag
or lead real transfers by days; a source changes format (the dataset project is paused; EA's feed
is undocumented); a different fetch day gives a different world — so each world package must
record its snapshot date and cache manifest; everything the CAPTCHA forbids stays forbidden.
