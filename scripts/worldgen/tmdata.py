#!/usr/bin/env python3
"""worldgen/tmdata.py -- Transfermarkt data from the open "transfermarkt-datasets" project
(github.com/dcaribou/transfermarkt-datasets, CC0; its data scraped from Transfermarkt), read
through fetch.get (cached once).

Why not Transfermarkt's own pages: on 2026-11-13 transfermarkt.com answers a script with an AWS
WAF "Human Verification" CAPTCHA (HTTP 405, x-amzn-waf-action: captcha), whatever its request
headers. A CAPTCHA is an access control, so worldgen does not get past it (docs/WORLD_DATAGEN.md
"Sources"). The dataset is frozen at its 2026-07-06 snapshot (before the summer window closed);
the Mod's own snapshot is ~2026-10-15.

Tables used (one CSV each): players (id, name, birth date, citizenship, position, sub-position,
foot, height, current club, market value), clubs (id, name, domestic competition, coach),
player_valuations (id, date, value, club: a value AS OF a date).

    python3 -I scripts/worldgen/tmdata.py        # fetch / read the tables, print a summary
"""
import csv
import gzip
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch                                            # noqa: E402

BASE = 'https://pub-e682421888d945d684bcae8890b0ec20.r2.dev/data/{}.csv.gz'
csv.field_size_limit(1 << 24)


def table(name, offline=False):
    raw = fetch.get(BASE.format(name), json_api=True, offline=offline)
    return list(csv.DictReader(io.TextIOWrapper(gzip.GzipFile(fileobj=io.BytesIO(raw)), encoding='utf-8')))


class TM:
    def __init__(self, offline=False, valuations=True):
        self.players = table('players', offline)
        self.clubs = {c['club_id']: c for c in table('clubs', offline)}
        self.by_club = {}
        for p in self.players:
            self.by_club.setdefault(p['current_club_id'], []).append(p)
        self.vals = {}
        if valuations:
            for v in table('player_valuations', offline):
                self.vals.setdefault(v['player_id'], []).append((v['date'], int(float(v['market_value_in_eur']))))
            for k in self.vals:
                self.vals[k].sort()

    def value_at(self, pid, date):
        """The player's last market value on or before date (YYYY-MM-DD), or None."""
        best = None
        for d, v in self.vals.get(pid, ()):
            if d <= date:
                best = v
        return best


if __name__ == '__main__':
    t = TM()
    print(f'{len(t.players)} players, {len(t.clubs)} clubs, {len(t.vals)} valuation histories; '
          f'fetched {fetch.COUNT["fetched"]}, cached {fetch.COUNT["cached"]}')
    print('columns:', list(t.players[0]))
    print('club columns:', list(next(iter(t.clubs.values()))))
    comps = {}
    for c in t.clubs.values():
        comps[c['domestic_competition_id']] = comps.get(c['domestic_competition_id'], 0) + 1
    print(sorted(comps.items()))
