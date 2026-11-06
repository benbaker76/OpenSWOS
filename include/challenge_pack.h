/*
 * challenge_pack.h -- a challenge pack (the challenges mod, docs/MODS.md §18): its JSON, which
 * the PC reads, and the fixed binary record the Amiga reads (docs/DATA_BSON.md: the Amiga parses
 * no JSON). One reader of the JSON for the game and OpenSWOS Setup, which writes the binary
 * when it puts a mod in an Amiga drawer (SwosData_AmigaMod), so the two never differ.
 *
 * The binary (all big-endian): "OSWCHAL" + a version byte (1), a u16 count, then that many
 * CHALLENGE_PACK_RECORD-byte records -- the fields of ChallengeDef in order, strings as their
 * fixed-length, zero-padded fields. The Amiga's file: <mod>/challenges/<NAME>.BIN for a
 * pack's data_json/challenges/<name>.json; the player's own (the editor) save/mods/challenges/
 * CUSTOM.BIN.
 */
#ifndef CHALLENGE_PACK_H
#define CHALLENGE_PACK_H

#include <stdint.h>

enum { CHALLENGE_WIN, CHALLENGE_DRAW, CHALLENGE_EXACT };
/* = MODRESTART_* (include/mod_api.h; mod_challenges.c checks) */
enum { CHALLENGE_RESTART_KICK_OFF, CHALLENGE_RESTART_CORNER, CHALLENGE_RESTART_FREE_KICK,
       CHALLENGE_RESTART_PENALTY };

#define CHALLENGE_DEF_FIELDS                                                                       \
    char     id[40], name[32], about[96], teamName[2][24], teamUuid[2][40];                        \
    uint16_t teamNumber[2];     /* "team": its number (hex), for the Amiga, which has no database */ \
    int8_t   difficulty, yours, score[2], winScore[2];                                             \
    uint8_t  win, extraTime, penalties, length;                                                    \
    int16_t  minute;                                                                               \
    uint32_t sentOff[2];                                                                           \
    int8_t   pitchType;                 /* -1 the game's */                                        \
    uint8_t  pitchNumber;               /* 0 the game's */                                         \
    uint8_t  restartKind, restartTeam, restartWing;                                                \
    int16_t  restartX, restartY;                                                                   \
    struct { int8_t team; uint8_t shirt, minute, penalty; } scorers[8];

typedef struct { CHALLENGE_DEF_FIELDS } ChallengeDef;

#define CHALLENGE_PACK_MAGIC   "OSWCHAL"
#define CHALLENGE_PACK_VERSION 1
#define CHALLENGE_PACK_HEADER  10           /* magic 7, version 1, count 2 */
#define CHALLENGE_PACK_RECORD  361          /* (the fields ChallengePack_Write lays down) */

/* The binary: its size for `count`; written into `out` (cap at least that). Bytes, or -1. */
int32_t ChallengePack_Write(const ChallengeDef *defs, int32_t count, uint8_t *out, uint32_t cap);
/* Read into defs[max]: the number read, or -1 when it is not a pack. */
int32_t ChallengePack_Read(const uint8_t *data, uint32_t size, ChallengeDef *defs, int32_t max);

#if defined(SWOS_SDL_BUILD) || defined(CHALLENGE_PACK_JSON)
/* A pack's JSON (format "openswos-challenges") into defs[max]: the number read, or -1 when it is
 * not one. Names upper-cased, as the game shows them. */
int32_t ChallengePack_FromJson(const char *json, int len, ChallengeDef *defs, int32_t max);
#endif

#endif /* CHALLENGE_PACK_H */
