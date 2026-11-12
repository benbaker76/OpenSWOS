#!/usr/bin/env python3
"""flic_check.py -- our FLIC decoder (src/flic.c) against Pillow's, every frame.

The game decodes the PC cutscenes (ARC/*.FLC) with a decoder of its own (docs/DOS_VIDEO.md).
This decodes each file both ways and compares every frame's pixels and palette:

  ours    the game's --flic-selftest=<dir>, with SWOS_FLIC_DUMP=<out> writing each file's
          frames as <out>/<NAME>.frames: per frame its palette (768 bytes, R G B) then its
          pixels (width x height palette indices)
  Pillow  PIL.FliImagePlugin, an independent implementation

  scripts/flic_check.py <dir of .FLC files> [--game bin/SWOS.x86_64] [--files A.FLC,B.FLC]

Exit status 0 when every frame of every file is the same, 1 otherwise.
"""
import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dir", help="a folder of .FLC files (the PC game's ARC, or the setup's video_dos)")
    ap.add_argument("--game", default=os.path.join(os.path.dirname(__file__), "..", "bin", "SWOS.x86_64"))
    ap.add_argument("--files", help="only these files, comma separated")
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.dir) if n.lower().endswith(".flc"))
    if args.files:
        want = {f.lower() for f in args.files.split(",")}
        names = [n for n in names if n.lower() in want]
    if not names:
        sys.exit("flic_check: no .FLC files in " + args.dir)

    bad = 0
    with tempfile.TemporaryDirectory(prefix="flic-") as out, tempfile.TemporaryDirectory(prefix="flic-in-") as src:
        for n in names:     # (the files asked for, alone, so the self-test decodes those only)
            os.symlink(os.path.abspath(os.path.join(args.dir, n)), os.path.join(src, n))
        env = dict(os.environ, SWOS_FLIC_DUMP=out, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        run = subprocess.run([os.path.abspath(args.game), "--flic-selftest=" + src], env=env,
                             capture_output=True, text=True, timeout=600)
        sys.stdout.write("".join(l + "\n" for l in run.stderr.splitlines() if l.startswith("[flic]")))
        if run.returncode != 0:
            print("flic_check: the game's self-test failed (exit %d)" % run.returncode)
            bad += 1
        for n in names:
            im = Image.open(os.path.join(args.dir, n))
            w, h = im.size
            frames = im.n_frames
            path = os.path.join(out, os.path.splitext(n)[0].upper() + ".frames")
            if not os.path.exists(path):
                print(f"{n}: the game wrote no frames")
                bad += 1
                continue
            ours = open(path, "rb").read()
            step = 768 + w * h
            if len(ours) != frames * step:
                print(f"{n}: the game decoded {len(ours) / step:g} frames, Pillow {frames}")
                bad += 1
                continue
            diff = 0
            for i in range(frames):
                im.seek(i)
                pal = bytes(im.getpalette()[:768]).ljust(768, b"\0")
                px = im.tobytes()
                o = ours[i * step:(i + 1) * step]
                if o[:768] != pal or o[768:] != px:
                    if not diff:
                        npx = sum(1 for a, b in zip(o[768:], px) if a != b)
                        print(f"{n}: frame {i} differs ({npx} pixels, palette "
                              f"{'same' if o[:768] == pal else 'different'})")
                    diff += 1
            if diff:
                bad += 1
                print(f"{n}: {diff} of {frames} frames differ")
            else:
                print(f"{n}: {frames} frames, {w}x{h}: every pixel and palette the same as Pillow's")
    print("flic_check: %s (%d files)" % ("PASS" if not bad else "FAIL", len(names)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
