/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* job_offers.c — Job offer queue management and menu functions.
 *
 * Chunk 79: 6 exported functions ported from SWOS2.s lines 84210-84322.
 * Chunk 81: 5 more exported functions from SWOS2.s lines 84081-84200.
 * Chunk 99: 8 job offer menu functions from SWOS2.s lines 82566-82903.
 */

#include "job_offers.h"
#include "translations.h"  /* tr_fmt — localize the offer-letter text */
#include "endian_helpers.h"
#include "game_types.h"
#include "teams.h"           /* Team_IsOutsideLeague */
#include "menu_system.h"
#include "random.h"
#include "string_utils.h"
#include "world.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

/* ---- ASM externs ---- */
extern uint16_t _numJobOffers;
extern JobOffer _jobOffers[];         /* 5-entry job offers queue (in SWOS2.s BSS) */
extern uint16_t _numNewJobOffers;
extern JobOffer _newJobOffers[];      /* 5-entry new job offers queue (in SWOS2.s BSS) */
extern uint8_t _fundsBuffer[];        /* temporary buffer for team name during offer conversion */
extern MenuEntry _menuEntries[];
extern uint16_t _teamNoOfferAccepted;
extern uint32_t _teamFundsAccepted;
extern uint16_t _youreSacked;
extern uint16_t _highlightedLeagueTeamId;

/* Additional externs for Career_MaybeGenerateJobOffer */
extern uint16_t _careerSeasonsCompleted;
extern uint16_t _currentSeasonShowing;
extern uint16_t _careerJobOfferMode;
extern uint8_t  _viewRandomPhase;
extern int8_t   _careerJobOfferPhaseBias;
extern uint16_t _wageScalingDivisor;
extern uint16_t _sameNationalCallFlag;
extern uint16_t _lastNationalityCall;
extern int16_t  _boardConfidenceScore;
extern int16_t  _sackedReputationScore;
extern int16_t  _nationalSelectorReputationPoints;
/* majorTrophyBonusValue migrated to primaryCompetitionBuf.tourn.header.trophyBonusValue (slot-0 untangle, S6) */
extern uint16_t _isNationalSelector;
extern uint16_t _selTeamNumber;
extern uint16_t _numSelectedTeams;
extern uint16_t _currentDivisionIndex;
extern CompetitionSlotBuffer _primaryCompetitionBuf;           /* slot-0 untangle (S6): typed buffer */
/* numDiyTeams migrated to primaryCompetitionBuf.tourn.header.numTeams (slot-0 untangle, S6) */
/* diyTeamMinDivisionIndex migrated to primaryCompetitionBuf.tourn.header.minDivisionIndex (slot-0 untangle, S6) */
/* diyTeamMaxDivisionOffset migrated to primaryCompetitionBuf.tourn.header.maxDivisionOffset (slot-0 untangle, S6) */
extern uint16_t _nat_teams_rand_limit1;
extern uint8_t  _nationalTeams[];
extern uint8_t  _nationalTeamFileCode;
extern uint8_t  _teamFileBuffer;
extern uint16_t _careerFileBuffer;
extern uint8_t  _tmdFileBuffer;

extern uint16_t Rand(void);
extern uint16_t GetAveragePlayerPrice(TeamFileEx *team);
extern int16_t  IsNewJobOffer(uint16_t teamNumber);
extern int16_t  IsOldJobOffer(uint16_t teamNumber);
extern void     AddJobOffer(uint16_t teamNumber, uint32_t initialFunds);
extern int16_t  GetBoardConfidenceScore(void);
extern int16_t  IsTeamCodeOkForNationalSelector(uint16_t code);
extern int16_t  IsPlayerOkForNationalSelector(void);
extern uint16_t FindTeamInDIY(uint16_t teamId, DIYCompetition *compBuffer);
extern uint32_t CalculateStartingBalance(uint16_t teamId);
extern int16_t  LoadTeamFile(uint16_t code);

/* Job offer menu data (in SWOS2.s) */
extern uint16_t _jobOffersFromRetired;
extern int16_t  _jobOfferOrdinal;
extern int16_t  _oldJobOfferOrdinal;
extern uint16_t _selectedOldJobOfferOrdinal;
extern uint16_t _teamNoOfferingJob;
extern uintptr_t _teamOfferingJobPtr;
extern int32_t  _jobOfferFunds;
extern uint16_t _viewFutureTeamModeFlag;

/* String constants */
extern uint8_t _aRetire[];
extern uint8_t _aInternationalJ[];
extern uint8_t _aNewJob[];
extern uint8_t _aDearAB[];
extern uint8_t _aWeHereAtAWould[];
extern uint8_t _aCoachingJobAtO[];
extern uint8_t _aWeCanOfferYouA[];
extern uint8_t _aWeHopeToBeAble[];
extern uint8_t _aInTheRegionOfA[];
extern uint8_t _aWeLookForwardT_1[];
extern uint8_t _aTheMembersOfTh[];
extern uint8_t _aOfAWouldLikeTo[];
extern uint8_t _aAsCoachOfTheNa[];
extern uint8_t _aWeCanPromiseYo[];
extern uint8_t _aAnAnnuallyRevi[];
extern uint8_t _aWeLookForwardT[];
extern uint8_t _aWeHereAtAAreDe[];
extern uint8_t _aHaveAcceptedTh[];
extern uint8_t _aFromTheStartOf_0[];
extern uint8_t _aWeHopeToBeAble_0[];
extern uint8_t _aInTheRegionOfA_0[];
extern uint8_t _aAndWeLookForwa[];
extern uint8_t _aTheMembersOfTh_0[];
extern uint8_t _aOfAAreDelighte[];
extern uint8_t _aAcceptedThePos[];
extern uint8_t _aFromTheStartOf[];
extern uint8_t _aWeLookForwardT_0[];
extern uint8_t _managerSexBuffer[];
extern uint8_t _managerSurnameBuffer[];
extern uint8_t _aViewTeam[];

extern void *_competitionsTable[];
extern uint8_t _seasonInformationsArray[];
extern uint16_t _currentSeasonShowing;

/* Season info row stride uses sizeof so 64-bit pointer-field growth in
 * SeasonInformations (106 → 122 bytes) is picked up automatically.
 * Field reads go through typed struct access to avoid the same drift. */

/* ---- C-ported function externs ---- */
extern void SetExitMenuFlag(void);
extern void SetCurrentEntry(uint16_t index);
extern TeamFileEx *GetTeamPointer(uint16_t teamId);
extern void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC);

/* ---- C-callable ASM bridges ---- */
extern void Int2Pounds(uint32_t value, uint8_t *dst);
extern int16_t ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo, uint32_t maskHi, uint16_t arg2, uint16_t arg3);
extern void Career_ViewOrEditCompetitionSlot(uint8_t *comp, uint16_t sel);

/* Helper: menu entry from byte offset */

/* Helper: access bg as the high 16 bits (big-endian BACKANDFRAMECOLOR) */

/* Helper: GetCurrentSeasonPointer */
static inline uint8_t *getCurrentSeasonPointer(void) {
    return _seasonInformationsArray +
           (uint16_t)_currentSeasonShowing * (uint32_t)sizeof(SeasonInformations);
}


/* ================================================================
 * IsOldJobOffer  (SWOS2.s line 84210)
 *
 * Checks if a team number already exists in the jobOffers queue.
 * Returns: 0 if found (Z set in asm), 1 if not found (Z clear).
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsOldJobOffer(uint16_t teamNumber)
{
    uint16_t count = _numJobOffers;
    if (count == 0)
        return 1;

    for (uint16_t i = 0; i < count; i++) {
        if (_jobOffers[i].teamNo == teamNumber)
            return 0;  /* found */
    }
    return 1;  /* not found */
}

/* ================================================================
 * GetJobOfferPtr  (SWOS2.s line 84231)
 *
 * Returns pointer to a job offer by index.
 * ================================================================ */
__attribute__((used, externally_visible))
JobOffer *GetJobOfferPtr(uint16_t index)
{
    return &_jobOffers[index];
}

/* ================================================================
 * AddJobOffer  (SWOS2.s line 84244)
 *
 * Adds a new job offer to the front of the newJobOffers queue.
 * Shifts existing entries right by one position. Max 5 offers.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void AddJobOffer(uint16_t teamNumber, uint32_t initialFunds)
{
    if (_numNewJobOffers >= 5)
        return;

    uint16_t count = _numNewJobOffers;

    /* Shift existing entries right by 26 bytes (word-at-a-time, backwards) */
    if (count > 0) {
        volatile uint16_t *base = (volatile uint16_t *)_newJobOffers;
        uint16_t totalWords = count * (sizeof(JobOffer) / 2);
        for (int16_t i = (int16_t)(totalWords - 1); i >= 0; i--) {
            base[i + (sizeof(JobOffer) / 2)] = base[i];
        }
    }

    /* Write new entry at position 0 */
    _newJobOffers[0].teamNo = teamNumber;
    _newJobOffers[0].amountOffered = initialFunds;
    *(uint8_t *)_newJobOffers[0].teamName = 0xFF;
    _numNewJobOffers++;
}

/* ================================================================
 * RemoveJobOffer  (SWOS2.s line 84271)
 *
 * Removes a job offer from the newJobOffers queue by team number.
 * Shifts following entries left to fill the gap.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void RemoveJobOffer(uint16_t teamNumber)
{
    uint16_t count = _numNewJobOffers;
    if (count == 0)
        return;

    for (uint16_t i = 0; i < count; i++) {
        if (_newJobOffers[i].teamNo == teamNumber) {
            _numNewJobOffers--;
            uint16_t remaining = count - 1 - i;
            if (remaining > 0) {
                volatile uint16_t *src = (volatile uint16_t *)&_newJobOffers[i + 1];
                volatile uint16_t *dst = (volatile uint16_t *)&_newJobOffers[i];
                uint16_t words = remaining * (sizeof(JobOffer) / 2);
                for (uint16_t w = 0; w < words; w++) {
                    *dst++ = *src++;
                }
            }
            return;
        }
    }
}

/* ================================================================
 * IsNewJobOffer  (SWOS2.s line 84301)
 *
 * Checks if a team number already exists in the newJobOffers queue.
 * Returns: 0 if found (Z set in asm), 1 if not found (Z clear).
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsNewJobOffer(uint16_t teamNumber)
{
    uint16_t count = _numNewJobOffers;
    if (count == 0)
        return 1;

    for (uint16_t i = 0; i < count; i++) {
        if (_newJobOffers[i].teamNo == teamNumber)
            return 0;  /* found */
    }
    return 1;  /* not found */
}

/* ================================================================
 * GetNewJobOfferPtr  (SWOS2.s line 84318)
 *
 * Returns pointer to a new job offer by index.
 * ================================================================ */
__attribute__((used, externally_visible))
JobOffer *GetNewJobOfferPtr(uint16_t index)
{
    return &_newJobOffers[index];
}

/* ================================================================
 * JobOffers_RemoveOffer  (SWOS2.s line 84180)
 *
 * Removes a job offer from the jobOffers (old offers) queue by
 * team number. Finds the entry, shifts following entries left by
 * 26 bytes (word-at-a-time), decrements count.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void JobOffers_RemoveOffer(uint16_t teamNumber)
{
    uint16_t count = _numJobOffers;
    if (count == 0)
        return;

    for (uint16_t i = 0; i < count; i++) {
        if (_jobOffers[i].teamNo == teamNumber) {
            _numJobOffers--;
            /* Shift remaining entries left by 26 bytes (word-at-a-time) */
            uint16_t wordsToShift = (count - 1 - i) * (sizeof(JobOffer) / 2);
            volatile uint16_t *dst = (volatile uint16_t *)&_jobOffers[i];
            for (uint16_t j = 0; j < wordsToShift; j++) {
                dst[j] = dst[j + (sizeof(JobOffer) / 2)];
            }
            return;
        }
    }
}

/* ================================================================
 * JobOffers_MaybeExpireSomeNewOffers  (SWOS2.s line 84081)
 *
 * For each new job offer, there is a 1/32 chance (Rand2 < 0x20)
 * of expiring it by clearing its amountOffered field.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffers_MaybeExpireSomeNewOffers(void)
{
    uint16_t count = _numNewJobOffers;
    if (count == 0)
        return;

    for (int16_t i = (int16_t)(count - 1); i >= 0; i--) {
        if (Rand2() < 0x20) {
            _newJobOffers[i].amountOffered = 0;
        }
    }
}

/* ================================================================
 * JobOffers_FillMissingNewOfferTeamNames  (SWOS2.s line 84099)
 *
 * For each new job offer whose team name starts with 0xFF (unset),
 * looks up the team via GetTeamPointer and copies the team name.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffers_FillMissingNewOfferTeamNames(void)
{
    uint16_t count = _numNewJobOffers;
    if (count == 0)
        return;

    for (int16_t i = (int16_t)(count - 1); i >= 0; i--) {
        JobOffer *offer = &_newJobOffers[i];
        if (*(uint8_t *)offer->teamName != 0xFF)
            continue;

        /* Look up team pointer and copy name */
        TeamFileEx *teamPtr = GetTeamPointer(offer->teamNo);
        StringCopy((uint8_t *)teamPtr->team.teamName, (uint8_t *)offer->teamName);
    }
}

/* ================================================================
 * JobOffers_RemoveExpiredOffers  (SWOS2.s line 84121)
 *
 * Removes all expired offers (amountOffered == 0) from both the
 * jobOffers and newJobOffers queues.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffers_RemoveExpiredOffers(void)
{
    /* Remove expired from old offers (jobOffers) */
    for (int16_t i = (int16_t)(_numJobOffers - 1); i >= 0; i--) {
        if (_jobOffers[i].amountOffered == 0) {
            JobOffers_RemoveOffer(_jobOffers[i].teamNo);
        }
    }

    /* Remove expired from new offers (newJobOffers) */
    for (int16_t i = (int16_t)(_numNewJobOffers - 1); i >= 0; i--) {
        if (_newJobOffers[i].amountOffered == 0) {
            RemoveJobOffer(_newJobOffers[i].teamNo);
        }
    }
}

/* ================================================================
 * JobOffers_ConvertNewOfferToOffer  (SWOS2.s line 84153)
 *
 * Converts a new job offer to an old (accepted) job offer.
 * Copies team name to fundsBuffer, removes from newJobOffers,
 * then appends to jobOffers with the team name from fundsBuffer.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffers_ConvertNewOfferToOffer(uint16_t teamNumber, uint32_t initialFunds)
{
    /* Find the new offer entry and copy its team name to fundsBuffer */
    uint16_t count = _numNewJobOffers;
    for (uint16_t i = 0; i < count; i++) {
        if (_newJobOffers[i].teamNo == teamNumber) {
            StringCopy((uint8_t *)_newJobOffers[i].teamName, _fundsBuffer);
            break;
        }
    }

    /* Remove from new offers */
    RemoveJobOffer(teamNumber);

    /* Add to old offers (max 5) */
    if (_numJobOffers >= 5)
        return;

    JobOffer *dest = &_jobOffers[_numJobOffers];
    dest->teamNo = teamNumber;
    dest->amountOffered = initialFunds;
    StringCopy(_fundsBuffer, (uint8_t *)dest->teamName);
    _numJobOffers++;
}

/* ================================================================
 * Chunk 99: Job Offer Menu Functions
 * ================================================================ */

/* Helper: format string into menu entry's text buffer */
static inline void printToEntry(uint16_t entryIdx, const uint8_t *fmt, const uint8_t *a, const uint8_t *b) {
    MenuEntry *e = &_menuEntries[entryIdx];
    uint8_t *out = (uint8_t *)e->fg;
    PrintFormatted(fmt, out, 0,0,0,0,0,0,0, a, b, 0);
}

/* ================================================================
 * GotJobOfferInit  (SWOS2.s line 82568)
 * ================================================================ */
__attribute__((used, externally_visible))
void GotJobOfferInit(void) {
    _teamNoOfferingJob = _teamNoOfferAccepted;
    _jobOfferFunds = (int32_t)_teamFundsAccepted;

    if (_teamNoOfferAccepted == 0xFFFF) {
        JobOffer *offer;
        if (_jobOfferOrdinal >= 0) {
            offer = GetNewJobOfferPtr((uint16_t)_jobOfferOrdinal);
        } else {
            offer = GetJobOfferPtr((uint16_t)_oldJobOfferOrdinal);
        }
        _teamNoOfferingJob = offer->teamNo;
        _jobOfferFunds = (int32_t)offer->amountOffered;
        if (_jobOfferOrdinal < 0) {
            SetCurrentEntry(11);
        }
    } else {
        SetCurrentEntry(11);
    }
    /* bra.w *+4 in asm = fallthrough to GotJobOfferFinish */
    GotJobOfferFinish();
}

/* ================================================================
 * GotJobOfferFinish  (SWOS2.s line 82593)
 *
 * Sets up the job offer menu display text based on whether this is
 * a new offer (teamNoOfferAccepted == -1) or an accepted offer.
 * ================================================================ */
__attribute__((used, externally_visible))
void GotJobOfferFinish(void) {
    /* If sacked and only 1 total offer, change exit to "RETIRE" with red color */
    if (_youreSacked) {
        if ((uint16_t)(_numNewJobOffers + _numJobOffers) == 1) {
            MenuEntry *exitEntry = &_menuEntries[8];
            exitEntry->fg = (uintptr_t)_aRetire;
            exitEntry->bgColor = (exitEntry->bgColor & 0xFFF0) | 10; /* RED_10 */
        }
    }

    /* International job offer adjustments */
    if (_jobOfferFunds == -1) {
        MenuEntry *e0 = &_menuEntries[0];
        e0->fg = (uintptr_t)_aInternationalJ;
        e0->x -= 30;
        e0->dwWidth += 60;
        _menuEntries[8].x += 65;
        _menuEntries[13].x -= 65;
        _menuEntries[9].isInvisible = 1;
        _menuEntries[12].isInvisible = 1;
    }

    if (_teamNoOfferAccepted != 0xFFFF) {
        /* ---- Accepted job path ---- */
        goto acceptedJobPath;
    }

    /* ---- New/existing offer path ---- */
    /* loc_14A400 */
    if (_jobOfferOrdinal >= 0) {
        _menuEntries[11].isInvisible = 1;
    } else {
        _menuEntries[10].isInvisible = 1;
    }

    if (!_jobOffersFromRetired) {
        MenuEntry *acceptBtn = &_menuEntries[13];
        acceptBtn->isDisabled = 1;
        acceptBtn->bgColor = (acceptBtn->bgColor & 0xFFF0) | 9; /* PINK_TO_BROWN_9 */
    }

    /* Set up team pointer and format strings */
    _teamOfferingJobPtr = (uintptr_t)GetTeamPointer(_teamNoOfferingJob);

    /* Format "DEAR %a %b" with manager sex and surname */
    {
        uint8_t *out = (uint8_t *)_menuEntries[1].fg;
        PrintFormatted(tr_fmt(_aDearAB), out, 0,0,0,0,0,0,0, _managerSexBuffer, _managerSurnameBuffer, 0);
    }

    if (_jobOfferFunds != -1) {
        /* Domestic club offer */
        uint8_t *teamName = ((uint8_t *)_teamOfferingJobPtr) + 5;

        {
            uint8_t *out = (uint8_t *)_menuEntries[2].fg;
            PrintFormatted(&_aWeHereAtAWould[1], out, 0,0,0,0,0,0,0, teamName, 0, 0);
        }
        printToEntry(3, _aCoachingJobAtO, 0, 0);

        Int2Pounds((uint32_t)_jobOfferFunds, _fundsBuffer);

        {
            uint8_t *out = (uint8_t *)_menuEntries[4].fg;
            PrintFormatted(tr_fmt(_aWeCanOfferYouA), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
        {
            uint8_t *out = (uint8_t *)_menuEntries[5].fg;
            PrintFormatted(tr_fmt(_aWeHopeToBeAble), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
        {
            uint8_t *out = (uint8_t *)_menuEntries[6].fg;
            PrintFormatted(tr_fmt(_aInTheRegionOfA), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
        {
            uint8_t *out = (uint8_t *)_menuEntries[7].fg;
            PrintFormatted(tr_fmt(_aWeLookForwardT_1), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
    } else {
        /* International offer */
        MenuEntry *e0xA8 = &_menuEntries[3];
        int16_t newY = e0xA8->y + 10;
        _menuEntries[4].y = newY;

        uint8_t *teamName = ((uint8_t *)_teamOfferingJobPtr) + 5;

        {
            uint8_t *out = (uint8_t *)_menuEntries[2].fg;
            PrintFormatted(tr_fmt(_aTheMembersOfTh), out, 0,0,0,0,0,0,0, teamName, 0, 0);
        }
        printToEntry(3, _aOfAWouldLikeTo, teamName, 0);
        printToEntry(4, _aAsCoachOfTheNa, 0, 0);
        printToEntry(5, _aWeCanPromiseYo, 0, 0);
        printToEntry(6, _aAnAnnuallyRevi, 0, 0);
        printToEntry(7, _aWeLookForwardT, 0, 0);
    }
    return;

acceptedJobPath:
    /* loc_14A5D0: team already accepted */
    _menuEntries[0].fg = (uintptr_t)_aNewJob;
    {
        int16_t newY = _menuEntries[3].y + 10;
        _menuEntries[4].y = newY;
    }
    _menuEntries[10].isInvisible = 1;
    _menuEntries[13].isInvisible = 1;
    _menuEntries[8].isInvisible = 1;

    _teamOfferingJobPtr = (uintptr_t)GetTeamPointer(_teamNoOfferAccepted);

    {
        uint8_t *out = (uint8_t *)_menuEntries[1].fg;
        PrintFormatted(tr_fmt(_aDearAB), out, 0,0,0,0,0,0,0, _managerSexBuffer, _managerSurnameBuffer, 0);
    }

    if (_jobOfferFunds != -1) {
        /* Domestic accepted */
        uint8_t *teamName = ((uint8_t *)_teamOfferingJobPtr) + 5;

        {
            uint8_t *out = (uint8_t *)_menuEntries[2].fg;
            PrintFormatted(tr_fmt(_aWeHereAtAAreDe), out, 0,0,0,0,0,0,0, teamName, 0, 0);
        }
        printToEntry(3, _aHaveAcceptedTh, 0, 0);

        Int2Pounds(_teamFundsAccepted, _fundsBuffer);

        printToEntry(4, _aFromTheStartOf_0, _fundsBuffer, 0);
        {
            uint8_t *out = (uint8_t *)_menuEntries[5].fg;
            PrintFormatted(tr_fmt(_aWeHopeToBeAble_0), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
        {
            uint8_t *out = (uint8_t *)_menuEntries[6].fg;
            PrintFormatted(tr_fmt(_aInTheRegionOfA_0), out, 0,0,0,0,0,0,0, _fundsBuffer, 0, 0);
        }
        printToEntry(7, _aAndWeLookForwa, _fundsBuffer, 0);
    } else {
        /* International accepted */
        {
            int16_t newY = _menuEntries[4].y + 10;
            _menuEntries[5].y = newY;
        }
        _menuEntries[7].y -= 15;

        uint8_t *teamName = ((uint8_t *)_teamOfferingJobPtr) + 5;

        {
            uint8_t *out = (uint8_t *)_menuEntries[2].fg;
            PrintFormatted(tr_fmt(_aTheMembersOfTh_0), out, 0,0,0,0,0,0,0, teamName, 0, 0);
        }
        printToEntry(3, _aOfAAreDelighte, teamName, 0);
        printToEntry(4, _aAcceptedThePos, 0, 0);
        printToEntry(5, _aFromTheStartOf, 0, 0);
        {
            uint8_t *out = (uint8_t *)_menuEntries[6].fg;
            PrintFormatted(tr_fmt(_aWeLookForwardT_0), out, 0,0,0,0,0,0,0, 0, 0, 0);
        }
        {
            /* asm uses (aWeLookForwardT_0+1) which skips the leading \x00 byte */
            uint8_t *out = (uint8_t *)_menuEntries[7].fg;
            PrintFormatted(&_aWeLookForwardT_0[1], out, 0,0,0,0,0,0,0, 0, 0, 0);
        }
    }
}

/* ================================================================
 * AcceptJobOffer  (SWOS2.s line 82822)
 * ================================================================ */
__attribute__((used, externally_visible))
void AcceptJobOffer(void) {
    JobOffer *offer;
    if (_jobOfferOrdinal >= 0) {
        offer = GetNewJobOfferPtr((uint16_t)_jobOfferOrdinal);
    } else {
        offer = GetJobOfferPtr((uint16_t)_oldJobOfferOrdinal);
    }
    _teamNoOfferAccepted = offer->teamNo;
    _teamFundsAccepted = offer->amountOffered;
    _numNewJobOffers = 0;
    _numJobOffers = 0;

    /* GetCurrentSeasonPointer + check manager status */
    SeasonInformations *season = (SeasonInformations *)getCurrentSeasonPointer();
    if (season->managerStatus >= 0) {
        season->managerStatus = 1;
    }
    SetExitMenuFlag();
}


/* ================================================================
 * RejectJobOffer  (SWOS2.s line 82849)
 * ================================================================ */
__attribute__((used, externally_visible))
void RejectJobOffer(void) {
    uint16_t teamNo = _teamNoOfferingJob;
    if (_jobOfferOrdinal < 0) {
        JobOffers_RemoveOffer(teamNo);
    } else {
        RemoveJobOffer(teamNo);
    }
    SetExitMenuFlag();
}

/* ================================================================
 * ViewFutureTeam  (SWOS2.s line 82861)
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewFutureTeam(void) {
    _viewFutureTeamModeFlag = 1;
    ShowSquad((TeamFile *)_teamOfferingJobPtr, _aViewTeam, 0, 0, 1, -1);
    _viewFutureTeamModeFlag = 0;
    GotJobOfferFinish();
}

/* ================================================================
 * ViewFutureTeamLeague  (SWOS2.s line 82876)
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewFutureTeamLeague(void) {
    TeamFile *teamPtr = (TeamFile *)_teamOfferingJobPtr;
    int16_t d7 = (int8_t)teamPtr->league;
    uint8_t countryIdx = teamPtr->countryNumber;
    /* competitionsTable is `void *[]` (native-width pointers).
     * Walk as `void **` so each slot is sizeof(void*), not 4. The
     * legacy `uint32_t *` cast read half a pointer on 64-bit. */
    void **compTable = _competitionsTable;
    uint8_t *compEntry = (uint8_t *)compTable[countryIdx];
    uint8_t *comp = *(uint8_t **)compEntry;

    _highlightedLeagueTeamId = BE_READ_U16(teamPtr);
    Career_ViewOrEditCompetitionSlot(comp, (uint16_t)d7);
    _highlightedLeagueTeamId = 0xFFFF;
    GotJobOfferFinish();
}

/* ================================================================
 * ConsiderJobOffer  (SWOS2.s line 82894)
 * ================================================================ */
__attribute__((used, externally_visible))
void ConsiderJobOffer(void) {
    if (_jobOfferOrdinal >= 0) {
        JobOffer *offer = GetNewJobOfferPtr((uint16_t)_jobOfferOrdinal);
        JobOffers_ConvertNewOfferToOffer(offer->teamNo, offer->amountOffered);
    }
    SetExitMenuFlag();
}

/* ================================================================
 * Chunk 116: JobOffers_UpdateMenuEntryLayout, JobOffersFinish,
 *            JobOffersCommon, OldJobOffersCommon, JobOffersExit
 * ================================================================ */

extern uint8_t _gotJobOfferMenu[];

/* JobOffers_UpdateMenuEntryLayout — position old offer menu entries
 * based on number of new/old offers (SWOS2.s line 81335) */
__attribute__((used, externally_visible))
void JobOffers_UpdateMenuEntryLayout(void)
{
    /* Hide entries 13,14 and 15,16 */
    _menuEntries[13].isInvisible = 1;
    _menuEntries[14].isInvisible = 1;
    _menuEntries[15].isInvisible = 1;
    _menuEntries[16].isInvisible = 1;

    /* Hide entry 6 (old offers header) */
    _menuEntries[6].isInvisible = 1;

    if (_numJobOffers == 0)
        return;

    /* Show entry 6 */
    _menuEntries[6].isInvisible = 0;

    /* Calculate y position: start from entry 1's y, offset by new offers */
    uint16_t yPos = _menuEntries[1].y;
    yPos += (uint16_t)(_numNewJobOffers * 0xF);
    yPos += 0x1E;

    /* Entry 6: old offers header */
    _menuEntries[6].y = yPos;
    yPos += 0x14;

    /* Entries 7-11: old offer lines (5 entries, each 0xF apart) */
    int16_t i;
    for (i = 0; i < 5; i++) {
        _menuEntries[7 + i].y = yPos;
        yPos += 0xF;
    }
}

/* JobOffersFinish — update menu entry layout (SWOS2.s line 81275) */
__attribute__((used, externally_visible))
void JobOffersFinish(void)
{
    JobOffers_UpdateMenuEntryLayout();
}

/* JobOffersCommon — handle clicking on a new job offer entry (SWOS2.s line 81281).
 * Asm flow: ACCEPT or no-offers-left → SetExitMenuFlag (close the JOB
 * OFFERS menu); otherwise SetCurrentEntry(0xC). Our previous translation
 * only set the exit flag on the ACCEPT branch and silently fell through
 * for "all offers rejected, list now empty" — leaving the JOB OFFERS
 * menu open with disabled (but still selectable) blank rows that
 * re-fired JobOffersCommon on the stale newJobOffers[0] entry. */
__attribute__((used, externally_visible))
void JobOffersCommon(MenuEntry *entry)
{
    uint16_t baseOrdinal = _menuEntries[1].ordinal;
    int16_t d7 = (int16_t)(entry->ordinal - baseOrdinal);
    _jobOfferOrdinal = d7;
    _oldJobOfferOrdinal = 0xFFFF;
    ShowMenu(_gotJobOfferMenu);

    if (_teamNoOfferAccepted != 0xFFFF) {
        SetExitMenuFlag();
        return;
    }
    if (_numNewJobOffers != 0 || _numJobOffers != 0) {
        SetCurrentEntry(0xC);
        return;
    }
    SetExitMenuFlag();
}

/* OldJobOffersCommon — handle clicking on an old job offer entry (SWOS2.s line 81305).
 * Mirror exit semantics of JobOffersCommon — see comment above. */
__attribute__((used, externally_visible))
void OldJobOffersCommon(MenuEntry *entry)
{
    uint16_t baseOrdinal = _menuEntries[7].ordinal;
    int16_t d7 = (int16_t)(entry->ordinal - baseOrdinal);
    _jobOfferOrdinal = 0xFFFF;
    _oldJobOfferOrdinal = d7;
    ShowMenu(_gotJobOfferMenu);

    if (_teamNoOfferAccepted != 0xFFFF) {
        SetExitMenuFlag();
        return;
    }
    if (_numNewJobOffers != 0 || _numJobOffers != 0) {
        SetCurrentEntry(0xC);
        return;
    }
    SetExitMenuFlag();
}

/* JobOffersExit — remove expired offers and exit menu (SWOS2.s line 81328) */
__attribute__((used, externally_visible))
void JobOffersExit(void)
{
    JobOffers_RemoveExpiredOffers();
    SetExitMenuFlag();
}

/* ================================================================
 * Chunk 169 — JobOffersInit, JobOffers_DrawOldOfferLine,
 *             JobOffersAfterDrawCommon
 * ================================================================ */

extern uint32_t _irqCookieObfuscated;
extern uint8_t _aInternationalJ_0[];  /* "INTERNATIONAL JOB OFFER FROM %a" */
extern uint8_t _aJobOfferFromA[];     /* "JOB OFFER FROM %a" */
extern uint8_t _aJobOfferFromAW[];    /* "JOB OFFER FROM %a WITHDRAWN" */

/* ================================================================
 * JobOffersInit  (SWOS2.s line 72959)
 *
 * Menu init callback for the job offers menu.
 * Does an anti-piracy cookie check (always passes in HDD version),
 * fills missing team names, resets selected ordinal, and falls
 * through to JobOffersFinish.
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffersInit(void)
{
    /* Anti-piracy check: irqCookieObfuscated XOR 0xCFD1F80D must equal 0x17EA */
    uint32_t d0 = _irqCookieObfuscated ^ 0xCFD1F80Du;
    if (d0 != 0x17EA)
        return;  /* In original asm this would crash; safe no-op here */

    JobOffers_FillMissingNewOfferTeamNames();
    _selectedOldJobOfferOrdinal = 0xFFFF;

    /* Fall through to JobOffersFinish */
    JobOffersFinish();
}

/* ================================================================
 * JobOffers_DrawOldOfferLine  (SWOS2.s line 73003)
 *
 * After-draw callback for old job offer entries.
 * Calculates offer index from ordinal difference, formats offer
 * text, sets color highlighting for selected/withdrawn offers.
 *
 * in: a5 = current menu entry
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffers_DrawOldOfferLine(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    a5->isDisabled = 1;

    /* d7 = a5->ordinal - entries[7].ordinal (entry 7 = 0x188/0x38) */
    int16_t d7 = (int16_t)a5->ordinal - (int16_t)_menuEntries[7].ordinal;

    if ((uint16_t)d7 >= _numJobOffers)
        return;

    a5->isDisabled = 0;

    /* Show entries 15 and 16 (asm offsets $348/$380) */
    _menuEntries[15].isInvisible = 0;
    _menuEntries[16].isInvisible = 0;

    /* d5 = highlight color (0 = normal, 0xD = selected) */
    uint16_t d5 = 0;
    if ((uint16_t)d7 == _selectedOldJobOfferOrdinal)
        d5 = 0xD;

    /* Apply d5 to bg color bits (low 4) of entries 15 and 16 */
    {
        uint32_t bg15 = _menuEntries[15].bg;
        _menuEntries[15].bg = (bg15 & 0xFFF0FFFF) | ((uint32_t)d5 << 16);
    }
    {
        uint32_t bg16 = _menuEntries[16].bg;
        _menuEntries[16].bg = (bg16 & 0xFFF0FFFF) | ((uint32_t)d5 << 16);
    }

    /* Entry 15 (a3): set stringFlags base, y, number */
    MenuEntry *a3 = &_menuEntries[15];
    uint16_t d3 = a3->stringFlags & 0xFFF0;
    a3->y = a5->y;
    a3->fgWord = (uint16_t)(d7 + 1);  /* ENTRY_NUMBER: write high word */

    /* Entry 16 (a4): set stringFlags base, y, get dest buffer */
    MenuEntry *a4 = &_menuEntries[16];
    uint16_t d4 = a4->stringFlags & 0xFFF0;
    a4->y = a5->y;
    uint8_t *destBuf = (uint8_t *)a4->fg;

    /* Get offer and format text */
    JobOffer *offer = GetJobOfferPtr((uint16_t)d7);
    uint8_t *teamName = offer->teamName;

    uint8_t *fmtStr;
    if (offer->amountOffered == 0xFFFFFFFF)
        fmtStr = _aInternationalJ_0;  /* "INTERNATIONAL JOB OFFER FROM %a" */
    else
        fmtStr = _aJobOfferFromA;     /* "JOB OFFER FROM %a" */

    /* If withdrawn (funds == 0), use withdrawn text and color 6 */
    if (offer->amountOffered == 0) {
        fmtStr = _aJobOfferFromAW;    /* "JOB OFFER FROM %a WITHDRAWN" */
        d3 |= 6;
        d4 |= 6;
        a5->isDisabled = 1;
    }

    PrintFormatted(fmtStr, destBuf, 0, 0, 0, 0, 0, 0, 0,
                    teamName, 0, 0);

    a3->stringFlags = d3;
    a4->stringFlags = d4;

    /* Draw entries 15-16 */
    DrawMultipleItems(2, &_menuEntries[15]);
}

/* ================================================================
 * JobOffersAfterDrawCommon  (SWOS2.s line 73070)
 *
 * After-draw callback for new job offer entries.
 * Similar to DrawOldOfferLine but operates on new offers queue
 * and uses entries 11-12.
 *
 * in: a5 = current menu entry
 * ================================================================ */
__attribute__((used, externally_visible))
void JobOffersAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    a5->isDisabled = 1;

    /* d7 = a5->ordinal - entries[1].ordinal (entry 1 = 0x38/0x38) */
    int16_t d7 = (int16_t)a5->ordinal - (int16_t)_menuEntries[1].ordinal;

    if ((uint16_t)d7 >= _numNewJobOffers)
        return;

    a5->isDisabled = 0;

    /* Show entries 13 and 14 (asm offsets $2D8/$310) */
    _menuEntries[13].isInvisible = 0;
    _menuEntries[14].isInvisible = 0;

    /* Entry 13 (a3): set stringFlags base, y, number */
    MenuEntry *a3 = &_menuEntries[13];
    uint16_t d3 = a3->stringFlags & 0xFFF0;
    a3->y = a5->y;
    uint16_t numVal = (uint16_t)d7 + 1;
    a3->fgWord = numVal;  /* ENTRY_NUMBER: write high word */

    /* Entry 14 (a4): set stringFlags base, y, get dest buffer */
    MenuEntry *a4 = &_menuEntries[14];
    uint16_t d4 = a4->stringFlags & 0xFFF0;
    a4->y = a5->y;
    uint8_t *destBuf = (uint8_t *)a4->fg;

    /* Get offer and format text */
    JobOffer *offer = GetNewJobOfferPtr((uint16_t)d7);
    uint8_t *teamName = offer->teamName;

    uint8_t *fmtStr;
    if (offer->amountOffered == 0xFFFFFFFF)
        fmtStr = _aInternationalJ_0;  /* "INTERNATIONAL JOB OFFER FROM %a" */
    else
        fmtStr = _aJobOfferFromA;     /* "JOB OFFER FROM %a" */

    /* If withdrawn (funds == 0), use withdrawn text and color 6 */
    if (offer->amountOffered == 0) {
        fmtStr = _aJobOfferFromAW;    /* "JOB OFFER FROM %a WITHDRAWN" */
        d3 |= 6;
        d4 |= 6;
        a5->isDisabled = 1;
    }

    PrintFormatted(fmtStr, destBuf, 0, 0, 0, 0, 0, 0, 0,
                    teamName, 0, 0);

    a3->stringFlags = d3;
    a4->stringFlags = d4;

    /* Draw entries 13-14 */
    DrawMultipleItems(2, &_menuEntries[13]);
}

/* ================================================================
 * Career_MaybeGenerateJobOffer  (SWOS2.s line 66143)
 *
 * Checks conditions and potentially generates a random job offer
 * from another team during career mode. Handles domestic teams,
 * foreign (TMD) teams, and national team offers.
 *
 * Asm register convention:
 *   in:  none (uses globals)
 *   out: none (may call AddJobOffer)
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_MaybeGenerateJobOffer(void)
{
    uint16_t foundTeamId = 0;

    if (_teamNoOfferAccepted != 0xFFFF)
        return;
    if (_careerSeasonsCompleted == 7)
        return;

    int16_t maxNewOffers = 5 - (int16_t)_numJobOffers;
    if (maxNewOffers == 0)
        return;
    if ((int16_t)_numNewJobOffers >= maxNewOffers)
        return;
    if (_currentSeasonShowing == 0x13)
        return;

#ifdef SWOS_INJECT_WC
    /* DEBUG (SWOS_INJECT_WC): force the national-team job OFFER so the WC can be
     * reached without grinding reputation. The normal national path below only
     * fires after a random-phase gate (returns most seasons), board confidence
     * >= 0xC8, and RNG — which is why a club career can run for years without
     * ever being offered the national side ("pick your WC team" never appears).
     * Reuse the SAME validity checks as the real checkNational path so the offer
     * is well-formed; only the confidence/RNG/phase gates are bypassed. Fires
     * while still a club manager; once you ACCEPT it, selTeamNumber becomes that
     * national team and InitializeNewSeason's SWOS_INJECT_WC override makes it
     * play the World Cup. _IsNew/IsOldJobOffer prevent re-offering, so this adds
     * the offer once per opening rather than flooding. */
    if (isNationalSelector == 0 &&
        lastNationalityCall != 0xFFFF &&
        IsNewJobOffer(lastNationalityCall) != 0 &&
        IsOldJobOffer(lastNationalityCall) != 0 &&
        IsTeamCodeOkForNationalSelector((uint16_t)(lastNationalityCall >> 8)) != 0) {
        AddJobOffer(lastNationalityCall, (uint32_t)-1);
        return;
    }
#endif

    if (_careerJobOfferMode == 1)
        goto searchDomestic;
    if (_careerJobOfferMode == 2)
        goto checkNational;
    if (_youreSacked != 0)
        goto searchDomestic;

    /* Random phase check */
    {
        int8_t d0 = (int8_t)_viewRandomPhase - _careerJobOfferPhaseBias;
        int16_t d0w = (int16_t)d0;
        int32_t d0l = (int32_t)(d0w << 2);
        uint16_t divided = (uint16_t)((uint32_t)d0l / (uint32_t)_wageScalingDivisor);
        if (divided != 3)
            return;
    }

checkNational:
    /* Try national team offer */
    if (_sameNationalCallFlag == 0) {
        uint16_t natCall = _lastNationalityCall;
        if (natCall != 0xFFFF) {
            if (_careerJobOfferMode != 2) {
                if (_boardConfidenceScore == 0)
                    goto noNational;
            }
            /* Check if already offered */
            if (IsNewJobOffer(natCall) == 0)
                goto noNational;
            if (IsOldJobOffer(natCall) == 0)
                goto noNational;
            if (_careerJobOfferMode != 2) {
                int16_t conf = GetBoardConfidenceScore();
                if (conf < 0xC8)
                    goto noNational;
            }
            /* Check if team code is valid for national selector */
            uint16_t natHigh = natCall >> 8;
            if (IsTeamCodeOkForNationalSelector(natHigh) != 0) {
                /* Valid national offer */
                AddJobOffer(natCall, (uint32_t)-1);
                return;
            }
        }
    }

noNational:
    if (_careerJobOfferMode == 2)
        return;

    /* Random chance to generate offer */
    {
        uint16_t rnd = Rand();
        if (rnd >= 42)
            return;
    }

searchDomestic:
    ;
    /* Calculate reputation range [d6..d7] */
    uint16_t repLow, repHigh;
    uint16_t repScore;

    if (_youreSacked != 0 && _isNationalSelector != 0) {
        repScore = (uint16_t)_sackedReputationScore;
    } else if (_isNationalSelector != 0) {
        repScore = (uint16_t)_nationalSelectorReputationPoints;
    } else {
        /* Calculate from major trophy bonus and league position */
        uint16_t mtb = (uint16_t)(uint8_t)_primaryCompetitionBuf.tourn.header.trophyBonusValue;
        uint16_t low75 = (uint16_t)((uint16_t)(mtb * 0x4B) / 100);
        uint16_t high150 = (uint16_t)((uint16_t)(mtb * 0x96) / 100);

        /* Find team position in DIY table */
        int16_t pos = FindTeamInDIY(_selTeamNumber, (DIYCompetition *)&_primaryCompetitionBuf);

        uint16_t posPct = (uint16_t)((uint16_t)((uint16_t)pos * 100) / (_primaryCompetitionBuf.tourn.header.numTeams - 1));
        uint16_t invPct = 100 - posPct;

        uint16_t range = high150 - low75;
        uint16_t adj = (uint16_t)((uint16_t)(range * invPct) / 100);
        repScore = adj + low75;

        if (_isNationalSelector == 0) {
            _sackedReputationScore = (int16_t)repScore;
            _nationalSelectorReputationPoints = (int16_t)repScore;
        }
    }

    if (_youreSacked == 0) {
        repHigh = repScore;
        repLow = (uint16_t)((uint16_t)(repHigh * 0x4B) / 100);
    } else {
        repHigh = repScore;
        repLow = (uint16_t)((uint16_t)(repHigh * 0) / 100);  /* mulu #0 */
    }

    if (_careerJobOfferMode == 1)
        goto searchNational;

    if (_youreSacked != 0 && _isNationalSelector != 0)
        goto searchSacked;

    /* Try domestic (selected teams) first */
    {
        uint16_t rnd = Rand();
        if (rnd < 0x15) {
            /* Try national team selector offer */
            if (_isNationalSelector != 0)
                goto searchNational;

            /* Random chance for national team if player qualifies */
            rnd = Rand();
            if (rnd < 0x55) {
                goto searchForeign;
            }
            if (IsPlayerOkForNationalSelector() == 0)
                goto searchNational;
            goto searchForeign;
        }

        /* Search among selected teams (home division) */
        if (_isNationalSelector != 0)
            return;

        uint16_t selTeam = _selTeamNumber;
        uint8_t *a0;

        for (int16_t tries = 99; tries >= 0; tries--) {
            rnd = Rand();
            uint16_t idx = (uint16_t)((uint16_t)(rnd * _numSelectedTeams) >> 8);
            a0 = &_selTeamsPtr + (uint32_t)idx * 0x2AC;

            /* Skip custom teams (type 4) */
            TeamFile *a0tf = (TeamFile *)a0;
            if (Team_IsOutsideLeague(a0tf))     /* was league == 4 */
                continue;

            /* Must be same division (country). selTeamNumber MSB = country.
             * Pre-fix `*(uint8_t *)&selTeamNumber` read LE byte[0] = ordinal,
             * which rarely matched any team's country byte — so the 99-try
             * home-country job-offer search effectively never found a
             * candidate, and offers only ever showed up via the
             * `searchForeign` fallback that triggers when youreSacked. */
            if (World_FileCountry((uint8_t)(_selTeamNumber >> 8)) !=
                World_FileCountry(a0tf->countryNumber))     /* (a second team file: include/world.h) */
                continue;

            /* Asm: move.w (a0),d3 — reads packed team number at offset 0 */
            uint16_t teamId = PACK_BE_U16(a0tf->countryNumber, a0tf->teamOrdinal);
            if (teamId == selTeam)
                continue;

            uint16_t avgPrice = GetAveragePlayerPrice((TeamFileEx *)a0);
            if (avgPrice < repLow || avgPrice > repHigh)
                continue;

            if (IsNewJobOffer(teamId) == 0)
                continue;
            if (IsOldJobOffer(teamId) == 0)
                continue;

            /* Check division index match */
            uint16_t divIdx = _currentDivisionIndex;
            if ((uint8_t)divIdx != a0tf->league) {
                foundTeamId = teamId;
                goto foundWithBalance;
            }

            /* Same division — check league position */
            FindTeamInDIY(teamId, (DIYCompetition *)&_primaryCompetitionBuf);
            uint16_t minDiv = _primaryCompetitionBuf.tourn.header.minDivisionIndex + _primaryCompetitionBuf.tourn.header.maxDivisionOffset;
            if ((uint16_t)divIdx < minDiv)
                continue;

            foundTeamId = teamId;
            goto foundWithBalance;
        }

        if (_youreSacked != 0)
            goto searchForeign;
        return;
    }

searchForeign:
    /* Search among TMD (foreign) teams */
    {
        uint16_t selTeam = _selTeamNumber;
        uint8_t *base = &_tmdFileBuffer;

        for (int16_t tries = 99; tries >= 0; tries--) {
            uint16_t rnd = Rand();
            uint16_t idx = (uint16_t)((uint16_t)(rnd * _careerFileBuffer) >> 8);
            uint8_t *a0 = base + (uint32_t)idx * 0x2AC;

            TeamFile *a0tf = (TeamFile *)a0;
            if (Team_IsOutsideLeague(a0tf))     /* was league == 4 */
                continue;
            /* Foreign job-offer search — skip teams from the user's own
             * country. MSB of selTeamNumber is the country byte; the LE
             * byte-read pre-fix used the ordinal, so the skip rarely
             * fired and same-country teams accidentally counted as
             * foreign candidates. */
            if (World_FileCountry((uint8_t)(_selTeamNumber >> 8)) ==
                World_FileCountry(a0tf->countryNumber))
                continue;

            /* Asm: move.w (a0),d3 — reads packed team number at offset 0 */
            uint16_t teamId = PACK_BE_U16(a0tf->countryNumber, a0tf->teamOrdinal);
            if (teamId == selTeam)
                continue;

            uint16_t avgPrice = GetAveragePlayerPrice((TeamFileEx *)a0);
            if (avgPrice < repLow || avgPrice > repHigh)
                continue;

            if (IsNewJobOffer(teamId) == 0)
                continue;
            if (IsOldJobOffer(teamId) == 0)
                continue;

            foundTeamId = teamId;
            goto foundWithBalance;
        }
        return;
    }

searchNational:
    /* Search among national teams */
    {
        for (;;) {
            for (int16_t tries = 99; tries >= 0; tries--) {
                uint16_t rnd = Rand();
                uint32_t idx32 = (uint32_t)rnd * (uint32_t)_nat_teams_rand_limit1;
                uint16_t idx = (uint16_t)(idx32 >> 8);
                NationalTeamRecord *rec = (NationalTeamRecord *)_nationalTeams + (uint32_t)idx;

                /* teamId is compared against selTeamNumber (BE-packed)
                 * below — compose the same form via PACK_BE_U16. */
                uint16_t teamId = PACK_BE_U16(rec->countryNumber, rec->teamOrdinal);
                uint16_t skill = rec->skillOrPrice;

                if (skill < repLow || skill > repHigh)
                    continue;
                if (teamId == _selTeamNumber)
                    continue;
                if (teamId == _lastNationalityCall)
                    continue;

                if (IsTeamCodeOkForNationalSelector(rec->countryNumber) == 0)
                    continue;
                if (IsNewJobOffer(teamId) == 0)
                    continue;
                if (IsOldJobOffer(teamId) == 0)
                    continue;

                /* Found a valid national team offer */
                AddJobOffer(teamId, (uint32_t)-1);
                return;
            }

            /* Didn't find one — widen range and retry */
            if (repLow == 0)
                return;
            repLow -= 8;
            if ((int16_t)repLow < 0)
                repLow = 0;
        }
    }

searchSacked:
    /* Sacked + national selector: search teamFileBuffer */
    LoadTeamFile(_nationalTeamFileCode);
    repLow = repHigh >> 1;

    for (;;) {
        uint16_t selTeam = _selTeamNumber;

        for (int16_t tries = 99; tries >= 0; tries--) {
            uint16_t rnd = Rand();
            uint16_t numTeams = *(uint16_t *)&_teamFileBuffer;
            uint16_t idx = (uint16_t)((uint16_t)(rnd * numTeams) >> 8);
            uint8_t *a0 = (&_teamFileBuffer + 2) + (uint32_t)idx * 0x2AC;

            TeamFile *a0tf2 = (TeamFile *)a0;
            if (Team_IsOutsideLeague(a0tf2))    /* was league == 4 */
                continue;

            /* Asm: move.w (a0),d3 — reads packed team number at offset 0 */
            uint16_t teamId = PACK_BE_U16(a0tf2->countryNumber, a0tf2->teamOrdinal);

            uint16_t avgPrice = GetAveragePlayerPrice((TeamFileEx *)a0);
            if (avgPrice < repLow || avgPrice > repHigh)
                continue;

            if (IsNewJobOffer(teamId) == 0)
                continue;
            if (IsOldJobOffer(teamId) == 0)
                continue;

            foundTeamId = teamId;
            goto foundWithBalance;
        }

        if (repLow == 0)
            return;
        repLow -= 8;
        if ((int16_t)repLow < 0)
            repLow = 0;
    }

foundWithBalance:
    {
        uint32_t funds = CalculateStartingBalance(foundTeamId);
        AddJobOffer(foundTeamId, funds);
    }
}
