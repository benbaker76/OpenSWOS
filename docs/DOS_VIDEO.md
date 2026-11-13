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
when Pillow is a user install). The game reads only `.FLC` today: an upscaled set needs a reader of
its own.

## How a clip is shown (src/mod_dosvideos.c, the PC game's PlayAnimation)

swos.asm `PlayAnimationCommon` / `PlayAnimation`, the `FadeAndPlay*Animation` around them:

1. Not with the sound off (`g_soundOff`) nor the intro skipped (`skipIntro`). Ours adds the
   career autopilot, a rig, online and a run with no window -- one rule,
   `ModApi_CutsceneBlocked` (src/mod_api.c).
2. The menu fades out (`FadeOutToBlack`); the music and sounds are held where they are.
3. The first frame is drawn and faded in (`FlipTitle`, `FadeIn`), then the clip's sound starts
   (`animationSoundFunction` after the first frame's fade).
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
