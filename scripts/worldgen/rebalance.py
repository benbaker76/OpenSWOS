#!/usr/bin/env python3
"""worldgen/rebalance.py -- World 2026-27's squads rebalanced to SWOS 96/97's (docs/WORLD_DATAGEN.md §16).

    python3 -I scripts/worldgen/rebalance.py [--apply] [--world DIR] [--only-caps FILE...]

The world's skills (0-7 each: a key skill's 8-15 counted as its 0-7) are brought to 96/97's balance,
the owner's rules (2026-11-18):
1. the top of each position (centre-back, full-back, midfielder, winger, forward) as 96/97's top --
   the world has more players, so the number of a position's 40+ players is 96/97's times the ratio of
   the two worlds' players: each player's skill total moved toward 96/97's at the same rank (a share,
   0 at the 85th percentile rising to all of it from the 95th), the order kept, a lift at most +3
   (so nobody passes a player who was better: Openda and Ronaldo stay near theirs);
2. no 16 over 96/97's best: the total 555 (Italy), and each skill's -- speed 82 (Italy, Barcelona),
   passing 86, shooting 91, heading 81, tackling 91, control 83, finishing 75 -- and no career squad
   (its extras too) over the same as an average a player; a team over one losing the points in
   proportion to what each player holds (its best stay its best).
A player's skills move in proportion (none over 7), his key skills kept; his price moves with his total.
The clubs' files are read from teams_before_rebalance/ (made on the first --apply: the import's own
output), so a rerun starts from the same data. Without --apply it reports only. --only-caps applies rule
2 alone to the files named (the national teams, after nations.py).
"""
import argparse
import collections
import glob
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'scripts'))

F = ('passing', 'velocity', 'heading', 'tackling', 'control', 'speed', 'finishing')
P = 'G RB LB D RW LW M A'.split()
GROUP = {'RB': 'FB', 'LB': 'FB', 'D': 'D', 'M': 'M', 'RW': 'W', 'LW': 'W', 'A': 'A'}
NAT = {'EUROPE', 'AFRICA', 'ASIA', 'SOUTH_AMERICA', 'NORTH_AMERICA', 'OCEANIA'}
SKIP96 = {'CUSTOM', 'POOLPLYR', 'EUROCUP', 'EUROCWC', 'UEFACUP'}
RAMP = (0.85, 0.95)
RAISE_MAX = 3      # (rule 1 lifts a player at most this much: the import's order of a position kept)
OVERRIDES = os.path.join(REPO, 'bin', 'data_json', 'worldgen', '2627', 'rebalance_overrides.json')
PINS = json.load(open(OVERRIDES, encoding='utf-8')).get('pins', {}) if os.path.exists(OVERRIDES) else {}


def total(p):
    return sum(p[f] & 7 for f in F)


def team_files(folder, skip=()):
    return [f for f in sorted(glob.glob(os.path.join(folder, '*.json')))
            if os.path.basename(f)[:-5] not in skip]


def sixteen_outfield(files):
    """{(file, team index, player index): (group, total)} of the 16s' outfield players."""
    out = {}
    for f in files:
        for ti, t in enumerate(json.load(open(f, encoding='utf-8')).get('teams', [])):
            for pi, p in enumerate(t['players'][:16]):
                if p['position']:
                    out[(f, ti, pi)] = (GROUP[P[p['position']]], total(p))
    return out


def caps_96(files):
    caps = {'total': 0}
    caps.update({f: 0 for f in F})
    for f in files:
        for t in json.load(open(f, encoding='utf-8')).get('teams', []):
            ps = t['players'][:16]
            if len(ps) < 16:
                continue
            caps['total'] = max(caps['total'], sum(total(p) for p in ps))
            for s in F:
                caps[s] = max(caps[s], sum(p[s] & 7 for p in ps))
    return caps


def set_total(p, new):
    """His skills scaled to the total `new` (none over 7, the largest remainders first), key marks kept;
    his price by the difference."""
    old = total(p)
    new = max(0, min(49, new))
    if new == old:
        return 0
    v = [p[f] & 7 for f in F]
    if old == 0:
        return 0
    want = [x * new / old for x in v]
    for _ in range(7):                      # (over 7: the excess to the others in proportion)
        over = sum(max(0.0, w - 7) for w in want)
        if over <= 1e-9:
            break
        want = [min(w, 7.0) for w in want]
        room = [7 - w if w < 7 else 0 for w in want]
        rs = sum(room)
        if rs <= 0:
            break
        want = [w + over * r / rs for w, r in zip(want, room)]
    out = [int(w) for w in want]
    for i in sorted(range(7), key=lambda i: -(want[i] - out[i]))[:new - sum(out)]:
        out[i] += 1
    for i, f in enumerate(F):
        p[f] = min(7, out[i]) | (p[f] & 8)
    d = total(p) - old
    p['price'] = max(0, min(49, p['price'] + d))
    return d


def shares(values, excess):
    """excess split over the values in proportion (whole points, the largest remainders first; none
    more than its value)."""
    tot = sum(values)
    if tot <= 0 or excess <= 0:
        return [0] * len(values)
    want = [v * excess / tot for v in values]
    out = [min(v, int(w)) for v, w in zip(values, want)]
    for i in sorted(range(len(values)), key=lambda i: -(want[i] - out[i])):
        if sum(out) >= excess:
            break
        if out[i] < values[i]:
            out[i] += 1
    return out


def cap_group(squad, lim):
    """Points off these players until none of the limits is passed, in proportion to what each holds
    (the owner: a team over a cap shrinks evenly, its best stay its best): a skill's excess from every
    player by his share of it, the total's from every player by his share of the total (his skills in
    proportion). A pinned player is never cut."""
    ps = [p for p in squad if p['position'] and p['player_name'] not in PINS]
    if not ps:
        return 0
    cut = 0
    for s in F:
        excess = sum(p[s] & 7 for p in squad) - int(lim[s])
        if excess > 0:
            for p, c in zip(ps, shares([p[s] & 7 for p in ps], excess)):
                if c:
                    p[s] = ((p[s] & 7) - c) | (p[s] & 8)
                    p['price'] = max(0, p['price'] - c)
                    cut += c
    excess = sum(total(p) for p in squad) - int(lim['total'])
    if excess > 0:
        for p, c in zip(ps, shares([total(p) for p in ps], excess)):
            if c:
                cut -= set_total(p, total(p) - c)
    return cut


def cap_team(t, caps, log):
    """96/97's caps on a team (the owner): its 16 -- who play the matches -- at most 96/97's best 16
    (555, speed 82 ...), and its whole career squad (the extras too, up to 30) at most the same as an
    average a player: a squad of n may hold n / 16 of each."""
    cut = cap_group(t['players'][:16], caps)
    squad = t['players'] + t.get('extra_players', [])
    if len(squad) > 16:
        k = len(squad) / 16
        cut += cap_group(squad, {c: v * k for c, v in caps.items()})
    if cut:
        log[t['team']['team_name']] += cut
    return cut


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--world', default=os.path.join(REPO, 'bin', 'data_json', 'worlds', '2627'))
    ap.add_argument('--apply', action='store_true', help='write the rebalanced files (else report)')
    ap.add_argument('--only-caps', nargs='*', help="rule 2 alone, on these files (the national teams')")
    a = ap.parse_args()
    caps = caps_96(team_files(os.path.join(REPO, 'bin', 'data_json', 'teams'), SKIP96))
    print('96/97 caps:', caps)
    teams_dir = os.path.join(a.world, 'teams')
    if a.only_caps is not None:
        log = collections.Counter()
        for f in a.only_caps:
            d = json.load(open(f, encoding='utf-8'))
            for t in d['teams']:
                cap_team(t, caps, log)
            if a.apply:
                json.dump(d, open(f, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        print('capped: %d squads, %d points' % (len(log), sum(log.values())), dict(log.most_common(10)))
        return
    before = os.path.join(a.world, 'teams_before_rebalance')
    src = before if os.path.isdir(before) else teams_dir
    club_skip = NAT | {os.path.basename(f)[:-5] for f in glob.glob(os.path.join(src, 'GUESTS_*.json'))}
    files = team_files(src, club_skip)
    pool_skip = {os.path.basename(f)[:-5] for f in files if f.endswith('_POOL.json')}
    ranked = sixteen_outfield([f for f in files if os.path.basename(f)[:-5] not in pool_skip])
    o96 = sixteen_outfield(team_files(os.path.join(REPO, 'bin', 'data_json', 'teams'), SKIP96))
    ratio = len(ranked) / len(o96)
    print('outfield players: 96/97 %d, the world %d: x%.2f' % (len(o96), len(ranked), ratio))
    # per group: a map of a total at a percentile -> the 96/97 total there (rank r of the world's
    # n is 96/97's rank r / ratio)
    maps = {}
    for g in ('D', 'FB', 'M', 'W', 'A'):
        w = sorted((t for (grp, t) in ranked.values() if grp == g), reverse=True)
        q = sorted((t for (grp, t) in o96.values() if grp == g), reverse=True)
        n = len(w)
        m = {}
        for r, t in enumerate(w):
            pct = 1 - r / n
            share = 0 if pct < RAMP[0] else 1 if pct >= RAMP[1] else (pct - RAMP[0]) / (RAMP[1] - RAMP[0])
            target = q[min(len(q) - 1, int(r / ratio))]
            m.setdefault(t, []).append(t + share * (target - t))
        maps[g] = {t: min(t + RAISE_MAX, round(sum(v) / len(v))) for t, v in m.items()}
        c96 = sum(1 for t in q if t >= 40)
        moved = sum(1 for t in w if maps[g][t] >= 40)
        print('  %-2s 40+: 96/97 %3d (scaled %3d); the world %3d -> %3d' % (g, c96, round(c96 * ratio),
                                                                         sum(1 for t in w if t >= 40), moved))
    pins = PINS
    # apply to every club file (pools and the extras too: by the same map), then the caps
    log, changed = collections.Counter(), 0
    out = {}
    for f in files:
        d = json.load(open(f, encoding='utf-8'))
        for t in d.get('teams', []):
            for p in t['players'] + t.get('extra_players', []):
                if not p['position']:
                    continue
                g = GROUP[P[p['position']]]
                old = total(p)
                new = maps[g].get(old)
                if new is None:                 # (a total no 16 has: the nearest one's move)
                    near = min(maps[g], key=lambda k: abs(k - old))
                    new = old + maps[g][near] - near
                changed += bool(set_total(p, new))
                if p['player_name'] in pins:     # (the owner's hand: his total and price; outfield only)
                    set_total(p, pins[p['player_name']])
                    p['price'] = pins[p['player_name']]
            cap_team(t, caps, log)
        out[f] = d
    print('players re-totalled: %d; squads capped: %d (%d points), the most: %s'
          % (changed, len(log), sum(log.values()), dict(log.most_common(8))))
    if a.apply:
        if not os.path.isdir(before):
            shutil.copytree(teams_dir, before)
            print('the import\'s files kept in', os.path.relpath(before, REPO))
        for f, d in out.items():
            dst = os.path.join(teams_dir, os.path.basename(f))
            json.dump(d, open(dst, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        print('written:', len(out), 'files into', os.path.relpath(teams_dir, REPO))


if __name__ == '__main__':
    main()
