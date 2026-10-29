"""Every split league's table (scripts/world_split.py) against the Mod's: its spec as mkseason.splits
makes it from the same pack, its bytes as split.entry makes them, the odd pair tables as oddtables'.

  python3 tests/league_playoffs/split_check.py <the Mod's tools/> <the world's mod/packs/>

Prints one line per league (PASS / FAIL / SKIP: more than 20 clubs) and exits 1 on a FAIL. The stage
titles' parts (after-stage routines, stage tables) are not compiled yet: compared without them."""
import os
import sys

TOOLS, PACKS = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import world_split as ours          # noqa: E402

sys.path.insert(0, TOOLS)
import oddtables                    # noqa: E402
import split                        # noqa: E402

bad = 0
for n in range(5, 24, 2):           # the odd sizes: our pair tables are oddtables' rounds
    if [tuple(p) for rd in oddtables.rounds(n) for p in rd] != ours.pair_table(n):
        print("FAIL: the pair table of %d clubs differs from oddtables'" % n)
        bad += 1

codes = sorted(c for c in os.listdir(PACKS) if os.path.exists(os.path.join(PACKS, c, "pack.json")))
os.environ["SEASON_PACKS"] = ",".join(os.path.join(PACKS, c) for c in codes)
import mkseason                     # noqa: E402

import world_import                 # noqa: E402

TITLE = ("then", "table", "annual")    # (the stage titles' parts: not compiled yet)
wcs = {c["code"].lower(): c for c in world_import.world_countries()}
picks = {}
for world, file, div, n, spec in mkseason.splits():
    pk = next(p for p in mkseason.packs() if p.files[p.league["clubs"]][0] == file)
    code = os.path.basename(pk.path.rstrip(os.sep))
    where = "%s division %d (%d clubs)" % (code, div + 1, n)
    if n > 20:
        print("SKIP: %s: more than 20 clubs" % where)
        continue
    wc = wcs[code]
    if code not in picks:
        picks[code] = world_import.country_teams(wc)[1]
    dv = pk.divisions()[div]
    sp = dv.get("split") or (pk.league.get("split") if div == 0 else None)
    games = dv.get("games") if "divisions" in pk.league else None
    mine = ours.division_spec(n, sp, games, world_import.division_rows(wc, picks[code], div))
    theirs = dict(spec, stages=[{k: v for k, v in st.items() if k not in TITLE} for st in spec["stages"]])
    table = ours.pair_table(n)
    a = ours.entry(world, file, div, n, mine, table)
    b = split.entry(world, file, div, n, dict(theirs, stages=[dict(st, then=None, table=None, annual=False)
                                                               for st in theirs["stages"]]), table)
    if a != b:
        print("FAIL: %s: the table's bytes differ (%d / %d bytes; days %d+%d / %d+%d)"
              % (where, len(a[0]), len(b[0]), a[1], a[2], b[1], b[2]))
        bad += 1
        continue
    print("PASS: %s: %d days before, %d after, %d bytes" % (where, a[1], a[2], len(a[0])))
sys.exit(1 if bad else 0)
