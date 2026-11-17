# The PC game's cutscenes: FLIC clips in a career

The PC (DOS) release of SWOS 96/97 plays short rendered clips in a career that the Amiga never
had: the dressing room after a big win or loss, a cup final, promotion and relegation, and the
chairman (or, for a national team, the president) at his desk before each of his memos. OpenSWOS
plays them through the **PC cutscenes** mod (`dosvideos`, docs/MODS.md §37) with a FLIC decoder
of its own -- no video codec, no library: the clips are the player's own files, which OpenSWOS
Setup copies from the PC game.

SDL builds only (every desktop, the phones). The Amiga build has none of it; the web build does
not preload the files (86 MB more to download), so it plays none.

## The files

From the PC game's folder (GOG), a copy of its CD or a CD image, into the assets folder -- all
**optional** for the setup (`SdManifest_DosOptional`, tools/swosdata/lib/sd_manifest.c): a copy
without them installs, says how many it lacked, and the game plays without the clips.

| where | from | files | what |
|---|---|---|---|
| `video_dos/` | `ARC/*.FLC` | 98 (74 MB) | `win lose wcup lcup prom rele` (the dressing room), `ch1_01`..`ch3_26` (the three chairmen, 26 scenes each), `chp_01`..`chp_14` (the president) |
| `sound_dos/flc/` | `SFX/FLC/*.RAW` | 6 | `winmtch losemat wincup losecp promote relegte`: the dressing room's sounds |
| `sound_dos/chairman/english/` | `SFX/ENGLISH/EC*.RAW` | 92 | `eca01`..`ecc26` (the chairmen's voices), `ecp01`..`ecp14` (the president's) |

Lower-case names, as `sound_dos/` has them. `ARC/SOCCER.BIN` (the CD's intro, 23 MB) is not
taken. The sounds are raw unsigned 8-bit mono at 22,050 Hz, like the rest of `sound_dos/`
(docs/DOS_SOUND.md). Only the English release has the chairmen's voices: `SFX/ENGLISH` holds them
and only `ENGLISH.EXE` names them (`sfx\english\EC`); FRENCH, GERMAN and ITALIAN.EXE name none, so
their scenes were silent. The game does the same: the voice is played from
`sound_dos/chairman/<english|french|german|italian>/` by `[general] language` (a language the PC
game had no version of uses English), and only `english/` exists.

These made the setup's PC part new: `dos_files` is at DATA VERSION 2 (include/assets_ini.h), so an
assets folder made by an older setup is told to update once.

## The decoder: src/flic.c

Autodesk's FLC (Animator Pro), magic `0xAF12`, 8 bits a pixel. Every one of the 98 files is
320 x 200 (the president's, and the chairmen's scenes 24-26, 320 x 240), its first frame a `BYTE_RUN` and every other one a
`DELTA_FLC` of the frame before, a `COLOR_256` chunk in the first. The decoder draws:

| chunk | type | |
|---|---|---|
| `COLOR_256` | 4 | packets of (skip, count) and count R G B triples, 0-255; a count of 0 is 256 |
| `DELTA_FLC` | 7 | the lines that changed: opcode words (skip lines / the last pixel of an odd width / the packet count), packets of a column skip and a signed count of WORDS, copied or repeated |
| `BLACK` | 13 | every pixel 0 |
| `BYTE_RUN` | 15 | the whole picture, run-length coded a line at a time |
| `FLI_COPY` | 16 | the whole picture, raw |

The PC game's files use 4, 7 and 15 only. Any other chunk -- a prefix chunk before a frame, a
`COLOR_64` or `DELTA_FLI` of the older FLI -- is skipped and counted. The header's frame count is
what is played: the ring frame after the last (the first again, for looping) never is.
Everything is bounds-checked: a frame or chunk past its end, a packet drawing outside the
picture or running out of data stops with an error (`FLIC_ERR_*`), never a read or write past
the buffers. Freestanding C99, fixed-width types, no libc, the caller's buffers (include/flic.h).

**Checked:**
- `bin/SWOS.x86_64 --flic-selftest=<dir>` decodes every `.FLC` in a folder to its last frame
  (`[flic] 98 files, 8614 frames: PASS`); with `SWOS_FLIC_DUMP=<out>` it writes each file's
  frames for
- `scripts/flic_check.py <dir>`, which decodes them again with Pillow (`PIL.FliImagePlugin`, an
  independent implementation) and compares every frame: `flic_check: PASS (98 files)` -- every
  pixel and every palette entry of all 8,614 frames the same.
- 1,500 damaged copies (truncated, bits flipped) under ASan and UBSan: each stopped with an
  error or decoded, nothing out of bounds.

**The frames as pictures** (2026-11-12, for editing or an upscaled set one day):
`scripts/flic_extract.py <file.FLC | dir> <out> [--rgb] [--scale N]` writes each clip's frames as
`<out>/<NAME>/0000.png ...` -- INDEXED PNGs: the clip's palette indices, with the palette in force
at that frame (the owner, 2026-11-12: the palette kept); `--rgb` writes RGB instead -- and a
`clip.json` with the frame count, size and the game's frame time (4/70 s; 6/70 s for CHP_nn).
Pillow's reader, proved identical to ours above. Run it with the python3 that has Pillow (not `-I`
when Pillow is a user install). The game plays an upscaled set through MPEG-1 files of its own
(below, "The upscaled clips").

**As video files** (2026-11-14): `scripts/flic_encode.py <frames> <PC game | sound_dos> <out>
[--info <extracted frames>] [--clip ...] [--crf N] [--codec h264|h265|av1] [--fps N]` makes one MP4
a clip from those frames -- or an upscaled copy of them, any size -- with its sound (AAC), timed as
the game shows it: the first frame faded in as the PC game fades (~0.9 s), then the sound and a frame every 4/70 s
(6/70 s), the last frame held until the sound ends, a 225 ms fade out. A 320x200 clip keeps its
pixels and carries a 5:6 sample aspect (shown 4:3). Needs ffmpeg.

**The upscaled set** (2026-11-14): all 8,614 frames enlarged 4x (1280x800 / 1280x960) by the
owner's `upscale` (github.com/benbaker76/upscale) -- model `RealESRGAN_anime_x4`, `--fp16
--no-tiles --alpha opaque`, its folder mode, about 0.65 s a frame on the AMD GPU -- chosen by eye
over APISR_GRL_x4 (adds dark outlines), UltraSharp_x4 (keeps the dither noise), RealESRGAN_x4plus
(soft faces) and UltraMix_Smooth_x4. fp16 in one pass is within 0.5/255 on average of fp32 tiled,
and has no tile seams. A second set from `RealESRGAN_x8` (2560x1600, tiled, about 10 s a frame) is
softer and more like the original render, with invented skin texture.

## The upscaled clips (2026-11-14)

The game plays a clip's **upscaled picture** when the assets folder has one:
`video_dos/hd/<name>.mpg` (lower case, as `video_dos/`), with the mod's **Upscaled clips** setting
on (`[mod.dosvideos] hd`, default on). Without one, or with the setting off, it plays the `.FLC`.

- **Only the picture comes from the `.mpg`.** The `.FLC` is still opened, and it decides how many
  frames there are, their size, when each is due, the sound, the fades and the skip. The `.mpg`
  gives one picture per `.FLC` frame, in order. When it runs out the last picture stays, and a
  file the game cannot read plays the original (said in the log).
- **MPEG-1**, decoded by pl_mpeg (`third_party/pl_mpeg`, MIT, one header; `src/pl_mpeg_impl.c`).
  It is a simple codec with no patents left and a small, permissive decoder, so it suits the game
  where MP4/H.264 would not. The rate in the file (25) is never read.
- **Shown at the window's resolution:** `ModApi_CutsceneFrameHD` → `Platform_CutsceneShowHD`.
  The frame is an RGBA texture drawn through gfx.h on either presenter. It covers exactly the
  rectangle the original would (its 320x200 or 320x240, centred in the game's picture, scaled
  as the picture is), over black, and the fades go through the vertices' colour. A 4x set is
  1280x800 in that place. The game's frame buffers and palette are untouched, as for the
  original.
- **Made by** `scripts/flic_encode.py <upscaled frames> - <out> --info <extracted frames>
  --mpeg1 [--q N]`. Quantiser 2 (near the best) gives about 4 MB a clip and 400 MB for the 98 at
  4x. It checks that each file has exactly as many pictures as its clip.
- **Not shipped:** the set is made from the PC game's own pictures, so like `video_dos/` it is the
  player's to make, never part of OpenSWOS. Making it needs the upscaler and a GPU for an hour
  or more (above). Whether OpenSWOS Setup should offer it is an open question (2026-11-14).

**Checked:** `SWOS_DOSVIDEO_TEST=win` with `video_dos/hd/win.mpg` logs `win: upscaled, 1280x800`
and plays 73 of 73. Its presented frames, captured with `SWOS_PRESENT_CAPTURE` on SDL's software
renderer, show the upscaled picture where the original goes, changing frame to frame, then holding
the last frame until the sound ends. With `hd = off` the original plays in the same place.

## How a clip is shown (src/mod_dosvideos.c, the PC game's PlayAnimation)

swos.asm `PlayAnimationCommon` / `PlayAnimation`, the `FadeAndPlay*Animation` around them:

1. Not with the sound off (`g_soundOff`) nor the intro skipped (`skipIntro`). Ours adds the
   career autopilot, a rig, online and a run with no window -- one rule,
   `ModApi_CutsceneBlocked` (src/mod_api.c).
2. The menu fades out (`FadeOutToBlack`); the music and sounds are held where they are.
3. The first frame is drawn and faded in (`FlipTitle`, `FadeIn`), then the clip's sound starts
   (`animationSoundFunction` after the first frame's fade). **The fade is the PC game's**
   (`FadeOut`): every colour component one VGA step (of 63) nearer its goal each 70 Hz retrace,
   so as many retraces as the palette's brightest component has steps -- ~0.9 s for a clip with
   white in it, darker colours arriving first. Faded out the same way, on the last frame shown.
   (Until 2026-11-17 a fixed 450 ms in and 225 ms out: the owner found the clips faded too
   quickly to be seen whole.) An upscaled clip is scaled over the same time.
4. A frame every `animationDelay + 1` ticks of the VGA's 70 Hz retrace (`menuCycleTimer >
   animationDelay`): 3 for the dressing room and the chairmen (4/70 s, 17.5 frames a second), 5
   for the president (6/70 s). Ours keeps that clock in real time and shows the frame due at
   each of the game's presents.
5. Any key, mouse or pad button, or a touch, skips (`JoyKeyOrCtrlPressed`); the press is then
   dropped, so it does not also choose the menu's entry.
6. After the last frame (not skipped) it waits for the sound to end (`cseg_19036`'s loop).
7. Fade out, the sound stops (`StopAudio`), and the menu fades in again (`LoadFillAndSwtitle`,
   `menuFade`) -- ours never drew over the menu: the picture is shown in place of the game's
   frame at the present (`Platform_CutsceneShow`), so the menu is there as it was.

The picture is shown 1:1 in the middle of the game's screen (336 x 272, or wider with
widescreen), the rest in the clip's darkest colour, in the clip's own palette; the presenter
scales the whole as it scales the menus. Nothing the mods draw over the picture is drawn over a
clip, nor the menus' backdrop and opacity.

Some chairmen's scenes are 320 x 240 too (`ch*_24`..`_26`). `SWOS_DOSVIDEO_TEST=win,ch2_05,chp_03` plays clips at the main menu (even headless, with
`SWOS_SKIP_INTRO=1`), then quits with `DOSVIDEOS TEST PASS`; with `SWOS_PRESENT_CAPTURE=N
SWOS_PRESENT_CAPTURE_FROM=F` the presented frames are written as PNGs.
