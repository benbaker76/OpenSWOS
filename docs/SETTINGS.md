# Settings, command line and display

Everything you can change: the settings file `SWOS.ini`, the command-line options, and the
display options (render modes, scaling, CRT effects). The keys and buttons are in
[PLAYING.md](PLAYING.md).

## Running

Launch the binary from anywhere — the program automatically `chdir()`s to
its own directory so `data/`, `grafs/`, `sound/`, and `save/` resolve
correctly. `stderr` is auto-redirected to `SWOS.log` next to the binary
(set `SWOS_NO_LOG=1` in the environment to disable).

### Settings file (`SWOS.ini`)

#### Where it lives

`SWOS.ini` is **not** kept next to the binary — it goes in a per-user
game folder, alongside your `save/` directory and `SWOS.log`. Install
directories are frequently read-only (Program Files, `/Applications`,
a Gatekeeper-quarantined bundle), so config and saves live where the
user can always write:

| Platform | Folder |
|----------|--------|
| Windows (incl. Cygwin) | `%USERPROFILE%\Documents\SWOS\` |
| macOS                  | `~/Library/Application Support/SWOS/` |
| Linux / other Unix     | `~/.swos/` |
| WebAssembly            | `/SWOS.ini` in the IndexedDB-backed mount (mirrored to `localStorage`) |

So on Windows the full path is
`C:\Users\<you>\Documents\SWOS\SWOS.ini`, with saves in
`…\Documents\SWOS\save\`. The folder is created at startup. If it
can't be determined or created (no `%USERPROFILE%` / `$HOME`), the game
falls back to the current directory, which is how a dev run from `bin/`
ends up with a local `./SWOS.ini`.

#### How it's applied

Resolution order at launch is: **defaults → SWOS.ini → command-line
flags** — a flag always wins over the file, but a value you only set in
the file (or via runtime keys like `+`/`-`, Alt+Enter, or the arcade F2
service menu) is remembered.

The file is created complete, with comments, if it doesn't exist. On
exit, any keys a newer build added are **appended** — your hand-edited
values and comments are never rewritten or reordered.

#### Full key reference

```ini
[video]
renderer=accel        ; software | accel (hardware-accelerated)
vsync=off             ; on | off — accel renderer only
subpixel_scroll=on    ; on | off — smooth sub-pixel scrolling (see below)
scaling=smooth        ; sharp | smooth — how the frame is scaled (see below)
crt_mask=off          ; off | aperture | slot | scanlines — synthetic CRT
                      ; shadow mask over the finished frame (see below)
crt_mask_strength=45  ; 0-100 — how dark the grille's off-phase gets
crt_vignette=0        ; 0-100 — darkens towards the corners, 0 = off
crt_corners=0         ; 0-100 — rounded-glass corner size, 0 = square
render_driver=auto    ; auto | opengl | direct3d11 | direct3d | software-backend
                      ; auto = SDL default (D3D11 on Windows). Use opengl if
                      ; replays/attract run in slow motion on Windows.
fullscreen=off        ; off | desktop | exclusive
scale=3               ; 1..4 window scale (windowed only)
resolution=auto       ; WxH (e.g. 2560x1080) drives the render aspect, or
                      ; auto for the desktop. Need not match the monitor.
screen=0              ; monitor index to open on, 0 = primary. Auto-clamped.

[audio]
mode=mono             ; mono | stereo

[general]
language=en           ; en | de | fr | es | it | nl
currency=pound        ; pound | dollar | euro
world=9697            ; the football world: 9697 (Sensible World of Soccer 96/97) | 2627
                      ; (World 2026-27, if installed); OPTIONS -> WORLD sets it, from the
                      ; next start; a save and an online match are played in the world
                      ; they were made in

[gameplay]
style=amiga           ; amiga | dos | xbox — the Amiga release (50 FPS); the DOS release
                      ; (70 FPS physics, timers, formations and its 320x200 view); or
                      ; SWOS 2020's XBOX (60 FPS, the Amiga's ball physics and view, the
                      ; DOS goalkeepers; docs/DOS_GAMEPLAY.md section 9). OPTIONS ->
                      ; GAMEPLAY sets it; online, a match plays the host's

[dos]                 ; PC-only extras (Amiga is the definitive version)
directory=grafs_dos   ; folder under bin/ to load the DOS assets from
graphics=false        ; true | false — render the DOS-version graphics

[arcade]              ; coin-op mode (PC-only). See docs/ARCADE.md
enabled=false         ; true | false — boot straight to attract mode, no menu
mode=GOLDEN GOAL      ; arcade game mode (F2 service menu)
game_seconds=45       ; 10..600
golden_goal=true      ; true | false
penalties_on_draw=true
goal_replay=true      ; replay each goal (the engine's AUTO REPLAYS)
attract_audio=music+sfx  ; music (HERO only) | sfx (reel crowd only) | music+sfx
gameplay=amiga        ; amiga (50fps feel) | dos (70fps); also picks the
                      ; attract_mode/<mode> reels
key_p1_credit=5       ; cabinet key bindings, SDL key names. MAME layout:
key_p1_start=1        ; 5/6 = coin P1/P2, 1/2 = start, F2 = service
key_p2_credit=6
key_p2_start=2
key_service=F2
```

Everything in `[arcade]` except `enabled` is also editable in-game from
the F2 service menu, which writes changes straight back to `SWOS.ini`.

#### `[mods]` and `[tweaks]` (docs/MODS.md)

```ini
[mods]
enabled=classics, hall   ; mods to play with, by folder name, first first (a file two have
                         ; comes from the first). DLC: folders in mods/ beside SWOS.ini
disabled=                ; the game's own (canonical) mods to turn off; they are on by default

[tweaks]                 ; changes to the original's behaviour, off by default, off line only
cpu_cards=false          ; the computer's players are booked and sent off too
no_teammate_tackles=false       ; a team-mate leaves your player on the ball alone
tackle_controlled_player=false  ; fire slides the player you control, not a nearer team-mate
```

OPTIONS -> MODS turns each mod on or off (from the next start) and writes `[mods]`.
`--mods=a,b` sets the mods for one run without saving them. A DLC mod that is not cosmetic
keeps MULTIPLAYER off. Every tweak is off online and in the test rigs.

### Command-line options

```
-f, --fullscreen          Shorthand for --fullscreen=desktop. Off by
                          default. Toggle at runtime with Alt+Enter.
--fullscreen=<desktop|exclusive|off>
                          Fullscreen mode at startup. Off by default.
                          'desktop' = borderless letterbox over the
                          current desktop, no resolution change. Fast
                          to toggle; on Windows XP the taskbar can
                          occasionally show through this borderless
                          window.
                          'exclusive' = true fullscreen with a display-
                          mode change. Always covers the taskbar (the
                          XP fix). Slower toggle, may flicker on
                          multi-monitor rigs.
--scale=<1|2|3|4>         Windowed pixel scale at startup. Default: 3.
                          Adjust at runtime with the + and - keys.
--renderer=<software|accel>
                          Choose the SDL renderer. Default: accel.
                          'accel' uses the platform's hardware backend
                          (D3D / OpenGL / Metal). 'software' is a CPU-only
                          fallback for hosts where the hardware path
                          misbehaves.
--subpixel=<on|off>       Smooth sub-pixel scrolling. Default: on.
                          The game draws at whole-pixel camera positions, so a
                          true scroll of 1.5 px/frame steps 1,2,1,2. This shifts
                          the presented frame by the fractional remainder.
                          Quantised to whole output pixels, so it never blurs;
                          inactive at 1x window scale, where there is no
                          sub-pixel resolution to use. Turn off to compare.
--scaling=<sharp|smooth>  How the frame is scaled to the window. Default:
                          smooth. A fullscreen scale is rarely a whole number
                          (1080/272 = 3.97), so a plain nearest copy gives one
                          source pixel 4 output pixels and its neighbour 3 —
                          uneven text, clock digits and shirt numbers. 'smooth'
                          pre-multiplies the frame by a whole number first and
                          resamples only the leftover fraction; 'sharp' is the
                          old single nearest copy. Both are identical at a
                          whole-number scale, which is every windowed scale, and
                          'smooth' needs a hardware renderer.
--vsync=<on|off>          Sync frame presents to monitor vblank.
                          Default: off. Only meaningful with
                          --renderer=accel. Turn on if you see screen
                          tearing; the manual 50 Hz game-tick pace
                          runs as a backstop regardless.
--audio=<mono|stereo>     Audio output mode. Default: mono.
                          The Amiga's 4 audio channels are hard-panned
                          (1,2 → left, 0,3 → right) by Paula, which
                          sounds lopsided on most modern speakers /
                          single-speaker laptops. 'stereo' restores
                          the original Amiga panning if you'd rather
                          hear it that way (e.g. on headphones).
--dos-graphics=<on|off>   Render the DOS-version graphics (from bin/grafs_dos)
                          instead of the Amiga graphics. Default: off.
                          PC-only extra; the Amiga look is the default.
--gameplay=<amiga|dos|xbox>  The gameplay style, as [gameplay] style.
--dos-gameplay=<on|off>   DOS gameplay tuning — 70 FPS physics/timer rates
                          instead of the Amiga's 50. Default: off. (--gameplay=dos)
--dos-dir=<folder>        Folder under bin/ to load the DOS assets from.
                          Default: grafs_dos.
-h, --help                Show usage and exit.
```

There is deliberately **no `--arcade` flag** — arcade mode is enabled only via
`[arcade] enabled=true` in `SWOS.ini`, so a cabinet can't be knocked out of
coin-op by a stray command line. Every other `[arcade]` key is adjustable
in-game from the F2 service menu.

#### Choosing a render mode

SWOS' game logic runs at a fixed 50 Hz (the original PAL VBlank rate).
How smooth that looks on a given monitor depends on whether each frame
present aligns with the display's refresh:

| Monitor refresh | Suggested flags                              | Result                                                                                       |
|-----------------|----------------------------------------------|----------------------------------------------------------------------------------------------|
| 50 Hz           | `--vsync=on`                                 | Perfect 1:1 pacing. Each game frame lands on a vblank.                                       |
| 60 Hz / 75 Hz   | default (`--vsync=off`) or `--vsync=on` if you see tearing | Manual 50 Hz pace gates the game; the 50 → 60 pulldown is inherent.              |
| 144 Hz / 240 Hz | default                                      | Same as above; the manual pace fills the slack between vblanks.                              |
| Buggy hardware backend | `--renderer=software`                  | Drops to the CPU path. No monitor sync, manual pace only.                                    |
| Any refresh that is an exact multiple of 50 (100 / 150 / 200 / 300 Hz) | `--vsync=on` | Every game frame gets a whole number of refreshes — pacing is exact. `SWOS.log` says which case you are in. |

**If motion still is not perfectly fluid, it is probably not pacing.** The game
advances the camera at a fixed 50 Hz and draws at whole-pixel positions, so a
long ball moves the view up to 5 px in one 20 ms step and a fractional velocity
renders as an alternating 1,2,1,2 px cadence. `subpixel_scroll` (above) smooths
the second of those; the first needs the render/logic decoupling designed in
`docs/AMIGA_60.md`. `SWOS.log` reports the detected refresh rate, whether vsync
actually engaged (a driver or control panel can silently drop it), and whether
the refresh is an exact multiple of the game's pace.

### CRT effects

A modern panel draws SWOS's 336x272 as huge, razor-sharp squares. The `crt_*`
settings put back what an arcade tube did to the picture:

| setting | effect |
|---|---|
| `crt_mask=aperture` | vertical RGB phosphor grille (Trinitron-ish) |
| `crt_mask=slot` | aperture plus row-staggered slots — a shadow mask |
| `crt_mask=scanlines` | horizontal scanlines only |
| `crt_vignette` | darkens towards the corners |
| `crt_corners` | rounded glass edge |

All default off. The mask, vignette and corners fold into **one** pre-built
texture that is multiplied over the finished frame, so the cost is a single
full-screen draw no matter how many are enabled — the approach CannonBall-SE
uses to hold 60 fps on a Pi2 without a shader. Because it is a multiply, black
stays black and the grille shows in the mid-tones, which is what reads as "CRT"
rather than merely "dark". The texture is built at output resolution so the
grille lands on whole screen pixels.

This is presentation only: it runs on the finished frame and cannot affect the
simulation, so it is safe to leave on for replays and recordings.

**Uneven pixels are a different problem from uneven pacing.** The game renders
272 rows, and a monitor is almost never a whole multiple of that — 1080/272 =
3.97 — so scaling the frame with a plain nearest copy hands one source pixel 4
output pixels and its neighbour 3. Nothing moves, but text, clock digits and
shirt numbers look subtly wrong. `scaling=smooth` (the default) fixes it without
black bars or cropped rows: the frame is first blown up by a *whole* number with
nearest — exact NxN blocks, no error — and only the leftover fraction (3.97/4 =
0.99) is resampled. Both passes run on the GPU off the same single upload, so
the cost is one extra blit; measured at 3440x1440 it is 0.33 ms of a 20 ms
frame, and it drops the edge-placement error from 0.42 to 0.10 output pixels.
`SWOS.log` reports the factor it chose. `--scaling=sharp` restores the old
single copy to compare.

## Saves

**Where saves go differs by target**, because the two have different
constraints:

- **SDL** writes saves to the per-user folder described under
  [the settings file](#settings-file-swosini) — e.g.
  `%USERPROFILE%\Documents\SWOS\save\` — so an installed copy in a
  read-only directory still works.
- **Amiga** writes to `save/` in its drawer (`bin/save/` in a development
  tree, since FS-UAE mounts `bin/` as DH1: and the game's paths are relative
  to it).

Both are created automatically. The JSON save format is designed to move
between the two — it serialises *named fields* and semantic identities rather
than raw memory and pointers, so a save survives 32↔64-bit and LE↔BE. Copying
a `.CAR`/`.SEA`/`.PRE`/`.DIY`/`.HIL` across has been verified for league
careers; cup and tournament careers aren't fully cross-target tested yet.
See `docs/SAVE_FORMAT_JSON.md`.
