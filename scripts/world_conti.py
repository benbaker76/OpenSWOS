"""world_conti.py -- the other confederations' club cups of World 2026-27, their spec bytes
(src/world_conti.c's CT_SPEC; docs/WORLD_2026_27.md).

The SWOS 96/97 Mod's tools/conti.py load() and the helpers it uses, copied as they are (MIT,
Davide Lorigliola; its notice in src/world_conti.c), reading the world's copy of its data
(bin/data_json/worlds/2627/mod/conti); its guest clubs (associations outside the world) read
here as its tools/guests.py reads them. scripts/league_playoffs_check.sh compares the bytes
with the Mod's own load().
"""
import json
import os
import struct

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bin", "data_json", "worlds", "2627",
                    "mod", "conti")


def _read(conf, kind):
    p = os.path.join(DATA, f'{conf}.{kind}.json')
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None


class _Guests:
    """The Mod's guests.py, as load() uses it: a confederation's guest clubs (one team file each)."""
    FILES = {'caf': 191, 'afc': 192, 'ofc': 193, 'concacaf': 194}

    @staticmethod
    def load(conf):
        return _read(conf, 'guests')

    @staticmethod
    def record(conf, i, c, base):
        """(only the players' prices: the strength load() sums from a record)"""
        r = bytearray(684)
        for j, p in enumerate(c['players'][:16]):
            r[76 + 38 * j + 32] = int(p['price']) & 255
        return bytes(r)


CONFS = ['conmebol', 'concacaf', 'afc', 'caf', 'ofc', 'fifa']
IDS = [0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD0, 0xD1, 0xDA, 0xDD, 0xDE, 0xDF, 0xEC, 0xED, 0xEE, 0xEF]   # (mkseason.RESERVED_IDS)
RB = 24
OUT, CHAMP = 0xFF, 0xFE
K = {'ko': 0, 'groups': 1, 'league_stage': 1}   # (a league stage: simulated as groups, one region a group)
DRAW = {'order': 0, 'pots2': 1, 'same_assoc_pairs': 2, 'seeded_1v2': 3, 'bracket': 4, 'groups': 5, 'fixed': 6, 'slot': 7,
        'fixed_by_place': 8}
ASSOC = {'allow': 0, 'any': 0, 'no': 1, 'no_unless_via': 2, 'allow_if_via': 2}
LEVEL = {'pens': 0, 'et_pens': 1, 'away_pens': 2, 'away_et_pens': 3}   # (3, 4.0.2: CONCACAF - away goals, then
AWAY = ('away_pens', 'away_et_pens')                                     # extra time, then penalties)


def level(rd):
    """A round's level: 'away_pens' with "extra_time": true in the data is 'away_et_pens' (4.0.2; the data keeps
    'away_pens' so older code reading the shared internal/conti files plays it as before)."""
    lv = rd.get('level', 'pens')
    return 'away_et_pens' if lv == 'away_pens' and rd.get('extra_time') else lv
CODE = {'cup': 1, 'next': 0x40, 'ap': 0x41, 'cl': 0x42}
ALIAS = {'r1': 'first', 'playoffs': 'po'}   # (access files' round names -> the format's)


def _draw(rd):
    """(draw byte, association rule byte) of a round's 'draw' value."""
    d = rd.get('draw')
    if rd['kind'] in ('groups', 'league_stage'):
        return DRAW['groups'], ASSOC.get((d or {}).get('same_assoc', 'no'), 1)
    if isinstance(d, dict) and d.get('kind') == 'fixed_by_place':
        return DRAW['fixed_by_place'], 0
    if isinstance(d, str):
        return DRAW.get(d, DRAW['order']) if d != 'fixed' else DRAW['order'], 0
    if not d:
        return DRAW['order'], 0
    kind = d.get('kind')
    rule = ASSOC.get(d.get('same_assoc', 'allow'), 0)
    if kind == 'same_assoc_pairs':
        return DRAW['same_assoc_pairs'], 0
    if kind == 'seeded_1v2':
        return DRAW['seeded_1v2'], rule
    if kind == 'fixed' and d.get('ties'):
        return DRAW['fixed'], 0
    if kind in ('bracket',):
        return DRAW['bracket'], 0
    if d.get('pots') == 2 or kind in ('pots2',):
        return DRAW['pots2'], rule
    return DRAW['order'], rule


def main_round(c, size=None):
    """The round a career club plays as the game's own contest (slot 3): the biggest group stage (OFC: the groups, not
    the qualifying group), else the first knockout round whose clubs are a power of two of the rounds left (CONCACAF:
    the round of 16, its first round of 22 a tie before it; size: load()'s round sizes, else FIELDS)."""
    _norm(c)
    lg = ('groups', 'league_stage') if os.environ.get('CONTI_LEAGUE_PLAYED') else ('groups',)   # (the played league
    g = [(r['groups'] * r['size'], -j, j) for j, r in enumerate(c['rounds']) if r['kind'] in lg   # stage: WIP)
         and not r.get('best') and not any(str(t).endswith('_best') for t in (r.get('out') or {}).values())]
    # (a group stage whose best runners-up go on: not the game's contest, simulated - the ACGL from its quarters)
    if g:
        return max(g)[2]
    size = size or next((v[0] for (cf, ck), v in FIELDS.items() if ck == c['key']), None)
    n = len(c['rounds'])
    if size:
        for j in range(n):                              # (a knockout round only: ACLE's league stage of 32 is not)
            if c['rounds'][j]['kind'] == 'ko' and size[j] == 2 ** (n - j):
                return j
    return 0


S1_HOLD = {}                            # (conf, cup key) -> the 2027 edition's winner: ours stays empty, the
                                        # game runs season 1 at the first season end (world_conti.c's caller)
FIELDS = {}                             # (conf, cup key) -> (round sizes, outside places, known places): load()'s


def supported(c):
    """The engine plays every round of the cup: knockouts and groups whose places go to a round of the cups."""
    keys = {r['key'] for r in c['rounds']}
    return all(r['kind'] in K for r in c['rounds']) and all(
        ':' in t or t.replace('_best', '') in keys for r in c['rounds'] for t in (r.get('out') or {}).values())


def fields(c, known, outside):
    """A cup's round sizes from the top (final 2, a group stage its own size): -> ([field per round], [outside places per
    round]). known[j] = the places of round j filled by world clubs (access, holders, losers of another cup);
    outside = the access file's 'outside' ({round key: count} or 'rest'): places of associations / cups not in the
    world, filled by the confederation's world associations' next clubs (declared). 'rest' fills what the sizes leave:
    above a group stage its gap, else the lowest round (data can give exact counts instead)."""
    rounds = c['rounds']
    n = len(rounds)
    key = {r['key']: j for j, r in enumerate(rounds)}
    rk_ = lambda rk: rk if rk in key else ALIAS.get(rk, rk)
    out = [0] * n
    if isinstance(outside, dict):
        for rk, k in outside.items():
            out[key[rk_(rk)]] = k
    feed = [0] * n                                      # (a group stage's places into a later round of the cup)
    best = {}
    for r in rounds:
        if r['kind'] in ('groups', 'league_stage'):
            for q, t in (r.get('out') or {}).items():
                if ':' not in t and str(r.get('best')) != str(q):
                    feed[key[t]] += r['groups'] * len(_places(q))
                elif ':' not in t:
                    best[key[t]] = r['groups']
    field = [0] * n
    for j in range(n - 1, -1, -1):
        r = rounds[j]
        if r['kind'] in ('groups', 'league_stage'):
            field[j] = r.get('clubs') or r['groups'] * r['size']
        elif j == n - 1:
            field[j] = 2
        else:
            field[j] = 2 * (field[j + 1] - known[j + 1] - out[j + 1] - feed[j + 1])
        if j in best:                                   # (the best runners-up: the places left, at most a group's)
            feed[j] += min(best[j], max(0, field[j] - known[j] - out[j] - feed[j]))
        if j < n - 1 and r['kind'] in ('groups', 'league_stage'):
            gap = field[j + 1] - known[j + 1] - out[j + 1] - feed[j + 1]
            if gap and outside == 'rest':
                out[j + 1] += gap
            elif gap:
                print(f"conti: {c['key']}:{rounds[j + 1]['key']} {gap} places left by the sizes")
    if outside == 'rest':
        out[0] += field[0] - known[0] - out[0] - feed[0]
    return field, out


def _norm(c):
    """A league stage as groups: its regions as groups of size clubs, one round (each pair once: the real 8 matches a
    club, declared), its 'out' ranges ('1-8') kept for the top-n routing."""
    for r in c['rounds']:
        for q, t in list((r.get('out') or {}).items()):  # ('qf_best': only that place's best clubs across the groups,
            if t.endswith('_best'):                      # as many as the next round has places left: ACGL's 3 best 2nds)
                r['out'][q] = t[:-5]
                r['best'] = int(q)
        if r['kind'] == 'league_stage':
            r.setdefault('groups', r.get('regions') or 2)
            r.setdefault('size', 16)
            r.setdefault('rounds', 1)
    return c


def _places(key):
    """'3' -> [3]; '1-8' -> [1..8]."""
    a, _, b = str(key).partition('-')
    return list(range(int(a), int(b or a) + 1))


IC_KEYS = {'cafcl': ('caf', 'cl')}       # (the Intercontinental's access file: holders by cup key)


def _cup_index(index, key):
    if key in IC_KEYS and IC_KEYS[key] in index:
        return index[IC_KEYS[key]]
    hits = [i for (cf, ck), i in index.items() if ck == key]
    return hits[0] if len(hits) == 1 else None


def _ic_holders(a, entries):
    """The Intercontinental's places: [(holder's cup key, round, slot, parity flag)]. Slots order the stepladder's
    fixed ties (draw 'slot'): r1 both 1 (its winner keeps 1), the African-Asian-Pacific tie = slots 1, 2, the
    Derby of the Americas = 3, 4, the final: the play-off winner v the UEFA holder (5). The AFC / CAF rotation:
    even editions as the access file's holders, odd ones its 'rotation'.odd (0x40 even only, 0x20 odd only)."""
    rot = a.get('rotation') or {}
    rotated = {k for side in rot.values() for ks in side.values() for k in ks}
    out = []
    for par, flag in (('even', 0x40), ('odd', 0x20)):
        for rk, keys in (rot.get(par) or {}).items():
            for k in keys:
                out.append((k, rk, 1 if rk == 'r1' else 2, flag))
    slots = {'lib': 3, 'ccc': 4, 'uefa:cl': 5}
    for k, rk in entries.items():
        if k not in rotated:
            out.append((k, rk, slots.get(k, 2), 0))
    return out


def load(confs, files=None, words=None, s1=None):
    """-> (spec bytes, [association pack keys], [(conf, cup key)]). files: {association key: its league team file}
    (the game's build; the tests: 100 + index), words: {club key: team word} (the ranking's clubs)."""
    cups, assocs = [], []
    for conf in confs:
        fm, ac = _read(conf, 'format'), _read(conf, 'access')
        assert fm, f'{conf}: no format file'
        acc = {c['key']: c for c in (ac or {}).get('cups', [])}
        for c in fm['cups']:
            _norm(c)
            if not supported(c):
                print(f"conti: {conf}:{c['key']} not built (a round kind / route the engine lacks)")
                continue
            cups.append((conf, c, acc.get(c['key'], {})))
            for a in acc.get(c['key'], {}).get('access', {}):
                if a not in ('holders', 'from', 'outside') and a not in assocs:
                    assocs.append(a)
    gmod = _Guests
    gl, gin = [], {}                                    # the guests: (word, association index, strength)
    for conf in confs:
        g = gmod.load(conf)
        for i, c in enumerate((g or {}).get('clubs', [])):
            ak = 'g:' + c['assoc']
            if ak not in assocs:
                assocs.append(ak)
            rec = gmod.record(conf, i, c, 0)
            gin[(conf, c['key'])] = len(gl)
            gl.append((gmod.FILES[conf] | i << 8, assocs.index(ak), sum(rec[76 + 38 * j + 32] for j in range(16))))
    gs1 = {conf: (gmod.load(conf) or {}).get('season1', {}) for conf in confs}
    index = {(conf, c['key']): i for i, (conf, c, _) in enumerate(cups)}
    xlosers = {}                                        # (an access file's 'from': {'acl2:pre_losers': 'groups'}: that
    for conf, c, a in cups:                             # round's losers into this cup's round, as a format 'losers')
        for src, rk in ((a.get('access') or {}).get('from') or {}).items():
            sc, _, what = src.partition(':')
            if what.endswith('_losers'):
                xlosers[(conf, sc, what[:-7])] = f"{c['key']}:{rk}"
    for conf, c, a in cups:                             # (the short group stages: a 'clubs' count below the groups')
        for rk, n in (a.get('clubs') or {}).items():
            for r in c['rounds']:
                if r['key'] == rk:
                    r['clubs'] = n
    rkey = {i: {r['key']: j for j, r in enumerate(c['rounds'])} for i, (_, c, _) in enumerate(cups)}

    def target(conf, ci, s):
        """'r16' / 'sud:po' / 'sud:groups' -> cup << 4 | round."""
        if ':' in s:
            ck, rk = s.split(':')
            cj = index[(conf, ck)]
            return cj << 4 | rkey[cj][rk]
        return ci << 4 | rkey[ci][s]

    body = b''
    gids = []
    access, holders = [], []
    incoming = {}                                       # (cup, round) -> places from another cup's losers
    for ci, (conf, c, a) in enumerate(cups):
        rounds = c['rounds']
        known = [incoming.get((ci, j), 0) for j in range(len(rounds))]
        for ak, entries in (a.get('access') or {}).items():
            if ak == 'holders':
                for rk in entries.values():
                    known[rkey[ci][rk]] += 1
            elif ak not in ('from', 'outside'):
                for rk, _ in entries:
                    known[rkey[ci][ALIAS.get(rk, rk) if rk not in rkey[ci] else rk]] += 1
        gr = gs1.get(conf, {}).get(c['key']) or {}     # the guests' real places: they ARE the outside places
        for rk, keys in gr.items():
            known[rkey[ci][rk if rk in rkey[ci] else ALIAS.get(rk, rk)]] += len([k for k in keys if (conf, k) in gin])
        size, outside = fields(c, known, None if gr else (a.get('access') or {}).get('outside'))
        for j, r in enumerate(rounds):                  # (data's lists must close the sizes: a report, not a guess)
            feed = sum(rr['groups'] * len(_places(q)) for rr in rounds if rr['kind'] in ('groups', 'league_stage')
                       for q, t in (rr.get('out') or {}).items() if t == r['key'] and str(rr.get('best')) != str(q))
            win = size[j - 1] // 2 if j and rounds[j - 1]['kind'] == 'ko' else 0
            have = known[j] + outside[j] + feed + win
            for rr in rounds:                           # (the best runners-up: the places left, at most a group's)
                if rr.get('best') and rr['out'].get(str(rr['best'])) == r['key']:
                    have += min(rr['groups'], max(0, size[j] - have))
            if gr and have != size[j]:
                print(f"conti: {conf}:{c['key']}:{r['key']} {have} clubs for {size[j]} places (data's lists)")
        FIELDS[(conf, c['key'])] = size, outside, known
        for r in rounds:                                # (a group place into another cup's round)
            for t in (r.get('out') or {}).values() if r['kind'] in ('groups', 'league_stage') else ():
                if ':' in t and (conf, t.split(':')[0]) in index:
                    cj = index[(conf, t.split(':')[0])]
                    incoming[(cj, rkey[cj][t.split(':')[1]])] = incoming.get((cj, rkey[cj][t.split(':')[1]]), 0) + r['groups']
        lmap = dict(c.get('losers') or {})
        lmap.update({k[2]: v for k, v in xlosers.items() if k[:2] == (conf, c['key'])})
        for rk, t in lmap.items():
            if ':' in t:
                ck, tr = t.split(':')
                if (conf, ck) in index:
                    cj = index[(conf, ck)]
                    incoming[(cj, rkey[cj][tr])] = incoming.get((cj, rkey[cj][tr]), 0) + size[rkey[ci][rk]] // 2
        out = b''
        for j, rd in enumerate(rounds):
            dr, rule = _draw(rd)
            b = bytearray(RB)
            b[0] = K[rd['kind']]
            b[1] = rd.get('legs', 2 if rd['kind'] == 'ko' else 0)
            b[2] = LEVEL.get(level(rd), 0)
            b[3] = 1 if rd.get('host') in ('neutral', 'centralised') else 0
            b[4], b[5] = dr, rule
            if c.get('stepladder'):                     # (the Intercontinental: each club in its slot)
                b[4] = DRAW['slot']
            if rd['kind'] in ('groups', 'league_stage'):
                b[6], b[7], b[8] = rd['groups'], rd['size'], rd.get('rounds', 2)
                b[16] = 1 if (rd.get('draw') or {}).get('holder') else 0
            b[9] = rd.get('month') or (rd.get('months') or [8])[0]
            b[17] = 1 if j == main_round(c, size) and not c.get('stepladder') else 0   # (the Intercontinental: no
                                                # contest of its own, every game a tie; its final as 'main' loaded
                                                # ct_structs past the end: the trap after the Challenger Cup)
            b[18] = min(size[j], 255)                   # (the round's places: byes when the world cannot fill it)
            b[10] = CHAMP if j == len(rounds) - 1 else (ci << 4 | (j + 1))
            b[11] = OUT
            for q in range(4):
                b[12 + q] = OUT
            if rd['kind'] in ('groups', 'league_stage'):
                b[10] = OUT
                for q, t in (rd.get('out') or {}).items():
                    ps = _places(q)
                    if len(ps) > 1:                     # (a range: the top n into one round)
                        assert ps[0] == 1, (c['key'], q)
                        b[19], b[20] = len(ps), target(conf, ci, t)
                    else:
                        b[12 + ps[0] - 1] = target(conf, ci, t)
                if rd.get('best'):                      # (the best of that place: as many as the target has left)
                    tj = rkey[ci][rd['out'][str(rd['best'])]]
                    b[21] = rd['best']
                    b[22] = size[tj] - rd['groups'] * len([q for q in rd['out'] if q != str(rd['best'])]) - known[tj]
            lt = (c.get('losers') or {}).get(rd['key']) or xlosers.get((conf, c['key'], rd['key']))
            if lt:
                b[11] = target(conf, ci, lt)
            if rd.get('clubs'):                         # (a short group stage: no fill to its places)
                b[23] = 1
            out += bytes(b)
        body += bytes((len(rounds), 0, 0, 0)) + out
        mj = main_round(c, size)                        # (the exe's group contest of this cup: conti's group-end hook)
        gids.append(IDS[ci] if not c.get('stepladder') and rounds[mj]['kind'] in ('groups', 'league_stage')
                    and ci < len(IDS) else 0)
        if s1 is not None:                              # season 1 (the build): data's real entrants as fixed clubs
            for (cf, ck, rk), ws in s1.items():
                if (cf, ck) != (conf, c['key']):
                    continue
                for w, ak in ws:
                    if ak not in assocs:
                        assocs.append(ak)
                    gin[('s1', w)] = gin.get(('s1', w), len(gl))
                    if gin[('s1', w)] == len(gl):
                        gl.append((w, assocs.index(ak), 1))
                    access.append((0xFE, ci, rkey[ci][rk if rk in rkey[ci] else ALIAS.get(rk, rk)], gin[('s1', w)]))
        for ak, entries in ({} if s1 is not None else (a.get('access') or {})).items():
            if ak == 'holders' and c.get('stepladder'):  # the Intercontinental: the confederations' holders by slot
                for src, rk, slot, par in _ic_holders(a, entries):
                    si = 0xFF if src == 'uefa:cl' else _cup_index(index, src)
                    if si is not None:
                        holders.append((si, ci, rkey[ci][rk] | par, slot))
                continue
            if ak == 'holders':                         # (CAF: a holder qualified through its league keeps that
                own = a.get('holder_rule', 'own_place' if conf == 'caf' else 'extra') == 'own_place'   # one place)
                for src, rk in entries.items():
                    if (conf, src) in index:
                        holders.append((index[(conf, src)], ci, rkey[ci][rk] | (0x80 if own else 0), 0))
                continue
            if ak == 'from':
                continue
            if ak == 'outside':
                continue
            for rk, code in entries:
                rk = ALIAS.get(rk, rk) if rk not in rkey[ci] else rk
                k = CODE[code] if isinstance(code, str) else (0 if code <= 1 else code)
                access.append((assocs.index(ak), ci, rkey[ci][rk], k))
        for rk, keys in (gs1.get(conf, {}).get(c['key']) or {}).items():   # the guests' places (data's real list)
            j = rkey[ci][rk if rk in rkey[ci] else ALIAS.get(rk, rk)]
            for gk in keys:
                if (conf, gk) in gin and len(gin) < 255:
                    access.append((0xFE, ci, j, gin[(conf, gk)]))
        mine = [x for x in (a.get('access') or {}) if x not in ('holders', 'from', 'outside')]
        q = 0
        for j, k in enumerate(outside if s1 is None else ()):                 # the outside places left: the world associations' next clubs
            for _ in range(k):
                access.append((assocs.index(mine[q % len(mine)]), ci, j, CODE['next']))
                q += 1
    force = (0xFF, 0xFF)                                # CONTI_FORCE=<cup>:<round> (test builds only): the
    if os.environ.get('CONTI_FORCE'):                   # career club enters that round at every season end
        fk, fr = os.environ['CONTI_FORCE'].split(':')
        fc = next((i for i, (_, c, _) in enumerate(cups) if c['key'] == fk), None)
        if fc is not None:                              # (season1_native loads one confederation at a time)
            force = (fc, rkey[fc][fr])
            print(f'exe: CONTI_FORCE={fk}:{fr} (test build)')
    spec = bytes((len(cups), len(assocs)) + force) + body
    spec += struct.pack('<H', len(access)) + b''.join(bytes(x) for x in access)
    spec += bytes((len(holders),)) + b''.join(bytes(x) for x in holders)     # (src, dst, round | flags, slot)
    spec += bytes(0 if a.startswith('g:') else (files or {}).get(a, 100 + i) & 0xFF for i, a in enumerate(assocs))
    rank = {}
    for conf in confs:
        for k, v in ((_read(conf, 'access') or {}).get('ranking') or {}).items():
            if words and k in words:
                rank[words[k]] = min(int(round(v)), 0xFFFF)
    spec += struct.pack('<H', len(rank)) + b''.join(struct.pack('<HH', w, v) for w, v in sorted(rank.items()))
    spec += struct.pack('<H', len(gl)) + b''.join(struct.pack('<HBBI', w, a, 0, st) for w, a, st in gl)
    reg = {}                                            # (a league stage's regions: data's <conf>.<cup>_league.json)
    for conf in confs:
        for c in (_read(conf, 'format') or {}).get('cups', []):
            lg = _read(conf, c['key'] + '_league')
            for ri, (rn, rd) in enumerate((lg or {}).get('regions', {}).items()):
                for pot in rd.get('pots', []):
                    for e in pot:
                        a = e.get('assoc', '').lower()
                        for ak in (a, 'g:' + e.get('assoc', '')):
                            if ak in assocs:
                                reg.setdefault(ak, ri)
    spec += bytes(reg.get(a, 0xFF) for a in assocs)
    wf = bytearray(32)                                  # (the world's team files: a UEFA holder outside it - the
    for f in (files or {}).values():                    # game's own 1996-97 Champions Cup holder - is no finalist)
        wf[f >> 3] |= 1 << (f & 7)
    spec += bytes(wf)
    for conf, c, _ in cups:                             # (each cup's season-1 holder: season1_native's run)
        w, an, st = S1_HOLD.get((conf, c['key']), (0, None, 0))
        spec += struct.pack('<HBBI', w, assocs.index(an) if an in assocs else 0xFE, 0, st)
    spec += bytes(gids)
    gp = int(os.environ.get('CONTI_GPLACE', '0'))       # CONTI_GPLACE=n (test builds only): the career club's place
    if gp:                                              # at every group stage's end is n (the 3rd's move, forced)
        print(f'exe: CONTI_GPLACE={gp} (test build)')
    spec += bytes((gp,))
    return spec, assocs, [(conf, c['key']) for conf, c, _ in cups]


# --- native test ------------------------------------------------------------------------------------------------------
