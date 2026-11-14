#!/usr/bin/env python3
"""worldgen/compare.py -- measure worldgen's output against the SWOS 96/97 Mod's packs, club by club.

    python3 -I scripts/worldgen/compare.py [--gen DIR] [--mod DIR] [--clubs] [--json FILE]

Clubs are paired by Transfermarkt id (both clubs.csv carry it in `source`), players within a club
by name (the same folded name tokens, else the same surname and initial). Prints, per division and
in total: the squads' overlap, then for the paired players how often each field is equal (and
within 1 / 3 for the level S: the outfield skill sum, a keeper's price), the club fields (name,
coach, kits) and the rank correlation of the clubs' strength ('skills11'). --clubs adds a line
per club.
"""
import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ea                                               # noqa: E402
import model                                            # noqa: E402
import modpack                                          # noqa: E402


def tm_of(source):
    m = re.search(r'TM (\d+)', source or '')
    return m.group(1) if m else None


def pair_players(gen, mod):
    """[(gen row, mod row)] of the players both have."""
    out, used = [], set()
    def key(r):
        t = ea.fold(r['name']).split()
        return t
    for rule in ('exact', 'surname'):
        for m in mod:
            if id(m) in used:
                continue
            mt = key(m)
            for g in gen:
                if id(g) in used:
                    continue
                gt = key(g)
                ok = set(gt) == set(mt) if rule == 'exact' else \
                    (gt and mt and gt[-1] == mt[-1] and gt[0][:1] == mt[0][:1])
                if ok:
                    out.append((g, m))
                    used.add(id(m))
                    used.add(id(g))
                    break
    return out


def S(r):
    v, _ = modpack.skills(r)
    return int(r['price']) if r['position'] == 'G' else sum(v)


def s11(rows):
    return sum(S(r) for r in rows[1:11]) + 2 * S(rows[0]) if rows else 0


def spearman(a, b):
    n = len(a)
    if n < 3:
        return float('nan')
    ra = {i: r for r, i in enumerate(sorted(range(n), key=lambda i: -a[i]))}
    rb = {i: r for r, i in enumerate(sorted(range(n), key=lambda i: -b[i]))}
    return 1 - 6 * sum((ra[i] - rb[i]) ** 2 for i in range(n)) / (n * (n * n - 1))


class Tally:
    def __init__(self):
        self.c = Counter()

    def add(self, k, ok):
        self.c[k + ' n'] += 1
        self.c[k] += bool(ok)

    def pct(self, k):
        n = self.c[k + ' n']
        return f"{100 * self.c[k] / n:5.1f}% of {n}" if n else '    -'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', default=os.path.join(modpack.REPO, 'bin', 'data_json', 'worldgen', '2627'))
    ap.add_argument('--mod', default=modpack.MOD_PACKS)
    ap.add_argument('--clubs', action='store_true')
    ap.add_argument('--json', help='write the totals here')
    a = ap.parse_args()
    groups = defaultdict(Tally)
    strength = defaultdict(lambda: ([], []))
    for cc in sorted(os.listdir(a.gen)):
        gp = modpack.Pack(os.path.join(a.gen, cc))
        mp = modpack.Pack(os.path.join(a.mod, cc))
        mod_by_tm = {modpack.tm_id(c['source']): c for c in mp.clubs}
        for gc in gp.clubs:
            mc = mod_by_tm.get(tm_of(gc['source']))
            if not mc:
                continue
            div = f"{cc} {gc['division']}"
            for t in (groups[div], groups['ALL']):
                t.add('club name', gc['name'] == mc['name'])
                t.add('coach', ea.fold(gc['coach']) == ea.fold(mc['coach']))
                for k in ('kit1', 'kit2'):
                    g, m = gc[k].split(), mc[k].split()
                    t.add(k + ' whole', g == m)
                    t.add(k + ' shirt type', g[0] == m[0])
                    t.add(k + ' shirt colours', sorted(g[1:3]) == sorted(m[1:3]))
                    t.add(k + ' shorts', g[3] == m[3])
                    t.add(k + ' socks', g[4] == m[4])
            gs, ms = gp.squads[gc['key']], mp.squads[mc['key']]
            pairs = pair_players(gs, ms)
            g16, m16 = gs[:16], ms[:16]
            in16 = sum(1 for g, m in pairs if g in g16 and m in m16)
            strength[div][0].append(s11(g16))
            strength[div][1].append(s11(m16))
            cl = Tally()
            for t in (groups[div], groups['ALL'], cl):
                for m in ms:
                    t.add('mod players found', any(m is mm for _, mm in pairs))
                for m in m16:
                    t.add("mod's 16 in our 16", any(m is mm and g in g16 for g, mm in pairs))
                for g, m in pairs:
                    both16 = g in g16 and m in m16
                    if both16:
                        t.add('position (both in the 16)', g['position'] == m['position'])
                        t.add('row in the 16', g16.index(g) == m16.index(m))
                    t.add('role', g['role'] == m['role'])
                    t.add('number', g['number'] == m['number'])
                    t.add('nationality', g['nat'] == m['nat'])
                    t.add('face', g['skin'] == m['skin'])
                    k = 'keeper' if m['position'] == 'G' else 'outfield'
                    if (g['position'] == 'G') != (m['position'] == 'G'):
                        continue
                    sub = ' (16)' if both16 else ' (extra)'
                    t.add(f'S {k}{sub} exact', S(g) == S(m))
                    t.add(f'S {k}{sub} within 1', abs(S(g) - S(m)) <= 1)
                    t.add(f'S {k}{sub} within 3', abs(S(g) - S(m)) <= 3)
                    if k == 'outfield':
                        gv, gk = modpack.skills(g)
                        mv, mk = modpack.skills(m)
                        t.add('7 skills exact', gv == mv)
                        t.add('key skills', gk == mk)
                        if S(g) == S(m):
                            t.add('7 skills exact when S equal', gv == mv)
                        for i, s in enumerate(modpack.SKILLS):
                            t.add(f'skill {s} within 1', abs(gv[i] - mv[i]) <= 1)
                    t.add('price exact', g['price'] == m['price'])
            if a.clubs:
                print(f"{div:6} {mc['name']:16} ours {gc['name']:16} 16 in 16 {in16:2}/16  "
                      f"S16 exact {cl.pct('S outfield (16) exact')}  s11 {s11(g16)} vs {s11(m16)}  "
                      f"{gc['source'].split('(')[-1].rstrip(')')}")
    order = ['mod players found', "mod's 16 in our 16", 'position (both in the 16)', 'row in the 16', 'role',
             'number', 'nationality', 'face',
             'S outfield (16) exact', 'S outfield (16) within 1', 'S outfield (16) within 3',
             'S keeper (16) exact', 'S keeper (16) within 1', 'S keeper (16) within 3',
             'S outfield (extra) exact', 'S outfield (extra) within 3',
             '7 skills exact', '7 skills exact when S equal', 'key skills'] + \
            [f'skill {s} within 1' for s in modpack.SKILLS] + \
            ['price exact', 'club name', 'coach', 'kit1 whole', 'kit1 shirt type', 'kit1 shirt colours',
             'kit1 shorts', 'kit1 socks', 'kit2 whole']
    divs = sorted(k for k in groups if k != 'ALL') + ['ALL']
    print(f"{'':32}" + ''.join(f'{d:>16}' for d in divs))
    for k in order:
        print(f'{k:32}' + ''.join(f"{groups[d].pct(k).split(' of')[0]:>16}" for d in divs))
    print(f"{'strength rank rho (s11)':32}" + ''.join(
        f"{spearman(*strength[d]):16.2f}" if d != 'ALL' else '' for d in divs))
    if a.json:
        with open(a.json, 'w') as f:
            json.dump({d: {k: groups[d].pct(k) for k in order} for d in divs}, f, indent=1)


if __name__ == '__main__':
    main()
