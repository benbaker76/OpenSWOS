#!/usr/bin/env python3
"""world_mod_cups.py -- World 2026-27's national cups, their first-season draws, Brazil's state championships,
Italy's Coppa Italia Serie D and the squad prices the importer's strengths need, decoded from an installed
SWOS 96/97 Mod (its patched DOS exe + DATA): a fragment for scripts/world_from_mod.py (docs/WORLD_2026_27.md 6d).

    decode(world) -> {"packs": {CODE: {"competitions": [...], "season": {...}, "state_spares": [...]}},
                      "ties": {CODE: [ties.csv rows]}, "cupd": {"ITA": {...}}, "players": {CODE: [...]}}

Where each part lives (every table found through pointers / code references, never by address):
- a country's competitionsTable entry (world 5's namespace row: mkseason.ns_emit) -> [league, -2, (first state),
  (cup), (league cup), -1]; a cup = the 96/97 national-cup header with the pack's rounds (mkseason._patch_pack),
  its names (offsets from the game's string base) and its clubs in contest order (round 1's ties, then the byes);
- the fixed draws (historic.draw_table, world 5's part after its [1, 0, 5] filter record): per cup round the
  permutation of the previous round's winners;
- the cups keeping numbered rounds: rnames' RN_NUMB id list;
- Brazil's states: the first one in the competitionsTable entry, the others after it in statecup's ENT entry (db id,
  dd struct); each with statesim's SS_TAB entry (its clubs, strengths, stage-1 calendar, relegation and stages
  programs: statespec.sim_table) and its off-file pool in gplay's ri_season table (ripool.table_bytes: the pool order,
  and the states' file's spare ordinals = state_spares);
- the Coppa Italia Serie D: gplay's CD_TAB + CD_NM + CD_DT (cupd.compile_table / strengths);
- names: {"en": [long, short], "*": the same} -- the English exe's; "*" too because world_import.italian_cups reads
  names["*"] only;
- players.csv: the team files' price bytes (TMW5 + XTW5, RS5 for the off-file pools), checked against the strengths
  the exe holds (SS_TAB, CD_DT).
NOT in the installed files: the cups' original short names (the exe keeps complist.fit_short's calendar label) and
some long ones (complist.fit_titles' fitted title); the first season's cup winners (only the bracket: later rounds
name a stand-in, the home club of each tie they follow from); the Coppa Italia Serie D's TM ids, match dates, round
names, rule texts and the clubs outside the file; the pool clubs' players past 16 (RS5 holds 16).
Many of the formats are the Mod's tools' (MIT, Davide Lorigliola): mkseason.py, historic.py, rnames.py, statecup.py,
statespec.py, cupd.py -- ported, not imported. Standard library only.
"""
import os
import struct

WORLD = 5
ROUND_NAMES = {0x94: "two_legs", 0x84: "two_legs_pens", 0x14: "single", 0x54: "single_et"}   # mkseason.ROUND


def _u32(e, obj, off):
    return struct.unpack_from("<I", e.obj[obj], off)[0]


def str_base(e):
    """sacups.STR_BASE (patch.patch_italy): the obj2 base of every contest name offset -- the game's own Italian
    league struct names SERIE A / SERIE B from it."""
    lo = e.find_all(2, bytes.fromhex("3c00144020150000000202033512"))
    assert len(lo) == 1, lo
    r26, r34 = _u32(e, 2, lo[0] + 26), _u32(e, 2, lo[0] + 34)
    o2 = e.obj[2]
    cands = [a - r26 for a in e.find_all(2, b"SERIE A\0") if o2[a - r26 + r34:a - r26 + r34 + 8] == b"SERIE B\0"]
    return min(cands)


def name_at(e, base, rel):
    return e.cstr(2, (base + rel) & 0xFFFFFFFF).decode("latin1")


def world_rows(e, world=WORLD):
    """{country number: (countriesTable entry, competitionsTable entry, teamsCountryNumbers word)} of a world's
    namespace (mkseason.ns_emit): a dd per world (wt_at) points at its rows, 10 bytes per number of the sorted list
    (list_at) -- the tagged entries: bit 30 obj1, bit 31 obj2 (0 none). The rows follow the dd table at once."""
    o1 = e.obj[1]
    tgt = {s: t for (so, s), (to, t) in e.ptr.items() if so == 1 and to == 1}
    for s, t in tgt.items():
        if not (0 < t - s <= 4 * 16 and (t - 4) in tgt):
            continue                            # (t: the first world's rows, right after the dd table)
        blocks = sorted({tgt[a] for a in range(s, t, 4) if a in tgt})
        if len(blocks) < 2 or any((b - blocks[0]) % 10 for b in blocks):
            continue
        n = min(b - a for a, b in zip(blocks, blocks[1:])) // 10
        for wt in range(t - 4, t - 4 * 64, -4):
            la = wt - ((n + 3) & ~3)
            nums = o1[la:la + n]
            if (1, la) in e.refs and all(nums[i] < nums[i + 1] for i in range(n - 1)) and \
                    all(a in tgt or o1[a:a + 4] == bytes(4) for a in range(wt, t, 4)):
                at = wt + 4 * world
                if at >= t or at not in tgt:
                    raise ValueError("world %d: no namespace rows" % world)
                r = tgt[at]
                return {num: struct.unpack_from("<IIH", o1, r + 10 * i) for i, num in enumerate(nums)}
    raise ValueError("the worlds' namespace table was not found")


def tagged(x):
    """A namespace row's entry -> (obj, offset), or None."""
    if not x:
        return None
    return (2 if x >> 31 else 1, x & 0x3FFFFFFF)


def country_tables(world):
    """{country code: [competition struct (obj, offset)] of its competitionsTable entry after the league's -2}."""
    e = world.exe
    out = {}
    for num, (_, comp, _) in world_rows(e).items():
        code = world.code_of.get(num)
        tc = tagged(comp)
        if not code or not tc:
            continue
        obj, off = tc
        ents, a = [], off
        while True:
            v = _u32(e, obj, a)
            if (obj, a) in e.ptr:
                ents.append(e.ptr[(obj, a)])
            elif v == 0xFFFFFFFE:
                ents.append(None)
            elif v == 0xFFFFFFFF:
                break
            else:
                raise ValueError("%s: competitionsTable entry %#x" % (code, a))
            a += 4
        if ents[1:2] != [None]:
            raise ValueError("%s: no [league, -2, ...]" % code)
        out[code] = (num, ents[0], ents[2:])
    return out


def cup_struct(world, base, at, code):
    """A national cup (mkseason._patch_pack): 14-byte header (id, 1, league file, ..., [5] names - 5, [7] clubs - 7,
    [10] clubs, [13] first round's number), the round bytes, 0, two name offsets, the clubs' (file, ordinal)."""
    e = world.exe
    obj, off = at
    h = e.bytes(obj, off, 14)
    nat = h[1] == 1
    na, cl = h[5] + 5, h[7] + 7
    rb = e.bytes(obj, off + 14, na - 15)
    rounds = [ROUND_NAMES[b] for b in rb]
    long_, short = (name_at(e, base, _u32(e, obj, off + na + 4 * i)) for i in range(2))
    clubs = []
    for i in range(h[10]):
        f, o = e.bytes(obj, off + cl + 2 * i, 2)
        if (f, o) not in world.key:
            raise ValueError("%s cup %02X: club (%d, %d) not in the team files" % (code, h[0], f, o))
        clubs.append(world.key[(f, o)])
    return {"id": "%02X" % h[0], "national": nat, "rounds": rounds, "names": [long_, short],
            "first_round": h[13], "clubs": clubs}


def draw_records(world):
    """historic.draw_table, world WORLD's part: [(id, perm, sort, first club or None)]."""
    e = world.exe
    want = bytes((1, 0, WORLD))
    for obj in (1, 2):
        for at in e.find_all(obj, want):
            d, p, recs, ok = e.obj[obj], at + 3, [], True
            fc = None
            while d[p] not in (0, 1):
                if d[p] == 2:
                    f, o = d[p + 1], d[p + 2]
                    fc = (f, o) if (f, o) != (0, 0) else None
                    p += 3
                    continue
                cid, n = d[p], d[p + 1]
                perm = list(d[p + 2:p + 2 + (n & 0x7F)])
                if not perm or sorted(perm) != list(range(len(perm))):
                    ok = False
                    break
                recs.append((cid, perm, bool(n & 0x80), fc))
                p += 2 + (n & 0x7F)
            if ok and recs:
                return recs
    raise ValueError("world %d's fixed draws not found" % WORLD)


def numbered_ids(world):
    """rnames' RN_NUMB: `mov al, [DIYCOPY + 2Dh]; mov ebx, RN_NUMB; cmp byte [ebx], 0; je; cmp [ebx], al` -> its ids (0 ends)."""
    e = world.exe
    o1 = e.obj[1]           # (after `test al, al; jnz .nb; mov al, [DIYCOPY + 2Dh]`)
    hits = [a for a in e.find_all(1, b"\x80\x3b\x00\x74") if o1[a - 5] == 0xBB and (1, a - 4) in e.ptr
            and o1[a + 5:a + 7] == b"\x38\x03" and o1[a - 14:a - 9] == b"\x84\xc0\x75\x05\xa0"]
    assert len(hits) == 1, hits
    obj, off = e.ptr[(1, hits[0] - 4)]
    out = []
    while e.obj[obj][off]:
        out.append(e.obj[obj][off])
        off += 1
    return out


def cup_ties(key, cup, perms):
    """ties.csv rows of a cup from its contest order (round 1's ties, then its byes) and its rounds' draws: round 1
    as listed; a later round's tie = the winners of the previous round's ties its permutation picks. The winners of
    the first season are not in the exe (only the bracket is): a later tie names the HOME club of each tie it follows
    from (a bye: the club) -- the same bracket (world_import.bracket_order reads nothing else) --, and so its
    'winner' is that club (every round but the final: the Mod's mkseason.Pack.bracket requires one)."""
    clubs = cup["clubs"]
    nr = len(cup["rounds"])
    if len(perms) != nr:
        raise ValueError("%s: %d draws, %d rounds" % (key, len(perms), nr))
    t1 = len(perms[0]) // 2
    rows = [{"competition": key, "round": "1", "home": clubs[2 * i], "away": clubs[2 * i + 1], "winner": ""}
            for i in range(t1)]
    byes = clubs[2 * t1:]
    rows += [{"competition": key, "round": "1", "home": c, "away": "", "winner": ""} for c in byes]
    prev = [clubs[2 * i] for i in range(t1)] + byes      # (stand-ins for the winners)
    for r in range(1, nr):
        pm = perms[r]
        if sorted(pm) != list(range(len(prev))):
            raise ValueError("%s round %d: not a draw of the %d winners" % (key, r + 1, len(prev)))
        ties = [(prev[pm[2 * i]], prev[pm[2 * i + 1]]) for i in range(len(pm) // 2)]
        rows += [{"competition": key, "round": str(r + 1), "home": h, "away": a, "winner": ""} for h, a in ties]
        prev = [h for h, _ in ties]
    for row in rows:                                    # (the stand-ins: a tie's home club, a bye's club)
        if row["round"] != str(nr):
            row["winner"] = row["home"]
    return rows


def decode_cups(world):
    e = world.exe
    base = str_base(e)
    numbered = set(numbered_ids(world))
    draws = {}
    for cid, perm, _, fc in draw_records(world):
        if fc is None:
            draws.setdefault(cid, []).append(perm)
    packs, ties = {}, {}
    for code, (num, league, ents) in sorted(country_tables(world).items()):
        comps, season = [], {}
        cups = []
        for at in ents:
            h1 = e.u8(at[0], at[1] + 1)
            if h1 == 1:
                cups.append(at)
        for key, at in zip(("cup", "league_cup"), cups):
            c = cup_struct(world, base, at, code)
            comp = {"key": key, "type": "cup", "id": c["id"],
                    "layout": "national" if c["national"] else "cwc",
                    "rounds": c["rounds"], "names": {"en": c["names"], "*": c["names"]}}
            if c["first_round"] != 1:
                comp["first_round"] = c["first_round"]
            if int(c["id"], 16) in numbered:
                comp["round_names"] = "numbered"
            comps.append(comp)
            season[key] = key
            perms = draws.get(int(c["id"], 16))
            if perms:
                ties.setdefault(code, []).extend(cup_ties(key, c, perms))
        packs[code] = {"competitions": comps, "season": season}
    return packs, ties



STATE_FIELDS = ("clubs", "stages", "months", "relegated", "relegation", "reserve", "win_points")

MATCHES = {0x14: "single", 0x54: "single_et", 0x94: "two_legs", 0x84: "two_legs_pens", 0x00: "single_replay"}
# a state championship's key: its Brazilian state (the pack's keys: the state's two letters), by the demonym of its
# name (CAMPEONATO <demonym>) -- the exe keeps no key
STATE_KEYS = {"PAULISTA": "sp", "CARIOCA": "rj", "MINEIRO": "mg", "GAUCHO": "rs", "ALAGOANO": "al", "BAIANO": "ba",
              "CEARENSE": "ce", "GOIANO": "go", "MATO-GROSSENSE": "mt", "PARAENSE": "pa", "PERNAMBUCANO": "pe",
              "PARANAENSE": "pr", "CATARINENSE": "sc", "POTIGUAR": "rn", "PARAIBANO": "pb", "SERGIPANO": "se",
              "PIAUIENSE": "pi", "MARANHENSE": "ma", "AMAZONENSE": "am", "CAPIXABA": "es", "BRASILIENSE": "df",
              "SUL-MATO-GROSSENSE": "ms", "TOCANTINENSE": "to", "RONDONIENSE": "ro", "ACREANO": "ac",
              "AMAPAENSE": "ap", "RORAIMENSE": "rr"}


def find_parsed(e, parse, objs=(1, 2)):
    """The first fixup target (in objs) that `parse(obj, off)` accepts (returns not None): a table found by what it
    holds and that something points at it."""
    for obj, off in sorted(t for t in e.refs if t[0] in objs):
        try:
            r = parse(obj, off)
        except (IndexError, ValueError, KeyError, struct.error, AssertionError):
            r = None
        if r is not None:
            return r
    return None


def parse_ss_tab(world, obj, off):
    """statesim.c's SS_TAB (statespec.sim_table): db states; per state dw key, db n, g, size, down, q; n x dw word;
    n x dw strength; dw pairs, pairs x (db home, away); the relegation program; the stages program."""
    e = world.exe
    d = e.obj[obj]
    ns, p, out = d[off], off + 1, []
    if not 1 <= ns <= 32:
        return None
    for _ in range(ns):
        key, n, g, size, down, q = struct.unpack_from("<HBBBBB", d, p)
        p += 7
        if not 2 <= n <= 16 or not g or n % g:
            return None
        words = list(struct.unpack_from("<%dH" % n, d, p))
        p += 2 * n
        if words[0] != key or any((w & 0xFF, w >> 8) not in world.key for w in words):
            return None
        strs = list(struct.unpack_from("<%dH" % n, d, p))
        p += 2 * n
        np_ = struct.unpack_from("<H", d, p)[0]
        p += 2
        pairs = [(d[p + 2 * i], d[p + 2 * i + 1]) for i in range(np_)]
        if any(a >= n or b >= n for a, b in pairs):
            return None
        p += 2 * np_
        t = d[p]
        ln = {0: 1, 1: 3 + 2 * d[p + 2], 3: 4}.get(t)
        if t == 2:
            nm = d[p + 2]
            nb = d[p + 3 + nm]
            ln = 3 + nm + 1 + nb + 3
        if ln is None:
            return None
        rel = bytes(d[p:p + ln])
        p += ln
        nst, q0, ko = d[p], p, []
        p += 1
        for _ in range(nst):
            if d[p] == 1:
                cnt = d[p + 5]
                ko.append(("groups",) + tuple(d[p + 1:p + 6]) + (list(d[p + 6:p + 6 + cnt]),))
                p += 6 + cnt
            elif d[p] == 2:
                q_ = d[p + 1]
                ko.append(("knockout",) + tuple(d[p + 1:p + 5]) + (list(d[p + 5:p + 5 + q_]),))
                p += 5 + q_
            else:
                return None
        out.append({"key": key, "n": n, "g": g, "size": size, "down": down, "q": q, "words": words, "strengths": strs,
                    "pairs": pairs, "relegation": rel, "ko": ko})
    return out


def parse_ri_tab(world, obj, off):
    """gplay.c ri_season's table (ripool.table_bytes): db files; per file db world, file, pools, dw base, dw map
    offset, dw map words, db spares, spares x db ordinal; per pool db kind, n: kind 0 db low, dw slots, slots x (dw, db,
    db), n x db area; kind 1 dw key, db relegated, db slots, slots x (db, dw, db), n x db strength, db entries, entries x
    dw word; then n x db pool record (the first order)."""
    d = world.exe.obj[obj]
    nf, p, out = d[off], off + 1, []
    if not 1 <= nf <= 8:
        return None
    for _ in range(nf):
        wd, fl, npl = d[p], d[p + 1], d[p + 2]
        base, moff, nmap, nsp = struct.unpack_from("<HHHB", d, p + 3)
        p += 10
        if wd > 9 or npl > 20:
            return None
        spares = list(d[p:p + nsp])
        p += nsp
        pools = []
        for _ in range(npl):
            kind, n = d[p], d[p + 1]
            p += 2
            if kind == 0:
                low, ns = d[p], struct.unpack_from("<H", d, p + 1)[0]
                p += 3 + 4 * ns + n
                pools.append({"kind": 0})
            elif kind == 1:
                key, rel, ns = struct.unpack_from("<HBB", d, p)
                p += 4 + 4 * ns + n
                ne = d[p]
                p += 1 + 2 * ne
                pools.append({"kind": 1, "key": key, "relegated": rel})
            else:
                return None
            order = list(d[p:p + n])
            p += n
            if sorted(order) != sorted(set(order)):
                return None
            pools[-1]["order"] = order
        out.append({"world": wd, "file": fl, "spares": spares, "pools": pools})
    if not all(1 <= f["world"] <= 9 and f["file"] in world.code_of for f in out if f["world"] == WORLD) or \
            not any(f["world"] == WORLD for f in out) or any(f["world"] == 0 for f in out):
        return None
    return out


def _label(code, g):
    return "%d%s" % (code // g + 1, chr(65 + code % g))


def state_relegation(world, code, rel, g, strength_of, pool_keys):
    """statesim's relegation program -> pack.json 'relegation' (None: 'bottom')."""
    t = rel[0]
    if t == 0:
        return None
    if t == 1:
        legs, nt = rel[1], rel[2]
        c = [_label(x, g) for x in rel[3:3 + 2 * nt]]
        return {"type": "playoffs", "ties": [[c[2 * i], c[2 * i + 1]] for i in range(nt)], "legs": legs}
    if t == 2:
        meet, nm = rel[1], rel[2]
        mem = [_label(x, g) for x in rel[3:3 + nm]]
        nb = rel[3 + nm]
        best = [_label(x, g) for x in rel[4 + nm:4 + nm + nb]]
        pts, place, xs = rel[4 + nm + nb:7 + nm + nb]
        r = {"type": "group", "members": mem, "meetings": meet}
        if best or pts:
            r["bonus"] = {"best": best, "points": pts}
        if place or xs:
            cand = [k for k in pool_keys if min(255, strength_of(k) // 4) == xs]
            r["extra"] = {"place": place, "club": cand[0]} if len(cand) == 1 else {"place": place, "strength": xs * 4}
        return r
    if t == 3:
        return {"type": "gap", "places": [rel[1], rel[2]], "gap": rel[3]}
    raise ValueError("relegation program %d" % t)


def state_first_stage(st, groups, meetings):
    """Stage 1 from statesim's calendar: a cross calendar (world_states.cross_calendar, the Mod's: seeded, so it is
    recognised), a calendar inside each group, a single round robin, else explicit fixtures (days of n/2 pairs)."""
    import world_states
    n, pairs = st["n"], st["pairs"]
    out = {"type": "groups", "teams": n, "groups": groups, "meetings": meetings}
    rr = []
    for r in range(meetings):
        for x in range(groups):
            mem = list(range(x * (n // groups), (x + 1) * (n // groups)))
            for i in range(len(mem)):
                for j in range(i + 1, len(mem)):
                    rr.append((mem[j], mem[i]) if r & 1 else (mem[i], mem[j]))
    if pairs == rr and groups == 1:
        return out
    for k in range(2, n + 1):
        if n % k or groups not in (1, k) or meetings != 1 or n % 2:
            continue
        nb = n * (n - n // k) // 2
        if nb == len(pairs) and world_states.cross_calendar(n, k)[0] == pairs:
            out["cross"] = k
            return out
    if groups > 1 and world_states.group_calendar(groups, n // groups, meetings)[0] == pairs:
        return out
    if groups == 1 and meetings == 1 and n % 2 == 0 and len(pairs) % (n // 2) == 0:
        out["fixtures"] = [[list(p) for p in pairs[i:i + n // 2]] for i in range(0, len(pairs), n // 2)]
        return out
    raise ValueError("state %04x: stage 1's calendar not recognised" % st["key"])


def state_stages(st, h, stage_bytes):
    """pack.json 'stages' from the contest header and statesim's stages program (world_states.ko_program, inverted)."""
    g0 = h[0x10]
    stages = [state_first_stage(st, g0, h[11])]
    prev = stages[0]
    for i, k in enumerate(st["ko"], 1):
        if k[0] == "groups":
            _, g2, size, meet, cross, n, codes = k
            gp = prev.get("groups", 1) if prev["type"] == "groups" else 1
            x = {"type": "groups", "teams": n, "groups": g2, "from": [_label(c, gp) for c in codes]}
            if meet != 1:
                x["meetings"] = meet
            if cross:
                x["cross"] = cross
        else:
            _, q, b, legs, mode, codes = k
            x = {"type": "knockout", "teams": q - b, "matches": MATCHES[stage_bytes[i]]}
            m = mode & 0x7F
            if m == 3:
                if q > 2 and codes == [v for j in range(q // 2) for v in (j, q - 1 - j)]:
                    x["pairing"] = "merit"
            elif m == 0:
                gp = prev.get("groups", 1)
                lab = [_label(c, gp) for c in codes]
                x["from"] = lab[:q - b]
                if b:
                    x["byes"] = lab[q - b:]
            elif m == 1:
                kk, bb = prev["teams"] // 2, len(prev.get("byes") or [])
                toks = ["W%d" % (j + 1) for j in range(kk)] + ["B%d" % (j + 1) for j in range(bb)]
                x["pairs"] = [toks[c] for c in codes]
            if mode & 0x80:
                x["host"] = "better"
        stages.append(x)
        prev = x
    return stages


def decode_states(world, base, tables, strengths):
    """Brazil's state championships: per country whose competitionsTable holds a state contest (header byte 1 = 2,
    statecup.contest_bytes), every state of its statecup ENT entry (db id, dd struct, after the first one's), with
    statesim's SS_TAB entry (by its first club) and ri_season's pool."""
    e = world.exe
    ss = find_parsed(e, lambda o, a: parse_ss_tab(world, o, a), (1,))
    ri = find_parsed(e, lambda o, a: parse_ri_tab(world, o, a), (1,))
    out = {}
    for code, (num, league, ents) in tables.items():
        firsts = [a for a in ents if e.u8(a[0], a[1] + 1) == 2]
        if not firsts:
            continue
        strength_of = strengths[code]
        ent = [r for r in e.refs[firsts[0]] if e.u8(r[0], r[1] - 1) == e.u8(*firsts[0])]
        assert len(ent) == 1, ent
        structs, r = [], ent[0]
        while r in e.ptr and e.u8(r[0], r[1] - 1) == e.u8(*firsts[0]):
            structs.append(e.ptr[r])
            r = (r[0], r[1] + 5)
        by_key = {s["key"]: s for s in ss or []}
        pools = {pl["key"]: (f, pl) for f in ri or [] if f["world"] == WORLD for pl in f["pools"] if pl["kind"] == 1}
        pool_keys = [k for _, k in world.pools.get(code, [])]
        comps = []
        for at in structs:
            h = e.bytes(at[0], at[1], 0x2F)
            nst, ncl = h[0x0E], h[0x0F]
            clubs = []
            for i in range(ncl):
                f, o = e.bytes(at[0], at[1] + 0x2F + 2 * i, 2)
                clubs.append(world.key[(f, o)])
            names = [name_at(e, base, _u32(e, at[0], at[1] + 0x27 + 4 * i)) for i in range(2)]
            w0 = struct.unpack_from("<H", e.bytes(at[0], at[1] + 0x2F, 2))[0]
            st = by_key[w0]
            if [world.key[(w & 0xFF, w >> 8)] for w in st["words"]] != clubs:
                raise ValueError("%s: SS_TAB and the contest list differ" % code)
            key = STATE_KEYS.get(names[0].split(" ", 1)[-1], "state%d" % (len(comps) + 1))
            c = {"type": "state", "key": key, "id": "%02X" % h[0], "names": {"en": names, "*": names}, "win_points": h[12],
                 "months": [h[3] // 8 + 1, h[4] // 8 + 1], "clubs": clubs,
                 "stages": state_stages(st, h, list(h[0x22:0x22 + nst])), "relegated": st["down"]}
            rel = state_relegation(world, code, st["relegation"], st["g"], strength_of,
                                   [pool_keys[i] for i in pools[w0][1]["order"]] if w0 in pools else [])
            if rel:
                c["relegation"] = rel
            if w0 in pools:
                c["reserve"] = {"off_file": True, "pool": [pool_keys[i] for i in pools[w0][1]["order"]]}
            comps.append(c)
            for k, s in zip(clubs, st["strengths"]):
                if strength_of(k) != s:
                    raise ValueError("%s %s: strength %d in SS_TAB, %d from its players" % (code, k, s, strength_of(k)))
        spares = []
        files = {f for f, c in world.code_of.items() if c == code}
        for f in ri or []:
            if f["world"] == WORLD and f["file"] in files and any(pl["kind"] == 1 for pl in f["pools"]):
                spares += [world.key[(f["file"], o)] for o in f["spares"]]
        out[code] = (comps, spares)
    return out


def decode_players(world):
    """players.csv rows {club, price}: every club's players' price bytes (the SWOS record's +32) -- its team-file
    record's 16 (TMW5.nnn), then its 17th.. (XTW5.nnn, the Mod's xtra.py) --, and the off-file pools' (RS5nnn.DAT:
    16 each, the extra players of a pool club are in no installed file)."""
    import world_from_mod as wfm
    out = {}
    for f, recs in sorted(world.records.items()):
        code = world.code_of.get(f)
        if not code:
            continue
        xp = os.path.join(world.data, "XTW%d.%03d" % (WORLD, f))
        xt = wfm.extras(xp, len(recs)) if os.path.exists(xp) else {}
        for i, r in enumerate(recs):
            key = world.key[(f, i)]
            ps = [r[76 + 38 * k:76 + 38 * (k + 1)] for k in range(16)] + list(xt.get(i, []))
            out.setdefault(code, []).extend({"club": key, "price": str(p[32])} for p in ps)
    seen = {(c, r["club"]) for c, rows in out.items() for r in rows}
    for x in sorted(os.listdir(world.data)):
        if not (x.upper().startswith("RS%d" % WORLD) and x.upper().endswith(".DAT")):
            continue
        f = int(x[3:6])
        code = world.code_of.get(f)
        if not code:
            continue
        recs = wfm.records(os.path.join(world.data, x))
        for (name, key), r in zip(world.pools[code], recs):
            if (code, key) in seen:
                continue
            seen.add((code, key))
            out.setdefault(code, []).extend({"club": key, "price": str(r[76 + 38 * k + 32])} for k in range(16))
    return out


def strength_fn(rows):
    """The importer's strength (world_states.strengths): the best 11 prices, at least 5."""
    by = {}
    for r in rows:
        by.setdefault(r["club"], []).append(int(r["price"] or 0))
    return lambda key: max(5, min(0xFFFF, sum(sorted(by.get(key, []), reverse=True)[:11])))


MONTH_DAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
CUPD_HOST = {0: "listed", 2: "drawn", 4: "alternate"}
CUPD_DEFAULT_NAMES = ["COPPA ITALIA SERIE D", "COPPA IT SERIE D"]     # (cupd.pack_table's, the pack giving none)


def week_date(w):
    """A career calendar week -> 'dd/mm[+1]', the first day split.week_of puts in it (world_import.week_of)."""
    y, r = divmod(w, 96)
    m, k = divmod(r, 8)
    d = next(d for d in range(1, MONTH_DAYS[m] + 1) if (d - 1) * 8 // MONTH_DAYS[m] == k)
    return "%02d/%02d%s" % (d, m + 1, "+%d" % y if y else "")


def parse_cd_tab(world, obj, off):
    """gplay's CD_TAB (cupd.compile_table): db world, rounds, team file, 0; per round db week1, week2, legs, host,
    ties, flags; dw first tie; per tie dw a, b, db winner, db 0. CD_NM follows: dd long, short (name offsets);
    then CD_DT: db first division, groups; dw base; 256 x db base division; dw map offset, map words, pool records;
    256 x dw strength; pool records x dw strength."""
    d = world.exe.obj[obj]
    wd, nr, lf, z = d[off:off + 4]
    if wd != WORLD or z or not 3 <= nr <= 12 or world.code_of.get(lf) is None:
        return None
    rounds, p, nt = [], off + 4, 0
    for i in range(nr):
        w1, w2, legs, host, n, flags = d[p:p + 6]
        first = struct.unpack_from("<H", d, p + 6)[0]
        if first != nt or legs not in (1, 2) or host not in CUPD_HOST or flags > 3 or not n:
            return None
        rounds.append((w1, w2, legs, host, n, flags, first))
        nt += n
        p += 8
    if nt > 168:
        return None
    ties = [struct.unpack_from("<HHBB", d, p + 6 * i)[:3] for i in range(nt)]
    p += 6 * nt
    nm = struct.unpack_from("<II", d, p)
    p += 8
    df, dc, gbase = struct.unpack_from("<BBH", d, p)
    base = list(d[p + 4:p + 260])
    p += 260
    moff, nmap, npool = struct.unpack_from("<HHH", d, p)
    p += 6
    st = list(struct.unpack_from("<256H", d, p))
    pst = list(struct.unpack_from("<%dH" % npool, d, p + 512))
    return {"file": lf, "rounds": rounds, "ties": ties, "names": nm, "first": df, "groups": dc, "base": base,
            "strength": st, "pool_strength": pst}


def decode_cupd(world, base):
    """cupd.json from CD_TAB (world_import.cupd_table, inverted): per round its date (the weeks), legs, host,
    pairing, rule (only 'pens' or not: flags bit 2) and every tie written out -- a club (its key; one outside the
    file: no key, as the table's 0), or the winner of a previous round's tie ('W<i>') --, the real winners known
    (played, winner = the home / away id). The ties' real dates, TM ids and the clubs outside the file are not in
    the exe."""
    e = world.exe
    cd = find_parsed(e, lambda o, a: parse_cd_tab(world, o, a), (1,))
    if cd is None:
        return None, None
    rounds, prev_first, prev_n = [], 0, 0
    for ri, (w1, w2, legs, host, n, flags, first) in enumerate(cd["rounds"]):
        date = week_date(w1) + (" " + week_date(w2) if legs == 2 and w2 else "")
        r = {"legs": legs, "date": date, "rule": ("two_legs" if legs == 2 else "single") + ("_pens" if flags & 2
                                                                                              else "_et"),
             "host": CUPD_HOST[host], "matches": []}
        if flags & 1:
            r["pairing"] = "draw"
        for k in range(n):
            a, b, win = cd["ties"][first + k]
            if flags & 1:
                if ri < 2:
                    r["matches"].append({})
                continue
            m = {}
            for side, v in (("home", a), ("away", b)):
                if v & 0xFF == 0xFF:
                    m[side + "_from"] = "W%d" % ((v >> 8) - prev_first)
                    m[side + "_key"] = None
                elif v:
                    m[side + "_key"] = world.key[(v & 0xFF, v >> 8)]
                else:
                    m[side + "_key"] = None
                m[side] = "%d%s" % (first + k, side[0])
            m["played"] = bool(win)
            m["winner"] = m["home"] if win == 1 else m["away"] if win == 2 else None
            r["matches"].append(m)
        rounds.append(r)
        prev_first, prev_n = first, n
    names = [name_at(e, base, x) for x in cd["names"]]
    out = {"rounds": rounds}
    if names != CUPD_DEFAULT_NAMES:
        out["names"] = {"en": names, "*": names}
    return out, cd


def decode(world):
    e = world.exe
    packs, ties = decode_cups(world)
    players = decode_players(world)
    strengths = {code: strength_fn(rows) for code, rows in players.items()}
    for code, (comps, spares) in decode_states(world, str_base(e), country_tables(world), strengths).items():
        pk = packs.setdefault(code, {"competitions": [], "season": {}})
        pk["competitions"] += comps
        if spares:
            pk["state_spares"] = spares
    out = {"packs": packs, "ties": ties, "players": players}
    cupd, cd = decode_cupd(world, str_base(e))
    if cupd:
        code = world.code_of[cd["file"]]
        out["cupd"] = {code: cupd}
        # its strengths (CD_DT, cupd.strengths: the best 11 prices) are the players' -- else the rows are wrong
        st = strengths[code]
        bad = [(o, s, st(world.key[(cd["file"], o)])) for o, s in enumerate(cd["strength"])
               if (cd["file"], o) in world.key and s != st(world.key[(cd["file"], o)])]
        pool = [k for _, k in world.pools.get(code, [])]
        bad += [(k, s, st(k)) for k, s in zip(pool, cd["pool_strength"]) if s != st(k)]
        if bad:
            raise ValueError("%s: Coppa Italia Serie D strengths differ from the players': %s" % (code, bad[:5]))
    return out
