#!/usr/bin/env python3
"""make_euro36.py -- writes src/mod_euro36_data.inc, the euro36 mod's data (docs/MODS.md §22).

  python3 scripts/make_euro36.py <assets>/data_json/teams > src/mod_euro36_data.inc

The three 36-club league phases are petarku's curated tiers (the5ar's fork, src/champions36.c),
kept in scripts/euro36_tiers.json.
Everything else is derived from the game's own team strength -- petarku's metric, the sum of a
club's first 16 players' value bytes -- not from real UEFA coefficients (none are in the repo):

  places   each country's clubs in each tier (CL, EL, UECL), from the second season on
  clubs    a season's units (eighths of points): 236 * (strength / strongest) ** 4, at least 1;
           each association's ten strongest, and every club of the tiers
  seasons  each European association's 5 seasons (thousandths of points), all alike:
           its four strongest clubs' mean units / 8 * 1000 * 3/4
"""
import glob, json, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EUROPE = list(range(0, 42)) + [76, 78]          # the 96/97 database's European team files

def tiers():
    d = json.load(open(os.path.join(REPO, "scripts/euro36_tiers.json")))
    out = [[tuple(r) for r in d[k]] for k in ("champions", "europa", "conference")]
    assert all(len(t) == 36 for t in out)
    return out

def strengths(teams_dir):
    s, names = {}, {}
    for f in glob.glob(os.path.join(teams_dir, "*.json")):
        d = json.load(open(f))
        if d.get("type") != "team" or "country" not in d or d["country"] not in EUROPE:
            continue
        for t in d["teams"]:
            h = t["team"]
            key = (h["country_number"], h["team_ordinal"])
            pl = t["players"][:16]
            s[key] = sum(p["price"] for p in pl)
            names[key] = h["team_name"]
    return s, names

def main():
    teams_dir = sys.argv[1]
    tier = tiers()
    s, names = strengths(teams_dir)
    top = max(s.values())
    units = {k: max(1, round(236 * (v / top) ** 4)) for k, v in s.items()}
    p = print
    p("/* mod_euro36_data.inc -- written by scripts/make_euro36.py: do not edit. */")
    p("")
    for i, (cup, rows) in enumerate(zip(("CHAMPIONS", "EUROPA", "CONFERENCE"), tier)):
        p(f"static const TeamId k{cup.title()}Seeds[36] = {{")
        for c, o, n in rows:
            assert (c, o) in s, (c, o, n)
            p(f"    {{ {c}, {o} }},   /* {names[(c, o)]} */")
        p("};")
    p("")
    p("/* {team file, CL, EL, UECL}: each country's clubs in each tier, from the second season */")
    p("static const uint8_t kPlaces[][4] = {")
    rows = []
    for c in EUROPE:
        n = [sum(1 for t in tier[k] if t[0] == c) for k in range(3)]
        if any(n):
            rows.append(c)
            p(f"    {{ {c}, {n[0]}, {n[1]}, {n[2]} }},")
    p("};")
    p("")
    p("static const WorldUefaCountry kCountries[] = {")
    for c in EUROPE:
        cl = sorted((units[k] for k in units if k[0] == c), reverse=True)[:4]
        if not cl:
            continue
        v = round(sum(cl) / len(cl) / 8 * 1000 * 3 / 4)
        p(f"    {{ {c}, {{ {v}, {v}, {v}, {v}, {v} }} }},")
    p("};")
    p("")
    p("/* each association's ten strongest clubs, and every club of the tiers */")
    p("static const WorldUefaClub kClubs[] = {")
    inTier = {(c, o) for t in tier for c, o, _ in t}
    keep = set(inTier)
    for c in EUROPE:
        keep |= set(sorted((k for k in units if k[0] == c), key=lambda k: -units[k])[:10])
    for (c, o), u in sorted(((k, units[k]) for k in keep), key=lambda kv: -kv[1]):
        p(f"    {{ {c}, {o}, {u} }},   /* {names[(c, o)]} */")
    p("};")

main()
