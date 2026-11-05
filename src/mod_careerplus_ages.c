/*
 * mod_careerplus_ages.c -- Career+'s ages and retirements (CAREER_PLUS.md F2).
 *
 * Every player has an age, kept by his place -- his club and file slot (0-31) -- and moved with
 * him (playerMoved; the mod's own AI transfers and line-up swaps). 1996/97's are the mod's data
 * (data_json/player_ages.json, benefactorr's); a player without one is 25 when the career starts,
 * a new player (a trialist, a club's replacement, a youth) 17. Each season everyone is a year
 * older: an age is kept as its value at the career's start, `base`, with the seasons since,
 * `s_aged`, so ageing is one count and the save holds only what differs from the data.
 *
 * At each season's end, players of 35 and more may retire (10% at 35, 20%, 40%, 80%, then 90%
 * from 39): the career club's first, then every other club's. A retiree's place goes to a youth
 * of 17 -- his nationality, position, face and shirt -- about as good as he was (a percentage of
 * his ratings' total by its tier, nudged at most two points towards the club's players of his
 * position), named after two players of his nationality. The career club's retirements are
 * listed at CONTINUE CAREER (RETIREMENT NEWS). The fork's SWOS_Retirement* (career_init.c at
 * 8a6baf60), on the mod's own random numbers and the real season (his counted job changes).
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"
#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

/* ---- the ages: an open-addressed table keyed by team << 5 | slot ---- */

enum { kTable = 1 << 16, kUnknownAge = 25, kYouthAge = 17 };

typedef struct {
    uint32_t key;       /* team << 5 | slot, + 1 (0: empty) */
    int16_t  base;      /* his age when the career started (17 - seasons, for a later youth) */
    int16_t  seed;      /* the data's base for this place, -1 none: what the save leaves out */
    uint16_t moved;     /* the AI batch he last moved in (mod_careerplus_ai.c), 0 never */
} AgeEntry;

static AgeEntry s_ages[kTable];
static int32_t  s_used;
static int32_t  s_aged;         /* seasons aged since the career started */

static uint32_t age_key(uint16_t team, int32_t slot) { return ((uint32_t)team << 5 | (uint32_t)(slot & 31)) + 1; }

static AgeEntry *entry(uint16_t team, int32_t slot, int32_t add)
{
    const uint32_t key = age_key(team, slot);
    uint32_t h = (key * 2654435761u) & (kTable - 1);
    while (s_ages[h].key && s_ages[h].key != key)
        h = (h + 1) & (kTable - 1);
    if (!s_ages[h].key) {
        if (!add || s_used >= kTable * 3 / 4)
            return NULL;
        s_ages[h].key = key;
        s_ages[h].base = (int16_t)kUnknownAge;
        s_ages[h].seed = -1;
        s_ages[h].moved = 0;
        s_used++;
    }
    return &s_ages[h];
}

int32_t CP_Age(uint16_t team, int32_t slot)
{
    const AgeEntry *e = entry(team, slot, 0);
    return (e ? e->base : kUnknownAge) + s_aged;
}

void CP_SetAge(uint16_t team, int32_t slot, int32_t age)
{
    AgeEntry *e = entry(team, slot, 1);
    if (e) {
        e->base = (int16_t)(age - s_aged);
        e->moved = 0;
    }
}

/* The player of (from) is now at (to): his age and when he last moved go with him. */
void CP_MoveAge(uint16_t fromTeam, int32_t fromSlot, uint16_t toTeam, int32_t toSlot)
{
    const AgeEntry *f = entry(fromTeam, fromSlot, 0);
    AgeEntry *t = entry(toTeam, toSlot, 1);
    if (t) {
        t->base = f ? f->base : (int16_t)kUnknownAge;
        t->moved = f ? f->moved : 0;
    }
}

void CP_SwapAges(uint16_t teamA, int32_t slotA, uint16_t teamB, int32_t slotB)
{
    AgeEntry *a = entry(teamA, slotA, 1), *b = entry(teamB, slotB, 1);
    if (a && b) {
        const int16_t base = a->base;
        const uint16_t moved = a->moved;
        a->base = b->base, a->moved = b->moved;
        b->base = base, b->moved = moved;
    }
}

uint16_t CP_MovedBatch(uint16_t team, int32_t slot)
{
    const AgeEntry *e = entry(team, slot, 0);
    return e ? e->moved : 0;
}

void CP_SetMovedBatch(uint16_t team, int32_t slot, uint16_t batch)
{
    AgeEntry *e = entry(team, slot, 1);
    if (e)
        e->moved = batch;
}

/* The data's ages (data_json/player_ages.json: [file, ordinal, [16 ages]] a club). */
static void seed_ages(void)
{
    memset(s_ages, 0, sizeof s_ages);
    s_used = 0;
    s_aged = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/data_json/player_ages.json", ModApi_ModDir(CP_SLUG));
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[careerplus] %s not found: every player is %d\n", path, (int)kUnknownAge);
        return;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *j = n > 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (!j || fread(j, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(j);
        return;
    }
    fclose(f);
    j[n] = 0;
    const char *arr;
    int arrLen, clubs = 0;
    if (mjson_find(j, (int)n, "$.clubs", &arr, &arrLen) == MJSON_TOK_ARRAY) {
        enum { kPerClub = 18 };             /* file, ordinal, the 16 ages */
        int32_t *v = (int32_t *)malloc((size_t)arrLen / 2 * sizeof *v + sizeof *v);
        const int32_t count = v ? CP_Numbers(arr, arrLen, v, arrLen / 2) : 0;
        for (int32_t k = 0; k + kPerClub <= count; k += kPerClub, clubs++) {
            const uint16_t team = (uint16_t)(v[k] << 8 | v[k + 1]);
            for (int32_t slot = 0; slot < 16; slot++) {
                const int32_t age = v[k + 2 + slot];
                AgeEntry *e = age >= 15 && age <= 60 ? entry(team, slot, 1) : NULL;
                if (e)
                    e->base = e->seed = (int16_t)age;
            }
        }
        free(v);
    }
    free(j);
    fprintf(stderr, "[careerplus] ages of %d clubs' players\n", clubs);
}

/* ---- the save: "aged": seasons, "ages": [[team, slot, base, moved], ...] where not the data's ---- */

void CP_Ages_Save(ModJsonWriter *out)
{
    ModJson_Printf(out, ",\"aged\":%d,\"ages\":[", (int)s_aged);
    int32_t first = 1;
    for (int32_t h = 0; h < kTable; h++) {
        const AgeEntry *e = &s_ages[h];
        if (!e->key || (e->base == e->seed && !e->moved))
            continue;
        const uint32_t key = e->key - 1;
        ModJson_Printf(out, "%s[%u,%u,%d,%u]", first ? "" : ",", (unsigned)(key >> 5), (unsigned)(key & 31),
                       (int)e->base, (unsigned)e->moved);
        first = 0;
    }
    ModJson_Printf(out, "]");
}

void CP_Ages_Load(const char *json, int32_t len)
{
    seed_ages();
    double v;
    const char *arr;
    int arrLen;
    if (!json)
        return;
    if (mjson_get_number(json, len, "$.aged", &v) && v >= 0 && v < 100)
        s_aged = (int32_t)v;
    if (mjson_find(json, len, "$.ages", &arr, &arrLen) != MJSON_TOK_ARRAY)
        return;
    int32_t *n = (int32_t *)malloc((size_t)arrLen / 2 * sizeof *n + sizeof *n);
    const int32_t count = n ? CP_Numbers(arr, arrLen, n, arrLen / 2) : 0;
    for (int32_t k = 0; k + 4 <= count; k += 4) {     /* [team, slot, base, moved] */
        const int32_t t = n[k], s = n[k + 1], b = n[k + 2], m = n[k + 3];
        if (t < 0 || t > 65535 || s < 0 || s > 31 || b < -100 || b > 100 || m < 0 || m > 65535)
            continue;
        AgeEntry *e = entry((uint16_t)t, s, 1);
        if (e) {
            e->base = (int16_t)b;
            e->moved = (uint16_t)m;
        }
    }
    free(n);
}

void CP_Ages_New(void)
{
    seed_ages();
}

/* ---- the game's own moves and new players ---- */

void CP_Ages_Moved(const ModPlayerMove *m)
{
    CP_MoveAge(m->fromTeam, m->fromSlot, m->toTeam, m->toSlot);
}

void CP_Ages_Created(const ModPlayerCreated *c)
{
    CP_SetAge(c->team, c->slot, kYouthAge);
}

/* ---- RETIREMENT NEWS: the career club's, this season ---- */

enum { kNewsMax = 32 };
typedef struct { char name[23]; uint8_t age, position; } News;
static News s_news[kNewsMax];
static int32_t s_newsCount, s_newsSeason;

int32_t CP_News_Count(void) { return s_newsCount; }
void CP_News_Clear(void) { s_newsCount = 0; }
int32_t CP_News_Season(void) { return s_newsSeason; }
const char *CP_News_Line(int32_t i, uint8_t *age, uint8_t *position)
{
    if (i < 0 || i >= s_newsCount)
        return NULL;
    *age = s_news[i].age;
    *position = s_news[i].position;
    return s_news[i].name;
}

/* ---- the youth who takes a retiree's place ---- */

static int32_t ratings_total(const PlayerFile *p)
{
    uint8_t a[7];
    int32_t t = 0;
    CP_Ratings(p, a);
    for (int32_t k = 0; k < 7; k++)
        t += a[k] & 7;
    return t;
}

/* A name of his nationality: one player's first word and another's last, not in use. */
static void youth_name(PlayerFile *p, uint8_t nationality)
{
    for (int32_t attempt = 0; attempt < 24; attempt++) {
        const PlayerFile *first = NULL, *last = NULL;
        uint32_t seenFirst = 0, seenLast = 0;
        for (int32_t c = 0; c < CP_World_Count(); c++) {
            const CP_Club *club = CP_World_Club(c);
            extern uint16_t _selTeamNumber;
            if (club->team == _selTeamNumber)
                continue;
            for (int32_t s = 0; s < 16; s++) {
                const PlayerFile *q = &club->players[s];
                if ((uint8_t)q->nationality != nationality || !CP_PlayerValid(q))
                    continue;
                if (CP_RandBelow((int32_t)++seenFirst) == 0)
                    first = q;
                if (CP_RandBelow((int32_t)++seenLast) == 0)
                    last = q;
            }
        }
        if (!first || !last)
            return;
        char a[23] = { 0 }, b[23] = { 0 }, name[23] = { 0 };
        memcpy(a, first->playerName, 22);
        memcpy(b, last->playerName, 22);
        char *space = strchr(a, ' ');
        if (space)
            *space = 0;
        const char *surname = strrchr(b, ' ');
        surname = surname && surname[1] ? surname + 1 : b;
        if (!a[0] || !surname[0] || !strcmp(a, surname))
            continue;
        snprintf(name, sizeof name, "%.10s %.11s", a, surname);
        int32_t taken = 0;
        for (int32_t c = 0; c < CP_World_Count() && !taken; c++)
            for (int32_t s = 0; s < 16 && !taken; s++)
                taken = strncmp((const char *)CP_World_Club(c)->players[s].playerName, name, 22) == 0;
        if (taken)
            continue;
        memset(p->playerName, 0, sizeof p->playerName);
        memcpy(p->playerName, name, strlen(name));
        return;
    }
}

/* The retiree at `slot` of `club` (its 16, or the career club's reserve copied in: `self`) becomes
 * a youth, in place. */
static void make_youth(const CP_Club *club, PlayerFile *p, int32_t selfSlot)
{
    const uint8_t pos = CP_Position(p);
    uint32_t sums[7] = { 0 }, peers = 0, priceSum = 0;
    for (int32_t i = 0; i < 16; i++) {
        const PlayerFile *q = &club->players[i];
        if (i == selfSlot || !CP_PlayerValid(q) || CP_Position(q) != pos)
            continue;
        uint8_t a[7];
        CP_Ratings(q, a);
        for (int32_t k = 0; k < 7; k++)
            sums[k] += a[k];
        priceSum += (uint8_t)q->price;
        peers++;
    }
    const int32_t baseline = pos == 0 ? (uint8_t)p->price : ratings_total(p);
    int32_t peerBench = baseline;
    if (peers) {
        uint32_t peerTotal = 0;
        for (int32_t k = 0; k < 7; k++)
            peerTotal += sums[k];
        peerBench = pos == 0 ? (int32_t)((priceSum + peers / 2) / peers) : (int32_t)((peerTotal + peers / 2) / peers);
    }
    int32_t lowPct, highPct;
    if (baseline < 10)      lowPct = 80, highPct = 120;
    else if (baseline < 20) lowPct = 85, highPct = 115;
    else if (baseline < 30) lowPct = 85, highPct = 110;
    else if (baseline < 40) lowPct = 90, highPct = 110;
    else                    lowPct = 90, highPct = 105;
    const int32_t pct = lowPct + CP_RandBelow(highPct - lowPct + 1);
    int32_t target = (baseline * pct + 50) / 100;
    if (peers) {
        int32_t adjust = (peerBench - baseline) / 10;
        adjust = adjust > 2 ? 2 : adjust < -2 ? -2 : adjust;
        const int32_t lower = (baseline * lowPct + 50) / 100, upper = (baseline * highPct + 50) / 100;
        target += adjust;
        target = target < lower ? lower : target > upper ? upper : target;
    }
    memset(p->goalStats, 0, sizeof p->goalStats);
    p->cardsInjuries = 0;
    p->form = 0;
    p->cachedMatchValue = 0;
    p->positionAndFace &= (uint8_t)~(PF_LOAN | PF_TRIALIST | PF_INFORM);
    if (pos == 0) {
        /* (49 at most: the fork's 127 made a youth of a 48-49 keeper "no player" to its transfers) */
        p->price = (int8_t)(target < 1 ? 1 : target > 49 ? 49 : target);
        p->passing = p->velocityHeading = p->tacklingControl = p->speedFinishing = 0;
    } else {
        uint8_t a[7] = { 0 };
        target = target < 1 ? 1 : target > 49 ? 49 : target;
        for (int32_t n = 0; n < target; n++) {
            int32_t best = 0, score = 0;
            for (int32_t k = 0; k < 7; k++) {
                if (a[k] >= 7)
                    continue;
                int32_t w = (peers ? (int32_t)(sums[k] * 8 / peers) : 24) + CP_RandBelow(17) - a[k] * 8;
                w = w < 0 ? 0 : w;
                if (w >= score)
                    score = w, best = k;
            }
            if (a[best] < 7)
                a[best]++;
        }
        CP_SetRatings(p, a);
        const int32_t total = ratings_total(p);
        p->price = (int8_t)(total < 1 ? 1 : total);
    }
    youth_name(p, (uint8_t)p->nationality);
}

static int32_t retires(int32_t age)
{
    static const uint8_t kChance[] = { 10, 20, 40, 80 };   /* 35..38; 90 from 39 */
    if (age < 35)
        return 0;
    return CP_RandBelow(100) < (age >= 39 ? 90 : kChance[age - 35]);
}

/* The season's retirements: the career club's first (its 32), then every other club's 16. The
 * clubs (CP_World) are read already. */
void CP_Ages_Retire(uint16_t season)
{
    extern uint16_t _selTeamNumber, _isNationalSelector;
    s_newsCount = 0;
    s_newsSeason = season;
    int32_t ai = 0, own = 0;
    if (!_isNationalSelector) {
        CP_Club *club = CP_World_Find(_selTeamNumber);
        if (club) {
            extern TeamFileEx _careerTeam;
            for (int32_t slot = 0; slot < 32; slot++) {
                PlayerFile p = *(const PlayerFile *)((const uint8_t *)&_careerTeam + sizeof(TeamFile) +
                                                    slot * sizeof(PlayerFile));
                const int32_t age = CP_Age(_selTeamNumber, slot);
                if (!CP_PlayerValid(&p) || !retires(age))
                    continue;
                if (s_newsCount < kNewsMax) {
                    News *n = &s_news[s_newsCount++];
                    memcpy(n->name, p.playerName, 22);
                    n->name[22] = 0;
                    n->age = (uint8_t)age;
                    n->position = CP_Position(&p);
                }
                make_youth(club, &p, slot < 16 ? slot : -1);
                if (ModApi_SetOwnPlayer((uint16_t)slot, &p) == 0) {
                    if (slot < 16)
                        club->players[slot] = p;
                    CP_SetAge(_selTeamNumber, slot, kYouthAge);
                    own++;
                }
            }
        }
    }
    for (int32_t c = 0; c < CP_World_Count(); c++) {
        CP_Club *club = CP_World_Club(c);
        if (club->team == _selTeamNumber)
            continue;
        for (int32_t slot = 0; slot < 16; slot++) {
            PlayerFile p = club->players[slot];
            if (!CP_PlayerValid(&p) || !retires(CP_Age(club->team, slot)))
                continue;
            make_youth(club, &p, slot);
            club->players[slot] = p;
            if (CP_World_Write(club, slot) != 0) {
                fprintf(stderr, "[careerplus] the career's pool is full: no more retirements this season\n");
                c = CP_World_Count();
                break;
            }
            CP_SetAge(club->team, slot, kYouthAge);
            ai++;
        }
    }
    fprintf(stderr, "[careerplus] season %u's end: %d retirements at your club, %d at the others\n",
            (unsigned)season, (int)own, (int)ai);
}

void CP_Ages_SeasonEnd(void)
{
    s_aged++;
}

#endif /* SWOS_SDL_BUILD */
