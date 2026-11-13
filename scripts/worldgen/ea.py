#!/usr/bin/env python3
"""worldgen/ea.py -- EA SPORTS FC player attributes from EA's public ratings feed (the data behind
ea.com/games/ea-sports-fc/ratings: drop-api.ea.com), through fetch.get (cached, 3 s apart).

The feed lists every rated player, 100 a page, best first; it takes no league / club filter (only
a name search), so the whole feed is read once (~180 pages) and kept in the cache. EA's data:
only skills DERIVED from it are ever written out (docs/WORLD_DATAGEN.md).

    python3 -I scripts/worldgen/ea.py           # fetch / read the feed, print a summary
"""
import json
import os
import re
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch                                            # noqa: E402

FEED = 'https://drop-api.ea.com/rating/ea-sports-fc?locale=en&limit=100&offset={}'


def players(offline=False):
    """Every player of the feed: dicts with id, name parts, birth (YYYY-MM-DD), overall, position,
    league, team, height, nationality and stats {attribute: value}."""
    out, off, total = [], 0, None
    while total is None or off < total:
        d = json.loads(fetch.get(FEED.format(off), json_api=True, offline=offline))
        total = d['totalItems']
        for it in d['items']:
            if it.get('gender', {}).get('id') != 0:      # men's football only
                continue
            m, dd, y = it['birthdate'].split()[0].split('/')
            out.append(dict(
                id=it['id'], first=it.get('firstName') or '', last=it.get('lastName') or '',
                common=it.get('commonName') or '', birth=f'{int(y):04}-{int(m):02}-{int(dd):02}',
                overall=it['overallRating'], pos=it['position']['shortLabel'],
                alt=[p['shortLabel'] for p in it.get('alternatePositions') or []],
                league=it.get('leagueName') or '', team=(it.get('team') or {}).get('label', ''),
                height=it.get('height'), nat=(it.get('nationality') or {}).get('label', ''),
                stats={k: v['value'] for k, v in (it.get('stats') or {}).items()}))
        off += 100
    return out


def fold(s):
    """Lower-case ASCII letters only, accents folded (name matching)."""
    s = unicodedata.normalize('NFKD', s.replace('ø', 'o').replace('Ø', 'O').replace('ß', 'ss')
                              .replace('ł', 'l').replace('đ', 'd').replace('ı', 'i'))
    return re.sub(r'[^a-z ]', '', s.encode('ascii', 'ignore').decode().lower().replace('-', ' '))


def tokens(*parts):
    return {t for p in parts for t in fold(p).split() if len(t) > 1}


class Index:
    """EA players by birth date, matched to a source player by birth date + a shared name token
    (the Mod's eamatch rule: same birth date + name tokens, accents folded)."""

    def __init__(self, ps):
        self.by_birth = {}
        for p in ps:
            self.by_birth.setdefault(p['birth'], []).append(p)

    def match(self, name, birth):
        cands = self.by_birth.get(birth) or []
        want = tokens(name)
        best, score = None, 0
        for p in cands:
            s = len(want & tokens(p['first'], p['last'], p['common']))
            if s > score:
                best, score = p, s
        return best


if __name__ == '__main__':
    ps = players()
    print(f'{len(ps)} players; fetched {fetch.COUNT["fetched"]}, cached {fetch.COUNT["cached"]}')
    leagues = {}
    for p in ps:
        leagues[p['league']] = leagues.get(p['league'], 0) + 1
    for k, v in sorted(leagues.items(), key=lambda x: -x[1])[:15]:
        print(f'  {v:5} {k}')
