#!/usr/bin/env python3
"""
palette_to_lut.py -- turn a palette into a colour-lookup (LUT) texture.

The texture is the 2D strip layout Unity, Darkbringer and most retro shaders
use: N tiles of N x N side by side, red across a tile, green down it, blue
from tile to tile. Every texel holds the palette colour nearest to the colour
it stands for, so a shader quantises an image to the palette with one texture
read:

    level = floor(saturate(colour) * (N - 1) + 0.5)
    uv    = ((level.b * N + level.r + 0.5) / (N * N), (level.g + 0.5) / N)

Sample it with nearest filtering. N = 16 gives the usual 256x16 strip.

A port of Darkbringer's CreatePaletteLUT, with more ways in and more ways to
measure "nearest". From Jungool (D:\\Projects\\Baker76\\Jungool); OpenSWOS's paletteq mod
(bin/mods/paletteq) makes its LUTs with it: amiga-ecs, swos-aga, vga-mode13h, c64-colodore.

Palettes:
    an image                every distinct colour in it, in reading order
                            (a palette swatch, or a GIF/PNG to take colours from)
    .act                    Adobe colour table (Photoshop)
    .pal                    JASC-PAL text (Paint Shop Pro, Aseprite) or RIFF PAL
    .gpl                    GIMP palette
    .hex / .txt             one RRGGBB per line (Lospec's "hex" download)
    --builtin NAME          one of the palettes below

Distance (--metric):
    weighted   RGB weighted by each channel's share of brightness (0.299,
               0.587, 0.114). The default: cheap, and keeps dark blues from
               turning grey.
    rgb        plain RGB distance, as CreatePaletteLUT
    lab        CIEDE2000 in CIELAB: perceptually the closest, and slowest

Examples:
    palette_to_lut.py --builtin c64-colodore lut_c64.png
    palette_to_lut.py sweetie-16.hex lut_sweetie.png --metric lab
    palette_to_lut.py nes.act lut_nes.png --levels 32
    palette_to_lut.py --identity lut_stock.png      the unmapped strip, for checking a shader
    palette_to_lut.py --list                         the built-in palettes

Needs Pillow (pip install pillow).
"""

import argparse
import math
import re
import struct
import sys
from pathlib import Path

from PIL import Image

BUILTIN = {
    # Philip Timmermann's measured PAL VIC-II colours, in the C64's own order.
    "c64-colodore": [
        "000000", "ffffff", "813338", "75cec8", "8e3c97", "56ac4d", "2e2c9b", "edf171",
        "8e5029", "553800", "c46c71", "4a4a4a", "7b7b7b", "a9ff9f", "706deb", "b2b2b2",
    ],
    # Pepto's 2001 measurement, the long-standing VICE default.
    "c64-pepto": [
        "000000", "ffffff", "68372b", "70a4b2", "6f3d86", "588d43", "352879", "b8c76f",
        "6f4f25", "433900", "9a6759", "444444", "6c6c6c", "9ad284", "6c5eb5", "959595",
    ],
    # The ZX Spectrum's eight colours, normal and bright.
    "zx-spectrum": [
        "000000", "0000d7", "d70000", "d700d7", "00d700", "00d7d7", "d7d700", "d7d7d7",
        "000000", "0000ff", "ff0000", "ff00ff", "00ff00", "00ffff", "ffff00", "ffffff",
    ],
    # The original Game Boy's four greens.
    "gameboy": ["0f380f", "306230", "8bac0f", "9bbc0f"],
    # CGA mode 4, palette 1 high intensity.
    "cga": ["000000", "55ffff", "ff55ff", "ffffff"],
    # The NES's 2C02, the long-standing table emulators started from (its blacks included once).
    "nes": [
        "7c7c7c", "0000fc", "0000bc", "4428bc", "940084", "a80020", "a81000", "881400",
        "503000", "007800", "006800", "005800", "004058", "000000",
        "bcbcbc", "0078f8", "0058f8", "6844fc", "d800cc", "e40058", "f83800", "e45c10",
        "ac7c00", "00b800", "00a800", "00a844", "008888",
        "f8f8f8", "3cbcfc", "6888fc", "9878f8", "f878f8", "f85898", "f87858", "fca044",
        "f8b800", "b8f818", "58d854", "58f898", "00e8d8", "787878",
        "fcfcfc", "a4e4fc", "b8b8f8", "d8b8f8", "f8b8f8", "f8a4c0", "f0d0b0", "fce0a8",
        "f8d878", "d8f878", "b8f8b8", "b8f8d8", "00fcfc", "f8d8f8",
    ],
    # The TMS9918's 15 colours and transparent-as-black (ColecoVision, MSX, SG-1000).
    "msx": [
        "000000", "21c842", "5edc78", "5455ed", "7d76fc", "d4524d", "42ebf5", "fc5554",
        "ff7978", "d4c154", "e6ce80", "21b03b", "c95bba", "cccccc", "ffffff",
    ],
    # The Virtual Boy's red LEDs: off and three levels.
    "virtual-boy": ["000000", "550000", "aa0000", "ff0000"],
    # Epoch's Super Cassette Vision: each channel 0, 75 or 100% (Wikipedia's table).
    "super-cassette-vision": [
        "0000bf", "000000", "0000ff", "bf00ff", "00ff00", "bfffbf", "00ffff", "00bf00",
        "ff0000", "ffbf00", "ff00ff", "ffbfbf", "ffff00", "bfbf00", "bfbfbf", "ffffff",
    ],
    # PICO-8, the fantasy console (not a machine, but the best-loved 16 colours since).
    "pico-8": [
        "000000", "1d2b53", "7e2553", "008751", "ab5236", "5f574f", "c2c3c7", "fff1e8",
        "ff004d", "ffa300", "ffec27", "00e436", "29adff", "83769c", "ff77a8", "ffccaa",
    ],
}



# --- OpenSWOS's palettes (the paletteq mod, bin/mods/paletteq) ---------------
# Made rather than listed: the ECS gamut and the VGA BIOS palette by rule, SWOS's own AGA palette
# read from the game's source.

def _ecs_gamut():
    """The Amiga OCS/ECS colour space: 4 bits a channel, 4,096 colours (0x0RGB, each x17)."""
    return ["%02x%02x%02x" % (r * 17, g * 17, b * 17) for r in range(16) for g in range(16) for b in range(16)]


def _vga_mode13h():
    """The VGA BIOS's default 256-colour palette (mode 13h), its 6-bit DAC values as 8-bit:
    the 16 EGA colours, 16 greys, then nine rings of 24 hues (three brightnesses, each at three
    saturations), and 8 blacks."""
    six = []
    ega = [(0, 0, 0), (0, 0, 42), (0, 42, 0), (0, 42, 42), (42, 0, 0), (42, 0, 42), (42, 21, 0), (42, 42, 42),
           (21, 21, 21), (21, 21, 63), (21, 63, 21), (21, 63, 63), (63, 21, 21), (63, 21, 63), (63, 63, 21), (63, 63, 63)]
    six += ega
    six += [(g, g, g) for g in (0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63)]

    def ring(lo, a, b, c, hi):
        up = [lo, a, b, c]
        out = [(v, lo, hi) for v in up] + [(hi, lo, v) for v in (hi, c, b, a)]
        out += [(hi, v, lo) for v in up] + [(v, hi, lo) for v in (hi, c, b, a)]
        out += [(lo, hi, v) for v in up] + [(lo, v, hi) for v in (hi, c, b, a)]
        return out

    for r in ((0x00, 0x10, 0x1f, 0x2f, 0x3f), (0x1f, 0x27, 0x2f, 0x37, 0x3f), (0x2d, 0x31, 0x36, 0x3a, 0x3f),
              (0x00, 0x07, 0x0e, 0x15, 0x1c), (0x0e, 0x11, 0x15, 0x18, 0x1c), (0x14, 0x16, 0x18, 0x1a, 0x1c),
              (0x00, 0x04, 0x08, 0x0c, 0x10), (0x08, 0x0a, 0x0c, 0x0e, 0x10), (0x0b, 0x0c, 0x0d, 0x0f, 0x10)):
        six += ring(*r)
    six += [(0, 0, 0)] * 8
    assert len(six) == 256
    return ["%02x%02x%02x" % tuple((v << 2) | (v >> 4) for v in c) for c in six]


def _swos_aga():
    """SWOS's AGA game palette, 256 Amiga 12-bit colours, as src/swos_amiga_data_imports.c has it
    (its `#ifdef AGA` table)."""
    src = Path(__file__).resolve().parent.parent / "src" / "swos_amiga_data_imports.c"
    text = src.read_text(encoding="latin-1")
    block = re.search(r"#ifdef AGA.*?uint16_t _gamePalette\[256\] = \{(.*?)\};", text, re.S)
    if not block:
        sys.exit("swos-aga: no AGA _gamePalette in %s" % src)
    words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{4})", block.group(1))]
    return ["%02x%02x%02x" % (((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17) for w in words]


def _levels_gamut(levels):
    """Every colour whose channels take only these values (a console's DAC steps)."""
    return ["%02x%02x%02x" % (r, g, b) for r in levels for g in levels for b in levels]


def _intellivision():
    """The STIC's 16 colours from Wikipedia's YIQ table (List of video game console palettes)."""
    yiq = [(0.000, 0.000, 0.000), (0.330, -0.733, 0.660), (0.523, 0.666, 0.200), (0.715, 0.266, -0.133),
           (0.413, -0.133, -0.600), (0.577, -0.200, -0.533), (0.853, 0.533, -0.333), (1.000, 0.000, 0.000),
           (0.550, 0.000, 0.000), (0.660, -0.533, -0.266), (0.687, 0.533, -0.066), (0.330, 0.266, -0.266),
           (0.550, 0.400, 0.667), (0.660, -0.400, 0.400), (0.687, 0.066, -0.533), (0.440, 0.133, 0.533)]
    out = []
    for y, i, q in yiq:
        rgb = (y + 0.956 * i + 0.621 * q, y - 0.272 * i - 0.647 * q, y - 1.106 * i + 1.703 * q)
        out.append("%02x%02x%02x" % tuple(max(0, min(255, round(c * 255))) for c in rgb))
    return out


def _odyssey2():
    """The Odyssey 2 / Videopac's 4-bit RGBI: eight colours, dim and bright."""
    out = []
    for bright in (0, 1):
        for c in range(8):
            r, g, b = (c >> 2) & 1, (c >> 1) & 1, c & 1
            v = 0xff if bright else 0xaa
            lo = 0x55 if bright else 0x00
            out.append("%02x%02x%02x" % tuple(v if x else lo for x in (r, g, b)))
    return out


GENERATED = {
    "amiga-ecs": _ecs_gamut,        # 4,096 colours: exact with --levels 16
    "swos-aga": _swos_aga,          # SWOS's own AGA palette (256, a few repeated)
    "vga-mode13h": _vga_mode13h,    # the VGA BIOS default
    "master-system": lambda: _levels_gamut((0, 85, 170, 255)),                     # 6-bit RGB, 64
    "mega-drive": lambda: _levels_gamut(tuple(round(v * 255 / 7) for v in range(8))),  # 9-bit RGB, 512
    "atari-secam": lambda: _levels_gamut((0, 255)),                                 # 3-bit RGB, 8
    "amstrad-cpc": lambda: _levels_gamut((0, 128, 255)),                            # 27 colours
    "intellivision": _intellivision,
    "odyssey2": _odyssey2,
}


# --- reading palettes ---------------------------------------------------------

def parse_hex(value):
    value = value.strip().lstrip("#")

    if not re.fullmatch(r"[0-9a-fA-F]{6}", value):
        raise ValueError(f"not an RRGGBB colour: {value!r}")

    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def read_act(data):
    # 768 bytes of RGB, optionally followed by a big-endian count and a
    # transparent index (Photoshop CS2 and later).
    if len(data) < 768:
        raise ValueError(".act: expected at least 768 bytes")

    count = 256

    if len(data) >= 772:
        count = struct.unpack(">H", data[768:770])[0] or 256

    return [tuple(data[i * 3:i * 3 + 3]) for i in range(count)]


def read_pal(data):
    if data[:4] == b"RIFF":
        # RIFF PAL: a "data" chunk of version, count, then RGBX entries.
        at = data.find(b"data")

        if at < 0:
            raise ValueError("RIFF .pal: no data chunk")

        count = struct.unpack("<H", data[at + 10:at + 12])[0]
        entries = data[at + 12:at + 12 + count * 4]

        return [tuple(entries[i * 4:i * 4 + 3]) for i in range(count)]

    lines = data.decode("latin-1").split()

    if lines[:1] != ["JASC-PAL"]:
        raise ValueError(".pal: neither JASC-PAL nor RIFF")

    count = int(lines[2])
    values = [int(v) for v in lines[3:3 + count * 3]]

    return [tuple(values[i * 3:i * 3 + 3]) for i in range(count)]


def read_gpl(text):
    colours = []

    for line in text.splitlines():
        parts = line.split()

        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            colours.append(tuple(int(p) for p in parts[:3]))

    return colours


def read_hex_list(text):
    return [parse_hex(line) for line in text.splitlines() if line.strip() and not line.lstrip().startswith(";")]


def read_image(path):
    # Every distinct colour, in reading order, as CreatePaletteLUT does.
    image = Image.open(path).convert("RGB")
    seen = {}

    pixels = image.load()

    for y in range(image.height):
        for x in range(image.width):
            seen.setdefault(pixels[x, y], None)

    if len(seen) > 4096:
        raise ValueError(f"{path}: {len(seen)} colours -- that is a picture, not a palette")

    return list(seen)


def load_palette(path):
    suffix = path.suffix.lower()
    data = path.read_bytes()

    if suffix == ".act":
        return read_act(data)
    if suffix == ".pal":
        return read_pal(data)
    if suffix == ".gpl":
        return read_gpl(data.decode("utf-8"))
    if suffix in (".hex", ".txt"):
        return read_hex_list(data.decode("utf-8"))

    return read_image(path)


# --- distance -----------------------------------------------------------------

def srgb_to_lab(colour):
    def linear(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = (linear(c) for c in colour)

    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 1.0
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883

    def f(t):
        return t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (24389.0 / 27.0 * t + 16.0) / 116.0

    fx, fy, fz = f(x), f(y), f(z)

    return (116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz))


def ciede2000(lab1, lab2):
    L1, a1, b1 = lab1
    L2, a2, b2 = lab2

    C1 = math.hypot(a1, b1)
    C2 = math.hypot(a2, b2)
    Cbar7 = ((C1 + C2) / 2.0) ** 7
    G = 0.5 * (1.0 - math.sqrt(Cbar7 / (Cbar7 + 25.0 ** 7)))

    a1p, a2p = a1 * (1.0 + G), a2 * (1.0 + G)
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360.0

    dLp = L2 - L1
    dCp = C2p - C1p

    if C1p * C2p == 0:
        dhp = 0.0
    elif abs(h2p - h1p) <= 180.0:
        dhp = h2p - h1p
    elif h2p - h1p > 180.0:
        dhp = h2p - h1p - 360.0
    else:
        dhp = h2p - h1p + 360.0

    dHp = 2.0 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp / 2.0))

    Lbarp = (L1 + L2) / 2.0
    Cbarp = (C1p + C2p) / 2.0

    if C1p * C2p == 0:
        hbarp = h1p + h2p
    elif abs(h1p - h2p) <= 180.0:
        hbarp = (h1p + h2p) / 2.0
    elif h1p + h2p < 360.0:
        hbarp = (h1p + h2p + 360.0) / 2.0
    else:
        hbarp = (h1p + h2p - 360.0) / 2.0

    T = (1.0 - 0.17 * math.cos(math.radians(hbarp - 30.0))
         + 0.24 * math.cos(math.radians(2.0 * hbarp))
         + 0.32 * math.cos(math.radians(3.0 * hbarp + 6.0))
         - 0.20 * math.cos(math.radians(4.0 * hbarp - 63.0)))

    dTheta = 30.0 * math.exp(-(((hbarp - 275.0) / 25.0) ** 2))
    Cbarp7 = Cbarp ** 7
    RC = 2.0 * math.sqrt(Cbarp7 / (Cbarp7 + 25.0 ** 7))
    SL = 1.0 + (0.015 * (Lbarp - 50.0) ** 2) / math.sqrt(20.0 + (Lbarp - 50.0) ** 2)
    SC = 1.0 + 0.045 * Cbarp
    SH = 1.0 + 0.015 * Cbarp * T
    RT = -math.sin(math.radians(2.0 * dTheta)) * RC

    return math.sqrt((dLp / SL) ** 2 + (dCp / SC) ** 2 + (dHp / SH) ** 2 + RT * (dCp / SC) * (dHp / SH))


def make_nearest(palette, metric):
    if metric == "lab":
        labs = [srgb_to_lab(p) for p in palette]

        def nearest(colour):
            lab = srgb_to_lab(colour)
            return palette[min(range(len(palette)), key=lambda i: ciede2000(lab, labs[i]))]

        return nearest

    weights = (1.0, 1.0, 1.0) if metric == "rgb" else (0.299, 0.587, 0.114)

    def nearest(colour):
        # min() keeps the first of equals, as CreatePaletteLUT's strict
        # less-than does, so a palette with duplicates maps the same way.
        return min(palette, key=lambda p: sum(w * (c - q) ** 2 for w, c, q in zip(weights, colour, p)))

    return nearest


# --- the strip ------------------------------------------------------------------

def identity(levels, x, y):
    step = 255.0 / (levels - 1)
    return (round((x % levels) * step), round(y * step), round((x // levels) * step))


def build_lut(levels, nearest):
    image = Image.new("RGB", (levels * levels, levels))
    pixels = image.load()

    for y in range(levels):
        for x in range(levels * levels):
            colour = identity(levels, x, y)
            pixels[x, y] = nearest(colour) if nearest else colour

    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("palette", nargs="?", type=Path, help="palette file or image")
    parser.add_argument("out", nargs="?", type=Path, help="the LUT to write (.png)")
    parser.add_argument("--builtin", metavar="NAME", help="use a built-in palette (see --list)")
    parser.add_argument("--metric", choices=("weighted", "rgb", "lab"), default="weighted")
    parser.add_argument("--levels", type=int, default=16, help="steps per channel: 16 makes 256x16, 32 makes 1024x32")
    parser.add_argument("--identity", action="store_true", help="write the unmapped strip")
    parser.add_argument("--list", action="store_true", help="list the built-in palettes")
    args = parser.parse_args()

    if args.list:
        for name, colours in BUILTIN.items():
            print(f"{name:14s} {len(colours):3d} colours")
        for name, make in GENERATED.items():
            print(f"{name:14s} {len(set(make())):4d} colours")
        return

    # With --builtin or --identity the one positional is the output.
    if (args.builtin or args.identity) and args.out is None:
        args.out, args.palette = args.palette, None

    if args.out is None:
        parser.error("give the LUT to write")

    if not 2 <= args.levels <= 64:
        parser.error("--levels must be 2..64")

    if args.identity:
        nearest = None
        what = "identity"
    else:
        if args.builtin:
            if args.builtin in GENERATED:
                colours = GENERATED[args.builtin]()
            elif args.builtin in BUILTIN:
                colours = BUILTIN[args.builtin]
            else:
                parser.error(f"no built-in palette {args.builtin!r} (see --list)")
            palette = [parse_hex(c) for c in dict.fromkeys(colours)]     # (each colour once)
            what = args.builtin
        elif args.palette:
            palette = load_palette(args.palette)
            what = args.palette.name
        else:
            parser.error("give a palette file or --builtin NAME")

        if not palette:
            sys.exit(f"{what}: no colours")

        nearest = make_nearest(palette, args.metric)
        what = f"{what}, {len(palette)} colours, {args.metric} distance"

    image = build_lut(args.levels, nearest)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)

    print(f"wrote {args.out}: {image.width}x{image.height} ({what})")


if __name__ == "__main__":
    main()
