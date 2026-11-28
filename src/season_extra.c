/*
 * season_extra.c -- a season's competition lines past the record's four (include/season_extra.h).
 */
#include "season_extra.h"
#include "career_params.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc on the Amiga) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

extern SeasonInformations _seasonInformationsArray[];

static SeasonExtraLine s_extra[CAREER_MAX_SEASONS][SEASON_EXTRA_LINES];

void SeasonExtra_Reset(void)
{
    uint8_t *p = (uint8_t *)s_extra;
    for (uint32_t i = 0; i < sizeof s_extra; i++)
        p[i] = 0;
}

int32_t SeasonExtra_Index(const SeasonInformations *si)
{
    if (!si || si < _seasonInformationsArray)
        return -1;
    const int32_t i = (int32_t)(si - _seasonInformationsArray);
    return i < CAREER_MAX_SEASONS ? i : -1;
}

void SeasonExtra_Clear(int32_t season)
{
    if (season < 0 || season >= CAREER_MAX_SEASONS)
        return;
    for (int32_t i = 0; i < SEASON_EXTRA_LINES; i++) {
        s_extra[season][i].name[0] = 0;
        s_extra[season][i].code = 0;
    }
}

static int32_t same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int32_t SeasonExtra_Has(int32_t season, const char *name)
{
    if (season < 0 || season >= CAREER_MAX_SEASONS || !name)
        return 0;
    for (int32_t i = 0; i < SEASON_EXTRA_LINES; i++)
        if (s_extra[season][i].name[0] && same(s_extra[season][i].name, name))
            return 1;
    return 0;
}

int32_t SeasonExtra_Add(int32_t season, const char *name, int8_t code)
{
    if (season < 0 || season >= CAREER_MAX_SEASONS || !name || !name[0])
        return 0;
    for (int32_t i = 0; i < SEASON_EXTRA_LINES; i++) {
        SeasonExtraLine *e = &s_extra[season][i];
        if (e->name[0] && !same(e->name, name))
            continue;
        int32_t k = 0;
        for (; name[k] && k < SEASON_EXTRA_NAME - 1; k++)
            e->name[k] = name[k];
        e->name[k] = 0;
        e->code = code;
        return 1;
    }
    return 0;
}

int32_t SeasonExtra_Count(int32_t season)
{
    int32_t n = 0;
    if (season < 0 || season >= CAREER_MAX_SEASONS)
        return 0;
    for (int32_t i = 0; i < SEASON_EXTRA_LINES; i++)
        n += s_extra[season][i].name[0] != 0;
    return n;
}

const SeasonExtraLine *SeasonExtra_Get(int32_t season, int32_t i)
{
    if (season < 0 || season >= CAREER_MAX_SEASONS || i < 0 || i >= SEASON_EXTRA_LINES)
        return 0;
    return s_extra[season][i].name[0] ? &s_extra[season][i] : 0;
}
