/*
 * build_info.c -- the build stamp. Interface and rationale in include/build_info.h.
 *
 * The commit strings come from include/swos_build.h, which
 * scripts/gen_build_info.sh regenerates at the start of every build. Keeping
 * them in this one small file is deliberate: a new commit then recompiles a
 * single object and relinks, instead of rebuilding everything that would have
 * included the header.
 *
 * The PLATFORM is decided here too, and this is the only place that decides it
 * -- platform_sdl.c used to detect the architecture a second time for its
 * window title, and two detectors would eventually disagree. It matters as much
 * as the commit: the first macOS<->Windows netplay match desynced because one
 * platform's compiler resolved an undefined behaviour differently, so "which
 * build" without "on what" only answers half the question.
 *
 * Compiles for m68k as well, so: string literals, a hand-rolled uppercase, and
 * no libc.
 */
#include "build_info.h"
#include "project_identity.h"
#include "swos_build.h"

/* ---- which platform is this binary for? ----------------------------------
 * The Amiga target is the one that defines no OS macro at all (-nostdlib m68k),
 * so it is recognised by the absence of SWOS_SDL_BUILD rather than by a macro
 * of its own. */
#if !defined(SWOS_SDL_BUILD)
#  if defined(AGA)
#    define SWOS_PLATFORM "amiga 68k aga"
#  else
#    define SWOS_PLATFORM "amiga 68k"
#  endif
#elif defined(SWOS_SWITCH_BUILD)
#  define SWOS_PLATFORM "switch"
#elif defined(SWOS_WASM_BUILD) || defined(__EMSCRIPTEN__)
#  define SWOS_PLATFORM "web"
#else
#  if   defined(__CYGWIN__) || defined(_WIN32)
#    define SWOS_PLATFORM_OS "windows"
#  elif defined(__APPLE__)
#    define SWOS_PLATFORM_OS "macos"
#  elif defined(__linux__)
#    define SWOS_PLATFORM_OS "linux"
#  else
#    define SWOS_PLATFORM_OS "unknown"
#  endif
#  if   defined(__x86_64__)
#    define SWOS_PLATFORM_ARCH "x86-64"
#  elif defined(__i386__)
#    define SWOS_PLATFORM_ARCH "x86"
#  elif defined(__aarch64__)
#    define SWOS_PLATFORM_ARCH "arm64"
#  elif defined(__arm__)
#    define SWOS_PLATFORM_ARCH "armhf"   /* the only 32-bit ARM target shipped */
#  elif defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
#    define SWOS_PLATFORM_ARCH "64-bit"
#  else
#    define SWOS_PLATFORM_ARCH "32-bit"
#  endif
#  define SWOS_PLATFORM SWOS_PLATFORM_OS " " SWOS_PLATFORM_ARCH
#endif

const char *Build_Id(void)
{
    return SWOS_BUILD_ID;
}

const char *Build_Date(void)
{
    return SWOS_BUILD_DATE;
}

const char *Build_Platform(void)
{
    return SWOS_PLATFORM;
}

/* A tester's build can switch the testers' rule on (no bookings, no injuries, offline:
 * include/test_rules.h) and a report from one is read differently -- so the version
 * line says which it is. Whether the rule is ON is in SWOS.log, where it is set. */
#ifdef SWOS_TEST_RULES
#define SWOS_BUILD_RULES ", tester build"
#else
#define SWOS_BUILD_RULES ""
#endif

const char *Build_Version(void)
{
    return PROJECT_NAME " " SWOS_BUILD_ID " (" SWOS_BUILD_DATE ", " SWOS_PLATFORM SWOS_BUILD_RULES ")";
}

/* ---- the on-screen form ---------------------------------------------------
 * Built once into a static buffer rather than as a literal, because only the
 * commit id has an uppercase twin from the generator; the platform is composed
 * here. The in-game font has no lowercase glyphs -- a lowercase letter draws as
 * a blank -- so everything is uppercased on the way in. */
static char s_screenText[96];

static char *appendUpper(char *dst, const char *end, const char *src)
{
    while (*src && dst < end) {
        char c = *src++;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        *dst++ = c;
    }
    return dst;
}

const char *Build_ScreenText(void)
{
    if (s_screenText[0] == '\0') {
        char *p = s_screenText;
        char *end = s_screenText + sizeof s_screenText - 1;
        p = appendUpper(p, end, "BUILD ");
        p = appendUpper(p, end, SWOS_BUILD_ID_UC);
        p = appendUpper(p, end, "  ");
        p = appendUpper(p, end, SWOS_BUILD_DATE);
        p = appendUpper(p, end, "  ");
        p = appendUpper(p, end, SWOS_PLATFORM);
        *p = '\0';
    }
    return s_screenText;
}
