/*
 * conti_season.c -- the season end's glue of the other confederations' cups (src/world_conti.c,
 * include/world_conti.h; docs/WORLD_2026_27.md): the SWOS 96/97 Mod's tools/conti.py GLUE
 * (ct_ctry, ct_fill) on our season end.
 *
 * Every country of the season end, as it is processed: its top division's final table (as
 * team words), its top 9 and its cup winner's records (the grids the qualifiers read) -- the
 * first one finishing this season's edition first; every country seen: the next edition of
 * every cup (the career club, a seed of the season, the UEFA Champions League's holder for the
 * Intercontinental).
 *
 * The Mod's team words are file | ordinal << 8; ours BE {country, ordinal}: converted here (a
 * record's first two bytes read little-endian are the Mod's word already).
 */
#include "world_conti.h"
#include "league_playoffs.h"
#include "world.h"
#include "game_types.h"
#include "endian_helpers.h"

#if SWOS_WORLDS

extern uint8_t _primaryQualifiedTeamGrids[];
extern uint16_t _selTeamNumber, _isNationalSelector;
extern uint16_t _euroCupTitleHolderTeamId;

static uint8_t s_on;

static u16 modWord(uint16_t team) { return (u16)((team >> 8) | (team << 8)); }

void Conti_Reset(void)
{
    uint8_t *p = (uint8_t *)&CT_ST;
    for (uint32_t i = 0; i < sizeof CT_ST; i++)
        p[i] = 0;
    p = (uint8_t *)&CT_ED;
    for (uint32_t i = 0; i < sizeof CT_ED; i++)
        p[i] = 0;
    CT_SEG.kind = 0xFF;
    s_on = 0;
}

void Conti_Country(void)
{
    const u8 *spec = World_ContiSpec();
    if (!spec)
        return;
    CT_SPEC = spec;
    if (!s_on) {
        s_on = 1;
        ct_begin();
    }
    uint16_t words[32];
    const int32_t n = LeaguePlayoffs_TopTable(words, 32);
    for (int32_t i = 0; i < n; i++)
        words[i] = modWord(words[i]);
    ct_table_words(words, n);
    ct_country(_primaryQualifiedTeamGrids, _primaryQualifiedTeamGrids + 9 * sizeof(TeamFileEx));
}

void Conti_SeasonEnd(void)
{
    if (!s_on)
        return;
    s_on = 0;
    const uint8_t *g = _primaryQualifiedTeamGrids;  /* (a seed: the Mod's dword [STAND]) */
    const u32 seed = g[0] | g[1] << 8 | (u32)g[2] << 16 | (u32)g[3] << 24;
    const u16 hold = _euroCupTitleHolderTeamId && _euroCupTitleHolderTeamId != 0xFFFF
                   ? modWord(_euroCupTitleHolderTeamId) : 0xFFFF;
    ct_go(_isNationalSelector ? 0 : modWord(_selTeamNumber), seed, hold);
    SWOS_DBG("[conti] the next editions: %d clubs; winners of the one ended:", (int)CT_IN.nclub);
    for (int32_t c = 0; c < CT_NCUP; c++)
        if (CT_ST.hold[c].id)
            SWOS_DBG(" %d:%04X", (int)c, (unsigned)CT_ST.hold[c].id);
    SWOS_DBG("\n");
}

#else

void Conti_Country(void) { }
void Conti_SeasonEnd(void) { }
void Conti_Reset(void) { }

#endif /* SWOS_WORLDS */
