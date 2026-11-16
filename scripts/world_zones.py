#!/usr/bin/env python3
"""A world's league-table zones (the SWOS 96/97 Mod's tools/zones.py, Davide Lorigliola, MIT) for SWOS United.

The Mod colours a league table's rows by where they end the season -- light blue (0Dh) the best way up
(Champions League places, promotion), green (0Eh) the second (Europa League, a play-off), dark blue (08h)
the Conference League, orange (0Ch) a play-out, gray (02h) relegation -- with a legend of the colours
under the title. It writes them, per language, to DATA\\ZONxx<world>.DAT (zones.py `write`): a count
(u32), then 96-byte records -- world, title (41: the pack's button name + the league's name), the number
of ranges, 8 x (first place, last place, colour), the number of legend boxes, 6 x (colour, label), then
a flag (80: a league-phase top flight, its European places from the association's rank at run time).

This reads the English file of an installed Mod (ZONEN5.DAT: World 2026-27, ZONEN4.DAT: Europe 1984-85)
and writes bin/data_json/zones/<tag>.json for src/league_zones.c, keyed as the game knows a table: its
league (comp id) and division. The records are found by name: each division's Mod name (the names CSV's
exe name) at the end of a record's title, the country's button name before it (one prefix per country).
Rules data only (places and colours), no squads.

  python3 scripts/world_zones.py [--tag 2627] [--mod-data "C:/Games/Sensible World of Soccer 96-97 GOG/DATA"]
"""
import argparse
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DATA = "/mnt/c/Games/Sensible World of Soccer 96-97 GOG/DATA"
KEYS = ('cl', 'el', 'conf', 'cc', 'uefa', 'up', 'po', 'out', 'down', 'lib', 'sud', 'ccc', 'acle', 'acl2',
        'cafcl', 'cafcc', 'ofc', 'next')
LABELS = dict(cl='CL', el='EL', conf='CONF', cc='EC', uefa='UEFA', up='UP', po='P.OFF', out='P.OUT', down='DOWN',
              lib='LIB', sud='SUD', ccc='CONC', acle='ACLE', acl2='ACL2', cafcl='CAFCL', cafcc='CAFCC', ofc='OFC',
              next='NEXT')                  # (zones.py LABELS['en'])
ALIASES = {"YUGOSLAVIA": "SERBIA", "REPUBLIC IRELAND": "EIRE", "NORTHERN IRELAND": "N. IRELAND"}  # ours: the Mod's
WORLD_FILE = {"2627": ("ZONEN5.DAT", "world_competition_names_2627.csv"),
              "8485": ("ZONEN4.DAT", "world_competition_names_8485.csv")}


def norm(s):
    return re.sub(r'[^A-Z0-9]', '', s.upper())


def read_zon(path):
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[:4])[0]
    out = []
    for i in range(n):
        r = d[4 + 96 * i:4 + 96 * (i + 1)]
        title = r[1:42].split(b'\0')[0].decode('latin-1')
        ranges = []
        for k in range(r[42]):
            a, b, c = r[43 + 3 * k:46 + 3 * k]
            if a & 0x80:                    # (a group range: its group headers' only, zones.py HDR_RANGE)
                continue
            ranges.append([a, b, c & 0x1F])  # (the colour's B-team flags 40h / 20h dropped: zones.py BSKIP)
        legend = []
        for k in range(r[67]):
            col, lab = r[68 + 2 * k], r[69 + 2 * k]
            legend.append([col, LABELS[KEYS[lab]] if lab < len(KEYS) else '?'])
        out.append(dict(title=title, ranges=ranges, legend=legend, europe=r[80]))
    return out


def read_names(path):
    """{iso: {division: (exe long name, full long name)}} from the names CSV (a league of one division has
    only its 'league' row)."""
    out = {}
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split(',')
        if len(f) < 5:
            continue
        kind, key, iso, exe, full = f[0], f[1], f[2], f[3], f[4]
        exe_l, full_l = exe.split('|')[0], (full or exe).split('|')[0]
        if kind == 'division' and key.startswith('league/'):
            out.setdefault(iso, {})[int(key.split('/')[1])] = (exe_l, full_l)
        elif kind == 'league':
            out.setdefault(iso, {}).setdefault(-1, (exe_l, full_l))
    for iso, d in out.items():
        if -1 in d:
            if 0 not in d:
                d[0] = d[-1]
            del d[-1]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tag", default="2627")
    ap.add_argument("--mod-data", default=DEFAULT_DATA, help="the installed Mod's DATA folder")
    args = ap.parse_args()
    zon_name, csv_name = WORLD_FILE[args.tag]
    zon = read_zon(os.path.join(args.mod_data, zon_name))
    names = read_names(os.path.join(REPO, "scripts", csv_name))
    leagues = json.load(open(os.path.join(REPO, "bin", "data_json", "worlds", args.tag, "competitions",
                                          "leagues.json")))["leagues"]

    # our league of each country: its divisions' names (a game string symbol, _aSerieA, read as its letters)
    def ours(lg):
        nm = lg.get("name") or []
        return [norm(re.sub(r'^_a', '', nm[2 * k])) if 2 * k < len(nm) else '' for k in range(lg["divisions_count"])]

    # our country names by team file (teams/<NAME>.json), the button names' counterparts
    country_name = {}
    tdir = os.path.join(REPO, "bin", "data_json", "worlds", args.tag, "teams")
    for f in os.listdir(tdir):
        if f.endswith(".json"):
            t = json.load(open(os.path.join(tdir, f), encoding="utf-8"))
            country_name.setdefault(t.get("country"), (t.get("country_name") or "").replace("_", " "))

    def likeness(prefix, name):
        a, b = norm(prefix), norm(ALIASES.get(name, name))
        n = 0
        while n < len(a) and n < len(b) and a[n] == b[n]:
            n += 1
        return n

    # per league of ours: the countries of the names CSV whose divisions' names are its (one league
    # name can be several countries': SUPERLIGA, LIGA NACIONAL ...), and per such country the records
    # each division's Mod name ends; the pair whose prefix (the button name) is nearest our country's
    out, unmatched = [], []
    for lg in leagues:
        got = ours(lg)
        cname = country_name.get(lg["comp"]["team_number"], "")
        best = None
        for iso, divs in names.items():
            want = [norm(divs[k][1]) for k in sorted(divs)]
            want_exe = [norm(divs[k][0]) for k in sorted(divs)]
            if not (len(got) == len(want) and all(g in (w, x) for g, w, x in zip(got, want, want_exe))):
                continue
            cands = {}
            for k, (exe, _full) in divs.items():
                for i, z in enumerate(zon):
                    if z["title"].endswith(" " + exe):
                        cands.setdefault(z["title"][:-len(exe) - 1], {})[k] = i
            for prefix, hits in cands.items():
                key = (likeness(prefix, cname), len(hits), -len(prefix))
                if best is None or key > best[0]:
                    best = (key, iso, prefix, hits, divs)
        if best is None:
            unmatched.append("%s (%s): no zone record" % (lg["id"], cname))
            continue
        key, iso, prefix, hits, divs = best
        if key[0] < 3:
            unmatched.append("%s (%s): the nearest record's country is %r -- not taken" % (lg["id"], cname, prefix))
            continue
        for k, i in sorted(hits.items()):
            z = zon[i]
            out.append({"comp_id": lg["comp"]["comp_id"], "division": k, "title": z["title"],
                        "europe": z["europe"], "ranges": z["ranges"], "legend": z["legend"]})
        for k in divs:
            if k not in hits:
                unmatched.append("%s division %d (%s): no record under %r" % (iso, k, divs[k][0], prefix))

    dest = os.path.join(REPO, "bin", "data_json", "zones", "%s.json" % args.tag)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "w", newline="\n") as f:
        f.write("{\n  \"source\": \"the SWOS 96/97 Mod's %s (tools/zones.py; Davide Lorigliola, MIT): "
                "scripts/world_zones.py\",\n  \"tables\": [\n" % zon_name)
        f.write(",\n".join("    " + json.dumps(t, separators=(", ", ": ")) for t in out))
        f.write("\n  ]\n}\n")
    used = {t["title"] for t in out}
    print("world_zones: %d tables of %d records -> %s" % (len(out), len(zon), os.path.relpath(dest, REPO)))
    for u in unmatched:
        print("  unmatched:", u)
    for z in zon:
        if z["title"] not in used:
            print("  record not used:", z["title"])


if __name__ == "__main__":
    main()
