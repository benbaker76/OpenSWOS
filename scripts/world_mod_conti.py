#!/usr/bin/env python3
"""world_mod_conti.py -- World 2026-27's other confederations' club cups (CONMEBOL, CONCACAF, AFC, CAF, OFC and the
FIFA Intercontinental) and their guest clubs, decoded from an installed SWOS 96/97 Mod (its patched DOS exe + DATA):
a fragment for scripts/world_from_mod.py (docs/WORLD_2026_27.md 6d).

    decode(world) -> {"conti": {"<conf>.format.json": {...}, "<conf>.access.json": {...},
                                "<conf>.guests.json": {...}, "afc.acle_league.json": {...}}}

The Mod's tools/conti.py load() compiles Davide's conti/<conf>.{format,access,guests}.json (+ a league stage's
<conf>.<cup>_league.json) into one spec (conti.c's CT_SPEC); scripts/world_conti.py is our copy of that load().
This module inverts it. Where each part lives (found through code references, never by address):
- the spec: the conti.c glue's ct_prep reads the cup names as `push dword [ct_snames + eax*4]` /
  `mov eax, [ct_names + eax*4]` (12 cups: the count is the gap between the two tables) and the round names as
  `mov al, [ct_rn + eax]` (16 bytes per cup: rnames' CT_IDX); the spec is the obj1 pointer target that parses as a
  spec of that many cups (header: cups, associations, CONTI_FORCE 0xFF 0xFF);
- the spec's parts: per cup 4 + 24 bytes a round (kind, legs, level, neutral host, draw, association rule, groups,
  size, rounds, month, next round, losers' round, the four places' rounds, holder in A1, main round, places, the top-n
  range, the best of a place, the short-group flag), the access entries (association, cup, round, code), the holders
  (source cup, cup, round | flags, slot), each association's team file (0: a guest association), the ranking (empty:
  the Mod's keys never matched its words before 4.1.1, which moved it out of the spec), the guests (team word,
  association, strength), each association's league-stage region, the world's files, season 1's holders, the group
  contests' ids;
- the names: ct_names / ct_snames hold offsets from the game's string base (sacups.STR_BASE) -- the long name as
  the data (upper case, 30 at most), the short one the calendar's fitted label (complist.fit_short), NOT the data's;
- the round keys: rnames' indices (CT_KEYS: league, po, r16, pre, r1, qf, sf, final, groups) or names appended past
  the game's (CT_NEW: q1 q2 q3 first qual, by their English long name; the Intercontinental's own names), read from
  the English name strings rnames adds (LEAGUE PHASE ..., long / medium / short blocks);
- the guests: DATA/TMW5.191 (CAF), .192 (AFC), .193 (OFC), .194 (CONCACAF) (guests.FILES), one 684-byte record a
  club in the data's order (guests.record): name, tactic, kits, coach, lineup, 16 players (mkseason
  _explicit_player: nationality, number, name, position | skin, the skills' nibbles with the key-skill bit, price).
Formats from the Mod's tools/conti.py, guests.py, rnames.py, mkseason.py (MIT, Davide Lorigliola) -- ported, not
imported. Standard library only (scripts/world_conti.py, ours, for the 'rest' check of an 'outside').

Not in the installed files (compile to nothing in the importer unless said): notes, sources, a host other than
neutral / centralised (one bit), a draw's pots / pot_by / ties content beyond its kind and association rule, the
months after a group stage's first, a holder's group (A1: one bit), the access codes 0 and 1 (both compile to 0),
the ranking's rank and the clubs ranked outside the world (only the world's clubs and their rounded points are
compiled, in 4.1.1 as a table of its own: ranking_table), the guests' "club" / "role" / "source" / "access", players past the 16th, the
guest associations' FIFA codes (only their order and each one's clubs are compiled: named here A1, A2, ...), the
league stage's pot layout and its regions' names (only each association's region: named West / East here), the
cups' short names (only the fitted calendar label is in the exe; the importer's kWorld..ContiCups short name uses
the data's), which side of a losers' route the data wrote it on (format 'losers' or access 'from': both given),
the cups' keys (CUP_KEYS: the Mod's tools' and world_conti's names; 'rec' / 'sc' are load-bearing, world_conti's
FIXES) and the Intercontinental's round keys (r1 r2 playoff final: world_conti._ic_split's), the guests' keys (their
name, lower case, letters and digits -- true of every 4.0.5 guest), a 'rest' outside (given as 'rest' when load()'s
fill is exactly that, else counts), and an 'outside' of a cup whose guests have season-1 places (load() ignores it).
4.1.1 compiles 4.0.6's Recopa / CAF Super Cup months and its split Intercontinental (r2a / r2b) into the spec: the
split is joined back to the data's r2 (its own long name is gone then: the short one is given); the months stay.
"""
import collections
import contextlib
import copy
import io
import json
import os
import re
import struct

CONFS = ["conmebol", "concacaf", "afc", "caf", "ofc", "fifa"]   # (conti.CONFS: the cups' order in the spec)
GUEST_FILES = {191: "caf", 192: "afc", 193: "ofc", 194: "concacaf"}   # (guests.FILES)
RB = 24
OUT, CHAMP = 0xFF, 0xFE
LEVEL = {0: "pens", 1: "et_pens", 2: "away_pens", 3: "away_pens"}     # (3: away_pens + extra_time)
ASSOC = {0: "allow", 1: "no", 2: "no_unless_via"}
CODE = {0x40: "next", 0x41: "ap", 0x42: "cl", 1: "cup"}
POS = ["G", "RB", "LB", "D", "RW", "LW", "M", "A"]                 # (mkseason EXPLICIT_POS: byte 26 bits 5-7)
SKIN = ["light", "ginger", "dark", "darkblond"]                     # (bits 3-4)
SKILLS = ["pa", "ve", "he", "ta", "co", "sp", "fi"]
ROLE = {"G": "G", "RB": "D", "LB": "D", "D": "D", "RW": "M", "LW": "M", "M": "M", "A": "A"}   # (mkseason EXPLICIT_ROLE)
# rnames: name indices of the format keys (CT_KEYS), and CT_NEW's English long names
N_OLD = 12
CT_KEYS = {12: "league", 13: "po", 14: "r16", 16: "pre", 21: "r1", 24: "qf", 25: "sf", 26: "final", 34: "groups"}
CT_NEW = {"FIRST QUALIFYING ROUND": "q1", "SECOND QUALIFYING ROUND": "q2", "THIRD QUALIFYING ROUND": "q3",
          "FIRST STAGE": "first", "QUALIFYING STAGE": "qual"}
IC_KEYS = ["r1", "r2", "playoff", "final"]     # (the stepladder's rounds: world_conti._ic_split reads r2 / playoff)


# ---- the exe -----------------------------------------------------------------------------------------------------

def _tables(e):
    """(ct_names, ct_snames, ct_rn, cups) from ct_prep's code."""
    d = e.obj[1]
    hits = []
    for m in re.finditer(rb"\xff\x34\x85....\x8b\x04\x85....", d, re.S):
        o = m.start()
        sn, nm = e.ptr.get((1, o + 3)), e.ptr.get((1, o + 10))
        k = d.find(b"\x8a\x80", o, o + 96)
        rn = e.ptr.get((1, k + 2)) if k >= 0 else None
        if sn and nm and rn and sn[0] == nm[0] and 0 < sn[1] - nm[1] <= 64:
            hits.append((nm, sn, rn, (sn[1] - nm[1]) // 4))
    assert len(hits) == 1, ("conti: ct_prep not found once", hits)
    return hits[0]


def _str_base(e, rels):
    """The base of the contest name offsets: the one that puts every name at the start of a string."""
    o2 = e.obj[2]
    ok = lambda a: 0 < a < len(o2) and o2[a - 1] == 0 and 0x20 < o2[a] < 0x7F
    r0 = rels[0]
    cands = []
    a = o2.find(b"\0", 0)
    while a >= 0 and a + 1 < len(o2):
        base = a + 1 - r0
        if base >= 0 and all(ok(base + r) for r in rels):
            cands.append(base)
        a = o2.find(b"\0", a + 1)
    assert len(cands) == 1, ("conti: string base", cands[:5])
    return cands[0]


def _parse(b, nc):
    """The spec (conti.load's layout); -> dict, or None when b does not parse as nc cups."""
    try:
        if b[0] != nc or b[2:4] != b"\xff\xff":
            return None
        na, o, cups = b[1], 4, []
        for _ in range(nc):
            n = b[o]
            if not 1 <= n <= 15 or b[o + 1:o + 4] != b"\0\0\0":
                return None
            o += 4
            rs = [bytes(b[o + RB * j:o + RB * (j + 1)]) for j in range(n)]
            if any(r[0] > 1 for r in rs):
                return None
            cups.append(rs)
            o += RB * n
        n = struct.unpack_from("<H", b, o)[0]
        o += 2
        acc = [tuple(b[o + 4 * i:o + 4 * i + 4]) for i in range(n)]
        o += 4 * n
        n = b[o]
        o += 1
        hol = [tuple(b[o + 4 * i:o + 4 * i + 4]) for i in range(n)]
        o += 4 * n
        files = list(b[o:o + na])
        o += na
        n = struct.unpack_from("<H", b, o)[0]
        o += 2
        rank = [struct.unpack_from("<HH", b, o + 4 * i) for i in range(n)]
        o += 4 * n
        n = struct.unpack_from("<H", b, o)[0]
        o += 2
        gl = [struct.unpack_from("<HBBI", b, o + 8 * i) for i in range(n)]
        o += 8 * n
        reg = list(b[o:o + na])
        o += na + 32
        s1 = [struct.unpack_from("<HBBI", b, o + 8 * i) for i in range(nc)]
        o += 8 * nc
        gids = list(b[o:o + nc])
        o += nc + 1
    except (IndexError, struct.error):
        return None
    if any(a[0] >= na and a[0] != 0xFE or a[1] >= nc for a in acc) or any(h[1] >= nc for h in hol):
        return None
    return {"na": na, "cups": cups, "acc": acc, "hol": hol, "files": files, "rank": rank, "gl": gl, "reg": reg,
            "s1": s1, "gids": gids, "len": o}


def find_spec(e, nc):
    """(obj1 offset, parsed spec): the pointer target that parses as nc cups (conti.c's CT_SPEC)."""
    o1 = e.obj[1]
    found = {}
    for (to, t) in e.refs:
        if to == 1 and t not in found and t + 4 < len(o1) and o1[t] == nc and o1[t + 2:t + 4] == b"\xff\xff":
            p = _parse(o1[t:t + 0x4000], nc)
            if p:
                found[t] = p
    assert len(found) == 1, ("conti: the spec", sorted(found))
    return next(iter(found.items()))


def round_names(e):
    """rnames' names past the game's 12, English: [(long, medium, short)] by index - N_OLD (three blocks of strings
    in obj2 from LEAGUE PHASE: the long names, then the medium (from LEAGUE PH.), then the short)."""
    o2 = e.obj[2]
    a = o2.find(b"LEAGUE PHASE\0KNOCKOUT PLAY-OFFS\0")
    assert a > 0 and o2[a - 1:a] != b"", "conti: rnames' names"
    out = []
    while True:
        s = e.cstr(2, a)
        out.append(s.decode("latin1"))
        a += len(s) + 1
        if s == b"" or len(out) > 600:
            break
    n = out.index("LEAGUE PH.")
    return [(out[i], out[n + i], out[2 * n + i]) for i in range(n)]


# ---- the guests --------------------------------------------------------------------------------------------------

def _text(b):
    return b.split(b"\0")[0].decode("latin1")


SLOT_POS = [0, 1, 3, 3, 2, 4, 6, 6, 5, 7, 7]                         # (mkseason.auto_lineup: the 4-4-2 starters)
POS_CLASS = {0: 0, 1: 1, 2: 1, 3: 1, 4: 2, 5: 2, 6: 2, 7: 3}


def auto_lineup(positions):
    """The lineup a club without one gets from its players' positions (mkseason.auto_lineup)."""
    used, start = set(), [None] * 11
    for exact in (True, False):
        for k, want in enumerate(SLOT_POS):
            if start[k] is not None:
                continue
            for j, ps in enumerate(positions):
                if j not in used and (ps == want if exact else POS_CLASS[ps] == POS_CLASS[want]):
                    start[k] = j
                    used.add(j)
                    break
    for k in range(11):
        if start[k] is None:
            start[k] = next(j for j in range(16) if j not in used)
            used.add(start[k])
    return start + [j for j in range(16) if j not in used]


def guest_club(r):
    """One guest record (guests.record) -> the data's club (the fields guests / world_conti read)."""
    players = []
    for j in range(16):
        q = r[76 + 38 * j:76 + 38 * (j + 1)]
        nib = [q[28 + i // 2] >> (4 * (1 - i % 2)) & 15 for i in range(8)]
        p = {"name": _text(q[3:26]), "role": ROLE[POS[q[26] >> 5]], "number": q[2], "nat": q[0],
             "position": POS[q[26] >> 5], "skin": SKIN[q[26] >> 3 & 3]}       # (role: the Mod's guests.record)
        for k, sk in enumerate(SKILLS):
            v = nib[k + 1]
            p[sk] = str(v & 7) + ("*" if v & 8 else "")
        p["price"] = q[32]
        players.append(p)
    name = _text(r[5:22])
    c = {"key": re.sub(r"[^a-z0-9]", "", name.lower()), "name": name, "coach": _text(r[36:59]),
         "tactic": r[24], "kit1": " ".join(str(x) for x in r[26:31]), "kit2": " ".join(str(x) for x in r[31:36])}
    if list(r[60:76]) != auto_lineup([r[76 + 38 * j + 26] >> 5 for j in range(16)]):
        c["lineup"] = " ".join(str(x) for x in r[60:76])      # (none in the data: the positions' own)
    c["players"] = players
    return c


GUEST_IDS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "world_guest_ids_2627.csv")


def guest_key(world, f, i, name, derived):
    """A guest club's key -- its id, "guest:<key>" (world_conti.guest_entry), so for ever: the registry's
    (scripts/world_guest_ids_2627.csv, made from 4.0.5) by its name in its file, else (a rename: 4.1's O&M ->
    O Y M) by its place when that row's name is nobody's now; a club in no row: derived from its name."""
    import csv
    rows = [r for r in csv.DictReader(line for line in open(GUEST_IDS, encoding="utf-8") if not line.startswith("#"))
            if int(r["file"]) == f]
    for r in rows:
        if r["name"] == name:
            return r["key"]
    names = {_text(rec[5:22]) for rec in world.records[f]}
    for r in rows:
        if int(r["ordinal"]) == i and r["name"] not in names:
            return r["key"]
    return derived


# ---- the cups ----------------------------------------------------------------------------------------------------

def _round_keys(rn, names, n, stepladder):
    """A cup's round keys from its rnames indices."""
    if stepladder:
        return list(IC_KEYS) if n == 4 else IC_KEYS[:1] + ["r2a", "r2b"] + IC_KEYS[2:]
    keys = []
    for j in range(n):
        i = rn[j]
        k = CT_KEYS.get(i) or (CT_NEW.get(names[i - N_OLD][0]) if i >= N_OLD and i - N_OLD < len(names) else None)
        keys.append(k or "round%d" % (j + 1))
    return keys


def _target(t, ci, keys):
    """cup << 4 | round -> 'rk' (this cup) / 'ck:rk'."""
    cj, j = t >> 4, t & 15
    return keys[cj][1][j] if cj == ci else "%s:%s" % (keys[cj][0], keys[cj][1][j])


def _draw(b, kind):
    if kind == 1:
        d = {"same_assoc": ASSOC.get(b[5], b[5])}
        if b[16]:
            d["holder"] = True
        return d
    dr, rule = b[4], b[5]
    if dr == 8:
        return {"kind": "fixed_by_place"}
    if dr == 2:
        return {"kind": "same_assoc_pairs"}
    if dr == 3:
        return {"kind": "seeded_1v2", "same_assoc": ASSOC.get(rule, rule)}
    if dr == 6:
        return {"kind": "fixed", "ties": True}       # (the ties themselves: not compiled)
    if dr == 4:
        return "bracket"
    if dr == 1:
        return {"pots": 2, "same_assoc": ASSOC.get(rule, rule)}
    if rule:
        return {"same_assoc": ASSOC.get(rule, rule)}
    return None


def _round(b, key, keys, ci, stepladder):
    kind = b[0]
    rd = {"key": key, "kind": "ko" if kind == 0 else "league_stage" if key == "league" else "groups"}
    if kind == 0 or b[1]:
        rd["legs"] = b[1]
    if kind == 0:
        rd["level"] = LEVEL[b[2]]
        if b[2] == 3:
            rd["extra_time"] = True
    if b[3]:
        rd["host"] = "neutral"
    if not stepladder:
        d = _draw(b, kind)
        if d is not None:
            rd["draw"] = d
    if kind == 1:
        if rd["kind"] == "league_stage":         # (world_conti._norm: groups = regions, size 16, one round)
            rd["regions"] = b[6]
            if b[7] != 16:
                rd["size"] = b[7]
            if b[8] != 1:
                rd["rounds"] = b[8]
        else:
            rd["groups"], rd["size"], rd["rounds"] = b[6], b[7], b[8]
        rd["months"] = [b[9]]
        out = {}
        for q in range(4):
            if b[12 + q] != OUT:
                out[str(q + 1)] = _target(b[12 + q], ci, keys)
        if b[19]:
            out["1-%d" % b[19]] = _target(b[20], ci, keys)
        if b[21]:
            out[str(b[21])] += "_best"
        rd["out"] = out
    else:
        rd["month"] = b[9]
    return rd


def _confs(p, stepladders, nc, gconf):
    """Each cup's confederation: the stepladder FIFA's; the others grouped by what links them (associations,
    holders, routes), each group by its guests' file (guests.FILES), the one without guests CONMEBOL's -- in
    conti.CONFS order."""
    parent = list(range(nc))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def join(a, b):
        if a < nc and b < nc and not stepladders[a] and not stepladders[b]:
            parent[find(a)] = find(b)
    by_assoc = collections.defaultdict(set)
    for a, ci, j, k in p["acc"]:
        by_assoc[("g", gconf[k]) if a == 0xFE else a].add(ci)
    for cs in by_assoc.values():
        cs = sorted(cs)
        for c in cs[1:]:
            join(cs[0], c)
    for si, ci, _, _ in p["hol"]:
        join(si, ci)
    for ci, rs in enumerate(p["cups"]):
        for b in rs:
            for t in [b[10], b[11]] + list(b[12:16]) + ([b[20]] if b[19] else []):
                if t not in (OUT, CHAMP):
                    join(ci, t >> 4)
    conf = {}
    for ci in range(nc):
        if stepladders[ci]:
            conf[ci] = "fifa"
    for a, ci, j, k in p["acc"]:
        if a == 0xFE:
            conf.setdefault(find(ci), gconf[k])
    out = []
    for ci in range(nc):
        out.append(conf.get(ci) or conf.get(find(ci)) or "conmebol")
    order = [CONFS.index(c) for c in out]
    assert order == sorted(order), ("conti: confederations out of order", out)
    return out


def ranking_table(world):
    """{(code, key): points} of the club ranking the Mod 4.1.1 moved out of the spec (its tools/conti.py d4_patch:
    `u16 count`, then count x (u16 team word, u16 points), the words ascending, in obj4; conti.c's pointer CT_RK in
    obj1's data points at it), or {} for an exe without one (4.0.5, 4.1: the spec's own ranking, empty). Found as
    the one pointer target that parses as such a table of the world's clubs, never by address; a word of no team
    file is left out. The points are the rounded ones conti.py compiled (world_conti.ranking rounds them again: the
    same integers)."""
    e = world.exe
    found = {}
    for (to, toff), srcs in e.refs.items():
        o = e.obj.get(to)
        if o is None or toff + 2 > len(o) or not any(so == 1 for so, _ in srcs):
            continue
        n = struct.unpack_from("<H", o, toff)[0]
        if not 8 <= n <= 4000 or toff + 2 + 4 * n > len(o):
            continue
        rows = [struct.unpack_from("<HH", o, toff + 2 + 4 * i) for i in range(n)]
        if any(rows[i][0] >= rows[i + 1][0] for i in range(n - 1)) or any(v == 0 for _, v in rows):
            continue
        clubs = [world.word(w) for w, _ in rows]
        named = {c: v for c, (_, v) in zip(clubs, rows) if c and c[0]}
        if len(named) * 10 < n * 9:     # (a word of no team file: a club the Mod keeps off its files -- 4.1.1's
            continue                    # table has one, file 253 -- has no club of ours to rank)
        found[(to, toff)] = named
    if len(found) > 1:
        raise ValueError("conti ranking: %d tables parse as the club ranking: %s" % (len(found), sorted(found)))
    return next(iter(found.values())) if found else {}


def decode(world):
    e = world.exe
    nm, sn, rnt, nc = _tables(e)
    rels = [struct.unpack_from("<I", e.obj[nm[0]], nm[1] + 4 * i)[0] for i in range(nc)]
    srels = [struct.unpack_from("<I", e.obj[sn[0]], sn[1] + 4 * i)[0] for i in range(nc)]
    base = _str_base(e, rels + srels)
    longs = [e.cstr(2, base + r).decode("latin1") for r in rels]
    shorts = [e.cstr(2, base + r).decode("latin1") for r in srels]
    rn = [list(e.obj[rnt[0]][rnt[1] + 16 * ci:rnt[1] + 16 * ci + 16]) for ci in range(nc)]
    _, p = find_spec(e, nc)
    names = round_names(e)

    # the guests: team words -> (conf, ordinal, club)
    gl, gconf, gclub = p["gl"], {}, {}
    files = {}
    for gi, (w, a, _, st) in enumerate(gl):
        f, i = w & 0xFF, w >> 8
        gconf[gi] = GUEST_FILES[f]
        rec = world.records[f][i]
        files.setdefault(f, {})[i] = (gi, a)
        gclub[gi] = guest_club(rec)
        gclub[gi]["key"] = guest_key(world, f, i, gclub[gi]["name"], gclub[gi]["key"])
    # associations: a world country's league file -> its code; guests' -> A1, A2 ... (their codes are not compiled)
    gass = {}
    assocs = []
    for ai, f in enumerate(p["files"]):
        if f and f in world.code_of:
            assocs.append(world.code_of[f].lower())
        elif f == 0:
            gass[ai] = "A%d" % (len(gass) + 1)
            assocs.append("g:" + gass[ai])
        else:
            assocs.append("x%d" % ai)     # (an association outside the world with no guests: not in this build)

    stepladders = [all(b[4] == 7 for b in rs) for rs in p["cups"]]
    conf_of = _confs(p, stepladders, nc, gconf)
    rkeys = []
    for ci in range(nc):
        rk = _round_keys(rn[ci], names, len(p["cups"][ci]), stepladders[ci])
        rkeys.append(rk)
    # cup keys: not compiled; from the long name's initials when needed (the data's keys are its own) -- the
    # importer and the format files only need them unique within a confederation
    ckeys = _cup_keys(longs, conf_of)
    keys = list(zip(ckeys, rkeys))

    fmt = {c: {"conf": c, "cups": []} for c in CONFS}
    acc = {c: {"conf": c, "ranking": {}, "cups": []} for c in CONFS}
    s1 = {c: {} for c in CONFS}
    xfrom = collections.defaultdict(dict)       # cup -> {'src:rk_losers': round}
    for ci in range(nc):
        rs = p["cups"][ci]
        c = {"key": ckeys[ci], "names": {"en": [longs[ci], shorts[ci]]}}
        if stepladders[ci]:
            c["stepladder"] = True
        rounds = [_round(b, rkeys[ci][j], keys, ci, stepladders[ci]) for j, b in enumerate(rs)]
        for j, b in enumerate(rs):              # (round names the data gave: the Intercontinental's)
            i = rn[ci][j]
            if stepladders[ci] and i >= N_OLD and i - N_OLD < len(names):
                rounds[j]["names"] = {"en": [names[i - N_OLD][0], names[i - N_OLD][1]]}
            if stepladders[ci]:
                rounds[j]["_places"] = b[18]     # (_ic_join: r2's ties)
        losers = {}
        for j, b in enumerate(rs):
            if b[11] != OUT:
                t = _target(b[11], ci, keys)
                losers[rkeys[ci][j]] = t if ":" in t else "%s:%s" % (ckeys[ci], t)
                cj = b[11] >> 4
                xfrom[cj]["%s:%s_losers" % (ckeys[ci], rkeys[ci][j])] = keys[cj][1][b[11] & 15]
            default = CHAMP if j == len(rs) - 1 else (ci << 4 | (j + 1))
            if b[0] == 0 and b[10] != default:
                rounds[j]["winners"] = _target(b[10], ci, keys)
        if losers:
            c["losers"] = losers
        c["rounds"] = rounds
        fmt[conf_of[ci]]["cups"].append(c)

    # the Intercontinental of 4.0.6 on (r2a / r2b): back to the data's one r2 (world_conti._ic_split splits it)
    for c in fmt["fifa"]["cups"]:
        _ic_join(c)

    hol = collections.defaultdict(list)
    for h in p["hol"]:
        hol[h[1]].append(h)
    for ci in range(nc):
        conf, ck = conf_of[ci], ckeys[ci]
        a = {"key": ck}
        ents = [x for x in p["acc"] if x[1] == ci]
        mine, plain, guests, tail = _split_access(ents)
        ac = {}
        for x in plain:
            ac.setdefault(assocs[x[0]], []).append(
                [rkeys[ci][x[2]], CODE.get(x[3], x[3])])
        ac = {k: v for k, v in ac.items()}
        if tail:
            ac["outside"] = dict(collections.Counter(rkeys[ci][x[2]] for x in tail))
        hs = hol.get(ci, [])
        if stepladders[ci]:
            ac["holders"], a["rotation"] = _ic_holders(hs, ckeys, conf_of, rkeys[ci])
        elif hs:
            ac["holders"] = {ckeys[h[0]]: rkeys[ci][h[2] & 0x7F] for h in hs}
            own = all(h[2] & 0x80 for h in hs)
            if own != (conf == "caf"):
                a["holder_rule"] = "own_place" if own else "extra"
        if xfrom.get(ci):
            ac["from"] = xfrom[ci]
        a["access"] = ac
        clubs = {rkeys[ci][j]: b[18] for j, b in enumerate(p["cups"][ci]) if b[23]}
        if clubs:
            a["clubs"] = clubs
        for x in guests:
            s1[conf].setdefault(ck, {}).setdefault(rkeys[ci][x[2]], []).append(gclub[x[3]]["key"])
        acc[conf]["cups"].append(a)
    for c in acc["fifa"]["cups"]:
        _ic_join_access(c)
    # the club ranking (4.1.1: the exe's table CT_RK, out of the spec): each club in the access file of the
    # confederation whose cups its association enters
    conf_of_assoc = {}
    for ci in range(nc):
        for x in p["acc"]:
            if x[1] == ci and x[0] < len(assocs):
                conf_of_assoc.setdefault(assocs[x[0]], conf_of[ci])
    for (code, key), points in sorted(ranking_table(world).items()):
        conf = conf_of_assoc.get(code.lower())
        if conf is None:
            raise ValueError("conti ranking: %s/%s, an association no cup's access names" % (code, key))
        acc[conf]["ranking"]["%s/%s" % (code.lower(), key)] = {"points": points}

    out = {}
    for conf in CONFS:
        if fmt[conf]["cups"]:
            out["%s.format.json" % conf] = fmt[conf]
            out["%s.access.json" % conf] = acc[conf]
    for f, conf in sorted(GUEST_FILES.items()):
        if f not in files:
            continue
        clubs = []
        for i in sorted(files[f]):
            gi, ai = files[f][i]
            c = dict(gclub[gi])
            c["assoc"] = gass.get(ai, assocs[ai])
            clubs.append(c)
        out["%s.guests.json" % conf] = {"conf": conf, "season1": s1[conf], "clubs": clubs}
    # a league stage's regions (<conf>.<cup>_league.json): each association's region
    for ci in range(nc):
        for j, b in enumerate(p["cups"][ci]):
            if b[0] == 1 and rkeys[ci][j] == "league":
                regs = collections.OrderedDict()
                for ai, r in enumerate(p["reg"]):
                    if r != 0xFF:
                        regs.setdefault(r, []).append(gass.get(ai) or assocs[ai].upper())
                labels = ["West", "East"] if len(regs) == 2 else ["R%d" % (k + 1) for k in range(len(regs))]
                out["%s.%s_league.json" % (conf_of[ci], ckeys[ci])] = {"regions": {
                    labels[r]: {"pots": [[{"assoc": x} for x in xs]]} for r, xs in sorted(regs.items())}}
    _outside_rest(out)
    return {"conti": out}


# the data's cup keys are not compiled: the keys the Mod's own tools (zones.py, confed2627.py, conti.py) and
# scripts/world_conti.py (FIXES: 'rec', 'sc' -- load-bearing, the importer fixes those cups' first rounds by key;
# the Intercontinental: 'ic', its CAF holder 'cafcl' = caf 'cl') call the cups by, from their long names; any other
# cup: its name's initials
CUP_KEYS = {"COPA LIBERTADORES": "lib", "COPA SUDAMERICANA": "sud", "RECOPA SUDAMERICANA": "rec",
            "CONCACAF CHAMPIONS CUP": "ccc", "AFC CHAMPIONS LEAGUE ELITE": "acle", "AFC CHAMPIONS LEAGUE TWO": "acl2",
            "CAF CHAMPIONS LEAGUE": "cl", "CAF CONFEDERATION CUP": "cc", "CAF SUPER CUP": "sc", "OFC CHAMPIONS LEAGUE": "ocl",
            "INTERCONTINENTAL CUP": "ic", "AFC CHALLENGE LEAGUE": "acgl"}   # (acgl: the data's key, as its guests' season1)


def _cup_keys(longs, conf_of):
    out = []
    for nm, conf in zip(longs, conf_of):
        k = CUP_KEYS.get(nm) or "".join(w[0] for w in nm.lower().split() if w[0].isalnum())
        while (conf, k) in [(conf_of[i], x) for i, x in enumerate(out)]:
            k += "x"
        out.append(k)
    return out


def _split_access(ents):
    """A cup's access entries -> (its associations in the data's order, the listed entries, the guests' entries,
    the outside places' entries): load() appends the lists association by association, then the guests (0xFE),
    then the outside places, each the next association of the list in turn ('next')."""
    guests = [x for x in ents if x[0] == 0xFE]
    rest = [x for x in ents if x[0] != 0xFE]
    if guests:
        return [], rest, guests, []

    def grouped(xs):
        seen, last = [], None
        for x in xs:
            if x[0] != last:
                if x[0] in seen:
                    return None
                seen.append(x[0])
                last = x[0]
        return seen
    for s in range(len(rest) + 1):              # the longest tail that is the outside cycle
        mine = grouped(rest[:s])
        if not mine:
            continue
        tail = rest[s:]
        if all(x[3] == 0x40 and x[0] == mine[q % len(mine)] for q, x in enumerate(tail)) and \
                all(a[2] <= b[2] for a, b in zip(tail, tail[1:])):
            return mine, rest[:s], [], tail
    return grouped(rest) or [], rest, [], []


def _ic_holders(hs, ckeys, conf_of, rk):
    """The Intercontinental's holders and rotation (conti._ic_holders inverted)."""
    name = lambda si: "uefa:cl" if si == 0xFF else ("cafcl" if (conf_of[si], ckeys[si]) == ("caf", "cl")
                                                    else ckeys[si])
    holders, rot = {}, {}
    for si, _, r, slot in hs:
        par = "even" if r & 0x40 else "odd" if r & 0x20 else None
        k = rk[r & 0x1F]
        if par:
            rot.setdefault(par, {}).setdefault(k, []).append(name(si))
            if par == "even":
                holders[name(si)] = k
        else:
            holders[name(si)] = k
    order = {"uefa:cl": 0}
    return dict(sorted(holders.items(), key=lambda kv: order.get(kv[0], 1))), rot


def _ic_join(c):
    """r2a + r2b (4.0.6's split, world_conti._ic_split) -> the data's r2."""
    rs = c["rounds"]
    keys = [r["key"] for r in rs]
    if "r2a" not in keys:
        r2 = next((r for r in rs if r["key"] == "r2"), None)
        if r2 is not None:
            r2["ties"] = [{} for _ in range(r2["_places"] // 2)]    # (their pairs are not compiled)
        for r in rs:
            r.pop("_places", None)
        return
    j = keys.index("r2a")
    a, b = rs[j], rs[j + 1]
    r2 = {k: v for k, v in a.items() if k not in ("winners", "names", "_places")}
    r2["key"] = "r2"
    r2.pop("host", None)
    r2["ties"] = [{} for _ in range((a["_places"] + b["_places"]) // 2)]
    for r in rs:
        r.pop("_places", None)
    na, nb = (a.get("names") or {}).get("en"), (b.get("names") or {}).get("en")
    if na and nb:
        r2["names"] = {"en": [na[1], na[1]]}    # (the data's own long name of r2 is not compiled from 4.0.6 on)
        r2["tie_names"] = {"aap": {"en": na[0]}, "americas": {"en": nb[0]}}
    rs[j:j + 2] = [r2]


def _ic_join_access(c):
    h = (c.get("access") or {}).get("holders") or {}
    for k, v in list(h.items()):
        if v in ("r2a", "r2b"):
            h[k] = "r2"
    for side in (c.get("rotation") or {}).values():
        for rk in ("r2a", "r2b"):
            if rk in side:
                side.setdefault("r2", []).extend(side.pop(rk))


def _outside_rest(out):
    """An access file's 'outside' as the data most likely wrote it: 'rest' when load()'s 'rest' fills exactly these
    places (scripts/world_conti.py's fields(), on load()'s known places), else the counts per round."""
    try:
        import world_conti as wc
    except ImportError:
        return
    files = {k: v for k, v in out.items()}
    old_read, old_files = wc._read, wc._Guests.FILES
    wc._read = lambda conf, kind: copy.deepcopy(files.get("%s.%s.json" % (conf, kind)))
    try:
        wc.FIELDS.clear()
        with contextlib.redirect_stdout(io.StringIO()):    # (load()'s reports: the check script's to show)
            wc.load(wc.CONFS)
        for name, doc in out.items():
            if not name.endswith(".access.json"):
                continue
            conf = name.split(".")[0]
            fm = files["%s.format.json" % conf]
            for a in doc["cups"]:
                o = (a.get("access") or {}).get("outside")
                if not isinstance(o, dict) or (conf, a["key"]) not in wc.FIELDS:
                    continue
                size, outside, known = wc.FIELDS[(conf, a["key"])]
                c = copy.deepcopy(next(x for x in fm["cups"] if x["key"] == a["key"]))
                wc._norm(c)
                for rk, n in (a.get("clubs") or {}).items():
                    for r in c["rounds"]:
                        if r["key"] == rk:
                            r["clubs"] = n
                if wc.fields(c, known, "rest") == (size, outside):
                    a["access"]["outside"] = "rest"
    finally:
        wc._read = old_read
        wc._Guests.FILES = old_files
        wc.FIELDS.clear()
