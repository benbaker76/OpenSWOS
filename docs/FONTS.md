# SWOS fonts — adding glyphs (Amiga + DOS)

The SWOS font is a **charset sprite sheet**: a 320×256 grid of glyph cells, each glyph
having a *small* (6px) and *big* (8px) variant. At draw time the game maps a string byte →
a sprite via the **conversionTable**, and the **image-script** defines each sprite's cell
rectangle. Adding a glyph (`$`, `€`, accents, …) means: draw it into a free cell, wire its
conversionTable entry + image-script rect, re-encode the sheet to the platform format, and
(for non-Latin-1 codepoints) add a translation byte mapping.

Two external trees drive this:
- **`C:\Projects\Python\SWOS_Font`** — `swos_font.py`, the glyph generator.
- **`C:\Projects\GitHub\SwosGfx`** (`bin\Debug\net8.0\SwosGfx.exe`) — the RAW ↔ PNG charset
  codec for both the Amiga (4-bitplane + RNC) and DOS charset formats.

---

## The pieces

| Piece | What it is | Where |
| --- | --- | --- |
| `CHARSET.RAW` | Amiga charset — 4-bitplane bitmap, RNC-compressed. DOS has its own charset in the DOS graphics set. | `bin/grafs*` |
| **conversionTable** | `byte → sprite index`, indexed by `byte − 0x20`; separate small + big tables (big sprite = small + 57). | `src/swos_globals.c` (uint16; sprite pointer table `_spriteGraphics` sized 1310 entries in `src/swos_amiga_data_stubs.c` — 1220 game slots + font-extension slots 1220..1303) |
| **image-script** | Per-sprite cell rect: `0xC000\|idx, (x<<8)\|y, (w<<8)\|h`, terminated by `0xFFFF`. | `src/intro_menu_image_script_cmds.inc` |
| `swos_font.py` | Injects new glyphs into free sheet cells; emits the C wiring. | `SWOS_Font/` |
| `glyphs.txt` | Editable glyph art (`*`=ink, `-`=blank) — the source of truth after the first run. | `SWOS_Font/` |
| `integration.txt` | The generated C wiring to apply (image-script + conversionTable + lowercase map). | `SWOS_Font/` |

---

## Process — add a glyph

1. **Decode** the stock charset to a PNG with **SwosGfx** → `CHARSET.png` (the 320×256 sheet
   `swos_font.py` reads).

2. **Author the glyph** in `swos_font.py`:
   - Punctuation → `PUNCT_SMALL` / `PUNCT_BIG` (hand-drawn `*`/`-` art).
   - Accented capitals → `ACCENTS` (derived from a base letter + an accent row; auto-compressed
     one row so the accent fits and the glyph keeps its native height, like the stock Ä/Ö/Ü).
   - Ligatures / symbols / currency → `LIGATURES` (hand-drawn art). `$` is derived from `S`
     (`dollar_art`); `€` is an authored C-with-two-bars.
   - **Codepoints > 0xFF** (no single Latin-1 byte: `Œ`, `Ÿ`, `€`) → assign a **spare
     conversionTable byte** in `CP_OVERRIDE` — a byte whose `conv[byte−0x20]` is 0. Current
     map: `Œ`=0x8C, `Ÿ`=0x9E, **`€`=0x9D** (chosen next to the stock pound 0x9C).

3. **Run** `python3 swos_font.py`. It:
   - Seeds the new glyph into `glyphs.txt` (the drop shadow, palette idx 8, is added
     automatically — author the white shape only).
   - Writes `new_charset.png` (sheet with the glyph injected into a free cell), `preview.png`,
     `new_charset_zoom.png`, and **`integration.txt`** (the C wiring).
   New glyphs are placed at sprite slots **1220+** — above the game's 0–226 (charset/menus)
   and the 227–1219 DOS player overlay — so they survive in **both** Amiga and DOS-graphics modes.

4. **Refine** the shape in `glyphs.txt`, check `preview.png`, re-run until it looks right.
   (`glyphs.txt` is only seeded once; after that your edits there are the source of truth.)

5. **Encode** `new_charset.png` back to `CHARSET.RAW` with **SwosGfx** — verified commands
   (`SwosGfx.exe` runs from WSL via interop *or* natively on Windows; run from `SWOS_Font/`):
   ```sh
   GFX="/mnt/c/Projects/GitHub/SwosGfx/SwosGfx/bin/Debug/net8.0/SwosGfx.exe"

   # a) PNG -> 8-bit indexed BMP — SwosGfx encodes from a BMP, not a PNG.
   python3 -c "from PIL import Image; Image.open('new_charset.png').save('new_charset.bmp')"

   # b) Amiga OCS font (bin/grafs/CHARSET.RAW). CHARSET uses the "menu" palette; 4 bitplanes
   #    + RNC are SwosGfx's defaults. -palette IS REQUIRED on encode (not auto-detected).
   "$GFX" -amiga -bmp -output=raw -palette=menu  new_charset.bmp  CHARSET.RAW

   # c) ALWAYS verify lossless: decode the new RAW back and diff indices vs the source BMP.
   #    Must be 0-diff (pixel-identical) — proves the encode round-trips the exact sheet.
   "$GFX" -amiga -raw -output=bmp  CHARSET.RAW  roundtrip.bmp
   ```
   Verify (c) in Python: `Image.open('new_charset.bmp').convert('P')` indices ==
   `Image.open('roundtrip.bmp').convert('P')` indices, 0 differences.
   - **AGA** (`bin/grafs_aga/CHARSET.RAW`): same, add `-bitplanes=8`.
   - **DOS** (`bin/grafs_dos/CHARSET.DAT`, `bin/grafs_2020/charset.dat`): the per-sprite path
     (`-dos -sprites-import <dir>` reading `sprNNNN.bmp`). The on-screen **text font is always
     the Amiga `grafs/charset.raw`** even in DOS-graphics mode, so the OCS encode above is what
     currency/menu glyphs need; the DOS `.dat` only matters for in-match digit overlays.
   - **Deploy**: copy the verified `CHARSET.RAW` over `bin/grafs/CHARSET.RAW` (the file the game
     loads — `strings_data.c`: `aGrafsCharsetRa = "grafs/charset.raw"`).

6. **Apply `integration.txt`** to the game — `scripts/gen_font_ext.py` does the mechanical part:
   ```sh
   python3 scripts/gen_font_ext.py   # integration.txt -> src/font_ext_script.inc + font_ext_conv.inc
   ```
   It regenerates the image-script rects (`font_ext_script.inc`, appended to `introMenuImageScript`)
   and the `conversionTable` `SB(idx, small, big)` entries (`font_ext_conv.inc`, `#include`d by
   `FontExt_ApplyConv` in `swos_globals.c`). Then, by hand:
   - **> 0xFF codepoints only**: add the spare-byte case to `tr_dup` (`src/translations.c`) —
     e.g. `cp == 0x20AC ? 0x9D` for `€` — so translated text round-trips through `tr()`.
   - **Deploy**: copy the re-encoded `CHARSET.RAW` over `bin/grafs/CHARSET.RAW`, then rebuild.

   > **Glyph order is logical, NOT minimal-diff.** Put each new glyph where it reads naturally
   > (`€` next to `$`, etc.). Inserting one shifts the sprite indices below it — that's expected
   > and fine: `gen_font_ext.py` regenerates the FULL wiring from `integration.txt`, so the
   > `.inc`s always match the sheet. **The sheet (`CHARSET.RAW`) and the `.inc`s are a matched
   > pair** — re-encode AND re-run `gen_font_ext.py` together and redeploy `CHARSET.RAW`, or the
   > glyphs misalign. The same applies to every charset you ship (AGA, DOS) — re-encode each.

---

## Currency symbols

A glyph becomes the money symbol via `Currency_Set(name)` in `src/string_utils.c`, which
patches the leading byte of every money format string (`aPound`, `a1k`, `a0m`, `a01m`):

| `SWOS.ini [general] currency=` | byte | glyph |
| --- | --- | --- |
| `pound` (default) | 0x9C | `£` (stock) |
| `dollar` | 0x24 | `$` (font ext) |
| `euro` | 0x9D | `€` (font ext) |

A world of the euro's years (World 2026-27) shows `pound` as the euro (`Currency_Apply`, the world
mod's setting `euro`; docs/WORLD_2026_27.md), on the Amiga too.

The supported languages (`en/de/es/fr/it/nl`) need only these three: `£` (UK English),
`$` (US English), and `€` for the five Eurozone languages (de/es/it/fr/nl). No other
currency glyph is required — the Swiss franc is the only near-miss and it's written `CHF`,
not a single symbol.

---

## Files

- `SWOS_Font/swos_font.py` — generator · `glyphs.txt` — editable art · `integration.txt` — wiring
- `SwosGfx.exe` — RAW ↔ PNG codec (Amiga + DOS)
- Game: `src/intro_menu_image_script_cmds.inc` (image-script), `src/swos_globals.c`
  (conversionTable), `src/swos_amiga_data_stubs.c` (sprite-table size), `src/translations.c`
  (`tr_dup` for > 0xFF), `src/string_utils.c` (`Currency_Set`)
