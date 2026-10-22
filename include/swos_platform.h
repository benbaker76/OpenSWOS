/*
 * swos_platform.h - Platform compatibility shim for SDL build
 *
 * Force-included via -include in CFLAGS so it precedes every translation unit.
 * Replaces Amiga-specific section attributes with no-ops for x86/x64 Linux/Windows
 * SDL builds.
 */

#ifndef SWOS_PLATFORM_H
#define SWOS_PLATFORM_H

/* When the Amiga m68k toolchain compiles a file that pulls this
 * header in transitively (one of the synced data files does), give
 * it an empty translation unit — the SDL_BUILD-only defines below
 * must NOT be visible on Amiga. The SDL build still injects this
 * header via -include in CFLAGS as before. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
/* Amiga build: no platform shim needed. The Amiga m68k-elf toolchain
 * doesn't prefix symbols (ELF on m68k uses bare names), so _ASM_PFX
 * expands to an empty string. The synced data files (swos_globals.c
 * etc.) use this macro inside .globl/.equ inline-asm declarations and
 * need it defined on both ports. */
#define _ASM_PFX ""
#define HLOG(...) do { } while (0)
#else

/* The SDL build is hosted on x86/x64 with little-endian ordering. */
#define SWOS_SDL_BUILD 1

/* WebAssembly/Emscripten target (a flavour of the SDL build). WASM-specific
 * bits read `#ifdef SWOS_WASM_BUILD` rather than raw `__EMSCRIPTEN__`:
 * single-threaded (no SDL timer / audio-pump thread) and the in-match loop
 * yields to the browser via emscripten_sleep in Platform_VBlank (ASYNCIFY).
 * See docs/WASM.md. */
#ifdef __EMSCRIPTEN__
#define SWOS_WASM_BUILD 1
#endif

/* Nintendo Switch target (another flavour of the SDL build — it links the
 * licensee SDL fork, so every SDL call site is unchanged). Makefile.switch
 * passes -DSWOS_SWITCH_BUILD=1; the __NX fallback is what NintendoClang
 * predefines, so a file compiled outside the makefile still agrees.
 *
 * What actually differs is small and all of it is filesystem: the content is
 * mounted read-only at rom:/ and there is no chdir, the per-user save is
 * mounted at save:/ and a write there is not durable until it is committed,
 * and printf reaches nobody — the log has to go through NN_LOG. See
 * docs/SWITCH.md. */
#if defined(SWOS_SWITCH_BUILD) || defined(__NX) || defined(__NINTENDO_SWITCH__)
#undef  SWOS_SWITCH_BUILD
#define SWOS_SWITCH_BUILD 1
#endif

#ifdef SWOS_SWITCH_BUILD
/* Route the game's diagnostics to the target log.
 *
 * printf and fprintf(stderr, ...) reach nobody on a console — nothing connects
 * stdio to the devkit — so a build that fails during startup gives an error
 * code and not one word about what it was doing. NN_LOG does reach
 * `ControlTarget read-log`, and src/platform_switch.cpp wraps it.
 *
 * Done here, as macros, rather than by editing several hundred call sites. The
 * fprintf form tests the stream so that REAL file writes (SWOS.ini, the JSON
 * saves) still go to their files — only the stderr side is diverted. The
 * `fprintf` inside the macro is not re-expanded: a macro is never expanded
 * recursively inside its own expansion, so that arm is the real function. */
#include <stdio.h>
int SwosSwitchLog(const char *fmt, ...);

#define fprintf(stream, ...) \
    (((stream) == stderr) ? SwosSwitchLog(__VA_ARGS__) : fprintf(stream, __VA_ARGS__))
#define printf(...)  SwosSwitchLog(__VA_ARGS__)
#endif

/* On Win32 PE-COFF the C compiler prepends '_' to every C global symbol
 * at the assembler level (so a C `foo` becomes `_foo` in the .o file).
 * ELF (Linux/macOS) does not. Many files alias buffers via inline asm
 * (`.globl name; .equ name, base + offset`) which by default emits the
 * unprefixed name — that links on Linux but not on Win32. Use _ASM_PFX
 * inside __asm__ string literals to prefix the symbol name correctly.
 *
 *   __asm__(".globl " _ASM_PFX "foo\n"
 *           ".equ "  _ASM_PFX "foo, " _ASM_PFX "base + 42\n");
 *
 * GCC's __USER_LABEL_PREFIX__ is "_" on platforms that prefix and empty
 * elsewhere — stringify it via two-step macro expansion. */
#define _SWOS_ASM_STR1(x) #x
#define _SWOS_ASM_STR(x)  _SWOS_ASM_STR1(x)
#define _ASM_PFX          _SWOS_ASM_STR(__USER_LABEL_PREFIX__)

/* Disable all audio output. The SDL audio device is never opened, the
 * RJP1 player is never loaded, and every AudioSDL_* trigger is a no-op.
 * Useful for isolating audio-related issues during testing. */
#define SWOS_AUDIO_DISABLED 0

/* The Amiga code uses its own typedefs (unsigned char uint8_t, etc.).
 * Make sure stdint.h is NOT pulled in indirectly to prevent typedef
 * conflicts. The Amiga code's typedefs match the SysV ABI on x86/x64.
 *
 * The Amiga port doesn't see this file, so this typedef is SDL-only.
 * uintptr_t is needed for the bitness-aware pointer slots
 * (MenuEntry.bg/fg, frontbufferBasePtr, *.fg = (uintptr_t)X writes,
 * etc.) introduced during Phase 6. GCC defines __UINTPTR_TYPE__ to
 * the platform's correct typedef — `unsigned int` on 32-bit ILP32,
 * `unsigned long` on 64-bit LP64, `unsigned long long` on Win64
 * LLP64 — which is exactly what we want. */
#ifndef _UINTPTR_T_DEFINED
typedef __UINTPTR_TYPE__ uintptr_t;
#define _UINTPTR_T_DEFINED 1
#endif

/* Override Amiga section macros — section attributes don't make sense on PC. */
#ifdef SWOS_CHIP_BSS
#undef SWOS_CHIP_BSS
#endif
#ifdef SWOS_FAST_BSS
#undef SWOS_FAST_BSS
#endif
#define SWOS_CHIP_BSS
#define SWOS_FAST_BSS

/* Pre-empt swos_sections.h from re-defining them */
#define SWOS_SECTIONS_H

/* The Amiga code's FixedPoint union is { int16_t i; uint16_t frac } which
 * works on big-endian. On little-endian we need to swap the order so that
 * a raw int32_t value of 0x00010000 still maps to i=1, frac=0. */
#define AMIGA_FIXEDPOINT_LE 1

/* ---- Chip-register base redirect -----------------------------------
 * The Amiga code pokes `*(volatile uint16_t *)(CUSTOM_BASE + OFFSET) = x;`
 * for registers that don't have named struct fields (audio regs, etc.).
 * On the Amiga, CUSTOM_BASE = 0x00DFF000 (memory-mapped hardware).
 * On the SDL host that address is unmapped and any poke segfaults.
 *
 * Redirect CUSTOM_BASE to the address of our stub `custom` Custom
 * struct, which has a pad_0A0[112] array that covers the audio
 * register window (0xA0..0x17F). All offset-relative writes now land
 * harmlessly in that buffer.
 *
 * `struct Custom` itself is fully defined in display.h. We only need
 * a forward declaration here so the extern compiles in translation
 * units that include this header before display.h. */
struct Custom;
extern volatile struct Custom *_custom;
/* uintptr_t, NOT unsigned long: Windows x64 is LLP64, where `unsigned long` is
 * 32 bits, so casting the stub pointer through it TRUNCATED the address and the
 * offset-relative pokes below (`*(uint16_t *)(CUSTOM_BASE + AUD0VOL_OFF)`)
 * dereferenced a 32-bit remnant -- `mov %r8w,0xa8(%eax)` -- and segfaulted on the
 * first one, in StopAllAudioChannels at the first match kick-off. LP64 hosts
 * (Linux, Cygwin, macOS) and the 32-bit Amiga were unaffected, which is why only
 * the MinGW build crashed. */
#define CUSTOM_BASE ((uintptr_t)(_custom))

/* ------------------------------------------------------------------
 * HLOG — harness diagnostic log. Writes "[harness] ..." to stderr
 * (main.c redirects stderr to SWOS.log) ONLY when env SWOS_HARNESS_LOG
 * is set. Lets the gdb test harness "see" the menu/flow it drives by
 * reading SWOS.log directly — independent of gdb, so it works even when
 * the inferior is mid-operation or hung (the last line printed pinpoints
 * where it stuck). Flushes each line so nothing is lost on a freeze. */
#include <stdio.h>
#include <stdlib.h>
#define HLOG(...) do { static int hl_ = -1; \
        if (hl_ < 0) hl_ = getenv("SWOS_HARNESS_LOG") ? 1 : 0; \
        if (hl_) { fprintf(stderr, "[harness] " __VA_ARGS__); fflush(stderr); } \
    } while (0)

#endif /* !__m68k__ */

#endif /* SWOS_PLATFORM_H */
