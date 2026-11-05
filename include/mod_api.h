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
 * its replay menu), shown only while an entry is there (H6). MODPOINT_COMPETITIONS: PRESET
 * COMPETITION's world list, one row above WORLD CUP -- the entry's label when there is one,
 * MORE COMPETITIONS (a list of them) when more; see "competitions from mods" below. A fork's
 * own screen shows any
 * point's list with ModApi_ShowEntries. Labels are shown in capitals; up to 8 a point. */
typedef enum { MODPOINT_MODES, MODPOINT_AFTER_MATCH, MODPOINT_COMPETITIONS, MODPOINT_COUNT } ModPoint;
int32_t ModApi_AddEntry(ModPoint point, const char *slug, const char *label, void (*select)(void));
int32_t ModApi_EntryCount(ModPoint point);
const char *ModApi_EntryLabel(ModPoint point, int32_t index);   /* (as shown: capitals) */
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
/* The menu being shown, from its handlers: an entry's text (kept by the caller: the menu holds
 * the pointer), whether it is shown, and its up / down neighbours (-1 none). */
void    ModApi_MenuText(int32_t entry, const char *text);
void    ModApi_MenuVisible(int32_t entry, int32_t shown);
void    ModApi_MenuNav(int32_t entry, int32_t up, int32_t down);
void    ModApi_MenuSelect(int32_t entry);       /* the cursor on it */

/* The menu being shown, more of it (a list or a teams screen laid out by the mod): the entry the
 * cursor is on (-1 none); an entry's place and size (a -1 in Set leaves that one); all four of
 * its neighbours (-1 none); its background colour (a palette index: the friendly team list's
 * 10 red COMPUTER, 11 purple PLAYER-COACH, 13 blue COACH); whether it is disabled; and drawing it
 * again now (after a change while the menu is up). */
int32_t ModApi_MenuCurrentEntry(void);
void    ModApi_MenuEntryRect(int32_t entry, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
void    ModApi_SetMenuEntryRect(int32_t entry, int32_t x, int32_t y, int32_t w, int32_t h);
void    ModApi_MenuNav4(int32_t entry, int32_t left, int32_t right, int32_t up, int32_t down);
void    ModApi_MenuColour(int32_t entry, int32_t colour);
void    ModApi_MenuDisabled(int32_t entry, int32_t disabled);
void    ModApi_MenuRedraw(int32_t entry);
/* The game's error box with `text` (copied), until the player presses fire. */
void    ModApi_ShowError(const char *text);
/* Every file in a folder (an absolute path: ModApi_ModDir(slug) + "/..."), not the dot ones:
 * `each` is called with its name (stop by returning nonzero); how many there were. */
/* The player's assets folder (where the setup put the game's data from their own copy): a mod
 * that plays files the player brings looks there, as HISTORIC TOURNAMENTS does. */
const char *ModApi_AssetsDir(void);
/* The last match's whole recording (a .SRP, docs/REPLAY_FORMAT_V2.md): its path, for an after-
 * the-match screen (SHARE THIS MATCH, SAVE REPLAY). Written at the final whistle, so read at
 * MODPOINT_AFTER_MATCH or later. 1 when there is one. */
int32_t ModApi_LatestReplay(char *out, int32_t cap);
int32_t ModApi_ListFiles(const char *dir, int32_t (*each)(const char *name, void *ctx), void *ctx);

/* ---- competitions from mods (MODPOINT_COMPETITIONS; docs/MODS.md §20) ----
 * A .DIY a mod brings, played as a preset competition: STARTED, not resumed. The flow: the
 * entry's select shows the mod's screens; one loads a file (ModApi_LoadCompetition), may show
 * its teams (ModApi_CompTeam*) for the player to pick who plays which, and on PLAY calls
 * ModApi_StartCompetition -- the screens are left (each its exit), the chooser closes, and the
 * competition starts as LOAD OLD COMPETITION's does. Backing out of one that was loaded:
 * ModApi_UnloadCompetition (a new load does it too).
 *
 * A file is this game's JSON .DIY, or the PC game's / SWOS 2020's own (diy_import_dos.c).
 * It loads in any world (a .DIY carries its teams) with the player's options and USER tactics
 * kept, every team COMPUTER; SAVE then offers the file's own name. 0 = loaded. */
int32_t ModApi_LoadCompetition(const char *path);
void    ModApi_UnloadCompetition(void);
void    ModApi_StartCompetition(void);
/* A file's competition name without loading it (for a list); 1 when it is a competition. */
int32_t ModApi_CompetitionFileName(const char *path, char *out, int32_t cap);
/* The loaded competition: its name, its teams, and who plays each. */
enum { MODCONTROL_COMPUTER, MODCONTROL_PLAYER_COACH, MODCONTROL_COACH };
const char *ModApi_CompName(void);
int32_t     ModApi_CompTeamCount(void);
const char *ModApi_CompTeamName(int32_t team);          /* (until the next call) */
int32_t     ModApi_CompTeamControls(int32_t team);      /* MODCONTROL_* */
void        ModApi_SetCompTeamControls(int32_t team, int32_t controls);

/* ---- Europe's cups as 36-club league phases in 96/97 (H8; docs/MODS.md §22) ----
 * A mod's three records (the Champions League, the Europa League, the Conference League -- the
 * engine's Champions Cup, Cup Winners' Cup and UEFA Cup slots; game_types.h
 * CompetitionLeaguePhase: six stages, 36 in 2 groups of 18, the play-offs 16, then 16, 8, 4, 2),
 * each country's places from the second season ({team file, CL, EL, UECL}, `count` rows) and the
 * UEFA seeds the qualifying rounds draw on (uefa_coef.h; NULL none). From the mod's init, before
 * the world is known: played only when 96/97 is the world (a world's own cups win), in PRESET
 * (an entry of the mod's, MODPOINT_COMPETITIONS), SEASON and CAREER. The arrays must outlive the
 * run (static). 0 = kept. */
struct WorldUefaSeeds;
int32_t ModApi_SetLeaguePhases(const char *slug, void *cups /* CompetitionLeaguePhase[3] */,
                               const uint8_t (*places)[4], int32_t count, const struct WorldUefaSeeds *seeds);

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
    int16_t  minute;        /* the clock at kick-off, 0..89 (45 plays as 46: half time is its own
                             * flow); from 46 the second half, playingUp then as it is played */
    const char *name;       /* the match's name on the screens ("PRACTICE"), NULL the friendly's */
    uint8_t  chosen;        /* the teams ModApi_ChooseTeams chose (team[] unused): the sides as
                             * the player set them there (a CPU team the CPU, the rest players) */
    /* the rules of a practice (SWOS United's map: G1, G4, G7, G9) */
    int8_t   playingUp;     /* the team attacking the top goal first, 0 / 1; -1 as the game draws */
    int8_t   kickOff;       /* the team kicking off, 0 / 1; -1 as the game draws */
    uint8_t  clockStopped;  /* the clock stays at its start: no half time, no end (EndMatch ends it) */
    uint8_t  noCards;       /* fouls, no bookings */
    uint8_t  noInjuries;    /* tackles injure nobody */
    uint8_t  linesOut[2];   /* per team, MODLINE_* bits: those outfielders are not on the pitch */
    uint32_t sentOff[2];    /* per team, bit n: the player with shirt number n starts sent off (G3) */
    /* petarku's game modes (PR #3, H1-H3) */
    uint8_t  kickOffAlways; /* kickOff's team takes EVERY kick-off: after a goal and each period too */
    uint8_t  hideClock;     /* no clock on the screen (with clockStopped: a practice that never ends) */
    uint8_t  playMatchScreen; /* the friendly's PLAY MATCH screen before kick-off (EDIT TACTICS, VIEW
                             * OPPONENT, the line-up), for the player's sides; 0 straight to the pitch */
    uint8_t  shootoutOrder[2][10]; /* per team, its penalty takers in turn by line-up slot (1..10, the
                             * goalkeeper never), the 11th kick the first again; a 0 anywhere in a
                             * team's list: the game's own order for it (H5) */
    uint8_t  manualKeeper;  /* at a penalty (a shoot-out's or a match's), a player's goalkeeper
                             * defending it dives where his stick is held -- left / right, down-left
                             * / down-right for low -- the moment the kick is struck (G8):
                             * 0 the game's keeper, 1 a neutral stick leaves it to the game,
                             * 2 a neutral stick stands still */
    /* where the match starts (H6, H7: a challenge's moment) */
    uint8_t  pitchNumber;   /* the pitch's own art, 1..6 (with pitchType, an exact pitch); 0 the game's */
    struct {
        uint8_t kind;       /* MODRESTART_*: the first kick-off is this break instead */
        uint8_t team;       /* 0 / 1: the team that takes it */
        uint8_t wing;       /* a corner: 0 the left flag, 1 the right, as the taker attacks */
        int16_t x, y;       /* a free kick: where the foul was (world; MODRESTART_FREE_KICK) */
    } restart;
    struct {
        int8_t  team;       /* 0 / 1 (its team-mate scores: an own goal is not one), -1 an unused row */
        uint8_t shirt;      /* the scorer's shirt number (a man on the pitch at kick-off) */
        uint8_t minute;
        uint8_t penalty;    /* scored from a penalty */
    } scorers[8];           /* goals already in the scorer lists at kick-off -- with score[], which
                             * is the score; a challenge's "2-1, 70th minute" names who scored */
    /* the match's own rules (G9: SWOS 2020's training and challenges) */
    uint8_t  quickRestarts; /* a break restarts at once: no wait for the ball, the CPU never waits */
    uint8_t  straightReds;  /* every foul that is carded is a red (noCards still wins) */
    uint8_t  noTackles;     /* nobody slides in */
    /* what is not drawn (G13, G5): only the picture, the match plays as ever */
    uint8_t  hideHud;       /* MODHUD_* bits */
    uint16_t ghosts[2];     /* per team, bit n: line-up slot n plays but is never drawn (training) */
} ModMatchSetup;
enum { MODRESTART_KICK_OFF, MODRESTART_CORNER, MODRESTART_FREE_KICK, MODRESTART_PENALTY };
enum { MODHUD_RESULT = 1,       /* the score / result banner (a goal's, half and full time) */
       MODHUD_PLAYER_NAME = 2   /* the name of the player on the ball */ };

enum { MODLINE_DEFENCE = 1, MODLINE_MIDFIELD = 2, MODLINE_ATTACK = 4 };   /* (backs and defenders;
                                                                           * wings and midfielders; attackers) */

/* The team packs the mods that are on bring (include/team_packs.h): how many, and each one's
 * number, name and description ("" when its file has none). */
int32_t ModApi_PackCount(void);
int32_t ModApi_PackInfo(int32_t i, int32_t *number, const char **name, const char **description);
/* The game's team list of that pack, two teams picked as on the friendly's team screen (who
 * plays each too); 1 when picked (then ModMatchSetup.chosen plays them), 0 when the player left. */
int32_t ModApi_ChooseFromPack(int32_t number);
/* The friendly's team screen: the player picks two teams, and who plays each (CPU / player).
 * 1 when two were chosen (then ModMatchSetup.chosen plays them), 0 when the player left. */
int32_t ModApi_ChooseTeams(void);
/* A database team by its uuid (docs/TEAM_IDS.md): its id for ModMatchSetup.team, or 0. */
uint16_t ModApi_TeamByUuid(const char *uuid);
/* A team's player by line-up slot (0 the goalkeeper, 1..10 the outfield, 11.. the bench), for a
 * mod's own squad screen (a shoot-out order): from the database by team id, or from a team
 * ModApi_ChooseTeams chose (0 / 1, as the player may have changed it). 1 when there is one. */
typedef struct {
    char    name[24];
    uint8_t position;       /* 0 GK, 1 RB, 2 LB, 3 CD, 4 RW, 5 LW, 6 MF, 7 ST */
    uint8_t shirt;
    uint8_t passing, shooting, heading, tackling, control, speed, finishing;   /* 0..7 each */
} ModTeamPlayer;
int32_t ModApi_TeamPlayer(uint16_t teamId, int32_t slot, ModTeamPlayer *out);
int32_t ModApi_ChosenTeamPlayer(int32_t team, int32_t slot, ModTeamPlayer *out);
/* A random national team's id (its own random numbers: the match's are not taken), or 0. */
uint16_t ModApi_RandomTeam(void);
/* The last chosen teams again (a retry: the ones the last ModMatchSetup.chosen match played,
 * as they were chosen). 1 when there are some. */
int32_t ModApi_ChosenAgain(void);
/* After ModApi_ChooseTeams: whether a chosen team (0 / 1) is a player's (1) or the CPU's (0),
 * and its name. */
int32_t     ModApi_ChosenIsPlayer(int32_t team);
/* A chosen team's uuid (docs/TEAM_IDS.md), for a mod that writes the choice down (a custom
 * challenge): 1 when it has one. */
int32_t     ModApi_ChosenTeamUuid(int32_t team, char *out, int32_t cap);
const char *ModApi_ChosenName(int32_t team);
/* The match's frames a second, by the gameplay style (Amiga 50, DOS 70, XBOX 60): a frame count
 * in seconds. */
int32_t ModApi_FrameRate(void);

/* A setup with the friendly's defaults: CPU v CPU, random pitch, length 0, 2 from 5 subs, no
 * extra time or penalties, replays on, the stadium and the replay menu shown, 0-0 at 0. */
void ModApi_DefaultSetup(ModMatchSetup *setup);    /* (and no scorers: every row's team -1) */

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
/* The referee's final whistle now (G10: a golden or silver goal, a speedrun's target): the match
 * ends as a shoot-out's does -- `winner` (0 / 1; -1 by the score, a level one the top team's)
 * celebrates, the other team mourns -- then the players leave and the result is told as ever. */
void    ModApi_FinalWhistle(int32_t winner);
/* A break now, as the referee would give it (G6: a training drill's corner, a free kick where a
 * drill wants one): MODRESTART_* for `team` (0 / 1) -- a corner's wing as the taker attacks, a
 * free kick's foul at x, y (world). From matchFrame, the ball in play; 0 when given. */
int32_t ModApi_Restart(int32_t kind, int32_t team, int32_t wing, int16_t x, int16_t y);
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
    MODEVENT_TACKLE,            /* team's player won the ball with a good tackle */
} ModEventKind;
/* A goal's arg (MODEVENT_GOAL / OWN_GOAL / PENALTY_GOAL): MODGOAL_HEADER when its scorer headed
 * it in (he headed the ball within the last 75 frames). */
enum { MODGOAL_HEADER = 1 };

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

/* ---- text on the menu frame (from menuDrawn) ----
 * The game's menu fonts: small (the build line's) or big (the entries'). x, y on the menu as its
 * entries are placed (the wide screen's margin added here); colour a palette index. Width in
 * pixels. The current menu's ticker box (its JSON's "ticker_entry", docs/MENUS.md §13), -1 none;
 * and a box of the menu's background painted back (a classic menu is not redrawn each frame, so
 * what moves paints its old place back first). */
void     ModApi_DrawText(int32_t x, int32_t y, int32_t colour, const char *text, int32_t small);
int32_t  ModApi_TextWidth(const char *text, int32_t small);
int32_t  ModApi_MenuTickerEntry(void);
void     ModApi_RestoreMenuBackground(int32_t x, int32_t y, int32_t w, int32_t h);

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

/* ---- switching a tweak: [tweaks] keys (docs/MODS.md §4) ----
 * Switches the game's own tweak `key` ("cpu_cards", ...) on or off for as long as the game
 * runs; never written to SWOS.ini, so the player's own setting is back when the mod is off.
 * Only an offline-only tweak: online, and under the rigs, it is off whatever a mod says. Call
 * it from init (before the menus) and again whenever the mod's own setting for it changes
 * -- matchStart is a good place, as a setting is seen at once. 0 = done, 1 = no such tweak
 * or not a mod's to switch. */
int32_t     ModApi_SetTweak(const char *slug, const char *key, int32_t on);

/* ---- its save data: the save folder's mods/<slug>/<name> ----
 * A mod's records (a challenge's stars, a speedrun's times): named fields -- JSON or text --
 * never memory dumps (CLAUDE.md). Saved as the game saves (durable on the web and the Switch).
 * `name` is a plain file name. Load returns a malloc'd buffer (NUL-terminated past its size;
 * free() it) or NULL. */
int32_t ModApi_SaveFile(const char *slug, const char *name, const void *data, uint32_t size);
void   *ModApi_LoadFile(const char *slug, const char *name, uint32_t *size);

#endif /* SWOS_SDL_BUILD */
#endif /* MOD_API_H */
