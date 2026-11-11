"""world_mod_leagues.py -- World 2026-27's per-country skeleton and leagues, decoded from an installed SWOS 96/97
Mod (its patched ENGLISH.EXE + DATA): docs/WORLD_2026_27.md 6d, the 'leagues' area of the decoders
(scripts/world_mod_*.py, on scripts/world_mod_exe.py).

decode(world) -> {"packs": {CODE: pack.json fields}, "clubs": {CODE: {key: {division, second_of, area}}}}

What it reads, all found through pointers (fixups) and code, never by a fixed offset (4.1.1's layout differs):
- the world's country namespace (the Mod's tools/mkseason.py ns_emit + careerworld.py NSLIST / NSWT, MIT, Davide
  Lorigliola): the list of country numbers, a row block per world of (countriesTable entry, competitionsTable
  entry, teamsCountryNumbers word), obj-tagged offsets (bit 30 obj1, bit 31 obj2);
- per country (mkseason._patch_pack): the country record (continent byte, button name), and the competitions
  table [league, -2, (first state), (cup), (league cup), -1]; the competitions' structs lie in the pack's order
  right before that table: the league struct (mkseason._league_body: header, 6 bytes a division, names),
  cups (a 14-byte header + rounds, names) and state contests (statecup.contest_bytes: names at +27h);
- the names: string offsets from STR_BASE (obj2, sacups.STR_BASE: the base of `sub eax, imm32`);
- pgroups' PG_TAB (parallel groups + each club's area bytes), ripool's RI_TAB (off-file pools: the league's
  reserve order, Brazil's states' pools and spare clubs) and IDA_TAB (the pool identities' areas), gplay's SEC_TAB
  (B teams), GP_PROM_TAB (Argentina's promedio history), split's SP_TAB (a division's own meetings), benchnum's
  BN_TAB (worlds with real squad numbers); DATA\\BAGE.<world> (4.0.6+, bteam.py: B teams and their parents).
Not in the installed files (so not decoded): the original long / short names where the Mod's complist.fit_titles
shortened them (the exe holds the fitted ones); whether a one-division league was written with a 'divisions' list
(the same struct either way: it comes back without one); a division's 'real_down' (only in the FORMAT page's prose,
DATA\\FMTxx5.DAT; the importer does not read it); before BAGE (4.0.5) Australia's B teams and the parents of New
Zealand's (another country's clubs: SEC_TAB says 0FFFFh, written here '?'). A Brazilian state's key is a label
only: its postal code, from the championship's name (BR_STATES).
Standard library only. The table layouts are the Mod's tools' (MIT, Davide Lorigliola): mkseason.py, pgroups.py,
ripool.py, gplay.py (second_table, promedio_table), split.py (entry), benchnum.py, bteam.py.
"""
import os
import re
import struct

WORLD = 5                                       # World 2026-27's world number (its packs' 'world')
CONTINENTS = ['europe', 'north_america', 'south_america', 'asia', 'oceania', 'africa']   # countries.CONTINENT
LETTERS = 'ABCDEFGHI'
# A Brazilian state championship's key (pack.json competition key) is a label only (no byte of the exe holds it):
# the state's postal code, from the championship's own name (its gentilic).
BR_STATES = {'ACREANO': 'ac', 'ALAGOANO': 'al', 'AMAPAENSE': 'ap', 'AMAZONENSE': 'am', 'BAIANO': 'ba',
             'CEARENSE': 'ce', 'BRASILIENSE': 'df', 'CAPIXABA': 'es', 'GOIANO': 'go', 'MARANHENSE': 'ma',
             'MATO-GROSSENSE': 'mt', 'SUL-MATO-GROSSENSE': 'ms', 'MINEIRO': 'mg', 'PARAENSE': 'pa',
             'PARAIBANO': 'pb', 'PARANAENSE': 'pr', 'PERNAMBUCANO': 'pe', 'PIAUIENSE': 'pi', 'CARIOCA': 'rj',
             'POTIGUAR': 'rn', 'GAUCHO': 'rs', 'RONDONIENSE': 'ro', 'RORAIMENSE': 'rr', 'CATARINENSE': 'sc',
             'PAULISTA': 'sp', 'SERGIPANO': 'se', 'TOCANTINENSE': 'to'}


# ---- helpers ---------------------------------------------------------------------------------------------------

def _tagged(e, v):
    """An ns row's obj-tagged offset -> (obj, off), or None."""
    if not v:
        return None
    return (1 if v & 1 << 30 else 2, v & ((1 << 30) - 1))


def find_ns(e):
    """(numbers, {world: {number: (ct, comp, tcn)}}): mkseason.ns_emit's rows. NSWT (a dd per world number, the
    rows' address or 0) follows NSLIST (the numbers, ascending) aligned to 4; the rows of the worlds are 10 bytes
    a number."""
    d1 = e.obj[1]
    for (so, soff), (to, toff) in sorted(e.ptr.items()):
        if so != 1 or to != 1:
            continue
        nxt = e.ptr.get((1, soff + 4))
        if not nxt or nxt[0] != 1 or nxt[1] <= toff or (nxt[1] - toff) % 10:
            continue
        n = (nxt[1] - toff) // 10
        if not 8 <= n <= 255:
            continue
        # NSWT starts at some soff - 4k (worlds without rows: 0); NSLIST ends right before NSWT (aligned)
        k = 0
        while soff - 4 * (k + 1) >= 0 and e.ptr.get((1, soff - 4 * (k + 1))) is None and \
                d1[soff - 4 * (k + 1):soff - 4 * k] == bytes(4):
            k += 1
        for kk in range(k + 1):
            wt = soff - 4 * kk
            for pad in range(4):
                la = wt - pad - n
                if la < 0 or (la + n + 3) & ~3 != wt:
                    continue
                nums = list(d1[la:la + n])
                if all(a < b for a, b in zip(nums, nums[1:])) and nums[0] > 0:
                    rows = {}
                    w = 0
                    while True:
                        t = e.ptr.get((1, wt + 4 * w))
                        if t is None and d1[wt + 4 * w:wt + 4 * w + 4] != bytes(4):
                            break
                        if t is not None:
                            if t[0] != 1:
                                break
                            rows[w] = {num: struct.unpack_from('<IIH', d1, t[1] + 10 * i) for i, num in enumerate(nums)}
                        w += 1
                        if w > 60:
                            break
                    if len(rows) >= 1 and WORLD in rows:
                        return nums, rows
    raise ValueError('no country namespace (NSLIST / NSWT) found')


def str_base(e, offs):
    """sacups.STR_BASE: the obj2 offset contest names are relative to. Candidates: every `sub eax, imm32` (2D) of
    obj1; the one that makes every name offset of offs a string start wins."""
    d1, d2 = e.obj[1], e.obj[2]
    cands = {}
    for m in re.finditer(rb'\x2d(....)', d1, re.S):
        v = struct.unpack('<I', m.group(1))[0]
        if 0 < v < len(d2):
            cands[v] = cands.get(v, 0) + 1
    best = None
    for v in sorted(cands, key=lambda x: -cands[x]):
        ok = 0
        for r in offs:
            a = v + r
            if 0 < a < len(d2) and d2[a - 1] == 0 and 32 <= d2[a] < 127:
                ok += 1
        if best is None or ok > best[1]:
            best = (v, ok)
        if ok == len(offs):
            return v
    raise ValueError('STR_BASE not found (best %r of %d)' % (best, len(offs)))


def _s(b):
    return b.decode('latin1')


def area_str(b):
    """pgroups' area byte (mkseason.area_byte) -> the clubs.csv text: low nibble the 1st choice, high nibble the
    2nd + 1 (F none, E any: '*'; + 8 never another group: '!')."""
    first = LETTERS[b & 15]
    hi = b >> 4
    if hi == 0xF:
        return first
    if hi == 0xE:
        return first + ',*'
    return first + ',' + LETTERS[(hi & 7) - 1] + ('!' if hi & 8 else '')


# ---- the tables ------------------------------------------------------------------------------------------------

def _club_ok(w, g, word=None):
    c = w.global_club(g)
    if c is None:
        return False
    return word is None or w.word(word) == c


def find_pg(e, w):
    """pgroups PG_TAB + RS_TAB (`push dword PG_TAB` / `push dword RS_TAB` in pg_fix): (pg entries, rs entries).
    PG: [(world, [(first, sizes)], [(global, base division, [areas])])]; RS: [(world, nd, low, clubs, pool)]."""
    d1 = e.obj[1]
    best = None
    for (so, soff), (to, t) in e.ptr.items():
        if so != 1 or to != 1 or soff < 1 or d1[soff - 1] != 0x68:
            continue
        try:
            pg, end = _parse_pg(d1, t)
        except (IndexError, ValueError, struct.error):
            continue
        if not pg or not any(x[0] == WORLD for x in pg):
            continue
        if not all(_club_ok(w, g) for x in pg if x[0] == WORLD for g, _, _ in x[2]):
            continue
        try:
            rs = _parse_rs(d1, end)
        except (IndexError, ValueError, struct.error):
            rs = []
        best = (pg, rs)
        break
    return best or ([], [])


def _parse_pg(d, t):
    n = d[t]
    if not 1 <= n <= 16:
        raise ValueError
    p, out = t + 1, []
    for _ in range(n):
        world, nl = d[p], d[p + 1]
        if not 1 <= world <= 60 or not 1 <= nl <= 32:
            raise ValueError
        p += 2
        levels = []
        for _ in range(nl):
            first, ng = d[p], d[p + 1]
            levels.append((first, list(d[p + 2:p + 2 + ng])))
            p += 2 + ng
        if levels[0][0] != 0 or any(b[0] != a[0] + len(a[1]) for a, b in zip(levels, levels[1:])):
            raise ValueError
        npar = d[p]
        if npar != sum(1 for _, s in levels if len(s) > 1):
            raise ValueError
        nc = struct.unpack_from('<H', d, p + 1)[0]
        p += 3
        clubs = []
        for _ in range(nc):
            g, b = struct.unpack_from('<HB', d, p)
            clubs.append((g, b, list(d[p + 3:p + 3 + npar])))
            p += 3 + npar
        out.append((world, levels, clubs))
    return out, p


def _parse_rs(d, p):
    n = d[p]
    p += 1
    out = []
    for _ in range(n):
        world, nd, low = d[p:p + 3]
        nc = struct.unpack_from('<H', d, p + 3)[0]
        p += 5
        clubs = [struct.unpack_from('<HB', d, p + 3 * i) for i in range(nc)]
        p += 3 * nc
        npool = d[p]
        pool = list(struct.unpack_from('<%dH' % npool, d, p + 1))
        p += 1 + 2 * npool
        out.append((world, nd, low, clubs, pool))
    return out


def find_ri(e, w):
    """ripool RI_TAB (`push dword RI_TAB` in pg_fix): [(world, file, base, spares, pools)], pools: ('league', low,
    slots, areas, order) / ('state', key word, relegated, slots, strength, order, entries)."""
    d1 = e.obj[1]
    for (so, soff), (to, t) in e.ptr.items():
        if so != 1 or to != 1 or soff < 1 or d1[soff - 1] != 0x68:
            continue
        try:
            ri = _parse_ri(d1, t)
        except (IndexError, ValueError, struct.error):
            continue
        if ri and any(x[0] == WORLD for x in ri) and all(x[1] in w.code_of for x in ri if x[0] == WORLD):
            return ri
    return []


def _parse_ri(d, t):
    nf = d[t]
    if not 1 <= nf <= 8:
        raise ValueError
    p, out = t + 1, []
    for _ in range(nf):
        world, fl, npools = d[p:p + 3]
        base, moff, nmap, nsp = struct.unpack_from('<HHHB', d, p + 3)
        if not 1 <= world <= 60 or not 1 <= npools <= 20 or nmap > 640:
            raise ValueError
        p += 10
        spares = list(d[p:p + nsp])
        p += nsp
        pools = []
        for _ in range(npools):
            kind, n = d[p], d[p + 1]
            if kind == 0:
                low = d[p + 2]
                ns = struct.unpack_from('<H', d, p + 3)[0]
                p += 5
                slots = [struct.unpack_from('<HBB', d, p + 4 * i) for i in range(ns)]
                p += 4 * ns
                areas = list(d[p:p + n])
                p += n
                order = list(d[p:p + n])
                p += n
                pools.append(('league', low, slots, areas, order))
            elif kind == 1:
                key, rel, ns = struct.unpack_from('<HBB', d, p + 2)
                p += 6
                slots = [struct.unpack_from('<BHB', d, p + 4 * i) for i in range(ns)]
                p += 4 * ns
                strength = list(d[p:p + n])
                p += n
                ne = d[p]
                entries = list(struct.unpack_from('<%dH' % ne, d, p + 1))
                p += 1 + 2 * ne
                order = list(d[p:p + n])
                p += n
                pools.append(('state', key, rel, slots, strength, order, entries))
            else:
                raise ValueError
            if len(set(order)) != n:                # (indices into the file's whole RS pool)
                raise ValueError
        out.append((world, fl, base, spares, pools))
    return out


def find_ida(e, w):
    """ripool IDA_TAB (4.0.5+, `mov edi, IDA_TAB` in ri_ida): [(world, base, slots, [areas per identity])]."""
    d1 = e.obj[1]
    for (so, soff), (to, t) in e.ptr.items():
        if so != 1 or to != 1 or soff < 1 or d1[soff - 1] != 0xBF:
            continue
        try:
            n = d1[t]
            if not 1 <= n <= 8:
                continue
            p, out = t + 1, []
            for _ in range(n):
                world = d1[p]
                base, moff, ns, nid, na = struct.unpack_from('<HHHHB', d1, p + 1)
                if not 1 <= world <= 60 or not 1 <= na <= 4 or not ns <= nid <= 1000:
                    raise ValueError
                p += 10
                ids = [list(d1[p + na * i:p + na * (i + 1)]) for i in range(nid)]
                p += na * nid
                out.append((world, base, ns, ids))
            if any(x[0] == WORLD and _club_ok(w, x[1]) for x in out):
                return out
        except (IndexError, ValueError, struct.error):
            continue
    return []


def find_sec(e, w):
    """gplay SEC_TAB (gplay.c, mkseason.second_table): {code: [(B key, first key or None)]}."""
    d1 = e.obj[1]
    seen = set()
    for (so, soff), (to, t) in e.ptr.items():
        if to != 1 or t in seen:
            continue
        seen.add(t)
        try:
            nc = d1[t]
            if not 1 <= nc <= 16:
                continue
            p, out = t + 1, {}
            for _ in range(nc):
                nd = d1[p]
                lv = list(d1[p + 1:p + 1 + nd])
                if not 1 <= nd <= 32 or lv[0] != 0 or any(b - a not in (0, 1) for a, b in zip(lv, lv[1:])):
                    raise ValueError
                p += 1 + nd
                n = d1[p]
                p += 1
                if not 1 <= n <= 64:
                    raise ValueError
                for _ in range(n):
                    gb, gf, top, fl, word = struct.unpack_from('<HHBBH', d1, p)
                    p += 8
                    if not _club_ok(w, gb, word):
                        raise ValueError
                    code, key = w.global_club(gb)
                    first = w.global_club(gf) if gf != 0xFFFF else None
                    if gf != 0xFFFF and (first is None or first[0] != code):
                        raise ValueError
                    out.setdefault(code, []).append((key, first[1] if first else None, top, fl))
            return out
        except (IndexError, ValueError, struct.error):
            continue
    return {}


def find_prom(e, w):
    """gplay GP_PROM_TAB (mkseason.promedio_table): {code: [[key, points, matches, points, matches]]}."""
    d1 = e.obj[1]
    seen = set()
    for (so, soff), (to, t) in e.ptr.items():
        if to != 1 or t in seen:
            continue
        seen.add(t)
        try:
            n = struct.unpack_from('<H', d1, t)[0]
            if not 4 <= n <= 48:
                continue
            out = {}
            for i in range(n):
                g, p1, m1, p2, m2 = struct.unpack_from('<HHBHB', d1, t + 2 + 8 * i)
                c = w.global_club(g)
                if c is None or m1 > 60 or m2 > 60 or p1 > 3 * m1 or p2 > 3 * m2:
                    raise ValueError
                out.setdefault(c[0], []).append([c[1], p1, m1, p2, m2])
            if len(out) == 1:
                return out
        except (IndexError, ValueError, struct.error):
            continue
    return {}


def find_sp(e, w):
    """split SP_TAB: {(file, division): (games, stages)} of the world (entry head: world, file, div, n, cycles, d1,
    d2, stages; cycles = games + 1 with stages)."""
    d1 = e.obj[1]
    seen = set()
    for (so, soff), (to, t) in e.ptr.items():
        if to != 1 or t in seen:
            continue
        seen.add(t)
        out, p, ok = {}, t, False
        try:
            while True:
                world, fl, div, n, cyc, a, b, ns = d1[p:p + 8]
                length = struct.unpack_from('<H', d1, p + 14)[0]
                if length == 0 and world == 0:
                    ok = bool(out)
                    break
                if not 1 <= world <= 60 or not 2 <= n <= 40 or length < 16 or length % 4 or ns > 8 or cyc > 5:
                    break
                if world == WORLD:
                    if fl not in w.code_of:
                        break
                    out[(fl, div)] = (cyc - (1 if ns else 0), ns)
                p += length
        except (IndexError, struct.error):
            ok = False
        if ok and out:
            return out
    return {}


def find_bn(e):
    """benchnum BN_TAB (bn_world: `mov esi, BN_TAB; cmp byte [esi], 0; je; cmp al, [esi]; je`): the worlds with
    real squad numbers."""
    d1 = e.obj[1]
    for m in re.finditer(rb'\xbe(....)\x80\x3e\x00\x74.\x3a\x06\x74', d1, re.S):
        t = e.ptr.get((1, m.start() + 1))
        if t and t[0] == 1:
            d = e.obj[1]
            out, p = [], t[1]
            while d[p]:
                out.append(d[p])
                p += 1
            return out
    return None


def read_bage(w):
    """DATA\\BAGE.<world> (bteam.py, 4.0.6+): [(B team word, parent word, rules index)], or None."""
    path = None
    for x in os.listdir(w.data):
        if x.upper() == 'BAGE.%03d' % WORLD:
            path = os.path.join(w.data, x)
    if not path:
        return None
    d = open(path, 'rb').read()
    if d[:2] != b'BA':
        return None
    n = d[3]
    p, out = 4, []
    for _ in range(n):
        bw, pw, ri, npl = struct.unpack_from('<HHBB', d, p)
        out.append((bw, pw, ri))
        p += 6 + npl
    return out


# ---- the competitions ------------------------------------------------------------------------------------------

def _names(e, sb, off):
    rl, rs = struct.unpack_from('<II', e.obj[1], off)
    return [_s(e.cstr(2, sb + rl)), _s(e.cstr(2, sb + rs))]


def _comps(e, w, league_at, table_at):
    """The structs from the league to its competitions table: [(type, offset)]."""
    d1 = e.obj[1]
    out, at = [], league_at
    while at < table_at:
        if at == league_at:
            nd = d1[at + 9]
            out.append(('league', at))
            size = 13 + 6 * nd + 1 + 8 * nd
        elif d1[at + 5] == 0x22 and d1[at + 7] == 0x28:          # a state contest (statecup.contest_bytes)
            out.append(('state', at))
            size = 0x27 + 8 + 2 * d1[at + 0x0F]
        else:                                                   # a cup: 14 bytes + rounds + 0, names, the clubs
            hl = d1[at + 5] + 5
            out.append(('cup', at))
            size = hl + 8 + 2 * d1[at + 10]
        at = (at + size + 3) & ~3
    if at != table_at:
        raise ValueError('competition structs %#x..%#x do not end at the table' % (league_at, table_at))
    return out


def decode(world):
    w, e = world, world.exe
    d1 = e.obj[1]
    nums, rows = find_ns(e)
    wrows = rows[WORLD]
    files_of = {}
    for f, code in sorted(w.code_of.items()):
        files_of.setdefault(code, []).append(f)

    # every country's competitions table, league struct and structs
    country = {}
    name_offs = []
    for code, files in files_of.items():
        for f in files:
            r = wrows.get(f)
            if not r or not r[1]:
                continue
            tobj, tab = _tagged(e, r[1])
            lg = e.ptr.get((1, tab))
            if tobj != 1 or not lg or d1[lg[1] + 12] != 0x35 or d1[lg[1] + 2] != f:
                continue
            slots, k = [], 8
            while struct.unpack_from('<i', d1, tab + k)[0] != -1:
                slots.append(e.ptr[(1, tab + k)][1])
                k += 4
            cobj, crec = _tagged(e, r[0])
            country[code] = dict(file=f, table=tab, league=lg[1], slots=slots, rec=(cobj, crec),
                                 comps=_comps(e, w, lg[1], tab))
            break
    for c in country.values():
        for typ, at in c['comps']:
            if typ == 'league':
                nd = d1[at + 9]
                no = at + 13 + 6 * nd + 1
                name_offs += [struct.unpack_from('<I', d1, no + 4 * i)[0] for i in range(2 * nd)]
    sb = str_base(e, name_offs)

    pg, rs = find_pg(e, w)
    ri = find_ri(e, w)
    ida = find_ida(e, w)
    sec = find_sec(e, w)
    prom = find_prom(e, w)
    sp = find_sp(e, w)
    bn = find_bn(e)
    bage = read_bage(w)

    packs, clubs = {}, {}
    for code in sorted(country, key=lambda c: country[c]['file']):
        c = country[code]
        lfile = c['file']
        cobj, crec = c['rec']
        cont = CONTINENTS[e.obj[cobj][crec]]
        pk = {'id': 'world-2026-27-' + code.lower(), 'continent': cont}
        comps, season = [], {'league': 'league'}
        slots = list(c['slots'])
        state_ptrs = [at for typ, at in c['comps'] if typ == 'state']
        if state_ptrs and slots and slots[0] == state_ptrs[0]:
            slots = slots[1:]
        cup_keys = {}
        if slots:
            cup_keys[slots[0]] = 'cup'
            season['cup'] = 'cup'
        if len(slots) > 1:
            cup_keys[slots[1]] = 'league_cup'
            season['league_cup'] = 'league_cup'
        for typ, at in c['comps']:
            if typ == 'league':
                comps.append(_league(e, w, sb, code, lfile, at, pg, sp, prom))
            elif typ == 'cup':
                hl = d1[at + 5] + 5
                comps.append({'key': cup_keys.get(at, 'cup%d' % len(comps)), 'type': 'cup',
                              'names': {'en': _names(e, sb, at + hl)}})
            else:
                nm = _names(e, sb, at + 0x27)
                gent = nm[0].split()[-1]
                comps.append({'key': BR_STATES.get(gent) or re.sub(r'[^a-z0-9]', '', nm[1].lower()), 'type': 'state',
                              'names': {'en': nm}, '_first': d1[at + 0x2F] | d1[at + 0x30] << 8})
        pk['competitions'] = comps
        pk['season'] = season
        packs[code] = pk
        # the clubs: every club of the country's files (+ its off-file pool)
        cl = {}
        for f in files_of[code]:
            for i in range(len(w.records[f])):
                key = w.key[(f, i)]
                div = str(w.records[f][i][25]) if f == lfile else ''
                cl[key] = {'division': div, 'second_of': '', 'area': ''}
        for nm, key in w.pools.get(code, []):
            cl[key] = {'division': '', 'second_of': '', 'area': ''}
        clubs[code] = cl

    # squad numbers (a world's flag: on its first country)
    if bn and WORLD in bn:
        first = min(country, key=lambda c: country[c]['file'])
        packs[first]['squad_numbers'] = True

    # parallel groups: the league's 'parallel', every club's area
    for world, levels, pclubs in pg:
        if world != WORLD:
            continue
        code = w.global_club(pclubs[0][0])[0]
        lg = packs[code]['competitions'][0]
        par = [{'upper': first - 1, 'first': first, 'count': len(sz)} for first, sz in levels if len(sz) > 1]
        lg['parallel'] = par if len(par) > 1 else par[0]
        for g, base, areas in pclubs:
            _, key = w.global_club(g)
            txt = ['' if a == 0xFF or a < 0x10 else area_str(a) for a in areas]
            clubs[code][key]['area'] = txt[0] if len(set(txt)) == 1 else '/'.join(txt)

    # off-file pools: the league's reserve (order), Brazil's state pools (their clubs' area = the state's key) and
    # spare clubs
    ida_of = {(x[0], x[1]): x for x in ida}
    for world, fl, base, spares, pools in ri:
        if world != WORLD:
            continue
        code = w.code_of[fl]
        pool = w.pools.get(code, [])
        if spares:
            packs[code]['state_spares'] = [w.key[(fl, o)] for o in spares]
        for pl in pools:
            if pl[0] == 'league':
                _, low, slots, areas, order = pl
                lg = packs[code]['competitions'][0]
                keys = [pool[i][1] for i in order]
                lg['reserve'] = {'off_file': True, 'first': keys, 'pool': list(keys), 'then': 'rotation'}
                x = ida_of.get((world, base))
                for i, (nm, key) in enumerate(pool):
                    if x:
                        txt = ['' if a == 0xFF else area_str(a) for a in x[3][x[2] + i]]
                        clubs[code][key]['area'] = txt[0] if len(set(txt)) == 1 else '/'.join(txt)
                    elif areas[i] != 0xFF:
                        clubs[code][key]['area'] = LETTERS[areas[i]]
            else:
                _, kw, rel, slots, strength, order, entries = pl
                st = next((cp for cp in packs[code]['competitions'] if cp.get('_first') == entries[0]), None)
                for i in order:
                    if st:
                        clubs[code][pool[i][1]]['area'] = st['key']
                if st:
                    st['_pool'] = [pool[i][1] for i in order]
    for world, nd, low, rclubs, rpool in rs:
        if world != WORLD:
            continue
        code = w.global_club(rclubs[0][0])[0]
        keys = [w.global_club(g)[1] for g in rpool]
        packs[code]['competitions'][0]['reserve'] = {'first': keys, 'pool': list(keys), 'then': 'rotation'}

    # B teams: SEC_TAB (their rules: the country's), BAGE (parents in other countries too)
    for code, rows_ in sec.items():
        packs[code]['competitions'][0]['second_teams'] = {'rules': code.lower()}
        for key, first, top, fl in rows_:
            clubs[code][key]['second_of'] = first or '?'
    for bw, pw, ri_ in bage or []:
        b, p = w.word(bw), w.word(pw)
        if not b:
            continue
        packs[b[0]]['competitions'][0].setdefault('second_teams', {'rules': b[0].lower()})
        if p:
            clubs[b[0]][b[1]]['second_of'] = p[1] if p[0] == b[0] else '%s:%s' % p

    for pk in packs.values():
        for cp in pk['competitions']:
            cp.pop('_first', None)
            cp.pop('_pool', None)
    return {'packs': packs, 'clubs': clubs}


def _league(e, w, sb, code, lfile, at, pg, sp, prom):
    d1 = e.obj[1]
    nd, games, win = d1[at + 9], d1[at + 10], d1[at + 11]
    no = at + 13 + 6 * nd + 1
    divs = []
    for i in range(nd):
        t, pr, _, rel, _, _ = d1[at + 13 + 6 * i:at + 19 + 6 * i]
        divs.append({'teams': t, 'promoted': pr, 'relegated': rel, 'names': {'en': _names(e, sb, no + 8 * i)}})
    lg = {'key': 'league', 'type': 'league', 'clubs': 'league', 'games': games, 'win_points': win,
          'relegated': divs[0]['relegated'], 'names': divs[0]['names']}
    for d in range(nd):
        x = sp.get((lfile, d))
        if x and (x[1] == 0 or x[0] not in (0, games)):     # (0: a fixture-list split, its own days)
            divs[d]['games'] = x[0]
    if nd > 1:
        lg['divisions'] = divs
    if code in prom:
        lg['promedio_history'] = prom[code]
    for so, soff in e.refs.get((1, at), []):            # dplay's DP_TAB (9-byte entries: dd league, db world,
        if so != 1 or d1[soff + 4] != WORLD or (1, soff + 5) in e.ptr:   # Serie B, first group, groups, lc id;
            continue                                    # GP_TAB's entries hold a pointer there instead)
        u, f, ng = d1[soff + 5:soff + 8]
        if f == u + 1 and 2 <= ng <= 4 and _dp_end(e, soff):
            lg['playoffs'] = 'ita'
    return lg


def _dp_end(e, p):
    d1 = e.obj[1]
    for _ in range(64):
        if d1[p:p + 4] == bytes(4) and (1, p) not in e.ptr:
            return True
        if (1, p) not in e.ptr or (1, p + 5) in e.ptr:
            return False
        p += 9
    return False


if __name__ == '__main__':
    import json
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import world_mod_exe
    print(json.dumps(decode(world_mod_exe.World(sys.argv[1])), indent=1)[:int(sys.argv[2]) if len(sys.argv) > 2 else None])
