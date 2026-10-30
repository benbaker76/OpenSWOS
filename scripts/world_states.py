"""State championships of a world (Brazil's estaduais): the SWOS 96/97 Mod's tools/statespec.py
(MIT, Davide Lorigliola) -- the parts the season end's simulation reads (statesim.c's SS_TAB:
compile_state, the calendars, the relegation programs, the knockout programs, sim_table), as it
writes them; the team words from the caller (word_of: a club key -> the Mod's word, file |
ordinal << 8), so the same bytes serve our world (scripts/world_import.py) and the check against
the Mod's own (scripts/league_playoffs_check.sh).

A pack's 'state' competition:
  {"key": "pr", "type": "state", "id": "C4", "names": {...}, "win_points": 3, "months": [1, 4],
   "clubs": [club keys in group order], "relegated": n, "relegation": {...},
   "stages": [{"type": "groups", "teams": 12, "groups": 2, "meetings": 1, "cross": 2},
              {"type": "knockout", "teams": 8, "matches": "two_legs", "from": ["4A", "1A", ...]}, ...]}
"""
import csv
import os
import random
import struct

MATCHES = {'single': 0x14, 'single_et': 0x54, 'two_legs': 0x94, 'two_legs_pens': 0x84, 'single_replay': 0x00}


class SpecError(Exception):
    pass


def _need(cond, msg):
    if not cond:
        raise SpecError(msg)


def group_calendar(g, size, meetings=1):
    """A round robin inside each of g groups of size: (calendar [(home, away) club indices], days)."""
    rounds = []
    for x in range(g):
        mem = list(range(x * size, (x + 1) * size)) + ([None] if size % 2 else [])
        m = len(mem)
        days = []
        for r in range(m - 1):
            day = []
            for k in range(m // 2):
                a, b = mem[k], mem[m - 1 - k]
                if a is None or b is None:
                    continue
                day.append((a, b) if (r + k) % 2 == 0 else (b, a))
            days.append(day)
            mem = [mem[0]] + [mem[-1]] + mem[1:-1]
        if meetings == 2:
            days += [[(b, a) for a, b in d] for d in days]
        rounds.append([pr for d in days for pr in d])
    npairs = len(rounds[0])
    cal = [rounds[x][q] for q in range(npairs) for x in range(g)]
    return cal, (size - 1 + size % 2) * meetings


def cross_calendar(n, blocks, seed=2026):
    """n clubs in `blocks` equal blocks (list order): every pair of different blocks once, in rounds where every club
    plays; home sides balanced. [(home, away)] by day."""
    size = n // blocks
    blk = [i // size for i in range(n)]
    pairs = [(i, j) for i in range(n) for j in range(i + 1, n) if blk[i] != blk[j]]
    per = n // 2
    rng = random.Random(seed + n * 31 + blocks)
    days_n = len(pairs) // per
    for attempt in range(2000):
        left = set(pairs)
        days = []
        ok = True
        for d in range(days_n):
            m = _matching(rng, n, left)
            if m is None:
                ok = False
                break
            days.append(m)
            left -= set(m)
        if ok and not left:
            break
    else:
        raise ValueError(f'cross calendar {n} / {blocks}: no rounds found')
    out = [list(m) for day in days for m in day]
    for k, m in enumerate(out):
        if k % 2:
            m.reverse()
    games = [sum(1 for m in out if v in m) for v in range(n)]
    for _ in range(10000):
        home = [sum(1 for m in out if m[0] == v) for v in range(n)]
        bad = [v for v in range(n) if abs(2 * home[v] - games[v]) > 1]
        if not bad:
            break
        v = bad[0]
        hi = 2 * home[v] > games[v]
        cand = [m for m in out if (m[0] if hi else m[1]) == v]
        rng.shuffle(cand)
        o = lambda m: m[1] if hi else m[0]
        m = min(cand, key=lambda m: (2 * home[o(m)] - games[o(m)]) * (1 if hi else -1))
        m.reverse()
    return [tuple(m) for m in out], days_n


def _matching(rng, n, edges):
    """A perfect matching of n vertices from `edges` (DFS on the lowest free vertex), or None."""
    adj = {v: [] for v in range(n)}
    for a, b in edges:
        adj[a].append(b)
        adj[b].append(a)
    for v in adj:
        rng.shuffle(adj[v])
    used, out = set(), []

    def go():
        free = next((v for v in range(n) if v not in used), None)
        if free is None:
            return True
        used.add(free)
        for w in adj[free]:
            if w not in used:
                used.add(w)
                out.append((min(free, w), max(free, w)))
                if go():
                    return True
                out.pop()
                used.discard(w)
        used.discard(free)
        return False
    return out if go() else None


def compile_state(c, n_clubs):
    """A 'state' comp -> {'stages': [(groups, per group, qualifiers)], 'stage_bytes', 'meetings', 'points',
    'months', 'fixture': (groups, per group, days, calendar) or None} (the Mod's, the parts the simulation reads)."""
    w = f"{c['key']}:"
    st = c.get('stages') or []
    _need(2 <= len(st) <= 5, f'{w} 2 to 5 stages (the last one the final)')
    m1, m2 = c.get('months', [1, 6])
    trip, sb, meet, fixture, prev = [], [], None, None, None
    for i, x in enumerate(st):
        t, n = x.get('type'), x.get('teams')
        sw = f'{w} stage {i + 1}:'
        _need(t in ('groups', 'knockout'), f'{sw} type groups or knockout')
        if i == 0:
            _need(n == n_clubs, f'{sw} {n} teams, {n_clubs} clubs')
        if t == 'groups':
            g = x.get('groups')
            mt = x.get('meetings', 1)
            meet = mt
            cr = x.get('cross')
            if cr:
                cal, days = cross_calendar(n, cr)
                fixture = (g, n // g, days, cal)
            if i == 0 and not cr and g > 1 and not x.get('fixtures'):
                cal, days = group_calendar(g, n // g, mt)
                fixture = (g, n // g, days, cal)
            fx = x.get('fixtures')
            if fx:
                fixture = (1, n, len(fx), [(a, b) for day in fx for a, b in day])
            nxt = st[i + 1]['teams'] + len(st[i + 1].get('byes') or []) if i + 1 < len(st) else 1
            trip.append((g, n // g, nxt))
            sb.append(0)
        else:
            b = x.get('byes') or []
            trip.append((0, n + len(b), n // 2 + len(b)))
            sb.append(MATCHES[x['matches']])
        prev = dict(x, teams=n)
    return {'stages': trip, 'stage_bytes': sb, 'meetings': meet or 1, 'points': c['win_points'],
            'months': (8 * (m1 - 1), 8 * (m2 - 1)), 'fixture': fixture}


def _code(label, g, w):
    """'5A' -> code = (place - 1) * g + group."""
    _need(isinstance(label, str) and len(label) >= 2 and label[:-1].isdigit() and 'A' <= label[-1] < chr(65 + g),
          f'{w} relegation: label {label!r} (place + group letter, {g} groups)')
    return (int(label[:-1]) - 1) * g + ord(label[-1]) - 65


def relegation_bytes(c, g, strength_of=None):
    """pack.json 'relegation' -> statesim.c's program (0 bottom, 1 playoffs, 2 group, 3 gap)."""
    w = f"{c['key']}:"
    r = c.get('relegation') or {'type': 'bottom'}
    t = r.get('type')
    if t == 'bottom':
        return bytes([0])
    if t == 'playoffs':
        ties = r.get('ties') or []
        _need(ties and r.get('legs') in (1, 2), f'{w} relegation playoffs: ties + legs 1 / 2')
        return bytes([1, r['legs'], len(ties)] + [_code(x, g, w) for a, b in ties for x in (a, b)])
    if t == 'group':
        mem = [_code(x, g, w) for x in r.get('members') or []]
        _need(2 <= len(mem) <= 8 and r.get('meetings') in (1, 2), f'{w} relegation group: members + meetings')
        bo = r.get('bonus') or {}
        bc = [_code(x, g, w) for x in bo.get('best') or []]
        ex = r.get('extra') or {}
        xs = ex.get('strength', 0)
        if ex.get('club'):
            _need(strength_of is not None, f"{w} relegation extra club {ex['club']!r}")
            xs = strength_of(ex['club'])
        return bytes([2, r['meetings'], len(mem), *mem, len(bc), *bc, bo.get('points', 0), ex.get('place', 0),
                      min(255, xs // 4)])
    if t == 'gap':
        a, b = r.get('places') or (0, 0)
        _need(1 <= a < b, f'{w} relegation gap: places [a, b]')
        return bytes([3, a, b, r.get('gap', 0)])
    _need(False, f'{w} relegation type {t!r}: bottom, playoffs, group or gap')


def ko_program(c):
    """statesim.c's stages after the first: db stages; group: 1, g, size, meetings, cross, q, q x code; knockout: 2,
    N, b, legs, mode (0 codes over the previous order, 1 pairs, 2 the winners in tie order, 3 the merit order;
    0x80 the better placed of stage 1 at home), N x code."""
    w = f"{c['key']}:"
    st = c['stages']
    out = bytearray([len(st) - 1])
    prev = dict(st[0], teams=st[0]['teams'])
    for i, x in enumerate(st[1:], 1):
        n = x['teams']
        if x['type'] == 'groups':
            g2 = x.get('groups', 1)
            gp = prev.get('groups', 1) if prev['type'] == 'groups' else 1
            frm = x.get('from')
            _need(frm and prev['type'] == 'groups', f'{w} statesim: a later group stage needs from')
            labels = [f'{r}{chr(65 + k)}' for r in range(1, -(-n // gp) + 1) for k in range(gp)]
            out += bytes([1, g2, n // g2, x.get('meetings', c.get('meetings', 1)) or 1, int(bool(x.get('cross'))), n]
                         + [labels.index(q) for q in frm])
        else:
            legs = 2 if x.get('matches', '').startswith('two_legs') else 1
            hb = 0x80 if x.get('host') == 'better' else 0
            b = x.get('byes') or []
            q = n + len(b)
            if prev['type'] == 'groups' and not x.get('from'):
                pm = list(range(q))
                if x.get('pairing') == 'merit':
                    pm = [v for k_ in range(q // 2) for v in (k_, q - 1 - k_)]
                out += bytes([2, q, len(b), legs, 3 | hb] + pm)
            elif prev['type'] == 'groups':
                gp = prev.get('groups', 1)
                labels = [f'{r}{chr(65 + k)}' for r in range(1, -(-q // gp) + 1) for k in range(gp)]
                frm = x['from'] + b
                out += bytes([2, q, len(b), legs, 0 | hb] + [labels.index(t) for t in frm])
            elif x.get('pairs'):
                k, bb = prev['teams'] // 2, len(prev.get('byes') or [])
                toks = [f'W{j + 1}' for j in range(k)] + [f'B{j + 1}' for j in range(bb)]
                out += bytes([2, n, 0, legs, 1 | hb] + [toks.index(t) for t in x['pairs']])
            else:
                out += bytes([2, n, 0, legs, 2 | hb] + list(range(n)))
        prev = dict(x, teams=n)
    return bytes(out)


def strengths(pack_dir):
    """club key -> its best 11 players' prices (players.csv), at least 5 (the Mod's club_strength)."""
    rows = {}
    for r in csv.DictReader(open(os.path.join(pack_dir, 'players.csv'), encoding='utf-8')):
        rows.setdefault(r['club'], []).append(int(r.get('price') or 0))
    return lambda key: max(5, min(0xFFFF, sum(sorted(rows.get(key, []), reverse=True)[:11])))


SIM_WORDS = []                          # (each state's words' offset in the last sim_table: the overrides')


def sim_table(states, word_of, strength_of):
    """statesim.c's SS_TAB bytes for a country's states (its pack's 'state' comps, pack order); word_of(key) -> a
    club's team word, strength_of(key) -> its strength."""
    out = bytearray()
    ns = 0
    SIM_WORDS.clear()
    for c in states:
        clubs = c.get('clubs') or []
        sp = compile_state(c, len(clubs))
        n = len(clubs)
        _need(n <= 16, f"{c['key']}: statesim takes up to 16 clubs")
        g0, size0, _ = sp['stages'][0]
        g = g0 if not c['stages'][0].get('cross') or g0 > 1 else 1
        size = size0 if g > 1 else n
        if sp['fixture']:
            pairs = list(sp['fixture'][3])
        else:
            pairs = []
            for r in range(sp['meetings']):
                for x in range(g0):
                    mem = list(range(x * size0, (x + 1) * size0))
                    for i in range(len(mem)):
                        for j in range(i + 1, len(mem)):
                            pairs.append((mem[j], mem[i]) if r & 1 else (mem[i], mem[j]))
        words = [word_of(k) for k in clubs]
        strs = [strength_of(k) for k in clubs]
        SIM_WORDS.append(1 + len(out) + 7)     # (its words' offset in the table: the overrides', the Mod's SIM_LAYOUT)
        out += struct.pack('<HBBBBB', words[0], n, g, size, c.get('relegated', 0), min(255, sp['stages'][0][2]))
        out += struct.pack(f'<{n}H', *words) + struct.pack(f'<{n}H', *strs)
        out += struct.pack('<H', len(pairs)) + bytes(x for p in pairs for x in p)
        out += relegation_bytes(c, g, strength_of)
        out += ko_program(c)
        ns += 1
    return bytes([ns]) + bytes(out) if ns else b''


# --- the state pools (the Mod's Pack.off_pool, its state branch, and ripool.table_bytes): gplay.c ri_season's table
RF, RP, RQT, RO, RMT = 8, 20, 192, 16, 640       # (gplay.c: files, pools, queue words, a contest's entries, map words)


def state_pools(states, where, pool_file, base, pool_index, price_of):
    """[('state', key, relegated, [(ordinal, global, strength)] its own clubs of the states' file, [strength] of its
    pool records in its first order, [pool index] its first order, [team word] the contest's entries)]: where(key) ->
    (file, ordinal), pool_index(key) -> its index among the pool's clubs, price_of(key) -> its 16 players' prices."""
    pools = []
    for c in states:
        if not (c.get('reserve') or {}).get('off_file'):
            continue
        f0, o0 = where(c['clubs'][0])
        own = [k for k in c['clubs'] if where(k)[0] == pool_file]
        order = list(c['reserve']['pool'])
        raw = [price_of(k) for k in own] + [price_of(k) for k in order]
        top = max(raw) or 1
        sc = [max(1, min(255, x * 255 // top)) for x in raw]
        pools.append(('state', f0 | o0 << 8, c['relegated'],
                      [(where(k)[1], base + where(k)[1], s) for k, s in zip(own, sc)],
                      sc[len(own):], [pool_index(k) for k in order],
                      [where(k)[0] | where(k)[1] << 8 for k in c['clubs']]))
    return pools


def pool_table(tabs):
    """gplay.c ri_season's table (ripool.table_bytes): db files; per file db world, file, pools, dw base, dw map
    offset, dw map words, db spares, spares x db ordinal; per pool (kind 1, a state): db 1, n; dw key; db relegated;
    db slots; slots x (db ordinal, dw global, db strength); n x db strength; db entries; entries x dw the contest's team
    words; then n x db the pool's first order. tabs: [(world, file, pools, base, [spare ordinals], records, pool
    records)]."""
    assert len(tabs) <= RF
    out = bytearray([len(tabs)])
    moff = npools = nq = 0
    for world, fl, pools, base, spares, nrec, npool in tabs:
        out += bytes((world, fl, len(pools))) + struct.pack('<HHHB', base, moff, nrec, len(spares)) + bytes(spares)
        moff += nrec
        for pl in pools:
            _, key, rel, slots, strength, order, entries = pl
            assert len(slots) <= 255 and len(strength) == len(order) <= 255 and len(entries) <= RO
            out += bytes((1, len(order))) + struct.pack('<HBB', key, rel, len(slots))
            for o, g, s in slots:
                out += struct.pack('<BHB', o, g, s)
            out += bytes(strength) + bytes((len(entries),)) + b''.join(struct.pack('<H', w) for w in entries)
            out += bytes(order)
            npools += 1
            nq += len(order)
    assert moff <= RMT and npools <= RP and nq <= RQT, (moff, npools, nq)
    return bytes(out)
