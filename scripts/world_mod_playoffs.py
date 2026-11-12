"""world_mod_playoffs.py -- the knockout play-offs of an installed SWOS 96/97 Mod's World 2026-27, decoded from its
patched DOS exe back into pack.json's league 'ko_playoffs' (docs/WORLD_2026_27.md 6d). Standard library only.

The Mod's tools/gplay.py (MIT, Davide Lorigliola) compiles every country's 'ko_playoffs' into tables of obj1, laid
out by its table_lines / names_lines one after another (nasm, no alignment):

  GP_TAB   per country: dd its league struct, db world, dd its spec        (a 0 world ends it)
  GP_EPT   per country of a world: db its team file, dd its spec           (0FFh ends it)
  GPS<i>   the specs: compile_spec's bytes, each ended by a 0
  GP_NMT   per GP_TAB entry dd GPN<i>: per play-off dd long, short, record (offsets from STR_BASE, obj2), 0, 0, 0
  GP_CMT   stage titles won together (derived from the names: not read)
  GP_THT   per entry dd GPT<i>: per play-off its 'then_build' target's index + 1, or 0
  GP_RNT   per entry dd GPR<i>: per play-off 12 bytes, its game rounds' names (rnames.KEYS indices, 0 = the game's)
  ba_<n>:  per split stage 'then' that builds a stage title's knockout ('build_after'): mov dword [GP_BPI], i;
           call gp_lastwk; mov [GP_BWEEK], eax; jmp gp_defer -- bound into split.py's SP_CB, which SP_TAB's stage
           records index (db callback, 1+).

Everything is found through pointers and code: gp_setup's `sub esi, GP_TAB ... mov eax, [GP_NMT + eax * 4] ...
[GP_RNT ...] ... [GP_THT ...]`, gp_fixload's `push SLOTS; push STR_BASE; push SEASREC`, split's `call [SP_CB +
eax * 4 - 4]`; never by an offset (4.1.1's layout differs from 4.0.5's).

What the exe does not keep, and how it is chosen here:
  key          not compiled. "<code>_po<i>" (its index), except a 'title' play-off whose SEASON WINNERS line (DATA\
               FMTEN<world>.DAT, the Mod's fmtgame) is 'PLAYOFFS - <title>' of the importer's WN_TITLES: that table's
               key (usa mls_po, aus finals, can cpl_po, mlt final_four), so the line stays the Mod's.
  build_after  the split stage's 'then' name is not compiled (only its routine, ba_<name>: the play-off index):
               then_name(code, i) ("arg_ba3"), as world_mod_splits names the stage's 'then'.
  names        English ({"en": [long, short]}). The exe's are the Mod's FITTED names (complist.fit_titles); the long
               name comes from the SEASON WINNERS line when there is one (fmtgame writes the pack's raw long name
               there), else the fit is undone where the Mod's own fit proves it (unfit_long / unfit_short). Not
               recoverable: a long name the fit replaced by the short one and that has no WINNERS line of its own
               (4.0.5: aus finals, can cpl_po). 'record' only where it is not the default (the fitted long name; for
               a 'title', the league's first division name -- gplay compile_spec's league_names).
  spellings    a spec byte with two spellings is written as TRUTH writes it most often: see decode_spec / respell.
"""
import re
import struct

KINDS = {1: "promotion", 2: "europe", 3: "placement", 4: "playout", 5: "title", 6: "continental", 7: "promedio",
         8: "stage_title"}
HOSTS = {0: "listed", 1: "better", 2: "drawn", 3: "neutral"}
LEVELS = {0: "better", 1: "et_pens", 2: "et_better", 3: "pens", 4: "away_pens", 5: "away_better"}
RESERVE, SKIP_SAME, BEST_LOSER = 0xFE, 0x80, 0x3F
TITLE_RULES = {1: "uru", 2: "per"}
# the Mod's tools/rnames.py KEYS (N_OLD 12; 4.0.5 and 4.1.1 alike): a round name -> its _cupRoundStringTable index
RN_N_OLD = 12
RN_KEYS = dict(zip(("WC", "R1G1", "R1G2", "R1G3", "CSF", "CF", "CUP"), range(RN_N_OLD + 15, RN_N_OLD + 22)))
RN_KEYS.update(PRE=RN_N_OLD + 4, R16=RN_N_OLD + 2, QF=RN_N_OLD + 12, SF=RN_N_OLD + 13, F=RN_N_OLD + 14)
RN_NAME = {v: k for k, v in RN_KEYS.items()}
RN_KG = 12
# scripts/world_import.py WN_TITLES' play-off titles (the Mod's fmtgame.WN_TITLES, English): a SEASON WINNERS line
# 'PLAYOFFS - <title>' of a 'title' play-off -> its key
WN_KEYS = {"MLS CUP": "mls_po", "A-LEAGUE MEN CHAMPIONS": "finals", "NORTH STAR CUP": "cpl_po",
           "MALTESE CHAMPIONS": "final_four"}
WN_PO = "PLAYOFFS - "


def then_name(code, i):
    """The 'then' name of a split stage whose after-stage routine builds play-off i of the country's league (its
    ba_ stub's `mov dword [GP_BPI], i`) -- that play-off's 'build_after'. The same as world_mod_splits.then_name,
    which names the stage's 'then' so (the importer pairs the two by name)."""
    return "%s_ba%d" % (code.lower(), i)


# ---- finding the tables ------------------------------------------------------------------------------------------

def _rel(o, at):
    return at + 5 + struct.unpack_from("<i", o, at + 1)[0]


def locate(exe):
    """-> dict: GP_TAB, GP_NMT, GP_RNT, GP_THT (obj1 offsets), STR_BASE (obj2 offset)."""
    o = exe.obj[1]
    hits = []
    for m in re.finditer(rb"\x81\xee(.{4}).{0,16}?\x50\x8b\x04\x85(.{4})\xa3.{4}\x58\x50\x8b\x04\x85(.{4})\xa3.{4}"
                         rb"\x58\x8b\x04\x85(.{4})", o, re.S):
        at = [m.start(k) for k in range(1, 5)]
        t = [exe.target(1, a) for a in at]
        if all(x and x[0] == 1 for x in t):
            hits.append(tuple(x[1] for x in t))
    if len(set(hits)) != 1:
        raise ValueError("gplay: gp_setup's GP_TAB / GP_NMT / GP_RNT / GP_THT not found (%d)" % len(set(hits)))
    tab, nmt, rnt, tht = hits[0]
    sb = set()
    for m in re.finditer(rb"\xff\x74\x24\x08\x68.{4}\x68(.{4})\x68.{4}\x55\xe8", o, re.S):
        t = exe.target(1, m.start(1))
        if t and t[0] == 2:
            sb.add(t[1])
    if len(sb) != 1:
        raise ValueError("gplay: gp_fixload's STR_BASE not found (%s)" % sorted(sb))
    return {"GP_TAB": tab, "GP_NMT": nmt, "GP_RNT": rnt, "GP_THT": tht, "STR_BASE": sb.pop()}


def tables(exe):
    """-> [(world, league struct (obj, off), spec offset, team file or None)] in GP_TAB order, and locate()'s."""
    loc = locate(exe)
    o, a = exe.obj[1], loc["GP_TAB"]
    out = []
    while o[a + 4]:
        lg, sp = exe.target(1, a), exe.target(1, a + 5)
        if not lg or not sp or sp[0] != 1:
            raise ValueError("gplay: GP_TAB entry at obj1+%#x" % a)
        out.append([o[a + 4], lg, sp[1], None])
        a += 9
    a += 9                                      # (GP_EPT: db file, dd spec)
    by_spec = {}
    while o[a] != 0xFF:
        sp = exe.target(1, a + 1)
        by_spec.setdefault(sp[1], o[a])
        a += 5
    for e in out:
        e[3] = by_spec.get(e[2])
    return [tuple(e) for e in out], loc


# ---- a spec ------------------------------------------------------------------------------------------------------

def _member(c):
    if c == BEST_LOSER:
        return "BL"
    if c < 0x40:
        return c
    return {0x80: "W", 0xC0: "L", 0x40: "P"}[c & 0xC0] + str(c & 0x3F)


def decode_spec(o, a, club_of=None):
    """compile_spec's bytes at o[a:] -> ([play-off dicts without key / names / then_build / round_names /
    build_after], the end). club_of(file, ordinal) -> the pack key of a club entrant (type 8), or None.

    Spellings (several compile to the same bytes; TRUTH's usual one is written): an entrant [div, place];
    {"stage_place", "place", "div"} (div always); {"split_seed", "stage"} (+ "div" when not 0); {"title_seed",
    "rule", "div"}; {"club": key} (its division is the club's, clubs.csv); {"stage_title", "div"}; {"best_without_europe": true, "div"};
    {"league_cup_winner": true, "if_places"}; a knockout round {"ties", "legs", "host", "level"} (+ "series": 3,
    "gap", "skip_if_same": true when set); a best_worst / draw block as the round itself ("pairing", "clubs",
    "ties": [...]) when it is the round's only ties, else as its first tie {"pairing", "clubs"}; a group {"group":
    {"rounds", "bonus", "host"}, "members"}; winner_div + loser_div for promotion, loser_div for
    playout (each when not 0 elsewhere)."""
    pos = []
    while o[a]:
        kind, ne, nr, wd, ld, fl = o[a:a + 6]
        a += 6
        if kind not in KINDS:
            raise ValueError("gplay spec: kind %d" % kind)
        if kind == 7:
            pos.append({"kind": "promedio", "matches": wd, "loser_div": ld})
            continue
        po = {"kind": KINDS[kind]}
        ents = []
        for _ in range(ne):
            t, b1, b2, b3 = o[a:a + 4]
            a += 4
            if t == 0:
                ents.append([b1, b2])
            elif t == 1:
                ents.append({"best_without_europe": True, "div": b1})
            elif t == 2:
                ents.append({"league_cup_winner": True, "if_places": [b2, b3]})
            elif t == 3:
                ents.append({"stage_place": b3, "place": b2, "div": b1})
            elif t == 4:
                ents.append({"loser_of_previous": True})
            elif t == 5:
                ents.append({"stage_title": b2, "div": b1})
            elif t == 6:
                e = {"split_seed": b2, "stage": b3}
                if b1:
                    e["div"] = b1
                ents.append(e)
            elif t == 7:
                ents.append({"title_seed": b2, "rule": TITLE_RULES[b3], "div": b1})
            elif t == 8:
                k = club_of(b2, b3) if club_of else None
                ents.append({"club": k} if k else {"club_team": b2 | b3 << 8, "div": b1})
            else:
                raise ValueError("gplay spec: entrant type %d" % t)
        rounds, pend = [], None                  # (pend: a best_of ranking round, for the next round's P member)
        npl = 0
        for _ in range(nr):
            n, legs, host, level, pairing, gap = o[a:a + 6]
            a += 6
            if legs & 0x80:
                mem = [_member(c) for c in o[a:a + n]]
                bonus = list(o[a + n:a + 2 * n])
                a += 2 * n
                if legs == 0x80:                 # (0 meetings: a best_of ranking round)
                    pend = ("P%d" % npl, mem)
                    npl += n
                    continue
                g = {"rounds": legs & 0x7F, "bonus": bonus, "host": HOSTS[host]}
                rounds.append({"group": g, "members": mem})
                npl += n
                continue
            codes = [_member(c) for c in o[a:a + 2 * n]]
            a += 2 * n
            if pend:
                codes = [{"best_of": pend[1]} if c == pend[0] else c for c in codes]
                pend = None
            nb = pairing & 0x7F
            blk = {"pairing": "draw" if pairing & 0x80 else "best_worst", "clubs": codes[:nb]} if nb else None
            ties = [codes[k:k + 2] for k in range(nb, len(codes), 2)]
            rd = {"ties": ([blk] if blk else []) + ties}    # (respell() may move the block to the round)
            rd["legs"] = 1 if legs == 3 else legs
            if legs == 3:
                rd["series"] = 3
            rd["host"] = HOSTS[host & 0x7F]
            rd["level"] = LEVELS[level]
            if host & SKIP_SAME:
                rd["skip_if_same"] = True
            if gap:
                rd["gap"] = gap
            rounds.append(rd)
        po["entrants"] = ents
        po["rounds"] = rounds
        if kind == 8:
            po["stage"] = wd & 3
        else:
            if kind == 1 or wd:
                po["winner_div"] = wd
            if kind in (1, 4) or ld:
                po["loser_div"] = "reserve" if ld == RESERVE else ld
        if fl & 1:
            po["skip_qualified"] = True
        if fl & 2:
            po["host_by_record"] = True
        if fl & 4:
            po["supercup"] = True
        if fl & 8:
            po["draw_pot"] = True
        if (fl >> 4) & 7:
            po["winners"] = (fl >> 4) & 7
        if fl & 0x80:
            po["skip_second"] = True
        pos.append(po)
    return pos, a + 1


def respell(po, rn):
    """A best_worst / draw block that is a round's only ties is written as the round itself ("ties": [], "pairing",
    "clubs": TRUTH's usual spelling, 11 of 13), not as its first tie {"pairing", "clubs"} -- the same bytes, but the
    Mod's default round names (default_rnames) see a round without ties: True when the defaults are the exe's
    (rn: GP_RNT's bytes), else the play-off needs its 'round_names'."""
    for r in po.get("rounds") or []:
        if len(r.get("ties") or []) == 1 and isinstance(r["ties"][0], dict):
            blk = r.pop("ties")[0]
            rest = {k: r.pop(k) for k in list(r)}
            r.update({"ties": []}, **rest)
            r.update(pairing=blk["pairing"], clubs=blk["clubs"])
    return _rn_bytes(default_rnames(po)) == rn


def default_rnames(po):
    """The Mod's gplay.py _default_rnames (MIT, Davide Lorigliola): a play-off's round names when the pack gives
    none (None: the game's own)."""
    rs = po.get("rounds") or []
    g = [i for i, r in enumerate(rs) if "group" in r]
    if g and g == list(range(len(g))) and len(g) < len(rs):
        ko = rs[len(g):]
        if len(ko) <= 4 and all("group" not in r and r.get("legs", 1) in (1, 2) and (r.get("ties") or r.get("clubs"))
                                for r in ko) and len(ko[-1].get("ties") or []) == 1:
            return [None] + ["R16", "QF", "SF", "F"][4 - len(ko):]
        return []
    if not rs or len(rs) > 4 or len(rs[-1].get("ties") or []) != 1 or \
            any("group" in r or r.get("legs", 1) not in (1, 2) or not r.get("ties") for r in rs):
        return []
    return ["R16", "QF", "SF", "F"][4 - len(rs):]


def _rn_bytes(names):
    return bytes(RN_KEYS[x] if x else 0 for x in names) + bytes(RN_KG - len(names))


# ---- the split stages' after-stage routines ----------------------------------------------------------------------

def ba_stubs(exe):
    """gplay's ba_<name> stubs: {obj1 offset: the play-off index it builds}."""
    o = exe.obj[1]
    out, bpi = {}, {}
    for m in re.finditer(rb"\xc7\x05(.{4})(.{4})\xe8.{4}\xa3(.{4})\xe9", o, re.S):
        i = struct.unpack_from("<I", o, m.start(2))[0]
        t, w = exe.target(1, m.start(1)), exe.target(1, m.start(3))
        if t and w and i < 64:
            bpi.setdefault((t, w), {})[m.start()] = i
    for v in bpi.values():                     # (the GP_BPI / GP_BWEEK pair the stubs share: they are SP_CB's)
        if any(exe.refs.get((1, s)) for s in v):
            out.update(v)
    return out


def stage_callbacks(exe):
    """Every split stage whose 'then' builds a play-off: {(world, team file, division, stage index): play-off index}
    (split.py SP_TAB: entries of +0 world, +1 file, +2 division, +7 stages, +14 word length, +16 stage records of 28
    bytes, +22 the callback (1+: SP_CB))."""
    o = exe.obj[1]
    stubs = ba_stubs(exe)
    if not stubs:
        return {}
    cbs = set()
    for s in stubs:
        for r in exe.refs.get((1, s), []):
            if r[0] == 1:
                cbs.add(r[1])
    sp_cb = None
    for m in re.finditer(rb"\xff\x14\x85", o):
        t = exe.target(1, m.start() + 3)
        if t and t[0] == 1 and t[1] + 4 <= min(cbs) and all((c - t[1] - 4) % 4 == 0 for c in cbs):
            k, ok = t[1] + 4, True
            while exe.target(1, k):
                k += 4
            if max(cbs) < k and (sp_cb is None or t[1] + 4 > sp_cb[0]):
                sp_cb = (t[1] + 4, k)
    if sp_cb is None:
        raise ValueError("split: SP_CB not found")
    table = [exe.target(1, x) for x in range(sp_cb[0], sp_cb[1], 4)]
    a = sp_cb[1]
    if o[a] != 0xC3:                            # (sp_nostage: ret; align 4; SP_TAB)
        raise ValueError("split: sp_nostage not after SP_CB")
    a = (a + 4) & ~3
    out = {}
    while struct.unpack_from("<H", o, a + 14)[0]:
        world, file, div, nst = o[a], o[a + 1], o[a + 2], o[a + 7]
        for k in range(nst):
            cb = o[a + 16 + 28 * k + 22]
            if cb and cb <= len(table) and table[cb - 1] and table[cb - 1][0] == 1 and table[cb - 1][1] in stubs:
                out[(world, file, div, k)] = stubs[table[cb - 1][1]]
        a += struct.unpack_from("<H", o, a + 14)[0]
    return out


# ---- the league struct's names (a 'title' play-off's default record line) ----------------------------------------

def _league_name(exe, lg, str_base):
    """The league struct's first division long name (countries.py: 13-byte header, 6 bytes per division, 0, then
    2 name dwords per division from STR_BASE), or None."""
    o = exe.obj[lg[0]]
    a = lg[1]
    if o[a + 5] == 0:
        return None
    nd = o[a + 9]
    at = a + 13 + 6 * nd + 1
    rel = struct.unpack_from("<I", o, at)[0]
    s = exe.cstr(2, str_base + rel, 64)
    return s.decode("latin-1") if s and s.isupper() else None


# ---- the SEASON WINNERS page's lines (DATA\\FMTEN<world>.DAT) ------------------------------------------------------

def win_titles(data_dir, world_byte, lang="EN"):
    """The Mod's fmtgame/fmtpage FMTxx<world>.DAT WIN entries (MIT, Davide Lorigliola): 'FMT1', word pages, word
    wins, 48-byte index entries (db contest id, 41-byte name, dd text offset, dw length), the texts; a WIN text is
    lines 'title 01h source', its first '01h H ff dd' (team file, division). -> {team file: {play-off index:
    title}} from the sources K pp, D s pp ss, Q r pp (fmtgame._wn_fit: the pack's raw long name, upper case, when it
    fits 298 px; 'PLAYOFFS - ' + WN_TITLES' title for those)."""
    import os
    path = None
    for x in os.listdir(data_dir):
        if x.upper() == "FMT%s%d.DAT" % (lang, world_byte):
            path = os.path.join(data_dir, x)
    if path is None:
        return {}
    d = open(path, "rb").read()
    if d[:4] != b"FMT1":
        raise ValueError("%s: not FMT1" % path)
    npg, nw = struct.unpack_from("<HH", d, 4)
    out = {}
    for k in range(npg, npg + nw):
        a = 8 + 48 * k
        off, ln = struct.unpack_from("<IH", d, a + 42)
        lines = [x.split(b"\x01", 1) for x in d[off:off + ln].split(b"\n") if b"\x01" in x]
        f = None
        for title, src in lines:
            src = src.decode("ascii")
            if src[:1] == "H":
                f = int(src[1:3], 16)
            elif f is not None and src[:1] in "KDQ":
                pi = int(src[1:3] if src[0] == "K" else src[2:4], 16)
                out.setdefault(f, {}).setdefault(pi, title.decode("latin-1"))
    return out


# ---- the Mod's name fitting, undone where the exe proves it ---------------------------------------------------------
# The Mod's tools/complist.py (MIT, Davide Lorigliola) fit_titles fits every play-off's names before gplay compiles
# them: the short one to the career calendar's label (fit_short, CAL_PX), the long one so '<country> <long>' fits the
# game's title (TITLE_MAX) and VISUAL. COMPETIZ.'s button (VC_PX) (_fit). The exe holds the fitted names; unfit_*
# look for the pack's name: candidates from the fitted words (an abbreviation's word back, a cut word completed from a
# word of the long name, a hand-picked form's original), accepted only when the Mod's own fit gives the exe's name
# back. Ported (the same code, 4.0.5 and 4.1.1 alike).
TITLE_MAX, VC_PX, CAL_PX, SHORT_MAX = 34, 158, 70, 16
SMALL_W = dict({c: 6 for c in 'ABCDEFGHJKLMNOPQRSTUVWXYZ023456789!"#$%&*+/<=>?@'},
               **{"I": 2, "1": 2, "'": 2, "(": 3, ")": 3, ",": 2, "-": 4, ".": 2, ":": 2, " ": 3})
FIT_SHORT = {
    'AUSTRALIA CUP': 'AUST. CUP', 'BULGARIAN CUP': 'BULG. CUP', "EMPEROR'S CUP": 'EMPEROR CUP', 'PUCHAR POLSKI': 'PUCHAR POL.',
    'SCHWEIZER CUP': 'SCHWEIZ. CUP', 'RELEGATION 1/2': 'RELEG. 1/2', 'RELEGATION 2/3': 'RELEG. 2/3', 'SEGUNDA FINAL': 'SEG. FINAL',
    'LANDESMEISTER': 'LANDESMEIST.', 'CAMPIONATO SMR': 'CAMP. SMR', 'BEKER VAN BELGIE': 'BEKER BELGIE',
    'COPA COSTA RICA': 'COPA C. RICA', 'CONCACAF CHAMP.': 'CONCACAF CC', 'AFC CHALLENGE L.': 'AFC CHALL. L.',
    'CAF CHAMPIONS L.': 'CAF CHAMP. L.', 'CAF CONFED. CUP': 'CAF CONF. CUP', 'CAF SUPER CUP': 'CAF SUPERCUP',
    'CHAMPIONS LEAGUE': 'CHAMPIONS L.', 'EUROPA LEAGUE': 'EUROPA L.', 'CUP WINNERS CUP': 'CUP W. CUP', 'GHANA PREM. LGE': 'GHANA PREM.',
    'EGYPT LEAGUE CUP': 'EGY LGE CUP', 'LIGA DE PRIMERA': 'LIGA PRIMERA', 'COPA DE LA LIGA': 'COPA LIGA',
    'P.NACIONAL FINAL': 'P. NAC. FINAL', 'CUPA MOLDOVEI': 'CUPA MOLDOVA', 'MALTA PREMIER': 'MALTA PREM.',
    'COPA COLOMBIA': 'COPA COL.', 'CYMRU PREMIER': 'CYMRU PREM.', 'LIGA PORTUGAL': 'LIGA PORT.',
}
ABBR = {'LEAGUE': 'LGE', 'DIVISION': 'DIV.', 'DIVISIO': 'DIV.', 'DIVISAO': 'DIV.', 'PREMIER': 'PREM.',
        'PROFESIONAL': 'PROF.', 'CHAMPIONS': 'CHAMP.', 'CHAMPIONNAT': 'CHAMP.', 'CAMPIONI': 'CAMP.', 'CAMPEONATO': 'CAMP.',
        'GIRONE': 'GIR.', 'GRUPO': 'GR.', 'NATIONAL': 'NAT.', 'NACIONAL': 'NAC.', 'NATIONALLIGA': 'NATIONALL.',
        'PLAY-OFFS': 'P-OFFS', 'PLAY-OFF': 'P-OFF', 'COUPE': 'C.', 'COPPA': 'C.', 'PRESIDENT': 'PRES.',
        "PRESIDENT'S": "PRES.", 'CONSTITUCIO': 'CONST.', 'SUPERIORE': 'SUP.', 'KNOCKOUT': 'K.O.', 'NORTHERN': 'NORTH.',
        'SOUTHERN': 'SOUTH.', 'CENTRAL': 'CENT.', 'OCCIDENTE': 'OCC.', 'OCCIDENT': 'OCC.', 'ORIENTE': 'OR.',
        'EXPANSION': 'EXP.', 'QUEENSLAND': 'QLD', 'LUXEMBOURG': 'LUX.', 'LUXEMB.': 'LUX.', 'PORTUGAL': 'PORT.',
        'URUGUAY': 'URU.', 'VENEZUELA': 'VEN.', 'COLOMBIA': 'COL.', 'PARAGUAY': 'PAR.', 'ARGENTINA': 'ARG.',
        'BRASIL': 'BRA.', 'TUNISIE': 'TUN.', "D'ALGERIE": "D'ALG.", 'SHQIPERISE': 'SHQ.', 'SHQIPERIS': 'SHQ.',
        'SVENSKA': 'SV.', 'SCOTTISH': 'SCOT.', 'IRISH': 'IR.', 'WELSH': 'WELSH', 'BELARUSIAN': 'BLR',
        'MACEDONIAN': 'MKD', 'AZERBAIJAN': 'AZE', 'KAZAKHSTAN': 'KAZ', 'CANADIAN': 'CAN.', 'CHAMP.': 'CH.'}
VC_ABBR = dict(ABBR, **{'BUNDESLIGA': 'BUNDESL.', 'RELEGATION': 'RELEG.', 'CHAMPIONSHIP': 'CHAMP.', 'PROMOZIONE': 'PROMOZ.',
                        'PROMOTION': 'PROMO.', 'GROUPE': 'GR.', 'GRUPPE': 'GR.', 'GROUP': 'GR.', 'PLAYOFFS': 'P-OFFS',
                        'PLAY-OUTS': 'P-OUTS', 'PLAY-OUT': 'P-OUT', 'PLAYOUTS': 'P-OUTS', 'BARRAGES': 'BARR.',
                        'PROFESSIONNELLE': 'PROF.', 'INDEPENDENCE': 'IND.', 'NOGOMETNA': 'NOG.', 'NOGOMETNI': 'NOG.',
                        'EUROPEO': 'EUR.', 'EUROPEEN': 'EUR.', 'EUROPACUP-PLAY-OFF': 'EUROPA PLAY-OFF'})
FIT_NAMES = {   # complist.FIT_NAMES, English and '*': original long name -> the long name used
    'SEGUNDA DIVISION OCCIDENTE': 'SEGUNDA OCCIDENTE', 'SEGUNDA DIVISION ORIENTE': 'SEGUNDA ORIENTE',
    'TERCERA DIVISION OCCIDENTE': 'TERCERA OCCIDENTE', 'TERCERA DIVISION ORIENTE': 'TERCERA ORIENTE',
    'CAMPEONATO BRASILEIRO SERIE A': 'BRASILEIRAO SERIE A', 'CAMPEONATO BRASILEIRO SERIE B': 'BRASILEIRAO SERIE B',
    'PLAY-OFF CONFERENCE LEAGUE': 'PLAY-OFF CONFERENCE', 'EUROPA CONFERENCE PLAY-OFF': 'PLAY-OFF CONFERENCE',
    'RELEGATION 2. BUNDESLIGA': 'RELEGATION 2./3. LIGA', 'SKUPINA O UMISTENI 7-10': 'PLACES 7-10',
    'TAIWAN FOOTBALL PREMIER LEAGUE': 'PREMIER LEAGUE', 'TAIWAN FOOTBALL LEAGUE DIV. 2': '2ND DIVISION'}
NATWORDS = {'EGYPTIAN', 'ARMENIAN', 'BELARUSIAN', 'UKRAINIAN', 'RUSSIAN', 'SAUDI', 'SCOTTISH', 'SWISS', 'SLOVAK',
            'KAZAKHSTAN', 'IRISH', 'WELSH', 'GIBRALTAR', 'SURINAME', 'LIECHTENSTEIN', 'CANADIAN', 'TAIWAN', 'MACEDONIAN',
            'SLOVENIAN', 'CROATIAN', 'GREEK', 'CYPRIOT', 'MALTESE', 'ALBANIAN', 'LATVIAN', 'ESTONIAN', 'LITHUANIAN'}


def px(s):
    return sum(SMALL_W.get(c, 6) for c in s.upper())


def fit_short(s):
    """complist.fit_short (English)."""
    s = s.upper()
    x = FIT_SHORT.get(s)
    if x:
        return x
    if px(s) <= CAL_PX:
        return s
    words = s.split()
    for i in range(len(words) - 1, -1, -1):
        if px(" ".join(words)) <= CAL_PX:
            return " ".join(words)
        if words[i] in ABBR:
            words[i] = ABBR[words[i]]
    for _ in range(12):
        if px(" ".join(words)) <= CAL_PX:
            return " ".join(words)
        cand = [i for i, w in enumerate(words) if len(w.rstrip(".")) > 4 and not w[0].isdigit()]
        if not cand:
            break
        i = max(cand, key=lambda k: px(words[k]))
        w = words[i].rstrip(".")
        n = len(w) - 1
        while n > 3 and px(" ".join(words[:i] + [w[:n] + "."] + words[i + 1:])) > CAL_PX:
            n -= 1
        words[i] = w[:max(n, 3)] + "."
    out = ""
    for c in " ".join(words):
        if px(out + c) > CAL_PX:
            break
        out += c
    return out.rstrip(" .") + ("." if px(out.rstrip(" .") + ".") <= CAL_PX and out != " ".join(words) else "")


def _fit_old(long_, short, room):
    for x in (FIT_NAMES.get(long_), long_, short):
        if x and len(x) <= room:
            return x
    words, w = long_.split(), ""
    for x in words:
        if len(w) + (1 if w else 0) + len(x) > room:
            break
        w = "%s %s" % (w, x) if w else x
    return w or short[:room]


def fit_long(country, long_, short):
    """complist.fit_titles' long name: complist._fit when '<country> <long>' does not fit, else the long name."""
    if not (len(country) + 1 + len(long_) > TITLE_MAX or px("%s %s" % (country, long_)) > VC_PX):
        return long_
    room = TITLE_MAX - len(country) - 1
    ok = lambda x: bool(x) and len(x) <= room and px("%s %s" % (country, x)) <= VC_PX
    old = _fit_old(long_, short, room)
    if ok(old):
        return old
    fn = FIT_NAMES.get(long_)
    cands = []
    for base in filter(None, (fn, long_)):
        w = base.split()
        if len(w) > 2 and w[0] in NATWORDS:
            w = w[1:]
        cands.append(list(w))
    for w in cands:
        if ok(" ".join(w)):
            return " ".join(w)
    for w in cands:
        w = list(w)
        for i in range(len(w) - 1, -1, -1):
            if w[i] in VC_ABBR:
                w[i] = VC_ABBR[w[i]]
                if ok(" ".join(w)):
                    return " ".join(w)
    if ok(short):
        return short
    words, w = (cands[-1] if cands else long_.split()), ""
    for x in words:
        if not ok("%s %s" % (w, x) if w else x):
            break
        w = "%s %s" % (w, x) if w else x
    return w or short[:room]


def _unfit(fitted, fit, abbr, pool, restrict, maxlen=99, limit=4096):
    """The pack's name the exe's fitted one came from: a hand-picked form's original, else the candidates c with
    fit(c) == fitted and c != fitted -- each word as it is, an abbreviation's word back (restrict: only a word of pool),
    or a cut word 'X.' (not itself an abbreviation) completed to any longer prefix of a pool word -- at most maxlen
    characters, fewest words changed first, then the fewest words that are not whole pool words, then the shortest;
    None when there is none (the fitted name may be the pack's own)."""
    import itertools
    hand = [k for k, v in FIT_SHORT.items() if v == fitted] if abbr is ABBR else []
    if hand:
        return hand[0]
    rev = {}
    for k, v in abbr.items():
        if not restrict or k in pool:
            rev.setdefault(v, set()).add(k)
    known = set(abbr.values()) | {w for v in FIT_SHORT.values() for w in v.split()}
    opts = []
    for w in fitted.split():
        o = {w} | rev.get(w, set())
        if w.endswith(".") and len(w) > 2 and w not in known:
            o |= {x[:n] for x in pool if x.startswith(w[:-1]) for n in range(len(w), len(x) + 1)}
        opts.append(sorted(o))
    good = set()
    for combo in itertools.islice(itertools.product(*opts), limit):
        c = " ".join(combo)
        if c != fitted and len(c) <= maxlen and fit(c) == fitted:
            good.add(c)
    good = sorted(good, key=lambda c: (sum(a != b for a, b in zip(c.split(), fitted.split())),
                                       sum(a != b and a not in pool for a, b in zip(c.split(), fitted.split())), len(c), c))
    return good[0] if good else None


def unfit_short(short, long_):
    """The pack's short name the exe's (fit_short's) came from (the long name's words complete it), or the exe's.
    SHORT_MAX: every short name of the Mod's packs is at most 16 characters (4.0.5: 16 x 56, none longer)."""
    return _unfit(short, fit_short, ABBR, set(long_.split()), True, SHORT_MAX) or short


def unfit_long(long_, short, country):
    """The pack's long name the exe's (fit_long's, for '<country> <long>') came from, or the exe's."""
    if country is None:
        return long_
    return _unfit(long_, lambda c: fit_long(country, c, short), VC_ABBR, set(short.split()), False) or long_


def page_countries(data_dir, world_byte, lang="EN"):
    """FMTxx<world>.DAT's page names '<country> <fitted long>' (fmtgame write: titles[lang][:40]) -> {name: [index
    entry names]} for unfit_long's country: the names ending in ' <long>'."""
    import os
    for x in os.listdir(data_dir):
        if x.upper() == "FMT%s%d.DAT" % (lang, world_byte):
            d = open(os.path.join(data_dir, x), "rb").read()
            npg = struct.unpack_from("<H", d, 4)[0]
            return [d[8 + 48 * k + 1:8 + 48 * k + 42].split(b"\0")[0].decode("latin-1") for k in range(npg)]
    return []


def country_of(pages, long_):
    c = {p[:-len(long_) - 1] for p in pages if p.endswith(" " + long_) and len(p) > len(long_) + 1}
    return c.pop() if len(c) == 1 else None


# ---- the decoder -------------------------------------------------------------------------------------------------

def decode(world):
    exe = world.exe
    o = exe.obj[1]
    tabs, loc = tables(exe)
    sb = loc["STR_BASE"]
    cbs = stage_callbacks(exe)
    wins, pages = {}, {}
    packs = {}
    for i, (wbyte, lg, spec_at, file) in enumerate(tabs):
        code = world.code_of.get(file) if file is not None else None
        if code is None:
            continue

        def club_of(f, ordinal):
            r = world.word(f | ordinal << 8)
            return r[1] if r else None
        pos, _ = decode_spec(o, spec_at, club_of)
        nmt = exe.target(1, loc["GP_NMT"] + 4 * i)[1]
        rnt = exe.target(1, loc["GP_RNT"] + 4 * i)[1]
        tht = exe.target(1, loc["GP_THT"] + 4 * i)[1]
        names = []
        for k in range(len(pos)):
            rl, rs, rr = struct.unpack_from("<3I", o, nmt + 12 * k)
            names.append(tuple(exe.cstr(2, sb + x, 64).decode("latin-1") for x in (rl, rs, rr)))
        if struct.unpack_from("<3I", o, nmt + 12 * len(pos)) != (0, 0, 0):
            raise ValueError("%s: GP_NMT block does not end after %d play-offs" % (code, len(pos)))
        lname = _league_name(exe, lg, sb)
        if wbyte not in wins:
            wins[wbyte] = win_titles(world.data, wbyte)
            pages[wbyte] = page_countries(world.data, wbyte)
        win = wins[wbyte].get(file, {})
        keys = ["%s_po%d" % (code.lower(), k) for k in range(len(pos))]
        longs = [n[0] for n in names]
        for k, po in enumerate(pos):
            w = win.get(k)
            if w is None:
                continue
            if w.startswith(WN_PO) and po["kind"] == "title" and w[len(WN_PO):] in WN_KEYS:
                keys[k] = WN_KEYS[w[len(WN_PO):]]
            elif not (w == names[k][1] != names[k][0]):     # (_wn_fit's fall-back to the short name: not a long)
                longs[k] = w
        shorts = [unfit_short(n[1], longs[k]) for k, n in enumerate(names)]
        for k, n in enumerate(names):
            if longs[k] == n[0]:                            # (no WINNERS line: the exe's fitted long, undone)
                longs[k] = unfit_long(n[0], shorts[k], country_of(pages[wbyte], n[0]))
        out = []
        for k, po in enumerate(pos):
            ln, sh, rec = names[k]
            d = {"key": keys[k], "names": {"en": [longs[k], shorts[k]]}}
            d.update(po)
            if rec != ln and not (po["kind"] == "title" and lname is not None and rec == lname):
                d["record"] = {"en": rec}
            then = o[tht + k]
            if then:
                d["then_build"] = keys[then - 1]
            rn = bytes(o[rnt + RN_KG * k:rnt + RN_KG * (k + 1)])
            if not respell(d, rn):
                d["round_names"] = [RN_NAME.get(x) if x else None for x in rn.rstrip(b"\0")]
            out.append(d)
        for (w, f, dv, st), pi in sorted(cbs.items()):
            if w == wbyte and f == file and pi < len(out):
                out[pi]["build_after"] = then_name(code, pi)
        packs[code] = {"competitions": [{"key": "league", "ko_playoffs": out}]}
    return {"packs": packs}


if __name__ == "__main__":
    import json
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import world_mod_exe
    print(json.dumps(decode(world_mod_exe.World(sys.argv[1])), indent=1, ensure_ascii=False))
