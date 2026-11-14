# WORLD_DATAGEN.md — the world data generator (`scripts/worldgen/`)

**Status (2026-11-14): phase 1 done, Transfermarkt pages supported.** `scripts/worldgen/`
generates a world's squads (World 2026-27 first) from the original sources in the SWOS 96/97
Mod's own pack format: a sample of leagues today (England's Premier League, Championship and
League Two, Serie A, the Scottish Premiership, Malta: 112 clubs, 3,027 players). Transfermarkt
comes from the open dataset or from **pages saved from the owner's own browser** (`--tm`), the
way the Mod's author works. Phase 2 (the whole world) is §14.

Part I is the user guide (§1-§8); Part II the study behind it: what the Mod does, the rules and
how exactly they reproduce it (§9-§14).

The goal (the owner, 2026-11-13): OpenSWOS makes World 2026-27's data **itself, from the original
sources**, instead of taking it from Davide Lorigliola's SWOS 96/97 Mod (docs/WORLD_2026_27.md).
The Mod's stated sources: Transfermarkt (squads, market values, coaches, the 2026-27 participants,
the 2025-26 tables), EA SPORTS FC ratings (player attributes), Wikipedia (kits, CC BY-SA 4.0) via
Wikidata (CC0), kassiesa.net (UEFA coefficients), the competitions' regulations (formats). "The
game files only contain derived data."

# Part I — the user guide

## 1. What it is, in one run

For each club of a **sample** (a CSV: country, division, Transfermarkt club id, its previous
season's level and place) the generator collects the club's squad and its players' market
values, positions, birth dates, nationalities and EA attributes, derives each player's level,
seven skills, price, position in the record, shirt number, nationality and face, and the club's
name, coach and kits, calibrates each division so its order follows the previous table, and
writes `clubs.csv` + `players.csv` per country — the Mod's pack format, what its `mkseason` and
our `scripts/world_import.py` read (§12). `compare.py` measures the result against the Mod's own
packs, field by field.

Every source page is fetched **once**, politely, and cached; a build reads the cache only
(`--offline`), so the same cache gives the same output. Nothing it reads or writes goes in git:
the cache and the output are real people's data (`bin/data_json/` is ignored).

## 2. Setup

**Python 3.8 or later, the standard library only** — no `pip install` for anything here.

- **Windows** (the owner's desktop): Python from python.org (tick "Add python.exe to PATH"), or
  the `py` launcher it installs. Run from a Command Prompt or PowerShell in the checkout:
  ```
  cd C:\Projects\AI\SWOS\OpenSWOS
  py -3 -I scripts\worldgen\tmpages.py urls --country eng --division 0
  ```
  (`-I` = isolated mode: Python ignores the current folder and `PYTHON*` variables, the habit
  for scripts that read downloaded data.) Paths work with either slash.
- **Linux / WSL / macOS**: `python3 -I scripts/worldgen/<script>.py ...` as in the examples.
- **The Mod's packs** (only for `fit.py` and `compare.py`): `bin/data_json/worlds/2627/mod/packs/`,
  made by `scripts/world_import.py` from an installed Mod (docs/WORLD_2026_27.md §6d).
- The browser: any. Its pages are saved by hand (§4); nothing here drives it.

## 3. The sources

| source | gives | read by | how |
|---|---|---|---|
| **Transfermarkt pages** you saved (`--tm pages` / `both`) | squads, numbers, sub-positions, birth dates, nationalities (first and second), height, foot, contract, market values, coaches, tables, a league's clubs | `tmpages.py` | from a folder; **never fetched**: transfermarkt.com answers a script with a CAPTCHA (§10) |
| **transfermarkt-datasets** (github.com/dcaribou/transfermarkt-datasets, CC0, scraped from Transfermarkt) (`--tm dataset`, the default) | the same fields for 32 top flights, frozen 2026-07-06 | `tmdata.py` | 3 gzipped CSVs, once |
| **EA SPORTS FC ratings** (drop-api.ea.com, the data of ea.com's ratings pages) | every attribute, overall, birth date of 16,228 men | `ea.py` | the whole feed, once (~180 requests, ~10 min) |
| **Wikidata** (CC0) | Transfermarkt club id → English Wikipedia article | `wiki.py` | SPARQL, 80 ids a query |
| **Wikipedia** (CC BY-SA 4.0) | squads (where no Transfermarkt page), kits, manager, club name | `wiki.py` | MediaWiki API, 50 articles a request |
| **Wikimedia Commons** | kit pattern images | `wiki.py` | one PNG each |
| **Wayback Machine** (web.archive.org) | archived copies of Transfermarkt pages: test fixtures and measurements only | `fetch.py` | one request at a time |

`--tm both` is the normal choice once pages exist: a club's saved pages win (its squad list
too, as the Mod's: Transfermarkt's current squad), a field a page lacks (a club page has no
height) comes from the dataset's record of the same player id, and a club without pages is read
from the dataset as before. `--tm pages` uses no dataset at all for Transfermarkt's fields.

## 4. Saving the Transfermarkt pages (the owner's workflow)

The pages come from your own browser session: you open each page and save it. ~4 pages a club
(about 3,100 for the whole world, the Mod's count; 42 for the Premier League).

1. **The list.** For the leagues of a run:
   ```
   py -3 -I scripts\worldgen\tmpages.py urls --country eng --division 0 --format html > pl.html
   ```
   then open `pl.html` in the browser: one link a page, with the file name to save it as.
   (`--format text` prints the same to the console, `--format tsv` as name TAB url; without
   `--country` / `--division` every club of the sample.) Per division: its clubs page (2026-27)
   and its final table (2025-26); per club: its **detailed squad**
   (`.../kader/verein/<id>/saison_id/2026/plus/1`) and its **staff** page (the coach).
2. **The folder.** Save everything into one folder — the default the generator reads is
   `bin\data_json\worldgen-cache\tm-pages\` (gitignored; `--tm-pages` for another). Subfolders are
   fine (one per league keeps it tidy).
3. **Save each page.** Open the link; when Transfermarkt shows its verification, solve it as you
   would anyway; when the page is there, **Ctrl+S**. Either "Webpage, HTML only" (one file, enough)
   or "Webpage, complete" (a file + a `_files` folder, which is ignored) — both parse. Type the
   suggested name, or keep the browser's own ("Arsenal FC - Detailed squad 26_27 _
   Transfermarkt.html"): **a page is recognised by its content** (its canonical link / og:url,
   the browser's "saved from" mark), never its file name. Use transfermarkt.com (English); the
   German site works too (§7).
4. **Check.** At any time:
   ```
   py -3 -I scripts\worldgen\tmpages.py check bin\data_json\worldgen-cache\tm-pages --country eng --division 0 --since 2026-09-01
   ```
   prints each page as `present` (with its date and rows: "29 rows, coach Mikel Arteta"),
   `STALE` (dated before `--since`: save it again), `MISSING` (with its URL), and any file that is
   `UNKNOWN` (not a Transfermarkt page, or a verification page saved by mistake). Save the missing
   ones and run it again.
5. **Generate** (§5, `gen.py --tm both`), then **compare** if the Mod's packs are there.

A page's date: the date it shows itself ("Last update: ..."), else a Wayback capture's, else the
file's (its save date). Save a league's pages within a few days: values and squads move.

**No automated browser.** The owner asked (2026-11-14) for a `fetch-with-browser` command: a
visible browser window, the human solving the verification, then the script loading the ~3,100
pages one by one in that session. It is **not built**: the verification is Transfermarkt's
control against automated access, and a script that loads thousands of pages through a session
a human unlocked is that automated access — the check passed by a person on the script's behalf.
That is the line this generator does not cross (§14, decision 1), whoever solves the check. What
it offers instead: the clickable list (step 1), the check (step 4), and any page saved by hand
works. If Transfermarkt gives written consent or a data feed (the Mod's own notes advise asking:
its `notes/world-2025-26-squads.md`, open question 2), automating the fetch becomes a different
question and the parser takes its pages unchanged.

## 5. The commands

All take `--help`. `py -3 -I` on Windows, `python3 -I` elsewhere.

| command | what | options |
|---|---|---|
| `tmpages.py urls` | the pages a run needs, with a file name each (§4.1) | `--sample CSV` (default `sample_2627.csv`), `--country`, `--division`, `--season 2026`, `--prev` (the table's season, default season − 1), `--format text\|tsv\|html` |
| `tmpages.py check DIR` | present / stale / missing / unknown (§4.4) | the same + `--since YYYY-MM-DD` |
| `tmpages.py parse FILE...` | what a saved page holds, as JSON | `--brief` (counts + first rows) |
| `tmpages.py sample DIR` | a division's sample rows from its saved clubs + table pages (a club not in the previous table gets `?` for its previous level: promoted / relegated, fill in by hand) | `--country`, `--division`, `--comp` (Transfermarkt code; default from the country / division), `--season` |
| `test_tmpages.py` | the parser's tests: our invented fixtures in `tests/worldgen/`, and with `--wayback DIR` real archived pages (field coverage) | `--wayback DIR` |
| `tmdata.py` | fetch / summarise the open dataset (once) | |
| `ea.py` | fetch / summarise EA's feed (once, ~10 min) | |
| `fit.py` | fit the scales to the Mod's packs → `curves.json` (committed; rerun only for a new Mod) | |
| `gen.py` | **generate** → `bin/data_json/worldgen/2627/<country>/` | `--tm dataset\|pages\|both`, `--tm-pages DIR`, `--season 2026`, `--wiki-squads` (Wikipedia's squad lists even where pages exist), `--sample`, `--out DIR`, `--offline`, `--no-calibrate`, `--no-move`, `--no-images` |
| `compare.py` | ours vs the Mod's packs, per division (`--clubs`: per club) | `--gen DIR`, `--mod DIR`, `--clubs`, `--json FILE` |
| `fetch.py --stats` | what the cache holds, per host | `fetch.py URL` fetches one page into the cache |

Examples:
```
py -3 -I scripts\worldgen\tmpages.py parse --brief "C:\Users\me\Downloads\Arsenal FC - Detailed squad.html"
py -3 -I scripts\worldgen\gen.py --tm both --offline
py -3 -I scripts\worldgen\gen.py --tm pages --tm-pages D:\tm\2026-10 --out D:\tm\out
py -3 -I scripts\worldgen\compare.py --clubs
py -3 -I scripts\worldgen\test_tmpages.py
```

## 6. Files and folders

| path | what | in git |
|---|---|---|
| `scripts/worldgen/*.py`, `curves.json`, `sample_2627.csv` | the tool, its fitted scales, the sample | yes |
| `tests/worldgen/tm_*.html` | parser fixtures: Transfermarkt's markup with **invented** players and clubs | yes |
| `bin/data_json/worldgen-cache/pages/`, `manifest.tsv` | every fetched page, gzipped, and its manifest line (url, date, status, size, sha1) — `$WORLDGEN_CACHE` moves it | no |
| `bin/data_json/worldgen-cache/tm-pages/` | **your saved Transfermarkt pages** (`--tm-pages`, `$WORLDGEN_TM_PAGES`) | no |
| `bin/data_json/worldgen-cache/tm-pages-wayback/` | Wayback copies used for the measurement (§11) | no |
| `bin/data_json/worldgen/2627/<country>/clubs.csv`, `players.csv` | the output (`--out`) | no |
| `bin/data_json/worlds/2627/mod/packs/` | the Mod's packs (fit / compare) | no |

| script | role |
|---|---|
| `fetch.py` | the one polite cached fetcher (§10) |
| `tmpages.py` | saved Transfermarkt pages: parser, URL list, check |
| `tmdata.py`, `ea.py`, `wiki.py` | the other sources; `wiki.py` also parses `{{Fs player}}` squads, infobox kits and kit PNGs |
| `join.py` | the sources per player (dataset ↔ EA ↔ the Mod) |
| `model.py` | every derivation rule of §11 |
| `fit.py` → `curves.json` | our scales fitted to the Mod's output (numbers of our own making, no squad data) |
| `gen.py`, `modpack.py`, `modport.py` | the generator, the pack reader / writer, `swos_name` + `_xtra_cap` from the Mod's `tools/` (MIT, its notice kept) |
| `compare.py`, `test_tmpages.py` | the measurements, the parser's tests |

## 7. A worked example: the Premier League 2026-27

```
:: 1. once: the other sources (the cache keeps them)
py -3 -I scripts\worldgen\tmdata.py
py -3 -I scripts\worldgen\ea.py
:: 2. the list of the 42 pages, opened in the browser
py -3 -I scripts\worldgen\tmpages.py urls --country eng --division 0 --format html > pl.html
start pl.html
::    ... save each page into bin\data_json\worldgen-cache\tm-pages\eng0\ (§4) ...
:: 3. what is still missing
py -3 -I scripts\worldgen\tmpages.py check bin\data_json\worldgen-cache\tm-pages --country eng --division 0 --since 2026-09-01
:: 4. (a new league: its sample rows from the clubs + table pages, then edit the '?')
py -3 -I scripts\worldgen\tmpages.py sample bin\data_json\worldgen-cache\tm-pages --country eng --division 0
:: 5. generate (the sample's other leagues come from the dataset) and compare with the Mod
py -3 -I scripts\worldgen\gen.py --tm both
py -3 -I scripts\worldgen\compare.py
```
`gen.py` says what it took: `[tm] both: saved squads for 20 of 112 clubs, coaches for 20 (pages
dated 2026-10-14 .. 2026-10-24)`, then a line per country. The PL's clubs.csv `source` column says
`squad tm-pages` for each club read from its pages.

**Languages**: transfermarkt.com (English), .co.uk and .us parse fully. The German sites (.de,
.at, .ch) parse too: positions are translated, "30,00 Mio. €" / "800 Tsd. €" and 15.09.1995 read,
and nationalities are kept with Transfermarkt's own country id (the flag image), named in
English through any English page in the folder (country names of a German-only folder stay
German and miss the nation table: save at least one English squad page). Other languages: the
values and dates read, the positions do not (untranslated) — use .com.

## 8. Troubleshooting, and what it cannot do yet

| symptom | cause / fix |
|---|---|
| `check` lists a file as `UNKNOWN ... captcha` | the verification page was saved instead of the page: open the URL again, solve the check, save once the squad shows |
| `MISSING` though you saved it | saved from another URL form (the club page `startseite`, the non-detailed `kader`): those are recognised as the same club but listed under their own kind — the detailed squad is still missing; or the page is another season's (`saison_id`); or it is in a `_files` folder |
| `present ... (club page: no height / foot / contract; save the detailed squad)` | the club's front page was saved: it works (`gen.py` uses it), but the detailed squad has height, foot, contract and birth dates |
| `STALE` | dated before `--since`: save it again (values and squads change) |
| a player with no value | Transfermarkt shows "-": the generator estimates it as the Mod does (70% of the club's lowest known value) |
| nationalities in German | a German page with no English page in the folder (§7) |
| `not in the cache (offline)` | `--offline` and a source page never fetched: run once without `--offline` |
| `[fetch] 429` / `503` from web.archive.org | the Wayback Machine is busy: fetch.py backs off and retries; run again later |

**Not yet**: the squad-statistics page (minutes, the Mod's order of importance) is recognised
but not used; the whole world's sample (§14: club lists and tables for 91 countries — `tmpages.py
sample` makes a division's rows from its pages); press names, the face hand list and blond hair
(§11); kits stay Wikipedia's; the writer to our team JSON (`mod/teams`) is phase 2; and no
automated fetching of Transfermarkt (§4).

# Part II — the study

## 9. What the Mod does (read from its public notes)

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

## 10. Sources, and how worldgen reads them

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
| **Transfermarkt** (transfermarkt.com) | squads, values, sub-positions, coaches, tables | **blocked**: every page answers a script with an AWS WAF *Human Verification* CAPTCHA (HTTP 405, `x-amzn-waf-action: captcha`), whatever the headers. A CAPTCHA is an access control: worldgen does not get past it (§14, decision 1). Pages the owner saves from his own browser are read by `tmpages.py` (§4) | 1 URL, 3 tries |
| **transfermarkt-datasets** (github.com/dcaribou/transfermarkt-datasets, CC0, data from Transfermarkt) | players (value, sub-position, birth date, citizenship, height, club), clubs (coach), valuations | `tmdata.py`: 3 gzipped CSVs from its public bucket. **Frozen at 2026-07-06** (before the window closed; the project is paused) and **32 top flights only** (no Championship, no Serie B ...) | 3 |
| **EA SPORTS FC ratings** (drop-api.ea.com, the data of ea.com's ratings pages) | 16,228 men's players: every attribute, overall, birth date, position | `ea.py`: the whole feed (it takes no league filter), 100 a page. Still the FC 26-era rosters the Mod used (17,873 items, the same count) | 179 |
| **Wikidata** (CC0) | Transfermarkt club id (P7223) → en.wikipedia article | `wiki.articles`: SPARQL, 80 ids a query | 8 |
| **Wikipedia** (CC BY-SA 4.0) | the club article: current squad (`{{Fs player}}`: number, FIFA nation, GK/DF/MF/FW, name), infobox kits and manager | `wiki.wikitext`: MediaWiki API, 50 articles a request | 10 |
| **Wikimedia Commons** | kit pattern images (`Kit_body_<pattern>.png`) | `wiki.pattern_image` + a small PNG decoder | 211 |

The squads are **Wikipedia's** (current: November 2026), joined by name to the dataset (value,
sub-position, birth date, height) and by birth date + a name token to EA (the Mod's own match
rule); the dataset's club list is the fallback where an article has no squad (3 of 111).

## 11. The rules and how exactly they reproduce the Mod

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
vs 4), which matters for §14.

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

### With Transfermarkt's own pages (`--tm both`, 2026-11-14)

The same generator on the Premier League's 20 clubs with **Transfermarkt pages** in place of the
dataset: each club's front page (`startseite`: number, name, sub-position, birth date, nationalities,
market value) as the Wayback Machine archived it between 2026-10-04 and 10-27 (the Mod's snapshot
is ~10-15), fetched from web.archive.org, never from Transfermarkt (`bin/data_json/worldgen-cache/
tm-pages-wayback/eng0/`, ignored). Nothing else changed: the same curves (fitted on the dataset's
July values), EA, Wikipedia kits. The archive holds club pages and a few non-detailed squads, no
detailed squad (`/plus/1`) of the summer, so height, foot and contract are untested on real pages
(the invented fixtures cover them).

| Premier League, vs the Mod | dataset (July values, Wikipedia squads) | **pages** (October, Transfermarkt's squads) | pages' values, Wikipedia's squads (`--wiki-squads`) |
|---|---|---|---|
| the Mod's players found | 92.6% | **100%** | 96.7% |
| the Mod's 16 in our 16 | 85.3% | **98.1%** | 96.2% |
| row in the 16 | 78.8% | **95.5%** | 94.2% |
| shirt number | 96.4% | **99.6%** | 95.6% |
| face (second nationality) | 72.1% | **93.5%** | 93.7% |
| S outfield (16): exact / within 1 / within 3 | 32% / 69% / 95% | **42% / 86% / 100%** | 41% / 80% / 100% |
| S keeper (16): exact / within 1 | 22% / 59% | **29% / 66%** | 23% / 60% |
| S extras: exact / within 3 | 15% / 51% | **22% / 82%** | 22% / 77% |
| 7 skills exact (all players) / when S equal | 20% / 81% | **27%** / 80% | 26% / 79% |
| price exact | 24% | **33%** | 31% |
| strength rank rho (s11) | 0.98 | **0.99** | 0.98 |

The squads become the Mod's (its 16 found 98%: the misses are the archive's date vs the Mod's),
and the level gains most where the dataset was stale: within 1 from 69% to 86%, the extras within
3 from 51% to 82%. What still separates S: the Mod's whole-world quantile mapping vs our fitted
curve (refit on page values once the world's pages exist: `fit.py`), and its calibration's private
details. `--tm pages` gives the same Premier League (every field the generator needs is on the
pages); `--tm both` matters for detailed squads missing a value, and for the leagues without pages.
The coach stays the Wikipedia infobox's here (90%): the archive has one staff page, of 2025.

## 12. The generator's output

The commands are §5, the files §6.

**Output format: the Mod's pack CSVs** (`clubs.csv` key, name, coach, country, file, division,
tactic, kit1, kit2, lineup, source; `players.csv` club, name, role, position, number, nat, skin, 7
skills with `*` on a key skill, price — `modpack.py`). Why: it is exactly what Davide's generator
writes and his mkseason compiles, so the comparison is field for field, and the step to our team
JSON (`bin/data_json/worlds/2627/mod/teams/`, what `scripts/world_import.py` reads) is a plain
format writer for phase 2 — while the competitions (`pack.json`) stay the importer's. The source
column records the squad's origin and the club's calibration step. The output is real people's
data: it stays in the ignored `bin/data_json/worldgen/`.

## 13. What stays manual

- **Formats, splits, play-offs, cups' draws, European places, B teams** (`pack.json`): from the
  regulations and Wikipedia season pages, by hand, as the Mod does them (its
  `notes/world-2025-26.md`, formats spec). Our importer keeps taking these from the Mod's packs.
- **Which clubs play which division in 2026-27, and the 2025-26 tables** (`sample_2627.csv` holds
  them for the sample). Phase 2 can read both from Wikipedia's season articles
  (`{{Sports table}}` modules) or the dataset's games for its 32 leagues.
- **Press names** of clubs (≤ 16), the face hand list, coach corrections after a mid-season change.
- **UEFA coefficients** (kassiesa.net: one page each for associations and clubs; not fetched here).
- **The calibration's ladder** is our reading of the Mod's description; its exact code is private.

## 14. Phase 2: the whole world (91 countries, ~2,400 clubs, ~24,000 extras)

**Decisions for the owner first:**

1. **Market values.** Transfermarkt cannot be read by a script (the CAPTCHA). Options:
   (a) the owner opens the pages in his own browser and saves them — ~3,100 pages, the Mod's count;
   (b) values only where the frozen dataset has them (32 top flights, July 2026), EA overall
   elsewhere (as good a level where EA rates the player: §11) and the division's level below that;
   (c) ask Transfermarkt for written consent / a data feed. **Decided 2026-11-14: both sources** —
   (a) through `tmpages.py` and `--tm both`, the dataset (b) filling what has no page (§3, §4). A
   script driving the browser through the pages after a human passed the check is not (a): that is
   the automated access the check exists to stop (§4, "No automated browser"); with (c) it would be.
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
