"""world_mod_europe.py -- World 2026-27's European fields, decoded from an installed SWOS 96/97 Mod (its patched
exe + DATA): per country pack, europe_places, first_europe, uefa_coef and season.league_cup_europe
(docs/WORLD_2026_27.md 6d; the importer's europe() / mod_pack() read them).

Where each lives (found through pointers and code, never fixed offsets: 4.0.5's and 4.1.1's layouts differ):
- The world record (the Mod's tools/careerworld.py, MIT, Davide Lorigliola): WTAB's record of world 5 holds
  `dd W5_CC, W5_CWC, W5_UEFA, W5_TMD, W5_LIST, W5_TCC, W5_TCWC, W5_TUEFA`; it is the one that points at the
  string "data\\\\world05.tmd" (+12). W5_T<cup> -> `dd W5_P<cup>; dw n, n; dd -1`, W5_P<cup> = one country
  league file a place, FF-terminated: europe_places = each country's count.
- The first season's clubs: W5_<cup> is the cup struct (careerworld.lp_struct): 36 (file, ordinal) pairs at
  +30h, the countries in pack order and each country's clubs in its first_europe order (mkseason.world_defs).
  (DATA\\WORLD05.TMD holds the same clubs' team records in the same order.)
- The coefficient seeds (the Mod's tools/uefacoef.py): `mov esi, UC_SEEDC; movzx edx, byte [esi]; test edx, edx; jz`
  -- UC_SEEDC = records of (db league file, 5 x dd thousandths), 0-terminated; the next `mov esi, UC_SEEDK;
  movzx eax, word [esi]; test eax, eax; jz` -- UC_SEEDK = (dw team word, dw units = round(v * 8 / 5)), 0-ended.
  The country values are int(round(v * 1000)): every value the Mod's packs hold has <= 3 decimals, so v = n / 1000
  is the exact float. A club's value is rounded to an eighth's multiple of 5/8 (e.g. 21.88 -> 35 -> 21.875): its
  exact float is NOT recoverable, but n * 5 / 8 gives the importer's very integer back (it is exact in binary).
- season.league_cup_europe (the Mod's tools/uefaq.py UQ_LCEU): `mov al, [UQ_CF]; mov esi, UQ_LCEU; cmp byte [esi],
  0FFh` -- league files whose league cup winner takes a Conference League place, FF-terminated.
Every UEFA country (one with seeds, or with places) gets europe_places and first_europe with all three keys.
Standard library only.
"""
import struct

CUPS = ("cc", "cwc", "uefa")


def _find_world_record(exe, n=5):
    name = b"world%02d.tmd" % n
    for obj in sorted(exe.obj):
        o = exe.obj[obj]
        at = o.lower().find(name)
        while at >= 0:
            s = o.rfind(b"data", max(0, at - 8), at)
            if s >= 0:
                for src in exe.refs.get((obj, s), []):
                    return src[0], src[1] - 12
            at = o.lower().find(name, at + 1)
    raise ValueError("the world record (WTAB, data\\world%02d.tmd) is not in the exe" % n)


def _code_ptr(exe, pattern, before):
    """[(obj, off) target of `<before> imm32` immediately preceding pattern] in obj1's code."""
    out = []
    for at in exe.find_all(1, pattern):
        if exe.obj[1][at - 5:at - 4] == before:
            t = exe.target(1, at - 4)
            if t:
                out.append((at, t))
    return out


def decode(world):
    exe = world.exe
    code_of = world.code_of
    packs = {}

    def pack(f):
        code = code_of.get(f)
        if code is None:
            raise ValueError("league file %d: no country" % f)
        return packs.setdefault(code, {})

    # the world record: places and the first season's clubs
    obj, rec = _find_world_record(exe)
    places, first = {}, {}
    for i, key in enumerate(CUPS):
        cup = exe.target(obj, rec + 4 * i)
        o, b = cup
        clubs = [(exe.u8(o, b + 0x30 + 2 * k), exe.u8(o, b + 0x31 + 2 * k))
                 for k in range(exe.u16(obj, rec + 34 + 2 * i))]
        for f, ordinal in clubs:
            first.setdefault(code_of[f], {}).setdefault(key, []).append(world.key[(f, ordinal)])
        t = exe.target(obj, rec + 20 + 4 * i)
        p = exe.target(*t)
        k = 0
        while exe.u8(p[0], p[1] + k) != 0xFF:
            f = exe.u8(p[0], p[1] + k)
            places.setdefault(code_of[f], {}).setdefault(key, 0)
            places[code_of[f]][key] += 1
            k += 1

    # the coefficient seeds
    seeds = _code_ptr(exe, b"\x0f\xb6\x16\x85\xd2\x74", b"\xbe")
    if len(seeds) != 1:
        raise ValueError("UC_SEEDC: %d candidates" % len(seeds))
    at, (o, b) = seeds[0]
    keyed = [t for k, t in _code_ptr(exe, b"\x0f\xb7\x06\x85\xc0\x74", b"\xbe") if 0 < k - at < 80]
    if len(keyed) != 1:
        raise ValueError("UC_SEEDK: %d candidates" % len(keyed))
    uefa = []
    while exe.u8(o, b):
        f = exe.u8(o, b)
        vals = struct.unpack_from("<5I", exe.obj[o], b + 1)
        pack(f)["uefa_coef"] = {"country": [v / 1000 for v in vals], "clubs": {}}
        uefa.append(code_of[f])
        b += 21
    o, b = keyed[0]
    while exe.u16(o, b):
        w, v = struct.unpack_from("<HH", exe.obj[o], b)
        code, key = world.word(w)
        packs[code]["uefa_coef"]["clubs"][key] = v * 5 / 8
        b += 4

    for code in uefa + [c for c in places if c not in uefa] + [c for c in first if c not in uefa]:
        pk = packs.setdefault(code, {})
        pk["europe_places"] = {k: places.get(code, {}).get(k, 0) for k in CUPS}
        pk["first_europe"] = {k: first.get(code, {}).get(k, []) for k in CUPS}

    # season.league_cup_europe
    lceu = []
    for at2 in exe.find_all(1, b"\x80\x3e\xff"):
        if exe.obj[1][at2 - 5] == 0xBE and exe.obj[1][at2 - 10] == 0xA0:
            t = exe.target(1, at2 - 4)
            if t and exe.target(1, at2 - 9):
                lceu.append(t)
    if len(lceu) != 1:
        raise ValueError("UQ_LCEU: %d candidates" % len(lceu))
    o, b = lceu[0]
    while exe.u8(o, b) != 0xFF:
        pack(exe.u8(o, b)).setdefault("season", {})["league_cup_europe"] = True
        b += 1
    return {"packs": packs}


if __name__ == "__main__":
    import json
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from world_mod_exe import World
    print(json.dumps(decode(World(sys.argv[1])), indent=1, sort_keys=True))
