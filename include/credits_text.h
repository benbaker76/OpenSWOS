/*
 * credits_text.h -- the words on the CREDITS screen, shared by both of them: the SDL
 * builds' (src/credits_menu.c) and the Amiga AGA one (menus.c CreditsAgaDraw). One list,
 * so a new tester is added once.
 *
 * Read top to bottom, one line each. Two markers take the place of a line:
 *   CREDITS_GAP     a part of a line's height of space between paragraphs;
 *   CREDITS_PHOTOS  where the developer photos go, with their names under them.
 * The Amiga's photos are part of its backdrop (scripts/make_credits_aga.py), at a fixed
 * place, and the lines after CREDITS_PHOTOS start under them there whatever the lines
 * before them do -- add a line above the photos and the backdrop must be rebuilt with
 * them lower (make_credits_aga.py PHOTO_Y).
 *
 * The menu font has capitals, digits and . , : - ! only.
 */
#ifndef CREDITS_TEXT_H
#define CREDITS_TEXT_H

#define CREDITS_GAP     ""
#define CREDITS_PHOTOS  ((const char *)0)

static const char *const k_creditsLines[] = {
    "OPENSWOS IS SENSIBLE WORLD OF SOCCER REBUILT IN C FROM",
    "THE ORIGINAL AMIGA GAME. REVERSE ENGINEERING BEGAN IN",
    "AUGUST 2025, BUILDING ON THE DOS WORK OF ZLATKO KARAKAS.",
    "THE C PORT BEGAN IN FEBRUARY 2026, DECOMPILED WITH IDA,",
    "GHIDRA AND SCRIPTS, WITH GPT AND LATER CLAUDE HELPING.",
    CREDITS_GAP,
    "A BIG THANKS TO PLAYAVELI FOR HELP TESTING, AND TO AMINTA",
    "FOR WORLD 2026-27 AND ITS COMPETITION RULES.",
    CREDITS_GAP,
    CREDITS_PHOTOS,
    CREDITS_GAP,
    "BETA TESTERS: PLAYAVELI, BRUMMIE, CROM1, ELMICHAJ,",
    "ELSHEIMY, EMPI, GATIFUN, MLADEN, MOZG, PETARKU, PREDICADOR,",
    "REBEL, REDHAIR, STARWINDZ, THD79, UMTYTM, YASHALKOK",
    CREDITS_GAP,
    "DONATORS: BENBAKER76, PLAYAVELI, STARWINDZ, PETARKU,",
    "GATIFUN, BURELLAPAOLO, AZ0MAN0, CARTOPLOTUS68, JOSON, ALIKO",
#ifdef SWOS_SDL_BUILD
    "THE WEATHER MOD: PITCH-WEATHER BY C4PPY77.",     /* (an SDL mod: not on the Amiga's screen) */
#endif
};

enum {
    CREDITS_LINE_PITCH = 9,     /* px from one line to the next */
    CREDITS_GAP_PITCH  = 5,     /* px a CREDITS_GAP adds */
    CREDITS_NAME_DROP  = 4,     /* px from the bottom of a photo to its name */
    CREDITS_NAME_PITCH = 8,     /* px a name takes, and a staggered one drops */
};

/* The photos in bin/data/credits_icons.bmp, in the order they are in the strip
 * (scripts/make_credits_icons.py). Each screen picks its own from it. */
enum {
    CREDITS_STRIP_PLAYAVELI,
    CREDITS_STRIP_BENBAKER76,
    CREDITS_STRIP_STARWINDZ,
    CREDITS_STRIP_ZKZ,
    CREDITS_STRIP_THD79,
    CREDITS_STRIP_REDHAIR,
    CREDITS_STRIP_ELMICHAJ,
    CREDITS_STRIP_COUNT
};

#endif
