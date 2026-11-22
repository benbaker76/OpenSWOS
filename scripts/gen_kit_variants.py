#!/usr/bin/env python3
"""gen_kit_variants.py -- the kit colour variants' tables, from scripts/kit_variants/kit_variants.json.

One source for the game and the swos.eu replayer (docs: the kit variants plan). It writes
src/kit_variants_table.inc:

  - kKitVariants[]: every variant -- its base colour (0-9), its number within that base (1-15),
    its colour (Amiga 12-bit, 0x0RGB) and its name for the menus;
  - kKitSimilar[]: one row per colour (the 10 base colours first, then the variants in the
    file's order), bit j set when colour j is too like it: CIEDE2000 below `similar_below`;
  - kKitSimilarBase[]: the same for the 10 base colours alone, in the layout of the original's
    _conflictingBasicTeamColorsTable (bit j = base colour j) -- the real Amiga's table;
  - kKitPatterns[]: the planned shirt patterns: number, the original pattern they fall back to.

Everything is worked out here, so the game compares bits and never computes a colour difference:
the same answer on every machine, which a net game needs.

The base colours' RGB is the game palette's own: the AGA _gamePalette (src/swos_amiga_data_imports.c),
entries 1,2,3,6,10,11,12,13,14,15 -- not repeated in the JSON.

    python3 scripts/gen_kit_variants.py                 write src/kit_variants_table.inc
    python3 scripts/gen_kit_variants.py --check         fail if it is not up to date
    python3 scripts/gen_kit_variants.py --js FILE       also write the replayer's table (ES module)
"""
import argparse
import json
import math
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_JSON = os.path.join(REPO, "scripts", "kit_variants", "kit_variants.json")
PALETTE_C = os.path.join(REPO, "src", "swos_amiga_data_imports.c")
OUT_INC = os.path.join(REPO, "src", "kit_variants_table.inc")
BASE_ENTRIES = (1, 2, 3, 6, 10, 11, 12, 13, 14, 15)   # _shirtColorTable: base colour -> palette entry


def game_palette():
    """The AGA _gamePalette[256] (0x0RGB words) from the C source."""
    src = open(PALETTE_C).read()
    m = re.search(r"#ifdef AGA.*?uint16_t _gamePalette\[256\] = \{(.*?)\};", src, re.S)
    if not m:
        sys.exit("%s: no AGA _gamePalette[256]" % PALETTE_C)
    words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))]
    if len(words) != 256:
        sys.exit("%s: _gamePalette has %d entries, not 256" % (PALETTE_C, len(words)))
    return words


def rgb12_to_8(w):
    return (((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17)


def lab(c):
    def lin(u):
        u /= 255
        return ((u + 0.055) / 1.055) ** 2.4 if u > 0.04045 else u / 12.92
    r, g, b = (lin(v) for v in c)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def ciede2000(c1, c2):
    """CIEDE2000 colour difference of two sRGB colours (kL = kC = kH = 1)."""
    L1, a1, b1 = lab(c1)
    L2, a2, b2 = lab(c2)
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - math.sqrt(Cb ** 7 / (Cb ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360
    h2p = math.degrees(math.atan2(b2, a2p)) % 360
    dLp, dCp = L2 - L1, C2p - C1p
    if C1p * C2p == 0:
        dhp = 0
    else:
        dhp = h2p - h1p
        if dhp > 180:
            dhp -= 360
        elif dhp < -180:
            dhp += 360
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp / 2))
    Lbp, Cbp = (L1 + L2) / 2, (C1p + C2p) / 2
    if C1p * C2p == 0:
        hbp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        hbp = (h1p + h2p) / 2
    else:
        hbp = (h1p + h2p + 360) / 2 if h1p + h2p < 360 else (h1p + h2p - 360) / 2
    T = (1 - 0.17 * math.cos(math.radians(hbp - 30)) + 0.24 * math.cos(math.radians(2 * hbp))
         + 0.32 * math.cos(math.radians(3 * hbp + 6)) - 0.20 * math.cos(math.radians(4 * hbp - 63)))
    dth = 30 * math.exp(-(((hbp - 275) / 25) ** 2))
    RC = 2 * math.sqrt(Cbp ** 7 / (Cbp ** 7 + 25 ** 7))
    SL = 1 + 0.015 * (Lbp - 50) ** 2 / math.sqrt(20 + (Lbp - 50) ** 2)
    SC = 1 + 0.045 * Cbp
    SH = 1 + 0.015 * Cbp * T
    RT = -math.sin(math.radians(2 * dth)) * RC
    return math.sqrt((dLp / SL) ** 2 + (dCp / SC) ** 2 + (dHp / SH) ** 2 + RT * (dCp / SC) * (dHp / SH))


def load():
    d = json.load(open(SRC_JSON, encoding="utf-8"))
    pal = game_palette()
    base = sorted(d["base"], key=lambda b: b["base"])
    if [b["base"] for b in base] != list(range(10)):
        sys.exit("kit_variants.json: base colours must be 0-9, once each")
    colours = [{"base": b["base"], "var": 0, "rgb": pal[BASE_ENTRIES[b["base"]]],
                "name_en": b["name_en"], "name_de": b["name_de"]} for b in base]
    seen = set()
    for v in d["variants"]:
        key = (v["base"], v["var"])
        if not 0 <= v["base"] <= 9 or not 1 <= v["var"] <= 15 or key in seen:
            sys.exit("kit_variants.json: bad or repeated variant %r" % (v,))
        if not re.fullmatch(r"0x0[0-9A-Fa-f]{3}", v["rgb"]):
            sys.exit("kit_variants.json: %s: rgb must be 0x0RGB" % v["name_en"])
        seen.add(key)
        colours.append({"base": v["base"], "var": v["var"], "rgb": int(v["rgb"], 16),
                        "name_en": v["name_en"], "name_de": v["name_de"], "legacy": v.get("legacy")})
    legacy = [c for c in colours if c.get("legacy") == 10]
    if len(legacy) != 1:
        sys.exit("kit_variants.json: exactly one variant must be legacy 10 (purple)")
    if len(colours) > 64:
        sys.exit("kit_variants.json: more than 64 colours do not fit a kKitSimilar row")
    pats = d.get("pattern_variants", [])
    for p in pats:
        if not 1 <= p["ext"] <= 15 or not 0 <= p["fallback"] <= 3:
            sys.exit("kit_variants.json: bad pattern %r" % (p,))
    offi = {}
    for who in ("keeper", "referee"):
        idx, ends = [], []
        tiers = d.get("officials", {}).get(who, [])
        if tiers and isinstance(tiers[0][0], int):
            tiers = [tiers]                             # one tier
        for tier in tiers:
          for base, var, _name in tier:
            found = [i for i, c in enumerate(colours) if c["base"] == base and c["var"] == var]
            if not found:
                sys.exit("kit_variants.json: officials.%s: no colour %d/%d" % (who, base, var))
            idx.append(found[0])
          ends.append(len(idx))
        offi[who] = idx
        offi[who + "_ends"] = ends
    kd = d.get("officials", {}).get("keeper_default")
    found = [i for i, c in enumerate(colours) if kd and c["base"] == kd[0] and c["var"] == kd[1]]
    if not found:
        sys.exit("kit_variants.json: officials.keeper_default is no colour")
    offi["keeper_default"] = found[0]
    d["_officials"] = offi
    return d, colours, pats


def similar_rows(colours, below):
    rows = []
    for c in colours:
        mask = 0
        for j, o in enumerate(colours):
            if ciede2000(rgb12_to_8(c["rgb"]), rgb12_to_8(o["rgb"])) < below:
                mask |= 1 << j
        rows.append(mask)
    return rows


def c_string(s):
    return '"%s"' % s.upper().replace("\\", "\\\\").replace('"', '\\"')


def make_inc(d, colours, pats):
    below = d["similar_below"]
    rows = similar_rows(colours, below)
    n = len(colours)
    out = ["/* src/kit_variants_table.inc -- GENERATED by scripts/gen_kit_variants.py from",
           " * scripts/kit_variants/kit_variants.json. Do not edit: change the JSON and run the script. */",
           "",
           "#define KIT_BASE_COLOURS    10",
           "#define KIT_VARIANT_COUNT   %d" % (n - 10),
           "#define KIT_COLOUR_COUNT    %d     /* the base colours, then the variants */" % n,
           "#define KIT_SIMILAR_BELOW   %d     /* CIEDE2000 */" % below,
           "#define KIT_PATTERN_COUNT   %d" % len(pats),
           "",
           "/* The colour value 10 of the World 2026-27 data (the SWOS 96/97 Mod's purple): this variant. */",
           "#define KIT_LEGACY_PURPLE_BASE  %d" % [c for c in colours if c.get("legacy") == 10][0]["base"],
           "#define KIT_LEGACY_PURPLE_VAR   %d" % [c for c in colours if c.get("legacy") == 10][0]["var"],
           "",
           "/* A variant: its base colour (0-9), its number within that base (1-15), its colour (Amiga",
           " * 12-bit 0x0RGB) and its name for the menus. Index = colour index - KIT_BASE_COLOURS. */",
           "static const struct { uint8_t base, var; uint16_t rgb; const char *name; } kKitVariants[KIT_VARIANT_COUNT] = {"]
    for c in colours[10:]:
        out.append("    { %d, %2d, 0x%04X, %s }," % (c["base"], c["var"], c["rgb"], c_string(c["name_en"])))
    out += ["};", "",
            "/* Colour index (0-9 the base colours, then kKitVariants' order) -> bit j: colour j is too",
            " * like it (CIEDE2000 < KIT_SIMILAR_BELOW). Every colour is like itself. */",
            "static const uint64_t kKitSimilar[KIT_COLOUR_COUNT] = {"]
    for c, r in zip(colours, rows):
        out.append("    0x%016XULL,   /* %s */" % (r, c["name_en"]))
    base_rows = [r & 0x3FF for r in rows[:10]]
    out += ["};", "",
            "/* Colour index x colour index: their CIEDE2000 difference, rounded (255 at most) -- what the",
            " * clash kits pick the colour that differs most by (clash_kits.c). */",
            "static const uint8_t kKitDistance[KIT_COLOUR_COUNT][KIT_COLOUR_COUNT] = {"]
    for c in colours:
        out.append("    { " + ", ".join(str(min(255, round(ciede2000(rgb12_to_8(c["rgb"]), rgb12_to_8(o["rgb"])))))
                                        for o in colours) + " },")
    out += ["};", "",
            "/* The keepers' own colour (a colour index), and the colours a keeper / the referee may change to (colour indices), clash_kits.c. */",
            "#define KIT_KEEPER_DEFAULT   %d" % d["_officials"]["keeper_default"],
            "#define KIT_KEEPER_CHOICES   %d" % len(d["_officials"]["keeper"]),
            "#define KIT_REFEREE_CHOICES  %d" % len(d["_officials"]["referee"]),
            "static const uint8_t kKitKeeperChoices[KIT_KEEPER_CHOICES] = { %s };"
            % ", ".join(str(i) for i in d["_officials"]["keeper"]),
            "static const uint8_t kKitRefereeChoices[KIT_REFEREE_CHOICES] = { %s };"
            % ", ".join(str(i) for i in d["_officials"]["referee"]),
            "/* The keepers' tiers: where each ends in kKitKeeperChoices (greens, greys). */",
            "#define KIT_KEEPER_TIERS     %d" % len(d["_officials"]["keeper_ends"]),
            "static const uint8_t kKitKeeperTierEnd[KIT_KEEPER_TIERS] = { %s };"
            % ", ".join(str(i) for i in d["_officials"]["keeper_ends"])]
    out += ["", "/* The 10 base colours alone, as the original's _conflictingBasicTeamColorsTable (bit j =",
            " * base colour j): the table for the real Amiga and for the base colours everywhere. */",
            "static const uint16_t kKitSimilarBase[KIT_BASE_COLOURS] = {"]
    for c, r in zip(colours[:10], base_rows):
        out.append("    0x%04X,   /* %s */" % (r, c["name_en"]))
    out += ["};", "",
            "/* Planned shirt patterns beyond the original four: their number (1-15, the shirt type's",
            " * upper nibble) and the original pattern (0-3) shown when their sprites are missing. */",
            "static const struct { uint8_t ext, fallback; const char *name; } kKitPatterns[%s] = {"
            % ("KIT_PATTERN_COUNT" if pats else "1")]
    for p in pats:
        out.append("    { %d, %d, %s }," % (p["ext"], p["fallback"], c_string(p["name_en"])))
    if not pats:
        out.append("    { 0, 0, 0 },")
    out += ["};", ""]
    return "\n".join(out)


def make_js(d, colours, pats):
    v = [{"base": c["base"], "var": c["var"], "rgb": "#%02x%02x%02x" % rgb12_to_8(c["rgb"]),
          "name": c["name_en"]} for c in colours[10:]]
    p = [{"ext": x["ext"], "fallback": x["fallback"], "name": x["name_en"]} for x in pats]
    return ("// GENERATED by SWOS United's scripts/gen_kit_variants.py from scripts/kit_variants/kit_variants.json.\n"
            "// Do not edit: change the JSON in SWOS United and run the script.\n"
            "export const KIT_VARIANTS = %s;\nexport const KIT_PATTERNS = %s;\n"
            % (json.dumps(v, indent=1), json.dumps(p, indent=1)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail if src/kit_variants_table.inc is stale")
    ap.add_argument("--js", metavar="FILE", help="also write the swos.eu replayer's table there")
    a = ap.parse_args()
    d, colours, pats = load()
    inc = make_inc(d, colours, pats)
    if a.check:
        have = open(OUT_INC).read() if os.path.exists(OUT_INC) else ""
        if have != inc:
            sys.exit("src/kit_variants_table.inc is out of date: run scripts/gen_kit_variants.py")
        print("src/kit_variants_table.inc is up to date")
        return
    open(OUT_INC, "w", newline="\n").write(inc)
    print("wrote %s: %d colours (%d variants), %d patterns" % (os.path.relpath(OUT_INC, REPO), len(colours),
                                                              len(colours) - 10, len(pats)))
    if a.js:
        open(a.js, "w", newline="\n").write(make_js(d, colours, pats))
        print("wrote %s" % a.js)


if __name__ == "__main__":
    main()
