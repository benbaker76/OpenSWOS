/*
 * world.c -- which football world the game plays (include/world.h).
 */
#include "world.h"

static const char *const kTags[WORLD_COUNT] = {
    "9697",
    "2627",
};

static WorldId s_world = WORLD_9697;

static const uint16_t kFirstYear[WORLD_COUNT] = { 1996, 2026 };   /* (its first season's start) */

uint16_t World_FirstYear(void)
{
    return kFirstYear[World_Current()];
}

uint16_t World_SeasonYear(uint8_t seasonPlaying)
{
    uint16_t year = (uint16_t)(1900u + seasonPlaying);
    if (year < 1996)
        year += 100;
    return (uint16_t)(year + World_FirstYear() - 1996);
}

WorldId (World_Current)(void)       /* (parenthesised: SWOS_WORLDS=0 makes it a macro) */
{
    return s_world;
}

const char *World_TagOf(WorldId w)
{
    return (uint32_t)w < WORLD_COUNT ? kTags[w] : NULL;
}

const char *World_Tag(void)
{
    return kTags[s_world];
}

/* By hand: the Amiga build has no C library (-nostdlib). */
static int32_t same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

int32_t World_FromTag(const char *tag)
{
    if (tag)
        for (int32_t i = 0; i < WORLD_COUNT; i++)
            if (same(tag, kTags[i]))
                return i;
    return -1;
}

#ifndef SWOS_SDL_BUILD
extern uint32_t Resload_ReadSmallFile(const char *name, uint8_t *dest, uint32_t cap);

void World_ReadFromData(void)
{
    char tag[16];
    uint32_t n = Resload_ReadSmallFile("data/WORLD", (uint8_t *)tag, sizeof tag - 1);
    while (n > 0 && (tag[n - 1] == '\n' || tag[n - 1] == '\r' || tag[n - 1] == ' '))
        n--;
    tag[n] = '\0';
    if (n > 0)
        World_Set(tag);
}
#endif

int32_t World_Set(const char *tag)
{
    const int32_t w = World_FromTag(tag);
    if (w < 0 || (!SWOS_WORLDS && w != WORLD_9697))    /* (SWOS_WORLDS=0: 96/97 alone) */
        return -1;
    s_world = (WorldId)w;
    return 0;
}

#if SWOS_WORLDS
/* ---- a country over two team files (include/world.h) ---- */

extern uint16_t _teamsCountryNumbers[256];
extern void *_competitionsTable[];
extern void *_countriesTable[];

uint16_t World_Slot(uint8_t file, uint8_t ordinal)
{
    const uint8_t country = World_FileCountry(file);
    return (uint16_t)(country != file ? 256 + ordinal : ordinal);
}

uint16_t World_SlotTeam(uint8_t country, uint16_t slot)
{
    const uint8_t second = slot >= 256 ? World_SecondFile(country) : 0;
    return second ? (uint16_t)(second << 8 | (slot - 256)) : (uint16_t)(country << 8 | (slot & 0xFF));
}

uint16_t World_GlobalBase(uint8_t file)
{
    const uint8_t country = World_FileCountry(file);
    return country != file ? (uint16_t)(_teamsCountryNumbers[country] + 256) : _teamsCountryNumbers[file];
}

void World_SyncSecondFiles(void)
{
    for (int32_t c = 0; c < 256; c++) {
        const uint8_t second = World_SecondFile((uint8_t)c);
        if (second) {
            _competitionsTable[second] = _competitionsTable[c];
            _countriesTable[second] = _countriesTable[c];
        }
    }
}
#endif
