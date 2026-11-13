#!/usr/bin/env python3
"""worldgen/model.py -- the derivation rules: how a squad's game data comes out of its sources
(docs/WORLD_DATAGEN.md lists each rule and how exactly it reproduces the SWOS 96/97 Mod's).

Pure functions and small tables; the fitted scales are in curves.json (fit.py writes it).
"""
import bisect
import csv
import json
import math
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SKILLS = ['pa', 've', 'he', 'ta', 'co', 'sp', 'fi']

# ---- level: market value and EA overall -> skill level S (0..49) -------------------------------


def age(birth, on=(2026, 10, 15)):
    """Whole years on the snapshot date (the Mod's Transfermarkt snapshot, mid-October 2026)."""
    y, m, d = (int(x) for x in birth[:10].split('-'))
    return on[0] - y - ((on[1], on[2]) < (m, d))


def adjusted_value(value, years):
    """The Mod's age correction of ln(market value): young players' values are inflated, old
    players' deflated -- 0.1 off per year under 25 (at most 4 years), 0.15 on per year over 28
    (at most 7)."""
    x = math.log(max(value, 10000))
    if years < 25:
        x -= 0.1 * min(4, 25 - years)
    if years > 28:
        x += 0.15 * min(7, years - 28)
    return x


def curve(steps, x):
    """A fitted isotonic curve [[x from, y], ...] at x."""
    k = bisect.bisect_right([s[0] for s in steps], x) - 1
    return steps[max(k, 0)][1]


def level(F, G, x, overall):
    """S before the club's step: the value curve, blended half and half with the EA overall curve
    where EA rates the player (the Mod: 0.5 the Transfermarkt value model + 0.5 the EA overall
    mapped onto the same scale)."""
    if overall is None:
        return curve(F, x)
    return 0.5 * curve(F, x) + 0.5 * curve(G, overall)


# ---- the 7 skills -------------------------------------------------------------------------------

# EA SPORTS FC attributes behind each game skill (the Mod's mapping, measured exact below)
EA_ATTR = {'pa': ['shortPassing', 'vision'], 've': ['shotPower', 'longShots'],
           'he': ['headingAccuracy', 'jumping'], 'ta': ['standingTackle', 'slidingTackle', 'interceptions'],
           'co': ['ballControl', 'dribbling'], 'sp': ['sprintSpeed', 'acceleration'], 'fi': ['finishing']}

# weights for players EA does not rate, by Transfermarkt sub-position: curves.json 'profiles'
# (fitted) wins; these are the starting points (the original game's mean skills per position)
PROFILES = {
    'Right-Back': [1.22, 1.13, 1.39, 4.29, 1.1, 3.74, 1.02], 'Left-Back': [1.22, 1.13, 1.39, 4.29, 1.1, 3.74, 1.02],
    'Centre-Back': [1.13, .99, 3.69, 3.92, .96, 1.08, .89],
    'Right Winger': [1.71, 1.36, 1.2, 1.55, 3.71, 3.83, 1.13], 'Left Winger': [1.71, 1.36, 1.2, 1.55, 3.71, 3.83, 1.13],
    'Right Midfield': [2.2, 1.2, 1.2, 1.4, 3.3, 3.3, 1.0], 'Left Midfield': [2.2, 1.2, 1.2, 1.4, 3.3, 3.3, 1.0],
    'Central Midfield': [3.35, 1.19, 1.26, 3.43, 1.3, 1.2, 1.19],
    'Defensive Midfield': [3.0, 1.1, 1.3, 4.0, 1.2, 1.2, 1.0],
    'Attacking Midfield': [3.6, 1.4, 1.0, 1.3, 3.0, 1.3, 1.3],
    'Centre-Forward': [1.3, 1.68, 3.43, 1.24, 1.19, 1.41, 3.75],
    'Second Striker': [1.5, 1.7, 1.5, 1.0, 3.4, 1.5, 3.6],
}
POS_PROFILE = {'RB': 'Right-Back', 'LB': 'Left-Back', 'D': 'Centre-Back', 'RW': 'Right Winger',
               'LW': 'Left Winger', 'M': 'Central Midfield', 'A': 'Centre-Forward'}


def split(S, w):
    """S over the 7 skills in proportion to the weights: a skill over 7 is clamped and its excess
    shared by the others (in proportion), then rounded down with the leftover points going to the
    largest remainders (the sum stays S)."""
    S = max(0, min(49, S))
    W = sum(w)
    x = [S * v / W for v in w] if W > 0 else [S / 7] * 7
    fixed = [False] * 7
    while True:
        over = [i for i in range(7) if not fixed[i] and x[i] > 7]
        if not over:
            break
        for i in over:
            x[i], fixed[i] = 7, True
        rest = S - sum(x[i] for i in range(7) if fixed[i])
        ws = sum(w[i] for i in range(7) if not fixed[i])
        for i in range(7):
            if not fixed[i]:
                x[i] = rest * w[i] / ws if ws > 0 else 0
    v = [int(math.floor(a)) for a in x]
    for i in sorted(range(7), key=lambda i: (-(x[i] - v[i]), i))[:S - sum(v)]:
        v[i] += 1
    return v


def ea_skills(S, stats):
    """(7 skills, key indices) from EA attributes: weights (attribute - 25) ^ 1.6, key skills = the
    two best attributes (measured: 92% of 7,400 players' seven skills exact, keys 93%)."""
    a = [sum(stats.get(k, 0) for k in EA_ATTR[s]) / len(EA_ATTR[s]) for s in SKILLS]
    v = split(S, [max(x - 25, 0) ** 1.6 for x in a])
    return v, sorted(sorted(range(7), key=lambda i: (-a[i], i))[:2])


def profile_skills(S, sub, curves, ident, move=True):
    """(7 skills, key indices) for a player EA lacks: his sub-position's profile, key skills its two
    largest weights; then (as the Mod does, with its own hash we cannot know) one point moved from
    one non-key skill to another, chosen by a CRC of the player's identity."""
    prof = (curves.get('profiles', {}).get(sub) or {}).get('weights') or PROFILES.get(sub) or [1.0] * 7
    v = split(S, prof)
    keys = sorted(sorted(range(7), key=lambda i: (-prof[i], i))[:2])
    if move:
        h = zlib.crc32(str(ident).encode())
        others = [i for i in range(7) if i not in keys]
        src = [i for i in others if v[i] > 0]
        if src:
            a = src[h % len(src)]
            dst = [i for i in others if i != a and v[i] < 7]
            if dst:
                b = dst[(h >> 8) % len(dst)]
                v[a] -= 1
                v[b] += 1
    return v, keys


# ---- positions and squads -----------------------------------------------------------------------

TM_POS = {'Goalkeeper': 'G', 'Right-Back': 'RB', 'Left-Back': 'LB', 'Centre-Back': 'D',
          'Defensive Midfield': 'M', 'Central Midfield': 'M', 'Attacking Midfield': 'M',
          'Right Winger': 'RW', 'Right Midfield': 'RW', 'Left Winger': 'LW', 'Left Midfield': 'LW',
          'Centre-Forward': 'A', 'Second Striker': 'A',
          'Defender': 'D', 'Midfield': 'M', 'Attack': 'A'}
EA_POS = {'GK': 'G', 'RB': 'RB', 'RWB': 'RB', 'LB': 'LB', 'LWB': 'LB', 'CB': 'D', 'CDM': 'M', 'CM': 'M',
          'CAM': 'M', 'RM': 'RW', 'RW': 'RW', 'LM': 'LW', 'LW': 'LW', 'ST': 'A', 'CF': 'A'}
EA_SUB = {'GK': 'Goalkeeper', 'RB': 'Right-Back', 'RWB': 'Right-Back', 'LB': 'Left-Back', 'LWB': 'Left-Back',
          'CB': 'Centre-Back', 'CDM': 'Defensive Midfield', 'CM': 'Central Midfield', 'CAM': 'Attacking Midfield',
          'RM': 'Right Midfield', 'RW': 'Right Winger', 'LM': 'Left Midfield', 'LW': 'Left Winger',
          'ST': 'Centre-Forward', 'CF': 'Second Striker'}
WIKI_POS = {'GK': 'G', 'DF': 'D', 'MF': 'M', 'FW': 'A'}
WIKI_SUB = {'GK': 'Goalkeeper', 'DF': 'Centre-Back', 'MF': 'Central Midfield', 'FW': 'Centre-Forward'}

# the record's 16 (the Mod): G RB LB D D RW LW M M A A G D M A + the best remaining, each slot the
# most valuable player of its position, then of the near positions
SLOTS = ['G', 'RB', 'LB', 'D', 'D', 'RW', 'LW', 'M', 'M', 'A', 'A', 'G', 'D', 'M', 'A']
NEAR = {'G': ['G'], 'RB': ['RB', 'LB', 'D', 'M'], 'LB': ['LB', 'RB', 'D', 'M'], 'D': ['D', 'RB', 'LB', 'M'],
        'RW': ['RW', 'LW', 'M', 'A'], 'LW': ['LW', 'RW', 'M', 'A'], 'M': ['M', 'RW', 'LW', 'A', 'D'],
        'A': ['A', 'RW', 'LW', 'M']}
MAX_SQUAD = 30


def pick_sixteen(players):
    """players: dicts with 'pos' and 'value' (sort key). -> (the 16 in record order, each with
    'position' = its slot's for rows 1-15, its own for row 16; the extras by value, at most 14,
    the best spare keeper among them when there is one)."""
    pool = sorted(players, key=lambda p: (-p['value'], p['order']))
    used, rows = set(), []
    for slot in SLOTS:
        pick = None
        for rated in (True, False):         # (worldgen: players a source rates first -- a Wikipedia
            for pos in NEAR[slot]:          # squad lists academy players Transfermarkt's does not)
                pick = next((p for p in pool if id(p) not in used and p['pos'] == pos
                             and (p.get('rated', True) or not rated)), None)
                if pick:
                    break
            if pick:
                break
        if pick is None and slot != 'G':
            pick = next((p for p in pool if id(p) not in used and p['pos'] != 'G'), None)
        if pick is None:
            pick = next((p for p in pool if id(p) not in used), None)
        if pick is None:
            continue
        used.add(id(pick))
        rows.append(dict(pick, position=slot))
    rest = [p for p in pool if id(p) not in used]
    if rest:
        p = next((p for p in rest if p['pos'] != 'G'), rest[0])
        used.add(id(p))
        rows.append(dict(p, position=p['pos']))
    extras = [dict(p, position=p['pos']) for p in pool if id(p) not in used][:MAX_SQUAD - len(rows)]
    spare_gk = [p for p in pool if id(p) not in used and p['pos'] == 'G']
    if spare_gk and not any(p['position'] == 'G' for p in extras) and extras:
        extras[-1] = dict(spare_gk[0], position='G')
    return rows, extras


def strength(rows):
    """The Mod's club strength ('skills11'): the first 11's levels, the keeper's twice."""
    return sum(p['S'] for p in rows[1:11]) + 2 * (rows[0]['S'] if rows else 0)


def calibrate(clubs, w_table=0.65, spread=70, max_step=5):
    """The Mod's division calibration: each club's strength moved toward the strength of its
    blended rank (w_table x its 2025-26 final position among the clubs that played the same level
    + the rest x its squad rank), the ladder widened to at least `spread` between first and last;
    every player of the club by the same step (|step| <= max_step; 13 = the strength's player
    weight). clubs: [{'rows': [...], 'prev_level_same': bool, 'prev_rank': int}] in table order.
    Returns the step of each club."""
    st = [strength(c['rows']) for c in clubs]
    n = len(clubs)
    if n < 2:
        return [0] * n
    squad_rank = {i: r for r, i in enumerate(sorted(range(n), key=lambda i: -st[i]))}
    same = [i for i in range(n) if clubs[i]['prev_level_same']]
    table_rank = {i: r for r, i in enumerate(sorted(same, key=lambda i: clubs[i]['prev_rank']))}
    blend = {}
    for i in range(n):
        if i in table_rank:
            blend[i] = w_table * table_rank[i] * (n - 1) / max(1, len(same) - 1) + (1 - w_table) * squad_rank[i]
        else:
            blend[i] = squad_rank[i]
    ladder = sorted(st, reverse=True)
    lo, hi = ladder[-1], ladder[0]
    if hi - lo < spread:
        mid = (hi + lo) / 2
        ladder = [mid + (x - mid) * spread / max(1, hi - lo) if hi > lo else mid + spread * (0.5 - k / (n - 1))
                  for k, x in enumerate(ladder)]
    order = sorted(range(n), key=lambda i: (blend[i], squad_rank[i]))
    steps = [0] * n
    for pos, i in enumerate(order):
        steps[i] = max(-max_step, min(max_step, round((ladder[pos] - st[i]) / 13)))
    return steps


# ---- nationality, face, names, numbers ----------------------------------------------------------

GAME_NATS = ("ALBAUTBELBULCROCYPTCHDENENGESTFARFINFRAGERGREHUNISLISRITALATLITLUXMLTHOLNIRNORPOLPORROMRUSSMRSCO"
             "SLOSWETURUKRWALYUGBLSSVKESPARMBOSAZBGEOSUIIRLMACTRKLIEMOLCRCSALGUAHONBHMMEXPANUSABAHNICBERJAMTRI"
             "CANBARELSSVCARGBOLBRACHLCOLECUPARSURURUVNZGUYPERALGSAFBOTBFSBURLESZAIZAMGHASENCIVTUNMLIMDGCMRCHD"
             "UGALIBMOZKENSUDSWAANGTOGZIMEGYTANNIGETHGABSIEBENCONGUISRLMARGAMMLWJAPTAIINDBANBRUIRAJORSRISYRKOR"
             "IRNVIEMLYSAUYEMKUWLAONKROMAPAKPHICHISGPMAUMYAPAPTADUZBQATUAEAUSNZLFIJSOLCUSGDRURSFRGMNEKOSANDGIB"
             "KAZKGZBLZHAICUWARUDOMCUBPURGRNGLPMTQGUFATGSKNLCAMSRCAYCPVGNBEQGMTNNERRWACOMCTASSDERISOMNAMLBYDJI"
             "STPREUMYTLBNPLEBHRHKGTHAIDNCAMAFGNEPMAOMGLMDVTLSGUMNCLTAHVANSAMTGA")
GAME_NAT = {GAME_NATS[i:i + 3]: i // 3 for i in range(0, len(GAME_NATS), 3)}   # src/swos_data_bulk_imports.c


def nationality(fifa, tm_country, curves, fallback):
    if fifa and fifa in curves.get('nat_fifa', {}):
        return curves['nat_fifa'][fifa]
    if tm_country and tm_country in curves.get('nat_tm', {}):
        return curves['nat_tm'][tm_country]
    if fifa and fifa in GAME_NAT:
        return GAME_NAT[fifa]
    return fallback


def face(nat, curves):
    """The Mod's nationality rule: dark when the nation is in the dark list, else light (the Mod
    also reads a second nationality, the blond hair from EA's portraits and a hand list; none of
    them here)."""
    return 'dark' if nat in set(curves.get('dark', ())) else 'light'


def free_numbers(players):
    """Shirt numbers 1..99, each once: a missing, duplicate or out-of-range number takes the
    lowest free one (the Mod: 'a free one when missing / > 99')."""
    used = set()
    for p in players:
        n = p.get('number')
        if n is not None and 1 <= n <= 99 and n not in used:
            used.add(n)
        else:
            p['number'] = None
    free = (n for n in range(1, 100) if n not in used)
    for p in players:
        if p['number'] is None:
            p['number'] = next(free)


# ---- kits ---------------------------------------------------------------------------------------

# the game's kit colours (0..10) as RGB for the nearest-colour test: the mean Wikipedia infobox
# colour of each game colour in the Mod's kits of the sample clubs (fit by hand from compare data;
# the menu palette's own -- src/swos_globals.c -- sends navy to purple and sky blue to grey)
KIT_RGB = [(170, 165, 170), (245, 245, 245), (10, 10, 15), (246, 140, 27), (235, 10, 20), (15, 40, 190),
           (120, 15, 30), (120, 185, 235), (10, 130, 70), (250, 225, 20), (95, 50, 125)]


COLOUR_WORDS = {'white': 1, 'black': 2, 'orange': 3, 'red': 4, 'blue': 5, 'navy': 5, 'maroon': 6, 'claret': 6,
                'sky': 7, 'lightblue': 7, 'green': 8, 'yellow': 9, 'gold': 9, 'amber': 9, 'purple': 10,
                'violet': 10, 'grey': 0, 'gray': 0, 'silver': 0}


def nearest_rgb(r, g, b):
    return min(range(len(KIT_RGB)), key=lambda k: (KIT_RGB[k][0] - r) ** 2 + (KIT_RGB[k][1] - g) ** 2
               + (KIT_RGB[k][2] - b) ** 2)


def nearest_colour(hexrgb):
    return nearest_rgb(*(int(hexrgb[i:i + 2], 16) for i in (1, 3, 5)))


def torso(px, base):
    """(main colour, second colour or None, 2 = vertical stripes / 3 = hoops) of a kit body
    pattern composed over the body colour, read in the torso box below the collar (the Mod: two
    main colours, a second one of >= 20% = stripes, vertical vs hoops by where the colour changes
    run)."""
    br, bg, bb = (int(base[i:i + 2], 16) for i in (1, 3, 5))
    grid = []
    for row in px[16:58]:
        line = []
        for r, g, b, al in row[4:34]:
            f = al / 255
            line.append(nearest_rgb(r * f + br * (1 - f), g * f + bg * (1 - f), b * f + bb * (1 - f)))
        grid.append(line)
    count = {}
    for line in grid:
        for c in line:
            count[c] = count.get(c, 0) + 1
    order = sorted(count, key=lambda c: -count[c])
    total = sum(count.values())
    if len(order) < 2 or count[order[1]] < 0.2 * total:
        return order[0], None, 0
    two = set(order[:2])
    across = sum(1 for line in grid for x in range(1, len(line))
                 if line[x] != line[x - 1] and line[x] in two and line[x - 1] in two)
    down = sum(1 for y in range(1, len(grid)) for x in range(len(grid[0]))
               if grid[y][x] != grid[y - 1][x] and grid[y][x] in two and grid[y - 1][x] in two)
    return order[0], order[1], 2 if across >= down else 3


def kit(k, body_px=None):
    """A Wikipedia infobox kit -> 'type stripes basic shorts socks' (types: 0 plain, 1 coloured
    sleeves, 2 vertical stripes, 3 hoops). Plain: both colours the body's; sleeves: 'stripes' is
    the body and 'basic' the sleeves; stripes / hoops: 'basic' is the infobox body colour and
    'stripes' the pattern's other main colour. body_px: the body pattern image's pixels (Commons),
    without it the pattern's name is read for stripes / hoops and a colour word."""
    if not k.get('body'):
        return None
    body = nearest_colour(k['body'])
    arm = nearest_colour(k.get('leftarm') or k['body'])
    shorts = nearest_colour(k['shorts']) if k.get('shorts') else body
    socks = nearest_colour(k['socks']) if k.get('socks') else shorts
    if body_px:
        main, second, kind = torso(body_px, k['body'])
        if second is not None:
            basic = body if body in (main, second) else main
            other = second if basic == main else main
            return f'{kind} {other} {basic} {shorts} {socks}'
        body = main
    else:
        pat = (k.get('pattern_body') or '').lower()
        second = next((c for w, c in COLOUR_WORDS.items() if w in pat), None)
        if ('stripe' in pat or 'hoop' in pat) and second is not None and second != body:
            return f"{2 if 'stripe' in pat else 3} {second} {body} {shorts} {socks}"
    if arm != body:
        return f'1 {body} {arm} {shorts} {socks}'
    return f'0 {body} {body} {shorts} {socks}'


# ---- club names ---------------------------------------------------------------------------------

LEGAL = {'FC', 'F.C.', 'AFC', 'A.F.C.', 'CF', 'SC', 'AC', 'A.C.', 'SSC', 'SS', 'S.S.', 'US', 'U.S.', 'CFC', 'FK',
         'SK', 'BK', 'IF', 'CD', 'SD', 'UD', 'CA', 'SV', 'VFB', 'VFL', '1.', 'CALCIO', 'FOOTBALL', 'CLUB',
         'ASD', 'SSD', 'SRL', 'S.P.A.', 'SPA', 'ACF', 'U.C.', 'UC', 'BC', 'B.C.', 'F.C', 'A.S.', 'AS'}


def club_name(title):
    """A Wikipedia article title -> the game's club name (<= 16): the disambiguation and the legal
    forms dropped, upper-case ASCII; too long -> its first words that fit."""
    import modport
    t = title.split(' (')[0]
    words = [w for w in t.replace('’', "'").split()
             if w.upper() not in LEGAL and not (w.isdigit() and len(w) == 4)]     # (Como 1907)
    s = modport.swos_name(' '.join(words) or t, 99)
    while len(s) > 16 and ' ' in s:
        s = s.rsplit(' ', 1)[0]
    return s[:16]


# ---- inputs -------------------------------------------------------------------------------------

def sample(path=os.path.join(HERE, 'sample_2627.csv')):
    with open(path, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f))


def load_curves(path=os.path.join(HERE, 'curves.json')):
    if not os.path.exists(path):
        return {}
    with open(path, encoding='utf-8') as f:
        c = json.load(f)
    c['nat_fifa'] = {k: int(v) for k, v in c.get('nat_fifa', {}).items()}
    c['nat_tm'] = {k: int(v) for k, v in c.get('nat_tm', {}).items()}
    return c
