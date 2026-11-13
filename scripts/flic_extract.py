#!/usr/bin/env python3
"""flic_extract.py -- the PC cutscenes' frames as PNG files, for editing or upscaling.

Each .FLC (the PC game's ARC/, or the setup's video_dos/) becomes a folder of its frames:

  <out>/<NAME>/0000.png, 0001.png, ...   every frame as the game shows it: an INDEXED PNG, its
                                          pixels the clip's palette indices and the palette in
                                          force at that frame with it (320x200, or 320x240);
                                          --rgb: RGB instead
  <out>/<NAME>/clip.json                  what it takes to put them back: the frame count, size,
                                          and the frame time the game plays them at

The game plays a clip at its own pace, not the file's (docs/DOS_VIDEO.md): a frame every 4/70 s,
6/70 s for the president's (CHP_nn). clip.json says which. Decoding is Pillow's FLIC reader, which
scripts/flic_check.py proved identical to the game's own (src/flic.c), every frame of all 98.

  scripts/flic_extract.py <file.FLC | dir> <out dir> [--rgb] [--scale N]

--scale N also writes each frame N times larger (nearest neighbour) beside it, as <frame>@Nx.png:
a starting point for hand work, not an upscaler.
"""
import argparse
import json
import os
import sys

from PIL import Image


def frame_ticks(name):
    """The game's frame time in 70 Hz ticks (swos.asm PlayChairmanAnimation: animationDelay 5 for
    the president, 3 otherwise; a frame waits animationDelay + 1 ticks)."""
    return 6 if name.upper().startswith("CHP_") else 4


def extract(path, out, scale, rgb):
    name = os.path.splitext(os.path.basename(path))[0].upper()
    dest = os.path.join(out, name)
    os.makedirs(dest, exist_ok=True)
    im = Image.open(path)
    n = 0
    while True:
        # (mode P: the frame's indices with the palette set by this frame or an earlier one)
        frame = im.convert("RGB") if rgb else im.copy()
        frame.save(os.path.join(dest, "%04d.png" % n))
        if scale > 1:
            frame.resize((frame.width * scale, frame.height * scale), Image.NEAREST).save(
                os.path.join(dest, "%04d@%dx.png" % (n, scale)))
        n += 1
        try:
            im.seek(n)
        except EOFError:
            break
    ticks = frame_ticks(name)
    with open(os.path.join(dest, "clip.json"), "w") as f:
        json.dump({"name": name, "frames": n, "width": im.width, "height": im.height,
                   "frame_ticks_70hz": ticks, "frame_seconds": round(ticks / 70.0, 6)}, f, indent=1)
        f.write("\n")
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", help="a .FLC file, or a folder of them")
    ap.add_argument("out", help="where each clip's folder of frames goes")
    ap.add_argument("--rgb", action="store_true", help="RGB frames instead of indexed ones (the palette applied)")
    ap.add_argument("--scale", type=int, default=1, help="also write each frame N times larger")
    args = ap.parse_args()
    if os.path.isdir(args.src):
        files = sorted(os.path.join(args.src, n) for n in os.listdir(args.src) if n.lower().endswith(".flc"))
    else:
        files = [args.src]
    if not files:
        sys.exit("flic_extract: no .FLC files in " + args.src)
    for p in files:
        print("%s: %d frames" % (os.path.basename(p), extract(p, args.out, args.scale, args.rgb)))


if __name__ == "__main__":
    main()
