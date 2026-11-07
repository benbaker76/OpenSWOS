/*
 * challenge_pack.h -- a challenge pack (the challenges mod, docs/MODS.md §18): its JSON, which
 * the PC reads, and the fixed binary record the Amiga reads (docs/DATA_BSON.md: the Amiga parses
 * no JSON). One reader of the JSON for the game and OpenSWOS Setup, which writes the binary
 * when it puts a mod in an Amiga drawer (SwosData_AmigaMod), so the two never differ.
 *
 * The binary (all big-endian): "OSWCHAL" + a version byte (2; 1 is still read), a u16 count, then
 * that many CHALLENGE_PACK_RECORD-byte records (version 1: CHALLENGE_PACK_RECORD_V1, its fields
 * only) -- the fields of ChallengeDef in order, strings as their fixed-length, zero-padded fields.
 * The Amiga's file: <mod>/challenges/<NAME>.BIN for a pack's data_json/challenges/<name>.json;
 * the player's own (the editor) save/mods/challenges/CUSTOM.BIN.
 */
#ifndef CHALLENGE_PACK_H
#define CHALLENGE_PACK_H

#include <stdint.h>

enum { CHALLENGE_WIN, CHALLENGE_DRAW, CHALLENGE_EXACT };
/* = MODRESTART_* (include/mod_api.h; mod_challenges.c checks) */
enum { CHALLENGE_RESTART_KICK_OFF, CHALLENGE_RESTART_CORNER, CHALLENGE_RESTART_FREE_KICK,
       CHALLENGE_RESTART_PENALTY, CHALLENGE_RESTART_THROW_IN, CHALLENGE_RESTART_GOAL_KICK };
/* = MODET_* */
enum { CHALLENGE_ET_NORMAL, CHALLENGE_ET_GOLDEN_GOAL, CHALLENGE_ET_SILVER_GOAL };

#define CHALLENGE_DEF_FIELDS                                                                       \
    char     id[40], name[32], about[96], teamName[2][24], teamUuid[2][40];                        \
    uint16_t teamNumber[2];     /* "team": its number (hex), for the Amiga, which has no database */ \
    int8_t   difficulty, yours, score[2], winScore[2];                                             \
    uint8_t  win, extraTime, penalties, length;                                                    \
    int16_t  minute;                                                                               \
    uint32_t sentOff[2];                                                                           \
    int8_t   pitchType;                 /* -1 the game's */                                        \
    uint8_t  pitchNumber;               /* 0 the game's */                                         \
    uint8_t  restartKind;                                                                          \
    int8_t   restartTeam, restartWing;  /* -1: drawn at PLAY (SWOS 2020's RANDOM) */               \
    int16_t  restartX, restartY;                                                                   \
    struct { int8_t team; uint8_t shirt, minute, penalty; } scorers[8];                            \
    /* version 2 (SWOS 2020's scenarios) */                                                        \
    char     category[32];              /* its pack's "category" (or its own): the list's group,   \
                                         * and how online records know it */                       \
    char     title[32];                 /* "title": shown in place of the name ("" the name) */    \
    char     lines[5][64];              /* "description": the challenge's page, five lines */      \
    char     winMessage[40];            /* "win_message": what wins it, in a line */               \
    char     teamFile[2][32];           /* teams[t].file: a .EDT in the challenge's mod, its team  \
                                         * teams[t].name; "" the database's */                     \
    int8_t   playingUp, kickOff;        /* the team attacking the top goal / kicking off at the    \
                                         * start, -1 drawn */                                      \
    uint8_t  subsSet, subsMin, subsMax; /* "subs": [min, max] (subsSet 0: the game's) */           \
    uint8_t  extraTimeRule;             /* CHALLENGE_ET_* */                                       \
    uint8_t  shootoutOnly;              /* straight to a penalty shoot-out */                      \
    uint8_t  unavailable;               /* "locked": never playable (SWOS 2020's LOCKED -- its     \
                                         * whole category is then) */                              \
    uint8_t  secondLeg;                 /* "first_leg": [a, b] -- the second leg of a tie */       \
    int8_t   firstLeg[2];                                                                          \
    int8_t   restartSpot;               /* a free kick: 1..7 SWOS 2020's spots across the pitch,   \
                                         * 0 restartX/Y, -1 drawn */                               \
    uint8_t  restartDeep;               /* SWOS 2020's "HOME" free kicks: from further out */      \
    struct { uint8_t shirt, level; } injured[2][8];  /* "injured": [[shirt, level], ...] a team */ \
    char     recordName[64];            /* "record_name": its name online, when longer than name   \
                                         * (SWOS 2020's shared custom challenges); "" the name */

typedef struct { CHALLENGE_DEF_FIELDS } ChallengeDef;

#define CHALLENGE_PACK_MAGIC     "OSWCHAL"
#define CHALLENGE_PACK_VERSION   2
#define CHALLENGE_PACK_HEADER    10         /* magic 7, version 1, count 2 */
#define CHALLENGE_PACK_RECORD_V1 361        /* (the fields ChallengePack_Write lays down) */
#define CHALLENGE_PACK_RECORD    (CHALLENGE_PACK_RECORD_V1 + 32 + 32 + 320 + 40 + 64 + 2 + 3 + 4 + 2 + 2 + 32 + 64)

/* The binary: its size for `count`; written into `out` (cap at least that). Bytes, or -1. */
int32_t ChallengePack_Write(const ChallengeDef *defs, int32_t count, uint8_t *out, uint32_t cap);
/* Read into defs[max]: the number read, or -1 when it is not a pack. */
int32_t ChallengePack_Read(const uint8_t *data, uint32_t size, ChallengeDef *defs, int32_t max);

#if defined(SWOS_SDL_BUILD) || defined(CHALLENGE_PACK_JSON)
/* A pack's JSON (format "openswos-challenges") into defs[max]: the number read, or -1 when it is
 * not one. Names upper-cased, as the game shows them. */
int32_t ChallengePack_FromJson(const char *json, int len, ChallengeDef *defs, int32_t max);
/* One challenge as a JSON object of the pack's "challenges" (every field the reader reads, in its
 * names), into out[cap]: its length, or -1 when it does not fit. */
int32_t ChallengePack_ToJson(const ChallengeDef *c, char *out, uint32_t cap);
#endif

#endif /* CHALLENGE_PACK_H */
