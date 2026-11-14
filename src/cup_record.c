/*
 * cup_record.c -- what a world career keeps of its cup matches for the cups' SUMMARY (include/cup_record.h;
 * docs/WORLD_2026_27.md 6m). Recorded on both targets (the save is the same); shown by src/conti_view.c.
 */
#include "cup_record.h"
#include "world.h"
#include "world_cups.h"
#include "world_conti.h"
#include "league_phase.h"
#include "diy_competition.h"
#include "comp_identity.h"
#include "endian_helpers.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc on the Amiga) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

extern DIYCompetitionEx _diyCompetitionStart;

CupRecordState g_cupRecord;

void CupRecord_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_cupRecord;
    for (uint32_t i = 0; i < sizeof g_cupRecord; i++)
        p[i] = 0;
}

#if SWOS_WORLDS

static uint8_t season_now(void) { return (uint8_t)(g_worldCups.seasons + 1); }
static uint8_t edition_now(void) { return (uint8_t)(CT_ST.season + 1); }

void CupRecord_SeasonStart(void)       /* (a new season: the European record starts again) */
{
    g_cupRecord.euroSeason = 0;
    g_cupRecord.euroComp = 0;
    g_cupRecord.nTable = 0;
    g_cupRecord.nGames = 0;
}

void CupRecord_LeaguePhaseEnd(uint8_t compId, const uint16_t *teams, const uint16_t *points, int32_t n)
{
    extern uint16_t _selTeamNumber, _isNationalSelector;
    int32_t in = 0;
    if (!World_Current() || _isNationalSelector)
        return;
    if (n > CUP_RECORD_TABLE)
        n = CUP_RECORD_TABLE;
    for (int32_t i = 0; i < n; i++)
        in |= teams[i] == _selTeamNumber;
    if (!in)                            /* (only the career club's own European cup, as the Mod) */
        return;
    g_cupRecord.euroSeason = season_now();
    g_cupRecord.euroComp = compId;
    g_cupRecord.nGames = 0;
    for (int32_t i = 0; i < n; i++) {
        g_cupRecord.table[i] = teams[i];
        g_cupRecord.points[i] = (uint8_t)(points[i] > 255 ? 255 : points[i]);
    }
    g_cupRecord.nTable = (uint8_t)n;
}

static void put(CupRecGame *g, const TeamFileEx *t1, const TeamFileEx *t2, uint16_t g1, uint16_t g2,
                uint16_t flags, uint16_t p1, uint16_t p2, int32_t league, uint8_t round)
{
    g->round = round;
    g->t1 = PACK_BE_U16(t1->team.countryNumber, t1->team.teamOrdinal);
    g->t2 = PACK_BE_U16(t2->team.countryNumber, t2->team.teamOrdinal);
    g->g1 = (uint8_t)(g1 > 99 ? 99 : g1);
    g->g2 = (uint8_t)(g2 > 99 ? 99 : g2);
    g->flags = 0;
    g->p1 = g->p2 = 0;
    if (league) {
        g->flags = CUPREC_LEAGUE;
        return;
    }
    if (flags & 0x20) g->flags |= CUPREC_LEG2;          /* (the knockout's flags byte, diy_competition.c) */
    if (flags & 4) g->flags |= CUPREC_ET;
    if (flags & 2) {
        g->flags |= CUPREC_DECIDED;
        if (flags & 1) g->flags |= CUPREC_T2WON;
    }
    if (flags & 8) {
        g->flags |= CUPREC_PENS;
        g->p1 = (uint8_t)(p1 > 99 ? 99 : p1);
        g->p2 = (uint8_t)(p2 > 99 ? 99 : p2);
    }
}

void CupRecord_Match(const TeamFileEx *t1, const TeamFileEx *t2, uint16_t g1, uint16_t g2, uint16_t flags,
                     uint16_t p1, uint16_t p2, int32_t league)
{
    if (!World_Current() || !t1 || !t2)
        return;
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    if (LeaguePhase_IsCup(id)) {
        if (league || !g_cupRecord.nTable || g_cupRecord.euroComp != id)
            return;                     /* (the league phase itself: its final table; another club's cup) */
        if (g_cupRecord.nGames < CUP_RECORD_GAMES) {
            /* (its stage, 1 the first knockout one: a first leg or a single match opens the next stage when
             * the current one has had its second legs, or already has one of the two clubs) */
            const uint16_t a = PACK_BE_U16(t1->team.countryNumber, t1->team.teamOrdinal);
            const uint16_t b = PACK_BE_U16(t2->team.countryNumber, t2->team.teamOrdinal);
            const int32_t n = g_cupRecord.nGames;
            uint8_t stage = n ? g_cupRecord.games[n - 1].round : 1;
            if (n && !(flags & 0x20)) {
                int32_t next = 0;
                for (int32_t i = 0; i < n; i++) {
                    const CupRecGame *g = &g_cupRecord.games[i];
                    if (g->round == stage && ((g->flags & CUPREC_LEG2) || g->t1 == a || g->t2 == a ||
                                              g->t1 == b || g->t2 == b))
                        next = 1;
                }
                stage = (uint8_t)(stage + next);
            }
            put(&g_cupRecord.games[g_cupRecord.nGames++], t1, t2, g1, g2, flags, p1, p2, 0, stage);
        }
        return;
    }
    if ((id == COMPID_contiTie || id == COMPID_contiMain) && CT_ED.mark == 0x4543 && CT_SEG.kind != 0xFF) {
        if (g_cupRecord.contiEdition != edition_now()) {
            g_cupRecord.contiEdition = edition_now();
            g_cupRecord.nConti = 0;
        }
        if (g_cupRecord.nConti < CUP_RECORD_CONTI)
            put(&g_cupRecord.conti[g_cupRecord.nConti++], t1, t2, g1, g2, flags, p1, p2, league,
                (uint8_t)(CT_SEG.cup << 4 | (CT_SEG.round & 15)));
    }
}

const CupRecordState *CupRecord_Euro(void)
{
    return World_Current() && g_cupRecord.nTable ? &g_cupRecord : 0;
}

const CupRecordState *CupRecord_Conti(void)
{
    return World_Current() && g_cupRecord.contiEdition && g_cupRecord.contiEdition == edition_now() &&
           g_cupRecord.nConti ? &g_cupRecord : 0;
}

#else

void CupRecord_Match(const TeamFileEx *t1, const TeamFileEx *t2, uint16_t g1, uint16_t g2, uint16_t flags,
                     uint16_t p1, uint16_t p2, int32_t league)
{
    (void)t1; (void)t2; (void)g1; (void)g2; (void)flags; (void)p1; (void)p2; (void)league;
}
void CupRecord_LeaguePhaseEnd(uint8_t compId, const uint16_t *teams, const uint16_t *points, int32_t n)
{
    (void)compId; (void)teams; (void)points; (void)n;
}
const CupRecordState *CupRecord_Euro(void) { return 0; }
void CupRecord_SeasonStart(void) {}
const CupRecordState *CupRecord_Conti(void) { return 0; }

#endif
