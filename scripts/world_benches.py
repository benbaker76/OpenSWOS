#!/usr/bin/env python3
"""World 2026-27's benches: how many substitutes each competition names (docs/WORLD_2026_27.md,
"Long and two-row benches"), from the SWOS 96/97 Mod's tools/bench2627.json (4.0.5 / 4.1, Davide
Lorigliola's, MIT) and the world's packs -- written as src/world_benches.c (committed: regulations,
not squads; no package changes).

    python3 -I scripts/world_benches.py [--bench FILE] [--repo SWOS9697MOD] [--mod DIR]

--bench    the Mod's bench2627.json (default: <mod>/bench2627.json, else --repo's tools/ at v4.1.2)
--mod      the world's converted Mod data (default bin/data_json/worlds/2627/mod: its packs/, teams/)

What the Mod's benchx.py bench_table() builds, kept as data the game looks up by what it knows:
  - per country (its team file number) and division: the league's bench, unknown divisions filled
    from the nearest higher one; division 0xFF the country's own (its top league, else its
    confederation's) -- a friendly / DIY match takes the longer of its two clubs', a career's
    league match its club's division, any other contest of the country its own;
  - per country, its other contests by name (its cups, the league's play-offs and the Coppa Italia
    Serie D): every name the pack gives them, any language;
  - the continental cups by name (country 0), with the UEFA cups' qualifying titles.
"""
import argparse
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
OUT = os.path.join(REPO, "src", "world_benches.c")
MAX = 15
QUAL = ("PRELIM. ", "QUAL. ", "QUALIF. ", "QUALI. ")     # (uefaq's qualifying titles, the Mod's)


def norm(x):
    """A value of bench2627.json -> (named, et), or None (unknown)."""
    if isinstance(x, dict):
        return (min(int(x["named"]), MAX), int(x.get("et") or 0)) if x.get("named") else None
    if isinstance(x, (int, float)) and x:
        return (min(int(x), MAX), 0)
    return None


def fallback_divs(v, n):
    vals = list(v) if isinstance(v, list) else [v] * n
    vals += [None] * (n - len(vals))
    for i, x in enumerate(vals):
        if x is None and i:
            vals[i] = vals[i - 1]
    return vals


def all_names(names):
    out = []
    for v in (names or {}).values():
        for n in (v if isinstance(v, list) else [v]):
            if n and n not in out:
                out.append(n)
    return out


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bench")
    ap.add_argument("--repo")
    ap.add_argument("--mod")
    a = ap.parse_args()
    import world_import as wi
    if a.mod:
        wi.MOD = os.path.abspath(a.mod)
    if a.bench:
        data = json.load(open(a.bench))
    elif os.path.exists(os.path.join(wi.MOD, "bench2627.json")):
        data = json.load(open(os.path.join(wi.MOD, "bench2627.json")))
    elif a.repo:
        data = json.loads(subprocess.run(["git", "-C", a.repo, "show", "v4.1.2:tools/bench2627.json"],
                                         check=True, capture_output=True).stdout)
    else:
        sys.exit("no bench2627.json: --bench, --repo or <mod>/bench2627.json")
    confed = data.get("confederations") or {}
    divs, titles = [], []
    for wc in wi.world_countries():
        code = wc["code"].lower()
        if not os.path.exists(os.path.join(wi.MOD, "packs", code, "pack.json")):
            continue
        pk = wi.mod_pack(wc["code"])
        ent = data["countries"].get(code) or {}
        number = wc["number"]
        lg = next((c for c in pk["competitions"] if c.get("type") == "league"), None)
        lvals = fallback_divs(ent.get(lg["key"]), len(lg.get("divisions") or [0])) if lg else []
        top = norm(lvals[0] if lvals and lvals[0] is not None else confed.get(pk.get("continent")))
        if not top:
            continue
        divs.append((number, 0xFF) + top)
        for i, x in enumerate(lvals):
            v = norm(x) or top
            if v != top:
                divs.append((number, i) + v)
        seen = set()

        def title(name, v):
            v = v or top
            if name and name not in seen and v != top:
                seen.add(name)
                titles.append((number, name) + v)
        for c in pk["competitions"]:
            if c is lg:
                for po in c.get("ko_playoffs") or []:       # (a play-off: its clubs' division, the lowest)
                    dv = [e[0] for e in po.get("entrants") or [] if isinstance(e, list)]
                    if dv and max(dv) < len(lvals):
                        for n in all_names(po.get("names")):
                            title(n, norm(ent.get(po.get("key"))) or norm(lvals[max(dv)]))
                if c.get("cup_d"):
                    for n in all_names(c["cup_d"].get("names") or {"*": ["COPPA ITALIA SERIE D"]}):
                        title(n, norm(ent.get("cup_d")))
                continue
            v = ent.get(c["key"])
            if isinstance(v, str) and v.startswith("=league."):
                k = int(v[8:])
                v = lvals[k] if k < len(lvals) else None
            for n in all_names(c.get("names")):
                title(n, norm(v))
    for name, v in (data.get("continental") or {}).items():
        x = norm(v)
        if not x:
            continue
        names = [name] + all_names(v.get("names"))
        if v.get("qual") and name.startswith("UEFA "):
            names += [q + name[5:] for q in QUAL]
        for n in names:
            titles.append((0, n) + x)

    with open(OUT, "w", newline="\n") as f:
        f.write("/* src/world_benches.c -- GENERATED by scripts/world_benches.py: World 2026-27's benches (the\n"
                " * SWOS 96/97 Mod's tools/bench2627.json, Davide Lorigliola's, MIT). Do not edit. */\n"
                '#include "long_bench.h"\n\n/* (the SDL build\'s: the Amiga has no world squads beyond 16, so no long bench) */\n'
                '#if SWOS_WORLDS && defined(SWOS_SDL_BUILD)\n\n')
        f.write("const LongBenchDiv g_worldBenchDivs[] = {\n")
        for e in divs:
            f.write("    { %d, %d, %d, %d },\n" % e)
        f.write("};\nconst int32_t g_worldBenchDivCount = %d;\n\n" % len(divs))
        f.write("const LongBenchTitle g_worldBenchTitles[] = {\n")
        for c, n, named, et in titles:
            f.write("    { %d, %d, %d, %s },\n" % (c, named, et, c_str(n.upper())))
        f.write("};\nconst int32_t g_worldBenchTitleCount = %d;\n\n#endif\n" % len(titles))
    print("wrote %s: %d divisions, %d titles" % (os.path.relpath(OUT, REPO), len(divs), len(titles)))


if __name__ == "__main__":
    main()
