/*
 * mod_api.h -- what a built-in mod may ask the game (src/mod_api.c; docs/MODS.md §16). SDL builds.
 *
 * A mod's code (src/mod_<slug>.c) runs at the hooks of include/mod_hooks.h, and asks the game what
 * it needs through these, never through the game's own globals: an engine change then changes
 * this file's insides and no mod. Everything here READS, except the frame, which a mod may draw
 * into at a hook that says so. None of it is match state, and nothing here changes any.
 *
 * Coordinates: "world" is the pitch's pixels (as a sprite's x, y); "screen" the frame's, 0,0 at
 * its top left. A world point is on screen at world - ModApi_Camera().
 */
#ifndef MOD_API_H
#define MOD_API_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"
#include <SDL.h>

struct Sprite;
struct SpriteGraphics;

/* ======================================================================================
 * A built-in mod: one ModDef, listed in src/mods_builtin.c (a fork lists its own in
 * src/fork_mods.inc, which OpenSWOS does not have, so a merge never meets the fork's list).
 * Each hook is optional (NULL) and runs only while the mod is on (OPTIONS -> MODS, [mods]).
 * They are fixed points of the engine, never patches of it; where each runs is said here and
 * in docs/MODS.md §16.
 *
 * Online, the match hooks (matchFrame, matchEvent) are told only frames as first played,
 * never one GGPO re-simulates. A mod whose "changes" is "gameplay" or "data" plays online only
 * against a player with the same mods -- uuid, version and files (Mods_OnlineSignature, the
 * handshake, §8.6); "cosmetic", "modes" and "career" mods never matter there.
 * ====================================================================================== */
typedef struct ModMatchEvent ModMatchEvent;
typedef struct ModMatchResult ModMatchResult;

typedef struct ModDef {
    const char *slug;                   /* its folder: bin/mods/<slug> (canonical) */

    /* At start-up, after the settings and the mod list, before the first menu: register its
     * entries (ModApi_AddEntry) and its menus' handlers (ModApi_RegisterHandlers) here. */
    void (*init)(void);

    /* -- drawing -- */
    void (*frameDrawn)(SDL_Renderer *renderer);     /* the finished frame, before the swap */
    void (*pitchDrawn)(void);                       /* a match's pitch in the frame, no sprites yet */
    void (*playerSetsRaw)(void);                    /* the match's raw player sprites (sets 227..) */
    void (*playerDrawn)(const struct Sprite *sprite, const struct SpriteGraphics *gfx,
                        int16_t x, int16_t y);      /* a player sprite just drawn at screen x, y */
    void (*replayFrameBegin)(int32_t first, int32_t fresh);         /* a replay's pitch drawn */
    void (*replaySprite)(uint16_t image, int16_t left, int16_t top); /* each recorded sprite */
    void (*menuDrawn)(void);            /* a menu frame drawn (ModApi_Frame), before it is shown */

    /* -- the match -- */
    void (*matchStart)(void);           /* kick-off: the teams on the pitch, the setup applied */
    void (*matchFrame)(void);           /* each frame played, after the clock moved */
    void (*matchEvent)(const ModMatchEvent *event);
    void (*matchEnded)(const ModMatchResult *result);   /* any match's final whistle */
} ModDef;

/* ---- entries: a mod's rows in the game's screens (H1) ----
 * MODPOINT_MODES: the MODES screen, which the main menu's MODES button opens (shown only while
 * an entry is there). MODPOINT_AFTER_MATCH: a screen after a friendly's final whistle (and
 * its replay menu), shown only while an entry is there (H6). A fork's own screen shows any
 * point's list with ModApi_ShowEntries. Labels are shown in capitals; up to 8 a point. */
typedef enum { MODPOINT_MODES, MODPOINT_AFTER_MATCH, MODPOINT_COUNT } ModPoint;
int32_t ModApi_AddEntry(ModPoint point, const char *slug, const char *label, void (*select)(void));
int32_t ModApi_EntryCount(ModPoint point);
void    ModApi_ShowEntries(ModPoint point);
/* One mod's entry, from a screen of a fork's own (a COMPETITIONS screen's SPEEDRUNS row): its
 * index at the point (the first with that slug; -1 none, the mod off), and running it. */
int32_t ModApi_FindEntry(ModPoint point, const char *slug);
int32_t ModApi_RunEntry(ModPoint point, int32_t index);     /* 0 when it ran */
/* The main menu is the one being shown (a mod's replacement of it too: by the menu, not its
 * onInit). */
int32_t ModApi_IsMainMenu(void);

/* ---- a mod's own menus' handlers (H10) ----
 * A mod's menus (its JSON, data_json/menus/<slug>/..., through the overlay) name their
 * handlers "<slug>.<name>"; these are registered under that name, so they resolve only while
 * the mod is on (its init registers them). A mod replacing one of the game's menus names its
 * own onInit, never the engine's. MenuJson_Load(<path>) + ShowMenu shows one. */
typedef struct {
    const char *name;                   /* without the "<slug>." */
    void (*fn)(void);
} ModHandler;
int32_t ModApi_RegisterHandlers(const char *slug, const ModHandler *handlers, int32_t count);
int32_t ModApi_ShowMenu(const char *menuPath);  /* MenuJson_Load + ShowMenu; 0 when shown */

/* ---- a match a mod plays (H2, H4) ---- */
typedef enum { MODSIDE_CPU, MODSIDE_PLAYER1, MODSIDE_PLAYER2 } ModSide;

typedef struct {
    uint16_t team[2];       /* a team's id: its file (country) << 8 | ordinal -- team 0 at the top */
    uint8_t  side[2];       /* ModSide */
    int8_t   pitchType;     /* 0..6 as ModApi_PitchType, -1 random */
    uint8_t  length;        /* the clock's speed, the friendly's game length: 0..3 */
    uint8_t  extraTime;     /* a draw at full time: extra time ... */
    uint8_t  penalties;     /* ... and/or penalties */
    uint8_t  shootoutOnly;  /* straight to a penalty shoot-out */
    uint8_t  subsMin, subsMax;
    uint8_t  autoReplays;   /* the goals' instant replays */
    uint8_t  stadium;       /* show the stadium screen before (0: straight to the pitch) */
    uint8_t  replayMenu;    /* show REPLAY / EXIT after (0: straight back to the mod) */
    int8_t   score[2];      /* the score at kick-off */
    int16_t  minute;        /* the clock at kick-off, 0..44 (the first half) */
    const char *name;       /* the match's name on the screens ("PRACTICE"), NULL the friendly's */
} ModMatchSetup;

/* A setup with the friendly's defaults: CPU v CPU, random pitch, length 0, 2 from 5 subs, no
 * extra time or penalties, replays on, the stadium and the replay menu shown, 0-0 at 0. */
void ModApi_DefaultSetup(ModMatchSetup *setup);

struct ModMatchResult {
    int16_t  goals[2], penaltyGoals[2];
    int8_t   winner;        /* 0 / 1, -1 a draw */
    uint8_t  quit;          /* left the match (the game quitting, or a team's ESC) */
    uint8_t  endedByMod;    /* ModApi_EndMatch ended it */
    uint8_t  modMatch;      /* it was ModApi_PlayMatch's */
    int16_t  minute;        /* the clock at the end */
    uint32_t frames;        /* frames played from kick-off */
};

/* Plays it now, and returns when it is over: 0 and the result, -1 when it could not start (a
 * team not found). Called from a mod's handler (a menu's onSelect, an entry): what comes after
 * is the mod's -- show its retry screen, play again, return to its menu. */
int32_t ModApi_PlayMatch(const char *slug, const ModMatchSetup *setup, ModMatchResult *result);
int32_t ModApi_ModMatch(const char *slug);      /* this mod's match is the one being played */
void    ModApi_EndMatch(void);                  /* the final whistle now, on the score as it is */
void    ModApi_StartShootout(void);             /* to penalties now */
void    ModApi_Score(int16_t goals[2]);         /* the score now */
uint32_t ModApi_MatchFrames(void);              /* frames played from kick-off */

/* ---- match events (H3): ModDef.matchEvent ---- */
typedef enum {
    MODEVENT_GOAL,              /* team scored: player its scorer (own goal: the other team's man) */
    MODEVENT_OWN_GOAL,
    MODEVENT_PENALTY_GOAL,
    MODEVENT_SHOOTOUT_KICK,     /* a shoot-out kick taken: arg 1 scored, 0 missed */
    MODEVENT_FOUL,              /* team's player fouled */
    MODEVENT_YELLOW_CARD,
    MODEVENT_SECOND_YELLOW,     /* (and so off) */
    MODEVENT_RED_CARD,
    MODEVENT_INJURY,
    MODEVENT_HALF_TIME,
    MODEVENT_SECOND_HALF,
    MODEVENT_FULL_TIME,         /* the 90 minutes up (a draw may go on) */
    MODEVENT_EXTRA_TIME,
    MODEVENT_SHOOTOUT,          /* the shoot-out starts */
    MODEVENT_FINAL_WHISTLE,     /* the match is over */
} ModEventKind;

struct ModMatchEvent {
    uint8_t  kind;              /* ModEventKind */
    int8_t   team;              /* 0 / 1 (top / bottom), -1 none */
    int8_t   player;            /* the player's index in his team (0 the goalkeeper), -1 none */
    int8_t   arg;
    int16_t  minute;
    uint32_t frame;             /* ModApi_MatchFrames() */
};

/* ---- when ---- */
int32_t  ModApi_InMenus(void);          /* the front end (no match, no replay) */
int32_t  ModApi_InMatch(void);          /* a match is being played (its frame loop runs) */
int32_t  ModApi_InReplay(void);         /* a replay is being shown: the in-match one or VIEW's */
uint32_t ModApi_Ticks(void);            /* real time, ms: for what a mod animates by itself */

/* ---- the match ---- */
int32_t  ModApi_PitchType(void);        /* 0 frozen, 1 muddy, 2 wet, 3 soft, 4 normal, 5 dry, 6 hard */
int32_t  ModApi_MatchMinute(void);      /* the clock's minute, 0.. (120 in extra time) */

/* The players as they are now (in a match, not a replay): up to 22, team 0's first, each team's
 * goalkeeper its first. */
typedef struct {
    int16_t x, y, z;                    /* world: his feet; z his height off the ground */
    int16_t direction;                  /* 0 up the screen, clockwise to 7 */
    uint8_t team;                       /* 0, 1 */
    uint8_t index;                      /* 0 the goalkeeper .. 10 */
    uint8_t onPitch;                    /* drawn this match (not sent off and walked away) */
    uint8_t face;                       /* 0 white, 1 ginger, 2 black, 3 black with blond hair */
    const char *name;                   /* his full name */
    const char *club;                   /* his team's name */
} ModPlayer;
int32_t  ModApi_Players(ModPlayer *out, int32_t max);

/* A recorded (replay) sprite, given as the replay hooks give it (its top left in the world):
 * whether it is an outfield player's frame, and where his feet are. */
int32_t  ModApi_ReplayPlayerFeet(uint16_t image, int16_t left, int16_t top, int16_t *x, int16_t *y);

/* ---- the screen ---- */
void     ModApi_Camera(int16_t *x, int16_t *y);     /* the world point at screen 0,0 */
void     ModApi_ScreenSize(int32_t *w, int32_t *h);
/* The 8bpp frame being drawn and its row stride; only at a hook that says it may be drawn into. */
uint8_t *ModApi_Frame(int32_t *stride);

/* ---- colours ---- */
void     ModApi_PaletteColour(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b);  /* as last shown */
uint8_t  ModApi_NearestColour(uint8_t r, uint8_t g, uint8_t b);                    /* (not entry 0) */

/* ---- the mod's own files ---- */
/* The folder of the mod of that slug (its mod.json beside), or NULL when it is not on. */
const char *ModApi_ModDir(const char *slug);

/* ---- its settings: SWOS.ini [mod.<slug>] ----
 * Declared in mod.json -- "settings": [{ "key", "label", "choices": ["a", "b"], "default": "a" }]
 * (no choices: off / on) -- and shown on the mod's page (OPTIONS -> MODS, fire on its name),
 * where fire cycles one; read here at any time (a change is seen at once). */
const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
int32_t     ModApi_SettingInt(const char *slug, const char *key, int32_t fallback);  /* (on/true 1, off/false 0) */
int32_t     ModApi_SetSetting(const char *slug, const char *key, const char *value);

/* ---- its save data: the save folder's mods/<slug>/<name> ----
 * A mod's records (a challenge's stars, a speedrun's times): named fields -- JSON or text --
 * never memory dumps (CLAUDE.md). Saved as the game saves (durable on the web and the Switch).
 * `name` is a plain file name. Load returns a malloc'd buffer (NUL-terminated past its size;
 * free() it) or NULL. */
int32_t ModApi_SaveFile(const char *slug, const char *name, const void *data, uint32_t size);
void   *ModApi_LoadFile(const char *slug, const char *name, uint32_t *size);

#endif /* SWOS_SDL_BUILD */
#endif /* MOD_API_H */
