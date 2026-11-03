/*
 * game_options.h -- the settings OpenSWOS Setup offers before the game is first started
 * (its wizard's GAME OPTIONS screen, tools/swosdata/app). Only the ones worth choosing before
 * playing; everything else is the game's OPTIONS.
 *
 * The GAME is the authority on SWOS.ini (src/swos_settings.c reads and writes it): this table
 * only names keys and values it already knows, and the setup only adds or changes those lines,
 * leaving the rest of the file as it is. --game-options-selftest (src/swos_settings.c) writes
 * every value here through the game's own reader and writer and fails when one does not come
 * back: a key or value renamed in the game and not here.
 *
 * Plain C, no includes but stdint: the setup (C++) includes it too.
 */
#ifndef GAME_OPTIONS_H
#define GAME_OPTIONS_H

#include <stdint.h>

/* Where an option means something. */
enum {
    GAME_OPTION_COMPUTER = 1,   /* the desktop game */
    GAME_OPTION_PHONE    = 2,   /* the phone / tablet game */
    GAME_OPTION_HANDHELD = 4,   /* the PortMaster port on a handheld (its own SWOS.ini on the card) */
};

/* What it needs, or it is not shown (the setup knows what was installed). */
enum {
    GAME_OPTION_NEEDS_NOTHING = 0,
    GAME_OPTION_NEEDS_WORLD   = 1,  /* World 2026-27 */
};

typedef struct {
    const char *value;          /* in SWOS.ini */
    const char *label;          /* on the setup's button */
    uint8_t     needs;          /* GAME_OPTION_VALUE_*: this value only with that data made */
} GameOptionValue;

/* Values that need a part of the game's data: shown only when the setup has made it. */
enum {
    GAME_OPTION_VALUE_ANY    = 0,
    GAME_OPTION_VALUE_PC     = 1,   /* the PC game's files (its sound, its graphics) */
    GAME_OPTION_VALUE_2020   = 2,   /* SWOS 2020's pitches */
    GAME_OPTION_VALUE_JAGUAR = 3,   /* the Jaguar's players */
};

typedef struct {
    const char            *section, *key;
    const char            *label;       /* the row's name */
    const char            *about;       /* one line under it */
    uint8_t                where;       /* GAME_OPTION_COMPUTER | GAME_OPTION_PHONE */
    uint8_t                needs;       /* GAME_OPTION_NEEDS_* */
    const char            *def;         /* the game's own default, as a value above */
    const GameOptionValue *values;
    int32_t                count;
} GameOption;

static const GameOptionValue kGoWorld[]    = { { "9697", "96/97", 0 }, { "2627", "2026-27", 0 } };
static const GameOptionValue kGoGameplay[] = { { "amiga", "Amiga", 0 }, { "dos", "PC (DOS)", 0 }, { "xbox", "Xbox", 0 } };
static const GameOptionValue kGoGraphics[] = { { "amiga", "Amiga", 0 }, { "dos", "PC (DOS)", GAME_OPTION_VALUE_PC },
                                               { "2020", "SWOS 2020", GAME_OPTION_VALUE_2020 } };
static const GameOptionValue kGoSprites[]  = { { "amiga", "Amiga", 0 }, { "jaguar", "Atari Jaguar", GAME_OPTION_VALUE_JAGUAR } };
static const GameOptionValue kGoSound[]    = { { "amiga", "Amiga", 0 }, { "pc", "PC", GAME_OPTION_VALUE_PC } };
static const GameOptionValue kGoMusic[]    = { { "amiga", "Amiga", 0 }, { "pc", "PC", GAME_OPTION_VALUE_PC } };
static const GameOptionValue kGoFull[]     = { { "off", "In a window", 0 }, { "desktop", "Full screen", 0 } };
static const GameOptionValue kGoWide[]     = { { "true", "Widescreen", 0 }, { "false", "4:3, as the original", 0 } };
static const GameOptionValue kGoTouch[]    = { { "auto", "Auto", 0 }, { "always", "Always", 0 }, { "never", "Never", 0 } };
static const GameOptionValue kGoLanguage[] = { { "en", "English", 0 }, { "de", "Deutsch", 0 }, { "fr", "Français", 0 },
                                               { "es", "Español", 0 }, { "it", "Italiano", 0 }, { "nl", "Nederlands", 0 } };

#define GAME_OPTION_COUNT(a) ((int32_t)(sizeof(a) / sizeof((a)[0])))
#define GAME_OPTION_BOTH (GAME_OPTION_COMPUTER | GAME_OPTION_PHONE)
#define GAME_OPTION_ALL  (GAME_OPTION_COMPUTER | GAME_OPTION_PHONE | GAME_OPTION_HANDHELD)

static const GameOption kGameOptions[] = {
    { "general",  "world",          "World",       "The football the game plays: 96/97, or today's",
      GAME_OPTION_ALL, GAME_OPTION_NEEDS_WORLD, "9697", kGoWorld, GAME_OPTION_COUNT(kGoWorld) },
    { "gameplay", "style",          "Gameplay",    "How a match plays: the Amiga's 50 fps, the PC's 70, or SWOS 2020's Xbox mode",
      GAME_OPTION_ALL, 0, "amiga", kGoGameplay, GAME_OPTION_COUNT(kGoGameplay) },
    { "graphics", "style",          "Graphics",    "What the pitch, the scoreboard and the menus look like",
      GAME_OPTION_ALL, 0, "amiga", kGoGraphics, GAME_OPTION_COUNT(kGoGraphics) },
    { "graphics", "sprites",        "Players",     "The players' look",
      GAME_OPTION_ALL, 0, "amiga", kGoSprites, GAME_OPTION_COUNT(kGoSprites) },
    { "audio",    "style",          "Sound",       "The sounds of a match: the Amiga's, or the PC game's crowd and chants",
      GAME_OPTION_ALL, 0, "amiga", kGoSound, GAME_OPTION_COUNT(kGoSound) },
    { "audio",    "music",          "Music",       "The Amiga's music, or the PC game's",
      GAME_OPTION_ALL, 0, "amiga", kGoMusic, GAME_OPTION_COUNT(kGoMusic) },
    { "video",    "fullscreen",     "Screen",      "Full screen, or in a window",
      GAME_OPTION_COMPUTER, 0, "off", kGoFull, GAME_OPTION_COUNT(kGoFull) },
    { "video",    "widescreen",     "Shape",       "Widescreen shows more of the pitch; 4:3 draws it as the original did",
      GAME_OPTION_COMPUTER, 0, "true", kGoWide, GAME_OPTION_COUNT(kGoWide) },
    { "input",    "touch_controls", "On-screen controls", "The stick and buttons drawn on a phone's screen: Never, for a handheld with its own",
      GAME_OPTION_PHONE, 0, "auto", kGoTouch, GAME_OPTION_COUNT(kGoTouch) },
    { "general",  "language",       "Language",    "The game's menus",
      GAME_OPTION_ALL, 0, "en", kGoLanguage, GAME_OPTION_COUNT(kGoLanguage) },
};

#endif
