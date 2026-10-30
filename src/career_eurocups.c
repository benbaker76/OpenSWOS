/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* career_eurocups.c — European Cup qualification, grid caching, title holder management.
 *
 * Chunk 65: 7 exported + 12 static functions ported from SWOS2.s.
 */

#include "career_eurocups.h"
#include "game_types.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "career_status.h"
#include "career_teams.h"
#include "panic.h"
#include "league_phase.h"
#include "uefa_coef.h"

/* ---- ASM externs (globals defined in assembly) ---- */
extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t _primaryQualifiedTeamGrids[];
extern uint8_t _tertiaryQualifiedTeamGrid[];
extern uint8_t _euroCupTitleHolderGrid[];
extern uint8_t _cupWinnersCupTitleHolderGrid[];
extern uint8_t _uefaCupTitleHolderGrid[];

extern uint16_t _euroCupTitleHolderTeamId;
extern uint16_t _cupWinnersCupTitleHolderTeamId;
extern uint16_t _uefaCupTitleHolderTeamId;
extern uint16_t _cachedPrimaryQualifiedTeamIds[];
extern uint16_t _cachedSecondaryQualifiedTeamIds[];
extern uint16_t _cachedTertiaryQualifiedTeamId;

extern uint8_t _euroCupEligibleCompetitionIds[];
extern uint8_t _cupWinnersCupEligibleCompetitionIds[];
extern uint8_t _uefaCupEligibleCompetitionIds[];

extern uint8_t _currentCompetitionIndex;
extern uint16_t _euroCupQualifiedTeamCount;
extern uint16_t _cupWinnersCupQualifiedTeamCount;
extern uint16_t _uefaCupQualifiedTeamCount;

extern uint8_t *_euroCupTeamListPtr;
extern uint8_t *_cupWinnersCupTeamListPtr;
extern uint8_t *_uefaCupTeamListPtr;
extern uint8_t *_euroCupTeamListWritePtr;
extern uint8_t *_cupWinnersCupTeamListWritePtr;
extern uint8_t *_uefaCupTeamListWritePtr;

extern void *_countriesTable[];
extern uint16_t _selTeamNumber;

/* C-to-asm bridges */
extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);
extern void Career_AppendTeamToTmdTeamPool(TeamFileEx *team);

/* Already-ported C functions */
extern void DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs(void);
extern void DIY_SortLeagueTableByStandings(void);

/* ================================================================
 * Group A: List manipulation helpers (from loc_12B1F4, loc_12B20A)
 * ================================================================ */

/* isTeamInList — checks if team number is in a word list.
 * Asm: loc_12B1F4. Returns 1 if found, 0 if not.
 *
 * The list stores team IDs as BE byte pairs (matching Amiga `move.w`
 * semantics and the static `unk1[]` seed format) so the same bytes
 * survive a careerFileBuffer save/load round-trip on either endian
 * host. `team` is a host-native value `(country<<8)|ordinal` produced
 * by BE_READ_U16, so each list slot must also be decoded with
 * BE_READ_U16 before comparison. A naive `list[i] == team` compares
 * (ordinal<<8|country) on x86 against (country<<8|ordinal), which
 * never matches and lets duplicates slip through, eventually tripping
 * the 0x3443 TMD-pool panic when the same team is appended twice. */
static int16_t isTeamInList(uint16_t team, uint8_t *listPtr, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        if (BE_READ_U16(&listPtr[i * 2]) == team)
            return 1;
    }
    return 0;
}

/* appendTeamToList — appends team number to a word list and adds to TMD pool.
 * Asm: loc_12B20A. teamGrid points to the team grid data (first word = team number).
 *
 * teamGrid[0..1] are the team's `{country, ordinal}` byte pair in
 * Amiga BE order. Preserve that byte order in the list — i.e. write
 * teamGrid[0] then teamGrid[1] — so the list ends up as a sequence of
 * BE byte pairs matching the static `unk1[]` seed format. The Amiga's
 * `move.w (a5),(a2)+` does exactly this on a BE host; on x86 we have
 * to do it explicitly because a host-native uint16 store would write
 * the bytes in LE order, and downstream consumers (the spec reader's
 * BE_READ_U16, isTeamInList, drawRandomTeamFromPoolAndCopyGrid, and
 * the careerFileBuffer save/load round-trip) all expect BE bytes. */
static void appendTeamToList(uint8_t *teamGrid, uint8_t **writePtrAddr, uint16_t *count)
{
    uint8_t *writePos = *writePtrAddr;
    writePos[0] = teamGrid[0];
    writePos[1] = teamGrid[1];
    *writePtrAddr = writePos + 2;
    (*count)++;
    Career_AppendTeamToTmdTeamPool((TeamFileEx *)teamGrid);
}

/* tryConsumeCompetitionIdFromList — searches byte list for currentCompetitionId,
 * marks found entry as 0xFE. Returns 1 if found, 0 if not.
 * Asm: TryConsumeCompetitionIdFromList */
static int16_t tryConsumeCompetitionIdFromList(uint8_t *list)
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

/* isTeamInCupWinnersCupList — checks if team d0 is in the CWC qualified list.
 * Asm: IsTeamInCupWinnersCupList */
static int16_t isTeamInCupWinnersCupList(uint16_t team)
{
    return isTeamInList(team, _cupWinnersCupTeamListPtr, _cupWinnersCupQualifiedTeamCount);
}

/* isTeamInUefaCupList — checks if team d0 is in the UEFA Cup qualified list.
 * Asm: IsTeamInUefaCupList */
static int16_t isTeamInUefaCupList(uint16_t team)
{
    return isTeamInList(team, _uefaCupTeamListPtr, _uefaCupQualifiedTeamCount);
}

/* clearEuropeanCupTitleHolderIfMatchTeam — clears the matching title holder.
 * Asm: Career_ClearEuropeanCupTitleHolderIfMatchTeam */
static void clearEuropeanCupTitleHolderIfMatchTeam(uint16_t team)
{
    if (team == _euroCupTitleHolderTeamId) {
        _euroCupTitleHolderTeamId = 0xFFFF;
        return;
    }
    if (team == _cupWinnersCupTitleHolderTeamId) {
        _cupWinnersCupTitleHolderTeamId = 0xFFFF;
        return;
    }
    if (team == _uefaCupTitleHolderTeamId) {
        _uefaCupTitleHolderTeamId = 0xFFFF;
    }
}

/* appendTeamToEuroCupList — appends team grid to Euro Cup list.
 * Asm: AppendTeamToEuroCupList */
static void appendTeamToEuroCupList(uint8_t *teamGrid)
{
    appendTeamToList(teamGrid, &_euroCupTeamListWritePtr, &_euroCupQualifiedTeamCount);
}

/* appendTeamToCupWinnersCupList — appends team grid to CWC list.
 * Asm: AppendTeamToCupWinnersCupList */
static void appendTeamToCupWinnersCupList(uint8_t *teamGrid)
{
    appendTeamToList(teamGrid, &_cupWinnersCupTeamListWritePtr, &_cupWinnersCupQualifiedTeamCount);
}

/* appendTeamToUefaCupList — appends team grid to UEFA Cup list.
 * Asm: AppendTeamToUefaCupList */
static void appendTeamToUefaCupList(uint8_t *teamGrid)
{
    appendTeamToList(teamGrid, &_uefaCupTeamListWritePtr, &_uefaCupQualifiedTeamCount);
}

/* ================================================================
 * Group B: Qualification logic
 * ================================================================ */

/* qualifyTeamForEuroCup — if competition is eligible, qualify the primary
 * league winner for the Euro Cup.
 * Asm: Career_QualifyTeamForEuroCup_IfEligibleCompetition */
static void qualifyTeamForEuroCup(void)
{
    if (!tryConsumeCompetitionIdFromList(_euroCupEligibleCompetitionIds))
        return;
    uint8_t *a5 = _primaryQualifiedTeamGrids;
    /* See note on BE_READ_U16 in qualifyTeamForCupWinnersCup. */
    uint16_t team = BE_READ_U16(a5);
    clearEuropeanCupTitleHolderIfMatchTeam(team);
    appendTeamToEuroCupList(a5);
}

/* qualifyTeamForCupWinnersCup — if competition is eligible, find a non-primary,
 * non-Euro-Cup-holder team from the grid for the Cup Winners' Cup.
 * Asm: Career_QualifyTeamForCupWinnersCup_IfEligibleCompetition.
 *
 * Team identity is read from the grid record's first two bytes
 * (countryNumber, teamOrdinal). On Amiga BE `move.w (a5)` produced
 * `(country<<8)|ordinal` — the same BE-packed form used by
 * cachedPrimaryQualifiedTeamIds and euroCupTitleHolderTeamId. On LE
 * x86 a raw `*(uint16_t *)a5` byte-swaps it, the duplicate-detection
 * never matches, and the function picks a slot that's already in the
 * TMD pool from Euro Cup processing — tripping
 * Career_AppendTeamToTmdTeamPool's 0x3443 panic. Use BE_READ_U16. */
static void qualifyTeamForCupWinnersCup(void)
{
    if (!tryConsumeCompetitionIdFromList(_cupWinnersCupEligibleCompetitionIds))
        return;
    /* Start at slot 8 (primaryQualifiedTeamGrids + 0x1560), advance to find
     * a team that doesn't match the primary cached team or Euro Cup holder.
     * Boundary spill: walks through contiguous BSS buffers. */
    uint8_t *a5 = _primaryQualifiedTeamGrids + (8 * sizeof(TeamFileEx));
    for (;;) {
        a5 += sizeof(TeamFileEx);
        uint16_t team = BE_READ_U16(a5);
        if (team == _cachedPrimaryQualifiedTeamIds[0])
            continue;
        if (team == _euroCupTitleHolderTeamId)
            continue;
        break;
    }
    uint16_t team = BE_READ_U16(a5);
    clearEuropeanCupTitleHolderIfMatchTeam(team);
    appendTeamToCupWinnersCupList(a5);
}

/* qualifyTeamsForUefaCup — for each eligible competition ID, find a team
 * from the grid that hasn't been claimed by any other European cup.
 * Asm: Career_QualifyTeamsForUefaCup_IfEligibleCompetition */
static void qualifyTeamsForUefaCup(void)
{
    uint8_t *a5;
    if (_cachedTertiaryQualifiedTeamId != 0xFFFF) {
        a5 = _tertiaryQualifiedTeamGrid;
    } else {
        a5 = _primaryQualifiedTeamGrids + sizeof(TeamFileEx); /* slot 1 */
    }
    a5 -= sizeof(TeamFileEx);

    while (tryConsumeCompetitionIdFromList(_uefaCupEligibleCompetitionIds)) {
        for (;;) {
            a5 += sizeof(TeamFileEx);
            if (a5 == _euroCupTitleHolderGrid) {
                a5 = _primaryQualifiedTeamGrids + sizeof(TeamFileEx); /* wrap to slot 1 */
            }
            /* See note on BE_READ_U16 in qualifyTeamForCupWinnersCup. */
            uint16_t team = BE_READ_U16(a5);
            if (team == _cachedPrimaryQualifiedTeamIds[0])
                continue;
            if (team == _cachedSecondaryQualifiedTeamIds[0])
                continue;
            if (team == _euroCupTitleHolderTeamId)
                continue;
            if (team == _cupWinnersCupTitleHolderTeamId)
                continue;
            if (isTeamInCupWinnersCupList(team))
                continue;
            if (isTeamInUefaCupList(team))
                continue;
            break;
        }
        uint16_t team = BE_READ_U16(a5);
        clearEuropeanCupTitleHolderIfMatchTeam(team);
        appendTeamToUefaCupList(a5);
    }
}

/* ================================================================
 * Group B: Exported qualification entry point
 * ================================================================ */

/* ---- A world's league phases (include/league_phase.h): every country's top 9 and cup winner
 * go to the UEFA qualifying rounds (src/uefa_coef.c, the SWOS 96/97 Mod's uefaq), which fill
 * the three lists; the holders join after (Career_DrawEuropeanCupTeamsAndCopyGrids). ---- */

static int32_t worldLeaguePhases(void)
{
    const uint8_t (*places)[4];
    int32_t n;
    return World_LeaguePhaseCups(&places, &n) != 0;
}

/* The qualifying rounds' lists (uefa_coef.c): a club into cup 0..2's list and the TMD pool. */
void Career_AppendTeamToEuroCupListN(int32_t cup, TeamFileEx *record)
{
    if (cup == 0)
        appendTeamToEuroCupList((uint8_t *)record);
    else if (cup == 1)
        appendTeamToCupWinnersCupList((uint8_t *)record);
    else
        appendTeamToUefaCupList((uint8_t *)record);
}

__attribute__((used, externally_visible))
void Career_ProcessCompetitionForEuropeanCupQualification(void)
{
    /* countriesTable is uint32_t[] (pointer table); index by competition ID */
    uint16_t idx = (uint8_t)_currentCompetitionIndex;
    uint8_t *countryPtr = (uint8_t *)_countriesTable[idx];
    if (*countryPtr != 0)
        return; /* not European (first byte != 0) → skip */
    if (worldLeaguePhases()) {     /* (its top 9 and cup winner: the qualifying rounds, uefa_coef.c) */
        UefaQual_Country();
        return;
    }
    qualifyTeamForEuroCup();
    qualifyTeamForCupWinnersCup();
    qualifyTeamsForUefaCup();
}

/* ================================================================
 * Group C: Grid caching — copy team data from loaded teams to grids
 * ================================================================ */

/* copyTeamGridWords — volatile word copy to prevent memcpy optimization */
static void copyTeamGridWords(uint8_t *src, uint8_t *dst, uint16_t wordCount)
{
    volatile uint16_t *vs = (volatile uint16_t *)src;
    volatile uint16_t *vd = (volatile uint16_t *)dst;
    for (int16_t i = (int16_t)wordCount; i >= 0; i--) {
        *vd++ = *vs++;
    }
}

__attribute__((used, externally_visible))
void DIY_CachePrimaryQualifiedTeamsGrid(void)
{
    uint8_t *dst = _primaryQualifiedTeamGrids;
    uint16_t *cachedIds = _cachedPrimaryQualifiedTeamIds;
    DIYCompetitionEx *comp = &_diyCompetitionStart;
    uint16_t *leaguePositions = comp->matchData.league.teamEntryOffsets;
    uint16_t *teamIdsTable = comp->matchData.league.leagueTeamIds;
    uint16_t count = 9;
    uint16_t numTeams = comp->base.numTeams;
    if (count > numTeams)
        count = numTeams;
    /* asm: subq.w #1,d2 then dbf → loops count times */
    for (int16_t i = (int16_t)(count - 1); i >= 0; i--) {
        int16_t posIdx = (int16_t)*leaguePositions++;
        /* ext.l d0 then divu.w #$12,d0: sign-extend to 32-bit then unsigned div by 18 */
        uint16_t slotIdx = (uint16_t)((uint32_t)(int32_t)posIdx / 18);
        slotIdx <<= 1;
        /* leagueTeamIds is BE-packed per 295c9f3. */
        uint16_t teamNum = BE_READ_U16((uint8_t *)teamIdsTable + slotIdx);
        *cachedIds++ = teamNum;
        TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
        copyTeamGridWords((uint8_t *)teamData, dst, 0x155);
        dst += sizeof(TeamFileEx);
    }
}

__attribute__((used, externally_visible))
void DIY_CacheSecondaryQualifiedTeamsGrid(void)
{
    DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs();
    /* asm: lea (primaryQualifiedTeamGrids+0x180C),a1 — slot 9 (0x2AC * 9 = 0x180C) */
    uint8_t *dst = _primaryQualifiedTeamGrids + 0x180C;
    uint16_t *cachedIds = _cachedSecondaryQualifiedTeamIds;
    /* See copyTitleHolderGrid below for why these go through typed
     * struct access instead of `compRaw + 0xNN` byte offsets. */
    uint8_t *grid = _diyCompetitionStart.teamGridPairings;

    for (int16_t i = 2; i >= 0; i--) {
        int8_t gridByte = (int8_t)*grid++;
        int16_t idx = gridByte;
        /* cupTeamIds is BE-packed `{country, ordinal}` byte pairs. */
        uint16_t teamNum = BE_READ_U16((uint8_t *)_diyCompetitionStart.cupTeamIds + (idx << 1));
        *cachedIds++ = teamNum;
        TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
        copyTeamGridWords((uint8_t *)teamData, dst, 0x155);
        dst += sizeof(TeamFileEx);
    }
}

__attribute__((used, externally_visible))
void DIY_CacheTertiaryQualifiedTeamGrid(void)
{
    DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs();
    /* See copyTitleHolderGrid below — typed access avoids the +4
     * struct-field shift bug that hit copyTitleHolderGrid on 64-bit. */
    int8_t gridByte = (int8_t)_diyCompetitionStart.teamGridPairings[0];
    int16_t idx = gridByte;
    /* cupTeamIds is BE-packed `{country, ordinal}` byte pairs. */
    uint16_t teamNum = BE_READ_U16((uint8_t *)_diyCompetitionStart.cupTeamIds + (idx << 1));
    _cachedTertiaryQualifiedTeamId = teamNum;
    TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
    copyTeamGridWords((uint8_t *)teamData, _tertiaryQualifiedTeamGrid, 0x155);
}

/* ================================================================
 * Group D: Title holder grid copy
 * ================================================================ */

/* copyTitleHolderGrid — shared helper for the three title holder copy functions.
 *
 * Both `cupTeamIds[64]` and `teamGridPairings[64]` are named members of
 * DIYCompetitionEx, originally documented at struct offsets 0x24E / 0x2CE
 * on 32-bit Amiga. The parent DIYCompetition grew by 4 bytes on 64-bit
 * (namePtr 4→8), shifting both fields to 0x252 / 0x2D2 — so the original
 * hardcoded byte-offset reads `compRaw + 0x24E` / `+ 0x2CE` walked into
 * the wrong field on 64-bit, producing garbage teamNum values that
 * panic'd inside VerifyTeamLoaded. Use typed field access instead. */
static void copyTitleHolderGrid(uint16_t *titleHolderTeamId, uint8_t *titleHolderGrid)
{
    DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs();
    int8_t gridByte = (int8_t)_diyCompetitionStart.teamGridPairings[0];
    int16_t idx = gridByte;
    /* cupTeamIds is BE-packed `{country, ordinal}` byte pairs per the
     * 295c9f3 convention; read with BE_READ_U16 so titleHolderTeamId
     * holds the packed value the rest of the code expects. The byte
     * offset within the array is `gridByte << 1` (each entry is 2 bytes). */
    uint16_t teamNum = BE_READ_U16((uint8_t *)_diyCompetitionStart.cupTeamIds + (idx << 1));
    *titleHolderTeamId = teamNum;
    TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
    copyTeamGridWords((uint8_t *)teamData, titleHolderGrid, 0x155);
}

__attribute__((used, externally_visible))
void Career_CopyEuroCupTeamGridAndTitleHolder(void)
{
    copyTitleHolderGrid(&_euroCupTitleHolderTeamId, _euroCupTitleHolderGrid);
}

__attribute__((used, externally_visible))
void Career_CopyCupWinnersCupTeamGridAndTitleHolder(void)
{
    copyTitleHolderGrid(&_cupWinnersCupTitleHolderTeamId, _cupWinnersCupTitleHolderGrid);
}

__attribute__((used, externally_visible))
void Career_CopyUefaCupTeamGridAndTitleHolder(void)
{
    copyTitleHolderGrid(&_uefaCupTitleHolderTeamId, _uefaCupTitleHolderGrid);
}

/* ---- Chunk 181: Euro Cup Slot Update functions ---- */

extern uint8_t _euroCupCopy[];
extern uint8_t _cupWinnersCupCopy[];
extern uint8_t _uefaCupCopy[];
extern uint8_t *_quaternaryCompetitionPtr;

extern void SaveSelectedTeams(void);
extern void RestoreSelectedTeams(void);
extern void DIY_EditCompetitionSlot(uint16_t paramA, uint16_t paramB, uint16_t paramC);
extern void ShowDIYCupMenuWithParams(uint16_t arg0, uint16_t arg1, uint16_t arg2);

extern uint16_t _numSelectedTeams;
extern void AppendTeamsToSelectionFromTmdOrTeamFile(uint16_t *out, uint16_t *teamIds,
                                                     uint16_t count);

/* reloadEuroCupTeamSelectionFromRestoredField — loaded-career fix for the
 * CUP-format euro slots (Cup Winners' Cup / UEFA Cup).
 *
 * The interactive cup update shows the cup menu, whose DIYCupFinish ->
 * DIY_BuildTeamSelectionPointerCache validates EVERY cup team via
 * VerifyTeamLoaded, which only searches selTeamsPtr (+ current/career team).
 * So selTeamsPtr must hold the cup's field.
 *
 * On a FRESH in-session career the cup's teams are already in selTeamsPtr
 * (loaded when the cup was contested that season), so RestoreSelectedTeams
 * brings them back and the validation passes. On a LOADED career the load only
 * establishes the CURRENT competition's selection (the player's league); the
 * background euro-cup slot's team selection is never re-loaded, so selTeamsPtr
 * holds league teams -> VerifyTeamLoaded(<a cup team, e.g. 0x0E17>) panics
 * (SIGILL) during the season-advance CWC/UEFA update (repro: DIV3-2.CAR, club
 * in the Cup Winners' Cup).
 *
 * cupTeamIds round-trips through the slot serializer intact, so rebuild the
 * selection from it. numSelectedTeams is reset first because the selTeamsPtr
 * buffer holds only ~32 records (it cannot hold league+cup), and because the
 * cup-only selection IS the fresh-career state -> idempotent on a fresh career
 * (Phase-1 dedup re-finds the player's club via current/career team; the rest
 * re-load from the same team files). Endian-neutral (cupTeamIds is BE-packed
 * and the append helper reads/writes via BE_READ/BE_WRITE), so it back-ports to
 * the Amiga build, which has the identical loaded-career gap. */
static void reloadEuroCupTeamSelectionFromRestoredField(void)
{
    _numSelectedTeams = 0;
    AppendTeamsToSelectionFromTmdOrTeamFile(
        (uint16_t *)_diyCompetitionStart.cupTeamIds,
        (uint16_t *)_diyCompetitionStart.cupTeamIds,
        (uint16_t)_diyCompetitionStart.base.numTeams);
}

/* The same for a world's league phase (type 2, its 36 in the tournament's list): a loaded
 * career's selection holds its league only, and the slot's update validates every club of the
 * contest (VerifyTeamLoaded panic on the first club of another country). */
extern DIYTournamentState _diyFileBufferCopy;
static void reloadLeaguePhaseSelection(void)
{
    _numSelectedTeams = 0;
    AppendTeamsToSelectionFromTmdOrTeamFile(
        (uint16_t *)_diyFileBufferCopy.teamIds,
        (uint16_t *)_diyFileBufferCopy.teamIds,
        (uint16_t)_diyFileBufferCopy.header.numTeams);
}

/* Career_UpdateEuroCupCompetitionSlot (SWOS2.s line 31764)
 * If euroCupCopy is the active quaternary competition, edit it interactively;
 * otherwise auto-advance it as a league competition and copy the grid. */
__attribute__((used, externally_visible))
void Career_UpdateEuroCupCompetitionSlot(void)
{
    uint8_t *a4 = _euroCupCopy;
    /* Branch purely on whether this euro cup is the player's quaternary
     * competition — matches SWOS2.s Career_UpdateEuroCupCompetitionSlot @57397
     * (cmpa.l quaternaryCompetitionPtr,a4 / bne). A club->national transition
     * must leave quaternaryCompetitionPtr != euroCupCopy (a national coach never
     * manages a club euro cup); if a 0x8888 VerifyTeamLoaded panic ever
     * resurfaces here, the root cause is a STALE quaternaryCompetitionPtr, not
     * this branch — fix THAT, don't re-gate. See
     * project_eurocup_national_backup_clobber. */
    if (a4 == _quaternaryCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyEuroCupsCompetitionContextToOrFromActive();
        if (worldLeaguePhases())
            reloadLeaguePhaseSelection();
        DIY_EditCompetitionSlot(1, 1, 0);
        Career_CopyEuroCupTeamGridAndTitleHolder();
        if (worldLeaguePhases())
            RestoreSelectedTeams();     /* (the backup stays the league's, as the cup path below) */
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceLeagueCompetitionFromRecord(a4);
        Career_CopyEuroCupTeamGridAndTitleHolder();
    }
}

/* Career_UpdateCupWinnersCupCompetitionSlot (SWOS2.s line 31792)
 * Same pattern but for Cup Winners' Cup — uses ShowDIYCupMenuWithParams instead of EditSlot. */
__attribute__((used, externally_visible))
void Career_UpdateCupWinnersCupCompetitionSlot(void)
{
    uint8_t *a4 = _cupWinnersCupCopy;
    if (a4[1] == 2) {               /* (a world's league phase: a type-2 contest, as the Champions Cup) */
        if (a4 == _quaternaryCompetitionPtr) {
            RestoreSelectedTeams();
            Career_CopyEuroCupsCompetitionContextToOrFromActive();
            reloadLeaguePhaseSelection();
            DIY_EditCompetitionSlot(1, 1, 0);
            Career_CopyCupWinnersCupTeamGridAndTitleHolder();
            RestoreSelectedTeams();
            SaveSelectedTeams();
        } else {
            Career_AutoAdvanceLeagueCompetitionFromRecord(a4);
            Career_CopyCupWinnersCupTeamGridAndTitleHolder();
        }
        return;
    }
    if (a4 == _quaternaryCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyEuroCupsCompetitionContextToOrFromActive();
        reloadEuroCupTeamSelectionFromRestoredField();
        ShowDIYCupMenuWithParams(1, 1, 0);
        Career_CopyCupWinnersCupTeamGridAndTitleHolder();
        /* reloadEuroCupTeamSelectionFromRestoredField overwrote selTeamsPtr with
         * the cup field (needed by the menu's cache build + the title-grid copy
         * above), but it did NOT touch the shared savedTeamsBackupBuffer — which
         * still holds the LEAGUE selection that RestoreSelectedTeams loaded at
         * entry. Restore it before SaveSelectedTeams so the backup stays the
         * league selection: the very next season-advance step
         * (processPrimaryCompetitionForNextSeason -> RestoreSelectedTeams ->
         * DIY_CachePrimaryQualifiedTeamsGrid) needs the player's league teams,
         * and would otherwise VerifyTeamLoaded-panic on a league team (0x0503).
         * No-op on a fresh career (the backup already holds the league teams). */
        RestoreSelectedTeams();
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceCupCompetitionFromRecord(a4);
        Career_CopyCupWinnersCupTeamGridAndTitleHolder();
    }
}

/* Career_UpdateUefaCupCompetitionSlot (SWOS2.s line 31820)
 * Same pattern for UEFA Cup. */
__attribute__((used, externally_visible))
void Career_UpdateUefaCupCompetitionSlot(void)
{
    uint8_t *a4 = _uefaCupCopy;
    if (a4[1] == 2) {               /* (a world's league phase: a type-2 contest, as the Champions Cup) */
        if (a4 == _quaternaryCompetitionPtr) {
            RestoreSelectedTeams();
            Career_CopyEuroCupsCompetitionContextToOrFromActive();
            reloadLeaguePhaseSelection();
            DIY_EditCompetitionSlot(1, 1, 0);
            Career_CopyUefaCupTeamGridAndTitleHolder();
            RestoreSelectedTeams();
            SaveSelectedTeams();
        } else {
            Career_AutoAdvanceLeagueCompetitionFromRecord(a4);
            Career_CopyUefaCupTeamGridAndTitleHolder();
        }
        return;
    }
    if (a4 == _quaternaryCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyEuroCupsCompetitionContextToOrFromActive();
        reloadEuroCupTeamSelectionFromRestoredField();
        ShowDIYCupMenuWithParams(1, 1, 0);
        Career_CopyUefaCupTeamGridAndTitleHolder();
        /* Restore the league selection into selTeamsPtr before saving — see the
         * Cup Winners' Cup update above for the full rationale (the reload left
         * the cup field in selTeamsPtr; the shared backup still holds the league
         * teams the next advance step needs). */
        RestoreSelectedTeams();
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceCupCompetitionFromRecord(a4);
        Career_CopyUefaCupTeamGridAndTitleHolder();
    }
}

/* ---- Chunk 183: European Cup Team Draw ---- */

extern uint16_t _teamNoOfferAccepted;
extern uint16_t _careerFileBuffer;
extern uint16_t _panicReasonCode;

extern uint16_t Rand2(void);
extern void Career_RemoveTeamFromTmdTeamPool(uint16_t teamNum);
extern void Career_AppendTeamToTmdTeamPool(TeamFileEx *grid);
extern void PanicHaltLoop(void);

/* Career_DrawRandomTeamFromPoolAndCopyGrid (SWOS2.s line 31528)
 * Given a title holder team ID (d2), a destination grid (a3), a team list (a2),
 * and a count (d3), randomly select a team from the pool that isn't from the
 * same country as the title holder or the player's team, then swap it into
 * the title holder slot and update the TMD team pool. */
static void drawRandomTeamFromPoolAndCopyGrid(uint16_t titleHolder,
                                               uint8_t *dstGrid,
                                               uint16_t *teamList,
                                               uint16_t count)
{
    if (titleHolder == 0xFFFF)
        return;

    uint8_t titleHolderCountry = (uint8_t)(titleHolder >> 8);
    /* teamList stores BE byte pairs (see appendTeamToList). Read each slot
     * with BE_READ_U16 to get the (country<<8)|ordinal value the rest of
     * the function works in. */
    uint8_t *listBytes = (uint8_t *)teamList;

    uint16_t randIdx;
    uint16_t selectedTeam;
    uint8_t selectedCountry;

    for (;;) {
        randIdx = Rand2();
        randIdx = (uint16_t)((uint32_t)randIdx * count >> 8);
        selectedTeam = BE_READ_U16(&listBytes[randIdx * 2]);
        selectedCountry = (uint8_t)(selectedTeam >> 8);

        if (selectedCountry == titleHolderCountry)
            continue;

        if (_teamNoOfferAccepted != 0xFFFF) {
            if (selectedCountry == (uint8_t)(_teamNoOfferAccepted >> 8))
                continue;
        } else {
            if (selectedCountry == (uint8_t)(_selTeamNumber >> 8))
                continue;
        }
        break;
    }

    /* Swap: put title holder in the selected slot, take the selected team out.
     * Write the title holder back as BE bytes to match the list's encoding. */
    uint16_t removedTeam = BE_READ_U16(&listBytes[randIdx * 2]);
    BE_WRITE_U16(&listBytes[randIdx * 2], titleHolder);

    uint16_t savedCareerFile = _careerFileBuffer;
    Career_RemoveTeamFromTmdTeamPool(removedTeam);
    Career_AppendTeamToTmdTeamPool((TeamFileEx *)dstGrid);
    if (savedCareerFile != _careerFileBuffer) {
        _panicReasonCode = 0x8997;
        PanicHaltLoop();
    }
}

/* Career_DrawEuropeanCupTeamsAndCopyGrids (SWOS2.s line 31510)
 * Draws random title holder replacement teams for all three European cups. */
__attribute__((used, externally_visible))
void Career_DrawEuropeanCupTeamsAndCopyGrids(void)
{
    if (worldLeaguePhases()) {      /* (a world's league phases: the holders join, none is swapped) */
        if (_euroCupTitleHolderTeamId != 0xFFFF)
            appendTeamToEuroCupList(_euroCupTitleHolderGrid);
        if (_cupWinnersCupTitleHolderTeamId != 0xFFFF)
            appendTeamToEuroCupList(_cupWinnersCupTitleHolderGrid);
        if (_uefaCupTitleHolderTeamId != 0xFFFF && !UefaQual_KeepsUeclHolder())
            appendTeamToCupWinnersCupList(_uefaCupTitleHolderGrid);     /* (unless it won its league) */
        return;
    }
    drawRandomTeamFromPoolAndCopyGrid(
        _euroCupTitleHolderTeamId, _euroCupTitleHolderGrid,
        (uint16_t *)_euroCupTeamListPtr, _euroCupQualifiedTeamCount);

    drawRandomTeamFromPoolAndCopyGrid(
        _cupWinnersCupTitleHolderTeamId, _cupWinnersCupTitleHolderGrid,
        (uint16_t *)_cupWinnersCupTeamListPtr, _cupWinnersCupQualifiedTeamCount);

    drawRandomTeamFromPoolAndCopyGrid(
        _uefaCupTitleHolderTeamId, _uefaCupTitleHolderGrid,
        (uint16_t *)_uefaCupTeamListPtr, _uefaCupQualifiedTeamCount);
}

/* ---- Chunk 184: European Championship + World Cup Competition Updates ---- */

extern uint8_t *_currentCompetitionPtr;
extern uint8_t _someTeamFlags;
extern uint16_t _euroChampionshipQualTeam;
extern uint8_t _europeanChampionships2Copy[];
extern uint8_t _ecQualifyingGroup1Copy[];
extern uint8_t _ecQualifyingGroup2Copy[];
extern uint8_t _ecQualifyingGroup3Copy[];
extern uint8_t _ecQualifyingGroup4Copy[];
extern uint8_t _ecQualifyingGroup5Copy[];
extern uint8_t _ecQualifyingGroup6Copy[];
extern uint8_t _ecQualifyingGroup7Copy[];
extern uint8_t _ecQualifyingGroup8Copy[];

extern CompetitionSlotBuffer _primaryCompetitionBuf;          /* slot-0 untangle (S6): typed buffer */
extern uint16_t _worldCupHostTeam;
extern uint8_t _worldCup2Copy[];
extern uint8_t _worldCupQualifyingGroup1Copy[];
extern uint8_t _worldCupQualifyingGroup2Copy[];
extern uint8_t _worldCupQualifyingGroup3Copy[];
extern uint8_t _worldCupQualifyingGroup4Copy[];
extern uint8_t _worldCupQualifyingGroup5Copy[];
extern uint8_t _worldCupQualifyingGroup6Copy[];
extern uint8_t _worldCupQualifyingGroup7Copy[];
extern uint8_t _worldCupQualifyingGroup8Copy[];
extern uint8_t _northAmericaWorldCupQualificationCopy[];
extern uint8_t _southAmericaWorldCupQualificationCopy[];
extern uint8_t _africaWorldCupQualificationCopy[];
extern uint8_t _asiaWorldCupQualificationCopy[];
extern uint8_t _oceaniaWorldCupQualificationCopy[];

extern void Career_CopyCompetitionFileBufferToOrFromActive(void);
extern void ShowDiyLeaguesTournamentMenu(uint16_t type, uint16_t viewOnly, uint16_t focusOverride);
extern void DIY_BuildQualifiedTeamListFromSortedTable(uint16_t count, uint8_t *out);
extern void DIY_BuildQualifiedTeamListFromDiyFileBuffer(uint16_t count, uint8_t *out);
extern void Career_AutoAdvanceInternationalCompetitionFixtures(uint8_t *record, uint16_t index);

/* Career_UpdateEuropeanChampionshipCompetitionSlot (SWOS2.s line 31555)
 * in: competition record, count of teams to qualify, output list ptr.
 * If active competition, edit interactively; else auto-advance. */
static void updateECSlot(uint8_t *competition, uint16_t count, uint8_t *out)
{
    if (competition == _currentCompetitionPtr) {
        RestoreSelectedTeams();
        Career_CopyCompetitionFileBufferToOrFromActive();
        ShowDiyLeaguesTournamentMenu(1, 1, 0);
        DIY_BuildQualifiedTeamListFromSortedTable(count, out);
        SaveSelectedTeams();
    } else {
        Career_AutoAdvanceInternationalCompetitionFixtures(competition, 0);
        DIY_BuildQualifiedTeamListFromSortedTable(count, out);
    }
}

/* Career_UpdateEuropeanChampionshipCompetitions (SWOS2.s line 31524) */
__attribute__((used, externally_visible))
void Career_UpdateEuropeanChampionshipCompetitions(void)
{
    if (!(_someTeamFlags & 0x01))
        return;

    uint8_t *a5 = _europeanChampionships2Copy;
    /* qualifier list = the cup *Copy's embedded teamSeedList. comp_team_seed_list
     * handles base+7+offset and the 64-bit name[2] pointer-growth delta — without
     * it the writes would land in name[1] and corrupt the comp name pointer
     * (SIGSEGV when the season list is drawn). */
    a5 = (uint8_t *)comp_team_seed_list((Competition *)a5);
    /* Asm `move.w euroChampionshipQualTeam,(a5)+` writes BE on m68k.
     * Use BE_WRITE_U16 so the destination byte order matches the rest
     * of europeanChampionships2Copy.unk1 (BE-packed `{country, ordinal}`
     * per the 295c9f3 convention). */
    BE_WRITE_U16(a5, _euroChampionshipQualTeam);
    a5 += 2;

    /* Asm uses `(a5)+` inside DIY_BuildQualifiedTeamListFromSortedTable_EC,
     * advancing a5 by `count * 2` bytes across each slot call. The m68k
     * register is shared between caller and callee; in C we have to
     * advance the caller's pointer explicitly after each call. Without
     * this, all 8 group calls overwrite the same first slots and entries
     * past index 2 stay stale. */
    updateECSlot(_ecQualifyingGroup1Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup2Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup3Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup4Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup5Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup6Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup7Copy, 2, a5); a5 += 2 * 2;
    updateECSlot(_ecQualifyingGroup8Copy, 1, a5);
}

/* Career_UpdateWorldCupCompetitionSlot (SWOS2.s line 31638)
 * More complex: handles type 0 (league), type 2 (cup), and error cases. */
static void updateWCSlot(uint8_t *competition, uint16_t count, uint8_t *out)
{
    int8_t compType = ((Competition *)competition)->type;
    if (competition == _currentCompetitionPtr) {
        RestoreSelectedTeams();
        if (compType == 2) {
            Career_CopyCompetitionFileBufferToOrFromActive();
            DIY_EditCompetitionSlot(1, 1, 0);
            DIY_BuildQualifiedTeamListFromDiyFileBuffer(count, out);
            SaveSelectedTeams();
        } else if (compType == 0) {
            Career_CopyCompetitionFileBufferToOrFromActive();
            ShowDiyLeaguesTournamentMenu(1, 1, 0);
            DIY_BuildQualifiedTeamListFromSortedTable(count, out);
            SaveSelectedTeams();
        } else {
            _panicReasonCode = 0x1991;
            PanicHaltLoop();
        }
    } else {
        if (compType == 2) {
            Career_AutoAdvanceLeagueCompetitionFromRecord(competition);
            DIY_BuildQualifiedTeamListFromDiyFileBuffer(count, out);
        } else if (compType == 0) {
            Career_AutoAdvanceInternationalCompetitionFixtures(competition, 0);
            DIY_BuildQualifiedTeamListFromSortedTable(count, out);
        } else {
            _panicReasonCode = 0x1992;
            PanicHaltLoop();
        }
    }
}

/* Career_UpdateWorldCupCompetitions (SWOS2.s line 31589) */
__attribute__((used, externally_visible))
void Career_UpdateWorldCupCompetitions(void)
{
    if (_someTeamFlags & 0x80)
        _worldCupHostTeam = _primaryCompetitionBuf.tourn.header.winnerTeam;

    if (!(_someTeamFlags & 0x40))
        return;

    uint8_t *a5 = _worldCup2Copy;
    /* qualifier list = the cup *Copy's embedded teamSeedList — see the EC variant
     * above. comp_team_seed_list handles the offset + 64-bit delta. */
    a5 = (uint8_t *)comp_team_seed_list((Competition *)a5);
    /* BE write — matches the eurochampionship variant above and the
     * BE-packed convention for *.unk1 team-id lists (295c9f3). */
    BE_WRITE_U16(a5, _worldCupHostTeam);
    a5 += 2;

    /* a5 is shared across slot calls in the m68k version because the
     * post-increment writes happen via the register inside
     * DIY_BuildQualifiedTeamListFromSortedTable_WC / FromDiyFileBuffer.
     * Advance the C pointer by `count * 2` bytes after each call so all
     * 13 group qualifiers land in distinct slots of worldCup2Copy.unk1. */
    updateWCSlot(_worldCupQualifyingGroup1Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup2Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup3Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup4Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup5Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup6Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup7Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_worldCupQualifyingGroup8Copy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_northAmericaWorldCupQualificationCopy, 3, a5); a5 += 3 * 2;
    updateWCSlot(_southAmericaWorldCupQualificationCopy, 4, a5); a5 += 4 * 2;
    updateWCSlot(_africaWorldCupQualificationCopy, 5, a5); a5 += 5 * 2;
    updateWCSlot(_asiaWorldCupQualificationCopy, 2, a5); a5 += 2 * 2;
    updateWCSlot(_oceaniaWorldCupQualificationCopy, 1, a5);
}
