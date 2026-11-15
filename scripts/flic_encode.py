#!/usr/bin/env python3
"""flic_encode.py -- the PC cutscenes as video files, from flic_extract.py's frames (or an
upscaled set of them) and the clips' sounds, timed as the game plays them.

  scripts/flic_encode.py <frames dir> <PC game folder | sound dir> <out dir>
                         [--clip NAME ...] [--crf N] [--codec h264|h265|av1] [--fps N]

<frames dir> holds a folder a clip (WIN/, CH1_05/, ...), each with its frames 0000.png ... and
the clip.json flic_extract.py wrote -- or an upscaled copy of those folders, frames only, in which
case clip.json is read from --info (the extracted set) when it is not beside them. Any size: the
frames are taken as they are.

The sound: the PC game's folder (its SFX/FLC and SFX/ENGLISH), or the setup's sound_dos/ (flc/,
chairman/english/). Raw unsigned 8-bit mono, 22,050 Hz. Which clip has which (docs/DOS_VIDEO.md,
src/mod_dosvideos.c): WIN WINMTCH, LOSE LOSEMAT, WCUP WINCUP, LCUP LOSECP, PROM PROMOTE,
RELE RELEGTE, CHn_xx ECA/ECB/ECC xx (n = 1..3), CHP_xx ECP xx.

The timing is the game's (src/mod_dosvideos.c, swos.asm PlayAnimationCommon): the first frame
faded in over 450 ms, then the sound starts and a frame comes every 4/70 s (6/70 s for the
president's, CHP_nn -- clip.json's frame_ticks_70hz); after the last frame the picture waits for
the sound to end, then fades out over 225 ms.

The picture keeps the frames' pixels and says how to show them: a 320x200 clip's pixels are
taller than wide on a 4:3 screen, so its files carry a sample aspect of 5:6 (a player shows
1280x800 as 1280x960); 320x240 ones are square. --fps N writes N frames a second (each of the
clip's frames repeated or not as its time falls) for a player or site that wants a common rate;
the default is the clip's own (17.5 or 11.67).

Needs ffmpeg on PATH.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

FADE_IN = 0.450
FADE_OUT = 0.225
RATE = 22050

ROOM = {"WIN": "WINMTCH", "LOSE": "LOSEMAT", "WCUP": "WINCUP", "LCUP": "LOSECP",
        "PROM": "PROMOTE", "RELE": "RELEGTE"}


def sound_file(sound_root, clip):
    """The clip's sound, in the PC game's folder or the setup's sound_dos/; None if it has none."""
    if clip in ROOM:
        names = [("SFX/FLC", ROOM[clip] + ".RAW"), ("flc", ROOM[clip].lower() + ".raw")]
    elif clip.startswith("CHP_"):
        n = clip[4:]
        names = [("SFX/ENGLISH", "ECP" + n + ".RAW"), ("chairman/english", "ecp" + n + ".raw")]
    elif clip.startswith("CH") and clip[2:3] in "123" and clip[3:4] == "_":
        v = "ABC"[int(clip[2]) - 1]
        n = clip[4:]
        names = [("SFX/ENGLISH", "EC" + v + n + ".RAW"), ("chairman/english", ("ec" + v + n).lower() + ".raw")]
    else:
        return None
    for sub, name in names:
        for base in (sound_root, os.path.join(sound_root, "sound_dos")):
            p = os.path.join(base, sub, name)
            if os.path.isfile(p):
                return p
    return None


def clip_info(frames_dir, info_dir, clip):
    for d in (os.path.join(frames_dir, clip), os.path.join(info_dir, clip) if info_dir else None):
        if d and os.path.isfile(os.path.join(d, "clip.json")):
            with open(os.path.join(d, "clip.json")) as f:
                return json.load(f)
    sys.exit(f"{clip}: no clip.json beside its frames; give --info <the extracted frames>")


def encode(frames_dir, info_dir, sound_root, out_dir, clip, args):
    info = clip_info(frames_dir, info_dir, clip)
    src = os.path.join(frames_dir, clip)
    frames = sorted(f for f in os.listdir(src) if f.endswith(".png") and f[:4].isdigit())
    if len(frames) != info["frames"]:
        print(f"{clip}: {len(frames)} frames here, {info['frames']} in the clip -- skipped", file=sys.stderr)
        return False
    ticks = info["frame_ticks_70hz"]
    fps_num, fps_den = 70, ticks                     # 17.5 (70/4) or 11.67 (70/6)
    frames_s = len(frames) * ticks / 70.0
    snd = sound_file(sound_root, clip)
    sound_s = os.path.getsize(snd) / RATE if snd else 0.0
    hold = max(0.0, sound_s - frames_s)              # the last frame waits for the sound
    total = FADE_IN + frames_s + hold + FADE_OUT
    sar = "5/6" if (info["width"], info["height"]) == (320, 200) else "1/1"

    vf = [f"tpad=start_duration={FADE_IN}:start_mode=clone:stop_duration={hold + FADE_OUT:.4f}:stop_mode=clone",
          f"fade=t=in:st=0:d={FADE_IN}", f"fade=t=out:st={total - FADE_OUT:.4f}:d={FADE_OUT}",
          f"setsar={sar}"]
    if args.fps:
        vf.append(f"fps={args.fps}")
    vf.append("format=yuv420p")

    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
           "-framerate", f"{fps_num}/{fps_den}", "-start_number", "0", "-i", os.path.join(src, "%04d.png")]
    if snd:
        cmd += ["-f", "u8", "-ar", str(RATE), "-ac", "1", "-i", snd]
    cmd += ["-vf", ",".join(vf)]
    if args.codec == "h264":
        cmd += ["-c:v", "libx264", "-preset", "slow", "-tune", "animation", "-crf", str(args.crf),
                "-profile:v", "high"]
    elif args.codec == "h265":
        cmd += ["-c:v", "libx265", "-preset", "slow", "-crf", str(args.crf), "-tag:v", "hvc1"]
    else:
        cmd += ["-c:v", "libaom-av1", "-crf", str(args.crf), "-b:v", "0", "-cpu-used", "4", "-row-mt", "1"]
    if snd:
        cmd += ["-af", f"adelay={int(FADE_IN * 1000)},apad", "-ar", "48000", "-c:a", "aac", "-b:a", "192k"]
    cmd += ["-t", f"{total:.4f}", "-movflags", "+faststart",
            "-metadata", f"title=Sensible World of Soccer 96/97 -- {clip}",
            os.path.join(out_dir, clip + ".mp4")]
    subprocess.run(cmd, check=True)
    print(f"{clip}: {len(frames)} frames at {70 / ticks:.2f}/s, sound {sound_s:.2f}s{'' if snd else ' (none)'}, "
          f"{total:.2f}s -> {clip}.mp4")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("frames")
    ap.add_argument("sound", help="the PC game's folder, or the setup's assets folder / sound_dos")
    ap.add_argument("out")
    ap.add_argument("--info", help="the extracted frames (flic_extract.py's), for clip.json")
    ap.add_argument("--clip", nargs="*", help="only these clips (WIN CH1_05 ...)")
    ap.add_argument("--crf", type=int, default=14, help="quality: lower is better (default 14)")
    ap.add_argument("--codec", choices=["h264", "h265", "av1"], default="h264")
    ap.add_argument("--fps", help="a fixed output rate, e.g. 30 (default: the clip's own)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    clips = args.clip or sorted(d for d in os.listdir(args.frames) if os.path.isdir(os.path.join(args.frames, d)))
    ok = sum(encode(args.frames, args.info, args.sound, args.out, c.upper(), args) for c in clips)
    print(f"{ok} of {len(clips)} clips written to {args.out}")
    return 0 if ok == len(clips) else 1


if __name__ == "__main__":
    sys.exit(main())
