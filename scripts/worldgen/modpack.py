#!/usr/bin/env python3
"""worldgen/modpack.py -- read a world pack in the SWOS 96/97 Mod's format (its mkseason 'explicit'
squads): clubs.csv, players.csv, pack.json of one country. Used both for the Mod's own packs (the
reference worldgen is measured against: bin/data_json/worlds/2627/mod/packs/, gitignored, read
only) and for worldgen's output, which is written in the same format.

Columns (players.csv): club,name,role,position,number,nat,skin,pa,ve,he,ta,co,sp,fi,price -- skills
0..7 with a trailing '*' on a key skill, price = the game's price byte (0..49).
Columns (clubs.csv): key,name,coach,country,file,division,tactic,kit1,kit2,lineup,source.
"""
import csv
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MOD_PACKS = os.path.join(REPO, 'bin', 'data_json', 'worlds', '2627', 'mod', 'packs')
SKILLS = ['pa', 've', 'he', 'ta', 'co', 'sp', 'fi']
PLAYER_COLS = ['club', 'name', 'role', 'position', 'number', 'nat', 'skin'] + SKILLS + ['price']
CLUB_COLS = ['key', 'name', 'coach', 'country', 'file', 'division', 'tactic', 'kit1', 'kit2', 'lineup', 'source']
ROLE = {'G': 'G', 'RB': 'D', 'LB': 'D', 'D': 'D', 'RW': 'M', 'LW': 'M', 'M': 'M', 'A': 'A'}


def skills(row):
    """([7 values], [7 key flags]) of a players.csv row."""
    return [int(row[s].rstrip('*')) for s in SKILLS], [row[s].endswith('*') for s in SKILLS]


def tm_id(source):
    m = re.search(r'\(TM (\d+)\)', source or '')
    return m.group(1) if m else None


class Pack:
    def __init__(self, folder):
        self.folder = folder
        with open(os.path.join(folder, 'clubs.csv'), encoding='utf-8', newline='') as f:
            self.clubs = list(csv.DictReader(f))
        with open(os.path.join(folder, 'players.csv'), encoding='utf-8', newline='') as f:
            rows = list(csv.DictReader(f))
        self.squads = {c['key']: [] for c in self.clubs}
        for r in rows:
            self.squads.setdefault(r['club'], []).append(r)
        self.club = {c['key']: c for c in self.clubs}

    def division(self, d):
        return [c for c in self.clubs if c['division'] == str(d)]


def mod_pack(cc):
    return Pack(os.path.join(MOD_PACKS, cc))


def write(folder, clubs, players):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'clubs.csv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, CLUB_COLS, lineterminator='\n')
        w.writeheader()
        w.writerows(clubs)
    with open(os.path.join(folder, 'players.csv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, PLAYER_COLS, lineterminator='\n')
        w.writeheader()
        w.writerows(players)
