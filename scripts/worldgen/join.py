#!/usr/bin/env python3
"""worldgen/join.py -- one source record per club and player: a club's Transfermarkt id (from a
pack's clubs.csv `source`, "Arsenal FC (TM 11)"), its players in the Transfermarkt dataset, and
each player's EA SPORTS FC entry (the Mod's eamatch rule: the same birth date and a shared name
token, accents folded).

Used two ways: to measure the Mod's derivation (a Mod row -> its sources) and by gen.py (a
Transfermarkt squad -> EA attributes).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ea                                               # noqa: E402
import tmdata                                           # noqa: E402


class Sources:
    def __init__(self, offline=True):
        self.tm = tmdata.TM(offline=offline)
        self.ea_players = ea.players(offline=offline)
        self.ea = ea.Index(self.ea_players)
        self.tm_by_tokens = {}
        for p in self.tm.players:
            self.tm_by_tokens.setdefault(frozenset(ea.tokens(p['name'])), []).append(p)
        self.ea_by_tokens = {}
        for p in self.ea_players:
            for key in (ea.tokens(p['first'], p['last']), ea.tokens(p['common'])):
                if key:
                    self.ea_by_tokens.setdefault(frozenset(key), []).append(p)

    def tm_player(self, name, club_id):
        """The dataset player of a game-form name ('BUKAYO SAKA', 'J. SURNAME') at a club, else a
        player of that exact name anywhere when only one has it."""
        want = ea.tokens(name)
        last = ea.fold(name).split()[-1:]               # the surname must be among the tokens
        best, score = None, 0
        for p in self.tm.by_club.get(club_id, ()):
            have = ea.tokens(p['name'])
            s = len(want & have)
            if s > score and (not last or last[0] in have):
                best, score = p, s
        if best:
            return best
        cands = self.tm_by_tokens.get(frozenset(want)) or []
        return cands[0] if len(cands) == 1 else None

    def ea_player(self, name, birth=None):
        if birth:
            return self.ea.match(name, birth)
        cands = self.ea_by_tokens.get(frozenset(ea.tokens(name))) or []
        return cands[0] if len(cands) == 1 else None
