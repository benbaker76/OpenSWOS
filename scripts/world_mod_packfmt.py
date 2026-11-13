#!/usr/bin/env python3
"""world_mod_packfmt.py -- World 2026-27's packs in the Mod's FULL pack format (packs/README.md of the Mod's repo,
tools/mkseason.py Pack), from an installed SWOS 96/97 Mod: what scripts/world_from_mod.py adds to the decoded
packs so the Mod's OWN code (mkseason.Pack, complist, conti, statespec, pgroups ... -- scripts/league_playoffs_check.sh
runs them) loads them as it loads Davide's packs (docs/WORLD_2026_27.md 6d).

    complete(world, whole, game)            the decoded whole -> + every pack.json field the Mod's code needs
    club_rows(world, whole, code)           clubs.csv, every column (the records' coach, tactic, kits, line-up)
    player_rows(world, code)                players.csv, every column (the records' players, 17.. from XTW5)
    roundtrip(path, world, code)            the Mod's mkseason.Pack over a written pack: its team files, XTW and
                                            off-file pool against the installed files (the check that the inverse
                                            is exact; tools/ of the Mod's repo on sys.path)

The squads are the Mod's 'explicit' method (mkseason.Pack._explicit / _explicit_player, MIT, Davide Lorigliola):
every record byte is a clubs.csv / players.csv column, so the records decode back into the columns one for one --
a club's tactic (+24), kit1 / kit2 (+26 / +31, 5 bytes each), line-up (+60, 16 bytes: 0 1 .. 15 when it is the
one mkseason.auto_lineup gives -- the identity means "from the positions" --, as Davide's packs write it), coach
(+36); a player's nat (+0), number (+2), name (+3, 23 bytes), position and skin (+26: bits 5-7 and 3-4), the 7
skills (+28..+31: a nibble each, 8 = a key skill, '*') and price (+32). A file's extra players (XTW5.nnn, the
Mod's xtra.py) are each club's rows 17..; squads "extra" is the XTW header's per-club count. What the records do
not hold: clubs.csv 'source' (the Transfermarkt name: provenance only, blank); the players' bald looks (+25, set
after the build from the Mod's packs/bald-looks-2627.csv by blond.py -- the name's 23rd byte, so not a players.csv
column in Davide's packs either); an off-file pool club's players past 16 (the Mod's build drops them: RS5 holds 16).

pack.json (beyond what the area decoders give):
- format 1, kind 'career', world 5 -- every world pack's;
- button, button_names: the country record (mkseason._patch_pack: continent byte, name) of each language's exe
  (ENGLISH / ITALIAN / FRENCH / GERMAN.EXE, the world's namespace row);
- files: the team files of the country (TMW5.nnn: 'league' the league's, 'states' the state championships' -- Brazil
  --, else 'file<nnn>') and its off-file pool ('pool': RS5nnn.DAT's own file number and global base); each base
  = its first record's global number; squads [{files: all of them, method 'explicit', extra}];
- the league's 'id' (the struct's first byte) and 'dates_from' / a cup's 'dates_from': the 1996-97 country whose
  league (and national cup) calendar bytes the struct copied (mkseason._league_body / _patch_pack) -- the lowest
  such country; the 96/97 competitionsTable is found through England's league struct's pointers;
- the root pack (the world's first league file: its squad_numbers flag's pack): world_cups (start_year: the world
  record's year byte, +42; name: '<word> <year>-<yy>' of each exe; cc / cwc / uefa 'league_phase' when the record's
  cup has 36 clubs, the league phase's size) and pool 'POOL.DAT' (DATA\\POOL05.DAT, copied by world_from_mod.py);
- other languages' names: the pack decoders run over each language's exe; a slot gets it / fr / de only where one
  differs from the English exe's (else '*' alone, as in Davide's packs).
Not in the installed files: 'sources' (provenance: written as this conversion's), shared_base_files (a file's global
numbers shared with another of the build's: none in 4.1.1's TMW5 files), format_prose (DATA\\FMTxx5.DAT's prose, not
decoded), continental_places (CONMEBOL / CAF ... qualification, compiled into the conti/ access files).
Standard library only.
"""
import collections
import importlib
import os
import re
import struct

WORLD = 5
LANGS = (("en", "ENGLISH.EXE"), ("it", "ITALIAN.EXE"), ("fr", "FRENCH.EXE"), ("de", "GERMAN.EXE"))
NAME_DECODERS = ("leagues", "playoffs", "splits", "cups", "europe")    # (conti's and the Super Cups' search English)
POS = ["G", "RB", "LB", "D", "RW", "LW", "M", "A"]                     # mkseason.EXPLICIT_POS (byte 26, bits 5-7)
ROLE = {"G": "G", "RB": "D", "LB": "D", "D": "D", "RW": "M", "LW": "M", "M": "M", "A": "A"}
SKIN = ["light", "ginger", "dark", "darkblond"]                        # mkseason.EXPLICIT_SKIN (bits 3-4)
SKILLS = ["pa", "ve", "he", "ta", "co", "sp", "fi"]
SLOT_POS = [0, 1, 3, 3, 2, 4, 6, 6, 5, 7, 7]
POS_CLASS = {0: 0, 1: 1, 2: 1, 3: 1, 4: 2, 5: 2, 6: 2, 7: 3}
CLUB_FIELDS = ["key", "name", "coach", "country", "file", "division", "tactic", "kit1", "kit2", "lineup", "source",
               "second_of", "area"]
PLAYER_FIELDS = ["club", "name", "role", "position", "number", "nat", "skin"] + SKILLS + ["price"]
SOURCES = ["The installed SWOS 96/97 Mod %s (Davide Lorigliola's): its DATA and exe, decoded by OpenSWOS's "
           "scripts/world_from_mod.py; the Mod's own sources are in its release notes"]


def _text(b):
    return b.split(b"\0")[0].decode("latin1")


def auto_lineup(positions):
    """mkseason.auto_lineup: the 11 starters take the 4-4-2 slots (exact position first, then the class)."""
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


def player_row(club, q):
    """One 38-byte player -> its players.csv row (mkseason._explicit_player inverted)."""
    pos = POS[q[26] >> 5]
    nib = [q[28 + i // 2] >> (4 * (1 - i % 2)) & 15 for i in range(8)][1:]
    row = {"club": club, "name": _text(q[3:26]), "role": ROLE[pos], "position": pos, "number": str(q[2]),
           "nat": str(q[0]), "skin": SKIN[q[26] >> 3 & 3], "price": str(q[32])}
    for sk, v in zip(SKILLS, nib):
        row[sk] = str(v & 7) + ("*" if v & 8 else "")
    return row


def club_record_cols(r):
    """A record's clubs.csv columns (mkseason._explicit / _empty_record inverted)."""
    ps = [r[76 + 38 * j + 26] >> 5 for j in range(16)]
    lineup = list(r[60:76])
    return {"coach": _text(r[36:59]), "tactic": str(r[24]), "kit1": " ".join(map(str, r[26:31])),
            "kit2": " ".join(map(str, r[31:36])),
            "lineup": " ".join(map(str, range(16) if lineup == auto_lineup(ps) else lineup))}


# ---- the files of a country ---------------------------------------------------------------------------------------

def country_files(world, code, pack):
    """[(file key, team file number or None for the pool, [(club key, record)])] in the Mod's order: the league
    file, the other team files, the off-file pool."""
    import world_from_mod as wfm
    files = sorted(f for f, c in world.code_of.items() if c == code)
    lfile = min(files)
    states = {k for c in pack.get("competitions", []) if c.get("type") == "state" for k in c.get("clubs", [])}
    out = []
    for f in files:
        recs = world.records[f]
        keys = [world.key[(f, i)] for i in range(len(recs))]
        name = "league" if f == lfile else "states" if states & set(keys) else "file%03d" % f
        out.append((name, f, list(zip(keys, recs))))
    pool = world.pools.get(code)
    if pool:
        rs = os.path.join(world.data, "RS%d%03d.DAT" % (WORLD, pool_owner(world, code)))
        recs = wfm.records(rs)
        out.append(("pool", None, [(k, r) for (_, k), r in zip(pool, recs)]))
    return out


def file_keys(world, code, pack):
    """The pack.json 'files' keys in Davide's order: the league's, the pool, the others."""
    names = [n for n, _, _ in country_files(world, code, pack)]
    return names[:1] + [n for n in names[1:] if n == "pool"] + [n for n in names[1:] if n != "pool"]


def pool_owner(world, code):
    for x in os.listdir(world.data):
        m = re.fullmatch(r"RS%d(\d{3})\.DAT" % WORLD, x, re.I)
        if m and world.code_of.get(int(m.group(1))) == code:
            return int(m.group(1))
    return None


def xtw(world, f):
    """(extra per club, {ordinal: [38-byte players]}) of XTW5.<f>, or (0, {})."""
    import world_from_mod as wfm
    p = os.path.join(world.data, "XTW%d.%03d" % (WORLD, f))
    if not os.path.exists(p):
        return 0, {}
    d = wfm.unpack(open(p, "rb").read())
    return d[2], wfm.extras(p, len(world.records[f]))


def club_rows(world, whole, code):
    """clubs.csv: every club of the country (file by file, record order; then the pool) with every column."""
    pack = whole["packs"][code]
    dec = (whole.get("clubs") or {}).get(code) or {}
    rows = []
    files = {n: (f, clubs) for n, f, clubs in country_files(world, code, pack)}
    for name in file_keys(world, code, pack):           # (file after file, in pack.json's order)
        f, clubs = files[name]
        for key, r in clubs:
            c = dec.get(key, {})
            row = {"key": key, "name": _text(r[5:22]), "country": code, "file": name,
                   "division": c.get("division", ""), "source": "", "second_of": c.get("second_of", ""),
                   "area": c.get("area", "")}
            row.update(club_record_cols(r))
            rows.append(row)
    return rows


def player_rows(world, code, pack):
    rows = []
    for name, f, clubs in country_files(world, code, pack):
        xt = xtw(world, f)[1] if f is not None else {}
        for i, (key, r) in enumerate(clubs):
            rows += [player_row(key, r[76 + 38 * j:76 + 38 * (j + 1)]) for j in range(16)]
            rows += [player_row(key, q) for q in xt.get(i, [])]
    return rows


# ---- pack.json ---------------------------------------------------------------------------------------------------

def _ns_rows(e):
    import world_mod_leagues as wl
    return wl.find_ns(e)[1][WORLD]


def buttons(game, files):
    """{league file: {lang: the country button}} from each language's exe."""
    import world_mod_exe as x
    import world_mod_leagues as wl
    out = collections.defaultdict(dict)
    for lang, exe in LANGS:
        p = os.path.join(game, exe)
        if not os.path.exists(p):
            continue
        e = x.Exe(p)
        rows = _ns_rows(e)
        for f in files:
            obj, off = wl._tagged(e, rows[f][0])
            out[f][lang] = e.cstr(obj, off + 1).decode("latin1")
    return out


def calendars(e):
    """{1996-97 country: (league calendar bytes, national cup calendar bytes or None)}: its competitionsTable entry
    (found as the pointer table whose entry 8 leads to England's league struct, byte 2 = 8)."""
    tab = None
    for (so, s), (to, t) in e.ptr.items():
        lg = e.ptr.get((to, t))
        if so != 2 or not lg:
            continue
        b = e.obj[lg[0]][lg[1]:lg[1] + 13]
        if len(b) == 13 and b[2] == 8 and b[1] == 0 and b[5] == 9 + 6 * b[9] and b[12] in (0x33, 0x35):
            base = s - 32
            ok = sum(1 for c in range(80) for t2 in [e.ptr.get((2, base + 4 * c))] if t2 for l2 in [e.ptr.get(t2)]
                     if l2 and e.obj[l2[0]][l2[1] + 2] == c)
            if ok >= 60:
                tab = base
                break
    if tab is None:
        raise ValueError("the 1996-97 competitionsTable was not found")
    out = {}
    for c in range(80):
        t = e.ptr.get((2, tab + 4 * c))
        lg = e.ptr.get(t) if t else None
        if not lg or e.obj[lg[0]][lg[1] + 2] != c:
            continue
        k = 4
        while struct.unpack_from("<i", e.obj[t[0]], t[1] + k)[0] != -2:
            k += 4
        cp = e.ptr.get((t[0], t[1] + k + 4))
        out[c] = (e.obj[lg[0]][lg[1] + 3:lg[1] + 5], e.obj[cp[0]][cp[1] + 3:cp[1] + 5] if cp else None)
    return out


def dates_from(cal, league, cup):
    """The 1996-97 countries whose league calendar is the struct's (and their cup's the national cup's), lowest
    first (the bytes are the calendar's: several countries can share one)."""
    ok = [c for c, (l, k) in sorted(cal.items()) if l == league and (cup is None or k == cup)]
    return ok or [c for c, (l, k) in sorted(cal.items()) if l == league]


def lang_names(game, whole):
    """[(the dict holding a name, its field, {lang: name})]: the it / fr / de names of every pack name slot (the
    decoders over each language's exe), where one differs from the English exe's (call before any name is replaced:
    world_from_mod.apply_names)."""
    import world_from_mod as wfm
    import world_mod_exe as x
    en = {pos: o for _, _, _, pos, o in wfm.name_slots(whole.get("packs", {}), whole.get("cupd", {}), {})}
    raw = {(pos, fld): o[fld].get("en") for pos, o in en.items() for fld in ("names", "record") if fld in o}
    got = collections.defaultdict(dict)
    for lang, exe in LANGS[1:]:
        if not os.path.exists(os.path.join(game, exe)):
            continue
        w = x.World(game, exe_name=exe)
        other = {}
        for area in NAME_DECODERS:
            wfm.merge(other, importlib.import_module("world_mod_" + area).decode(w), area)
        for _, _, _, pos, o in wfm.name_slots(other.get("packs", {}), other.get("cupd", {}), {}):
            for fld in ("names", "record"):
                if (pos, fld) in raw and fld in o:
                    got[(pos, fld)][lang] = o[fld].get("en")
    return [(en[pos], fld, v) for (pos, fld), v in got.items() if any(n != raw[(pos, fld)] for n in v.values())]


def complete(world, whole, game, version):
    """Every pack.json field the Mod's code needs, added to the decoded packs (whole['packs'])."""
    e = world.exe
    packs = whole["packs"]
    files_of = collections.defaultdict(list)
    for f, c in world.code_of.items():
        files_of[c].append(f)
    lfiles = {code: min(fs) for code, fs in files_of.items() if code in packs}
    btn = buttons(game, lfiles.values())
    cal = calendars(e)
    tabs = _country_structs(world)
    fits = {}                           # code -> the 1996-97 countries whose calendars its structs copy
    for code in packs:
        at = tabs[code]
        lgb = e.obj[at["league"][0]][at["league"][1] + 3:at["league"][1] + 5]
        cup = e.obj[at["cup"][0]][at["cup"][1] + 3:at["cup"][1] + 5] if at["cup"] else None
        fits[code] = dates_from(cal, lgb, cup)
    # (a pack without a national cup fits more countries: it takes the one its world's other packs copied)
    usual = collections.Counter(f[0] for c, f in fits.items() if f and tabs[c]["cup"]).most_common(1)
    usual = usual[0][0] if usual else None
    for code, pk in packs.items():
        lfile = lfiles[code]
        fl = {}
        for name, f, clubs in country_files(world, code, pk):
            r0 = clubs[0][1]
            fl[name] = {"file": r0[0], "base": struct.unpack_from(">H", r0, 2)[0]}
        sq = file_keys(world, code, pk)
        fl = {k: fl[k] for k in sq}
        extra = max([xtw(world, f)[0] for f in files_of[code]] + [0])
        b = btn.get(lfile, {})
        head = {"format": 1, "id": pk["id"], "kind": "career", "button": b.get("en", code),
                "button_names": b, "continent": pk["continent"], "sources": [SOURCES[0] % version], "files": fl,
                "squads": [dict({"files": sq, "method": "explicit"}, **({"extra": extra} if extra else {}))],
                "world": WORLD}
        rest = {k: v for k, v in pk.items() if k not in head}
        pk.clear()
        pk.update(head)
        pk.update(rest)
        # the competitions' ids and calendars (mkseason._league_body, _patch_pack)
        lg = next(c for c in pk["competitions"] if c["type"] == "league")
        at = tabs[code]
        league = e.obj[at["league"][0]][at["league"][1]:at["league"][1] + 13]
        df = usual if usual in fits[code] else (fits[code] or [None])[0]
        new = {}
        for k, v in lg.items():
            new[k] = v
            if k == "type":
                new["id"] = "%02X" % league[0]
            if k == "clubs" and df is not None:
                new["dates_from"] = df
        lg.clear()
        lg.update(new)
        # a one-division league over 24 clubs plays a split's fixture list (USA's MLS): the Mod's Pack checks the
        # size against the division's split, so the split goes into a one-division 'divisions' list (the same
        # struct either way; the Mod's own packs write it so)
        nt = sum(1 for c in (whole.get("clubs") or {}).get(code, {}).values() if c.get("division") == "0")
        if "split" in lg and "divisions" not in lg and nt > 24 and "fixtures" in str(lg["split"]):
            lg["divisions"] = [{"teams": nt, "promoted": 0, "relegated": lg.get("relegated", 0),
                                "names": lg["names"], "split": lg.pop("split")}]
        for c in pk["competitions"]:
            if c["type"] == "cup" and c.get("layout") == "national" and df is not None:
                c["dates_from"] = df
        if code in (whole.get("cupd") or {}):       # the Coppa Italia Serie D: the league's 'cup_d' (cupd.py)
            lg["cup_d"] = {"file": "cupd.json", "names": whole["cupd"][code].get("names")}
    root = min(lfiles, key=lfiles.get)
    wc = world_cups(game, e)
    if wc:
        packs[root]["world_cups"] = wc
        packs[root]["pool"] = "POOL.DAT"
    return root


def _country_structs(world):
    """{code: {'league': (obj, off), 'cup': the national cup's (obj, off) or None}} (world_mod_cups.country_tables)."""
    import world_mod_cups as wc
    if not hasattr(world, "_structs"):
        out = {}
        for code, (num, lg, cups) in wc.country_tables(world).items():
            nat = [c for c in cups if c and world.exe.obj[c[0]][c[1] + 1] == 1]
            out[code] = {"league": lg, "cup": nat[0] if nat else None}
        world._structs = out
    return world._structs


def world_cups(game, e):
    """The root pack's world_cups: the world record's year and cups (36 clubs: a league phase), each exe's name."""
    import world_mod_europe as we
    obj, rec = we._find_world_record(e)
    year = 1900 + e.u8(obj, rec + 42)
    out = {"start_year": year, "name": {}}
    tag = ("%d-%02d" % (year, (year + 1) % 100)).encode()
    for lang, exe in (("it", "ITALIAN.EXE"), ("en", "ENGLISH.EXE"), ("fr", "FRENCH.EXE"), ("de", "GERMAN.EXE")):
        p = os.path.join(game, exe)
        if not os.path.exists(p):
            continue
        d = open(p, "rb").read()
        found = sorted(set(re.findall(rb"\0([A-Z]+ " + re.escape(tag) + rb")\0", d)))
        if len(found) == 1:
            out["name"][lang] = found[0].decode("latin1")
    for i, key in enumerate(we.CUPS):
        if e.u16(obj, rec + 34 + 2 * i) == 36:
            out[key] = {"format": "league_phase"}
    return out


# ---- the check: the Mod's compiler over the written pack ---------------------------------------------------------

def roundtrip(path, world, code):
    """The Mod's mkseason.Pack(path).team_files() (its tools/ on sys.path) against the installed TMW5 / XTW5 / RS5
    files: [differences]. Byte 25 of a player (the bald looks, blond.py's, set after the build) is not compared."""
    import mkseason
    import world_from_mod as wfm
    pk = mkseason.Pack(path)
    import contextlib
    import io
    with contextlib.redirect_stdout(io.StringIO()):
        built = pk.team_files()
    bad = []

    def cmp(what, a, b, off=2):
        a, b = bytearray(a), bytearray(b)
        if len(a) != len(b):
            bad.append("%s: %d bytes, the game's %d" % (what, len(a), len(b)))
            return
        for x in (a, b):
            for p in range(off + 76 + 25, len(x), 38):      # (player byte 25 of the team records' players)
                if (p - off - 76) % 684 < 16 * 38:
                    x[p] = 0
        if a != b:
            i = next(i for i in range(len(a)) if a[i] != b[i])
            bad.append("%s: first difference at byte %d" % (what, i))

    for f in sorted(f for f, c in world.code_of.items() if c == code):
        cmp("TMW5.%03d" % f, built.get(f, b""), open(os.path.join(world.data, "TMW5.%03d" % f), "rb").read())
        xp = os.path.join(world.data, "XTW5.%03d" % f)
        if os.path.exists(xp) or f in pk.xtra:
            a = bytearray(pk.xtra.get(f, b""))
            b = bytearray(wfm.unpack(open(xp, "rb").read()) if os.path.exists(xp) else b"")
            for x in (a, b):
                for p in range(3 + 25, len(x), 38):
                    x[p] = 0
            if a != b:
                bad.append("XTW5.%03d differs" % f)
    if getattr(pk, "pool_data", None) is not None:
        rs = os.path.join(world.data, "RS%d%03d.DAT" % (WORLD, pool_owner(world, code)))
        cmp(os.path.basename(rs), pk.pool_data, open(rs, "rb").read())
    return bad


def main():
    """python3 -I scripts/world_mod_packfmt.py GAME DIR TOOLS: every pack of DIR (a mod/ folder world_from_mod.py
    wrote from the installed GAME) through the Mod's mkseason.Pack (TOOLS: the tools/ of a checkout of the Mod at
    the installed version), its team files against GAME's -- the exactness of the columns."""
    import glob
    import sys
    if len(sys.argv) != 4:
        sys.exit(main.__doc__)
    game, out, tools = sys.argv[1:]
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(1, tools)
    import world_mod_exe
    world = world_mod_exe.World(game)
    bad = 0
    packs = sorted(glob.glob(os.path.join(out, "packs", "*", "pack.json")))
    for p in packs:
        code = os.path.basename(os.path.dirname(p)).upper()
        try:
            r = roundtrip(os.path.dirname(p), world, code)
        except Exception as ex:                  # (the Mod's PackError: a pack its compiler refuses)
            r = ["%s: %s" % (type(ex).__name__, ex)]
        for x in r:
            print("FAIL: %s: %s" % (code, x))
        bad += bool(r)
    print("%s: %d packs, the Mod's mkseason.Pack builds %s" % ("FAIL" if bad else "PASS", len(packs),
          "%d of them differently" % bad if bad else "every team file, XTW5 and off-file pool the game's "
          "(player byte 25, the bald looks, aside)"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
