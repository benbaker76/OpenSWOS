#!/usr/bin/env python3
"""world_mod_bteams.py -- the B-team call-ups' data of an INSTALLED SWOS 96/97 Mod (4.0.6+): its
DATA\\BAGE.<world>, which the Mod's tools/bteam.py writes (MIT, Davide Lorigliola) and its exe reads
when the call-up page opens (docs/WORLD_2026_27.md, "B-team call-ups"). Standard library only.

    decode(data_dir, world=5) -> None (no file) or
        {"teams": [{"b": (file, ordinal), "parent": (file, ordinal), "rules": index,
                    "born": [year or 0, ...]}],          # the B team's players: its record's places
                                                         # 1..16, then its XTRA places 17..; 0 unknown
         "rules": [{"over_age_cap": n (255: none), "u23": age, "gk_any_age": bool,
                    "windows": [[first week, last week], ...]}]}

The format (bteam.py's bage(), version 2):
  'BA', db 2, db teams; per team: dw B team word, dw parent word (both file | ordinal << 8, little
  endian), db rules index, db players, db age codes (birth year - 1900; 0 = unknown);
  'BR', db rules; per rules: db over-age cap (0FFh: none), db U23 age, db flags (bit 0: a goalkeeper
  is callable at any age), db windows, (db first week, db last week) per window -- the career
  calendar's week byte (split.week_of: 8 a month from January of the season's first year, +96 the
  next year).
"""
import os
import struct
import sys


def find(data_dir, world=5):
    for f in os.listdir(data_dir):
        if f.upper() == "BAGE.%03d" % world:
            return os.path.join(data_dir, f)
    return None


def parse(d):
    if d[:2] != b"BA" or len(d) < 4:
        raise ValueError("not a BAGE file")
    if d[2] != 2:
        raise ValueError("BAGE version %d (2 known)" % d[2])
    n, p, teams = d[3], 4, []
    for _ in range(n):
        bw, pw, ri, npl = struct.unpack_from("<HHBB", d, p)
        ages = d[p + 6:p + 6 + npl]
        teams.append({"b": (bw & 0xFF, bw >> 8), "parent": (pw & 0xFF, pw >> 8), "rules": ri,
                      "born": [1900 + a if a else 0 for a in ages]})
        p += 6 + npl
    if d[p:p + 2] != b"BR":
        raise ValueError("BAGE: no rules after the teams")
    nr, p, rules = d[p + 2], p + 3, []
    for _ in range(nr):
        cap, u23, flags, nw = d[p:p + 4]
        w = d[p + 4:p + 4 + 2 * nw]
        rules.append({"over_age_cap": cap, "u23": u23, "gk_any_age": bool(flags & 1),
                      "windows": [[w[2 * i], w[2 * i + 1]] for i in range(nw)]})
        p += 4 + 2 * nw
    for t in teams:
        if t["rules"] >= len(rules):
            raise ValueError("BAGE: team %r has rules %d of %d" % (t["b"], t["rules"], len(rules)))
    return {"teams": teams, "rules": rules}


def decode(data_dir, world=5):
    path = find(data_dir, world)
    if not path:
        return None
    with open(path, "rb") as f:
        return parse(f.read())


if __name__ == "__main__":
    out = decode(sys.argv[1] if len(sys.argv) > 1 else ".")
    if out is None:
        sys.exit("no BAGE file")
    for t in out["teams"]:
        known = sum(1 for y in t["born"] if y)
        print("B %3d/%-3d parent %3d/%-3d rules %d  %2d players, %2d with a year" %
              (t["b"] + t["parent"] + (t["rules"], len(t["born"]), known)))
    for i, r in enumerate(out["rules"]):
        print("rules %d: %r" % (i, r))
