#!/usr/bin/env python3
"""world_mod_supercups.py -- World 2026-27's Super Cups, decoded from an installed SWOS 96/97 Mod (its patched DOS
exe): a fragment for scripts/world_from_mod.py (docs/WORLD_2026_27.md 6d, 6g).

    decode(world) -> {"supercups": {"ita": {...}, ..., "uefa": {...}}}

The Mod's tools/supercup.py compiles tools/supercups2627.json into gplay's GP_SCT table (SC_TAB in gplay.py's data:
`mov dword [GP_SCT], SC_TAB` points at it): db count; per Super Cup 36 bytes -- db the league's team file (FFh: the
UEFA Super Cup, world-level), the national cup's contest id, from_season | 80h early, the first match's week, legs,
level (3 penalties, 1 extra time then penalties), n (2 one match, 4 a final four), the final's week; db rule[4]
(SCR_* | the fallback << 4); dw the world's first season's entrants[4] (team words); dd long name, short name
(offsets from the game's string base); db the round names' codes (semi-final, final), the stage-title play-offs
(title0 / title1: the pack's ko_playoffs indices); dd the season record's name. The table is found by what it holds
(every team file a country's or FFh, every byte in range) and that something points at it, never by address.

Written as supercups2627.json has it (its keys; the club keys of the packs, 'code:key' for the UEFA one), plus
"cup_id" (the national cup's contest id: the Mod's generator's seed for the final four's other semi-final) and
"titles" as the pack's ko_playoffs indices. Names: {"en": [long, short], "*": the same}, "record": {"en": ...} -- the
English exe's (the long one fitted by complist._fit to the country's button: scripts/world_competition_names_2627.csv
gives the full ones). Standard library only.
"""
import struct

import world_mod_cups as cups

SC_E = 36
RULES = {1: "champion", 2: "second", 3: "cup_winner", 4: "cup_finalist", 5: "third", 6: "fourth",
         7: "title0", 8: "title1", 9: "uefa_cl", 10: "uefa_el", 11: "uefa_ecl"}   # (gplay.c SCR_*)
LEVELS = {3: "pens", 1: "et_pens"}                                                   # (gplay.c LV_*)
UEFA_FILE = 0xFF


def parse_sct(world, obj, off):
    d = world.exe.obj[obj]
    n = d[off]
    if not 1 <= n <= 32 or off + 1 + SC_E * n > len(d):
        return None
    out = []
    for i in range(n):
        x = bytes(d[off + 1 + SC_E * i:off + 1 + SC_E * (i + 1)])
        lf, cid, fs, w1, legs, lev, nn, w2 = x[:8]
        if not (lf == UEFA_FILE or lf in world.code_of) or legs != 1 or lev not in LEVELS or nn not in (2, 4):
            return None
        if (fs & 0x7F) not in (1, 2) or fs & 0x60 or not 0 < w1 <= w2 < 128:
            return None
        rules = list(x[8:12])
        if any((r & 15) not in RULES for r in rules[:nn]) or any(rules[nn:]):
            return None
        out.append(x)
    return out if any(x[0] == UEFA_FILE for x in out) or len(out) >= 2 else None


def decode(world):
    e = world.exe
    sct = cups.find_parsed(e, lambda o, a: parse_sct(world, o, a), (1, 2))
    if not sct:
        return {}
    base = cups.str_base(e)
    out = {}
    for x in sct:
        lf, cid, fs, w1, legs, lev, n, w2 = x[:8]
        rules = list(x[8:8 + n])
        ents = struct.unpack_from("<4H", x, 12)[:n]
        lo, sh = struct.unpack_from("<II", x, 20)
        rec = struct.unpack_from("<I", x, 32)[0]
        uefa = lf == UEFA_FILE
        code = "uefa" if uefa else world.code_of[lf].lower()
        first = []
        for w in ents:
            c = world.word(w)
            if not c:
                raise ValueError("supercup %s: entrant %04X of no club" % (code, w))
            first.append("%s:%s" % (c[0].lower(), c[1]) if uefa else c[1])
        fb = [RULES[r >> 4] if r >> 4 else None for r in rules]
        sc = {"date": cups.week_date(w1)}
        if n == 4:
            sc["final_date"] = cups.week_date(w2)
        sc.update({"from_season": fs & 0x7F, "early": bool(fs & 0x80), "legs": legs, "level": LEVELS[lev],
                   "host": "neutral", "rule": [RULES[r & 15] for r in rules],
                   "if_double": fb if n == 4 else fb[1], "first_entrants": first})
        if any(RULES[r & 15].startswith("title") for r in rules):
            sc["titles"] = [x[30], x[31]]
        if cid:
            sc["cup_id"] = cid
        names = [cups.name_at(e, base, lo), cups.name_at(e, base, sh)]
        sc["names"] = {"en": names, "*": names}
        r = cups.name_at(e, base, rec)
        sc["record"] = {"en": r, "*": r}
        out[code] = sc
    return {"supercups": out}
