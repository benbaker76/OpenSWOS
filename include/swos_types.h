/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* swos_types.h — fixed-width integer types portable across both ports.
 *
 * The Amiga m68k cross-toolchain (m68k-amiga-elf-gcc) is built without
 * a hosted libc and has no <stdint.h> / <stddef.h>. Each existing
 * Amiga port header (asset_loader.h, audio.h, …) typedef's
 * `uint8_t`/`uint16_t`/`uint32_t`/`int16_t`/`int32_t` at the top.
 *
 * The back-ported headers and source files from the SDL port use the
 * full set: `int8_t`, `uintptr_t`, `size_t`, `offsetof`. To keep both
 * trees compiling from the same sources, include this header instead
 * of `<stdint.h>` / `<stddef.h>` in files that need any of those.
 *
 * On hosted builds (SDL Linux/macOS/Cygwin64) it pulls in the standard
 * headers. On the Amiga build it provides minimal manual typedefs that
 * match the existing per-header definitions exactly (gnu11+ allows
 * typedef redeclaration with identical types, so this is safe to
 * include alongside the legacy headers).
 */
#ifndef SWOS_TYPES_H
#define SWOS_TYPES_H

#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
/* Amiga toolchain: no <stdint.h>. Define the types manually so the
 * back-ported SDL files compile against the same declarations the
 * SDL build gets from libc's stdint.h.
 *
 * Use `unsigned int`/`int` (not long): matches what hosted libc's
 * stdint.h does on every 64-bit target (where long is 64-bit), and
 * matches the existing per-header typedefs in game_types.h /
 * menu_system.h on the Amiga port. m68k int is 32-bit so this is
 * still a 4-byte type. */
typedef unsigned char   uint8_t;
typedef signed char     int8_t;
typedef unsigned short  uint16_t;
typedef short           int16_t;
typedef unsigned int    uint32_t;
typedef int             int32_t;
typedef unsigned int    uintptr_t;     /* pointer is 32-bit on Amiga */
typedef int             intptr_t;      /* signed pointer-width (e.g. save_format_json.c
                                          sentinel arithmetic `pv > -0x1000`) */
/* size_t: GCC's built-in str/mem functions expect `long unsigned
 * int`, so use unsigned long here even though it is the same width
 * as unsigned int on m68k. Matching the canonical type avoids
 * Wbuiltin-declaration-mismatch when our headers declare
 * strlen/memcmp/strncmp on Amiga. */
typedef unsigned long   size_t;
typedef long            ptrdiff_t;
typedef long long       int64_t;
typedef unsigned long long uint64_t;

/* Limits — UINTPTR_MAX is used as the 32-vs-64-bit selector in
 * sync-set headers (e.g. `#if UINTPTR_MAX == 0xFFFFFFFFu`). Without
 * this, the preprocessor sees `UINTPTR_MAX == 0` and silently picks
 * the 64-bit struct-size branch, producing bogus _Static_assert
 * failures on Amiga. */
#ifndef UINTPTR_MAX
#define UINTPTR_MAX 0xFFFFFFFFu
#endif
#ifndef UINT32_MAX
#define UINT32_MAX  0xFFFFFFFFu
#endif
#ifndef INT32_MAX
#define INT32_MAX   0x7FFFFFFF
#endif
#ifndef UINT16_MAX
#define UINT16_MAX  0xFFFF
#endif
#ifndef INT16_MAX
#define INT16_MAX   0x7FFF
#endif

/* offsetof — usually lives in <stddef.h> which Amiga build also
 * lacks. The classic null-pointer-deref form is undefined per the
 * C standard but every real-world compiler accepts it; gcc has
 * always supported this exact expansion. */
#ifndef offsetof
#define offsetof(t, m) ((size_t)((char *)&(((t *)0)->m) - (char *)0))
#endif

/* NULL — also normally in <stddef.h>. */
#ifndef NULL
#define NULL ((void *)0)
#endif

#else
/* Hosted build (SDL on Linux/macOS/Cygwin64). Use the standard
 * headers — they give us identical types plus full implementations
 * of size_t/ptrdiff_t/offsetof. */
#include <stdint.h>
#include <stddef.h>
#endif

/* SWOS_WORLDS: the football worlds beyond 96/97 (include/world.h; docs/WORLD_2026_27.md) --
 * World 2026-27 and everything it brought: its countries, leagues, splits, play-offs, cups,
 * pools, the Coppa Italia Serie D, Europe's league phases. `make SWOS_WORLDS=0` compiles them
 * out: the game is 96/97 alone, as it was before them (its world pinned, the worlds' data
 * gone, every world hook inert). Default on. */
#ifndef SWOS_WORLDS
#define SWOS_WORLDS 1
#endif

/* Global team numbers (a country's base + ordinal, _teamsCountryNumbers): the tables indexed
 * by them -- the league offsets (_diyLeagueSeasonStateTable, its scratch). The original's
 * 2000 held 96/97's 1730; a world's whole set needs more (World 2026-27: 2,226 clubs). */
#if SWOS_WORLDS
#define GLOBAL_TEAM_NUMBERS 2560
#else
#define GLOBAL_TEAM_NUMBERS 2000
#endif

/* The season end's TMD pool of the European cups' (and the Copa's) club records
 * (_tmdFileBuffer): 96/97 stages 80 + 20; a world's league phases 108 + 20. */
#if SWOS_WORLDS
#define TMD_POOL_RECORDS 160
#else
#define TMD_POOL_RECORDS 128
#endif

/* Cross-toolchain debug print. Amiga m68k toolchain has no <stdio.h>,
 * so fprintf(stderr, ...) doesn't compile. SWOS_DBG / SWOS_DBG_FLUSH
 * expand to a real fprintf on hosted SDL builds and to a no-op on
 * Amiga — match the endian_helpers.h pattern of "host-side fix that
 * compiles to nothing on Amiga".
 *
 * (KPrintF was tried as an Amiga path but lives in gcc8_c_support.c
 * which the Amiga Makefile filters out, so referencing it breaks the
 * link. For per-call diagnostic output on Amiga, write to a fixed
 * global buffer and inspect via WinUAE memory dump instead.) */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
#define SWOS_DBG(...)         ((void)0)
#define SWOS_DBG_FLUSH()      ((void)0)
#else
#include <stdio.h>
#define SWOS_DBG(...)         fprintf(stderr, __VA_ARGS__)
#define SWOS_DBG_FLUSH()      fflush(stderr)
#endif

#endif /* SWOS_TYPES_H */
