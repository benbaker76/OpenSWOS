"""Every split league's table (scripts/world_split.py) against the Mod's: its spec as mkseason.splits
makes it from the same pack, its bytes as split.entry makes them, the odd pair tables as oddtables'.

  python3 tests/league_playoffs/split_check.py <the Mod's tools/> <the world's mod/packs/>

Prints one line per league (PASS / FAIL / SKIP: a stage not compiled yet) and exits 1 on a FAIL."""
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

for world, file, div, n, spec in mkseason.splits():
    pk = next(p for p in mkseason.packs() if p.files[p.league["clubs"]][0] == file)
    code = os.path.basename(pk.path.rstrip(os.sep)) if hasattr(pk, "path") else str(file)
    dv = pk.divisions()[div]
    sp = dv.get("split") or (pk.league.get("split") if div == 0 else None)
    games = dv.get("games") if "divisions" in pk.league else None
    where = "%s division %d (%d clubs)" % (code, div + 1, n)
    try:
        mine = ours.division_spec(n, sp, games)
    except NotImplementedError:
        print("SKIP: %s: a stage not compiled yet" % where)
        continue
    theirs = {"games": spec["games"], "stages": [
        {k: st[k] for k in ("after", "groups", "rounds", "points", "home")} for st in spec["stages"]]}
    if mine != theirs:
        print("FAIL: %s: spec\n  ours   %s\n  Mod's  %s" % (where, mine, theirs))
        bad += 1
        continue
    table = ours.pair_table(n)
    a = ours.entry(world, file, div, n, mine, table)
    b = split.entry(world, file, div, n, spec, table)
    if a != b:
        print("FAIL: %s: the table's bytes differ" % where)
        bad += 1
        continue
    print("PASS: %s: %d days before, %d after, %d bytes" % (where, a[1], a[2], len(a[0])))
sys.exit(1 if bad else 0)
