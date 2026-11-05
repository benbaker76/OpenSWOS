/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_copa.c — Copa Libertadores career qualification (CONMEBOL).
 *
 * Part of the competition dynamic / pointer-free refactor (Inc3 — see
 * docs/COMPETITION_DYNAMIC_REFACTOR.md §8d). Copa Libertadores is a brand-new,
 * data-only continental CLUB cup (compId 162, defined purely in
 * tournaments.json — no compiled record). Its career field is earned each
 * season: the champion + runner-up of each of the 10 CONMEBOL leagues feed the
 * NEXT season's Copa, giving exactly 10 x 2 = 20 teams.
 *
 * This is the single-cup, top-2-of-each-league analog of career_eurocups.c's
 * three-cup European qualification. It deliberately mirrors that file's model
 * (eligible-competition-id consume list + append-to-the-cup-*Copy-seed-list +
 * TMD-pool registration) so the two stay structurally parallel and back-port
 * to the Amiga tree the same way.
 *
 * SLICE 3a: the qualification STATE + ROUTING + the prepare/qualify functions
 * only. These are NOT yet hooked into the season transition
 * (Career_AdvanceToNextSeason_UpdateCompetitions) — that, the next-season
 * *Copy rebuild (Career_UpdateCopaLibertadoresCompetitionSlot), and the
 * draw-readiness/validation gate are slice 3b. Until 3b wires them in these are
 * dead but unit-testable in isolation.
 */

#include "game_types.h"
#include "comp_identity.h"
#include "competition_data.h"
#include "career_teams.h"
#include "conti_season.h"

/* ---- routing: the 10 CONMEBOL league competition indices ----
 *
 * These are country numbers, matching _currentCompetitionIndex (set by
 * Career_ProcessCompetitionForNextSeason, career_season.c) and the entries in
 * _careerCompetitionIdRemapTable (competition_data.c) — all 10 are present
 * there, so every CONMEBOL league is simulated each season and produces a
 * champion + runner-up. Argentina 43, Bolivia 45, Brazil 46, Chile 48,
 * Colombia 49, Ecuador 50, Paraguay 64, Peru 65, Uruguay 71, Venezuela 77.
 * 0xFF-terminated like the euro-cup eligible lists. */
static const uint8_t s_copaEligibleMaster[] = {
    43, 45, 46, 48, 49, 50, 64, 65, 71, 77, 0xFF
};

/* Mutable working copy: copaTryConsume marks a consumed entry 0xFE so each
 * league qualifies at most once per season. Reset from the master at the start
 * of every season transition by Career_PrepareCopaLibertadoresQualificationState. */
static uint8_t s_copaEligibleWork[sizeof s_copaEligibleMaster];

/* Transient qualification cursor (recomputed each transition; the team data
 * itself lives in the Copa *Copy seed list, which the save layer serializes —
 * so no new persistent state is introduced here). */
static uint8_t  *s_copaTeamListPtr;
static uint8_t  *s_copaTeamListWritePtr;
static uint16_t  s_copaQualifiedTeamCount;

/* Cached top-9 finishers of the just-processed league (slot 0 = champion,
 * slot 1 = runner-up, ...), filled by DIY_CachePrimaryQualifiedTeamsGrid. */
extern uint8_t  _primaryQualifiedTeamGrids[];
extern uint8_t  _currentCompetitionIndex;

/* Auto-advance (background-simulate) a competition from its record — used by the
 * play-path slot update (mirrors how Career_UpdateEuroCupCompetitionSlot drives
 * euroCup). Defined in career_status.c. */
extern void Career_AutoAdvanceLeagueCompetitionFromRecord(uint8_t *record);

/* Interactive play-path machinery (a player managing a CONMEBOL club). Copa
 * shares the slot-3 (quaternary) slot with the euro cups — mutually exclusive,
 * since a club is either European or South-American — so it reuses the same
 * slot-3 context-load (Career_CopyEuroCupsCompetitionContextToOrFromActive
 * just loads the generic slot-3 buffer). Routing into the slot is done by
 * Career_SelectCopaLibertadoresSlotIfTeamParticipates (career_init.c). */
extern uint8_t *_quaternaryCompetitionPtr;
extern void RestoreSelectedTeams(void);
extern void SaveSelectedTeams(void);
extern void Career_CopyEuroCupsCompetitionContextToOrFromActive(void);
extern void DIY_EditCompetitionSlot(uint16_t paramA, uint16_t paramB, uint16_t paramC);

/* ================================================================
 * Helpers (mirror career_eurocups.c)
 * ================================================================ */

/* Resolve the working *Copy of a dynamic competition by compId. The dynamic
 * Copa comp was appended to _careerContests by the JSON loader
 * (DataJson_InstallCompetitionGraph), so its {template, copy} pair lives there.
 * Comp_ById returns the TEMPLATE record; we want the per-career working *Copy
 * (the one the season machinery edits and the save reconstructs). Returns NULL
 * if Copa isn't installed (data not present).
 *
 * Exported (career_init.c's Copa slot-selection reaches Copa's *Copy through
 * this, mirroring how the euro cascade names _euroCupCopy directly). */
__attribute__((used, externally_visible))
Competition *Career_CopaWorkingCopy(void)
{
    Competition *tmpl = Comp_ById(COMPID_copaLibertadores);
    if (!tmpl || Conti_ReplacesCopa())  /* (a world's own cups take its place: conti_season.c) */
        return NULL;
    for (int i = 0; i < CAREER_CONTESTS_CAP; i++) {
        if (_careerContests[i].competition == tmpl)
            return _careerContests[i].competitionCopy;
    }
    return NULL;
}

/* Career_InstallCopaLibertadoresPresetEntry — make Copa Libertadores selectable
 * in the "CHOOSE PRESET COMPETITION - SOUTH AMERICA" menu.
 *
 * That menu (SelectTeamsForPresetCompetition -> SelectTeamsFinalMenu, teams.c)
 * builds its competition list by walking _southAmericaTable.compCup[] — a
 * compiled token stream that ships with only _southAmericaWorldCupQualification
 * and _copaAmerica (competition_data.c). Copa Libertadores is a dynamic (heap)
 * competition on SDL, so there is no compiled symbol to list in that static
 * initializer; instead we splice its TEMPLATE record (the same standalone/preset
 * field _copaAmerica is listed by — NOT the per-career *Copy) into the compCup[]
 * token stream at runtime, just before the -1 terminator.
 *
 * Idempotent (re-running finds Copa already present and returns) and inert when
 * Copa is not installed (Comp_ById(162) == NULL, e.g. SWOS_DATA_JSON disabled or
 * a build without the Copa data) or the fixed-size list has no free slot. Called
 * once from FrontEndPreLoopSetup on BOTH ports — after the SDL dynamic loader
 * (DataJson_InstallCompetitionGraph) or the Amiga bake (CopaBaked_Install) has
 * registered the record — so the single shared code path keeps the trees in sync. */
__attribute__((used, externally_visible))
void Career_InstallCopaLibertadoresPresetEntry(void)
{
    Competition *copa = Comp_ById(COMPID_copaLibertadores);
    if (!copa)
        return;

    Competition **cc = _southAmericaTable.compCup;
    const int cap = (int)(sizeof _southAmericaTable.compCup / sizeof cc[0]);  /* 9 */
    int i = 0;
    /* Walk to the -1 terminator; bail out if Copa is already spliced in. */
    while (i < cap && cc[i] != (Competition *)-1) {
        if (cc[i] == copa)
            return;                 /* already installed */
        i++;
    }
    if (i >= cap - 1)               /* need room for Copa + the moved terminator */
        return;
    cc[i]     = copa;               /* overwrite the -1 slot with Copa Libertadores */
    cc[i + 1] = (Competition *)-1;  /* re-plant the -1 terminator after it */
}

/* appendTeamToCopaList — mirror of appendTeamToList (career_eurocups.c). The
 * grid's first two bytes are the team's {country, ordinal}; preserve that byte
 * order in the list (it is both the TeamId struct layout and the BE byte-pair
 * convention BE_READ_U16 consumers + the save round-trip expect). Also register
 * the team in the TMD pool so VerifyTeamLoaded can find it when the cup runs. */
static void appendTeamToCopaList(uint8_t *teamGrid)
{
    if (!s_copaTeamListWritePtr)
        return;
    s_copaTeamListWritePtr[0] = teamGrid[0];
    s_copaTeamListWritePtr[1] = teamGrid[1];
    s_copaTeamListWritePtr += 2;
    s_copaQualifiedTeamCount++;

    /* Stage the team in the TMD pool so it is loadable when Copa is contested
     * (mirrors the euro qualifier). This adds 20 Copa teams on top of the 80
     * European-cup teams every season transition appends — the tmdFileBuffer was
     * enlarged 80 -> 128 (career_team_buffers.c) to hold them, and the
     * _careerFileBuffer == 0x50 (80-append) season-transition gate was
     * generalised to 0x50 + the Copa count (career_season.c) so the season still
     * completes. */
    Career_AppendTeamToTmdTeamPool((TeamFileEx *)teamGrid);
}

/* copaTryConsume — mirror of tryConsumeCompetitionIdFromList: find
 * _currentCompetitionIndex in the byte list, mark it 0xFE (consumed), return 1. */
static int16_t copaTryConsume(uint8_t *list)
{
    uint8_t id = _currentCompetitionIndex;
    while (*list != 0xFF) {
        if (*list == id) {
            *list = 0xFE;
            return 1;
        }
        list++;
    }
    return 0;
}

/* ================================================================
 * Exported entry points
 * ================================================================ */

/* Career_PrepareCopaLibertadoresQualificationState — analog of
 * Career_PrepareEuropeanCupQualificationState. Resets the eligible working
 * list, points the team-list write cursor at the Copa *Copy's embedded seed
 * list (reached via comp_team_seed_list so the 64-bit name[2] pointer-growth
 * delta is handled), and zeroes the qualified count.
 *
 * Unlike the euro cups there is NO random fill: the field is exactly the
 * champion + runner-up of the 10 CONMEBOL leagues (20 teams), appended during
 * qualification, so the cursor starts at the seed list head with count 0. */
__attribute__((used, externally_visible))
void Career_PrepareCopaLibertadoresQualificationState(void)
{
    for (unsigned i = 0; i < sizeof s_copaEligibleMaster; i++)
        s_copaEligibleWork[i] = s_copaEligibleMaster[i];

    s_copaQualifiedTeamCount = 0;

    Competition *copy = Career_CopaWorkingCopy();
    if (copy) {
        s_copaTeamListPtr      = (uint8_t *)comp_team_seed_list(copy);
        s_copaTeamListWritePtr = s_copaTeamListPtr;
    } else {
        s_copaTeamListPtr      = (uint8_t *)0;
        s_copaTeamListWritePtr = (uint8_t *)0;
    }
}

/* Career_ProcessCompetitionForCopaLibertadoresQualification — analog of
 * Career_ProcessCompetitionForEuropeanCupQualification. Called per-competition
 * during the season transition, AFTER Career_ProcessCompetitionForNextSeason
 * has set _currentCompetitionIndex and cached the league's finishers into
 * _primaryQualifiedTeamGrids. If the just-processed competition is one of the
 * 10 CONMEBOL leagues, qualify its champion (grid slot 0) and runner-up
 * (grid slot 1) into next season's Copa field. */
__attribute__((used, externally_visible))
void Career_ProcessCompetitionForCopaLibertadoresQualification(void)
{
    if (!copaTryConsume(s_copaEligibleWork))
        return;
    appendTeamToCopaList(_primaryQualifiedTeamGrids + 0 * sizeof(TeamFileEx)); /* champion  */
    appendTeamToCopaList(_primaryQualifiedTeamGrids + 1 * sizeof(TeamFileEx)); /* runner-up */
}

/* Career_CopaLibertadoresQualifiedTeamCount — accessor for the season-transition
 * readiness/validation gate (slice 3b) and for unit tests. */
__attribute__((used, externally_visible))
uint16_t Career_CopaLibertadoresQualifiedTeamCount(void)
{
    return s_copaQualifiedTeamCount;
}

/* Career_CopaLibertadoresExpectedTeamCount — the count the season-transition
 * validation should assert: 20 (10 CONMEBOL leagues x champion+runner-up) when
 * Copa is INSTALLED (compId 162 present as a data competition), else 0. Keying
 * on installation keeps the validation correct on a build WITHOUT the Copa data
 * (e.g. SWOS_DATA_JSON disabled, or the Amiga tree before the bake) — there the
 * qualifier appends nothing, so 0 == 0 and the season-transition gate stays the
 * original 0x50. */
__attribute__((used, externally_visible))
uint16_t Career_CopaLibertadoresExpectedTeamCount(void)
{
    return Comp_ById(COMPID_copaLibertadores) && !Conti_ReplacesCopa() ? 20 : 0;
}

/* Career_UpdateCopaLibertadoresCompetitionSlot — analog of
 * Career_UpdateEuroCupCompetitionSlot's non-interactive branch. Auto-advances
 * (simulates) Copa Libertadores in the background each season, using the field
 * set by the PREVIOUS season's qualification (the preset 1996 field on the first
 * Copa-enabled season). Career_AutoAdvanceLeagueCompetitionFromRecord loads the
 * field from the record's own seed list and runs the group->knockout, so the
 * teams need not be pre-staged in selTeamsPtr.
 *
 * When the player's own CONMEBOL club is in Copa this season it is the active
 * quaternary competition (routed by
 * Career_SelectCopaLibertadoresSlotIfTeamParticipates in career_init.c, exactly
 * as the euro cascade routes _euroCupCopy): take the INTERACTIVE branch and show
 * the cup menu so the player plays/sims it — the analog of
 * Career_UpdateEuroCupCompetitionSlot's `a4 == _quaternaryCompetitionPtr` path.
 * Copa reuses the slot-3 context buffer (shared with the euro cups, mutually
 * exclusive), so the same context-load applies. No title-holder grid copy: Copa
 * has no holder berth (the field is fully re-earned each season). Inert when Copa
 * is not installed. */
__attribute__((used, externally_visible))
void Career_UpdateCopaLibertadoresCompetitionSlot(void)
{
    Competition *copy = Career_CopaWorkingCopy();
    if (!copy)
        return;

    uint8_t *a4 = (uint8_t *)copy;
    if (a4 == _quaternaryCompetitionPtr) {
        /* INTERACTIVE: the player's club is in Copa — let them play it. */
        RestoreSelectedTeams();
        Career_CopyEuroCupsCompetitionContextToOrFromActive();
        DIY_EditCompetitionSlot(1, 1, 0);
        SaveSelectedTeams();
    } else {
        /* AUTO-ADVANCE (background-simulate): a non-CONMEBOL career, or a
         * CONMEBOL career whose club did not qualify this season. */
        Career_AutoAdvanceLeagueCompetitionFromRecord(a4);
    }
}
