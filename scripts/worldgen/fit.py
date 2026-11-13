#!/usr/bin/env python3
"""worldgen/fit.py -- fit worldgen's scales to the SWOS 96/97 Mod's World 2026-27 (docs/WORLD_DATAGEN.md).

Reads the Mod's packs (bin/data_json/worlds/2627/mod/packs/, gitignored, READ ONLY) and the cached
sources (fetch / ea / tmdata / wiki), and writes scripts/worldgen/curves.json -- numbers of our
own making, derived from the Mod's output, never the Mod's data itself:

  level.<outfield|keeper>.value   isotonic steps: age-adjusted ln(market value) -> skill level S
  level.<outfield|keeper>.overall isotonic steps: EA SPORTS FC overall -> S
                                  (the Mod: S = 0.5 the value curve + 0.5 the overall curve, then
                                  a step per club; fitted here with those steps as unknowns)
  nat_fifa                        FIFA / Wikipedia country code -> game nationality number
  nat_tm                          Transfermarkt citizenship name -> game nationality number
  dark                            game nationalities whose players are drawn dark-skinned
  profiles                        Transfermarkt sub-position -> 7 skill weights (players EA lacks)

    python3 -I scripts/worldgen/fit.py [--report]
"""
import bisect
import json
import math
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ea                                               # noqa: E402
import join                                             # noqa: E402
import model                                            # noqa: E402
import modpack                                          # noqa: E402
import wiki                                             # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'curves.json')


def isotonic(xs, ys):
    """Pool-adjacent-violators: [(x from, mean y)] non-decreasing."""
    order = sorted(range(len(xs)), key=lambda i: xs[i])
    blocks = []
    for i in order:
        blocks.append([ys[i], 1, xs[i]])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] > blocks[-1][0] / blocks[-1][1]:
            s, n, _ = blocks.pop()
            blocks[-1][0] += s
            blocks[-1][1] += n
    return [[round(b[2], 4), round(b[0] / b[1], 3)] for b in blocks]


def gather(src):
    """(player records, wiki nationality votes) of every Mod 16 the sources match."""
    recs = []
    for cc in sorted(os.listdir(modpack.MOD_PACKS)):
        if not os.path.exists(os.path.join(modpack.MOD_PACKS, cc, 'clubs.csv')):
            continue
        pk = modpack.mod_pack(cc)
        for c in pk.clubs:
            tid = modpack.tm_id(c['source'])
            for i, r in enumerate(pk.squads[c['key']]):
                t = src.tm_player(r['name'], tid)
                e = src.ea_player(r['name'], t['date_of_birth'][:10] if t and t['date_of_birth'] else None)
                if e is None and t is None:
                    e = src.ea_player(r['name'])
                recs.append(dict(cc=cc, club=cc + ':' + c['key'], div=cc + ':' + c['division'], idx=i, row=r,
                                 tm=t, ea=e))
    return recs


def fit_levels(recs):
    out = {}
    for keeper in (False, True):
        data = []
        for p in recs:
            r, t = p['row'], p['tm']
            if p['idx'] >= 16 or (r['position'] == 'G') != keeper or not t or not t['market_value_in_eur'] \
                    or not t['date_of_birth']:
                continue
            if len(ea.tokens(r['name']) & ea.tokens(t['name'])) < 2 and not p['ea']:
                continue                            # a one-token name match: too unsure to fit on
            v, _ = modpack.skills(r)
            data.append(dict(S=int(r['price']) if keeper else sum(v), club=p['club'], div=p['div'],
                             x=model.adjusted_value(float(t['market_value_in_eur']), model.age(t['date_of_birth'])),
                             o=p['ea']['overall'] if p['ea'] else None))
        step = defaultdict(float)
        F = G = None
        for _ in range(8):                          # backfitting: value curve, overall curve, club steps
            xs, ys = [], []
            for d in data:
                y = d['S'] - step[d['club']]
                if d['o'] is None:
                    xs.append(d['x']); ys.append(y)
                elif G:
                    xs.append(d['x']); ys.append(2 * y - model.curve(G, d['o']))
            F = isotonic(xs, ys)
            xs, ys = [], []
            for d in data:
                if d['o'] is not None:
                    xs.append(d['o']); ys.append(2 * (d['S'] - step[d['club']]) - model.curve(F, d['x']))
            G = isotonic(xs, ys)
            res = defaultdict(list)
            for d in data:
                res[d['club']].append(d['S'] - model.level(F, G, d['x'], d['o']))
            med = {k: sorted(v)[len(v) // 2] for k, v in res.items()}
            divof = {d['club']: d['div'] for d in data}
            dsum = defaultdict(list)
            for k, v in med.items():
                dsum[divof[k]].append(v)
            # a division's steps sum to zero (the Mod's ladder is the division's own strengths), so
            # the curves carry the level and the steps only the order
            step = defaultdict(float, {k: max(-5, min(5, round(v - sum(dsum[divof[k]]) / len(dsum[divof[k]]))))
                                       for k, v in med.items()})
        n = len(data)
        ex = sum(round(model.level(F, G, d['x'], d['o']) + step[d['club']]) == d['S'] for d in data)
        w1 = sum(abs(round(model.level(F, G, d['x'], d['o']) + step[d['club']]) - d['S']) <= 1 for d in data)
        out['keeper' if keeper else 'outfield'] = dict(value=F, overall=G, fitted_on=n,
                                                       exact_with_club_step=round(ex / n, 3),
                                                       within1_with_club_step=round(w1 / n, 3))
    return out


def fit_profiles(recs):
    """Weights per Transfermarkt sub-position for players EA lacks: coordinate search maximising the
    players reproduced exactly or with one point moved (the Mod moves one point between two non-key
    skills by a hash of the player's Transfermarkt id, which we cannot know)."""
    grp = defaultdict(list)
    for p in recs:
        r = p['row']
        if p['ea'] is not None or r['position'] == 'G' or p['idx'] >= 16 or not p['tm']:
            continue
        grp[p['tm']['sub_position']].append(modpack.skills(r)[0])

    def score(vs, w):
        n = 0
        for v in vs:
            pr = model.split(sum(v), w)
            n += sum(abs(a - b) for a, b in zip(v, pr)) <= 2
        return n
    out = {}
    for sub, vs in grp.items():
        if len(vs) < 40 or sub == 'Goalkeeper':
            continue
        w = list(model.PROFILES.get(sub) or [1.0] * 7)
        sc, step = score(vs, w), 0.4
        while step > 0.02:
            better = False
            for i in range(7):
                for d in (step, -step):
                    w2 = list(w)
                    w2[i] = max(0.05, w2[i] + d)
                    s = score(vs, w2)
                    if s > sc:
                        sc, w, better = s, w2, True
            if not better:
                step /= 2
        tot = sum(w)
        out[sub] = dict(weights=[round(x * 7 / tot, 3) for x in w], fitted_on=len(vs),
                        within_one_move=round(sc / len(vs), 3))
    return out


def fit_nations(recs):
    tmvote, wvote = defaultdict(Counter), defaultdict(Counter)
    for p in recs:
        if p['tm'] and p['tm']['country_of_citizenship']:
            tmvote[p['tm']['country_of_citizenship']][int(p['row']['nat'])] += 1
    dark = defaultdict(Counter)
    for p in recs:
        dark[int(p['row']['nat'])][p['row']['skin']] += 1
    return ({k: v.most_common(1)[0][0] for k, v in sorted(tmvote.items())},
            sorted(n for n, c in dark.items() if (c['dark'] + c['darkblond']) * 2 > sum(c.values())))


def fit_nat_fifa(src, recs):
    """FIFA codes (Wikipedia squads of the sample clubs) -> the Mod's nationality of the same player."""
    vote = defaultdict(Counter)
    by_club = defaultdict(dict)
    for p in recs:
        by_club[p['club']][frozenset(ea.tokens(p['row']['name']))] = int(p['row']['nat'])
    clubs = []
    for cc in sorted(os.listdir(modpack.MOD_PACKS)):
        if os.path.exists(os.path.join(modpack.MOD_PACKS, cc, 'clubs.csv')):
            for c in modpack.mod_pack(cc).clubs:
                clubs.append((cc + ':' + c['key'], modpack.tm_id(c['source'])))
    sample = {r['tm_id'] for r in model.sample()}
    clubs = [c for c in clubs if c[1] in sample]
    arts = wiki.articles([c[1] for c in clubs], offline=True)
    texts = wiki.wikitext(arts.values(), offline=True)
    for key, tid in clubs:
        if tid not in arts or arts[tid] not in texts:
            continue
        for pl in wiki.squad(texts[arts[tid]]):
            n = by_club[key].get(frozenset(ea.tokens(pl['name'])))
            if n is not None and pl['nat']:
                vote[pl['nat']][n] += 1
    return {k: v.most_common(1)[0][0] for k, v in sorted(vote.items())}


def main():
    src = join.Sources(offline=True)
    recs = gather(src)
    curves = dict(about=__doc__.split('\n\n')[0], level=fit_levels(recs), profiles=fit_profiles(recs))
    curves['nat_tm'], curves['dark'] = fit_nations(recs)
    curves['nat_fifa'] = fit_nat_fifa(src, recs)
    with open(OUT, 'w', encoding='utf-8') as f:
        json.dump(curves, f, indent=1, sort_keys=True)
        f.write('\n')
    for k, v in curves['level'].items():
        print(f"level {k}: fitted on {v['fitted_on']}, with the club steps exact {v['exact_with_club_step']:.0%}, "
              f"within 1 {v['within1_with_club_step']:.0%}")
    for k, v in sorted(curves['profiles'].items(), key=lambda x: -x[1]['fitted_on']):
        print(f"profile {k:20} n {v['fitted_on']:5}  within one moved point {v['within_one_move']:.0%}  {v['weights']}")
    print(f"nat_tm {len(curves['nat_tm'])}, nat_fifa {len(curves['nat_fifa'])}, dark {len(curves['dark'])}")


if __name__ == '__main__':
    main()
