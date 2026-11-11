"""world_mod_splits.py -- the leagues' mid-season splits and stages, decoded from an installed SWOS 96/97 Mod's exe
(docs/WORLD_2026_27.md 6d; scripts/world_mod_exe.py is the base).

What the Mod's tools/mkseason.py (splits) + tools/split.py (entry, patch) compile into obj1 is inverted here: the
split code's table SP_TAB, an entry per division with a split or meetings of its own -- the Mod's tools are MIT,
Davide Lorigliola; the layout below is theirs (split.py ASM's comment, entry()):

  entry: +0 world, +1 team file, +2 division, +3 clubs, +4 cycles, +5 days before, +6 days after, +7 stages,
         +8 word days' counts, +10 word days' first pair, +12 word pairs, +14 word entry length (from the entry),
         +16 the stages, 28 bytes each: first day (after the days before), groups, flags (1 reset, 2 keep, 4 a
         fixture list by rows, 8 members, 10h tiebreak wins, 20h explicit places), -, (first place, size, points)
         x 4, word matrix, word places permutation, word members, db callback (1+: SP_CB), db table (1+), db
         annual, -, word weeks (stage 0: a byte per day).
  pairs: (a, b, flag) per match; a group stage's are places 0-based in the frozen table, a fixture stage's are
         the division's rows (its clubs in team-file order).
  the table ends with an entry of length 0.

SP_TAB is found through the code, never by an offset: the stage callback's `call [SP_CB + eax * 4 - 4]` (its
disp32 a fixup) gives SP_CB, the after-stage routines' dwords; after them `sp_nostage: ret`, then (aligned) SP_TAB.
Each SP_CB routine is gplay's `ba_<name>: mov dword [GP_BPI], i` -- i the league's play-off index the stage's end
builds; the name itself is not in the exe (see then_name()).

Not recoverable (not in the installed files): the group and stage titles; the order of the clubs inside a zone
(the exe keeps each row's zone: the zones come out in team-file order); the 'then' names (decoded as
"<cc>_ba<i>", i the play-off index -- the play-offs' decoder must give that play-off the same build_after);
the exact 'weeks' dates (a week byte is 8 a month: the first day of that week is written); whether a split sat at
the league's level or in its first division (the importer reads both alike: a league of one division gets the
league's, more divisions each their own); the league-level 'tiebreak' ('points_before_rounding', not used).
"""
import struct

POINTS = {0: "full", 1: "half_up", 2: "half_down", 3: "reset", 4: "h2h_half"}
ROUNDS = {0: "none", 1: "single", 2: "double", 3: "triple", 4: "quadruple"}
MONTH_DAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
SR = 28


def then_name(code, index):
    """The 'then' name of a stage whose end builds the league's play-off `index` (the play-offs' build_after)."""
    return "%s_ba%d" % (code.lower(), index)


def date_of(week):
    """A career calendar week byte -> 'dd/mm' (+'+y'): the first day of that week (split.week_of's inverse)."""
    y, w = divmod(week, 96)
    m, k = divmod(w, 8)
    md = MONTH_DAYS[m]
    d = next(d for d in range(1, md + 1) if (d - 1) * 8 // md == k)
    return "%02d/%02d" % (d, m + 1) + ("+%d" % y if y else "")


# ---- finding the tables ------------------------------------------------------------------------------------------

def _walk(exe, off):
    """The entries from obj1 `off` until the length-0 one, or None if they do not look like SP_TAB's."""
    o, out = exe.obj[1], []
    while True:
        if off + 16 > len(o):
            return None
        ln = struct.unpack_from("<H", o, off + 14)[0]
        if ln == 0:
            return out if out and not any(o[off:off + 16]) else None
        if ln & 3 or ln < 16 or o[off + 3] == 0 or o[off + 7] > 8:
            return None
        out.append(off)
        off += ln


def find_tables(exe):
    """(SP_TAB offset in obj1, [SP_CB routine (obj, offset)])."""
    o = exe.obj[1]
    for i in exe.find_all(1, b"\xff\x14\x85"):
        t = exe.target(1, i + 3)
        if not t or t[0] != 1:
            continue
        cb, p = [], t[1] + 4
        while exe.target(1, p):
            cb.append(exe.target(1, p))
            p += 4
        if o[p] != 0xC3 or not cb:
            continue
        tab = (p + 1 + 3) & ~3
        if _walk(exe, tab):
            return tab, cb
    raise ValueError("SP_TAB not found (the split code's `call [SP_CB + eax * 4 - 4]`)")


def callback_index(exe, at):
    """gplay's ba_<name> stub: `mov dword [GP_BPI], i` -> i."""
    obj, off = at
    b = exe.obj[obj]
    if b[off:off + 2] != b"\xc7\x05":
        raise ValueError("SP_CB routine at %d:%#x is not a ba_ stub" % at)
    return struct.unpack_from("<I", b, off + 6)[0]


# ---- an entry ----------------------------------------------------------------------------------------------------

def parse_entry(exe, off):
    o = exe.obj[1]
    world, file, div, n, cycles, d1, d2, nst = o[off:off + 8]
    o_counts, o_firsts, o_pairs, ln = struct.unpack_from("<4H", o, off + 8)
    counts = o[off + o_counts:off + o_counts + d2]
    days = []
    for d in range(d2):
        f = struct.unpack_from("<H", o, off + o_firsts + 2 * d)[0]
        days.append([tuple(o[off + o_pairs + 3 * (f + k):off + o_pairs + 3 * (f + k) + 3]) for k in range(counts[d])])
    stages = []
    for k in range(nst):
        r = off + 16 + SR * k
        j0, ng, flags = o[r], o[r + 1], o[r + 2]
        groups = [tuple(o[r + 4 + 3 * i:r + 7 + 3 * i]) for i in range(ng)]
        mato, po, mo = struct.unpack_from("<HHH", o, r + 16)
        cb, tb, ann = o[r + 22], o[r + 23], o[r + 24]
        wo = struct.unpack_from("<H", o, r + 26)[0]
        stages.append(dict(j0=j0, groups=groups, flags=flags, mat=o[off + mato:off + mato + n * n] if mato else None,
                           perm=list(o[off + po:off + po + n]) if po else None,
                           mem=list(o[off + mo:off + mo + n]) if mo else None, cb=cb, tb=tb, ann=ann,
                           weeks=list(o[off + wo:off + wo + d1 + d2]) if wo else None))
    for k, st in enumerate(stages):
        end = stages[k + 1]["j0"] if k + 1 < len(stages) else d2
        st["days"] = days[st["j0"]:end]
    return dict(world=world, file=file, div=div, n=n, cycles=cycles, d1=d1, d2=d2, stages=stages)


# ---- home rules (split.matrix, ported from the Mod's tools/split.py, MIT, Davide Lorigliola) ---------------------

def _matrix(table, n, games, cycles, home):
    hosts = {}
    for c in range(games):
        swap = (cycles - c) & 1
        for a, b in table:
            if swap:
                a, b = b, a
            hosts.setdefault((a, b), []).append(c)
    out = bytearray(n * n)
    for a in range(n):
        for b in range(n):
            if a == b:
                continue
            ha, hb = hosts.get((a, b), []), hosts.get((b, a), [])
            if home == "balance":
                out[a * n + b] = 1 if len(ha) < len(hb) else 0 if len(ha) > len(hb) else 2
            else:
                k = int(home[7:]) - 1
                if games <= k:
                    return None
                out[a * n + b] = 1 if k in hb else 0
    return bytes(out)


HOMES = ("balance", "reverse1", "reverse2", "reverse3", "reverse4")


def _home(mat, n, games, cycles, pairs_of, real=None):
    """The home rule whose matrix is `mat`. pairs_of(n): the engine's pair table (or None). Several rules can
    give the same matrix (one meeting before the split: 'balance' = 'reverse1'; three: 'balance' = 'reverse1' =
    'reverse3' on the engine's calendar): then the rule of the league's real first-season list (LGREAL.DAT's host
    table, real_rules()) if it has one, else 'reverse1' after one meeting, 'balance' after more -- the same bytes
    either way, so the same game."""
    table = pairs_of(n)
    fits = [h for h in HOMES if table is not None and _matrix(table, n, games, cycles, h) == bytes(mat)]
    if not fits:
        return "balance" if 2 in mat else None      # (no pair table: 2 only in a balance)
    if real:
        both = [h for h in fits if h in real]
        if both:
            return both[0]
    if len(fits) > 1 and games == 1 and "reverse1" in fits:
        return "reverse1"
    return fits[0]


def _lg_hosts(pairs, n, home):
    """The Mod's tools/lgreal.py hosts() (MIT, Davide Lorigliola): a host table from a real list's matches."""
    met = {}
    for a, b in pairs:
        met.setdefault(frozenset((a, b)), []).append(a)
    out = bytearray(n * n)
    for a in range(n):
        for b in range(n):
            if a == b:
                continue
            h = met.get(frozenset((a, b)), [])
            if home == "balance":
                ha, hb = h.count(a), h.count(b)
                out[a * n + b] = 1 if ha < hb else 0 if ha > hb else 2
            else:
                k = int(home[7:]) - 1
                if len(h) <= k:
                    return None
                out[a * n + b] = 1 if h[k] == b else 0
    return bytes(out)


def real_rules(world):
    """{team file: [home rules]} from DATA/LGREAL.DAT (the Mod's tools/lgreal.py: dd n; n x (db world, clubs, days,
    matches a day (+ 80h: a host table), clubs x dw team word, days x matches x (db home, away), [clubs x clubs])):
    the rules whose host table, made from the real list, is the one stored."""
    import os
    path = next((os.path.join(world.data, x) for x in os.listdir(world.data) if x.upper() == "LGREAL.DAT"), None)
    if not path:
        return {}
    b, out = open(path, "rb").read(), {}
    p = 4
    for _ in range(struct.unpack_from("<I", b, 0)[0]):
        _, n, days, mpd = b[p:p + 4]
        p += 4
        words = struct.unpack_from("<%dH" % n, b, p)
        p += 2 * n
        m = mpd & 0x7F
        pairs = [(b[p + 2 * i], b[p + 2 * i + 1]) for i in range(days * m)]
        p += 2 * days * m
        if mpd & 0x80:
            tab = b[p:p + n * n]
            p += n * n
            out[words[0] & 0xFF] = [h for h in HOMES if _lg_hosts(pairs, n, h) == tab]
    return out


# ---- the JSON ----------------------------------------------------------------------------------------------------

def _snake(n, ng):
    order = [k for b in range(n // ng) for k in (range(ng) if b % 2 == 0 else reversed(range(ng)))]
    return [[p + 1 for p, k in enumerate(order) if k == x] for x in range(ng)]


def stage_json(e, st, rows, code, cbs, pairs_of, real=None):
    n, games = e["n"], e["cycles"] - 1
    out = {"after": e["d1"] + st["j0"]}
    if st["flags"] & 4:                                     # a fixture list by rows
        out["fixtures"] = [[[rows[a], rows[b]] for a, b, _ in d] for d in st["days"]]
        if st["mem"]:
            out["zones"] = [[rows[r] for r in range(n) if st["mem"][r] == g] for g in range(max(st["mem"]) + 1)]
        if st["flags"] & 16:
            out["tiebreak"] = "wins"
    else:
        gs = []
        for start, size, pts in st["groups"]:
            m = size * (size - 1) // 2
            cnt = sum(1 for d in st["days"] for a, b, _ in d if start <= a < start + size)
            gs.append({"places": [start + 1, start + size], "rounds": ROUNDS[cnt // m if m else 0],
                       "points": POINTS[pts]})
        if st["mat"] is not None:
            h = _home(st["mat"], n, games, e["cycles"], pairs_of, real)
            if h:
                out["home"] = h
        if st["flags"] & 2:
            out["keep"] = "opening"
        out["groups"] = gs
        if st["perm"]:
            expl, k = [], 0
            for start, size, _ in st["groups"]:
                expl.append([p + 1 for p in st["perm"][k:k + size]])
                k += size
            if expl == _snake(n, len(gs)):
                out["places_snake"] = True
            else:
                out["places"] = expl
    if st["flags"] & 1:
        out["reset"] = True
    if st["tb"]:
        out["table"] = st["tb"] - 1
    if st["ann"]:
        out["annual"] = True
    if st["cb"]:
        out["then"] = then_name(code, cbs[st["cb"] - 1])
    return out


def split_json(e, rows, code, cbs, pairs_of, real=None):
    stages = [stage_json(e, st, rows, code, cbs, pairs_of, real) for st in e["stages"]]
    weeks = e["stages"][0]["weeks"] if e["stages"] else None
    if len(stages) == 1 and "fixtures" not in stages[0] and not weeks:
        sp = stages[0]
        sp.pop("reset", None)                               # (a single split: after, home, groups)
        return sp
    sp = {"stages": stages}
    if weeks:
        sp["weeks"] = [date_of(w) for w in weeks]
    return sp


def division_rows(world, file, div):
    """The division's clubs in team-file order (the split's rows): the records' league byte."""
    return [world.key[(file, i)] for i, r in enumerate(world.records[file]) if r[25] == div]


def decode(world):
    exe = world.exe
    tab, cb_at = find_tables(exe)
    cbs = [callback_index(exe, at) for at in cb_at]
    pairs_cache = {}

    def pairs_of(n):
        if n not in pairs_cache:
            pairs_cache[n] = engine_pairs(exe, n)
        return pairs_cache[n]

    reals = real_rules(world)
    packs = {}
    for off in _walk(exe, tab):
        e = parse_entry(exe, off)
        if not e["stages"]:
            continue                                        # (meetings of their own only: the leagues' 'games')
        code = world.code_of.get(e["file"])
        if code is None:
            continue
        rows = division_rows(world, e["file"], e["div"])
        if len(rows) != e["n"]:
            raise ValueError("%s division %d: %d rows, the split %d clubs" % (code, e["div"], len(rows), e["n"]))
        sp = split_json(e, rows, code, cbs, pairs_of, reals.get(e["file"]) if e["div"] == 0 else None)
        lg = packs.setdefault(code, {"competitions": [{"key": "league"}]})["competitions"][0]
        ndiv = len({r[25] for r in world.records[e["file"]] if r[25] < 0x10})
        if ndiv <= 1 and e["div"] == 0:
            lg["split"] = sp
        else:
            dl = lg.setdefault("divisions", [])
            while len(dl) <= e["div"]:
                dl.append(None)
            dl[e["div"]] = {"split": sp}
    return {"packs": packs}


# ---- the engine's pair tables ------------------------------------------------------------------------------------

def engine_pairs(exe, n):
    """The engine's pairs of an n-club group (the table of pointers split.pair_table reads, dseg_11F356): found as
    a run of pointers whose k-th entry (k = size - 2) holds size(size-1)/2 slot pairs, each pair of slots once.
    None when not found."""
    if not hasattr(exe, "_sp_pairtab"):
        exe._sp_pairtab = _find_pairtab(exe)
    pt = exe._sp_pairtab
    if pt is None:
        return None
    t = exe.target(pt[0], pt[1] + 4 * (n - 2))
    if not t:
        return None
    raw = exe.bytes(t[0], t[1], n * (n - 1))
    return [(raw[2 * i], raw[2 * i + 1]) for i in range(n * (n - 1) // 2)]


def _ok_pairs(exe, t, m):
    raw = exe.bytes(t[0], t[1], m * (m - 1))
    if len(raw) < m * (m - 1):
        return False
    ps = {frozenset((raw[2 * i], raw[2 * i + 1])) for i in range(m * (m - 1) // 2)}
    return len(ps) == m * (m - 1) // 2 and all(len(p) == 2 and max(p) < m for p in ps)


def _find_pairtab(exe):
    for (obj, off), t in sorted(exe.ptr.items()):
        ok = True
        for m in (2, 4, 6, 10, 12):
            tt = exe.target(obj, off + 4 * (m - 2))
            if not tt or not _ok_pairs(exe, tt, m):
                ok = False
                break
        if ok:
            return obj, off
    return None
