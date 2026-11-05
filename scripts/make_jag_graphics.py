#!/usr/bin/env python3
"""make_jag_graphics.py -- build bin/grafs_jag from SwosGfx's Jaguar conversion.

The Atari Jaguar release of Sensible Soccer redrew the Amiga gameplay art at 256
colours, and SwosGfx's -jag2amiga mode lays it back into the ORIGINAL AMIGA BANK
LAYOUT: same sheets, same cell coordinates, so the game's own image-command
scripts cut the same sprites out of it. What it writes is 8bpp BMPs; what the
game loads is the Amiga's planar bank format. This does that conversion, and
nothing else:

    scripts/make_jag_graphics.py [SRC] [-o bin/grafs_jag]

SRC defaults to SwosGfx's output directory (Amiga_Images_256_Jag). Each bank
becomes a 320x256 sheet of 8 bitplanes -- 8 slabs of 40 bytes per scanline,
81920 bytes -- which the loader recognises as 8bpp from its size alone
(DetectBankPlanes), exactly as it already does for the AGA soccer_s bank.
The sheets' shared 256-colour palette is written beside them as JAG.PAL, 256
RGB triplets, which the game installs into free palette entries at run time
(src/jag_graphics.c, docs/JAGUAR.md).

Only the in-match banks are converted. Everything else -- menus, charset, the
loader pictures -- keeps falling back to grafs_aga/grafs through resload's
existing tier search, so Jaguar mode changes the players and nothing else.

One more file comes from the Jaguar's own sheet rather than the converted banks:
JAGKITS.BIN, the big shirts, shorts, socks and shirt-type icons the Jaguar game
drew for its kit screen. They are not in the Amiga layout at all (the Amiga keeps
its kit art in the MENUS bank), so they are cut from CJCGRAFS_JAGSHEET.bmp by the
boxes in KIT_BOXES and stored chunky, with the sheet's palette: src/kit_art.c
draws them in the menus when [graphics] kit_art = jaguar.

A SPRITE SET AS A MOD (docs/JAGUAR.md, "Sprite sets as mods"): your own sheets, drawn to the
Jaguar tier's colour roles, made into a mod folder the game draws instead of the Amiga art:

    scripts/make_jag_graphics.py MY_SHEETS --mod mods/mysprites [--name "My sprites"]

writes mods/mysprites/grafs_jag/ (the banks you have; the rest come from the player's own
Jaguar data, or the Amiga's) and, when there is none yet, a mod.json saying "sprites":
"jaguar", with the four shading settings as the mod's own (its page in OPTIONS -> MODS).
"""
import argparse
import os
import sys

BANKS = ["CJCTEAM1", "CJCTEAM2", "CJCTEAM3", "CJCTEAMG", "CJCBITS", "CJCBENCH"]
WIDTH, HEIGHT, PLANES = 320, 256, 8
ROW_BYTES = WIDTH // 8                      # 40, one bitplane row
DEFAULT_SRC = ("/mnt/c/Projects/GitHub/SwosGfx/SwosGfx/bin/Debug/net8.0/"
               "Amiga_Images_256_Jag")


# The kit art in CJCGRAFS_JAGSHEET.bmp: (x, y, w, h), drop shadow included. In the
# order src/kit_art.c reads them: a shirt and an icon per shirt type (0 ordinary,
# 1 coloured sleeves, 2 vertical stripes, 3 hoops), the shorts, the two socks.
KIT_SHEET = "CJCGRAFS_JAGSHEET"
KIT_BOXES = [
    (209, 63, 44, 40), (257, 1, 44, 40), (161, 1, 44, 41), (209, 1, 44, 40),
    (161, 42, 21, 20), (257, 42, 21, 20), (193, 42, 21, 20), (225, 42, 21, 20),
    (289, 42, 28, 22), (161, 63, 14, 25), (183, 63, 15, 25),
]


def write_kits(src, out, Image):
    """JAGKITS.BIN: "JGK1", the sheet's 768 palette bytes, a count, then per
    graphic its width, height and width*height sheet colours (0 = nothing)."""
    path = os.path.join(src, KIT_SHEET + ".bmp")
    if not os.path.exists(path):
        print("  skip kits      (%s.bmp is not in %s)" % (KIT_SHEET, src))
        return
    im = Image.open(path)
    if im.mode != "P" or im.size != (WIDTH, HEIGHT):
        sys.exit("make_jag_graphics: %s is %s %dx%d, expected an 8bpp %dx%d sheet"
                 % (KIT_SHEET, im.mode, im.width, im.height, WIDTH, HEIGHT))
    pixels = bytes(im.tobytes())
    blob = bytearray(b"JGK1")
    blob += bytes(im.getpalette()[:768])
    blob.append(len(KIT_BOXES))
    for x, y, w, h in KIT_BOXES:
        blob += bytes((w, h))
        for row in range(y, y + h):
            blob += pixels[row * WIDTH + x:row * WIDTH + x + w]
    with open(os.path.join(out, "JAGKITS.BIN"), "wb") as f:
        f.write(blob)
    print("  kits      -> JAGKITS.BIN  (%d graphics, %d bytes)" % (len(KIT_BOXES), len(blob)))


def to_planar(pixels):
    """One 320x256 8bpp sheet -> Amiga interleaved planar bytes."""
    out = bytearray(HEIGHT * PLANES * ROW_BYTES)
    for y in range(HEIGHT):
        row = pixels[y * WIDTH:(y + 1) * WIDTH]
        base = y * PLANES * ROW_BYTES
        for x, value in enumerate(row):
            if not value:
                continue
            byte, bit = x >> 3, 7 - (x & 7)
            for p in range(PLANES):
                if value & (1 << p):
                    out[base + p * ROW_BYTES + byte] |= 1 << bit
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?", default=DEFAULT_SRC,
                    help="SwosGfx's Jaguar output directory (default: %(default)s)")
    ap.add_argument("-o", "--out", default="bin/grafs_jag", help="where the banks go")
    ap.add_argument("--mod", metavar="DIR",
                    help="make a sprite-set mod there instead: DIR/grafs_jag/ and DIR/mod.json")
    ap.add_argument("--name", default=None, help="the mod's name (with --mod)")
    args = ap.parse_args()
    if args.mod:
        args.out = os.path.join(args.mod, "grafs_jag")

    try:
        from PIL import Image
    except ImportError:
        sys.exit("make_jag_graphics: needs Pillow (pip install pillow)")

    if not os.path.isdir(args.src):
        sys.exit("make_jag_graphics: no such directory: %s\n"
                 "  Run SwosGfx's !Convert_Jag_to_Amiga.bat first." % args.src)
    os.makedirs(args.out, exist_ok=True)

    palette = None
    used = bytearray(256)       # the colours the art actually draws in
    written = 0
    for bank in BANKS:
        path = os.path.join(args.src, bank + ".bmp")
        if not os.path.exists(path):
            print("  skip %-9s (not in %s)" % (bank, args.src))
            continue
        im = Image.open(path)
        if im.mode != "P" or im.size != (WIDTH, HEIGHT):
            sys.exit("make_jag_graphics: %s is %s %dx%d, expected an 8bpp %dx%d sheet"
                     % (bank, im.mode, im.width, im.height, WIDTH, HEIGHT))
        # All the sheets share one palette -- that is what makes a single remap
        # table at run time possible, so a stray sheet must not pass quietly.
        this = im.getpalette()[:768]
        if palette is None:
            palette = this
        elif this != palette:
            sys.exit("make_jag_graphics: %s has a different palette from the other "
                     "banks; the game installs ONE Jaguar palette" % bank)
        pixels = bytes(im.tobytes())
        for value in set(pixels):
            used[value] = 1
        data = to_planar(pixels)
        with open(os.path.join(args.out, bank + ".RAW"), "wb") as f:
            f.write(data)
        print("  %-9s -> %s.RAW  (%d bytes, %d planes)" % (bank, bank, len(data), PLANES))
        written += 1

    if not written:
        sys.exit("make_jag_graphics: no banks found in %s" % args.src)
    # JAG.PAL: the palette AND which of it the art uses, so the game installs
    # exactly those colours in its free entries instead of all 256.
    with open(os.path.join(args.out, "JAG.PAL"), "wb") as f:
        f.write(b"JGP1")
        f.write(bytes(palette))
        f.write(bytes(used))
    print("  palette   -> JAG.PAL   (256 RGB triplets, %d of them used)" % sum(used))
    write_kits(args.src, args.out, Image)
    print("%d bank(s) written to %s" % (written, args.out))
    if args.mod:
        write_mod_manifest(args.mod, args.name)


def write_mod_manifest(mod_dir, name):
    """A sprite set's mod.json, unless the mod has one: "sprites": "jaguar" makes the game draw
    its grafs_jag/ with the Jaguar tier; the four shading settings are its own."""
    import json, re, uuid
    path = os.path.join(mod_dir, "mod.json")
    if os.path.exists(path):
        print("  mod.json  kept (%s)" % path)
        return
    slug = re.sub(r"[^A-Za-z0-9_-]", "_", os.path.basename(os.path.normpath(mod_dir)))[:31]
    onoff = ["on", "off"]
    manifest = {
        "uuid": str(uuid.uuid4()),          # its own, for ever (docs/MODS.md §14)
        "id": slug,
        "name": name or slug,
        "version": "1.0",
        "author": "",
        "about": "Players drawn with more colours, by the Jaguar sprite tier.",
        "targets": ["sdl"],
        "changes": "cosmetic",
        "default": False,
        "sprites": "jaguar",
        "settings": [
            {"key": "outfit_shading", "label": "Kit shading", "choices": onoff, "default": "on"},
            {"key": "shadow_shading", "label": "Shadow shading", "choices": onoff, "default": "on"},
            {"key": "hair_shading", "label": "Hair shading", "choices": onoff, "default": "on"},
            {"key": "shade_step", "label": "Shade step", "choices": ["1", "2", "3", "4", "5"], "default": "2"},
        ],
    }
    with open(path, "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    print("  mod.json  -> %s (uuid %s)" % (path, manifest["uuid"]))


if __name__ == "__main__":
    main()
