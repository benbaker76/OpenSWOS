/*
 * world_europe.c -- a world's European cups as knockouts (include/world_europe.h).
 */
#include "world_europe.h"
#include "world.h"
#include "competition_data.h"

extern CompetitionCareer _careerContests[];

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
