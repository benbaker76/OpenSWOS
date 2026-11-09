#!/usr/bin/env python3
"""Copy SWOS 2020's training pictures out of its source into the practice mod.

SWOS 2020 draws its small menu pictures from character maps in src/PixelmapAndStrings.h: one
character a pixel, '1'..'9' 'A'..'F' a palette index (1..15), anything else clear (drawMapEx,
game.cpp). The practice mod draws them the same way (src/mod_practice.c), from
bin/mods/practice/pixmaps.txt, written here: for each map a line "name width height", then its rows.

  python3 scripts/extract_swos2020_pixmaps.py [--swos2020 DIR]
"""
import argparse
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "bin", "mods", "practice", "pixmaps.txt")
MAPS = [
    "fields_default_99x101_1x1_Map",            # TEAM TRAINING: the pitch beside its settings
    "arrows_default_scaled_Map",                # ... the goal attacked: down
    "arrows_default_scaled_2_Map",              # ... up
    "starsMap",                                 # ... the CPU's skill, in stars
    "goalkeeper_gloved_scaled_32x44_4x1_Map",   # ... its keeper, four frames
    "team_training_controls_230x68_1x1_map",    # TEAM TRAINING CONTROLS: the keys
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--swos2020", default=os.environ.get("SWOS2020_DIR", "/mnt/c/Projects/GitHub/swos-2020"))
    args = ap.parse_args()
    src = open(os.path.join(args.swos2020, "src", "PixelmapAndStrings.h"), encoding="latin-1").read()
    out = ["# SWOS 2020's pictures (src/PixelmapAndStrings.h): scripts/extract_swos2020_pixmaps.py"]
    for name in MAPS:
        m = re.search(r"const char\*\s*" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
        if not m:
            raise SystemExit("missing map %s" % name)
        rows = re.findall(r'"([^"]*)"', m.group(1))
        width = max(len(r) for r in rows)
        out.append("%s %d %d" % (name, width, len(rows)))
        out.extend(r.ljust(width, ".") for r in rows)
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d maps)" % (os.path.relpath(OUT, REPO), len(MAPS)))


if __name__ == "__main__":
    main()
