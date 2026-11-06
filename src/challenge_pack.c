/*
 * challenge_pack.c -- a challenge pack's JSON and its binary for the Amiga
 * (include/challenge_pack.h). Built into the game (both targets: the Amiga only the binary)
 * and into OpenSWOS Setup (tools/swosdata, with CHALLENGE_PACK_JSON).
 */
#include "challenge_pack.h"

#include <string.h>

/* ---- the binary ---- */

typedef struct { uint8_t *p; const uint8_t *end; int bad; } Put;
typedef struct { const uint8_t *p, *end; int bad; } Get;

static void put_bytes(Put *w, const void *v, uint32_t n)
{
    if (w->p + n > w->end) { w->bad = 1; return; }
    memcpy(w->p, v, n);
    w->p += n;
}
static void put8(Put *w, uint32_t v) { uint8_t b = (uint8_t)v; put_bytes(w, &b, 1); }
static void put16(Put *w, uint32_t v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; put_bytes(w, b, 2); }
static void put32(Put *w, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put_bytes(w, b, 4);
}
static void put_str(Put *w, const char *s, uint32_t field)    /* zero-padded, always terminated */
{
    uint8_t b[96] = { 0 };
    uint32_t n = 0;
    while (n + 1 < field && s[n]) { b[n] = (uint8_t)s[n]; n++; }
    put_bytes(w, b, field);
}

static void get_bytes(Get *r, void *v, uint32_t n)
{
    if (r->p + n > r->end) { r->bad = 1; memset(v, 0, n); return; }
    memcpy(v, r->p, n);
    r->p += n;
}
static uint32_t get8(Get *r) { uint8_t b = 0; get_bytes(r, &b, 1); return b; }
static uint32_t get16(Get *r) { uint8_t b[2]; get_bytes(r, b, 2); return ((uint32_t)b[0] << 8) | b[1]; }
static uint32_t get32(Get *r)
{
    uint8_t b[4];
    get_bytes(r, b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static void get_str(Get *r, char *s, uint32_t field)
{
    get_bytes(r, s, field);
    s[field - 1] = 0;
}

/* One record, either way: the same list of fields, so writer and reader cannot part. */
#define FIELDS(STR, U8, U16, U32)                                                  \
    STR(id, 40) STR(name, 32) STR(about, 96)                                       \
    STR(teamName[0], 24) STR(teamName[1], 24) STR(teamUuid[0], 40) STR(teamUuid[1], 40) \
    U16(teamNumber[0]) U16(teamNumber[1])                                          \
    U8(difficulty) U8(yours) U8(score[0]) U8(score[1]) U8(winScore[0]) U8(winScore[1]) \
    U8(win) U8(extraTime) U8(penalties) U8(length) U16(minute)                     \
    U32(sentOff[0]) U32(sentOff[1])                                                \
    U8(pitchType) U8(pitchNumber) U8(restartKind) U8(restartTeam) U8(restartWing)  \
    U16(restartX) U16(restartY)

int32_t ChallengePack_Write(const ChallengeDef *defs, int32_t count, uint8_t *out, uint32_t cap)
{
    Put w = { out, out + cap, 0 };
    put_bytes(&w, CHALLENGE_PACK_MAGIC, 7);
    put8(&w, CHALLENGE_PACK_VERSION);
    put16(&w, (uint32_t)count);
    for (int32_t i = 0; i < count; i++) {
        const ChallengeDef *d = &defs[i];
        uint8_t *const start = w.p;
#define W_STR(f, n) put_str(&w, d->f, n);
#define W_U8(f)     put8(&w, (uint32_t)(uint8_t)d->f);
#define W_U16(f)    put16(&w, (uint32_t)(uint16_t)d->f);
#define W_U32(f)    put32(&w, d->f);
        FIELDS(W_STR, W_U8, W_U16, W_U32)
        for (int32_t k = 0; k < 8; k++) {
            put8(&w, (uint32_t)(uint8_t)d->scorers[k].team);
            put8(&w, d->scorers[k].shirt);
            put8(&w, d->scorers[k].minute);
            put8(&w, d->scorers[k].penalty);
        }
        if (!w.bad && w.p - start != CHALLENGE_PACK_RECORD)
            return -1;      /* (the record size in the header is wrong) */
    }
    return w.bad ? -1 : (int32_t)(w.p - out);
}

int32_t ChallengePack_Read(const uint8_t *data, uint32_t size, ChallengeDef *defs, int32_t max)
{
    Get r = { data, data + size, 0 };
    char magic[7];
    get_bytes(&r, magic, 7);
    if (r.bad || memcmp(magic, CHALLENGE_PACK_MAGIC, 7) != 0 || get8(&r) != CHALLENGE_PACK_VERSION)
        return -1;
    const int32_t count = (int32_t)get16(&r);
    if (r.bad || size < CHALLENGE_PACK_HEADER + (uint32_t)count * CHALLENGE_PACK_RECORD)
        return -1;
    int32_t n = 0;
    for (int32_t i = 0; i < count && n < max; i++, n++) {
        ChallengeDef *d = &defs[n];
        memset(d, 0, sizeof *d);
#define R_STR(f, n) get_str(&r, d->f, n);
#define R_U8(f)     d->f = get8(&r);   /* (an int8_t field: back to its sign) */
#define R_U16(f)    d->f = get16(&r);
#define R_U32(f)    d->f = get32(&r);
        FIELDS(R_STR, R_U8, R_U16, R_U32)
        for (int32_t k = 0; k < 8; k++) {
            d->scorers[k].team = (int8_t)get8(&r);
            d->scorers[k].shirt = (uint8_t)get8(&r);
            d->scorers[k].minute = (uint8_t)get8(&r);
            d->scorers[k].penalty = (uint8_t)get8(&r);
        }
    }
    return r.bad ? -1 : n;
}

/* ---- the JSON (the PC and the setup) ---- */

#if defined(SWOS_SDL_BUILD) || defined(CHALLENGE_PACK_JSON)
#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>

static void upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

static int32_t number(const char *json, int len, const char *path, int32_t fallback)
{
    long v;
    return mjson_get_long(json, len, path, &v) ? (int32_t)v : fallback;
}

int32_t ChallengePack_FromJson(const char *json, int len, ChallengeDef *defs, int32_t max)
{
    char fmt[40] = "";
    mjson_get_string(json, len, "$.format", fmt, (int)sizeof fmt);
    if (strcmp(fmt, "openswos-challenges") != 0)
        return -1;
    int32_t n = 0;
    for (int32_t i = 0; n < max; i++) {
        char q[80], w[16] = "";
        ChallengeDef *c = &defs[n];
        memset(c, 0, sizeof *c);
        snprintf(q, sizeof q, "$.challenges[%d].id", (int)i);
        if (mjson_get_string(json, len, q, c->id, (int)sizeof c->id) < 0)
            break;
#define STR(field, key) snprintf(q, sizeof q, "$.challenges[%d]." key, (int)i), \
                        mjson_get_string(json, len, q, c->field, (int)sizeof c->field)
#define NUM(key, fb) (snprintf(q, sizeof q, "$.challenges[%d]." key, (int)i), number(json, len, q, fb))
        STR(name, "name");
        STR(about, "about");
        upper(c->name);
        upper(c->about);
        c->difficulty = (int8_t)NUM("difficulty", 1);
        c->yours = (int8_t)(NUM("yours", 0) ? 1 : 0);
        c->score[0] = (int8_t)NUM("score[0]", 0);
        c->score[1] = (int8_t)NUM("score[1]", 0);
        c->minute = (int16_t)NUM("minute", 0);
        c->length = (uint8_t)NUM("length", 0);
        c->winScore[0] = (int8_t)NUM("win_score[0]", 0);
        c->winScore[1] = (int8_t)NUM("win_score[1]", 0);
        for (int32_t t = 0; t < 2; t++) {
            snprintf(q, sizeof q, "$.challenges[%d].teams[%d].uuid", (int)i, (int)t);
            mjson_get_string(json, len, q, c->teamUuid[t], (int)sizeof c->teamUuid[t]);
            {
                char hexNumber[8] = "";
                snprintf(q, sizeof q, "$.challenges[%d].teams[%d].team", (int)i, (int)t);
                if (mjson_get_string(json, len, q, hexNumber, (int)sizeof hexNumber) > 0)
                    c->teamNumber[t] = (uint16_t)strtol(hexNumber, NULL, 16);
            }
            snprintf(q, sizeof q, "$.challenges[%d].teams[%d].name", (int)i, (int)t);
            mjson_get_string(json, len, q, c->teamName[t], (int)sizeof c->teamName[t]);
            upper(c->teamName[t]);
            for (int32_t k = 0; k < 11; k++) {
                snprintf(q, sizeof q, "$.challenges[%d].sent_off[%d][%d]", (int)i, (int)t, (int)k);
                const int32_t shirt = number(json, len, q, 0);
                if (shirt <= 0)
                    break;
                if (shirt < 32)
                    c->sentOff[t] |= 1u << shirt;
            }
        }
        int b = 0;
        snprintf(q, sizeof q, "$.challenges[%d].extra_time", (int)i);
        c->extraTime = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        b = 0;
        snprintf(q, sizeof q, "$.challenges[%d].penalties", (int)i);
        c->penalties = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        /* where it starts (H6, H7): the pitch, the break, who has scored */
        c->pitchType = (int8_t)NUM("pitch_type", -1);
        c->pitchNumber = (uint8_t)NUM("pitch_number", 0);
        {
            char kind[16] = "";
            snprintf(q, sizeof q, "$.challenges[%d].restart.kind", (int)i);
            mjson_get_string(json, len, q, kind, (int)sizeof kind);
            c->restartKind = !strcmp(kind, "corner") ? CHALLENGE_RESTART_CORNER
                           : !strcmp(kind, "free_kick") ? CHALLENGE_RESTART_FREE_KICK
                           : !strcmp(kind, "penalty") ? CHALLENGE_RESTART_PENALTY : CHALLENGE_RESTART_KICK_OFF;
            c->restartTeam = (uint8_t)(NUM("restart.team", 0) ? 1 : 0);
            c->restartWing = (uint8_t)(NUM("restart.wing", 0) ? 1 : 0);
            c->restartX = (int16_t)NUM("restart.x", 336);
            c->restartY = (int16_t)NUM("restart.y", 449);
        }
        for (int32_t k = 0; k < 8; k++) {
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].team", (int)i, (int)k);
            const int32_t team = number(json, len, q, -1);
            c->scorers[k].team = (int8_t)(team == 0 || team == 1 ? team : -1);
            if (c->scorers[k].team < 0)
                continue;
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].shirt", (int)i, (int)k);
            c->scorers[k].shirt = (uint8_t)number(json, len, q, 0);
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].minute", (int)i, (int)k);
            c->scorers[k].minute = (uint8_t)number(json, len, q, 0);
            b = 0;
            snprintf(q, sizeof q, "$.challenges[%d].scorers[%d].penalty", (int)i, (int)k);
            c->scorers[k].penalty = (uint8_t)(mjson_get_bool(json, len, q, &b) && b);
        }
        snprintf(q, sizeof q, "$.challenges[%d].win", (int)i);
        mjson_get_string(json, len, q, w, (int)sizeof w);
        c->win = !strcmp(w, "draw") ? CHALLENGE_DRAW : !strcmp(w, "exact") ? CHALLENGE_EXACT : CHALLENGE_WIN;
#undef STR
#undef NUM
        n++;
    }
    return n;
}
#endif
