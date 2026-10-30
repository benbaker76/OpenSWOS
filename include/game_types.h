/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* game_types.h — Shared game struct definitions for SWOS C port
 *
 * Adapted from data/SWOS2_IDA.h (IDA Pro export).
 * All IDA annotations stripped, types mapped to stdint-style typedefs.
 * Packing via __attribute__((packed, aligned(2))) to match 68k memory layout.
 */
#ifndef GAME_TYPES_H
#define GAME_TYPES_H

/* All fixed-width integer types (uint8_t..uint64_t, intN_t, uintptr_t,
 * size_t) come from swos_types.h. On hosted builds it pulls in
 * <stdint.h>/<stddef.h>; on Amiga it provides manual typedefs. */
#include "swos_types.h"
#include "endian_helpers.h"   /* PACK_BE_U16, used by TEAMID_PACK below */

/* ---- Forward declarations ---- */
typedef struct Sprite Sprite;
typedef struct AnimTable AnimTable;
typedef struct TeamGeneralInfo TeamGeneralInfo;
typedef struct TeamStatisticsData TeamStatisticsData;
typedef struct TeamGame TeamGame;
typedef struct PlayerInfo PlayerInfo;
typedef struct GoalieSkill GoalieSkill;
typedef struct SpriteGraphics SpriteGraphics;
typedef struct GoalInfo GoalInfo;
typedef struct ScorerInfo ScorerInfo;

/* ==================================================================
 * FixedPoint — 16.16 fixed-point (M68k big-endian: high=integer, low=frac)
 * ================================================================== */
typedef union {
    int32_t raw;                    /* full 32-bit value */
    struct {
#if defined(AMIGA_FIXEDPOINT_LE) && AMIGA_FIXEDPOINT_LE
        uint16_t frac;              /* fractional part (low addr on LE) */
        int16_t  i;                 /* integer part (high addr on LE) */
#else
        int16_t  i;                 /* integer part (high word, BE) */
        uint16_t frac;              /* fractional part (low word, BE) */
#endif
    } s;
} FixedPoint;

typedef char _fixedpoint_size_check[(sizeof(FixedPoint) == 4) ? 1 : -1];

/* ==================================================================
 * Sprite — 110 bytes, pack(2)
 * ================================================================== */
struct __attribute__((packed, aligned(2))) Sprite {
    int16_t    teamNumber;          /* 0 */
    int16_t    playerOrdinal;       /* 2 */
    int16_t    frameOffset;         /* 4 */
    AnimTable *animationTable;       /* 6: pointer to AnimTable */
    int16_t    startingDirection;   /* 10 */
    uint8_t    playerState;         /* 12 */
    int8_t     playerDownTimer;     /* 13 */
    int16_t    field_E;             /* 14 */
    int16_t    field_10;            /* 16 */
    int16_t   *frameIndicesTable;   /* 18: ptr to int16_t[] */
    int16_t    frameIndex;          /* 22 */
    int16_t    frameDelay;          /* 24 */
    int16_t    cycleFramesTimer;    /* 26 */
    int16_t    frameSwitchCounter;  /* 28 */
    FixedPoint x;                   /* 30 */
    FixedPoint y;                   /* 34 */
    FixedPoint z;                   /* 38 */
    int16_t    direction;           /* 42 */
    int16_t    speed;               /* 44 */
    FixedPoint deltaX;              /* 46 */
    FixedPoint deltaY;              /* 50 */
    FixedPoint deltaZ;              /* 54 */
    int16_t    destX;               /* 58 */
    int16_t    destY;               /* 60 */
    uint8_t    field_3E[6];         /* 62 */
    int16_t    visible;             /* 68 */
    int16_t    imageIndex;          /* 70 */
    int16_t    saveSprite;          /* 72 */
    int32_t    ballDistance;        /* 74 */
    int16_t    field_4E;            /* 78 */
    int16_t    field_50;            /* 80 */
    int16_t    fullDirection;       /* 82 */
    int16_t    onScreen;            /* 84 */
    int16_t    field_56;            /* 86 */
    int16_t    field_58;            /* 88 */
    int16_t    field_5A;            /* 90 */
    int16_t    playerDirection;     /* 92 */
    int16_t    isMoving;            /* 94 */
    int16_t    tackleState;         /* 96 */
    int16_t    heading;             /* 98 */
    int16_t    destReachedState;    /* 100 */
    int16_t    cards;               /* 102 */
    int16_t    injuryLevel;         /* 104 */
    int16_t    tacklingTimer;       /* 106 */
    int16_t    sentAway;            /* 108 */
};

/* Sprite has 2 pointer fields (animationTable, frameIndicesTable);
 * each grows from 4→8 bytes on 64-bit. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _sprite_size_check[(sizeof(Sprite) == 110) ? 1 : -1];
#else
typedef char _sprite_size_check[(sizeof(Sprite) == 118) ? 1 : -1];
#endif

/* Never a literal: 110 on the Amiga and 32-bit builds, 118 on 64-bit. Two swaps in
 * bench_subs.c copied "110 bytes" and left the last four fields behind (ReportBot 0037). */
#define SPRITE_SIZE ((int32_t)sizeof(Sprite))

/* ==================================================================
 * AnimTable — animation frame table variants
 * Standard has 32 frame pointers, variants have fewer.
 * ================================================================== */
struct __attribute__((packed, aligned(2))) AnimTable {
    int16_t  frameDelay;
    int16_t *animFrames[32];
};

typedef struct __attribute__((packed, aligned(2))) {
    int16_t  frameDelay;
    int16_t *animFrames[20];
} AnimTable_20;

typedef struct __attribute__((packed, aligned(2))) {
    int16_t  frameDelay;
    int16_t *animFrames[12];
} AnimTable_12;

typedef struct __attribute__((packed, aligned(2))) {
    int16_t  frameDelay;
    int16_t *animFrames[8];
} AnimTable_8;

/* Compile-time size checks for AnimTable variants */
/* AnimTable* have `int16_t *animFrames[N]` where N varies per variant;
 * each pointer grows from 4→8 bytes on 64-bit. 32-bit/64-bit sizes:
 *   AnimTable    : N=32 → 2 + 32*4 = 130 / 2 + 32*8 = 258
 *   AnimTable_20 : N=20 → 2 + 20*4 = 82  / 2 + 20*8 = 162
 *   AnimTable_12 : N=12 → 2 + 12*4 = 50  / 2 + 12*8 = 98
 *   AnimTable_8  : N=8  → 2 +  8*4 = 34  / 2 +  8*8 = 66 */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _animtable_size_check[(sizeof(AnimTable) == 130) ? 1 : -1];
typedef char _animtable20_size_check[(sizeof(AnimTable_20) == 82) ? 1 : -1];
typedef char _animtable12_size_check[(sizeof(AnimTable_12) == 50) ? 1 : -1];
typedef char _animtable8_size_check[(sizeof(AnimTable_8) == 34) ? 1 : -1];
#else
typedef char _animtable_size_check[(sizeof(AnimTable) == 258) ? 1 : -1];
typedef char _animtable20_size_check[(sizeof(AnimTable_20) == 162) ? 1 : -1];
typedef char _animtable12_size_check[(sizeof(AnimTable_12) == 98) ? 1 : -1];
typedef char _animtable8_size_check[(sizeof(AnimTable_8) == 66) ? 1 : -1];
#endif

/* ==================================================================
 * TeamGeneralInfo — 144 bytes, pack(1)
 * ================================================================== */
struct __attribute__((packed, aligned(2))) TeamGeneralInfo {
    TeamGeneralInfo   *opponentsTeam;              /* 0 */
    int16_t            playerNumber;               /* 4 */
    int16_t            playerCoachNumber;           /* 6 */
    int16_t            isPlCoach;                   /* 8 */
    TeamGame          *inGameTeamPtr;               /* 10 */
    TeamStatisticsData *teamStatsPtr;               /* 14 */
    int16_t            teamNumber;                  /* 18 */
    Sprite           **spritesTable;                /* 20 */
    GoalieSkill       *shotChanceTable;             /* 24 */
    int16_t            tactics;                     /* 28 */
    int16_t            updatePlayerIndex;           /* 30 */
    Sprite            *controlledPlayer;            /* 32 */
    Sprite            *passToPlayerPtr;             /* 36 */
    int16_t            playerHasBall;               /* 40 */
    int16_t            allowedDirections;           /* 42 */
    int16_t            currentAllowedDirection;     /* 44 */
    int16_t            direction;                   /* 46 */
    int8_t             quickFire;                   /* 48 */
    int8_t             normalFire;                  /* 49 */
    int8_t             firePressed;                 /* 50 */
    int8_t             fireThisFrame;               /* 51 */
    int16_t            headerOrTackle;              /* 52 */
    int16_t            fireCounter;                 /* 54 */
    int16_t            controlledPlDirection;       /* 56 */
    int16_t            shooting;                    /* 58 */
    int8_t             field_3C;                    /* 60 */
    int8_t             plVeryCloseToBall;           /* 61 */
    int8_t             plCloseToBall;               /* 62 */
    int8_t             plNotFarFromBall;            /* 63 */
    int8_t             ballLessEqual4;              /* 64 */
    int8_t             ball4To8;                    /* 65 */
    int8_t             ball8To12;                   /* 66 */
    int8_t             ball12To17;                  /* 67 */
    int8_t             ballAbove17;                 /* 68 */
    int8_t             prevPlVeryCloseToBall;       /* 69 */
    int16_t            field_46;                    /* 70 */
    Sprite            *lastHeadingTacklingPlayer;   /* 72 */
    int16_t            goalkeeperSavedCommentTimer; /* 76 */
    int16_t            field_4E;                    /* 78 */
    int16_t            goalkeeperDivingRight;       /* 80 */
    int16_t            goalkeeperDivingLeft;        /* 82 */
    int16_t            ballOutOfPlayOrKeeper;       /* 84 */
    int16_t            goaliePlayingOrOut;          /* 86 */
    int16_t            passingBall;                 /* 88 */
    int16_t            passingToPlayer;             /* 90 */
    int16_t            playerSwitchTimer;           /* 92 */
    int16_t            ballInPlay;                  /* 94 */
    int16_t            ballOutOfPlay;               /* 96 */
    int16_t            ballX;                       /* 98 */
    int16_t            ballY;                       /* 100 */
    int16_t            passKickTimer;               /* 102 */
    Sprite            *passingKickingPlayer;        /* 104 */
    int16_t            ballControlTurnTimer;        /* 108: frames the ball-controlling player has been turning; if > ballControlTurnThresholds[ballControl] the player loses the ball on the turn */
    int16_t            ballCanBeControlled;         /* 110 */
    int16_t            ballControllingPlayerDirection; /* 112 */
    int16_t            field_72;                    /* 114 */
    int16_t            field_74;                    /* 116 */
    int16_t            spinTimer;                   /* 118 */
    int16_t            leftSpin;                    /* 120 */
    int16_t            rightSpin;                   /* 122 */
    int16_t            longPass;                    /* 124 */
    int16_t            longSpinPass;                /* 126 */
    int16_t            passInProgress;              /* 128 */
    int16_t            AI_timer;                    /* 130 */
    int16_t            field_84;                    /* 132 */
    int16_t            AI_afterTouchStrength;       /* 134 */
    int16_t            AI_ballSpinDirection;        /* 136 */
    int16_t            wonTheBallTimer;             /* 138 */
    int16_t            goalkeeperPlaying;           /* 140 */
    int16_t            resetControls;               /* 142 */
};

/* TeamGeneralInfo has 9 pointer fields (opponentsTeam, inGameTeamPtr,
 * teamStatsPtr, spritesTable, shotChanceTable, controlledPlayer,
 * passToPlayerPtr, lastHeadingTacklingPlayer, passingKickingPlayer);
 * +36 bytes on 64-bit. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _tgi_size_check[(sizeof(TeamGeneralInfo) == 144) ? 1 : -1];
#else
typedef char _tgi_size_check[(sizeof(TeamGeneralInfo) == 180) ? 1 : -1];
#endif

/* ==================================================================
 * PlayerInfo — 61 bytes (embedded in TeamGame)
 * ================================================================== */
struct PlayerInfo {
    int8_t  skip;               /* 0 */
    int8_t  playerIndex;        /* 1 */
    int8_t  goalsScored;        /* 2 */
    int8_t  shirtNumber;        /* 3 */
    int8_t  position;           /* 4 */
    int8_t  face;               /* 5 */
    int8_t  isInjured;          /* 6 */
    int8_t  field_7;            /* 7 */
    int8_t  field_8;            /* 8 */
    int8_t  previousCards;      /* 9 */
    int8_t  numYellowCards;     /* 10 */
    int8_t  field_B;            /* 11 */
    int8_t  shortName[15];      /* 12 */
    int8_t  passing;            /* 27 */
    int8_t  shooting;           /* 28 */
    int8_t  heading;            /* 29 */
    int8_t  tackling;           /* 30 */
    int8_t  ballControl;        /* 31 */
    int8_t  speed;              /* 32 */
    int8_t  finishing;          /* 33 */
    int8_t  goalieSkill;        /* 34 */
    int8_t  injuriesBitfield;   /* 35 */
    int8_t  halfPlayed;         /* 36 */
    int8_t  face2;              /* 37 */
    int8_t  fullName[23];       /* 38 */
};

typedef char _playerinfo_size_check[(sizeof(PlayerInfo) == 61) ? 1 : -1];

#define PLAYERINFO_SIZE 61

/* ==================================================================
 * TeamGame — 1018 bytes (0x3FA)
 * ================================================================== */
struct TeamGame {
    int16_t    prShirtType;     /* 0 */
    int16_t    prShirtCol;      /* 2 */
    int16_t    prStripesCol;    /* 4 */
    int16_t    prShortsCol;     /* 6 */
    int16_t    prSocksCol;      /* 8 */
    int16_t    secShirtType;    /* 10 */
    int16_t    secShirtCol;     /* 12 */
    int16_t    secStripesCol;   /* 14 */
    int16_t    secShortsCol;    /* 16 */
    int16_t    secSocksCol;     /* 18 */
    int16_t    markedPlayer;    /* 20 */
    int8_t     teamName[17];    /* 22 */
    int8_t     reserved1;       /* 39: confirmed no reads (tree + SWOS2.s) — TEAMGAME_UNK1
                                 * EQU 39 is defined but never referenced; in-match team
                                 * header pad between teamName and numOwnGoals (was unk1) */
    int8_t     numOwnGoals;     /* 40 */
    int8_t     reserved3;       /* 41: confirmed no reads (tree + SWOS2.s) — TEAMGAME_UNK3
                                 * EQU 41 defined but never referenced; pad before players[]
                                 * (was unk3) */
    PlayerInfo players[16];     /* 42 */
};

typedef char _teamgame_size_check[(sizeof(TeamGame) == 1018) ? 1 : -1];

#define TEAMGAME_SIZE 1018
#define TEAMGAME_PLAYERS_OFFSET 42  /* byte offset of players[] within TeamGame */

/* ==================================================================
 * TeamStatisticsData — 14 bytes
 * ================================================================== */
struct TeamStatisticsData {
    int16_t ballPossession;     /* 0 */
    int16_t cornersWon;         /* 2 */
    int16_t foulsConceded;      /* 4 */
    int16_t bookings;           /* 6 */
    int16_t sendingsOff;        /* 8 */
    int16_t goalAttempts;       /* 10 */
    int16_t onTarget;           /* 12 */
};

typedef char _teamstats_size_check[(sizeof(TeamStatisticsData) == 14) ? 1 : -1];

/* ==================================================================
 * GoalieSkill — 60 bytes
 * ================================================================== */
struct GoalieSkill {
    int16_t field_0;            /* 0 */
    int16_t field_2;            /* 2 */
    int16_t field_4;            /* 4 */
    int16_t speed_unk;          /* 6 */
    int16_t speed_unk2;         /* 8 */
    int16_t index[16];          /* 10 */
    int16_t field_2A;           /* 42 */
    int16_t field_2C;           /* 44 */
    int16_t field_2E;           /* 46 */
    int16_t field_30;           /* 48 */
    int16_t field_32;           /* 50 */
    int16_t field_34;           /* 52 */
    int16_t field_36;           /* 54 */
    int16_t field_38;           /* 56 */
    int16_t field_3A;           /* 58 */
};

typedef char _goalieskill_size_check[(sizeof(GoalieSkill) == 60) ? 1 : -1];

/* ==================================================================
 * SpriteGraphics — 24 bytes
 * ================================================================== */
struct SpriteGraphics {
    uint8_t *sprData;           /* 0: ptr to pixel data */
    uint8_t *maskPtr;           /* 4: ptr to mask data (NULL = no mask) */
    uint16_t type;              /* 8: 0=separate planes, nonzero=linked/interleaved */
    int16_t  pixWidth;          /* 10 */
    int16_t  nlines;            /* 12 */
    int16_t  wquads;            /* 14: width in 16-pixel quads */
    int16_t  centerX;           /* 16 */
    int16_t  centerY;           /* 18 */
    uint16_t bltsize;            /* 20: pre-computed blitter size value */
    int16_t  ordinal;           /* 22 */
};

/* SpriteGraphics has 2 pointer fields (sprData, maskPtr); +8 on 64-bit. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _spritegfx_size_check[(sizeof(SpriteGraphics) == 24) ? 1 : -1];
#else
typedef char _spritegfx_size_check[(sizeof(SpriteGraphics) == 32) ? 1 : -1];
#endif

#define SPRITEGRAPHICS_SIZE 24

/* ==================================================================
 * CharsTableHeader — font/character rendering table header (14 bytes)
 * The conversion table data (uint8_t[]) follows immediately after.
 * ================================================================== */
typedef struct {
    uint8_t  reserved[8];       /* 0-7: unknown/unused header data */
    int16_t  charSpacing;       /* 8 */
    int16_t  spaceWidth;        /* 10 */
    int16_t  spriteIndexOffset; /* 12 */
    /* conversion table uint8_t[] follows at offset 14 */
} CharsTableHeader;

typedef char _charstblhdr_size_check[(sizeof(CharsTableHeader) == 14) ? 1 : -1];

/* ==================================================================
 * Player state constants (from PlayerStates enum in IDA)
 * ================================================================== */
#define PL_NORMAL                0
#define PL_TACKLING              1
#define PL_TACKLED               3
#define PL_GOALIE_CATCHING_BALL  4
#define PL_THROW_IN              5
#define PL_GOALIE_DIVING_HIGH    6
#define PL_GOALIE_DIVING_LOW     7
#define PL_STATIC_HEADING        8
#define PL_JUMP_HEADING          9
#define PL_DOWN                  10
#define PL_GOALIE_CLAIMED        11
#define PL_BOOKED                12
#define PL_ROLLING_INJURED       13
#define PL_SAD                   14
#define PL_HAPPY                 15

/* ==================================================================
 * Game state constants (from GameState enum in IDA)
 * ================================================================== */
#define ST_PLAYERS_TO_INITIAL_POSITIONS  0
#define ST_GOAL_OUT_LEFT                 1
#define ST_GOAL_OUT_RIGHT                2
#define ST_KEEPER_HOLDS_BALL             3
#define ST_CORNER_LEFT                   4
#define ST_CORNER_RIGHT                  5
#define ST_FREE_KICK_LEFT1               6
#define ST_FREE_KICK_LEFT2               7
#define ST_FREE_KICK_LEFT3               8
#define ST_FREE_KICK_CENTER              9
#define ST_FREE_KICK_RIGHT1              10
#define ST_FREE_KICK_RIGHT2              11
#define ST_FREE_KICK_RIGHT3              12
#define ST_FOUL                          13
#define ST_PENALTY                       14
#define ST_THROW_IN_FORWARD_RIGHT        15
#define ST_THROW_IN_CENTER_RIGHT         16
#define ST_THROW_IN_BACK_RIGHT           17
#define ST_THROW_IN_FORWARD_LEFT         18
#define ST_THROW_IN_CENTER_LEFT          19
#define ST_THROW_IN_BACK_LEFT            20
#define ST_STARTING_GAME                 21
#define ST_CAMERA_GOING_TO_SHOWERS       22
#define ST_GOING_TO_HALFTIME             23
#define ST_PLAYERS_GOING_TO_SHOWER       24
#define ST_RESULT_ON_HALFTIME            25
#define ST_RESULT_AFTER_THE_GAME         26
#define ST_FIRST_EXTRA_STARTING          27
#define ST_FIRST_EXTRA_ENDED             28
#define ST_FIRST_HALF_ENDED              29
#define ST_GAME_ENDED                    30
#define ST_PENALTIES                      31
/* ---- The two camera origins ----------------------------------------------
 * `_cameraX` is the GAME's camera: MoveCamera integrates it, every gameplay
 * decision reads it, and the `onScreen` cull is measured from it. It is
 * clamped to the SIMULATION range (0 .. 0x2A0-336) so that the window size
 * cannot change which players the AI may auto-select.
 *
 * `Render_CameraX()` is where we DRAW from: the simulation camera's range
 * SCALED onto the pan range a wide view actually has (0 .. 0x2A0-W), so the
 * picture drifts across the whole match instead of sitting still and then
 * snapping through a narrow live window — see camera.c for the measurements.
 * On Amiga and on any 4:3 display the two are identical, so the m68k codegen
 * and the classic view are unchanged.
 * -------------------------------------------------------------------------- */
#ifdef SWOS_SDL_BUILD
int16_t Render_CameraX(void);
/* Same origin in 16.16 — the presenter's sub-pixel shift needs the fraction,
 * which under the scaled map is most of the per-frame motion. */
int32_t Render_CameraX16(void);
/* Sub-pixel scroll residual (16.16 fractions) — see camera.c. SDL-only; the
 * m68k build scrolls in whole pixels on real hardware and defines neither. */
void    Render_PublishScrollSubpixel(void);
int     Render_ScrollSubpixel(uint16_t *fx, uint16_t *fy);
/* The origin a view of the SIMULATION DRAW width (display.h, _sim_draw_width) draws
 * from. For what the match DECIDES from the drawn view; placing pixels stays on
 * Render_CameraX(). Equal to it whenever the two widths are. */
int16_t Sim_DrawCameraX(void);
int16_t Sim_DrawCameraY(void);      /* the same for the height that goes with that width */
int16_t Sim_DrawHeight(void);
/* The vertical render origin (camera.c): `_cameraY` scaled onto a picture taller than
 * the simulation view (the DOS view with widescreen on); identity otherwise. Draw with
 * it; never make a game decision from it. And its inverse, for a recorded drawn camera. */
int16_t Render_CameraY(void);
int32_t Render_CameraY16(void);
int16_t Render_SimCameraYFromRender(int16_t renderY);
#else
#define Render_CameraX() (_cameraX.s.i)
#define Sim_DrawCameraX() (_cameraX.s.i)
#define Sim_DrawCameraY() (_cameraY.s.i)
#define Sim_DrawHeight()  (_sim_view_height)
#define Render_CameraY() (_cameraY.s.i)
#define Render_SimCameraYFromRender(y) (y)
#endif

#define ST_GAME_IN_PROGRESS              100
#define ST_STOPPED                       101
#define ST_WAITING_ON_PLAYER             102

/* ==================================================================
 * TeamFile — on-disk team data format (header portion, 76 bytes)
 * Full entry is 684 bytes including player data that follows.
 * ================================================================== */
struct __attribute__((packed, aligned(2))) TeamFile {
    uint8_t  countryNumber;      /* 0 */
    uint8_t  teamOrdinal;        /* 1 */
    int16_t  globalTeamNumber;   /* 2 */
    uint8_t  teamControls;       /* 4 */
    int8_t   teamName[17];       /* 5 */
    uint8_t  writesZeroHere;     /* 22 */
    uint8_t  andWith0xFE;        /* 23 */
    uint8_t  tactics;            /* 24 */
    uint8_t  league;             /* 25 */
    uint8_t  prShirtType;        /* 26 */
    uint8_t  prStripesColor;     /* 27 */
    uint8_t  prBasicColor;       /* 28 */
    uint8_t  prShortsColor;      /* 29 */
    uint8_t  prSocksColor;       /* 30 */
    uint8_t  secShirtType;       /* 31 */
    uint8_t  secStripesColor;    /* 32 */
    uint8_t  secBasicColor;      /* 33 */
    uint8_t  secShortsColor;     /* 34 */
    uint8_t  secSocksColor;      /* 35 */
    int8_t   coachName[23];      /* 36 */
    uint8_t  playerValuesDirty;  /* 59 (was someFlag): dirty flag for the per-player
                                  *     cachedMatchValue cache. TEAM_VALUES_DIRTY when the
                                  *     squad changes (tactics edit / transfer / teamInit);
                                  *     TEAM_VALUES_CLEAN once UpdatePlayerPositionAndReturnRole
                                  *     recomputes the values. See TEAM_VALUES_* below. */
    uint8_t  playerNumbers[16];  /* 60 */
    /* player data follows at offset 76 */
};

typedef char _teamfile_size_check[(sizeof(struct TeamFile) == 76) ? 1 : -1];

/* TeamFile.playerValuesDirty (byte 59) states — is the per-player
 * cachedMatchValue cache stale? */
#define TEAM_VALUES_CLEAN  0   /* cache valid — use cachedMatchValue as-is */
#define TEAM_VALUES_DIRTY  1   /* squad changed — recompute cachedMatchValue before use */

typedef struct TeamFile TeamFile;

/* ==================================================================
 * GoalInfo — 6 bytes
 * goalType: 0=normal, 1=penalty, 2=own goal
 * time: 4 bytes (ASCII digits, patched in from gameTime[])
 * ================================================================== */
struct GoalInfo {
    int16_t goalType;           /* 0 */
    int8_t  time[4];            /* 2: four ASCII digit bytes */
};

typedef char _goalinfo_size_check[(sizeof(GoalInfo) == 6) ? 1 : -1];

/* ==================================================================
 * ScorerInfo — 66 bytes, 8 slots per team (528 bytes total)
 * ================================================================== */
struct ScorerInfo {
    int16_t  shirtNum;          /* 0: shirt number (or +100 for own goal) */
    int16_t  numSpritesTaken;   /* 2: number of sprite rows used */
    int16_t  numGoals;          /* 4: number of goals scored */
    GoalInfo goals[10];         /* 6: goal records (10 * 6 = 60 bytes) */
};

typedef char _scorerinfo_size_check[(sizeof(ScorerInfo) == 66) ? 1 : -1];

#define SCORERINFO_SIZE 66

/* Goal type constants */
#define GT_NORMAL   0
#define GT_PENALTY  1
#define GT_OWN_GOAL 2

/* ==================================================================
 * PlayerFile — 38 bytes per player within a team file
 *
 * Field names mirror the canonical SWOS2.s PLAYERFILE_* equates; the comments
 * add byte interpretations from the SWOS HEX editing guide (BLISZKA) and from
 * our own runtime analysis. Three formerly-opaque fields are now resolved from
 * how the code uses them: field_1 -> cachedMatchValue, someFlag -> form, and
 * field_19 (the NUL at byte 25) folded into playerName[23] (see each comment +
 * the PLAYER_FORM_* / PF_* defines below).
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) PlayerFile {
    int8_t   nationality;          /* 0: nationality code */
    /* 1 (was field_1): RUNTIME scratch — the player's computed match value/rating.
     * UpdatePlayerPositionAndReturnRole (match_sim.c) writes the per-player match
     * value here (GetPlayerPrice for outfielders / price for GK+subs, forced to 1
     * if sold / 0 if injured) and caches it; career_status.c reads it back as
     * `formDelta = cachedMatchValue - expected` to drive the post-match form change.
     * NOT the HEX guide's "invisible skill", and NOT a formation slot. */
    int8_t   cachedMatchValue;     /* 1 */
    int8_t   shirtNumber;          /* 2: shirt / squad number */
    int8_t   playerName[23];       /* 3-25: first + last name. 22 chars max (the canonical
                                    *     PLAYERFILE_PLAYERNAME) plus byte 25 = the always-NUL
                                    *     terminator (was the separate PLAYERFILE_FIELD_19);
                                    *     folded in so the name buffer is self-terminating. */
    uint8_t  positionAndFace;      /* 26: bits 5-7 = position (PF_POSITION_MASK: 0=GK..7=ST),
                                    *     bits 3-4 = face/hair (PF_FACE_MASK), and three runtime
                                    *     flags in the low bits: PF_INFORM (0), PF_LOAN (1),
                                    *     PF_TRIALIST (2). See the PF_* defines below. */
    uint8_t  cardsInjuries;        /* 27: injury condition + card discipline flags */
    int8_t   passing;              /* 28: high nibble 0 / low nibble = passing */
    int8_t   velocityHeading;      /* 29: hi nibble = velocity (Ve — the SWOS player-card
                                    *     label, aka shot power; cross-validated against
                                    *     AnoXic's SavCar.ASI attVH) / lo = heading */
    int8_t   tacklingControl;      /* 30: hi nibble = tackling (Ta) / lo = control (Co) */
    int8_t   speedFinishing;       /* 31: hi nibble = speed / lo = finishing */
    int8_t   price;                /* 32: base market price ID (lookup into the .exe
                                    *     price table; valid range 0x00-0x31) */
    /* 33-36: PLAYERFILE_GOALSTATS — per-competition goals-scored counters. Kept as
     * goalStats[] so the canonical indexed access still works (goalStats[d7],
     * indexed by competition slot, as in game_init.c / match_sim.c); the named
     * members document each index per the HEX guide. */
    union {
        uint8_t goalStats[4];      /* 33-36 (indexed by competition slot) */
        struct {
            uint8_t leagueGoals;       /* 33: domestic league */
            uint8_t domesticCupGoals;  /* 34: domestic cup */
            uint8_t leagueCupGoals;    /* 35: league cup */
            uint8_t euroCupGoals;      /* 36: european / continental cup */
        };
    };
    int8_t   form;                 /* 37 (was someFlag): player form/morale, signed
                                    *     [PLAYER_FORM_MIN..PLAYER_FORM_MAX] = [-100..+100].
                                    *     Two out-of-range sentinels mark non-first-team
                                    *     players: PLAYER_FORM_TRIALIST / PLAYER_FORM_RESERVE. */
} PlayerFile;

typedef char _playerfile_size_check[(sizeof(PlayerFile) == 38) ? 1 : -1];

/* ---- PlayerFile.form (byte 37) values ----
 * Normal range is signed [-100, +100]. The two values below -100 are sentinels
 * for non-first-team players (the form slot is repurposed as a status marker,
 * shown verbatim by FormatPlayerValueOrStatusText / teams.c): a trialist is
 * signed at TRIALIST (status "TRIAL", 33% wage), demoted to RESERVE (status
 * "RES", 66% wage) the next week, then has a ~20%/week chance of promotion to a
 * real form value with PF_INFORM set. These are NOT "sold"/"transferred" — that
 * was a transfer_market.c misread; a loaned/transfer-listed player is flagged by
 * PF_LOAN (positionAndFace bit 1), not here.
 *
 * Plain byte values (no cast): callers keep their existing (int8_t)/(uint8_t)
 * casts, so signed clamps and unsigned equality tests both stay correct. */
#define PLAYER_FORM_MAX        0x64   /* +100 (signed clamp ceiling) */
#define PLAYER_FORM_MIN        0x9C   /* -100 (signed clamp floor) */
#define PLAYER_FORM_TRIALIST   0x9B   /* trialist — in-game status "TRIAL" */
#define PLAYER_FORM_RESERVE    0x9A   /* reserve  — in-game status "RES" */

/* ---- PlayerFile.positionAndFace (byte 26) bitfields ----
 * Static (from the team file): position in the top 3 bits, face/hair in bits 3-4.
 * The low 3 bits are runtime flags (0 in static .EDT/.TMD files). */
#define PF_POSITION_MASK   0xE0   /* bits 5-7: 0=GK,1=RB,2=LB,3=CD,4=RW,5=LW,6=MF,7=ST */
#define PF_POSITION_SHIFT  5
#define PF_FACE_MASK       0x18   /* bits 3-4: face/hair index 0-3 */
#define PF_FACE_SHIFT      3
#define PF_INFORM          0x01   /* bit 0: player is "in form" (doubles form gains) */
#define PF_LOAN            0x02   /* bit 1: on loan — in-game status "LOAN" (match_sim
                                  *        "preserves loan bit"). NOT transfer-list/sold. */
#define PF_TRIALIST        0x04   /* bit 2: trialist marker (set when adding a trialist) */

#define PLAYERFILE_SIZE 38

/* ==================================================================
 * TeamFileEx — TeamFile header (76 bytes) + 16 PlayerFile entries
 * Total: 684 bytes. Used for careerTeam, currentTeam, etc.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) TeamFileEx {
    TeamFile   team;               /* 0-75  (76 bytes) */
    PlayerFile players[16];        /* 76-683 (608 bytes) */
} TeamFileEx;

typedef char _teamfileex_size_check[(sizeof(TeamFileEx) == 684) ? 1 : -1];

/* _teamFileBuffer: a team file as LoadTeamFile reads it -- a count word, then the
 * records. The original's held 93 (0xFA0E bytes); a world's file holds up to 256, all an
 * ordinal byte can number (the World 2026-27's Italy: docs/WORLD_2026_27.md, phase 2). */
#define TEAM_FILE_MAX_TEAMS    256
#define TEAM_FILE_BUFFER_SIZE  (2 + TEAM_FILE_MAX_TEAMS * 684)

/* The selected-teams roster (career "SELECT TEAMS" list), max 32. Typed alias
 * for the relocated carrier in selected_teams.c — same address as the legacy
 * `selTeamsPtr` overlay base (both labels sit at +0). Prefer `selectedTeams[i]`
 * over the historical `(TeamFileEx *)&selTeamsPtr` cast (STRUCT_CONSOLIDATE /
 * SELECTED_TEAMS_UNTANGLE phase 3). The raw `(uint8_t*)&selTeamsPtr + offset`
 * identity-resolve sites stay as-is (teamSelOffset is a byte-offset identity). */
/* The team cache: the career's (or competition's) teams in play. The original held 100;
 * a big world needs more (the SWOS 96/97 Mod's World 2026-27 raised it to 160,
 * docs/WORLD_2026_27.md phase 2). AddTeamToBuffer panics (0x8F01) rather than write a
 * team past the end. */
#define SELECTED_TEAMS_MAX 160
extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];

/* The historical sub-labels of the selected-teams overlay, now pure-C offset
 * macros into the one backing symbol (was asm .skip labels in selected_teams.c;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — docs/WASM.md §4b). `_selectedTeams` is a 68400-byte load-bearing
 * union: records [0..1] are the real selected teams; [2..] physically overlaps
 * the edit-teams workspace, exactly as the Amiga overlay reserved it.
 *   - `_selTeamsPtr` keeps its dual role: the bare macro is the buffer's first
 *     byte (value), `&_selTeamsPtr` is the buffer base (address).
 *   - the selTeam* field macros are record[0] TeamFile fields.
 * Offsets are locked by _Static_asserts in selected_teams.c. */
#define _selTeamsPtr            (*(uint8_t *)_selectedTeams)           /* +0    base / first byte */
#define _selTeamNameBase        (*((uint8_t *)_selectedTeams + 5))     /* +5    record[0].teamName */
#define _selTeamImportField     (*((uint8_t *)_selectedTeams + 25))    /* +25   record[0].league   */
#define _selTeamPlayerIndices   ((uint8_t *)_selectedTeams + 60)       /* +60   record[0].playerNumbers */
#define _editTeamsBuffer        ((uint8_t *)_selectedTeams + 1368)     /* +1368  edit-teams workspace */
#define _editTeamsSaveVarsArea  ((uint8_t *)_selectedTeams + 45146)    /* +45146 edit-teams save scratch */

/* The careerTeam and currentTeam buffers are each a TeamFileEx (16 active
 * players) immediately followed by 16 reserve PlayerFile entries = 1292 bytes.
 * Use this for "is this player pointer inside the career/current team buffer?"
 * range checks (e.g. GetDisplayedPlayerValue) instead of bounding on the next
 * BSS symbol — the latter is a fragile adjacency that breaks once those
 * neighbours (_playerPositions, careerNumPlayers) are lifted out of the overlay.
 * See docs/CAREERTEAM_STEP4_REWORK.md. */
#define CAREER_TEAM_BUF_SIZE (sizeof(TeamFileEx) + 16 * sizeof(PlayerFile))

/* ==================================================================
 * DIYCompetition — 96 bytes (0x60), base competition descriptor.
 * Used for diyFileBufferCopy and as the first 0x60 bytes of
 * diyCompetitionStart (which is actually 0x5C8 bytes total).
 *
 * Original IDA struct had opaque field_2[48] and field_3A[36];
 * sub-fields are broken out here based on actual access patterns.
 * ================================================================== */
/* DUAL-USE FIELDS (untangle Part A, decision D1): several offsets carry a cup
 * meaning (the field name) and a league/comp-slot meaning (the SLOT_ALIAS legacy
 * name). They are the SAME bytes — modelled as anonymous unions so cup code reads
 * the cup name and league/slot code reads the alias name, both self-documenting
 * and byte-identical (mirrors how DIYCompetitionEx.matchData unions cup/league
 * views). The standalone SLOT_ALIAS globals (swos_competition_slots.c) still exist
 * and alias the same bytes; consumers migrate to these typed names per-slot. */
typedef struct DIYCompetition {
    int16_t  competitionType;          /* 0x00 */
    /* --- field_2 sub-fields (48 bytes, 0x02-0x31) --- */
    union { uint16_t isActive; uint16_t competitionOver; }; /* 0x02: in-use flag (cup) / "over" flag (slot alias) */
    int8_t   competitionName[36];      /* 0x04: competition name string buffer */
    uint8_t *namePtr;                  /* 0x28: ptr to competition name string */
    uint8_t  compSlot;                   /* 0x2C: competition slot cache-key byte (pairs with slotId) */
    uint8_t  slotId;                   /* 0x2D: competition slot index for cache matching */
    uint8_t  field_2E;                 /* 0x2E: team configuration byte */
    uint8_t  viewRandomPhase;          /* 0x2F: current month/phase for scheduling */
    union { uint8_t avgSkill; uint8_t trophyBonusValue; }; /* 0x30: avg skill (cup) / trophy bonus (slot alias *TrophyBonusValue) */
    uint8_t  strengthTier;             /* 0x31: strength bracket tier */
    /* --- end field_2 --- */
    int16_t  numTeams;                 /* 0x32 */
    int16_t  pitchTypeOrSeason;        /* 0x34 */
    int16_t  pitchType;                /* 0x36 */
    int8_t   startMonth;               /* 0x38 */
    int8_t   endMonth;                 /* 0x39 */
    /* --- field_3A sub-fields (36 bytes, 0x3A-0x5D) --- */
    union { int16_t resultCode; uint16_t winnerTeam; };    /* 0x3A: result code (cup) / winner-team id (slot alias *CompWinnerTeam) */
    union { int16_t substitutesCount; uint16_t squadViewParam; }; /* 0x3C: sub count (cup) / squad-view param (slot alias primaryCompSquadViewParam) */
    int16_t  slotStateA;               /* 0x3E: competition slot state A / menu ordinal, init 3 */
    int16_t  slotStateB;               /* 0x40: competition slot state B, init 2 */
    int16_t  slotStateC;               /* 0x42: competition slot state C / menu ordinal, init 8 */
    int16_t  matchStarted;             /* 0x44: 1 when match processing begins */
    int16_t  careerTeamInvolved;       /* 0x46: 1 when career team participates */
    int16_t  hasGroups;                /* 0x48: 0=no, 1=groups, 2=groups with final */
    int16_t  menuState;                /* 0x4A: cup menu state (0-6) */
    union { int16_t roundIterator; uint16_t compFinished; }; /* 0x4C: round iter (cup) / finished flag (slot alias *CompFinished) */
    int16_t  numTeamsInStage;          /* 0x4E: teams in current stage */
    int16_t  matchPairsPerRound;       /* 0x50: match pairs per round (half teams) */
    int16_t  totalMatchesInRound;      /* 0x52: total matches/fixtures in round */
    union { int16_t roundComplete; uint16_t minDivisionIndex; }; /* 0x54: round-complete (cup) / min-division index (slot alias diyTeamMinDivisionIndex) */
    union { int16_t competitionFinished; uint16_t maxDivisionOffset; }; /* 0x56: comp-finished (cup) / max-division offset (slot alias diyTeamMaxDivisionOffset) */
    union { int16_t currentRoundIndex; uint16_t promotionCount; }; /* 0x58: round idx (cup) / promotion count (slot alias leaguePromotionCount) */
    union { int16_t numRounds; uint16_t relegationCount; }; /* 0x5A: num rounds (cup) / relegation count (slot alias leagueRelegationCount) */
    int16_t  subRoundCounter;          /* 0x5C: sub-round counter for multi-fixture rounds */
    /* --- end field_3A --- */
    int16_t  playEachTeam;             /* 0x5E */
} DIYCompetition;

/* DIYCompetition has 1 pointer field (namePtr); +4 from pointer growth
 * + 4 from 8-byte tail alignment (struct is NOT packed, so its
 * alignment matches the largest member = pointer = 8 bytes on 64-bit)
 * → +8 total. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _diycompetition_size_check[(sizeof(DIYCompetition) == 0x60) ? 1 : -1];
#else
typedef char _diycompetition_size_check[(sizeof(DIYCompetition) == 0x68) ? 1 : -1];
#endif

/* Pin every dual-use (D1 union) offset on BOTH targets so a future struct edit or
 * a wrong consumer path is caught at compile time. (sizeof(void*) - 4) = CFB_NP
 * (0 on 32-bit, 4 on 64-bit) tracks the namePtr@0x28 growth for fields past it;
 * competitionOver@0x02 sits before namePtr so it never shifts. The cup-name
 * sibling of each union shares the same offset by construction. */
_Static_assert(offsetof(DIYCompetition, competitionOver)   == 0x02,                       "competitionOver@0x02");
_Static_assert(offsetof(DIYCompetition, trophyBonusValue)  == 0x30 + (sizeof(void *) - 4), "trophyBonusValue@0x30");
_Static_assert(offsetof(DIYCompetition, winnerTeam)        == 0x3A + (sizeof(void *) - 4), "winnerTeam@0x3A");
_Static_assert(offsetof(DIYCompetition, squadViewParam)    == 0x3C + (sizeof(void *) - 4), "squadViewParam@0x3C");
_Static_assert(offsetof(DIYCompetition, compFinished)      == 0x4C + (sizeof(void *) - 4), "compFinished@0x4C");
_Static_assert(offsetof(DIYCompetition, minDivisionIndex)  == 0x54 + (sizeof(void *) - 4), "minDivisionIndex@0x54");
_Static_assert(offsetof(DIYCompetition, maxDivisionOffset) == 0x56 + (sizeof(void *) - 4), "maxDivisionOffset@0x56");
_Static_assert(offsetof(DIYCompetition, promotionCount)    == 0x58 + (sizeof(void *) - 4), "promotionCount@0x58");
_Static_assert(offsetof(DIYCompetition, relegationCount)   == 0x5A + (sizeof(void *) - 4), "relegationCount@0x5A");

/* Slot-4 (diy) untangle, Part A S2: the clean fields the SLOT_ALIAS names
 * diyCompetitionContext (@0, VALUE = competitionType) and diyCompSlotStateA/B/C
 * (@0x3E/40/42) pointed at. Pinned so a wrong typed consumer path is caught at
 * compile time on both targets (CFB_NP = sizeof(void*)-4 tracks the namePtr@0x28
 * growth for the slot-state fields; competitionType@0 sits before it). */
_Static_assert(offsetof(DIYCompetition, competitionType) == 0x00,                        "competitionType@0x00");
/* competitionName@0x04 (slot-2 S3 + others): before namePtr@0x28, never shifts. */
_Static_assert(offsetof(DIYCompetition, competitionName) == 0x04,                        "competitionName@0x04");
_Static_assert(offsetof(DIYCompetition, slotStateA)      == 0x3E + (sizeof(void *) - 4), "slotStateA@0x3E");
_Static_assert(offsetof(DIYCompetition, slotStateB)      == 0x40 + (sizeof(void *) - 4), "slotStateB@0x40");
_Static_assert(offsetof(DIYCompetition, slotStateC)      == 0x42 + (sizeof(void *) - 4), "slotStateC@0x42");

/* ==================================================================
 * DIYTeamStats / DIYLeagueTableRow — 18 bytes
 * Per-team league standings entry written by clearLeagueTableStats.
 * Each row is one teamSelOffset (NOT cleared by the loop) plus 7 stats
 * fields cleared per row. 64 rows total = the `leagueStandings[64]`
 * field of DIYCompetitionEx.
 *
 * UNTANGLE (roadmap step 3): this table used to OVERLAY cupTeamIds[51]
 * (diyCompetitionStart + 0x2B4), so writing standings for a league
 * (sub-)slot zeroed the cup (sub-)slot's teamGridPairings /
 * bracketScratch.teamOffsetTable — the type-2 hybrid clobber that forced
 * the teamOffsetTable rebuilds. It is now a real trailing field placed
 * AFTER bracketScratch (offset 0x5C8 / 0x5D0 on 64-bit), so the cup
 * scratch and the league standings no longer share bytes. See
 * docs/TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) DIYTeamStats {
    int32_t  teamSelOffset;         /* 0x00: team IDENTITY (step-4 phase 3), NOT a byte offset despite the legacy name. -1 == careerTeam; >= 0 == the team's index into the selTeamsPtr array. Resolve via diy_resolve_team()/diy_resolve_team_name(); set via diy_team_to_identity() (diy_competition.c). Was a signed selTeamsPtr-relative byte offset before phase 3. */
    uint16_t gamesPlayed;           /* 0x04 */
    uint16_t wins;                  /* 0x06 */
    uint16_t draws;                 /* 0x08 */
    uint16_t losses;                /* 0x0A */
    uint16_t goalsFor;              /* 0x0C */
    uint16_t goalsAgainst;          /* 0x0E */
    uint16_t points;                /* 0x10 */
} DIYTeamStats;

typedef DIYTeamStats DIYLeagueTableRow;
typedef char _diyteamstats_size_check[(sizeof(DIYTeamStats) == 18) ? 1 : -1];

/* ==================================================================
 * DIYTournamentSlot — 118 bytes (0x76)
 *
 * Tournament-format slot record. SWOS2.s does not name this type, but
 * the C code in src/diy_competition.c iterates over a 6-element array
 * of these records embedded inside `diyTournamentTeamIds`, starting
 * at offset 0xC0 (= diyFileBufferCopy + 0x15A) with a stride of 0x76.
 *
 * Accessed via `slot = (uint8_t *)diyFileBufferCopy + slotIdx*0x76`
 * with field offsets +0x15A..+0x172 (24-byte field range), +0x174
 * (matchesPerSlot), +0x176 (progression month bytes tail).
 *
 * Field offsets within this struct begin at 0; the +0x15A bias from
 * the diyFileBufferCopy base is not represented here.
 *
 * See DIYCOMPETITIONEX_AUDIT.md for the per-field investigation.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) DIYTournamentSlot {
    uint8_t  _pad_00[2];            /* 0x00 (slot+0x15A): cleared to 0 in init, purpose unknown */
    uint16_t doubleCount;           /* 0x02 (slot+0x15C): cup-bracket doubled count flag */
    uint16_t isLeagueFormat;        /* 0x04 (slot+0x15E): nonzero=league/grouped, 0=cup */
    uint16_t seedingType;           /* 0x06 (slot+0x160) */
    uint16_t numTeams;              /* 0x08 (slot+0x162) */
    uint16_t divisionFactor;        /* 0x0A (slot+0x164): "next slot" / division factor */
    uint16_t playEachTeam;          /* 0x0C (slot+0x166) */
    uint16_t initConst168;          /* 0x0E (slot+0x168): init=2 */
    uint16_t groups;                /* 0x10 (slot+0x16A) */
    uint16_t teamsPerGroup;         /* 0x12 (slot+0x16C) */
    uint16_t formatFlag16E;         /* 0x14 (slot+0x16E): 1=normal, 2=skip */
    uint16_t initConst170;          /* 0x16 (slot+0x170): init=1 */
    uint16_t formatFlag172;         /* 0x18 (slot+0x172): 1=halve */
    uint16_t matchesPerSlot;        /* 0x1A (slot+0x174): computed matches-per-team this slot */
    uint8_t  progressionTable[90];  /* 0x1C (slot+0x176): month bytes — fills to 118 (0x76) */
} DIYTournamentSlot;

typedef char _diyslot_size_check[(sizeof(DIYTournamentSlot) == 0x76) ? 1 : -1];

/* ==================================================================
 * DIYTournamentState — typed view of the `diyFileBufferCopy` tournament
 * working buffer. Replaces the raw-offset overlay that the Amiga used.
 *
 * On the Amiga (and the legacy SDL layout) this buffer was addressed by
 * literal byte offsets: header at 0x00, a 64-byte team-index/seed list at
 * 0x5A (deliberately overlapping the header's numRounds/subRound/playEach
 * tail), team-ids at 0x9A, group-map at 0x11A, six slots at 0x15A. That
 * overlap is exactly what broke on 64-bit: the `namePtr` widening (4->8)
 * grew DIYCompetition by +4, sliding the *used* header fields
 * competitionFinished (->0x5A) and currentRoundIndex (->0x5C) on top of
 * the team list, so a routine header reset zeroed team entries [0..3]
 * (ROMANIA x5, inflated PL column, panic 0x8034 on round advance).
 *
 * Modelling the buffer as a real struct removes the overlap by
 * construction: `teamIndexOrder` is now a named field the compiler places
 * AFTER the header, so no header write can ever land on it, on any target.
 * The other regions (teamIds/groupMap/slots) become named fields too, so
 * no code addresses this buffer by raw offset anymore.
 *
 * NB the legacy `competitionFileBuffer` tournament backup keeps the old
 * raw-offset layout (it lives in the careerFileBuffer overlay); the
 * SaveDIYTournament / RestoreDIYTournament copies field-map between the
 * two (see diy_competition.c). diyCompetitionStart (the current-stage
 * DIYCompetitionEx) is a separate allocation reached by symbol, not by the
 * old `+0x420` adjacency.
 * ================================================================== */
typedef struct DIYTournamentState {
    DIYCompetition    header;             /* 0x00: competition descriptor */
    /* Pad from the end of the header to 0x9A. This region (0x5A..0x99) was
     * the team-index list's old home, deliberately overlapping the header
     * tail — the source of the 64-bit collision. The list is relocated to
     * `teamIndexOrder` (past the slots) so nothing lives on the header tail
     * anymore. The pad sizes itself per arch (sizeof(DIYCompetition) is
     * 0x60 on 32-bit, 0x68 on 64-bit) so teamIds/groupMap/slots keep their
     * HISTORICAL byte offsets — every access to them converts to a named
     * field with identical behaviour, and the slot-passing convention
     * (`slot + 0x15A`) and the competitionFileBuffer field-map copies stay
     * correct. Only `teamIndexOrder` actually moves. */
    /* leagueWarningsEnabled (slot-0 SLOT_ALIAS untangle, S6 / decision D-W1):
     * the league competition's flag, sitting at exactly sizeof(DIYCompetition)
     * (0x60 on 32-bit, 0x68 on 64-bit) — the same byte the DIYCompetitionEx view
     * calls minSubstitutes@0x60. Named here (leading member of the old
     * _reserved_5A pad) so league consumers read `tourn.leagueWarningsEnabled`
     * instead of the retired standalone `leagueWarningsEnabled` alias. Host-native
     * u16 (not BE). The reserved pad shrinks by 2 so teamIds stays pinned @0x9A. */
    uint16_t          leagueWarningsEnabled; /* == sizeof(DIYCompetition) */
    uint8_t           _reserved_5C[0x9A - sizeof(DIYCompetition) - 2];
    uint16_t          teamIds[64];        /* 0x9A: BE-packed {country,ordinal} ids (was diyTournamentTeamIds) */
    uint8_t           groupMap[64];       /* 0x11A: team-index -> group letter */
    DIYTournamentSlot slots[6];           /* 0x15A: the six tournament-stage slot records (stride 0x76) */
    uint8_t           teamIndexOrder[64]; /* 0x41E: seed/shuffle order — RELOCATED off the header (was 0x5A) */
} DIYTournamentState;

/* Pin the historical offsets on both arches so the layout can't silently
 * drift (the cfb copies and slot-passing depend on these). */
typedef char _diytourn_warn_off_check  [(offsetof(DIYTournamentState, leagueWarningsEnabled) == sizeof(DIYCompetition)) ? 1 : -1];
typedef char _diytourn_teamids_off_check[(offsetof(DIYTournamentState, teamIds)  == 0x9A)  ? 1 : -1];
typedef char _diytourn_grpmap_off_check [(offsetof(DIYTournamentState, groupMap) == 0x11A) ? 1 : -1];
typedef char _diytourn_slots_off_check  [(offsetof(DIYTournamentState, slots)    == 0x15A) ? 1 : -1];

/* ==================================================================
 * CompetitionSlotBuffer — the UNTANGLED career competition-slot buffer.
 *
 * Replaces the legacy careerFileBuffer-overlay slot regions
 * (competitionFileBuffer / competition/tertiary/euroCups/diyCompetition-
 * Context) — tightly-packed, `.equ`-aliased byte blobs where teamIndexOrder
 * had to overlay the DIYCompetition header tail at raw 0x5A (which the
 * 64-bit namePtr growth slid `currentRoundIndex`@0x5C into, corrupting it
 * and skipping the euro-cup cache fill). Here every field has its own home:
 *
 *   - `tourn` IS a DIYTournamentState, so teamIndexOrder lives at its proper
 *     0x41E — no header overlap, `currentRoundIndex` survives.
 *   - `stageSnapshot` is the current-stage diyCompetitionStart image that the
 *     legacy layout stashed at cfb +0x420 (and which spilled into adjacent
 *     overlay globals by design). Now a real trailing field. It must hold the
 *     WHOLE DIYCompetitionEx image including the relocated leagueStandings
 *     tail, so it is sized DIY_STAGE_SNAPSHOT_LEN == sizeof(DIYCompetitionEx)
 *     (locked by a static assert after the DIYCompetitionEx definition).
 *
 * The DIYCompetition header fields the old aliases pointed at
 * (competitionName@0x04, resultCode/winner@0x3A, slotStateA/B/C@0x3E.., the
 * avgSkill/bonus byte@0x30) keep their header offsets, so those aliases just
 * re-base onto `tourn.header` with identical semantics.
 * ================================================================== */
/* Full diyCompetitionStart image length, through the relocated leagueStandings
 * tail (the standings untangle moved them from the 0x2B4 cupTeamIds[51] overlay
 * to a trailing field). CompetitionSlotBuffer is defined before DIYCompetitionEx
 * so we can't use sizeof() here directly; the post-DIYCompetitionEx static
 * assert keeps this in lockstep with sizeof(DIYCompetitionEx). Was 0x734 (the
 * old standings-end, when standings overlaid the cup scratch). */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define DIY_STAGE_SNAPSHOT_LEN  0xA48u   /* 32-bit: 0x5C8 struct + 0x480 standings */
#else
#define DIY_STAGE_SNAPSHOT_LEN  0xA50u   /* 64-bit: 0x5D0 struct + 0x480 standings */
#endif
typedef struct __attribute__((packed, aligned(2))) CompetitionSlotBuffer {
    DIYTournamentState tourn;              /* 0x000: header + teamIds + groupMap + slots + teamIndexOrder */
    uint8_t            stageSnapshot[DIY_STAGE_SNAPSHOT_LEN]; /* current-stage diyCompetitionStart image (was cfb +0x420), incl. relocated leagueStandings */
} CompetitionSlotBuffer;

typedef char _compslotbuf_tourn_off_check    [(offsetof(CompetitionSlotBuffer, tourn) == 0) ? 1 : -1];
typedef char _compslotbuf_snapshot_off_check [(offsetof(CompetitionSlotBuffer, stageSnapshot) == sizeof(DIYTournamentState)) ? 1 : -1];

/* ==================================================================
 * DIYCompetitionEx_CupView — 0xEA bytes (matchData cup interpretation)
 * Region at parent 0x64..0x14D when the competition is a cup
 * (competitionType == 1 or compId in {1, 2, 4}).
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) DIYCompetitionEx_CupView {
    uint16_t currentMatchIndex;         /* 0x00 (parent 0x64) */
    uint16_t roundLegs[6];              /* 0x02 (parent 0x66): cup roundLegs[0..5] */
    uint16_t roundExtraTime[6];         /* 0x0E (parent 0x72): cup roundExtraTime[0..5] */
    uint16_t roundPenalties[6];         /* 0x1A (parent 0x7E): cup roundPenalties[0..5] */
    uint8_t  cupRoundResults[100];      /* 0x26 (parent 0x8A): diyCupRoundResults */
    uint8_t  _shuffleIndicesAlias[2];   /* 0x8A (parent 0xEE): aliases diyTeamShuffleIndices */
    uint8_t  _bss_2E01E[62];            /* 0x8C (parent 0xF0): BSS_00_2E01E (purpose unclear) */
    uint16_t leagueTeamIdsAlias[14];    /* 0xCA (parent 0x12E): aliases diyLeagueTeamIds */
    uint16_t competitionFormat;         /* 0xE6 (parent 0x14A): was SWOS2.s diyCompetitionFormat (global alias removed) */
    uint16_t roundFormat;               /* 0xE8 (parent 0x14C): was SWOS2.s diyRoundFormat (global alias removed) */
} DIYCompetitionEx_CupView;

typedef char _diycupview_size_check[(sizeof(DIYCompetitionEx_CupView) == 0xEA) ? 1 : -1];

/* ==================================================================
 * DIYCompetitionEx_LeagueView — 0xEA bytes
 * League competition interpretation of the match data region (0x64-0x14E).
 * Used when competitionType == 0 (league) or compId == 0.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) DIYCompetitionEx_LeagueView {
    uint16_t leagueMinSubstitutes;      /* 0x00 (parent 0x64) */
    uint16_t leagueMaxSubstitutes;      /* 0x02 (parent 0x66) */
    uint16_t hasGroupsDiy;              /* 0x04 (parent 0x68) */
    uint16_t totalMatchesPerSection;     /* 0x06 (parent 0x6A): BSS_00_2DF5E reset value */
    uint16_t totalMatchesPerSectionLive; /* 0x08 (parent 0x6C): BSS_00_2DF5E live counter, decrements per match */
    /* 0x0A..0x8A (parent 0x6E): aliases diyTeamEntryOffsetsByIndex; sorted by standings,
     * value/18 = team index -- one per place of the league, 24 in 96/97's largest and more in
     * a world's. It was declared [14] with the cup view's cupRoundResults[100] after it, and
     * a league's places past the 14th ran on into that memory, as in the original: undefined
     * in C, and GCC took the bound at its word -- a loop over a 20-club table "proved" the
     * table had at most 14 places and folded `n >= 17` to false (world_playoff.c, 2026-10-27).
     * Declared whole now: the same bytes, every later offset unchanged; the league view never
     * used cupRoundResults. */
    uint16_t teamEntryOffsets[64];
    uint8_t  shuffleAndPad[64];         /* 0x8A (parent 0xEE): diyTeamShuffleIndices(2) + BSS_00_2E01E(62) */
    uint16_t leagueTeamIds[14];         /* 0xCA (parent 0x12E): aliases diyLeagueTeamIds. NB: a
                                         * league of more than 14 runs on past this array (into
                                         * competitionFormat and past the match data), as the
                                         * original's overlay did -- index it through a pointer
                                         * only; the same GCC hazard as teamEntryOffsets above. */
    uint16_t competitionFormat;         /* 0xE6 (parent 0x14A): was SWOS2.s diyCompetitionFormat (global alias removed) */
    uint16_t roundFormat;               /* 0xE8 (parent 0x14C): was SWOS2.s diyRoundFormat (global alias removed) */
} DIYCompetitionEx_LeagueView;

typedef char _diyleagueview_size_check[(sizeof(DIYCompetitionEx_LeagueView) == 0xEA) ? 1 : -1];

/* ==================================================================
 * DIYCompetitionEx_MatchData — 0xEA bytes (union)
 * Region at offset 0x64 within DIYCompetitionEx.
 * Same memory interpreted differently for cup vs league competitions.
 * ================================================================== */
typedef union __attribute__((packed, aligned(2))) DIYCompetitionEx_MatchData {
    DIYCompetitionEx_CupView cup;
    DIYCompetitionEx_LeagueView league;
    uint8_t raw[0xEA];
} DIYCompetitionEx_MatchData;

typedef char _diymatchdata_size_check[(sizeof(DIYCompetitionEx_MatchData) == 0xEA) ? 1 : -1];

/* ==================================================================
 * DIYCompetitionEx_LeagueSectionState — 136 bytes (0x88)
 * BSS_00_2E13E broken out: per-section progression counters and the
 * phase-start-month table used by the league progression path in
 * DIY_RecomputeCompetitionDerivedFields and friends.
 * ================================================================== */
/* A world league division's mid-season split (src/league_split.c), while it runs: its days'
 * weeks in the match days' table (a split league has at most LEAGUE_SPLIT_MAX_DAYS days), its
 * state in the table's rows a league of LEAGUE_SPLIT_MAX_CLUBS leaves unused (DIYCompetitionEx
 * leagueSplit), so it is copied with the league, as the SWOS 96/97 Mod keeps its own (split.py's
 * state, 30 rows since its 4.1: Argentina's and the MLS's 30 clubs); saved by name
 * (save_format_json.c "league_split"). Every league init clears its mark. */
#define LEAGUE_SPLIT_MAX_DAYS  48
#define LEAGUE_SPLIT_MAX_CLUBS 32
#define LEAGUE_SPLIT_MAGIC     0x50FE   /* (FEh: never a week) */
typedef struct __attribute__((packed)) LeagueSplitState {
    uint16_t magic;                     /* LEAGUE_SPLIT_MAGIC: a split league */
    uint8_t  entry;                     /* its table: World_LeagueSplits()'s index + 1 */
    uint8_t  frozen;                    /* the stage (+ 1) whose groups are set; 0: before the split */
    uint8_t  n;                         /* its clubs */
    uint8_t  compId;                    /* its league's (the struct holds another competition
                                         * after it -- a European group stage -- whose init
                                         * leaves these bytes: the mark alone is not enough) */
    uint8_t  placeRow[LEAGUE_SPLIT_MAX_CLUBS];  /* the table at the split: place -> row */
    uint8_t  group[LEAGUE_SPLIT_MAX_CLUBS];     /* row -> its group */
    uint8_t  pre[LEAGUE_SPLIT_MAX_CLUBS];       /* row -> its points before rounding (a tie-break) */
} LeagueSplitState;

/* A world's 36-club league phase while it runs (src/league_phase.c; include/league_phase.h):
 * the draw's matches (home row, away row) matchday by matchday, each row's away goals and away
 * wins and disciplinary points (the table's tie-breaks). In the league table's rows 36.. */
typedef struct __attribute__((packed)) LeaguePhaseState {
    uint8_t cal[144][2];
    uint8_t awayGoals[36];
    uint8_t awayWins[36];
    uint8_t discipline[36];
} LeaguePhaseState;

typedef struct __attribute__((packed, aligned(2))) DIYCompetitionEx_LeagueSectionState {
    uint16_t halfGroupCount;            /* 0x00 (parent 0x1C6) */
    uint16_t matchesPerHalf;            /* 0x02 (parent 0x1C8) */
    uint16_t halfGroupsCounter;         /* 0x04 (parent 0x1CA): == matchesPerHalf triggers boundary */
    uint16_t sectionMatchesProgress;    /* 0x06 (parent 0x1CC) */
    union __attribute__((packed)) {
        uint8_t  phaseStartMonths[128]; /* 0x08 (parent 0x1CE..0x24D): viewRandomPhase[phaseIdx] source */
        uint8_t  splitWeeks[LEAGUE_SPLIT_MAX_DAYS]; /* (a split league's days, its state in the rows) */
    };
} DIYCompetitionEx_LeagueSectionState;

typedef char _diysecstate_size_check[
    (sizeof(DIYCompetitionEx_LeagueSectionState) == 0x88) ? 1 : -1];

/* ==================================================================
 * DIYCompetitionEx — 0xA48 bytes (32-bit) / 0xA50 (64-bit), the extended
 * runtime DIY competition record at &diyCompetitionStart.
 *
 * Layout matches the SWOS2.s `ds.b` walk starting at line 2840
 * (diyCompetitionStart) for the first 0x5C8 bytes; the backing buffer
 * (`diyCompetitionStartBuf`) is 7136 bytes.
 *
 * UNTANGLE (roadmap step 3): the league standings table written by
 * clearLeagueTableStats used to OVERLAY cupTeamIds[51] (offset 0x2B4),
 * extending 1152 bytes to 0x734 over teamGridPairings and the whole
 * bracketScratch (incl. teamOffsetTable). For a type-2 hybrid that ran
 * both a league and a cup (sub-)slot in one block, the league standings
 * write zeroed the cup scratch — the clobber that forced the
 * teamOffsetTable rebuilds. The standings are now the explicit trailing
 * `leagueStandings[64]` field (offset 0x5C8 / 0x5D0), clear of the cup
 * scratch. See docs/TEAMOFFSETTABLE_OVERLAY_UNTANGLE.md.
 *
 * `diyFileBufferCopy` is a SEPARATE buffer that shares only the first
 * 0x60 bytes of layout (DIYCompetition base). Its post-base region has
 * a different adjacent-globals layout (BSS_00_2DB6A, then
 * diyTournamentTeamIds), so this struct does NOT model it past byte
 * 0x60. The 6-element DIYTournamentSlot array embedded inside
 * diyTournamentTeamIds is reached via raw pointer arithmetic, not
 * through this struct.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) DIYCompetitionEx {
    DIYCompetition base;                /* 0x00..0x5F: competition header */

    /* --- 0x60..0x63: SWOS2.s named diyMinSubstitutes / diyPointsForAWin --- */
    uint16_t minSubstitutes;            /* 0x60: diyMinSubstitutes (also overridden = playEachTeam in derived-fields recompute) */
    uint16_t pointsForAWin;             /* 0x62: diyPointsForAWin */

    /* --- 0x64..0x14D: cup/league shared region (aliased view) --- */
    DIYCompetitionEx_MatchData matchData;

    /* --- 0x14E..0x1BD: BSS_00_2E0AE (112 bytes) — two sequential sub-regions
     *     named by use (cup mode vs league mode never overlap). Same 112 bytes,
     *     layout byte-identical to the old uint8_t[112] blob. --- */
    uint16_t cupAggregateScores[48];        /* 0x14E: cup two-leg 2nd-leg aggregate, packed goals by matchIdx (save uses [0..17]) */
    uint16_t qualifiedTeamsByDivision[8];   /* 0x1AE: league promotion/relegation qualified team ids, by divIndex (was _bss_2E0AE+0x60) */

    /* --- 0x1BE..0x1C5: SWOS2.s named diy{MatchesPlayed,LeagueCurrentSlot,
     *                                   LeagueMatchProgress,LeagueTotalMatches} --- */
    uint16_t matchesPlayedCount;        /* 0x1BE: was SWOS2.s diyMatchesPlayedCount (global alias removed) */
    uint16_t leagueCurrentSlot;         /* 0x1C0: diyLeagueCurrentSlot (also section-progression flag) */
    uint16_t leagueMatchProgress;       /* 0x1C2: diyLeagueMatchProgress (also phase index) */
    uint16_t leagueTotalMatches;        /* 0x1C4: diyLeagueTotalMatches (also matchesPerSeason) */

    /* --- 0x1C6..0x24D: BSS_00_2E13E broken out (league section state) --- */
    DIYCompetitionEx_LeagueSectionState leagueSectionState;

    /* --- 0x24E..0x2CD: SWOS2.s named diyCupTeamIds[64] --- */
    uint16_t cupTeamIds[64];            /* 0x24E */

    /* --- 0x2CE..0x30D: SWOS2.s named diyTeamGridPairings[64] --- */
    uint8_t teamGridPairings[64];       /* 0x2CE */

    /* --- 0x30E..0x5C7: SWOS2.s named diyTeamShuffleOrder[698].
     *
     * The first 0x20 bytes hold raw shuffled team-byte indices, but the
     * rest of the array is reused as a scratch region for bracket-state
     * counters, the cup-progression month table, and the per-team
     * selTeamsPtr offset cache used by ApplyHomeAwaySwap and friends.
     * Anonymous union so existing `teamShuffleOrder[i]` callers keep
     * working while typed scratch-field access is also available via
     * `bracketScratch.<field>`. */
    union {
        uint8_t teamShuffleOrder[698];                   /* 0x30E */
        struct __attribute__((packed, aligned(2))) {
            uint8_t  shuffleHead[0x20];                  /* 0x30E..0x32D: raw shuffle bytes */
            uint16_t secondLegCounter;                    /* 0x32E */
            uint16_t groupSectionFlag;                    /* 0x330 */
            uint16_t matchCount;                          /* 0x332 */
            uint16_t totalMatches;                        /* 0x334 */
            uint16_t curMonthIdx;                         /* 0x336 */
            uint8_t  progTable[0xC];                      /* 0x338..0x343: month-progression bytes */
            int32_t  teamOffsetTable[64];                 /* 0x344..0x443: per-team byte offsets from selTeamsPtr (NOT pointers despite the historical "PtrTable" name). Signed so `&selTeamsPtr + offset` sign-extends on 64-bit when teamPtr is in BSS *below* selTeamsPtr (e.g. careerTeam). */
            uint8_t  scratchTail[0x184];                  /* 0x444..0x5C7: bracket scratch tail */
        } bracketScratch;
    };

    /* --- 0x5C8 (0x5D0 on 64-bit): league standings, RELOCATED here from the
     *     old cupTeamIds[51] overlay (offset 0x2B4) by the step-3 untangle.
     *     64 × 18-byte DIYTeamStats rows written by clearLeagueTableStats.
     *     Now a real field, so a league (sub-)slot's standings write no longer
     *     clobbers a cup (sub-)slot's teamGridPairings / teamOffsetTable.
     *     Reached via DIY_STANDINGS_OFFSET / diy_standings_row (offsetof-based,
     *     so the 32/64-bit struct growth tracks automatically). --- */
    union __attribute__((packed)) {
        DIYTeamStats leagueStandings[64];   /* 0x5C8 / 0x5D0 .. +0x480 */
        struct __attribute__((packed)) {    /* a world's 36-club league phase (src/league_phase.c):
                                             * its own figures in the rows a stage of 36 leaves
                                             * unused, copied and saved with the contest, as the
                                             * SWOS 96/97 Mod keeps them (lp36.py: rows 36+) */
            DIYTeamStats     leaguePhaseRows[36];
            LeaguePhaseState leaguePhase;
        };
        struct __attribute__((packed)) {    /* a world league's split (src/league_split.c): its
                                             * state in the rows a league of 32 leaves unused */
            DIYTeamStats     leagueSplitRows[LEAGUE_SPLIT_MAX_CLUBS];
            LeagueSplitState leagueSplit;
        };
    };
} DIYCompetitionEx;

typedef char _leaguesplit_fits_check[(LEAGUE_SPLIT_MAX_CLUBS * sizeof(DIYTeamStats) + sizeof(LeagueSplitState) <=
                                      64 * sizeof(DIYTeamStats)) ? 1 : -1];

typedef char _leaguephase_fits_check[(36 * sizeof(DIYTeamStats) + sizeof(LeaguePhaseState) <=
                                      64 * sizeof(DIYTeamStats)) ? 1 : -1];

/* DIYCompetitionEx embeds DIYCompetition (which has 1 pointer);
 * +4 on 64-bit via that. The DIYCompetitionEx_* view variants are
 * pointer-free. The trailing leagueStandings[64] adds 0x480 (1152). */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _diycompetitionex_size_check[(sizeof(DIYCompetitionEx) == 0xA48) ? 1 : -1];
#else
typedef char _diycompetitionex_size_check[(sizeof(DIYCompetitionEx) == 0xA50) ? 1 : -1];
#endif

/* Word count for a "through teamOffsetTable" cup-state copy of the DIYCompetitionEx
 * image — used by the comp-slot store/load (career_teams.c) and the
 * competitionFileBuffer<->diyCompetitionStart copies (diy_competition.c). offsetof
 * is the END of bracketScratch.teamOffsetTable (scratchTail starts immediately
 * after). MUST be offsetof-derived, NOT the historical hardcoded 0x222: on 64-bit
 * DIYCompetition's namePtr grew (+8), pushing teamOffsetTable[62] (0x444) and
 * [63] (0x448) PAST the old 0x444-byte (0x222-word) boundary, so a hardcoded copy
 * dropped them — those cup slots then resolved to selTeams[0] (the player) and
 * double-booked a fixture into a phantom PLAY MATCH menu. Yields 0x222 on 32-bit
 * (Amiga/ARM, unchanged) and 0x226 on 64-bit. */
#define DIY_CUP_COPY_WORDS  ((int16_t)(offsetof(DIYCompetitionEx, bracketScratch.scratchTail) / 2))
/* stageSnapshot must hold the full DIYCompetitionEx image (incl. the relocated
 * standings). Lock the two together so a future struct change can't silently
 * truncate the per-slot stage snapshot or the tournament field-map copy. */
typedef char _diystagesnapshot_len_check[
    (DIY_STAGE_SNAPSHOT_LEN == sizeof(DIYCompetitionEx)) ? 1 : -1];
/* Boundary of the former _bss_2E0AE blob: qualifiedTeamsByDivision must follow
 * the 96-byte cupAggregateScores exactly (target-independent relative check). */
typedef char _diycompex_2e0ae_split_check[
    (offsetof(DIYCompetitionEx, qualifiedTeamsByDivision) ==
     offsetof(DIYCompetitionEx, cupAggregateScores) + 96) ? 1 : -1];

/* ==================================================================
 * SeasonInformations — 106 bytes per row, pack(2)
 *
 * Per-season summary used on the season-end recap screen. The
 * seasonInformationsArray global holds 20 of these (one per season
 * the player has played). Pointer fields (leagueStrPtr / strPtr1-3)
 * are runtime-derived comp-name strings — the JSON save emits them
 * as bare string values via emit_str_pool_field, re-interned into
 * the string pool on load.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) SeasonInformations {
    uint16_t clubNo;                  /* 0x00 */
    int8_t   clubName[18];            /* 0x02 */
    uint16_t trophyFlags;             /* 0x14 */
    uint8_t *leagueStrPtr;            /* 0x16: ptr to league name */
    uint8_t *strPtr1;                 /* 0x1A: ptr to cup name */
    uint8_t *strPtr2;                 /* 0x1E: ptr to cup name */
    uint8_t *strPtr3;                 /* 0x22: ptr to cup name */
    uint16_t bestScorerNumGoals;      /* 0x26 */
    int8_t   bestScorerName[23];      /* 0x28 */
    int8_t   secondScorerName[23];    /* 0x3F */
    int8_t   comp1Result;             /* 0x56 */
    int8_t   comp2Result;             /* 0x57 */
    int8_t   comp3Result;             /* 0x58 */
    int8_t   comp4Result;             /* 0x59 */
    uint16_t totalGoalsScored;        /* 0x5A */
    uint16_t totalGoalsTaken;         /* 0x5C */
    uint8_t  totalGames;              /* 0x5E */
    uint8_t  totalWins;               /* 0x5F */
    uint8_t  totalDrawn;              /* 0x60 */
    uint8_t  totalLost;               /* 0x61 */
    uint8_t  friendlyWins;            /* 0x62 */
    uint8_t  friendlyDraws;           /* 0x63 */
    uint8_t  friendlyLost;            /* 0x64 */
    uint8_t  tourWins;                /* 0x65 */
    uint8_t  tourDraws;               /* 0x66 */
    uint8_t  tourLost;                /* 0x67 */
    uint8_t  tournamentStatus;        /* 0x68 */
    int8_t   managerStatus;           /* 0x69 */
} SeasonInformations;

/* SeasonInformations has 4 pointer fields (leagueStrPtr, strPtr1/2/3);
 * +16 on 64-bit. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _seasoninformations_size_check[(sizeof(SeasonInformations) == 106) ? 1 : -1];
#else
typedef char _seasoninformations_size_check[(sizeof(SeasonInformations) == 122) ? 1 : -1];
#endif

/* ==================================================================
 * JobOffer — 26 bytes, pack(2)
 * Queue entry for career mode job offers.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) JobOffer {
    uint16_t teamNo;               /* 0 */
    int8_t   teamName[18];         /* 2: 0xFF = unset */
    uint32_t amountOffered;        /* 20 */
    uint16_t reserved;             /* 24: confirmed no reads (tree + SWOS2.s) — JOBOFFER_UNKN
                                    * EQU 24 defined but never referenced; the offer builder
                                    * only writes teamNo/amountOffered/teamName. Trailing pad
                                    * to the 26-byte record size (was unkn). NOTE: the save
                                    * format still emits this as JSON key "unkn" for save
                                    * compat — see s_job_offer_fields in save_format_json.c */
} JobOffer;

typedef char _joboffer_size_check[(sizeof(JobOffer) == 26) ? 1 : -1];

/* ==================================================================
 * TransferOfferEntry — 14 bytes, pack(1)
 * One slot of transferOfferTable[5] (career third-party transfer offers).
 * Pointer-free → identical layout on 32/64-bit; the save format stores
 * the whole table as an opaque 70-byte blob (transfer_offer_table), so
 * this typed view does not change the on-disk format.
 *
 *   wanted*  = players the offering team wants from you
 *   offered* = players they put up in a swap (>=0) or none (<0)
 * negotiationStep1/2Amount double as the haggling progress sentinels
 * 0xFFFF/0xFFFE (see OffersCommon status decode).
 * ================================================================== */
typedef struct __attribute__((packed)) TransferOfferEntry {
    uint16_t teamOffering;            /* 0x00: offering team id (host-native, compared to teamNumber) */
    int8_t   primaryPlayerOrdinal;    /* 0x02: first wanted player */
    int8_t   secondPlayerOrdinal;     /* 0x03: second wanted player; <0 = single-player offer */
    int8_t   swapPlayerOrdinal;       /* 0x04: offered swap player; <0 = none */
    int8_t   extraPlayerOrdinal;      /* 0x05: offered extra player; <0 = none */
    uint16_t offerAmount;             /* 0x06 */
    int16_t  negotiationStep1Amount;  /* 0x08: first improved-offer amount; 0xFFFF/0xFFFE sentinel */
    int16_t  negotiationStep2Amount;  /* 0x0A: second improved-offer amount; 0xFFFF sentinel */
    uint8_t  lifespan;                /* 0x0C: expiry counter, decremented per turn */
    uint8_t  seenFlag;                /* 0x0D: 0 = empty/active slot, 1 = seen */
} TransferOfferEntry;

typedef char _transferofferentry_size_check[(sizeof(TransferOfferEntry) == 14) ? 1 : -1];

/* ==================================================================
 * GoalieFaceState — 10 bytes. Untangled from the goalieFacesBackingBuf
 * adjacency overlay (SWOS2.s 16482..16493 laid 6 separately-named
 * globals back-to-back; the SDL port .equ-aliased them into one BSS
 * run). DetermineGoalieFaces writes each team's main GK face at [0]
 * and the reserve face at [1] of a 2-byte face array. On Amiga the [1]
 * write landed on the *next* named global (the misleadingly-named
 * team2GoalieFaces / team2ReserveGoalieSkinType labels, which had no
 * real code consumers); here it is an in-bounds array element, so the
 * overrun and the misleading labels are both gone.
 * ================================================================== */
typedef struct GoalieFaceState {
    uint8_t team1FaceToPosition[3];  /* 0x00: per-position GK face index, team 1 */
    uint8_t team2FaceToPosition[3];  /* 0x03: per-position GK face index, team 2 */
    uint8_t team1Faces[2];           /* 0x06: [0]=main GK face, [1]=reserve GK face */
    uint8_t team2Faces[2];           /* 0x08: [0]=main GK face, [1]=reserve GK face */
} GoalieFaceState;

/* Pin the layout to the old goalieFacesBackingBuf byte offsets so the
 * conversion is provably byte-identical on every target. */
typedef char _goaliefacestate_size_check   [(sizeof(GoalieFaceState)              == 10) ? 1 : -1];
typedef char _goaliefacestate_t2pos_check  [(offsetof(GoalieFaceState, team2FaceToPosition) == 3) ? 1 : -1];
typedef char _goaliefacestate_t1faces_check[(offsetof(GoalieFaceState, team1Faces)          == 6) ? 1 : -1];
typedef char _goaliefacestate_t2faces_check[(offsetof(GoalieFaceState, team2Faces)          == 8) ? 1 : -1];

/* ==================================================================
 * OfferOrdinals — 14 bytes. Untangled (roadmap step 3) from the
 * offerOrdinalsBackingBuf adjacency overlay (SWOS2.s 96267..96280 laid
 * 7 separately-named `dc.w` globals back-to-back; the SDL port .equ-
 * aliased them into one BSS run). These hold the in-progress transfer
 * offer's parameters — transient menu state, NOT serialized (the saved
 * data is the TransferOfferEntry array, which has its own fields).
 *
 * All seven are host-native uint16/int16 (the original `dc.w`, so there
 * is no BE byte-pair concern on either target — the struct is byte-
 * identical on SDL/LE and Amiga/BE).
 *
 * The four player-ordinal slots (offerPrimaryPlayerOrdinal,
 * numExtraPlayersWanted, offerSwapPlayerOrdinal, offerExtraPlayerOrdinal)
 * MUST stay contiguous in this order: offerAfterDrawCommonShared
 * (transfer_market.c) walks them as
 * `(int16_t *)&offerOrdinals.offerPrimaryPlayerOrdinal` indexing [0..3]
 * to fetch primary / numExtra / swap / extra in turn.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) OfferOrdinals {
    uint16_t teamOffering;                /* 0x00: team id that wants to buy our player */
    uint16_t offerPrimaryPlayerOrdinal;   /* 0x02: the player they want */
    int16_t  numExtraPlayersWanted;       /* 0x04: extra players wanted (>=0) / -1 sentinel */
    int16_t  offerSwapPlayerOrdinal;      /* 0x06: swap player ordinal / -1 */
    int16_t  offerExtraPlayerOrdinal;     /* 0x08: extra player ordinal / -1 */
    uint16_t offerAmount;                 /* 0x0A: amount offered (units of 1000) */
    uint16_t offerNegotiationStep1Amount; /* 0x0C: first negotiation counter-offer */
} OfferOrdinals;

/* Pin the layout to the old offerOrdinalsBackingBuf byte offsets. */
typedef char _offerordinals_size_check [(sizeof(OfferOrdinals)                              == 14) ? 1 : -1];
typedef char _offerordinals_prim_check [(offsetof(OfferOrdinals, offerPrimaryPlayerOrdinal) ==  2) ? 1 : -1];
typedef char _offerordinals_extra_check[(offsetof(OfferOrdinals, offerExtraPlayerOrdinal)   ==  8) ? 1 : -1];
typedef char _offerordinals_amt_check  [(offsetof(OfferOrdinals, offerAmount)               == 10) ? 1 : -1];

/* ==================================================================
 * EuroCupQualSeedBucketBlock — 78 bytes. The four euro-cup qualification
 * seed buckets packed into one struct so they stay contiguous: a UEFA Cup
 * pre-scan walks all four end-to-end up to the 0xFF sentinel in
 * bucket22Full (Career_FillRandomTeamListExcludingSelected). Was four
 * separate globals + `.equ` aliases (euroCupQualSeedBucket15/18/22/22Full);
 * promoted to named fields. All uint8 -> byte-identical on 32/64-bit.
 * Defined in competition_data.c; the consumer table lives in
 * swos_amiga_ptr_imports.c (which can't include competition_data.h), so
 * the typedef lives here in the shared header both files already include.
 * ================================================================== */
typedef struct __attribute__((packed)) EuroCupQualSeedBucketBlock {
    uint8_t bucket15[15];
    uint8_t bucket18[18];
    uint8_t bucket22[22];
    uint8_t bucket22Full[23];
} EuroCupQualSeedBucketBlock;
typedef char _eurocupseedbucket_size_check[(sizeof(EuroCupQualSeedBucketBlock) == 78) ? 1 : -1];
extern EuroCupQualSeedBucketBlock _euroCupQualSeedBucketBlock;

/* ==================================================================
 * NationalTeamRecord — 6 bytes, pack(1)
 * Entry in nationalTeams[]. Indexed by ordinal × 6.
 *
 * countryNumber/teamOrdinal are an Amiga BE byte pair: callers compare
 * the packed id against selTeamNumber via PACK_BE_U16, *not* a host
 * uint16 read.
 *
 * skillOrPrice is read host-native (`*(uint16_t *)(entry+4)`) by the
 * random-pick filters — see _PickRandomNationalTeamId / job_offers
 * searchNational. Keep that behaviour unchanged here; if it turns out
 * to be a latent endian bug, fix it separately and document.
 * ================================================================== */
typedef struct __attribute__((packed)) NationalTeamRecord {
    uint8_t  countryNumber;      /* 0 */
    uint8_t  teamOrdinal;        /* 1 */
    uint8_t  nationalityCode;    /* 2 */
    uint8_t  teamFileId;          /* 3 */
    uint16_t skillOrPrice;        /* 4..5 — host-native, see note above */
} NationalTeamRecord;

typedef char _nationalteamrecord_size_check[(sizeof(NationalTeamRecord) == 6) ? 1 : -1];

/* ==================================================================
 * TeamSelectionKeyCache — variable-length entry stream in
 * `teamSelectionKeyCache` (BSS overlay over careerFileBufferBuf+73292).
 *
 * Each entry is a 10-byte header followed by N × 12-byte match slots,
 * with `entryStride = 10 + N*12`. Entries are sorted by `sortKey` and
 * walked by adding `entryStride` to the current entry pointer.
 *
 * The original asm `TeamSelectionKeyCache_GetCurrentSelectedElementPtr`
 * helper returns `entry + diff*12` where `diff = totalElements -
 * remainingElements`. Writes go to elem[+0x0B..+0x15], so the asm
 * pointer is 10 bytes *before* the actual match slot start. The typed
 * helper `TeamSelectionKeyCache_GetCurrentMatchSlot()` returns the
 * properly-aligned `TeamSelectionKeyCacheMatchSlot *`.
 *
 * Layout reverse-engineered from SWOS2.s lines 59549-59656 (entry
 * build), 60046-60054 (match-result write), 60723-60738
 * (GetCurrentSelectedElementPtr), and slot-build reads at
 * diy_competition.c:2776+ (compId/roundIdx lookup).
 * ================================================================== */
typedef struct __attribute__((packed)) TeamSelectionKeyCacheMatchSlot {
    uint8_t  pairType;          /* +0  (elem[0x0A]) — 0/1/2 fixture type */
    uint8_t  matchFlags;        /* +1  (elem[0x0B]) — written post-match */
    uint16_t teamId1;           /* +2  (elem[0x0C]) — host-native */
    uint16_t teamId2;           /* +4  (elem[0x0E]) — host-native */
    uint16_t packedGoals;       /* +6  (elem[0x10]) — high=team1, low=team2 */
    uint16_t aggregate;         /* +8  (elem[0x12]) */
    uint16_t penaltyScore;      /* +10 (elem[0x14]) */
} TeamSelectionKeyCacheMatchSlot;

typedef char _tskc_slot_size_check[(sizeof(TeamSelectionKeyCacheMatchSlot) == 12) ? 1 : -1];

typedef struct __attribute__((packed)) TeamSelectionKeyCacheEntry {
    uint8_t  compSlot;            /* +0 — competition slot cache key (matched in lookup) */
    uint8_t  sortKey;           /* +1 — InsertSpaceSorted insertion key */
    uint16_t entryStride;       /* +2 — host-native; = 10 + numTeams*12 */
    uint8_t  phaseFlag;         /* +4 — 0 == unprocessed */
    uint8_t  totalElements;     /* +5 — count of match slots */
    int8_t   remainingElements; /* +6 — slots left to play (< 0 means none) */
    uint8_t  roundIdx;          /* +7 — cup round index (cup mode) */
    uint8_t  legFlag;           /* +8 — leg/format variant: 0 single-leg,
                                 *      1/2 = first/second leg of a two-leg tie,
                                 *      3 = replay. Written by the cup-bracket
                                 *      builder TeamSelectionKeyCache_BuildFromCompetitionBlock
                                 *      (career_teams.c: `out[8] = N` after each
                                 *      addNextRoundCupEntry). Read at
                                 *      diy_competition.c:2804+. */
    uint8_t  cupRoundStringIdx; /* +9 — cup round string index */
    /* +10..: TeamSelectionKeyCacheMatchSlot slots[totalElements] */
} TeamSelectionKeyCacheEntry;

typedef char _tskc_entry_size_check[(sizeof(TeamSelectionKeyCacheEntry) == 10) ? 1 : -1];

/* ==================================================================
 * EditTeamsSpriteCopy — MENU_CMD_SPRITE_COPY descriptor for the
 * edit-teams kit-colour preview. Was a uint8_t[36] built by the
 * EDIT_TEAMS_BUF LE-byte macro + `.equ editTeams* / editTeams*ColTable`
 * aliases (swos_globals.c). HOST-NATIVE uint16 fields so the handler's
 * `*(uint16_t*)` read (menu_system.c:774) is correct on BOTH LE and BE —
 * the old LE-byte init byte-swapped on the BE Amiga (vs SWOS2.s `dc.w`
 * head + `dc.b` {0,N} BE-word colTable). See project_edit_teams_buf_endian_bug.
 * ================================================================== */
typedef struct __attribute__((packed, aligned(2))) EditTeamsSpriteCopy {
    uint16_t srcSprite;     /* +0: source sprite  (dc.w SPR_BIG_*) */
    uint16_t dstSprite;     /* +2: dest sprite    (dc.w SPR_EDIT_TEAMS_*) */
    uint16_t colTable[16];  /* +4: palette-remap colour table */
} EditTeamsSpriteCopy;
typedef char _editteams_sc_size_check[(sizeof(EditTeamsSpriteCopy) == 36) ? 1 : -1];

/* ==================================================================
 * MarketPlayer — 42 bytes, pack(1)
 * Career foreign market player entry.
 * ================================================================== */
typedef struct MarketPlayer {
    uint16_t   teamId;             /* 0 */
    int8_t     playerOrdinal;      /* 2 */
    int8_t     padding;            /* 3 */
    PlayerFile player;             /* 4 (38 bytes) */
} MarketPlayer;

typedef char _marketplayer_size_check[(sizeof(MarketPlayer) == 42) ? 1 : -1];

/* ==================================================================
 * PoolPlayer — 58 bytes, pack(1)
 * Pool player database entry (poolplyr.dat).
 * ================================================================== */
typedef struct PoolPlayer {
    uint16_t   teamId;             /* 0 */
    int8_t     playerOrdinal;      /* 2 */
    int8_t     teamName[17];       /* 3 */
    PlayerFile player;             /* 20 (38 bytes) */
} PoolPlayer;

typedef char _poolplayer_size_check[(sizeof(PoolPlayer) == 58) ? 1 : -1];

/* ==================================================================
 * PlayerPositions — 35 bytes, pack(1)
 * 5 ball-location zones × 7 packed position bytes per zone.
 * ================================================================== */
typedef struct PlayerPositions {
    uint8_t positions[5][7];       /* packed x/y nibbles */
} PlayerPositions;

typedef char _playerpositions_size_check[(sizeof(PlayerPositions) == 35) ? 1 : -1];

/* ==================================================================
 * TeamTactics — 370 bytes, pack(1)
 * Formation/tactics definition: name + 10 outfield player positions
 * (5 ball zones × 7 quadrants each) + unk pairing table + redirect.
 * ================================================================== */
typedef struct TeamTactics {
    int8_t           name[9];              /* 0: tactic name */
    PlayerPositions  positions[10];        /* 9: 10 players × 35 bytes = 350 */
    uint8_t          pairIds[10];          /* 359: per-outfield-player pair/marking id —
                                            * one byte per the 10 outfield players; equal ids
                                            * = a linked pair, 0xFF/-1 = unlinked. Read+written
                                            * by edit_tactics.c (EditTactics_FindFreePairId /
                                            * _ClearPairId / swap-paired-players / row colour).
                                            * SWOS2.s TEAMTACTICS_UNKTABLE EQU 359, init data
                                            * SWOS2.s:47291+ ({0,-1,-1,0,1,...}). Was unkTable */
    uint8_t          ballOutOfPlayTactics; /* 369: redirect tactic index */
} TeamTactics;

typedef char _teamtactics_size_check[(sizeof(TeamTactics) == 370) ? 1 : -1];

/* ==================================================================
 * CountryNumbers — country identifier constants
 * ================================================================== */

typedef uint8_t CountryNumbers;

#define CN_ALBANIA          0
#define CN_AUSTRIA          1
#define CN_BELGIUM          2
#define CN_BULGARIA         3
#define CN_CROATIA          4
#define CN_CYPRUS           5
#define CN_CZECH_REPUBLIC   6
#define CN_DENMARK          7
#define CN_ENGLAND          8
#define CN_ESTONIA          10
#define CN_FAROE_ISLANDS    11
#define CN_FINLAND          12
#define CN_FRANCE           13
#define CN_GERMANY          14
#define CN_GREECE           15
#define CN_HUNGARY          16
#define CN_ICELAND          17
#define CN_REPUBLIC_IRELAND 18
#define CN_ISRAEL           19
#define CN_ITALY            20
#define CN_LATVIA           21
#define CN_LITHUANIA        22
#define CN_LUXEMBOURG       23
#define CN_MALTA            24
#define CN_HOLLAND          25
#define CN_NORTHERN_IRELAND 26
#define CN_NORWAY           27
#define CN_POLAND           28
#define CN_PORTUGAL         29
#define CN_ROMANIA          30
#define CN_RUSSIA           31
#define CN_SAN_MARINO       32
#define CN_SCOTLAND         33
#define CN_SLOVENIA         34
#define CN_SPAIN            35
#define CN_SWEDEN           36
#define CN_SWITZERLAND      37
#define CN_TURKEY           38
#define CN_UKRAINE          39
#define CN_WALES            40
#define CN_YUGOSLAVIA       41
#define CN_ALGERIA          42
#define CN_ARGENTINA        43
#define CN_AUSTRALIA        44
#define CN_BOLIVIA          45
#define CN_BRAZIL           46
#define CN_CHILE            48
#define CN_COLOMBIA         49
#define CN_ECUADOR          50
#define CN_EL_SALVADOR      51
#define CN_JAPAN            55
#define CN_MEXICO           60
#define CN_NEW_ZEALAND      62
#define CN_PARAGUAY         64
#define CN_PERU             65
#define CN_SURINAM          66
#define CN_TAIWAN           67
#define CN_SOUTH_AFRICA     69
#define CN_URUGUAY          71
#define CN_CUSTOM           72   /* TEAM.072 — the custom/joke preset teams (customsLeague) */
#define CN_USA              73
#define CN_INDIA            75
#define CN_BELARUS          76
#define CN_VENEZUELA        77
#define CN_SLOVAKIA         78
#define CN_GHANA            79
#define CN_EUROPE           80
#define CN_AFRICA           81
#define CN_SOUTH_AMERICA    82
#define CN_NORTH_AMERICA    83
#define CN_ASIA             84
#define CN_OCEANIA          85

/* ==================================================================
 * TacticsSlotIndex — player slot indices within a formation
 * ================================================================== */

typedef uint8_t TacticsSlotIndex;

#define TSI_GOALKEEPER  0x0
#define TSI_1           0x1
#define TSI_2           0x2
#define TSI_3           0x3
#define TSI_4           0x4
#define TSI_5           0x5
#define TSI_6           0x6
#define TSI_7           0x7
#define TSI_8           0x8
#define TSI_9           0x9
#define TSI_10          0xA

/* ==================================================================
 * TacticsPosition — maps 11 player slots for a formation
 * ================================================================== */

typedef struct __attribute__((packed)) {
    TacticsSlotIndex index[11];
} TacticsPosition;

/* ==================================================================
 * TacticsEnum — formation identifiers
 * ================================================================== */

#define TACTIC_DEFAULT    0
#define TACTIC_5_4_1      1
#define TACTIC_4_5_1      2
#define TACTIC_5_3_2      3
#define TACTIC_3_5_2      4
#define TACTIC_4_3_3      5
#define TACTIC_4_2_4      6
#define TACTIC_3_4_3      7
#define TACTIC_SWEEP      8
#define TACTIC_5_2_3      9
#define TACTIC_ATTACK     10
#define TACTIC_DEFEND     11
#define TACTIC_USER_A     12
#define TACTIC_USER_B     13
#define TACTIC_USER_C     14
#define TACTIC_USER_D     15
#define TACTIC_USER_E     16
#define TACTIC_USER_F     17
#define TACTIC_IMPORTED   1000

/* ==================================================================
 * Positions enum — player position types
 * ================================================================== */

#define POS_GOALKEEPER    0x0
#define POS_RIGHT_BACK    0x1
#define POS_LEFT_BACK     0x2
#define POS_DEFENDER      0x3
#define POS_RIGHT_WING    0x4
#define POS_LEFT_WING     0x5
#define POS_MIDFIELDER    0x6
#define POS_ATTACKER      0x7

/* ==================================================================
 * Competition — 9 bytes, base competition descriptor
 * ================================================================== */

typedef struct __attribute__((packed)) {
    /* compId — byte-0 competition identity (the COMPID_* values; see
     * comp_identity.h). UNSIGNED: the original SWOS ids fill 0..127, so a
     * data-only competition (e.g. Copa Libertadores, compId 128) needs the full
     * 0..255 range. The canonical accessor comp_id() already reads this byte as
     * uint8_t; this makes the field itself unsigned so direct reads agree and the
     * Amiga bake (emit_competitions.py) emits `{ 128, ... }` without an int8_t
     * overflow warning. Byte layout/size unchanged (still 1 byte). */
    uint8_t compId;
    int8_t type;
    int8_t teamNumber;       /* TEAM.XXX team-file selector: country/region number for
                              * nationals; 0xFF (255) = .TMD / special team file */
    int8_t beginMonth;       /* season start month, encoded ×8 (monthIdx = value/8) */
    int8_t endMonth;         /* season end month, ×8 (wraps +0x60 if < beginMonth) */
    int8_t reserved1;        /* byte 5: WAS nameTableOffset. Untangle Step 4b — the name table
                              * is now reached by comp_name_table() (switch on type + isPlayoff
                              * → the concrete struct's name[] field), so this self-offset byte
                              * is no longer read. Initializers still carry the old value; it
                              * is inert. */
    int8_t isPlayoff;        /* byte 6: discriminator for the otherwise-ambiguous type==1
                              * records — 1 = Playoffs, 0 = CompetitionCup. Both share
                              * comp.type == 1 (the cup-init path) but have different layouts
                              * (Playoffs name@16 / qualifiedTeamIds; Cup name@20 /
                              * teamSeedList), so type alone can't select the right field. Was
                              * the unused `reserved` pad (confirmed no prior reads); set only
                              * on the Playoffs records. */
    int8_t reserved2;        /* byte 7: WAS teamListOffset. Untangle Step 4b — the team-seed /
                              * qualifier list is now reached by comp_team_seed_list(); this
                              * self-offset byte is no longer read (inert). */
    int8_t seedReorderFlag;  /* was unk4: 0 = shuffle bracket, nonzero = preset seed order */
} Competition;

/* ==================================================================
 * CompLeagueHeader — 13 bytes, league competition header (embedded as
 * CompetitionLeague.header after the consolidation). Was named
 * `CompetitionLeague`; that name now belongs to the merged record below.
 * ================================================================== */

typedef struct __attribute__((packed)) {
    Competition comp;
    int8_t divisionsCount;
    int8_t rounds;
    int8_t pointsForWin;
    int8_t subsAndMaxSubs;   /* was subsReserves: hi nibble = min subs, lo = MAX subs */
} CompLeagueHeader;

/* ==================================================================
 * Division — 6 bytes, division descriptor within a league
 * ================================================================== */

typedef struct __attribute__((packed)) {
    int8_t teams;
    int8_t promoted;
    int8_t promotedPo;
    int8_t relegated;
    int8_t relegatedPo;
    int8_t qualifierLinkOffset; /* was unk1 (byte 5): self-relative byte offset (from the
                                 * Amiga divisions[] base + i*6 + 0x12, i.e. from this field)
                                 * to the division's qualification/promotion link — a
                                 * {Playoffs *, qIdx-pair list} pair. 0 = no link (most
                                 * countries / Premier). Only the English block uses it
                                 * (divisions[1..3] = 46/120/194). Read by
                                 * career_init.c DIY_SelectCompetitionSlot4FromCurrentDivision
                                 * and career_season.c processDiyLeagueQualificationEntry
                                 * (now resolved via typed fields because the 32-bit byte
                                 * offset breaks on 64-bit). SWOS2.s DIVISION_UNK1 EQU 5 /
                                 * Career_ProcessDiyLeagueQualificationEntry:58055 ($12(a2)) */
} Division;

/* ==================================================================
 * CompCupHeader — 14 bytes, cup competition header (embedded as
 * CompetitionCup.header after the consolidation). Was named `CompetitionCup`;
 * that name now belongs to the merged record below.
 * ================================================================== */

typedef struct __attribute__((packed)) {
    Competition comp;
    int8_t numRoundsOverride;   /* was unk1: nonzero overrides runtime numRounds (third-place count) */
    int8_t teams;
    int8_t awayGoalsMode;
    int8_t subsAndPointsForWin; /* was subsReserves: hi nibble = min subs, lo = points-for-win */
    int8_t roundStringBase;     /* was firstRoundNo: cup round-string base (also friendly participant count) */
} CompCupHeader;

/* ==================================================================
 * Round — 3 bytes, round descriptor for CompetitionTournament
 * ================================================================== */

typedef struct __attribute__((packed)) {
    int8_t teamCount;
    int8_t groupCount;
    int8_t teamGroupCount;
} Round;

/* ==================================================================
 * CompTournamentHeader — 34 bytes, type-2 group-tournament header (embedded as
 * CompetitionTournament.header after the consolidation). Was named `CompetitionTournament`;
 * that name now belongs to the merged record below.
 * ================================================================== */

typedef struct __attribute__((packed)) {
    Competition comp;
    int8_t seedingType;         /* was unk1 (byte 9): copied to every stage slot's seedingType
                                 * (slot $160); InitDIYLeagueFromRecord / SWOS2.s:52485 */
    int8_t menuState;           /* was unk2 (byte 0xA): copied to runtime DIYComp menuState
                                 * ($4A); InitDIYLeagueFromRecord / SWOS2.s:52458 */
    int8_t playEachTeam;        /* was unk3 (byte 0xB): copied to every stage slot's playEachTeam
                                 * (slot $166); InitDIYLeagueFromRecord / SWOS2.s:52488 */
    int8_t awayGoalsMode;
    int8_t subsAndPointsForWin; /* was subsReserves: hi nibble = min subs, lo = points-for-win */
    int8_t roundStringBase;     /* was firstRoundNo: round-string base / rounds[0] source */
    Round rounds[6];
    int8_t reserved;            /* byte 33: confirmed no reads (tree + SWOS2.s). The
                                 * InitDIYLeagueFromRecord round loop (SWOS2.s:52480-52531)
                                 * walks the per-round bytes via a1 (rounds region) and reads
                                 * the per-round rule nibbles via a2 = $22(a5) = byte 34
                                 * (roundRuleFlags), so byte 33 is the gap between the two and
                                 * is never read. Initializers do store 0/1 here (euroCup 0,
                                 * worldCup 1) but no consumer reads it. Was unk4 — distinct
                                 * from Competition.seedReorderFlag (byte 8, also formerly
                                 * unk4) which IS read. */
} CompTournamentHeader;

/* ==================================================================
 * CompetitionLeague — merged record (consolidation, docs/STRUCT_CONSOLIDATE.md
 * §4). Collapses all 13 CompetitionLeague_* variants into one: the
 * CompLeagueHeader header + divisions[4] (divisionsCount says how many used) +
 * the per-variant trailing byte + the division name table + the playoff link
 * pointers + the 24-byte qualifier/promotion link data. Arrays are sized to the
 * observed maxima; unused tail entries are zero.
 *
 * Field order matches the old variant layout (header, divisions, byte, name,
 * playoff ptrs, link data), so most instance initializers carry over unchanged
 * (C zero-fills the widened arrays). Only the nameTableOffset byte changes (the
 * name table moved to a fixed offset — see LEAGUE_NAME_TABLE_OFFSET) and the few
 * variants whose trailing arrays sat before the playoff pointers need reordering.
 * ================================================================== */

/* A league's divisions: 96/97 has at most 4; a world more (the World 2026-27's Italy 14,
 * its Serie C and D groups: docs/WORLD_2026_27.md phase 4). Two name strings each. */
#define LEAGUE_MAX_DIVISIONS 16

typedef struct __attribute__((packed)) {
    CompLeagueHeader header;              /* 13 B (was the CompetitionLeague header struct) */
    Division         divisions[LEAGUE_MAX_DIVISIONS];  /* divisionsCount says how many used (96/97: <= 4) */
    uint8_t          reserved1;            /* was the per-variant trailing `unk1` byte */
    char            *name[2 * LEAGUE_MAX_DIVISIONS];   /* division name strings, two each (was text[N], N<=10) */
    char            *playoffLinkPtrs[2];   /* was the char* unk2[2] in _*_2_* variants (→ Playoffs/Leagues) */
    uint8_t          promotionLinkData[24];/* was the int8 unk2[24]/unk3[24]/unk2[8] qualifier-index bytes */
} CompetitionLeague;

/* nameTableOffset (Competition byte 5) for a NAMED merged league: name[] now sits
 * at a fixed offset, so the byte is one constant derived from the real layout
 * (offsetof) instead of a per-variant value. Unnamed leagues keep 0. */
#define LEAGUE_NAME_TABLE_OFFSET ((int8_t)(offsetof(CompetitionLeague, name) - 5))
_Static_assert(offsetof(CompetitionLeague, name) - 5 > 0 &&
               offsetof(CompetitionLeague, name) - 5 < 128,
               "league nameTableOffset must be a positive int8");

/* ==================================================================
 * TeamId — 2 bytes, an Amiga SWOS team identity: {countryNumber, teamOrdinal}.
 *
 * This is the {country, ordinal} byte pair the whole game uses to name a team
 * (cf. TeamFile@0, NationalTeamRecord@0). Unlike a host uint16 read, a two-byte
 * struct is endian-NEUTRAL: countryNumber is always byte 0 and teamOrdinal byte
 * 1 on LE and BE alike, so no byte-swap is needed to access the fields. The
 * packed comparison key the game compares against selTeamNumber/globalTeamNumber
 * is the big-endian word (countryNumber << 8 | teamOrdinal) — use TEAMID_PACK,
 * never `*(uint16_t *)`.
 *
 * Currently applied to the STATIC competition draw lists (CompetitionCup/Cup2
 * teamSeedList). The runtime team-id buffers (diyTournamentTeamIds, cupTeamIds,
 * euroCupCopy/cwc/uefa, qualifier lists) still use raw BE byte pairs — migrating
 * those is a separate, save-format-aware pass (see docs/STRUCT_FIELDS_UNK.md).
 * ================================================================== */
typedef struct __attribute__((packed)) TeamId {
    uint8_t countryNumber;       /* 0: country/region number (BE high byte) */
    uint8_t teamOrdinal;         /* 1: team index within country (BE low byte) */
} TeamId;
typedef char _teamid_size_check[(sizeof(TeamId) == 2) ? 1 : -1];

/* Packed BE comparison key (countryNumber << 8 | teamOrdinal), matching what an
 * m68k `move.w` produced and what isTeamInList/seed-reorder compare against. */
#define TEAMID_PACK(t)  PACK_BE_U16((t).countryNumber, (t).teamOrdinal)

/* ==================================================================
 * Polymorphic competition accessors (untangle Step 4) — reach a competition
 * record's embedded name-pointer table and team-seed/qualifier list WITHOUT the
 * per-call-site `base + 5/7 + selfOffset` byte arithmetic (which was fragile and
 * 64-bit-unsafe). Defined in competition_data.c.
 *
 *  comp_name_table(c)     -> the char *[] name table, or NULL if the record uses
 *                            the default names. Callers index per their own
 *                            stride: leagues use 2 ptrs/division ([i*2]=full,
 *                            [i*2+1]=abbrev); cups/tournaments a single [0].
 *  comp_team_seed_list(c) -> the {country,ordinal} draw / qualifier list
 *                            (TeamId *), or NULL if the record has none (leagues).
 * ================================================================== */
char  **comp_name_table(Competition *c);
TeamId *comp_team_seed_list(Competition *c);

/* ==================================================================
 * CompetitionCup — merged record (consolidation, docs/STRUCT_CONSOLIDATE.md §4).
 * Collapses all 8 CompetitionCup_* variants into one — the old _6_2_64 layout,
 * the largest: CompCupHeader header + per-round rule flags + cup name strings +
 * the embedded team-seed draw list. roundRuleFlags is sized [6] (the max USED;
 * no _10 instance exists), so name[] and teamSeedList[] keep the SAME offsets the
 * _6_2* cups already used (nameTableOffset=15, teamListOffset=21) — most cup
 * initializers carry over unchanged. Only the two _4_2 (japanese) cups, whose
 * text[] sat after rounds[4], need their nameTableOffset bumped 13 -> 15.
 * ================================================================== */

typedef struct __attribute__((packed)) {
    CompCupHeader header;          /* 14 B (was the CompetitionCup header struct) */
    int8_t   roundRuleFlags[6];    /* per-round legs/ET/pen flag bytes */
    char    *name[2];              /* cup name strings (was text[2]) */
    TeamId   teamSeedList[32];     /* embedded competition draw: {country,ordinal} pairs */
} CompetitionCup;

/* Self-offset bytes for the merged layout (Competition byte 5 / byte 7). Both are
 * layout-derived constants; the *_TEAMLIST one subtracts the 64-bit name[2]
 * pointer-growth delta so the STORED byte is identical on 32/64-bit (consumers
 * re-add that delta), matching the historical _6_2* values 15 / 21. */
#define CUP_NAME_TABLE_OFFSET     ((int8_t)(offsetof(CompetitionCup, name) - 5))
#define CUP_TEAMLIST_TABLE_OFFSET ((int8_t)(offsetof(CompetitionCup, teamSeedList) - 7 \
                                   - 2 * (int)(sizeof(char *) - sizeof(uint32_t))))
_Static_assert(CUP_NAME_TABLE_OFFSET == 15 && CUP_TEAMLIST_TABLE_OFFSET == 21,
               "merged CompetitionCup offsets must match the historical _6_2* values");

/* ==================================================================
 * CompetitionTournament — merged record (consolidation, docs/STRUCT_CONSOLIDATE.md §4).
 * Collapses all 11 CompetitionTournament_* variants into one — the old _6_2_64 layout,
 * the largest: CompTournamentHeader header + per-round rule flags + tournament name
 * strings + the embedded team-seed draw list. roundRuleFlags is sized [6] (the
 * max used; the header's Round[6] already fixes the per-stage count), so name[]
 * and teamSeedList[] keep the SAME offsets the _6_2* tournaments already used
 * (nameTableOffset=35, teamListOffset=41) — those initializers carry over
 * unchanged. Only the five _4_2* tournaments, whose text[]/teamSeedList[] sat
 * after roundRuleFlags[4], need their offset bytes bumped (33->35 / 39->41).
 * ================================================================== */

typedef struct __attribute__((packed)) {
    CompTournamentHeader header;         /* 34 B (was the CompetitionTournament header struct) */
    int8_t   roundRuleFlags[6];    /* per-round legs/ET/pen flag bytes */
    char    *name[2];              /* tournament name strings (was text[2]) */
    TeamId   teamSeedList[32];     /* embedded competition draw: {country,ordinal} pairs */
} CompetitionTournament;

/* A world's European cup with a 36-club league phase (src/league_phase.c; the SWOS 96/97 Mod's
 * careerworld.lp_struct): CompetitionTournament's layout, its list 36 long -- type 2, six stages
 * (36 in 2 groups of 18, the play-offs 16, the round of 16, 8, 4, 2). */
typedef struct __attribute__((packed)) {
    CompTournamentHeader header;
    int8_t   roundRuleFlags[6];
    char    *name[2];
    TeamId   teamSeedList[36];
} CompetitionLeaguePhase;

_Static_assert(offsetof(CompetitionLeaguePhase, teamSeedList) == offsetof(CompetitionTournament, teamSeedList),
               "a league-phase cup is a tournament record with a longer list");

/* Self-offset bytes for the merged layout (Competition byte 5 / byte 7). Both are
 * layout-derived constants; the *_TEAMLIST one subtracts the 64-bit name[2]
 * pointer-growth delta so the STORED byte is identical on 32/64-bit (consumers
 * re-add that delta), matching the historical _6_2* values 35 / 41. */
#define TOURNAMENT_NAME_TABLE_OFFSET     ((int8_t)(offsetof(CompetitionTournament, name) - 5))
#define TOURNAMENT_TEAMLIST_TABLE_OFFSET ((int8_t)(offsetof(CompetitionTournament, teamSeedList) - 7 \
                                    - 2 * (int)(sizeof(char *) - sizeof(uint32_t))))
_Static_assert(TOURNAMENT_NAME_TABLE_OFFSET == 35 && TOURNAMENT_TEAMLIST_TABLE_OFFSET == 41,
               "merged CompetitionTournament offsets must match the historical _6_2* values");

/* ==================================================================
 * Playoff and Leagues helper structs
 * ================================================================== */

/* Playoffs — merged Playoffs_8/_4 (consolidation step, see docs/STRUCT_CONSOLIDATE.md
 * §4b). qualifiedTeamIds sized to the max (8); the former Playoffs_4 instances
 * (scottish/uruguayan) leave the last 4 entries unused.
 *
 * The former opaque `playoffHeader[16]` is a CUP competition record: a
 * CompCupHeader (14 B) + roundRuleFlags[2]. comp.type == 1 (cup); the player's
 * qualified teams contest a knockout, and it is initialised through the cup path
 * (Career_AutoAdvanceCupCompetitionFromRecord). Because CompetitionCup is ALSO
 * comp.type == 1 but has a different layout (name@20 / teamSeedList vs Playoffs
 * name@16 / qualifiedTeamIds), Playoffs records set comp.isPlayoff = 1 (untangle
 * Step 4b) — that is how comp_name_table() / comp_team_seed_list() select the
 * Playoffs fields rather than the cup ones. (Pre-4b this used the per-record
 * nameTableOffset/teamListOffset self-offset bytes, now inert.)
 *
 * Field meanings cross-checked against the in-game DIY-competition editor
 * (DUTCH / URUGUAYAN PLAYOFFS screenshots, 2026-06-18) — see each comment. */
typedef struct __attribute__((packed)) {
    Competition comp;            /* 0x00: cup header (type=1; teamNumber = country, e.g.
                                  *       URUGUAY=71; months ×8 encoded — DEC=88, MAY=128) */
    int8_t  numRoundsOverride;   /* 0x09: forces runtime numRounds — Dutch=1 collapses its
                                  *       4-team bracket to a single FINAL; English div=0 →
                                  *       computed 2 rounds (two-leg semis + one-leg final) */
    int8_t  teams;               /* 0x0A: bracket size = qualified-team count. Editor "TEAMS"
                                  *       (Eng/Dutch/Spanish 4, Scottish/Uruguay 2). The team
                                  *       count read in career_season.c qualification loop. */
    int8_t  awayGoalsMode;       /* 0x0B: away-goals rule (editor "AWAY GOALS" tristate via
                                  *       diyCupAwayGoalsStringTable on runtime playEachTeam/$5E):
                                  *       0=OFF, 1="AFTER 90 MINS", 2="AFTER EXTRA TIME". All
                                  *       playoffs = 1. */
    int8_t  subsAndMaxSubs;      /* 0x0C: hi nibble = substitutes, lo = bench "FROM" size.
                                  *       Editor "SUBSTITUTES 3 FROM 5" = 0x35 (inherited from
                                  *       the parent league's subs byte). NB the cup-init path
                                  *       reads the lo nibble as pointsForAWin — a no-op in a
                                  *       knockout; the editor's "FROM" is the real meaning. */
    int8_t  roundStringBase;     /* 0x0D: cup round-string base (all 1) */
    int8_t  roundRuleFlags[2];   /* 0x0E: per-round packed legs/ET/pens (bits 7-6/5-4/3-2).
                                  *       0x94 = 2-leg + ET + pens; 0x14 = 1-leg + ET + pens
                                  *       (e.g. English two-leg semis then a one-leg final).
                                  *       [1] = 0 for a single-round playoff. */
    char   *name[2];             /* 0x10: playoff name strings (was text[2]) */
    int8_t  qualifiedTeamIds[8]; /* comp.teamListOffset target: BE-packed qualified team ids,
                                  *       zero-init, filled at runtime (was unk2) */
} Playoffs;

/* Pin the layout the self-offset bytes encode: comp.nameTableOffset(11) ⇒
 * 5+11 = 16 = offsetof(name); roundRuleFlags follows the 14-byte cup header.
 * Both are pre-pointer, so identical on 32/64-bit. */
_Static_assert(offsetof(Playoffs, roundRuleFlags) == 14, "Playoffs roundRuleFlags@14");
_Static_assert(offsetof(Playoffs, name)           == 16, "Playoffs name@16 (= 5 + nameTableOffset 11)");

/* Leagues — merged Leagues_4 / Leagues_2 / Leagues_4_2_24 (consolidation,
 * docs/STRUCT_CONSOLIDATE.md §4c). The layout mirrors the largest variant
 * (Leagues_4_2_24) exactly — four {league*, linkParams[8]} promotion-link
 * records followed by the playoff/next-group pointers and the qualifier-index
 * tail — so the englishLeagues1/._div2 instances (which were
 * Leagues_4_2_24) stay byte-identical and their typed .playoffs /
 * .qualifierIndexPairs reads are unchanged. The plain Leagues_4 (dutch/spanish/
 * _div3) and Leagues_2 (scottish/uruguay) instances leave the trailing
 * playoffs/nextGroup/qualifierIndexPairs (and, for _2, records 3-4) zero.
 *
 * NO numLeagues count is added: neither C port walks the records (only the
 * typed .playoffs/.qualifierIndexPairs/.playoffLinkPtrs reads in
 * career_season.c ever consume a Leagues block — verified against SWOS2.s), so
 * a count would be dead data that only shifts the layout. The 4-vs-2 record
 * distinction is informational, captured by leaving the unused records zero. */
typedef struct Leagues {
    CompetitionLeague *league1;
    int8_t             linkParams1[8];   /* promotion/relegation link config */
    CompetitionLeague *league2;
    int8_t             linkParams2[8];
    CompetitionLeague *league3;
    int8_t             linkParams3[8];
    CompetitionLeague *league4;
    int8_t             linkParams4[8];
    Playoffs          *playoffs;             /* Leagues_4_2_24 only (NULL otherwise) */
    struct Leagues    *nextGroup;            /* was leagues2 (NULL otherwise) */
    int8_t             qualifierIndexPairs[24]; /* was unk2[24] (zero otherwise) */
} Leagues;

/* ==================================================================
 * CompetitionCareer — career competition entry
 * ================================================================== */

typedef struct __attribute__((packed)) {
    Competition *competition;
    Competition *competitionCopy;
    uint16_t templateSize;   /* was length: sizeof(template) for the template→Copy memcpy */
} CompetitionCareer;

/* ==================================================================
 * QualifyingGroup — merged QualifyingGroup_3/_9_2/_2_8/_6_44/_3_4/_3_12
 * (consolidation, docs/STRUCT_CONSOLIDATE.md §4d). Arrays sized to the observed
 * maxima (compCup 9, countries 44). compCup[] is -1 (0xFFFFFFFF)-terminated with
 * -2/0xFC in-band markers; countries[] is 0xFF-terminated. Padded slots are zero.
 *
 * compCup holds a MIXED cup/cup2 list (e.g. europeTable carries
 * &europeanChampionships, a CompetitionTournament) — it is retyped to the polymorphic
 * Competition* base in the CompetitionCup family step; kept as CompetitionCup*
 * here to match the current header type.
 * ================================================================== */

typedef struct __attribute__((packed)) {
    Competition *compCup[9];     /* region competitions; -1-terminated */
    CountryNumbers  countries[44];  /* region countries; 0xFF-terminated */
} QualifyingGroup;

/* ==================================================================
 * DIYStage — DIY competition stage descriptor
 * ================================================================== */

typedef struct __attribute__((packed)) {
    uint16_t legs;
    uint16_t extraTime;
    uint16_t penalties;
    uint16_t playCount;
    uint16_t ptsForWin;
} DIYStage;

#endif /* GAME_TYPES_H */
