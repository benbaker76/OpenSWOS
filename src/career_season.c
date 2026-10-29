/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_season.c — Career season management, qualification, team list building.
 *
 * Chunks 67-68: 2 exported functions ported from SWOS2.s.
 */

#include "career_season.h"
#include "world.h"
#include "career_eurocups.h"
#include "career_init.h"
#include "career_status.h"
#include "career_teams.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "swos_platform.h"   /* HLOG — the SDL Makefile force-includes this; the
                               Amiga Makefile doesn't, so include it explicitly */
#include "job_offers.h"
#include "panic.h"
#include "league_phase.h"
#include "random.h"
#include "statistics.h"
#include "transfer_market.h"
#include "league_playoffs.h"
#include "world_playoff.h"
#include "world_cups.h"

extern uint8_t _diyLeagueSeasonStateTable[];
extern uint8_t _diyLeagueScratchBuffer[];

/* ---- ASM externs ---- */
extern uint16_t _selTeamNumber;
extern uint16_t _isNationalSelector;
extern uint16_t _teamNoOfferAccepted;
extern uint16_t _seasonReview;
extern volatile uint16_t _currentMessage;

extern uint8_t _euroCupCopy[];
extern uint8_t _cupWinnersCupCopy[];
extern uint8_t _uefaCupCopy[];

extern uint8_t *_euroCupTeamListPtr;
extern uint8_t *_euroCupTeamListWritePtr;
extern uint8_t *_cupWinnersCupTeamListPtr;
extern uint8_t *_cupWinnersCupTeamListWritePtr;
extern uint8_t *_uefaCupTeamListPtr;
extern uint8_t *_uefaCupTeamListWritePtr;

extern uint16_t _euroCupQualifiedTeamCount;
extern uint16_t _cupWinnersCupQualifiedTeamCount;
extern uint16_t _uefaCupQualifiedTeamCount;

extern uint8_t _euroCupQualGroupSeedLists_4_6_11[];
extern uint8_t _euroCupQualGroupSeedListsReduced_4_6_11[];
extern uint8_t _euroCupQualGroupSeedBuckets_15_18_22[];

extern void *_countriesTable[];

extern uint8_t _euroCupEligibleCompetitionIds[];
extern uint8_t _cupWinnersCupEligibleCompetitionIds[];
extern uint8_t _uefaCupEligibleCompetitionIds[];

/* ================================================================
 * Career_FillRandomTeamListExcludingSelected  (SWOS2.s line 34278)
 *
 * Fills competition team list from a table of seed group entries,
 * performing a Fisher-Yates shuffle on each group. Tracks how
 * many times the selected team naturally appears and injects
 * additional copies at the end if needed.
 *
 * Asm: d0 = excludeCount, a0 = table, a1 = out
 *
 * Table format: each entry is {uint32_t groupPtr, uint16_t groupSize,
 *   uint16_t copyCount}, terminated by groupPtr == 0xFFFFFFFF.
 *
 * After filling: appends 0xFF terminator, then if excludeCount
 * natural appearances haven't been found, walks backward and
 * replaces non-selTeam entries with selTeam to reach the target.
 *
 * NOTE: selTeamNumber MSB = country/league index (68k move.b reads MSB
 * on big-endian). Seed list entries are country indices, so we compare
 * against (selTeamNumber >> 8).
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_FillRandomTeamListExcludingSelected(uint16_t excludeCount,
                                                  uint8_t *table, uint8_t *compIndexs)
{
    /* move.b (selTeamNumber).l,d0 — reads MSB on big-endian 68k */
    uint8_t selTeam = (uint8_t)(_selTeamNumber >> 8);
    int16_t d7 = (int16_t)excludeCount;
    uint8_t *out = compIndexs;

    /* Iterate as the typed struct so the per-entry stride matches the
     * pointer width on both targets. The original code advanced `table`
     * by hardcoded 4+2+2=8 bytes per iteration, which is correct on
     * 32-bit Amiga but skips 4 bytes per entry on 64-bit (where the
     * pointer field is 8 bytes wide, making each entry 12 bytes total).
     * That misalignment SIGSEGV'd inside the seed-list copy when
     * advancing from EUROCUP qualification setup at season transition.
     * Struct mirrors the definition in swos_amiga_ptr_imports.c. */
    typedef struct __attribute__((packed)) {
        void    *ptr;
        uint16_t size;
        uint16_t count;
    } SeedListEntryLocal;
    SeedListEntryLocal *e = (SeedListEntryLocal *)table;
    for (;;) {
        uint8_t *groupPtr = (uint8_t *)e->ptr;
        if (groupPtr == (uint8_t *)(uintptr_t)-1)
            break;

        uint16_t groupSize = e->size;
        uint16_t copyCount = e->count;
        e++;

        /* Fisher-Yates shuffle if groupSize != copyCount */
        if (groupSize != copyCount) {
            uint16_t n = groupSize;     /* d3 = groupSize */
            for (int16_t i = (int16_t)(groupSize - 1); i >= 0; i--) {
                uint16_t r = Rand2();
                /* mulu.w d3,d0; lsr.w #8,d0 — index = low_word(r * n) >> 8 */
                uint16_t idx = (uint16_t)((uint32_t)r * n) >> 8;
                /* Swap groupPtr[i] and groupPtr[idx] */
                uint8_t tmp = groupPtr[i];
                groupPtr[i] = groupPtr[idx];
                groupPtr[idx] = tmp;
            }
        }

        /* Copy copyCount entries, tracking selTeam matches */
        for (int16_t j = (int16_t)(copyCount - 1); j >= 0; j--) {
            *out++ = *groupPtr++;
            if (*(out - 1) == selTeam) {
                d7--;   /* one less replacement needed */
            }
        }
    }

    /* Append 0xFF terminator */
    *out = 0xFF;

    /* Check if we still need to inject selTeam entries */
    d7--;
    if (d7 < 0)
        return;

    /* Walk backward, skip selTeam entries, replace others with selTeam */
    do {
        while (*(--out) == selTeam)
            ;   /* skip entries already containing selTeam */
        *out = selTeam;
    } while (--d7 >= 0);
}

/* ================================================================
 * Career_PrepareEuropeanCupQualificationState  (SWOS2.s line 33743)
 *
 * Sets up team lists for European Cup, Cup Winners' Cup, and UEFA Cup
 * qualification draws. For each cup:
 *   - Computes team list write pointer from cup copy data
 *   - Clears qualified team count
 *   - Determines exclude count (1 if player's country is European)
 *   - Calls Career_FillRandomTeamListExcludingSelected to fill the
 *     shuffled team list
 *
 * UEFA Cup has a special pre-scan: counts how many times the selected
 * team's country index appears across all seed bucket data (contiguous
 * from euroCupQualSeedBucketBlock.bucket15 through .bucket22Full,
 * terminated by 0xFF).
 *
 * Temporarily overrides selTeamNumber from teamNoOfferAccepted if a
 * national team transfer is pending (team byte in 0x50-0x55 range).
 * Saves and restores both selTeamNumber and isNationalSelector.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_PrepareEuropeanCupQualificationState(void)
{
    /* Save state — asm: move.w (selTeamNumber),-(sp); move.w (isNationalSelector),-(sp) */
    uint16_t savedSelTeam = _selTeamNumber;
    uint16_t savedIsNational = _isNationalSelector;

    /* If a team offer has been accepted, temporarily use that team number */
    if (_teamNoOfferAccepted != 0xFFFF) {
        _selTeamNumber = _teamNoOfferAccepted;
        /* move.b (selTeamNumber).l,d0 — reads MSB (country/league index) */
        uint8_t teamByte = (uint8_t)(_selTeamNumber >> 8);
        if (teamByte >= 0x50 && teamByte <= 0x55) {
            /* National team range — set flag (asm: move.b #1,(isNationalSelector)) */
            _isNationalSelector = 1;
        }
    }

    /* --- European Cup --- */
    {
        uint8_t *base = _euroCupCopy;
        /* base[7] is the 32-bit Amiga byte distance from base+7 to the
         * embedded unk1[] qualified-team list. Every cup *Copy struct has a
         * `char *text[2]` pair between the descriptor and unk1[]; on 64-bit
         * those two pointers widen 8->16 bytes, pushing unk1[] +8. Without
         * this delta the list pointer lands inside text[1], the qualified
         * teams overwrite the name pointer, and DrawMenuText later
         * dereferences team-id bytes as a string (SIGSEGV). Same
         * compensation as Career_SelectEuroCupSlotIfTeamParticipates
         * (career_init.c) and the AltTableHeader readers (career_teams.c). */
        uint16_t offset = base[7] + 2u * (sizeof(char *) - sizeof(uint32_t));
        uint8_t *teamList = &base[7 + offset];
        _euroCupTeamListPtr = teamList;
        _euroCupTeamListWritePtr = teamList;
        _euroCupQualifiedTeamCount = 0;

        uint16_t excludeCount = 0;
        if (_isNationalSelector == 0) {
            uint8_t teamByte = (uint8_t)(_selTeamNumber >> 8);
            uint8_t *countryPtr = (uint8_t *)_countriesTable[teamByte];
            if (*countryPtr == 0) {
                excludeCount = 1;
            }
        }
        Career_FillRandomTeamListExcludingSelected(excludeCount,
            _euroCupQualGroupSeedLists_4_6_11, _euroCupEligibleCompetitionIds);
    }

    /* --- Cup Winners' Cup --- */
    {
        uint8_t *base = _cupWinnersCupCopy;
        /* +text[2]-growth delta — see European Cup block above. */
        uint16_t offset = base[7] + 2u * (sizeof(char *) - sizeof(uint32_t));
        uint8_t *teamList = &base[7 + offset];
        _cupWinnersCupTeamListPtr = teamList;
        _cupWinnersCupTeamListWritePtr = teamList;
        _cupWinnersCupQualifiedTeamCount = 0;

        uint16_t excludeCount = 0;
        if (_isNationalSelector == 0) {
            uint8_t teamByte = (uint8_t)(_selTeamNumber >> 8);
            uint8_t *countryPtr = (uint8_t *)_countriesTable[teamByte];
            if (*countryPtr == 0) {
                excludeCount = 1;
            }
        }
        Career_FillRandomTeamListExcludingSelected(excludeCount,
            _euroCupQualGroupSeedListsReduced_4_6_11, _cupWinnersCupEligibleCompetitionIds);
    }

    /* --- UEFA Cup --- */
    {
        uint8_t *base = _uefaCupCopy;
        /* +text[2]-growth delta — see European Cup block above. */
        uint16_t offset = base[7] + 2u * (sizeof(char *) - sizeof(uint32_t));
        uint8_t *teamList = &base[7 + offset];
        _uefaCupTeamListPtr = teamList;
        _uefaCupTeamListWritePtr = teamList;
        _uefaCupQualifiedTeamCount = 0;

        /* Special count: scan ALL seed bucket data (contiguous from first bucket
         * through euroCupQualSeedBucketBlock.bucket22Full which ends with 0xFF) to count how
         * many times the selected team's country index appears.
         * Asm: movea.l (euroCupQualGroupSeedBuckets_15_18_22).l,a0 — loads first
         * entry's pointer (to start of contiguous bucket data). */
        uint8_t *bucketData = *(uint8_t **)_euroCupQualGroupSeedBuckets_15_18_22;
        uint16_t excludeCount = 0;
        if (_isNationalSelector == 0) {
            uint8_t teamByte = (uint8_t)(_selTeamNumber >> 8);
            while (*bucketData != 0xFF) {
                if (*bucketData == teamByte) {
                    excludeCount++;
                }
                bucketData++;
            }
        }
        Career_FillRandomTeamListExcludingSelected(excludeCount,
            _euroCupQualGroupSeedBuckets_15_18_22, _uefaCupEligibleCompetitionIds);
    }

    /* A world's league phases: each country's own places (include/league_phase.h), not 96/97's
     * drawn ones -- the eligible lists one country a place, in the world's order. */
    {
        const uint8_t (*places)[4];
        int32_t n;
        if (World_LeaguePhaseCups(&places, &n)) {
            uint8_t *lists[3] = { _euroCupEligibleCompetitionIds, _cupWinnersCupEligibleCompetitionIds,
                                  _uefaCupEligibleCompetitionIds };
            for (int32_t c = 0; c < 3; c++) {
                int32_t k = 0;
                for (int32_t i = 0; i < n; i++)
                    for (int32_t j = 0; j < places[i][1 + c] && k < 39; j++)
                        lists[c][k++] = places[i][0];
                lists[c][k] = 0xFF;
            }
        }
    }

    /* Restore state — asm: move.w (sp)+,(isNationalSelector); move.w (sp)+,(selTeamNumber) */
    _isNationalSelector = savedIsNational;
    _selTeamNumber = savedSelTeam;
}

/* ---- Chunk 145: GetCompetitionAbbrevStr ---- */
/* SWOS2.s line 54905 — returns abbreviation string for a competition pointer.
 * Compares pointer values against known competition buffer addresses. */

extern uint8_t _friendlyCopy[];
extern uint8_t _worldCup2Copy[];
extern uint8_t _worldCupQualifyingGroup1Copy[];
extern uint8_t _oceaniaWorldCupQualificationCopy[];
extern uint8_t _europeanChampionships2Copy[];
extern uint8_t _ecQualifyingGroup1Copy[];
extern uint8_t _ecQualifyingGroup8Copy[];
extern uint8_t _northAmericanCupCopy[];
extern uint8_t _copaAmericaCopy[];
extern uint8_t _africanNationsCupCopy[];
extern uint8_t _asianCupCopy[];
extern uint8_t _oceaniaCupCopy[];
extern uint8_t _invitationTournamentCopy[];
extern uint8_t _tourCopy[];

extern uint16_t _panicReasonCode;
extern void PanicHaltLoop(void);

__attribute__((used, externally_visible))
uint8_t *GetCompetitionAbbrevStr(uint8_t *competition)
{
    if (competition == 0)
        return (uint8_t *)"FR";

    /* By-id dispatch (was by-*Copy-address). compId is shared by template
     * and *Copy, so this matches both and no longer depends on the *Copy layout. */
    uint8_t id = comp_id(competition);
    if (id == COMPID_friendly)
        return (uint8_t *)"FR";
    if (id == COMPID_worldCup2)
        return (uint8_t *)"WC";
    if (comp_is_wc_qualifier(id))
        return (uint8_t *)"WCQ";
    if (id == COMPID_europeanChampionships2)
        return (uint8_t *)"EC";
    if (comp_is_ec_qualifier(id))
        return (uint8_t *)"ECQ";
    if (id == COMPID_northAmericanCup)
        return (uint8_t *)"NAC";
    if (id == COMPID_copaAmerica)
        return (uint8_t *)"CA";
    if (id == COMPID_africanNationsCup)
        return (uint8_t *)"ANC";
    if (id == COMPID_asianCup)
        return (uint8_t *)"AC";
    if (id == COMPID_oceaniaCup)
        return (uint8_t *)"OC";
    if (id == COMPID_invitationTournament)
        return (uint8_t *)"TNM";
    if (id == COMPID_tour)
        return (uint8_t *)"TR";

    _panicReasonCode = 0x2184;
    PanicHaltLoop();
    return (uint8_t *)"FR"; /* unreachable */
}

/* ================================================================
 * Chunk 161: ShowSeasonOpinion  (SWOS2.s line 35922)
 *
 * Maps the season review score (0-4+) to a chairman message constant
 * and calls ShowChairmanMessage.
 * ================================================================ */
extern void ShowChairmanMessage(void);

#define EXCELLENT_JOB_THIS_SEASON   27
#define GOOD_SEASON                 28
#define UP_AND_DOWN_SEASON2         29
#define NOT_A_VERY_GOOD_SEASON2     30
#define VERY_DISSAPOINTING_SEASON   31

__attribute__((used, externally_visible))
void ShowSeasonOpinion(void)
{
    uint16_t d6 = _seasonReview;
    uint16_t msg;
    if (d6 >= 4)
        msg = VERY_DISSAPOINTING_SEASON;
    else if (d6 == 3)
        msg = NOT_A_VERY_GOOD_SEASON2;
    else if (d6 == 2)
        msg = UP_AND_DOWN_SEASON2;
    else if (d6 == 1)
        msg = GOOD_SEASON;
    else
        msg = EXCELLENT_JOB_THIS_SEASON;

    _currentMessage = msg;
    ShowChairmanMessage();
}

/* ================================================================
 * Career_UpdateLastNationalityCall (SWOS2.s line 35081)
 *
 * Determines which national team the current player's nationality
 * maps to, and stores it in lastNationalityCall (or 0xFFFF if the
 * player's national team IS the selected team, or if not found).
 *
 * Returns the value stored in lastNationalityCall.
 * ================================================================ */
extern uint8_t  _plNationality;
extern uint16_t _lastNationalityCall;
extern int16_t GetNationalTeamNumber(uint16_t nationality);
extern int16_t GetNationalTeamNumberFromTeamFileId(uint16_t teamFileId);

__attribute__((used, externally_visible))
uint16_t Career_UpdateLastNationalityCall(void)
{
    int16_t d0;
    uint8_t nat = _plNationality;

    if (nat != 0xFF) {
        d0 = GetNationalTeamNumber((uint16_t)nat);
        if (d0 != (int16_t)0xFFFF)
            goto check_self;
    }

    /* Nationality not found or is 0xFF — try team file ID fallback */
    d0 = (int16_t)0xFFFF;
    if (_isNationalSelector != 0)
        goto check_self;

    /* move.b (selTeamNumber).l,d0 — reads high byte on big-endian 68k.
     * This extracts the team file ID (country file index) from selTeamNumber. */
    d0 = GetNationalTeamNumberFromTeamFileId(_selTeamNumber >> 8);

check_self:
    /* If the national team IS the selected team, set to 0xFFFF (no call-up) */
    if ((uint16_t)d0 == _selTeamNumber) {
        d0 = (int16_t)0xFFFF;
    }

    _lastNationalityCall = (uint16_t)d0;
    return (uint16_t)d0;
}

/* ================================================================
 * Chunk 174: Season Review functions (SWOS2.s lines 35525-35785)
 *
 * 4 functions:
 *   SeasonReview_AdjustWeightsForCompetition (static helper)
 *   SeasonReview_ComputeCategoryForCompetition (static helper)
 *   SeasonFinished_ComputeSeasonReview
 *   ShowChairmanOpinionAboutTheSeason
 * ================================================================ */

extern CompetitionSlotBuffer _primaryCompetitionBuf;          /* slot-0 untangle (S6): typed buffer */
extern CompetitionSlotBuffer _competitionContextBuf;          /* slot-1 untangle (S4): typed buffer */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;   /* slot-2 untangle (S3): typed buffer */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf;   /* slot-3 untangle (S5): typed buffer */

extern uint8_t *_currentCompetitionPtr;
extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;
extern uint8_t *_quaternaryCompetitionPtr;

extern uint16_t _wonWorldCup;
extern uint16_t _wonContinentalCup;
extern uint16_t _findTeamInDIY_SuppressSideEffectsFlag;

/* ---- SeasonReview_AdjustWeightsForCompetition (SWOS2.s line 35686) ----
 * Classifies a competition by id, then multiplies the category by a weight
 * factor and accumulates the weight.
 * Only called from SeasonFinished_ComputeSeasonReview (static). */
static void adjustWeightsForCompetition(uint8_t *compPtr, uint16_t *weightAccum,
                                         uint16_t *weightedCategory)
{
    /* compPtr may be NULL (e.g. an empty primary slot); the original only
     * compared the pointer, so NULL/unknown fell through to the domestic
     * default. 0xFF matches no COMPID, preserving that. */
    uint8_t id = compPtr ? comp_id(compPtr) : 0xFFu;

    /* World Cup: weight 8 */
    if (id == COMPID_worldCup2) {
        *weightedCategory *= 8;
        *weightAccum += 8;
        return;
    }
    /* Friendly / Invitation Tournament / Tour: weight 1, d7 NOT multiplied */
    if (id == COMPID_friendly || id == COMPID_invitationTournament ||
        id == COMPID_tour) {
        *weightAccum += 1;
        return;
    }
    /* Continental cups: weight 6 */
    if (id == COMPID_europeanChampionships2 || id == COMPID_northAmericanCup ||
        id == COMPID_copaAmerica || id == COMPID_africanNationsCup ||
        id == COMPID_asianCup || id == COMPID_oceaniaCup) {
        *weightedCategory *= 6;
        *weightAccum += 6;
        return;
    }
    /* World Cup qualifiers (range check): weight 4 */
    if (comp_is_wc_qualifier(id)) {
        *weightedCategory *= 4;
        *weightAccum += 4;
        return;
    }
    /* Default: domestic competition, weight 3 */
    *weightedCategory *= 3;
    *weightAccum += 3;
}

/* ---- SeasonReview_ComputeCategoryForCompetition (SWOS2.s line 35733) ----
 * Computes season review category (0=best, 4=worst) for a competition buffer.
 * Returns -1 if team not found. Only called from SeasonFinished_ComputeSeasonReview. */
static int16_t computeCategoryForCompetition(uint8_t *comp)
{
    _findTeamInDIY_SuppressSideEffectsFlag = 1;
    int16_t position = (int16_t)FindTeamInDIY(_selTeamNumber, (DIYCompetition *)comp);
    _findTeamInDIY_SuppressSideEffectsFlag = 0;

    if (position < 0)
        return -1;

    uint16_t rank = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)comp);
    uint16_t numTeams = (uint16_t)((DIYCompetition *)comp)->numTeams;  /* 0x32; typed access auto-corrects the 64-bit namePtr shift */

    /* ratio = (rank - position) * 100 / numTeams (signed) */
    int32_t product = (int32_t)(int16_t)((int16_t)rank - position) * 100;
    int16_t ratio = (int16_t)(product / (int16_t)numTeams);

    int16_t category = 0;
    if (position != 0) {
        if (ratio < 30) {
            category = 1;
            if (ratio < 10) {
                category = 2;
                if (ratio <= -10) {
                    category = 3;
                    if (ratio <= -30)
                        category = 4;
                }
            }
        }
    }

    /* Position percentile (unsigned): position * 100 / numTeams */
    uint16_t percentile = (uint16_t)((uint16_t)position * 100) / numTeams;

    /* Top 25%: upgrade category by 1 (guarded: not if already 0) */
    if (percentile <= 25 && category != 0)
        category--;

    /* Bottom 75%: downgrade category by 1 (guarded: not if already 4) */
    if (percentile >= 75 && category != 4)
        category++;

    return category;
}

/* ---- SeasonFinished_ComputeSeasonReview (SWOS2.s line 35638) ----
 * Computes weighted average season review from primary and secondary
 * competitions, with bonuses for World Cup / Continental Cup wins.
 * Stores result in seasonReview global. */
__attribute__((used, externally_visible))
void SeasonFinished_ComputeSeasonReview(void)
{
    uint16_t d5 = 0;  /* weight accumulator */
    uint16_t d6 = 0;  /* weighted score accumulator */

    /* Primary competition */
    int16_t cat = computeCategoryForCompetition((uint8_t *)&_primaryCompetitionBuf);
    if (cat >= 0) {
        uint16_t ucat = (uint16_t)cat;
        adjustWeightsForCompetition((uint8_t *)_currentCompetitionPtr, &d5, &ucat);
        d6 += ucat;
    }

    /* Secondary competition (if exists) */
    if (_secondaryCompetitionPtr != 0) {
        cat = computeCategoryForCompetition((uint8_t *)&_competitionContextBuf);
        if (cat >= 0) {
            uint16_t ucat = (uint16_t)cat;
            adjustWeightsForCompetition((uint8_t *)_secondaryCompetitionPtr, &d5, &ucat);
            d6 += ucat;
        }
    }

    /* Weighted average with rounding: (d6 + d5/2) / d5.
     * Asm `divu.w d5,d6` on a zero divisor triggers a m68k trap #5;
     * the Amiga default handler resumes seamlessly with d6 unchanged
     * (in our path d6 is also 0 because no comp contributed a
     * category, so the implicit result is 0). On x86 the same divide
     * raises SIGFPE — guard it. Reached on national-selector mode
     * when neither the primary nor secondary comp's team list
     * contains selTeamNumber (e.g. first FINISH SEASON for GERMANY
     * managing only national selector with no qualified-team match
     * yet — Playaveli's repro). */
    if (d5 != 0) {
        d6 += (d5 >> 1);
        /* ext.l d6; divu.w d5,d6 — sign-extend d6 word to long, unsigned divide */
        uint32_t d6_32 = (uint32_t)(int32_t)(int16_t)d6;
        d6 = (uint16_t)(d6_32 / (uint32_t)d5);
    } else {
        d6 = 0;
    }

    /* Bonus: World Cup win → best rating; Continental Cup win → -2 */
    if (_wonWorldCup != 0) {
        d6 = 0;
    } else if (_wonContinentalCup != 0) {
        d6 -= 2;
    }

    /* Clamp to >= 0 */
    if ((int16_t)d6 < 0)
        d6 = 0;

    _seasonReview = d6;
}

/* ---- ShowChairmanOpinionAboutTheSeason (SWOS2.s line 35525) ----
 * For national teams, delegates to ShowSeasonOpinion.
 * For club teams, computes category with inline logic (same as ComputeCategory
 * but WITHOUT suppressing side effects), checks secondary/tertiary/quaternary
 * competitions for top-2 finishes, then maps category to a chairman message. */
#define CHAIRMAN_MSG_EXCELLENT   13
#define CHAIRMAN_MSG_GOOD        14
#define CHAIRMAN_MSG_AVERAGE     15
#define CHAIRMAN_MSG_POOR        16
#define CHAIRMAN_MSG_TERRIBLE    17

#include "career_params.h"

__attribute__((used, externally_visible))
void ShowChairmanOpinionAboutTheSeason(void)
{
    /* National teams → use season review score */
    if (_isNationalSelector != 0) {
        ShowSeasonOpinion();
        return;
    }

    /* --- Compute category for primary competition (inline) --- */
    uint8_t *comp = (uint8_t *)&_primaryCompetitionBuf;
    int16_t position = (int16_t)FindTeamInDIY(_selTeamNumber, (DIYCompetition *)comp);
    uint16_t rank = GetMoreExpensiveTeamsCount(_selTeamNumber, (DIYCompetition *)comp);
    uint16_t numTeams = (uint16_t)((DIYCompetition *)comp)->numTeams;  /* 0x32; typed access auto-corrects the 64-bit namePtr shift */

    int32_t product = (int32_t)(int16_t)((int16_t)rank - position) * 100;
    int16_t ratio = (int16_t)(product / (int16_t)numTeams);

    int16_t d7 = 0;
    if (position != 0) {
        if (ratio < _careerParams.board_ratio_ok) {
            d7 = 1;
            if (ratio < _careerParams.board_ratio_good) {
                d7 = 2;
                if (ratio <= _careerParams.board_ratio_poor) {
                    d7 = 3;
                    if (ratio <= _careerParams.board_ratio_terrible)
                        d7 = 4;
                }
            }
        }
    }

    /* Percentile adjustments (NO guards for d7==0 or d7==4) */
    uint16_t percentile = (uint16_t)((uint16_t)position * 100) / numTeams;
    if (percentile <= _careerParams.board_percentile_low)
        d7--;
    if (percentile >= _careerParams.board_percentile_high)
        d7++;

    /* Secondary competition: top-2 finish → -2 */
    if (_secondaryCompetitionPtr != 0) {
        int16_t pos2 = (int16_t)FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_competitionContextBuf);
        if ((uint16_t)pos2 < 2)
            d7 -= _careerParams.board_secondary_top2_bonus;
    }

    /* Tertiary competition: top-2 finish → -2 */
    if (_tertiaryCompetitionPtr != 0) {
        int16_t pos3 = (int16_t)FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_tertiaryCompetitionContextBuf);
        if ((uint16_t)pos3 < 2)
            d7 -= _careerParams.board_tertiary_top2_bonus;
    }

    /* Quaternary competition (Euro cups): top-2 finish → -3 */
    if (_quaternaryCompetitionPtr != 0) {
        int16_t pos4 = (int16_t)FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_euroCupsCompetitionContextBuf);
        if ((uint16_t)pos4 < 2)
            d7 -= _careerParams.board_euro_top2_bonus;
    }

    /* Clamp d7 to >= 0 */
    if (d7 < 0)
        d7 = 0;

    /* Map category to chairman message */
    uint16_t msg;
    if (d7 >= 4)
        msg = CHAIRMAN_MSG_TERRIBLE;
    else if (d7 == 3)
        msg = CHAIRMAN_MSG_POOR;
    else if (d7 == 2)
        msg = CHAIRMAN_MSG_AVERAGE;
    else if (d7 == 1)
        msg = CHAIRMAN_MSG_GOOD;
    else
        msg = CHAIRMAN_MSG_EXCELLENT;

    _currentMessage = msg;
    ShowChairmanMessage();
}

/* ---- Chunk 181: BeginContractNegotiationSequence ---- */

extern uint16_t _contractNegotiationActive;
extern uint16_t _timeToNegotiate;
extern uint16_t _lastOfferPrimaryPlayerOrdinal;
extern uint16_t _offerNegotiationAttemptCount;
extern uint16_t _financeTotalsNeedResetFlag;

extern void Career_MaybeGenerateJobOffer(void);

/* BeginContractNegotiationSequence (SWOS2.s line 28035)
 * Called after a career match to begin contract negotiation phase.
 * Sets up negotiation globals, commits finances, processes offer expiry,
 * and potentially generates a new job offer.
 * a0=comp (diyCompetitionStart), a1=team (the home side), a2=opponent: both passed through to
 * UpdateCareerFinancesAfterMatch, whose gate factors weigh the two clubs (the original keeps
 * a2 from the result code; the C used to pass the home team twice). */
__attribute__((used, externally_visible))
void BeginContractNegotiationSequence(DIYCompetitionEx *comp, TeamFileEx *team, TeamFileEx *opponent)
{
    _contractNegotiationActive = 1;
    _timeToNegotiate = 2;
    _lastOfferPrimaryPlayerOrdinal = 0xFFFF;
    _offerNegotiationAttemptCount = 0;
    Career_CommitNewBalanceAndResetFinanceTotals();
    _financeTotalsNeedResetFlag = 1;
    UpdateCareerFinancesAfterMatch(comp, team, opponent);
    ProcessOfferExpiryCountdown();
    JobOffers_MaybeExpireSomeNewOffers();
    Career_MaybeGenerateJobOffer();
}

/* ---- Chunk 182: Competition Season Processing ---- */

extern uint8_t *_secondaryCompetitionPtr;
extern uint8_t *_tertiaryCompetitionPtr;

extern void *_competitionsTable[];
extern uint16_t _cachedTertiaryQualifiedTeamId;
extern uint16_t _panicReasonCode;
extern uint8_t _currentCompetitionIndex;

/* Forward declaration for static function defined in Chunk 186 below */
static void processPrimaryCompetitionForNextSeason(uint8_t *competition);

/* Career_ProcessSecondaryCompetitionForNextSeason (SWOS2.s line 31973)
 * Process secondary competition for next season: if active, edit interactively;
 * otherwise auto-advance as cup and cache qualified grid. */
__attribute__((used, externally_visible))
void Career_ProcessSecondaryCompetitionForNextSeason(uint8_t *competition)
{
    if (((Competition *)competition)->type != 1) {
        _panicReasonCode = 0x3003;
        PanicHaltLoop();
        return;
    }
    if (competition == _secondaryCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyCompetitionContextToOrFromActive();
        ShowDIYCupMenuWithParams(1, 1, 0);
        DIY_CacheSecondaryQualifiedTeamsGrid();
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceCupCompetitionFromRecord(competition);
        DIY_CacheSecondaryQualifiedTeamsGrid();
    }
}

/* Career_ProcessTertiaryCompetitionForNextSeason (SWOS2.s line 32000)
 * Same pattern for tertiary competition. */
__attribute__((used, externally_visible))
void Career_ProcessTertiaryCompetitionForNextSeason(uint8_t *competition)
{
    if (((Competition *)competition)->type != 1) {
        _panicReasonCode = 0x3003;
        PanicHaltLoop();
        return;
    }
    if (competition == _tertiaryCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyTertiaryCompetitionContextToOrFromActive();
        ShowDIYCupMenuWithParams(1, 1, 0);
        DIY_CacheTertiaryQualifiedTeamGrid();
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceCupCompetitionFromRecord(competition);
        DIY_CacheTertiaryQualifiedTeamGrid();
    }
    /* a world's league cup with no European place (world_cups.c): its winner takes none */
    if (!WorldCups_LeagueCupEurope(competition))
        _cachedTertiaryQualifiedTeamId = 0xFFFF;
}

/* Career_ProcessCompetitionForNextSeason (SWOS2.s line 31908)
 * Main dispatcher: processes primary, secondary, tertiary competitions.
 * d0 = competition ID. */
__attribute__((used, externally_visible))
void Career_ProcessCompetitionForNextSeason(uint16_t compIndex)
{
    _currentCompetitionIndex = (uint8_t)compIndex;
    _cachedTertiaryQualifiedTeamId = 0xFFFF;

    /* Walk competitionsTable[idx]'s sub-table as void** so each slot is
     * native pointer-width. Was uint32_t* — read half-pointers on 64-bit. */
    void **tableEntry = (void **)_competitionsTable[compIndex & 0xFF];

    /* Entry layout: [0]=primary ptr, [1]=skip, [2]=secondary ptr, [3]=tertiary ptr */
    uint8_t *primary = (uint8_t *)tableEntry[0];
    processPrimaryCompetitionForNextSeason(primary);

    uint8_t *secondary = (uint8_t *)tableEntry[2];
    if (secondary != (uint8_t *)(uintptr_t)-1) {
        Career_ProcessSecondaryCompetitionForNextSeason(secondary);

        uint8_t *tertiary = (uint8_t *)tableEntry[3];
        if (tertiary != (uint8_t *)(uintptr_t)-1) {
            Career_ProcessTertiaryCompetitionForNextSeason(tertiary);
        }
    }

    DIY_ValidateLeagueScratchBuffer();

    /* A world's play-offs and play-outs: after the original's moves are checked, the
     * places they decide (one-level moves, placed by InitializeNewSeason's groups pass). */
    LeaguePlayoffs_Decide(compIndex & 0xFF, (const int8_t *)_diyLeagueSeasonStateTable,
                          (int8_t *)_diyLeagueScratchBuffer);
}

/* ================================================================
 * Chunk 185: Career Cup Pick List System (SWOS2.s lines 32019-33004)
 *
 * 13 functions: pickRandomNationalTeamId, fillWorldCupQualifyingGroupTeams,
 * setupWorldCupQualifyingGroups, setCupPickListCommon (helper for 5 regions),
 * setCupPickList_Europe, setupFriendlyCupTeams, setupInvitationTournamentTeams,
 * setupTourCupTeams, Career_SetCupPickList_ForSelectedRegion (exported).
 * ================================================================ */

/* --- Additional externs for Chunk 185 --- */
extern uint8_t _someTeamFlags;
extern uint16_t _qualifyingStage1PendingFlag0;
extern uint16_t _qualifyingStage1PendingFlag1;
extern uint16_t _worldCupHostTeam;
extern uint16_t _worldCupQualifyingTeam;
extern uint16_t _euroChampionshipQualTeam;
extern uint16_t _natTeamsRandLimit2;
extern uint8_t _nationalTeams[];
/* careerCupPickList: a 5-entry array of COMPETITION POINTERS (not uint32) —
 * holds the national-coach cup-pick-list comp records (copied from the
 * cupPick* tables, which competition_data.c defines as CompetitionCup *[5]).
 * Was extern'd uint32_t[] here, which truncated the 64-bit pointers and made
 * Rebuild_CompetitionSlotPointers (reads it as void**) deref garbage. Now a
 * pointer array; see the standalone definition in swos_data_typed_stubs.c. */
extern void *_careerCupPickList[];
extern TeamFileEx _careerTeam;
extern uint8_t _teamSortBuffer[];

/* Competition qualification copies */
extern uint8_t _worldCupQualifyingGroup8Copy[];
extern uint8_t _northAmericaWorldCupQualificationCopy[];
extern uint8_t _southAmericaWorldCupQualificationCopy[];
extern uint8_t _africaWorldCupQualificationCopy[];
extern uint8_t _asiaWorldCupQualificationCopy[];

/* Cup pick list data arrays (stay in asm, contain relocatable pointers) */
extern void *_cupPickOceaniaWorldCupFriendly[];
extern void *_cupPickOceaniaWorldCupInvite[];
extern void *_cupPickOceaniaOceaniaCupFinalist[];
extern void *_cupPickOceaniaOceaniaCupNonFinalist[];
extern void *_cupPickOceaniaOceaniaCupFriendly[];
extern void *_cupPickOceaniaInviteFriendly[];

extern void *_cupPickNAmericaWorldCupFriendly[];
extern void *_cupPickNAmericaWorldCupInvite[];
extern void *_cupPickNAmericaNaCupFinalist[];
extern void *_cupPickNAmericaNaCupNonFinalist[];
extern void *_cupPickNAmericaNaCupFriendly[];
extern void *_cupPickNAmericaInviteFriendly[];

extern void *_cupPickAsiaWorldCupFriendly[];
extern void *_cupPickAsiaWorldCupInvite[];
extern void *_cupPickAsiaAsianCupFinalist[];
extern void *_cupPickAsiaAsianCupNonFinalist[];
extern void *_cupPickAsiaAsianCupFriendly[];
extern void *_cupPickAsiaInviteFriendly[];

extern void *_cupPickAfricaWorldCupFriendly[];
extern void *_cupPickAfricaWorldCupInvite[];
extern void *_cupPickAfricaAfricanCupFinalist[];
extern void *_cupPickAfricaAfricanCupNonFinalist[];
extern void *_cupPickAfricaAfricanCupFriendly[];
extern void *_cupPickAfricaInviteFriendly[];

extern void *_cupPickSAmericaWorldCupFriendly[];
extern void *_cupPickSAmericaWorldCupInvite[];
extern void *_cupPickSAmericaCopaAmericaFinalist[];
extern void *_cupPickSAmericaCopaAmericaNonFinalist[];
extern void *_cupPickSAmericaCopaAmericaFriendly[];
extern void *_cupPickSAmericaInviteFriendly[];

extern void *_cupPickListEuropeWorldCupFriendly[];
extern void *_cupPickLisEuropeWorldCupInviteFriendly[];
extern void *_cupPickLisEuropeEuroChampsFriendly[];
extern void *_cupPickListEuropeEuroChampsInviteFriendly[];
extern void *_cupPickListEuropeInviteTournamentFriendly[];
extern void *_cupPickEuropeNoCupFriendly[];
extern void *_cupPickEuropeNoCupFriendlyAlt[];
extern void *_cupPickListEuropeInviteTournamentFriendlyAlt[];

/* Real storage is `CompetitionCup *[8]` in competition_data.c:1175.
 * Same trap shape as worldCupQualifyingGroupsTable below — see
 * comment there. Use `void *[]` so the stride is native pointer-width. */
extern void    *_ecQualifyingGroupsTable[];
/* Real storage is `CompetitionCup *[8]` in competition_data.c:1191
 * — native pointer-width table. The legacy extern said `uint32_t[]`
 * (Amiga 32-bit pointer slot) which read half a pointer on 64-bit
 * at every `(uint8_t *)tbl[i]` consumer site. Declared as
 * `void *[]` here to avoid pulling in competition_data.h's other
 * typedefs (some clash with locals in this file). */
extern void    *_worldCupQualifyingGroupsTable[];
/* Real storage is `char *tourStringTable[6]` (competition_data.c). Declaring
 * it uint32_t here truncated each entry to 4 bytes on 64-bit (wrong stride
 * AND a truncated pointer value), corrupting the runtime tour-name store. */
extern char    *_tourStringTable[];

/* Forward declarations */
static void setupWorldCupQualifyingGroups(uint16_t **teamListPtr);

/* ---- pickRandomNationalTeamId (SWOS2.s line 32962) ----
 * Picks a random national team, optionally filtered by region and price.
 * Retries up to 100 times with price filtering, then continues without.
 * Ensures no duplicates against the current list. */
static uint16_t pickRandomNationalTeamId(uint16_t minPrice, uint16_t maxPrice,
                                          uint8_t regionFilter,
                                          uint16_t *currentList, uint16_t countSoFar)
{
    int16_t retries = 99;
    for (;;) {
        uint16_t r = Rand();
        uint32_t idx = (uint32_t)r * _natTeamsRandLimit2;
        idx >>= 8;
        NationalTeamRecord *rec = (NationalTeamRecord *)_nationalTeams + (uint16_t)idx;
        /* teamId is compared against selTeamNumber (BE-packed), so
         * compose the same form via PACK_BE_U16 on either host. */
        uint16_t teamId = PACK_BE_U16(rec->countryNumber, rec->teamOrdinal);

        if (regionFilter != 0) {
            if (rec->countryNumber != regionFilter)
                continue;
        }
        if (teamId == _selTeamNumber)
            continue;

        if (retries >= 0) {
            uint16_t price = rec->skillOrPrice;
            if (price < minPrice || price > maxPrice) {
                retries--;
                continue;
            }
        }

        if (countSoFar == 0)
            return teamId;

        /* currentList holds BE-packed team ids (setupFriendlyCupTeams /
         * setupInvitationTournamentTeams / setupTourCupTeams now use
         * BE_WRITE_U16 to match the dc.b storage). Read the same way
         * so the host-native teamId compares equal to the matching
         * entry on both endians. */
        int16_t dup = 0;
        uint16_t *p = currentList;
        for (uint16_t i = countSoFar; i > 0; i--) {
            if (BE_READ_U16(p) == teamId) { dup = 1; break; }
            p++;
        }
        if (!dup)
            return teamId;

        if (retries >= 0)
            retries--;
    }
}

/* ---- fillWorldCupQualifyingGroupTeams (SWOS2.s line 32761) ----
 * Fills a world cup qualifying group with teams from the sorted list.
 * Places the player's team if their region matches. */
static void fillWorldCupQualifyingGroupTeams(uint8_t regionCode, uint8_t *group,
                                              uint16_t **teamListPtr)
{
    Career_SortTeamFilePtrsByAvgPlayerPrice(regionCode);
    /* Sort leaves a6=teamSortBuffer in the asm; re-point our explicit
     * teamListPtr at the same buffer the sort just populated so the
     * `*(*teamListPtr)++` reads walk the sorted team ids for this
     * region. See setCupPickList_Europe for the full rationale. */
    *teamListPtr = (uint16_t *)_teamSortBuffer;
    uint8_t *origGroup = group;
    uint8_t compType = group[1];
    int16_t teamCount;

    if (compType == 0) {
        teamCount = (int8_t)group[0xD];
    } else if (compType == 2) {
        teamCount = (int8_t)group[0xF];
    } else {
        _panicReasonCode = 0x7623;
        PanicHaltLoop();
        return;
    }

    uint8_t hdrSize = group[7];
    /* +text[2]-growth delta — the embedded unk1[] team list sits 8 bytes
     * later on 64-bit (cup struct's char *text[2] widens 8->16). Without it
     * the writes land in text[1] and stomp the comp name pointer. Matches
     * the readers in career_teams.c / career_init.c. */
    uint16_t *teamSlots = (uint16_t *)(group + 7 + (hdrSize & 0xFF)
                          + 2u * (sizeof(char *) - sizeof(uint32_t)));

    uint16_t firstTeam;
    if (_selTeamNumber != _worldCupHostTeam &&
        _selTeamNumber != _worldCupQualifyingTeam &&
        (uint8_t)(_selTeamNumber >> 8) == regionCode) {
        _careerCupPickList[0] = (void *)origGroup;
        firstTeam = _selTeamNumber;
    } else {
        do {
            firstTeam = *(*teamListPtr)++;
        } while (firstTeam == _selTeamNumber ||
                 firstTeam == _worldCupHostTeam ||
                 firstTeam == _worldCupQualifyingTeam);
    }

    /* Slot writes use BE byte order to match the static-init `dc.b
     * country,ordinal` competition data — downstream
     * AppendTeamsToSelectionFromTmdOrTeamFile reads each slot with
     * BE_READ_U16, so host-native writes on LE produced byte-swapped
     * ids (Playaveli's GERMANY.CAR repro: VerifyTeamLoaded(0x0350)
     * panic after the new sort fix had stored the correct 0x5003 in
     * teamSortBuffer). asm `move.w` writes BE on m68k; mirror with
     * BE_WRITE_U16 so both endians match the dc.b layout. */
    BE_WRITE_U16(teamSlots, firstTeam); teamSlots++;
    for (int16_t i = teamCount - 1; i > 0; i--) {
        uint16_t teamId;
        do {
            teamId = *(*teamListPtr)++;
        } while (teamId == _selTeamNumber ||
                 teamId == _worldCupHostTeam ||
                 teamId == _worldCupQualifyingTeam);
        BE_WRITE_U16(teamSlots, teamId); teamSlots++;
    }
}

/* ---- setupWorldCupQualifyingGroups (SWOS2.s line 32649) ----
 * Sets up all world cup qualifying groups: 8 European groups (column-major fill),
 * then 5 continental groups. Only runs during continental cup year (bit 6). */
static void setupWorldCupQualifyingGroups(uint16_t **teamListPtr)
{
    if (!(_someTeamFlags & 0x40))
        return;

    /* Calculate group 8 team count based on European host/qualifying */
    int16_t d5 = 6;
    int skipQualCheck = 0;
    if ((uint8_t)(_worldCupHostTeam >> 8) == 0x50) {
        d5--;
        if (_worldCupHostTeam == _worldCupQualifyingTeam)
            skipQualCheck = 1;
    }
    if (!skipQualCheck && (uint8_t)(_worldCupQualifyingTeam >> 8) == 0x50)
        d5--;
    if (d5 & 1)
        d5--;
    _worldCupQualifyingGroup8Copy[0xD] = (uint8_t)d5;

    Career_SortTeamFilePtrsByAvgPlayerPrice(0x50);
    /* Sort leaves a6=teamSortBuffer in the asm — see comment in
     * fillWorldCupQualifyingGroupTeams. Re-point teamListPtr at the
     * just-populated buffer. */
    *teamListPtr = (uint16_t *)_teamSortBuffer;
    uint16_t randOff = Rand2() & 0x1C;
    uint8_t *selectedGroup = (uint8_t *)_worldCupQualifyingGroupsTable[randOff >> 2];
    int16_t slotsRemaining = 0x30 - 6 + d5;
    d5 <<= 1;
    int16_t colOffset = 0;

    for (;;) {
        for (int16_t g = 0; g < 8; g++) {
            uint8_t *group = (uint8_t *)_worldCupQualifyingGroupsTable[g];
            uint8_t hdrSize = group[7];
            /* +text[2]-growth delta — see fillWorldCupQualifyingGroupTeams. */
            uint16_t *slot = (uint16_t *)(group + 7 + (hdrSize & 0xFF)
                            + 2u * (sizeof(char *) - sizeof(uint32_t)) + colOffset);

            uint16_t teamId;
            if (colOffset == 0 && group == selectedGroup &&
                _selTeamNumber != _worldCupHostTeam &&
                _selTeamNumber != _worldCupQualifyingTeam &&
                (uint8_t)(_selTeamNumber >> 8) == 0x50) {
                _careerCupPickList[0] = (void *)group;
                teamId = _selTeamNumber;
            } else {
                if (comp_id(group) == COMPID_worldCupQualifyingGroup8 && colOffset >= d5)
                    continue;
                do {
                    teamId = *(*teamListPtr)++;
                } while (teamId == _selTeamNumber);
            }

            while (teamId == _worldCupHostTeam || teamId == _worldCupQualifyingTeam) {
                do {
                    teamId = *(*teamListPtr)++;
                } while (teamId == _selTeamNumber);
            }

            /* BE write — see fillWorldCupQualifyingGroupTeams comment. */
            BE_WRITE_U16(slot, teamId);
            if (--slotsRemaining == 0)
                goto wc_groups_done;
        }
        colOffset += 2;
    }

wc_groups_done:
    fillWorldCupQualifyingGroupTeams(0x53, _northAmericaWorldCupQualificationCopy, teamListPtr);
    fillWorldCupQualifyingGroupTeams(0x52, _southAmericaWorldCupQualificationCopy, teamListPtr);
    fillWorldCupQualifyingGroupTeams(0x51, _africaWorldCupQualificationCopy, teamListPtr);
    fillWorldCupQualifyingGroupTeams(0x54, _asiaWorldCupQualificationCopy, teamListPtr);

    /* Oceania: adjust team count based on Oceanian host/qualifying */
    uint8_t oceaniaCount = 4;
    int skipOceaniaQual = 0;
    if ((uint8_t)(_worldCupHostTeam >> 8) == 0x55) {
        oceaniaCount--;
        if (_worldCupHostTeam == _worldCupQualifyingTeam)
            skipOceaniaQual = 1;
    }
    if (!skipOceaniaQual && (uint8_t)(_worldCupQualifyingTeam >> 8) == 0x55)
        oceaniaCount--;
    if (oceaniaCount & 1)
        oceaniaCount--;
    _oceaniaWorldCupQualificationCopy[0xD] = oceaniaCount;
    fillWorldCupQualifyingGroupTeams(0x55, _oceaniaWorldCupQualificationCopy, teamListPtr);
}

/* ---- setCupPickListCommon (helper for 5 simple regional functions) ----
 * Implements the identical cup pick list selection logic used by Oceania,
 * NorthAmerica, Asia, Africa, and SouthAmerica. */
static void setCupPickListCommon(void **worldCupFriendly, void **worldCupInvite,
                                  void **cupFinalist, void **cupNonFinalist,
                                  void **cupFriendly, void **inviteFriendly,
                                  uint16_t **teamListPtr)
{
    void **src = worldCupFriendly;
    if (_someTeamFlags & 0x80) {
        if (_qualifyingStage1PendingFlag0 == 0)
            src = worldCupInvite;
    } else if (_someTeamFlags & 0x40) {
        src = cupFinalist;
        if (_selTeamNumber != _worldCupHostTeam && _selTeamNumber != _worldCupQualifyingTeam)
            src = cupNonFinalist;
    } else {
        src = cupFriendly;
        if (!(_someTeamFlags & 0x02)) {
            if (_someTeamFlags != 0) {
                _panicReasonCode = 0x3775;
                PanicHaltLoop();
                return;
            }
            src = inviteFriendly;
        }
    }

    _careerCupPickList[0] = src[0];
    _careerCupPickList[1] = src[1];
    _careerCupPickList[2] = src[2];
    _careerCupPickList[3] = src[3];
    _careerCupPickList[4] = src[4];

    Career_RandomizeInvitationTournamentCup();
    setupWorldCupQualifyingGroups(teamListPtr);
}

/* ---- setCupPickList_Europe (SWOS2.s line 32469) ----
 * Europe has a different flag structure and includes EC group filling. */
static void setCupPickList_Europe(uint16_t **teamListPtr)
{
    void **src;
    if (_someTeamFlags & 0x80) {
        src = _cupPickListEuropeWorldCupFriendly;
        if (_qualifyingStage1PendingFlag0 == 0)
            src = _cupPickLisEuropeWorldCupInviteFriendly;
    } else if (_someTeamFlags & 0x02) {
        src = _cupPickLisEuropeEuroChampsFriendly;
        if (_qualifyingStage1PendingFlag1 == 0)
            src = _cupPickListEuropeEuroChampsInviteFriendly;
    } else if (_someTeamFlags & 0x40) {
        src = _cupPickListEuropeInviteTournamentFriendlyAlt;
        if (_selTeamNumber != _worldCupHostTeam && _selTeamNumber != _worldCupQualifyingTeam)
            src = _cupPickEuropeNoCupFriendlyAlt;
    } else if (_someTeamFlags & 0x01) {
        src = _cupPickListEuropeInviteTournamentFriendly;
        if (_selTeamNumber != _euroChampionshipQualTeam)
            src = _cupPickEuropeNoCupFriendly;
    } else {
        _panicReasonCode = 0x3775;
        PanicHaltLoop();
        return;
    }

    _careerCupPickList[0] = src[0];
    _careerCupPickList[1] = src[1];
    _careerCupPickList[2] = src[2];
    _careerCupPickList[3] = src[3];
    _careerCupPickList[4] = src[4];

    Career_RandomizeInvitationTournamentCup();
    setupWorldCupQualifyingGroups(teamListPtr);

    /* EC group filling (only during EC qualifying year, bit 0) */
    if (!(_someTeamFlags & 0x01))
        return;

    Career_SortTeamFilePtrsByAvgPlayerPrice(0x50);
    /* Asm "register-return" pattern: Career_SortTeamFilePtrsByAvgPlayerPrice's
     * final instruction is `lea (teamSortBuffer).l,a6` and its movem doesn't
     * restore a6, so the sort intentionally leaks a6 = teamSortBuffer to the
     * caller. Career_SetCupPickList_Europe's group-fill loop then reads
     * `(a6)+` to consume the just-sorted team id list. Our C translation
     * threaded a teamListPtr through but the outer caller passes NULL — fix
     * by pointing it at teamSortBuffer here, after the sort populates it. */
    *teamListPtr = (uint16_t *)_teamSortBuffer;
    uint16_t randIdx = (uint16_t)((uint32_t)Rand2() * 7 >> 8);
    uint8_t *selectedGroup = (uint8_t *)_ecQualifyingGroupsTable[randIdx];
    int16_t slotsRemaining = 0x2E; /* 46 */
    int16_t colOffset = 0;

    for (;;) {
        for (int16_t g = 0; g < 8; g++) {
            uint8_t *group = (uint8_t *)_ecQualifyingGroupsTable[g];
            uint8_t hdrSize = group[7];
            /* +text[2]-growth delta — see fillWorldCupQualifyingGroupTeams. */
            uint16_t *slot = (uint16_t *)(group + 7 + (hdrSize & 0xFF)
                            + 2u * (sizeof(char *) - sizeof(uint32_t)) + colOffset);

            uint16_t teamId;
            int readFromList = 1;
            if (colOffset == 0 && group == selectedGroup) {
                if (_selTeamNumber != _euroChampionshipQualTeam) {
                    _careerCupPickList[0] = (void *)group;
                    teamId = _selTeamNumber;
                    readFromList = 0;
                }
            }

            if (readFromList) {
                if (comp_id(group) == COMPID_ecQualifyingGroup8 && (uint16_t)colOffset >= 8)
                    continue;
                do {
                    teamId = *(*teamListPtr)++;
                } while (teamId == _selTeamNumber);
            }

            while (teamId == _euroChampionshipQualTeam) {
                do {
                    teamId = *(*teamListPtr)++;
                } while (teamId == _selTeamNumber);
            }

            /* BE write — see fillWorldCupQualifyingGroupTeams comment. */
            BE_WRITE_U16(slot, teamId);
            if (--slotsRemaining == 0)
                return;
        }
        colOffset += 2;
    }
}

/* ---- setupFriendlyCupTeams (SWOS2.s line 32860) ----
 * Fills the friendly competition team list with teams of similar strength. */
static void setupFriendlyCupTeams(void)
{
    uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
    uint16_t minPrice = (uint16_t)((uint32_t)avgPrice * 75 / 100);
    uint16_t maxPrice = (uint16_t)((uint32_t)avgPrice * 125 / 100);

    CompetitionCup *comp = (CompetitionCup *)_friendlyCopy;
    int16_t teamCount = comp->header.roundStringBase;
    /* teamSeedList = the embedded draw list, a NAMED field (offset handled by the
     * struct, 64-bit-safe). Do NOT use comp_team_seed_list(): it type-gates on
     * Competition.type (cup/tournament only) and returns NULL for these type-0
     * friendly/invitation CUPs, so the BE_WRITE_U16 below wrote to NULL — the
     * "national coach then crash" SIGSEGV in the national-selector season's
     * InitializeNewSeason -> Career_SetCupPickList_ForSelectedRegion. */
    uint16_t *teamList = (uint16_t *)comp->teamSeedList;
    uint16_t *listBase = teamList;

    /* BE writes so the slot bytes match the static-init dc.b layout
     * (downstream AppendTeamsToSelectionFromTmdOrTeamFile reads with
     * BE_READ_U16). pickRandomNationalTeamId's dup-check also reads
     * via BE_READ_U16. */
    BE_WRITE_U16(teamList, _selTeamNumber); teamList++;
    uint16_t countSoFar = 1;

    for (int16_t i = teamCount - 2; i >= 0; i--) {
        uint16_t teamId = pickRandomNationalTeamId(minPrice, maxPrice, 0, listBase, countSoFar);
        BE_WRITE_U16(teamList, teamId); teamList++;
        countSoFar++;
    }
}

/* ---- setupInvitationTournamentTeams (SWOS2.s line 32889) ----
 * Fills the invitation tournament team list (same pattern as friendly). */
static void setupInvitationTournamentTeams(void)
{
    uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
    uint16_t minPrice = (uint16_t)((uint32_t)avgPrice * 75 / 100);
    uint16_t maxPrice = (uint16_t)((uint32_t)avgPrice * 125 / 100);

    CompetitionCup *comp = (CompetitionCup *)_invitationTournamentCopy;
    int16_t teamCount = comp->header.roundStringBase;
    /* teamSeedList named field — see setupFriendlyCupTeams (comp_team_seed_list()
     * returns NULL for this type-0 invitation CUP). */
    uint16_t *teamList = (uint16_t *)comp->teamSeedList;
    uint16_t *listBase = teamList;

    /* BE writes — see setupFriendlyCupTeams comment. */
    BE_WRITE_U16(teamList, _selTeamNumber); teamList++;
    uint16_t countSoFar = 1;

    for (int16_t i = teamCount - 2; i >= 0; i--) {
        uint16_t teamId = pickRandomNationalTeamId(minPrice, maxPrice, 0, listBase, countSoFar);
        BE_WRITE_U16(teamList, teamId); teamList++;
        countSoFar++;
    }
}

/* ---- setupTourCupTeams (SWOS2.s line 32918) ----
 * Sets up the tour cup with a random region's teams. */
static void setupTourCupTeams(void)
{
    uint16_t randRegion = (uint16_t)((uint32_t)Rand() * 6 >> 8);
    uint8_t regionCode = (uint8_t)(randRegion + 0x50);

    CompetitionCup *comp = (CompetitionCup *)_tourCopy;
    /* Point the tour's name at this region's string (name[0] = name-table start). */
    comp->name[0] = _tourStringTable[randRegion];

    int16_t teamCount = comp->header.roundStringBase;
    uint16_t *teamList = (uint16_t *)comp->teamSeedList;
    uint16_t *listBase = teamList;

    /* BE writes — see setupFriendlyCupTeams comment. */
    BE_WRITE_U16(teamList, _selTeamNumber); teamList++;
    uint16_t countSoFar = 1;

    for (int16_t i = teamCount - 2; i >= 0; i--) {
        uint16_t teamId = pickRandomNationalTeamId(0, 0x31, regionCode, listBase, countSoFar);
        BE_WRITE_U16(teamList, teamId); teamList++;
        countSoFar++;
    }
}

/* ---- Career_SetCupPickList_ForSelectedRegion (SWOS2.s line 32820) ----
 * Dispatches to the regional cup pick list function based on team region,
 * then sets up friendly, invitation tournament, and tour cup teams. */
__attribute__((used, externally_visible))
void Career_SetCupPickList_ForSelectedRegion(uint16_t *teamList)
{
    uint8_t region = (uint8_t)(_selTeamNumber >> 8);
    uint16_t **tlp = &teamList;

    switch (region) {
    case 0x50: /* Europe */
        setCupPickList_Europe(tlp);
        break;
    case 0x52: /* South America */
        setCupPickListCommon(_cupPickSAmericaWorldCupFriendly, _cupPickSAmericaWorldCupInvite,
            _cupPickSAmericaCopaAmericaFinalist, _cupPickSAmericaCopaAmericaNonFinalist,
            _cupPickSAmericaCopaAmericaFriendly, _cupPickSAmericaInviteFriendly, tlp);
        break;
    case 0x51: /* Africa */
        setCupPickListCommon(_cupPickAfricaWorldCupFriendly, _cupPickAfricaWorldCupInvite,
            _cupPickAfricaAfricanCupFinalist, _cupPickAfricaAfricanCupNonFinalist,
            _cupPickAfricaAfricanCupFriendly, _cupPickAfricaInviteFriendly, tlp);
        break;
    case 0x54: /* Asia */
        setCupPickListCommon(_cupPickAsiaWorldCupFriendly, _cupPickAsiaWorldCupInvite,
            _cupPickAsiaAsianCupFinalist, _cupPickAsiaAsianCupNonFinalist,
            _cupPickAsiaAsianCupFriendly, _cupPickAsiaInviteFriendly, tlp);
        break;
    case 0x53: /* North America */
        setCupPickListCommon(_cupPickNAmericaWorldCupFriendly, _cupPickNAmericaWorldCupInvite,
            _cupPickNAmericaNaCupFinalist, _cupPickNAmericaNaCupNonFinalist,
            _cupPickNAmericaNaCupFriendly, _cupPickNAmericaInviteFriendly, tlp);
        break;
    case 0x55: /* Oceania */
        setCupPickListCommon(_cupPickOceaniaWorldCupFriendly, _cupPickOceaniaWorldCupInvite,
            _cupPickOceaniaOceaniaCupFinalist, _cupPickOceaniaOceaniaCupNonFinalist,
            _cupPickOceaniaOceaniaCupFriendly, _cupPickOceaniaInviteFriendly, tlp);
        break;
    default:
        _panicReasonCode = 0x4434;
        PanicHaltLoop();
        return;
    }

    setupFriendlyCupTeams();
    setupInvitationTournamentTeams();
    setupTourCupTeams();
}

/* ====================================================================
 * Chunk 186: Career Season Update Functions
 * ===================================================================*/

/* ---- ASM externs for Chunk 186 ---- */
extern uint16_t _playPromotedAnimation;
extern uint16_t _playRelegatedAnimation;
extern volatile uint16_t _seasonUpdateInProgress;
extern uint16_t _careerFileBuffer;
extern uint16_t _euroCupQualifiedTeamCount;
extern uint16_t _cupWinnersCupQualifiedTeamCount;
extern uint16_t _uefaCupQualifiedTeamCount;
extern uint8_t _careerCompetitionIdRemapTable[];
extern uint16_t _diyQualifiedTeamIndices[];
extern uint16_t _currentDivisionIndex;
extern uint16_t _youreSacked;
extern uint8_t *_diyCompetitionPtr;
extern CompetitionSlotBuffer _diyCompetitionContextBuf;   /* slot-4 untangle (S2): typed buffer */

/* England's leagues — standalone globals, consolidated from the old
 * EnglishLeaguesBlock (competition_data.c). competition_data.h is kept out of
 * this TU (opaque forward decls for several competition tables); CompetitionLeague
 * / Leagues come from game_types.h, so extern the four globals here. */
extern CompetitionLeague _englishLeague;
extern Leagues _englishLeagues1, _englishLeagues2, _englishLeagues3;
extern Playoffs _div1Playoffs;
extern Playoffs _div2Playoffs;
extern Playoffs _div3Playoffs;

/* ---- Career_ProcessDiyLeagueQualificationEntry ----
 * (SWOS2.s line 31903)
 * Processes a single DIY league qualification entry during season transition.
 * index d7 selects which division qualification entry to process.
 * competition a4 is the competition record. */
/* Refactored from asm-style byte walking to typed struct access (May 2026).
 *
 * SWOS2.s Career_ProcessDiyLeagueQualificationEntry (line 58055) walks the
 * country's leagues block via byte offsets stored in
 * `divisions[i].qualifierLinkOffset` (was unk1) to
 * find a `Playoffs *` qualification competition and a qIdx pair list. The
 * walk assumed the 32-bit Amiga struct layout. On 64-bit several involved
 * structs grow because of pointer-field widening:
 *   - CompetitionLeague_4_8_2_24: text[8]+unk2[2] grow → 102 → 142 bytes
 *   - Leagues_4_2_24: Leagues_4+playoffs+leagues2 grow → 80 → 104 bytes
 *   - Playoffs: text[2] grows → 32 → 40 bytes
 *
 * The static `divisions[i].qualifierLinkOffset` byte hints were authored for
 * 32-bit, so the walk `competition + i*6 + 0x12 + qualifierLinkOffset` lands
 * inside text[8] (a string pointer) instead of unk2[0] on 64-bit, returning
 * garbage as qualComp.
 *
 * In practice only the English block (EnglishLeaguesBlock) ever exercises
 * this walk — its divisions[1..3].qualifierLinkOffset are non-zero (46, 120,
 * 194) while every other country has all-zero division.qualifierLinkOffset and
 * exits at the early
 * `if (d0 == 0) return;` test. The typed dispatch below is English-specific.
 *
 * For each English division, by static data analysis:
 *   index=1: walk lands at englishLeague.unk2[0] = &div1Playoffs;
 *            qIdx pairs live in englishLeague.unk3[].
 *   index=2: walk lands at englishLeagues1.playoffs = &div2Playoffs;
 *            qIdx pairs in englishLeagues1.qualifierIndexPairs[].
 *   index=3: walk lands at englishLeagues2.playoffs = &div3Playoffs;
 *            qIdx pairs in englishLeagues2.qualifierIndexPairs[].
 *
 * teamData = qualComp + 7 + qualComp[7] in the asm; for all three Playoffs
 * statics qualComp->comp.teamListOffset = 17 (Amiga byte offset to qualifiedTeamIds[0]).
 * On 64-bit Playoffs.qualifiedTeamIds sits 8 bytes later (text[2] grew). Using
 * typed `&qualComp->qualifiedTeamIds[0]` resolves correctly on both targets. */
static void processDiyLeagueQualificationEntry(uint16_t index, uint8_t *competition)
{
    uint8_t *a2 = competition + index * 6;
    uint8_t d0 = a2[0x12];

    if (d0 == 0)
        return;

    /* Last-division-same-offset gate (matches asm `cmp 9(a4),d1` and the
     * `addq.b #6,d1; cmp.b d1,d0` test). Byte 9 of CompetitionLeague is
     * divisionsCount. Both bytes here are within the original Amiga
     * struct's head (offsets < 0x28), so no namePtr shift applies. */
    uint16_t nextDiv = index + 1;
    if ((uint8_t)nextDiv != (uint8_t)((CompLeagueHeader *)competition)->divisionsCount) {
        uint8_t d1 = a2[0x18] + 6;
        if (d0 == d1)
            return;
    }

    /* Typed dispatch (see header comment for derivation). The walk via
     * `divisions[i].qualifierLinkOffset` is replaced with direct struct-member
     * access. */
    if (comp_id(competition) != COMPID_englishLeague) {  /* non-NULL: deref'd above */
        /* Non-English country reached the walk despite divisions[i].qualifierLinkOffset
         * being expected zero — unexpected. Bail safely rather than
         * re-introduce the broken byte walk. */
        return;
    }

    Playoffs *qualComp;
    uint8_t   *teamIdxList;
    switch (index) {
    case 1:
        qualComp    = (Playoffs *)_englishLeague.playoffLinkPtrs[0]; /* &div1Playoffs */
        teamIdxList = (uint8_t *)_englishLeague.promotionLinkData;
        break;
    case 2:
        qualComp    = _englishLeagues1.playoffs;              /* &div2Playoffs */
        teamIdxList = (uint8_t *)_englishLeagues1.qualifierIndexPairs;
        break;
    case 3:
        qualComp    = _englishLeagues2.playoffs;              /* &div3Playoffs */
        teamIdxList = (uint8_t *)_englishLeagues2.qualifierIndexPairs;
        break;
    default:
        return; /* Premier (index=0) returned via early-exit. */
    }

    int8_t count = (int8_t)qualComp->teams;   /* qualified-team count (was playoffHeader[10]) */

    if (!_youreSacked && (uint8_t *)qualComp == _diyCompetitionPtr) {
        HLOG("promotion-playoff INTERACTIVE (player in playoff): index=%u teams=%d\n",
             (unsigned)index, (int)count);
        /* Player's competition — interactive processing. Advance the
         * teamIdxList past the consumed qIdx pairs (asm `lea (a2,d0.w),a2`
         * with d0 = count*2) so DIY_ApplyPromotionRelegationWeights sees
         * the post-list region. */
        teamIdxList += (int16_t)count * 2;

        RestoreSelectedTeams();
        Career_LoadCompetitionSlotStateFromBuffer((uint8_t *)&_diyCompetitionContextBuf);
        ShowDIYCupMenuWithParams(1, 1, 0);
        DIY_ApplyPromotionRelegationWeightsToScratch(teamIdxList);
        SaveSelectedTeams();
    } else {
        HLOG("promotion-playoff auto: index=%u teams=%d\n", (unsigned)index, (int)count);
        /* Not player's competition — auto-fill teams from qualification indices. */
        uint16_t savedSelTeam = _selTeamNumber;
        uint8_t  savedCtrl    = _careerTeam.team.teamControls;

        _selTeamNumber = 0xFFFF;
        _careerTeam.team.teamControls = 1;

        /* Sanity check matching the asm `beq.w loc_12BCD0` — Playoffs
         * statics all have comp.teamListOffset=17, so 0 should never appear. */
        if (comp_team_seed_list(&qualComp->comp) == NULL) {
            _panicReasonCode = 0x3003;
            PanicHaltLoop();
            return;
        }

        /* teamData lands at qualComp->qualifiedTeamIds[0] on both targets via
         * typed access (replaces the asm's `lea 7(a1,d0.w),a0` byte walk where
         * d0 = qualComp->comp.teamListOffset = 17). On 32-bit byte 7+17=24 =
         * qualifiedTeamIds[0]; on 64-bit the same logical position is byte 32
         * because text[2] widened. Typed lookup picks the right byte on each. */
        uint8_t *teamData = (uint8_t *)&qualComp->qualifiedTeamIds[0];

        int16_t cnt = (int16_t)count - 1;

        /* qIdx pair stream is BE on m68k (`move.w (a2)+,d0`) — keep
         * BE_READ_U16 to recover the value on LE hosts. diyQualifiedTeamIndices
         * is BE-packed (295c9f3) and the destination unk2 is also read by
         * downstream code with BE_READ_U16 — write BE to match. */
        for (int16_t i = cnt; i >= 0; i--) {
            uint16_t qIdx = BE_READ_U16(teamIdxList);
            teamIdxList += 2;
            uint16_t teamId = BE_READ_U16(&_diyQualifiedTeamIndices[qIdx]);
            BE_WRITE_U16(teamData, teamId);
            teamData += 2;
        }

        /* Auto-advance this competition via cup competition path. */
        Career_AutoAdvanceCupCompetitionFromRecord((uint8_t *)qualComp);
        DIY_ApplyPromotionRelegationWeightsToScratch(teamIdxList);

        _careerTeam.team.teamControls = savedCtrl;
        _selTeamNumber = savedSelTeam;
    }
}

/* ---- Career_ProcessPrimaryCompetitionForNextSeason ----
 * (SWOS2.s line 31825)
 * Processes the primary competition for the next season.
 * Handles league (type 0) and cup (type 2) competitions differently. */
static void processPrimaryCompetitionForNextSeason(uint8_t *competition)
{
    int8_t compType = ((Competition *)competition)->type;

    if (compType == 2) {
        /* Cup competition */
        if (competition == _currentCompetitionPtr) {
            RestoreSelectedTeams();
            Career_CopyCompetitionFileBufferToOrFromActive();
            DIY_EditCompetitionSlot(1, 1, 0);
            SaveSelectedTeams();
        } else {
            /* Auto-advance via league competition record path
             * (asm calls Career_AutoAdvanceLeagueCompetitionFromRecord even for cups) */
            Career_AutoAdvanceLeagueCompetitionFromRecord(competition);
        }
        return;
    }

    if (compType != 0) {
        /* Unknown type → panic */
        _panicReasonCode = 0x3003;
        PanicHaltLoop();
        return;
    }

    /* League competition — loop through all divisions.
     *
     * `qualCursor` mirrors the asm's a3 register: set ONCE before the
     * loop, advanced inside DIY_CopyCompetitionTeamIndexTable on each
     * iteration. This way each division's promotion list APPENDS to
     * diyQualifiedTeamIndices rather than overwriting from base, so
     * the per-division qualification entries below see the cumulative
     * list (matches Career_ProcessPrimaryCompetitionForNextSeason at
     * SWOS2.s line 57992 / loc_12BDC8). Resetting to base each call
     * caused only the last division's data to be visible — and the
     * qualification-loop's Career_AutoAdvanceCupCompetitionFromRecord
     * picked teams from the wrong division, putting promotion weights
     * into wrong scratch slots and tripping the 0x5555 validate panic. */
    uint8_t numDivisions = (uint8_t)((CompLeagueHeader *)competition)->divisionsCount;
    uint16_t d7;
    uint8_t *qualCursor = (uint8_t *)_diyQualifiedTeamIndices;

    LeaguePlayoffs_Begin();     /* a world's play-offs: the final tables (league_playoffs.c) */

    for (d7 = 0; d7 < numDivisions; d7++) {
        if (competition == _currentCompetitionPtr &&
            d7 == _currentDivisionIndex) {
            /* Player's current division — interactive */
            RestoreSelectedTeams();
            Career_CopyCompetitionFileBufferToOrFromActive();
            ShowDiyLeaguesTournamentMenu(1, 1, 0);
            if (d7 == 0)
                DIY_CachePrimaryQualifiedTeamsGrid();
            SaveSelectedTeams();
        } else {
            /* Other division — auto-advance */
            Career_AutoAdvanceInternationalCompetitionFixtures(competition, d7);
            if (competition == _currentCompetitionPtr)   /* (the career's league, not one the */
                WorldPlayoff_RestoreEarlyTable(d7);      /* European places simulate: world_playoff.c) */
            if (d7 == 0)
                DIY_CachePrimaryQualifiedTeamsGrid();
        }
        DIY_UpdatePromotionRelegationScratchCounts();
        LeaguePlayoffs_KeepTable(competition, d7);
        qualCursor = DIY_CopyCompetitionTeamIndexTable(qualCursor);
    }

    /* Process qualification entries for all divisions */
    for (d7 = 0; d7 < numDivisions; d7++) {
        processDiyLeagueQualificationEntry(d7, competition);
    }
}

/* Copa Libertadores career qualifier (Inc3 — src/career_copa.c). These run as
 * siblings of the European-cup qualification calls below: Prepare resets the
 * CONMEBOL eligible list + points the write cursor at the Copa *Copy seed list;
 * Process appends the champion + runner-up of each CONMEBOL league (read from
 * _primaryQualifiedTeamGrids, just cached by Career_ProcessCompetitionForNextSeason)
 * into next season's Copa field. See docs/COMPETITION_DYNAMIC_REFACTOR.md §8d.
 * (3b-part-1: field-building only — the TMD-pool staging, the play-path
 * Career_UpdateCopaLibertadoresCompetitionSlot, and the count-gate
 * generalisation are deferred to the play path.) */
extern void Career_PrepareCopaLibertadoresQualificationState(void);
extern void Career_ProcessCompetitionForCopaLibertadoresQualification(void);
extern uint16_t Career_CopaLibertadoresQualifiedTeamCount(void);
extern uint16_t Career_CopaLibertadoresExpectedTeamCount(void);
extern void Career_UpdateCopaLibertadoresCompetitionSlot(void);

/* ---- Career_AdvanceToNextSeason_UpdateCompetitions ----
 * (SWOS2.s line 31686)
 * Main season transition function. Updates all competition slots,
 * processes next season, and handles European cup draws. */
__attribute__((used, externally_visible))
void Career_AdvanceToNextSeason_UpdateCompetitions(void)
{
    DIY_LoadLeagueScratchFromTable();

    _playPromotedAnimation = 0;
    _playRelegatedAnimation = 0;

    Career_PrepareEuropeanCupQualificationState();
    Career_PrepareCopaLibertadoresQualificationState();   /* Inc3: build next season's CONMEBOL field */
    SaveSelectedTeams();

    _seasonUpdateInProgress = 1;

    if (_isNationalSelector) {
        if ((uint8_t)(_selTeamNumber >> 8) == 0x50)
            Career_UpdateEuropeanChampionshipCompetitions();
        Career_UpdateWorldCupCompetitions();
    }

    Career_UpdateEuroCupCompetitionSlot();
    Career_UpdateCupWinnersCupCompetitionSlot();
    Career_UpdateUefaCupCompetitionSlot();
    Career_UpdateCopaLibertadoresCompetitionSlot();   /* Inc3b-part-2: auto-advance Copa */

    _careerFileBuffer = 0;

    if (!_isNationalSelector) {
        uint16_t compIndex = (uint8_t)(_selTeamNumber >> 8);
        Career_ProcessCompetitionForNextSeason(compIndex);
        Career_ProcessCompetitionForEuropeanCupQualification();
        Career_ProcessCompetitionForCopaLibertadoresQualification();   /* Inc3 */
    }

    /* Loop through all competition IDs in the remap table */
    const uint8_t *remapPtr = World_SeasonCountries() ? World_SeasonCountries()    /* (a world's: */
                                                      : _careerCompetitionIdRemapTable; /* its new ones too) */
    for (;;) {
        uint8_t compByte = *remapPtr++;
        if (compByte == 0xFF)
            break;
        if (!_isNationalSelector && compByte == (uint8_t)(_selTeamNumber >> 8))
            continue;
        Career_ProcessCompetitionForNextSeason((uint16_t)compByte);
        Career_ProcessCompetitionForEuropeanCupQualification();
        Career_ProcessCompetitionForCopaLibertadoresQualification();   /* Inc3 */
    }

    /* The 0x50 (80) here is the TMD-pool append COUNT, not a region code: every
     * season transition appends 16 euroCup + 32 cupWinnersCup + 32 uefaCup teams.
     * The Copa Libertadores qualifier (Inc3) appends its CONMEBOL field on top,
     * so the expected total is 0x50 + the Copa count (0 on a build without the
     * Copa data). Without this the gate would miss (e.g. 100 != 0x50) and fall
     * through to panic 0x8999. */
    /* (a world's league phases: 34 + 35 + 36 before the holders, 36 each after them) */
    const uint8_t (*lpPlaces)[4];
    int32_t lpN;
    const int32_t lp = World_LeaguePhaseCups(&lpPlaces, &lpN) != 0;
    const uint16_t euroBefore = lp ? 105 : 0x50, euroAfter = lp ? 108 : 0x50;
    if (_careerFileBuffer == euroBefore + Career_CopaLibertadoresQualifiedTeamCount()) {
        Career_DrawEuropeanCupTeamsAndCopyGrids();
        _seasonUpdateInProgress = 0;
        RestoreSelectedTeams();

        if (_euroCupQualifiedTeamCount == (lp ? 36 : 16) &&
            _cupWinnersCupQualifiedTeamCount == (lp ? 36 : 32) &&
            _uefaCupQualifiedTeamCount == (lp ? 36 : 32) &&
            Career_CopaLibertadoresQualifiedTeamCount() == Career_CopaLibertadoresExpectedTeamCount() &&
            _careerFileBuffer == euroAfter + Career_CopaLibertadoresQualifiedTeamCount()) {
            Career_StorePromotionRelegationInSeasonInfo();
            WorldCups_SeasonEnded();    /* (the next season plays its cups' own draw) */
            return;
        }
        _panicReasonCode = 0x8998;
        PanicHaltLoop();
        return;
    }

    _panicReasonCode = 0x8999;
    PanicHaltLoop();
}
