# The Atari Jaguar sprites (`--jag-graphics`)

Sensible Soccer's 1995 Atari Jaguar release redrew the gameplay art at 256
colours: the same players, the same animations, the same bank layout — every flat
colour turned into a small ramp of shades. SwosGfx converts it back into the
Amiga banks (`-jag2amiga`), which means the game's own image scripts cut the same
sprites out of it, and the whole port of it comes down to one question: **colour**.

```bash
scripts/make_jag_graphics.py            # SwosGfx's BMPs -> bin/grafs_jag
bin/SWOS.x86_64 --jag-graphics=on       # or [graphics] sprites = jaguar in SWOS.ini
```

It is the **players and the ball**. The pitch, the HUD, the menus and every other
screen are exactly what they were — this is a sprite tier, not a palette swap.
SDL only: an 8bpp bank is twice the chip RAM and twice the blitter bandwidth of
the 4bpp one the A1200 draws today, so the Amiga build does not take it.

**The web build carries the banks** in its preload (`--preload-file bin/grafs_jag`,
~0.5 MB) but, like every other build, plays with the Amiga sprites unless asked:
`[graphics] sprites = jaguar` in the in-browser `SWOS.ini` turns the Jaguar art on
(`auto`, the default, is `amiga`). It was on by default there until 2026-10-01.

## What the art does with colour

Comparing the Jaguar sheets against the 16-colour originals pixel for pixel —
they are the same drawings, so the comparison is exact — every Amiga colour
became either itself or a ramp:

| Amiga colour | | Jaguar |
|---|---|---|
| 1, 2, 3 | greys, black | the same, 1:1 |
| 4, 5, 6 | skin | the same, 1:1 |
| 9, 12, 13 | hair | the same, 1:1 |
| 8 | the drop shadow | 29–31, 220–223 — a ramp, see below |
| 10 | shirt stripes | **240–243** |
| 11 | shirt | **244–247** |
| 14 | shorts | **248–251** |
| 15 | socks | **252–255** |

So a Jaguar pixel is "*the colour SWOS means*, at *a shade*", and **only the kit
has to follow the team**: four slots of four shades at the top of the palette,
brightest first. Skin and hair need nothing new — the game already varies exactly
those indices per player (`ConvertColorTableToSkinColor` rewrites 4,5,6 and
9,12,13, and no others), and the art kept them. The keeper needs nothing either:
its jersey is drawn in the shorts and socks slots, as on the Amiga.

### Hair, where SWOS throws the shading away

The art draws hair in three shades (13 light, 9 mid, 12 dark) and the game's own
table decides what they become per face type. For a ginger head it keeps three
(`F60 B40 620`), so that hair shades by itself. For a **black** head — face types
0 and 2 — it sends all three to colour 3, and the Jaguar's shading is lost: a flat
block, exactly what the 16-colour game draws.

So when the three collapse onto one colour, this tier shades that colour instead.
Black has nothing darker than it, so the highlight lifts rather than the shadow
deepening: `000000` stays the shadow and the lit side becomes `333333` and
`666666`.

**How far it lifts is the whole point of the tier, and it was too timid.** The
first attempt lifted to `111111` and `333333`, and `111111` is indistinguishable
from the black beside it — so a black-haired player still read as the flat block
the 16-colour game draws, which is exactly what got reported (2026-09-21). The
other end is just as wrong: lift to `777777` and it reads as grey hair, not shaded
black. `666666`/`333333`/`000000` shades it while the mass stays black. It is
taste, so `SWOS_JAG_HAIR=<hi>,<mid>` (4-bit channels) overrides `s_lift0`/`s_lift1`
without a rebuild, and `SWOS_JAG_DUMP=1` prints the ramp a collapse produced.
Hair the game already shades is left alone.

**The lift follows `[graphics] shade_step`**, the kit's own contrast setting: the
highlight lifts by 3 × step and the mid shade by half that, so the default step of
2 is the `666666`/`333333` above, 1 is `333333`/`111111` and 3 is `999999`/`444444`.
A collapsed colour that is *not* near-black deepens instead, by the kit's ramp
(16, 16−step, 16−2·step sixteenths). `[graphics] hair_shading = false`
(`--hair-shading=off`) skips the whole step: the game's table as it stands, so a
black head is the flat block the 16-colour game draws.

Skin has a milder version of the same thing: face type 2 sends 6,5,4 to 4,4,3, so
its darkest tone is drawn with two shades rather than three. Left as the game has
it — changing it would move a skin tone, not just its shading.

### The shadow is the pitch, not a colour

The 16-colour game draws its drop shadow in one near-black colour (`0x020`),
which reads as a shadow on any pitch. The Jaguar replaced it with seven shades
— and they are **green**, because a shadow is *the ground in shade* and its
ground was the Jaguar's own grass (`317100` in the cartridge's pitch CLUT). Ours
is not: the pitch palette changes with the condition, and muddy is brown
(`705000`). Keeping the art's greens put green patches under the players on a
muddy pitch — reported 2026-09-21, and the whole reason this section exists.

So these seven are the only entries the tier re-makes after placing them. Each
keeps the brightness the artist gave it, measured against the grass it was drawn
on, and is rebuilt from **our** grass whenever the pitch palette is applied
(`JagGfx_PitchApplied`, called from `match_setup.c` and again when the banks are
decoded, since either can come first):

| art | of the grass | muddy `750` | normal `790` |
|---|---|---|---|
| 29 | 16/16 | `750` | `790` |
| 30 | 14/16 | `640` | `670` |
| 31 | 12/16 | `530` | `560` |

**These three and no others.** 220–223 look like a deeper shadow ramp and are
not. A pixel-for-pixel diff says they come from Amiga colour 8 — but only on
`CJCTEAMG`, where colour 8 *also* draws the keeper's green jersey, and the Jaguar
artists split the two: 29–31 for the ground shadow, 220–223 for the jersey. The
outfield sheets contain no 220–223 at all. Recolouring them per pitch turned the
keeper brown on a muddy pitch (reported 2026-09-22). An index does not say what a
colour *means* — only the sheet it is drawn on does.

The grass is worked out for the pitch being set up rather than read back out of
the palette, so it does not matter which of the two callers runs first — and it
comes from whichever palette the match will be drawn in, the DOS one included.
`SWOS_JAG_DUMP=1` prints the ramp at kick-off; `SWOS_PITCH_TYPE=0..6` pins the
pitch (frozen, muddy, wet, soft, normal, dry, hard) so all seven can be looked at
without playing a season.

## It needs almost no palette of its own

The game's 256 colours were converted from the DOS palette, which carries dimmed
copies of the 16 SWOS colours — which is exactly what a kit ramp is. **Every kit
colour's four shades are already in there**: the furthest is 3 units of 4096 from
the shade we would have computed. So are 64 of the art's own 76 colours. The tier
therefore **re-uses** entries instead of installing them:

- a kit shade is the nearest existing entry to colour × {16/16, 13/16, 9/16,
  6/16} — the ratios the Jaguar's own artwork uses (FF, D6, A5, 7B) — and
  **shade 0 is the kit colour itself**, so a Jaguar kit is the colour the Amiga
  draws with shading under it;
- an art colour takes the nearest entry when one is close enough and not already
  spoken for, and only otherwise costs an entry of its own — first a **donated**
  duplicate (below), and only on a tier with genuinely free entries (AGA) the old
  high pool (201–239).

In practice: 59 of the art's colours found in the palette, 22 given spare
entries. A very dark kit shades very little and a black one not at all — which is
what the 16-colour game does too.

### The palette is not a scratchpad

This is the part that bit. These entries belong to someone, and the tier must
neither write them nor re-use them:

| Entries | Whose |
|---|---|
| the nine in `_paletteIndices` | the **pitch** — rewritten per pitch condition |
| 0x70–0x7F | the **replay "R"** bank (`soccer_s`, sprites 0x447–0x486) |
| 240–255 | the kit block: what a Jaguar sprite's pixels *mean*, never a destination |

The first version of this tier installed the art's colours from entry 82 upward,
straight through 0x70–0x7F, and the spinning R came out in the wrong colours.
The stadium and crowd backgrounds are 8bpp art that reaches across the palette
too, which is why the pool sat high and small — and in the DOS tiers even that
was wrong: `grafs_2020` draws in every entry of 201–239 (the stadium roof turned
yellow). So the tier now takes **donations**: the DOS palette carries 62 entries
byte-identical to another (109, 110, 147, ... are all `#000000`, like entry 3);
the art drawn in one is remapped to its twin, invisibly, and the entry is free
(`src/jag_graphics.c`, "THE DONATION MAP"; docs/PALETTE_V2.md §1). To check: `SWOS_JAG_DUMP=1`
prints the R bank's entries at kick-off — they must be what the game put there.

## With the DOS graphics

`[graphics] style = dos | 2020` and `[graphics] sprites = jaguar` can both be on — DOS pitch and
backgrounds (`grafs_2020`, say) with Jaguar players. Two things make that work,
and neither is automatic:

- **The DOS overlay leaves the players alone.** DOS mode rebuilds the player,
  keeper and bench sprites from its own `.dat` files *after* the banks have
  loaded, which overwrote the Jaguar ones. With the Jaguar tier on it skips them
  and keeps the rest of its look.
- **A DOS match is drawn in the DOS palette**, not the Amiga one
  (`platform_sdl.c`), so this tier matches its colours against whichever palette
  is live — otherwise it picks entries by colours nobody displays. The DOS
  palette covers the art just as well (worst kit shade 3 of 4096, 64 of 76 art
  colours within 2), which is no coincidence: the Amiga palette was converted
  from it. The few colours that need an entry of their own are placed in both
  (`DosGfx_SetExtraColour`).

The mode is read at startup, so the colours are placed for the mode the game
started in.

## How it is wired

- **`scripts/make_jag_graphics.py`** turns SwosGfx's `Amiga_Images_256_Jag` BMPs
  into `bin/grafs_jag/*.RAW`: 320×256 sheets of 8 bitplanes, 8 slabs of 40 bytes
  a scanline. The loader recognises 8bpp from the size alone
  (`DetectBankPlanes`), exactly as it already does for the AGA `soccer_s` bank —
  nothing in the sprite pipeline needed changing. Beside them it writes
  `JAG.PAL`: the sheets' shared palette *and which of it the art draws in*, so
  the game places exactly the colours the art uses and looks at no others.
- **`src/resload.c`** puts `grafs_jag/` in front of `grafs_aga/` when the tier is
  on. It holds the gameplay banks only (`CJCTEAM1/2/3`, `CJCTEAMG`, `CJCBITS`,
  `CJCBENCH`), so everything else falls through to the existing tiers.
- **`src/image_cmd.c`** maps a Jaguar bank's colours into the palette as its
  pixels are decoded; the low 16 and the kit block pass through untouched.
- **`src/color_sprites.c`** turns the 16-entry table the game builds for a team
  into the 256-entry one a Jaguar sprite needs (`JagGfx_TeamTable`), per range,
  because the skin and hair entries change between ranges.
- **`src/jag_graphics.c`** is the colour: which entry each of the art's colours
  lands on, and which four a kit's shades come from.

## Keeping it honest

The tier is off by default (except on the web build, above) and every part of it
is behind that flag: with it off,
`JagGfx_Enabled()` is false, the loader searches where it always did, the decode
remap is a null pointer and the recolour is the 16-entry table the game built.

To see it: `--jag-graphics=on --view-replay=<file>.SRP` with
`SWOS_SCREENSHOT_INTERVAL=N` renders a recorded match to PNGs, and
`SWOS_PITCH_TYPE=1 --replay-test=tests/replay/p95-spr.swr` plays one on a muddy
pitch (the rig's own assertions diverge under a pinned pitch — it is being used
as a match, not as a test). A player sprite
comes out with about 19 colours in its box where the Amiga has 10, and the kit's
brightest shade equals the Amiga's flat kit colour.

## Where the Jaguar has not got the sprite

The conversion lays the Jaguar sheet into the Amiga bank layout by lifting each
placement from the *same coordinates*, so it only works while the two releases
agree on where a sprite lives. Where they do not, the walk quietly picks up
whatever the Jaguar put there instead, and you get a real sprite in the wrong
slot — much harder to notice than a blank one.

`JagBank.Only` / `JagBank.Except` in `JagToAmiga.cs` are that list. To re-derive
it, score every placement's non-zero mask in the Jaguar sheet against the same
rectangle in `Amiga_Images_256/<bank>.bmp` (intersection over union) and look at
anything below ~0.7. Look at it — a low score is as often a legitimate Jaguar
redraw (its clock captions are the same words in a different font) as it is a
wrong sprite.

**The bench managers were the expensive one.** The Amiga has two: COACH1 in a
brown coat at 1182-1187, COACH2 in a white shirt at 1189-1194. The Jaguar drew
only the brown one, and repeated it across COACH2's six cells and the blanks
either side. `bench_subs.c` picks the coach by team (`teamNumber != 1` adds 7),
so the top bench drew a brown manager and the white-shirted one was missing from
the tier entirely — reported as a coach "turned 180° vs the pitch", because the
white shirt is what gives that figure its facing.

**The SELECT SUBSTITUTE arrow (`CJCBENCH` 1170) points the other way.** The Jaguar
draws it pointing LEFT; ours sits at the bench's top and must point RIGHT, at the
substitutes beside it (ReportBot 0009, "the arrow is turned 180°"). A right place,
a right shape, a wrong direction -- the placement score cannot see that, since a
mirror image matches as well as the original. `Jag/CJCBENCH_PATCH_272_120.bmp` is
the Jaguar's own arrow mirrored, with its drop shadow recast down-right (the body
shifted one right and one down, shaded 62/61/60 as the Jaguar shades it) instead of
mirrored to the left.

Two options each time:

- **Fall back to DOS.** Add the range to `Except` and the DOS sprite is blitted
  in the Jaguar palette instead. Correct immediately, and it looks like DOS art.
- **Paint it.** `SwosGfx -dos -jag2amiga -jag-template` writes, per bank,
  `<bank>_DOS.bmp` (the whole original bank's DOS art in the Jaguar palette),
  `<bank>_TODO.bmp` (only the cells the Jaguar has not got — the redraw list)
  and `<bank>_placements.csv` (every cell's index, source and rectangle).
  The repaint goes back as `Jag/<bank>_PATCH.bmp`, a whole 320x256 bank, or as
  `Jag/<bank>_PATCH_<x>_<y>.bmp`, a crop of any size pasted at that bank
  position — the coordinates are in the name, so the art carries its own
  placement and no table in the converter has to know about it. Either way the
  cells it covers are then taken from the sheet whatever `Except` says.
  `JAG_GAME.act` is the palette to load.

  The two differ in what colour 0 means. In the whole-bank file it is
  transparent, or the file would have to redraw the bank. **A positioned crop
  replaces its rectangle outright, zeros included**, because the sprite it
  covers is usually a different shape: painting only the non-zero pixels of the
  white-shirted manager left 11–38 pixels of the Jaguar's brown one showing
  around each of the six frames.

  What the repaint may use is the same rule as any other sprite: **not Amiga
  colours 10, 11, 14 or 15**, which are the kit slots and follow the team. The
  original manager sprites stay inside 0–8 and 12 for exactly that reason, so
  the practical recipe is to keep the Amiga art and move its shadow (Amiga 8)
  onto a Jaguar shadow index — which is what was done here.

`-jag-dump-sheets` writes the raw unpacked Jaguar sheet, which is how you tell
"the Jaguar has not got it" from "the Jaguar has it somewhere else".

## Sprite sets as mods

The Jaguar tier is also the format for **sprite sets a mod brings** (docs/MODS.md §16-17): players
drawn with more colours than the Amiga's 16, by anyone. A mod whose `mod.json` says `"sprites":
"jaguar"` switches the tier on while it is on (no `[graphics] sprites = jaguar` needed), and its
`grafs_jag/` sheets go over the player's own Jaguar data through the overlay -- every sheet it
has; the ones it does not have come from the player's Jaguar data, else the Amiga's. It is
cosmetic, so it never matters online.

**The colour roles** the sheets must keep to -- the same as the Jaguar's own art (above), so
the game can still dress every team and every player:

| index | means | at run time |
|---|---|---|
| 1-3 | greys | as the Amiga's |
| 9, 12, 13 | hair | each player's (ConvertColorTableToSkinColor; the hair mod's too) |
| 4, 5, 6 | skin | each player's |
| 29-31, 220-223 | the shadow's shades | the grass-tinted shadow (`shadow_shading`) |
| 240-243 / 244-247 / 248-251 / 252-255 | stripes / shirt / shorts / socks, four shades each (lightest first) | the team's kit (`outfit_shading`, `shade_step`) |
| any other | the art's own colour, from its palette | fitted to the nearest free palette entry (`JAG.PAL`) |

**Making one:** draw 8bpp sheets in the Amiga's bank layout (320x256, the same cells: SwosGfx's
`Amiga_Images_256_Jag` output is the template), all with one palette, then

    scripts/make_jag_graphics.py MY_SHEETS --mod mods/mysprites --name "My sprites"

writes `mods/mysprites/grafs_jag/` (the planar banks and `JAG.PAL`) and, when there is none, a
`mod.json` with a uuid of its own, `"sprites": "jaguar"` and **the four shading settings as the
mod's own** -- `outfit_shading`, `shadow_shading`, `hair_shading`, `shade_step`, with the defaults
its art wants. They show on its page (OPTIONS -> MODS, fire on its name) and are read at start-up
while it is on; with no sprite mod on, `[graphics]`'s keys work as before (SWOS.ini always holds
those, so they cannot be a mod's defaults). Checked: a mod of the Jaguar's own sheets, with the
setting off -- `[mods] testsprites: its sprites, by the Jaguar tier (outfit 1, shadow 1, hair 1,
step 2)`, then `[jag] Jaguar sprites: ...`; without it, the Amiga art.

## What is not done

- **The Jaguar's own pitch, menus and HUD.** The Jaguar CLUTs include a pitch
  palette (`clut_6FE48_pitch`), so the art exists on the cartridge; SwosGfx does
  not extract it yet, and the pitch is a different problem from the sprites (its
  colours are patched per pitch condition).
- **The celebration cells.** The Jaguar game has no goal celebration; SwosGfx
  stamps hand-drawn art over those cells, so they are not Jaguar art.
- **`CJCBITS` 1090**, which comes from the DOS sprites — see above. The
  white-shirted bench manager (`CJCBENCH` 1189-1194) was the other one and is
  now painted: `Jag/CJCBENCH_PATCH_16_24.bmp` and `_PATCH_256_24.bmp`. The bench
  arrow (1170) is the Jaguar's own, mirrored: `Jag/CJCBENCH_PATCH_272_120.bmp`.
- **`CJCGRAFS`** (the scoreboard font and clock captions) is left to the Amiga
  set, so the HUD keeps its own palette entries.
- **The Amiga target**, for the memory and bandwidth reason above.
