#!/usr/bin/env python3
"""world_pool_check.py SAVE.CAR ... -- a World 2026-27 career's Italy, as a save holds it.

Italy is one league over two team files (include/world.h: 262 clubs, slots 0..255 its file,
256.. its second file's) with a pool of clubs below Serie D (include/team_pool.h). For each save
it checks what the season end must keep true, and fails on the first that does not:

  - every division its size (the league's record: Serie A, B, C's groups 20, Serie D's nine
    groups 18), each slot's division its file's plus the save's league offset;
  - every club in exactly one place: each identity -- the 262 slots' own and the pool's -- held
    by one slot (the pool's map) or waiting in the queue, none twice, none lost;

and says where the second file's clubs are (division, and which identity each slot plays as).

  python3 -I scripts/world_pool_check.py tests/career-golden/ITA2627-*/SEASON0*.CAR
"""
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLD = os.path.join(REPO, "bin", "data_json", "worlds", "2627")
ITALY = 20
POOL_BASE = 512                 # (team_pool.h TEAM_POOL_SLOTS: WORLD_SLOTS)


def italy_base():
    """Italy's first global number in the world: 96/97's table, every resize before it."""
    src = open(os.path.join(REPO, "src", "swos_amiga_data_imports.c"), encoding="utf-8").read()
    table = [int(x) for x in re.search(r"_teamsCountryNumbers\[256\] = \{([^}]*)\}", src).group(1).split(",")]
    data = open(os.path.join(REPO, "src", "world_data.c"), encoding="utf-8").read()
    for country, n in re.findall(r"^\s+resize_country\((\d+), (\d+)\);", data, re.M):
        country, n = int(country), int(n)
        delta = n - (table[country + 1] - table[country])
        for i in range(country + 1, 256):
            table[i] += delta
    return table[ITALY]


def main(paths):
    teams = json.load(open(os.path.join(WORLD, "teams", "ITALY.json"), encoding="utf-8"))["teams"]
    second = os.path.join(WORLD, "teams", "ITALY_2.json")
    if os.path.exists(second):
        teams += json.load(open(second, encoding="utf-8"))["teams"]
    pool = json.load(open(os.path.join(WORLD, "teams", "ITALY_POOL.json"), encoding="utf-8"))["teams"]
    leagues = json.load(open(os.path.join(WORLD, "competitions", "leagues.json"), encoding="utf-8"))["leagues"]
    sizes = [d["teams"] for d in next(l for l in leagues if l["id"] == "_italianLeague")["divisions"]]
    base = italy_base()
    names = [t["team"]["team_name"] for t in teams] + [""] * (POOL_BASE - len(teams))
    names += [t["team"]["team_name"] for t in pool]
    ids = set(range(len(teams))) | set(range(POOL_BASE, POOL_BASE + len(pool)))
    bad = 0
    for path in paths:
        d = json.load(open(path, encoding="utf-8"))
        offs = d["career_scalars"]["diy_league_season_state_table"]
        div = []
        for s, t in enumerate(teams):
            g = base + s
            off = offs[g] if g < len(offs) else 0
            div.append(t["team"]["league"] + (off - 256 if off > 127 else off))
        count = [div.count(k) for k in range(len(sizes))]
        msgs = []
        if count != sizes:
            msgs.append("divisions %s, the record's %s" % (count, sizes))
        st = next((p for p in d.get("team_pools", []) if p["country"] == ITALY), None)
        held = []
        if st:
            if st.get("ids", 0) < 2:
                msgs.append("pool ids %s (not 2)" % st.get("ids"))
            m = st["map"] + [0] * (len(teams) - len(st["map"]))
            held = [m[s] - 1 if m[s] else s for s in range(len(teams))] + list(st["queue"])
        else:
            held = list(range(len(teams))) + list(range(POOL_BASE, POOL_BASE + len(pool)))
        seen = {}
        for i in held:
            seen[i] = seen.get(i, 0) + 1
        twice = sorted(i for i, n in seen.items() if n > 1)
        lost = sorted(ids - set(seen))
        alien = sorted(set(seen) - ids)
        if twice or lost or alien:
            msgs.append("identities twice %s, lost %s, unknown %s" % (twice[:8], lost[:8], alien[:8]))
        six = ", ".join("%s d%d%s" % (names[s], div[s], "" if held[s] == s else " as " + names[held[s]])
                        for s in range(256, len(teams)))
        print("%s %s: %s%s" % ("FAIL" if msgs else "ok  ", os.path.relpath(path, REPO),
                              "; ".join(msgs) + (" | " if msgs else ""), "second file: " + six))
        bad += bool(msgs)
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
