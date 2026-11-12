#!/usr/bin/env python3
"""convert_swos2020_challenges.py -- SWOS 2020's challenge mode as an OpenSWOS challenge pack.

SWOS 2020 (C:\\Projects\\GitHub\\swos-2020) ships its scenarios as challenges.7z: challenges.ini
(each [SCENARIO n] a challenge) and the team files it names (.EDT, and .DIY competitions holding
two teams). This writes them as a MOD in OpenSWOS's own format (docs/MODS.md §18, "openswos-
challenges", version 2) -- one file per category, in SWOS 2020's order:

    <out>/mod.json
    <out>/data_json/challenges/<nn>-<category>.json
    <out>/teams/<file>.EDT          the .EDT files as they are; each .DIY's teams as an .EDT

Its teams are SWOS 2020's authors' (and its squads others'): the pack is never part of OpenSWOS.
SWOS United ships it, as its own mod.

    scripts/convert_swos2020_challenges.py <dir with challenges.ini> <out dir> [--check]

What each scenario field becomes is SWOS 2020's own behaviour (game.cpp ScenarioGame): the
STARTING EVENT's taker is TEAM STARTING; its LEFT / RIGHT is the screen's side, written here as
the taker's wing as he attacks; RANDOM is drawn at PLAY; a GOLDEN / SILVER GOAL is a win with
that extra-time rule; OVERRIDE ... START PENALTY SHOOTOUT is a shoot-out alone; LOCKED closes
the scenario's whole category, as it did there. As SWOS 2020's SELECT CATEGORY did: EUROPEAN CUPS,
DOMESTIC COMPETITIONS and FANTASY CUPS (never released) are left out, and SWOS MASTERS comes first,
open once FIFA WORLD CUP, UEFA EURO CUP, ISS DELUXE and ISS 64 are completed ("unlock_after"). CPU KEEPER LEVEL, CPU INJURY SPEED, CAMERA
STARTING and PITCH SET are not read (SWOS 2020 ignores the first two and the last has one value).
"""
import json
import os
import re
import shutil
import struct
import sys
import uuid

TEAM_SIZE = 684             # TeamFileEx: TeamFile (76) + 16 PlayerFile (38)
DIY_TEAMS_AT = 5189         # a DOS .DIY: its competition, then a LE u16 count and the teams


def read_ini(path):
    """[SCENARIO n] sections, in file order: (number, {KEY: value})."""
    sections, cur = [], None
    with open(path, encoding='latin-1') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            s = line.strip()
            if not s or s.startswith(';'):
                continue
            m = re.match(r'^\[(.+)\]$', s)
            if m:
                cur = {}
                sections.append((m.group(1).strip(), cur))
                continue
            if cur is not None and '=' in line:
                k, v = line.split('=', 1)
                cur[k.strip().upper()] = v.strip()
    return sections


def teams_of(path):
    """A team file's teams, each its 684 bytes as an .EDT holds them (big-endian)."""
    data = open(path, 'rb').read()
    out = []
    if path.upper().endswith('.EDT'):
        n = struct.unpack_from('>H', data, 0)[0]
        for k in range(n):
            out.append(data[2 + k * TEAM_SIZE:2 + (k + 1) * TEAM_SIZE])
    elif path.upper().endswith('.DIY'):
        n = struct.unpack_from('<H', data, DIY_TEAMS_AT)[0]
        for k in range(n):
            rec = bytearray(data[DIY_TEAMS_AT + 2 + k * TEAM_SIZE:DIY_TEAMS_AT + 2 + (k + 1) * TEAM_SIZE])
            rec[2], rec[3] = rec[3], rec[2]     # (globalTeamNumber: the record's one 16-bit field)
            out.append(bytes(rec))
    else:
        raise SystemExit(f'{path}: not a team file')
    for rec in out:
        if len(rec) != TEAM_SIZE:
            raise SystemExit(f'{path}: cut short')
    return out


def team_name(rec):
    return rec[5:22].split(b'\0')[0].decode('latin-1').rstrip(' ')


def norm(name):
    return re.sub(r'\s+', ' ', name.strip().upper())


def ints(text):
    return [int(x) for x in re.findall(r'-?\d+', text or '')]


def yes(v):
    return (v or '').strip().upper() == 'YES'


def slug(text):
    return re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')


PITCH = ['FROZEN', 'MUDDY', 'WET', 'SOFT', 'NORMAL', 'DRY', 'HARD']
LENGTH = {'3': 0, '5': 1, '7': 2, '10': 3}

# SWOS 2020's SELECT CATEGORY (game.cpp selectCategoryMenu_OnDraw) is drawn by hand for its
# categories: SWOS MASTERS at the top, open once the four below it are completed
# (confirmCategory), and these three never shown -- not released ("THIS CATEGORY WILL BE
# AVAILABLE, AFTER RELEASING LATER"). They are left out of the pack, and SWOS MASTERS comes first.
UNRELEASED = {'EUROPEAN CUPS', 'DOMESTIC COMPETITIONS', 'FANTASY CUPS'}
MASTERS = 'SWOS MASTERS'
MASTERS_AFTER = ['FIFA WORLD CUP', 'UEFA EURO CUP', 'ISS DELUXE', 'ISS 64']


def side_team(v):
    v = (v or '').strip().upper()
    return 0 if v == 'TEAM1' else 1 if v == 'TEAM2' else -1


def convert(src, out, check):
    sections = read_ini(os.path.join(src, 'challenges.ini'))
    files = {f.upper(): f for f in os.listdir(src)}
    team_files = {}             # SWOS 2020's file name -> (our path in the mod, its teams)
    categories, problems = [], []

    def team_file(name):
        key = name.strip().upper()
        if key not in team_files:
            if key not in files:
                raise SystemExit(f'{name}: not in {src}')
            stem = os.path.splitext(files[key])[0].upper()
            team_files[key] = (f'teams/{stem}.EDT', teams_of(os.path.join(src, files[key])))
        return team_files[key]

    packs = {}
    for number, sc in sections:
        sid = slug(number.replace('SCENARIO', ''))
        cat = sc.get('CATEGORY', '').upper()
        if cat in UNRELEASED:
            continue
        if cat not in packs:
            packs[cat] = []
            categories.append(cat)
        name = sc.get('SCENARIO NAME', '').upper()
        cpu = side_team(sc.get('CPU TEAM'))
        yours = 0 if cpu == 1 else 1
        path, recs = team_file(sc.get('TEAM FILE NAME', ''))
        teams = []
        for t in (1, 2):
            want = norm(sc.get(f'TEAM{t} NAME', ''))
            found = [team_name(r) for r in recs if norm(team_name(r)) == want]
            if not found:
                problems.append(f'{number}: no team {want!r} in {path}')
                found = [want]
            teams.append({'file': path, 'name': found[0]})
        up = (sc.get('TEAM PLAYING UP') or '').upper()
        playing_up = 0 if up == 'UP' else 1 if up == 'DOWN' else -1     # (TEAM1's direction)
        kick_off = side_team(sc.get('TEAM STARTING'))
        c = {
            'id': f'swos2020-{sid}',
            'name': name,
            'category': cat,
            'difficulty': int(sc.get('DIFFICULTY') or 1),
            'about': (sc.get('DESCRIPTION5') or '').strip(' *').upper(),
            'description': [(sc.get(f'DESCRIPTION{k}') or '').upper() for k in range(1, 6)],
            'win_message': (sc.get('WIN MESSAGE') or '').upper(),
            'teams': teams,
            'yours': yours,
            'score': [int(sc.get('TEAM1 SCORE') or 0), int(sc.get('TEAM2 SCORE') or 0)],
            'minute': int(sc.get('INITIAL TIME') or 0),
            'extra_time': yes(sc.get('EXTRA TIME')),
            'penalties': yes(sc.get('PENALTY AFTER EXTRA TIME')),
            'length': LENGTH.get((sc.get('GAME LENGTH') or '3').split()[0], 0),
            'playing_up': playing_up,
            'kick_off': kick_off,
            'subs': [int(sc.get('MIN SUBS') or 0), int(sc.get('MAX SUBS') or 0)],
        }
        if sc.get('SCENARIO NICKNAME'):
            c['title'] = sc['SCENARIO NICKNAME'].upper()
        sent = [ints(sc.get('TEAM1 SENT OFF PLAYERS')), ints(sc.get('TEAM2 SENT OFF PLAYERS'))]
        if sent[0] or sent[1]:
            c['sent_off'] = sent
        injured = []
        for t in (1, 2):
            shirts = ints(sc.get(f'TEAM{t} INJURY PLAYERS'))
            levels = ints(sc.get(f'TEAM{t} INJURY PLAYERS LEVELS'))
            injured.append([[s, levels[k] if k < len(levels) else 1] for k, s in enumerate(shirts)])
        if injured[0] or injured[1]:
            c['injured'] = injured
        if yes(sc.get('SECOND LEG')):
            c['first_leg'] = [int(sc.get('TEAM1 FIRST LEG SCORE') or 0), int(sc.get('TEAM2 FIRST LEG SCORE') or 0)]
        pitch = (sc.get('PITCH TYPE') or 'RANDOM').upper()
        if pitch in PITCH:
            c['pitch_type'] = PITCH.index(pitch)
        if cat == MASTERS:          # (LOCKED there until the four are done: confirmCategory)
            c['unlock_after'] = MASTERS_AFTER
        elif yes(sc.get('LOCKED')):
            c['locked'] = True

        win = (sc.get('WIN CONDITION') or 'WIN BY ANY SCORE').upper()
        if win == 'AT LEAST DRAW':
            c['win'] = 'draw'
        elif win == 'CUSTOM':
            ws = [int(sc.get('WIN SCORE1') or 0), int(sc.get('WIN SCORE2') or 0)]
            c['win'] = 'exact'
            c['win_score'] = ws if yours == 0 else [ws[1], ws[0]]       # (yours first)
        else:
            c['win'] = 'win'
            if win == 'GOLDEN GOAL':
                c['extra_time_rule'] = 'golden_goal'
            elif win == 'SILVER GOAL':
                c['extra_time_rule'] = 'silver_goal'
            elif win != 'WIN BY ANY SCORE':
                problems.append(f'{number}: WIN CONDITION {win!r}')

        if (sc.get('OVERRIDE EXTRA TIME OPTION') or '').upper().startswith('IGNORE RELATED OPTIONS'):
            c['shootout'] = True

        ev = (sc.get('STARTING EVENT') or 'KICK OFF').upper()
        taker = kick_off
        r = None
        m = re.match(r'^(LEFT|RIGHT|CENTER) FREE KICK ?(\d)?( HOME)?$', ev)
        if ev in ('KICK OFF', 'NONE'):
            pass
        elif ev == 'PENALTY SHOOTOUT':
            c['shootout'] = True
        elif ev in ('RANDOM FREE KICK', 'RANDOM FREE KICK HOME'):
            r = {'kind': 'free_kick', 'spot': -1}
            if ev.endswith('HOME'):
                r['deep'] = True
        elif m:
            spot = {'LEFT': 0, 'CENTER': 4, 'RIGHT': 4}[m.group(1)] + (int(m.group(2)) if m.group(2) else 0)
            r = {'kind': 'free_kick', 'spot': spot}
            if m.group(3):
                r['deep'] = True
        elif ev == 'CUSTOM FREE KICK':
            r = {'kind': 'free_kick', 'x': int(sc.get('CUSTOM FREE KICK X') or 336),
                 'y': int(sc.get('CUSTOM FREE KICK Y') or 449)}
        elif ev == 'PENALTY KICK':
            r = {'kind': 'penalty'}
        else:
            m = re.match(r'^(LEFT|RIGHT|RANDOM) (CORNER KICK|GOAL KICK|THROW IN)$', ev)
            if not m:
                problems.append(f'{number}: STARTING EVENT {ev!r}')
            else:
                kind = {'CORNER KICK': 'corner', 'GOAL KICK': 'goal_kick', 'THROW IN': 'throw_in'}[m.group(2)]
                r = {'kind': kind}
                if m.group(1) == 'RANDOM':
                    r['wing'] = -1
                elif taker < 0 or playing_up < 0:
                    problems.append(f'{number}: {ev} with the taker or the direction drawn')
                else:
                    # the team attacking the top goal: TEAM1 if it plays UP
                    attacks_top = (taker == 0) == (playing_up == 0)
                    # (his left as he attacks is the screen's left attacking the top goal --
                    #  a goal kick's too, taken from his own goal)
                    r['wing'] = 0 if (m.group(1) == 'LEFT') == attacks_top else 1
        if r is not None:
            r['team'] = taker
            c['restart'] = r
        packs[cat].append(c)

    if problems:
        print('\n'.join(problems), file=sys.stderr)
        if check:
            return 1
    missing = [c for c in MASTERS_AFTER + [MASTERS] if c not in categories]
    if missing:
        problems.append(f'categories SWOS MASTERS needs are not in challenges.ini: {missing}')
        print(problems[-1], file=sys.stderr)
        return 1
    categories.sort(key=lambda c: c != MASTERS)       # (SWOS MASTERS first, the rest as they were)
    count = sum(len(packs[c]) for c in categories)
    if check:
        print(f'{count} scenarios, {len(categories)} categories: fine')
        return 0

    os.makedirs(os.path.join(out, 'data_json', 'challenges'), exist_ok=True)
    os.makedirs(os.path.join(out, 'teams'), exist_ok=True)
    for key, (path, recs) in team_files.items():
        with open(os.path.join(out, path), 'wb') as f:
            f.write(struct.pack('>H', len(recs)) + b''.join(recs))
    for k, cat in enumerate(categories):
        doc = {'format': 'openswos-challenges', 'version': 3 if cat == MASTERS else 2, 'category': cat,
               'challenges': packs[cat]}
        with open(os.path.join(out, 'data_json', 'challenges', f'{k + 1:02d}-{slug(cat)}.json'), 'w') as f:
            json.dump(doc, f, indent=1)
            f.write('\n')
    mod = {
        'uuid': str(uuid.uuid5(uuid.NAMESPACE_URL, 'openswos-mod:swos2020-challenges')),
        'id': 'swos2020-challenges',
        'name': 'SWOS 2020 Challenges',
        'version': '1.1',             # (1.1: SWOS 2020's screen -- SWOS MASTERS unlocks, three unreleased out)
        'author': 'SWOS 2020 (its challenge mode: github.com/starwindz/swos-2020)',
        'about': f'SWOS 2020\'s challenge mode: {count} scenarios in {len(categories)} categories, '
                 'from the 1966 World Cup final on. Converted from its challenges.ini.',
        'targets': ['sdl', 'amiga'],
        'changes': 'modes',
        'default': True,
    }
    with open(os.path.join(out, 'mod.json'), 'w') as f:
        json.dump(mod, f, indent=1)
        f.write('\n')
    print(f'{count} scenarios in {len(categories)} categories, {len(team_files)} team files -> {out}')
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    return convert(args[0], args[1], '--check' in sys.argv)


if __name__ == '__main__':
    sys.exit(main())
