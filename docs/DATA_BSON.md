# Fast binary data on the Amiga — BSON, alternatives, and compression

Companion to `docs/DATA_JSON.md`. The JSON migration gives the data a portable,
editable, layout-decoupled representation — ideal for the SDL/modern host. This
document covers the **other half**: how the **Amiga** keeps reading/writing this
data quickly, since parsing text JSON on m68k is too slow. It weighs **BSON** (a
binary JSON encoding) and alternatives, and surveys **compression** options.

TL;DR recommendation is in §6.

> **Status (2026-09-30): decided — §3's "author JSON, export binary" is what
> shipped.** Competitions, menus and tactics are baked JSON → C at build time
> (`scripts/emit_competitions.py` / `emit_menus.py` / `emit_tactics.py` →
> `src/competition_data.c` / `menu_data.c` / `tactics_data.c`), and the teams /
> TMD / POOLPLYR binaries are regenerated from JSON by `scripts/emit_teams.py`
> (RNC-packed). The Amiga parses no JSON, BSON or MessagePack at run time. See
> `docs/DATA_PIPELINE.md`. The rest of this document is the analysis behind that
> choice.
>
> **2026-11-06:** the mods follow it too (docs/MODS.md §30): their menus are baked into the
> Amiga program at build time (`tools/menu_bake`), their data converted by OpenSWOS Setup, their
> records fixed binary. **The one exception is saves** -- career, season and competition saves are
> the save format's JSON (`src/save_format_json.c`) on both targets, which this decision never
> covered. Measured on an emulated A1200 (68020 + 8 MB fast RAM): an 836 KB career loads in
> 254 s and saves in 14 s. The owner: leave them as they are for now.

---

## 1. The problem: JSON parsing is too slow on m68k

The Amiga targets are 7–50 MHz 68000/020/030 with little RAM and no fast JSON
library. A team database like `ENGLAND.json` is ~93 teams × (13 header fields +
16 players × 14 fields) ≈ **20,000 values**, mostly small integers. Text JSON
parsing means, per value: tokenize, then `atoi`/`strtol` (decimal→int), plus
string handling and object/array bookkeeping. On a 7 MHz 68000 that is on the
order of **seconds per file** — unacceptable for a game load, and worse for the
900-record `POOLPLYR.DAT`.

By contrast the **current** binary (`TeamFileEx[]` etc.) loads in roughly the
time to **decompress + `memcpy`** — there is no parse at all; the file *is* the
in-memory struct. That speed is the bar any Amiga format must clear.

So: **the Amiga stays on a binary format.** The question is *which* binary, and
how to compress it.

---

## 2. Candidate formats

| format | parse cost on m68k | size (uncompressed) | named / self-describing | endianness | decoder code |
|--------|-------------------|---------------------|------------------------|-----------|--------------|
| **Raw fixed structs** (current) | ~none (cast in place) | smallest (=data) | no (layout-coupled) | native (BE on disk) | none |
| **MessagePack** | low (typed, compact) | small–medium | optional (arrays vs maps) | **multibyte BE** ✓ | small |
| **BSON** | low–medium | **largest** (field names + int32 min) | yes (always keyed) | multibyte **LE** ✗ | small–medium |
| **JSON** (text) | **high** (atoi/strtol) | medium | yes | n/a | medium (mjson) |

Notes:

- **Raw fixed structs** — what `TEAM.NNN` / `*.TMD` / `POOLPLYR.DAT` already are.
  Fastest possible, smallest. The downside is the *exact* thing the JSON
  migration removes: it is coupled to the struct layout (offsets, padding,
  endianness) and not portable/editable. But on a **single frozen target** it is
  perfectly fine — and the Amiga *is* such a target.
- **BSON** — length-prefixed, typed elements (`int32`/`int64`/`double`/`string`/
  `document`/`array`/`binary`). Numbers are raw binary (no `atoi`), so parsing is
  far cheaper than text JSON. But for *this* data BSON is a poor fit:
  - It stores a **field-name string with every element**; our records are tiny
    (mostly `u8`), so the names dominate the file — BSON is *larger* than the raw
    data, often larger than pretty JSON.
  - Its smallest integer type is **int32**, so a `u8` skill byte costs 4 bytes +
    name. ~4–5× bloat per number unless you pack fields into BSON `binary`
    blobs — at which point you've wrapped a struct in BSON overhead.
  - Multibyte values are **little-endian**; the Amiga is big-endian, so every
    int needs a byteswap on read.
- **MessagePack** — same idea as BSON but compact: small-int encoding (a `u8`
  fits in 1–2 bytes), and *arrays* avoid per-field name strings entirely. Its
  multibyte integers are **big-endian**, i.e. no byteswap on Amiga. If a
  self-describing binary is genuinely wanted, MessagePack beats BSON on every
  axis that matters here (size, speed, endianness).
- **JSON** — kept as the **host** source of truth; not parsed on the Amiga.

---

## 3. Recommended pipeline: author JSON, export binary

The cleanest answer avoids putting *any* document parser on the Amiga:

```
   bin/data_json/*.json   ──(host build step)──►   bin/data/*  (binary, RNC)
   (source of truth,            encode_* +              (what the Amiga and
    edited/versioned)           RNC pack                 SDL read at runtime)
```

- **JSON is the source of truth** (portable, editable, diffable, layout-decoupled
  — see DATA_JSON.md). All authoring happens here.
- A **host build step** converts JSON → the Amiga's binary. The reverse encoders
  already exist (`swos_team_file.encode_blob` / `encode_tmd_blob` /
  `encode_poolplyr_blob`), proven byte-exact by the 0-mismatch round-trip; add an
  RNC pack (`rnc_propack` `do_pack`) to match the shipped `.TEAM/.TMD/.DAT`.
- **Amiga runtime is unchanged**: it reads the fixed binary (fastest), no JSON or
  BSON parser on m68k.
- The **SDL** build can read JSON directly (host has the speed) or the same
  binary — its loader auto-detects (DATA_JSON.md §5).

This delivers every benefit of the migration (named fields, portability,
editability, cross-target safety) while paying **zero** parse cost on the Amiga.
It is the recommended path, and needs no new on-Amiga format at all.

---

## 4. If a self-describing binary must run on the Amiga

The pipeline above means the Amiga never needs BSON. The only reason to add a
self-describing binary at runtime is **mod support without a host rebuild** (drop
in an edited data file and have the game read it). If that becomes a goal:

- **Prefer MessagePack over BSON.** Big-endian multibyte (no swap), compact ints
  (our `u8` fields stay 1 byte), array form drops field-name overhead. A
  streaming reader decodes straight into the existing structs (no DOM, low RAM).
- **BSON is feasible but ill-suited** here: little-endian (byteswap), int32
  minimum + per-field names bloat tiny records. Only choose it for ecosystem
  interop reasons that don't apply to SWOS.
- **Either way, parse streaming into the struct** — read element, dispatch on a
  short key/index table (like `load_struct_object`), write the field. No
  allocation. Expect a few ms on 020/030; more on a 7 MHz 68000 but far below
  text-JSON cost. **Measure before adopting.**

A fixed-schema "SWOS binary record v2" (the current layout + a version/endian
tag) is the in-between: keeps near-struct speed, gains forward-compat, no
field-name overhead. Often the better trade than a generic format.

---

## 5. Compression

Whatever binary ships, compress it (the current files already are RNC). m68k
decompressors trade ratio vs decode speed vs code/RAM:

| codec | ratio | decode speed | m68k decoder | notes |
|-------|-------|-------------|--------------|-------|
| **RNC ProPack v1** (Huffman) | good | medium | **already in `src/rnc.c`** | what the data ships as today |
| **RNC ProPack v2** (LZ) | ok | fast | **already in `src/rnc.c`** | faster decode, slightly worse ratio |
| **LZ4** | low | **fastest** | tiny | best when decode speed dominates |
| **LZSA / LZSA2** | good | very fast | tiny | designed for 8/16-bit; great 68000 fit |
| **aPLib** | very good | fast | small | Amiga-scene staple; better ratio than LZ4/RNC v2 |
| **Exomizer** | very good | medium | small | popular on retro; good ratio |
| **Shrinkler** | **best** | slow + more RAM | medium | demo/intro packer; size-over-speed, read-once |
| zlib/deflate | good | medium | larger | heavier inflate on m68k; only if interop needed |
| zstd | excellent | fast (modern) | **large/heavy** | impractical on m68k — not recommended |

Guidance:

- **Default: keep RNC.** It is already integrated (`src/rnc.c` decompress,
  `rnc_propack.py` pack), proven on all current data, and costs nothing new. Use
  **v1** for best ratio, **v2** when decode latency matters.
- If files grow or load time becomes a bottleneck, evaluate **aPLib** (best
  ratio with a small fast m68k decoder) or **LZ4/LZSA** (max decode speed). All
  have compact, well-known 68000 decoders.
- **Avoid zstd/zlib on the Amiga** — decoder size/RAM outweigh the benefit at
  this data scale.
- Compression also blunts BSON's verbosity, but it cannot make BSON smaller than
  a compressed compact format — compress the *leaner* representation, don't rely
  on the packer to fix a bloated one.

---

## 6. Summary recommendation

1. **JSON stays the source of truth** on the host (DATA_JSON.md).
2. **Amiga runtime stays on the fixed binary** (current `TeamFileEx[]` /
   `PoolPlayer[]` layout) — fastest, no parser. **Generate it from JSON** with a
   host build step (`encode_*` + RNC pack already available).
3. **Compress with RNC** (status quo); revisit **aPLib** (ratio) or **LZ4/LZSA**
   (speed) only on a measured need. Skip zstd/zlib on m68k.
4. **Adopt a self-describing runtime binary only if mod-without-rebuild is
   required** — and then prefer **MessagePack** (big-endian, compact) over BSON,
   or a versioned fixed-schema record. Benchmark on the slowest target first.

The headline: the Amiga does not need BSON. The JSON→binary export pipeline gives
the migration's portability/editability with none of the m68k parse cost, and RNC
already handles compression.
