/*
 * world_europe.c -- a world's European cups as knockouts (include/world_europe.h).
 */
#include "world_europe.h"
#include "world.h"
#include "competition_data.h"
#include "endian_helpers.h"
#include "play_match_menu.h"

#include <stdio.h>

extern CompetitionCareer _careerContests[];
extern DIYCompetitionEx _diyCompetitionStart;
extern uint16_t _gameType;

#if SWOS_WORLDS
static const WorldEuropeKO *s_installed;
#endif

void WorldEurope_Install(const WorldEuropeKO *ko)
{
#if SWOS_WORLDS
    const Competition *was[3] = { (const Competition *)&_euroCup, (const Competition *)&_cupWinnersCup,
                                  (const Competition *)&_uefaCup };
    s_installed = ko;
    for (int32_t k = 0; k < 3; k++) {           /* (the contest table's first three: the euro cups) */
        _careerContests[k].competition = (Competition *)ko->cup[k];
        _careerContests[k].templateSize = ko->size[k];
        /* VIEW WORLD's Europe row finds its cups in _careerContests (league_phase.c's reason) */
        for (int32_t i = 0; i < 9 && (uintptr_t)_europeTable.compCup[i] != (uintptr_t)-1; i++)
            if (_europeTable.compCup[i] == was[k])
                _europeTable.compCup[i] = (Competition *)ko->cup[k];
    }
#else
    (void)ko;
#endif
}

int32_t WorldEurope_Active(void)
{
#if SWOS_WORLDS
    return s_installed && World_EuropeKnockouts() == s_installed;
#else
    return 0;
#endif
}

const uint8_t *WorldEurope_Places(uint8_t country)
{
#if SWOS_WORLDS
    if (!WorldEurope_Active())
        return 0;
    for (int32_t i = 0; i < s_installed->count; i++)
        if (s_installed->places[i][0] == country)
            return &s_installed->places[i][1];
#else
    (void)country;
#endif
    return 0;
}

#if SWOS_WORLDS
/* The UEFA rule of the time (the Mod 3.0, careerworld.py w_fixpairs): a tie of two clubs of one
 * country swaps its away club with another tie's, so that neither is a same-country one; four
 * passes at most. grid: the round's clubs, a tie two by two (indices into cupTeamIds). */
#define COUNTRY(i) World_FileCountry((uint8_t)(BE_READ_U16(&_diyCompetitionStart.cupTeamIds[grid[(i)]]) >> 8))
static int32_t same_ties(const uint8_t *grid, int32_t ties)
{
    int32_t n = 0;
    for (int32_t k = 0; k < ties; k++)
        n += COUNTRY(2 * k) == COUNTRY(2 * k + 1);
    return n;
}

static void fix_pairs(uint8_t *grid, int32_t ties, uint8_t compId, int32_t round)
{
    const int32_t before = same_ties(grid, ties);
    for (int32_t pass = 0; pass < 4; pass++) {
        int32_t changed = 0;
        for (int32_t k = 0; k < ties; k++) {
            const uint8_t bl = COUNTRY(2 * k), bh = COUNTRY(2 * k + 1);
            if (bl != bh)
                continue;
            for (int32_t j = 0; j < ties; j++) {
                if (j == k || COUNTRY(2 * j) == bh || COUNTRY(2 * j + 1) == bl)
                    continue;
                const uint8_t t = grid[2 * k + 1];
                grid[2 * k + 1] = grid[2 * j + 1];
                grid[2 * j + 1] = t;
                changed++;
                break;
            }
        }
        if (!changed)
            break;
    }
    if (before)
        fprintf(stderr, "[world-europe] cup %u round %d: %d same-country ties, %d left\n", (unsigned)compId,
                (int)round, (int)before, (int)same_ties(grid, ties));
}
#undef COUNTRY

/* A career's European cup of a classic world (ids 0..2, played as knockouts). */
static int32_t euro_knockout(uint8_t compId)
{
    return WorldEurope_Active() && _gameType == GAME_TYPE_CAREER && compId <= 2;
}
#endif

void WorldEurope_DrawMade(uint8_t compId)
{
#if SWOS_WORLDS
    DIYCompetitionEx *x = &_diyCompetitionStart;
    if (!euro_knockout(compId))
        return;
    fix_pairs(x->teamGridPairings, x->base.matchPairsPerRound, compId, 1);
#else
    (void)compId;
#endif
}

void WorldEurope_RoundPlayed(void)
{
#if SWOS_WORLDS
    DIYCompetitionEx *x = &_diyCompetitionStart;
    if (!euro_knockout(x->base.field_2E) || x->base.currentRoundIndex != 0)
        return;
    /* round 1's winners, two by two the second round's ties */
    fix_pairs(x->teamGridPairings, x->base.matchPairsPerRound / 2, x->base.field_2E, 2);
#endif
}
