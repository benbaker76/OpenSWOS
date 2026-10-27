/*
 * challenge.c -- SAVE STATE / CHALLENGES. See include/challenge.h and docs/CHALLENGE.md.
 *
 * THE FILE (big-endian throughout, like every other format in this tree; uncompressed).
 *
 *   header, 96 bytes
 *     0   4   "SWCH"
 *     4   2   version (1)
 *     6   2   flags (0)
 *     8   8   SimState_LayoutHash() of the build that wrote it
 *     16  8   SimState_Hash(SIM_TIER_SIM) of the position
 *     24  4   the frame of the match
 *     28  4   setup bytes that follow the header
 *     32  4   state bytes that follow the setup
 *     36  2   the width the match was simulated at
 *     38  2   _pitchNumberAndType
 *     40  1   rules (CHAL_RULE_*)
 *     41  1   the human's side, 1 or 2
 *     42  2   the minute on the clock
 *     44  2   team 1 goals
 *     46  2   team 2 goals
 *     48  16  team 1's name
 *     64  16  team 2's name       (both for the list: no load needed to show a position)
 *     80  16  the build stamp that wrote it
 *   setup   the scalars below, then both teams, then the six user tactics
 *   state   SimState_SavePortable(SIM_TIER_SIM)
 *
 * WHY THE SETUP EXISTS AT ALL. The state registry holds what the match READS frame to
 * frame, which is why loading it resumes the match. It deliberately does not hold what
 * the match was SET UP from, and some of that is still read after kick-off:
 *
 *   - ApplyTeamTactics re-rolls a player's skill from _randomSeed, _newRandomSeedCopy and
 *     _currentSeasonShowing on every SUBSTITUTION, so a resumed match whose values
 *     differed would give a substitute different skills than the original match would;
 *   - _gameLength decides when the whistle goes, _gameMaxSubstitutes how many subs are
 *     left to make, the tie rules whether a draw goes to extra time and penalties;
 *   - the two TEAM FILES are where the bench comes from, and the team the match plays is
 *     built from them by the init.
 *
 * The random GENERATORS, on the other hand, are registry regions (_seed, _randXorIndex,
 * _randXorKey and their secondary three), so the state carries them and this file must
 * not: the position's seeds are the ones it continues with.
 *
 * ONE TABLE, NOT TWO LISTS. The scalars are described once, in s_scalars, and the writer,
 * the reader and the self-test all walk it. A format with a `write` list and a parallel
 * `read` list is where a field quietly stops making the trip -- it keeps whatever this
 * machine had and the round trip still passes.
 */
#include "challenge.h"

#include "sim_state.h"
#include "game_types.h"
#include "endian_helpers.h"
#include "swos_io.h"
#include "animated_patterns.h"   /* TEMPORARY: tile base for the pitch dump */
#include "display.h"        /* _sim_draw_width */
#include "dos_mode.h"       /* _gDosGameplay, _gDosView */
#include "test_rules.h"
#include "build_info.h"

#include <string.h>

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#define CHAL_LOG(...)   fprintf(stderr, "[challenge] " __VA_ARGS__)
#else
#define CHAL_LOG(...)   do { } while (0)
#endif

/* ---- the globals a position is set up from ---- */

extern TeamFileEx _selectedTeams[100];
extern uint16_t   _numSelectedTeams;

extern uint8_t _teamTacticsUSERA[], _teamTacticsUSERB[], _teamTacticsUSERC[];
extern uint8_t _teamTacticsUSERD[], _teamTacticsUSERE[], _teamTacticsUSERF[];

extern uint16_t _pitchTypeOrSeason, _friendlyPitchType, _friendlySeason, _pitchType;
extern uint16_t _gameLength, _autoReplays, _autoSaveHighlights;
extern uint16_t _allPlayerTeamsEqual, _minSubstitutes, _maxSubstitutes;
extern uint16_t _gameType, _trainingGame, _isCareer, _twoPlayers;
extern uint16_t _pl1Coach, _pl2Coach;
extern uint16_t _topTeamPlayerNo, _topTeamCoachNo;
extern uint16_t _bottomTeamPlayerNo, _bottomTeamCoachNo;
extern int16_t  _pl1Tactics, _pl2Tactics;
extern uint16_t _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
extern uint16_t _randomSeed, _currentSeasonShowing;
extern uint8_t  _newRandomSeedCopy, _viewRandomPhase;
extern uint16_t _extraTimeState, _penaltiesState, _secondLeg, _playing2ndGame;
extern uint16_t _team1GoalsFirstLeg, _team2GoalsFirstLeg;
extern uint16_t _gameMaxSubstitutes;

/* ---- what the save key has to check, and what the header reports ---- */

extern volatile uint16_t _playGame;
extern uint16_t _gameState;
extern uint16_t _frameCount;
extern uint8_t  _gameTime[4];
extern uint16_t _team1TotalGoals, _team2TotalGoals;
extern uint16_t _pitchNumberAndType;
extern uint16_t _team1Computer, _team2Computer;
extern uint16_t _inSubstitutesMenu, _replayState, _playHighlightFlag;
extern TeamGame        _topTeamInGame, _bottomTeamInGame;
extern TeamGeneralInfo _topTeamData, _bottomTeamData;

extern void InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);
extern void InitTeams(void);

#define CHAL_MAGIC        0x53574348u   /* "SWCH" */
#define CHAL_VERSION      1
#define CHAL_HEADER_BYTES 96
#define TACTIC_BYTES      370
#define NUM_TACTICS       6

/* ---- the scalar table ---- */

typedef struct {
    void       *addr;
    uint8_t     width;      /* 1 or 2 bytes in this build's memory */
    const char *name;       /* for the self-test's report */
} ChalScalar;

#define S16(sym) { &sym, 2, #sym }
#define S8(sym)  { &sym, 1, #sym }

static const ChalScalar s_scalars[] = {
    /* the pitch and the match's shape */
    S16(_pitchTypeOrSeason), S16(_friendlyPitchType), S16(_friendlySeason), S16(_pitchType),
    S16(_gameLength), S16(_autoReplays), S16(_autoSaveHighlights),
    S16(_minSubstitutes), S16(_maxSubstitutes), S16(_gameMaxSubstitutes),
    S16(_gameType), S16(_trainingGame), S16(_isCareer), S16(_twoPlayers),
    S16(_allPlayerTeamsEqual),
    /* who is playing which side, and with which tactics */
    S16(_pl1Coach), S16(_pl2Coach),
    S16(_topTeamPlayerNo), S16(_topTeamCoachNo),
    S16(_bottomTeamPlayerNo), S16(_bottomTeamCoachNo),
    S16(_pl1Tactics), S16(_pl2Tactics),
    S16(_team1SelectedPlayerIndex), S16(_team2SelectedPlayerIndex),
    /* what a substitution's skill rolls mix in (see the file header) */
    S16(_randomSeed), S16(_currentSeasonShowing),
    S8(_newRandomSeedCopy), S8(_viewRandomPhase),
    /* how a draw is settled -- the match init clears all six, so they are applied again
     * after it (Challenge_ApplySetup is called twice) */
    S16(_extraTimeState), S16(_penaltiesState), S16(_secondLeg), S16(_playing2ndGame),
    S16(_team1GoalsFirstLeg), S16(_team2GoalsFirstLeg),
};

#undef S16
#undef S8

#define SCALAR_COUNT ((int32_t)(sizeof s_scalars / sizeof s_scalars[0]))
#define SETUP_BYTES  (4 + SCALAR_COUNT * 2 + 2 * (int32_t)sizeof(TeamFileEx) \
                      + NUM_TACTICS * TACTIC_BYTES)

static uint8_t *tactic_slot(int32_t i)
{
    switch (i) {
    case 0:  return _teamTacticsUSERA;
    case 1:  return _teamTacticsUSERB;
    case 2:  return _teamTacticsUSERC;
    case 3:  return _teamTacticsUSERD;
    case 4:  return _teamTacticsUSERE;
    default: return _teamTacticsUSERF;
    }
}

/* ---- the armed position ---- */

static uint8_t      *s_file;        /* the whole file, while a position is armed */
static uint32_t      s_fileLen;
static const uint8_t *s_setup;      /* into s_file */
static const uint8_t *s_state;
static uint32_t      s_setupLen, s_stateLen;
static ChallengeInfo s_info;
static int32_t       s_armed;
static int32_t       s_stateLoaded;
static int32_t       s_noticeFrames;
static char          s_notice[24];
/* The menus' own setup globals, kept across a challenge (Challenge_Play). */
static uint16_t      s_menuScalars[64];

/* ---- the setup block ---- */

static void setup_write(uint8_t *p)
{
    int32_t i;

    BE_WRITE_U16(p, CHAL_VERSION);              p += 2;
    BE_WRITE_U16(p, (uint16_t)SCALAR_COUNT);    p += 2;
    for (i = 0; i < SCALAR_COUNT; i++) {
        const ChalScalar *s = &s_scalars[i];
        uint16_t v = (s->width == 1) ? *(const uint8_t *)s->addr
                                     : *(const uint16_t *)s->addr;
        BE_WRITE_U16(p, v);
        p += 2;
    }
    /* The teams as bytes, which is right because of what they are and not by luck: no
     * pointers, packed, size-asserted at 684, every PlayerFile field a single byte -- and
     * TeamFile's one multi-byte field written in wire order. */
    for (i = 0; i < 2; i++) {
        memcpy(p, &_selectedTeams[i], sizeof(TeamFileEx));
        BE_WRITE_U16(p + 2, (uint16_t)_selectedTeams[i].team.globalTeamNumber);
        p += sizeof(TeamFileEx);
    }
    for (i = 0; i < NUM_TACTICS; i++) {
        memcpy(p, tactic_slot(i), TACTIC_BYTES);
        p += TACTIC_BYTES;
    }
}

/* 0 if this block can be applied here. */
static int32_t setup_check(const uint8_t *p, uint32_t len)
{
    if (len != (uint32_t)SETUP_BYTES)
        return 1;
    if (BE_READ_U16(p) != CHAL_VERSION)
        return 2;
    if (BE_READ_U16(p + 2) != (uint16_t)SCALAR_COUNT)
        return 3;
    return 0;
}

static void setup_apply(const uint8_t *p)
{
    int32_t i;

    p += 4;     /* version, count -- checked by setup_check */
    for (i = 0; i < SCALAR_COUNT; i++) {
        const ChalScalar *s = &s_scalars[i];
        uint16_t v = BE_READ_U16(p);
        if (s->width == 1) *(uint8_t *)s->addr  = (uint8_t)v;
        else               *(uint16_t *)s->addr = v;
        p += 2;
    }
    for (i = 0; i < 2; i++) {
        memcpy(&_selectedTeams[i], p, sizeof(TeamFileEx));
        _selectedTeams[i].team.globalTeamNumber = (int16_t)BE_READ_U16(p + 2);
        p += sizeof(TeamFileEx);
    }
    for (i = 0; i < NUM_TACTICS; i++) {
        memcpy(tactic_slot(i), p, TACTIC_BYTES);
        p += TACTIC_BYTES;
    }
    if (_numSelectedTeams < 2)
        _numSelectedTeams = 2;
}

/* ---- the rules a position was played under ---- */

static uint8_t rules_here(void)
{
    uint8_t r = 0;
#ifdef SWOS_SDL_BUILD
    if (_gDosGameplay) r |= CHAL_RULE_DOS_GAMEPLAY;
    if (_gDosView)     r |= CHAL_RULE_DOS_VIEW;
#endif
#ifdef SWOS_TEST_RULES
    if (TestRules_NoInjuriesCards()) r |= CHAL_RULE_NO_CARDS;
#endif
    return r;
}

static uint16_t clock_minute(void)
{
    return (uint16_t)(_gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3]);
}

/* ---- saving ---- */

/* How many sides a human is playing, asked of the match itself rather than of the menu
 * globals: a side is somebody's if its TeamGeneralInfo names a player or a player-coach,
 * which is the same question MatchStep_TeamOwner asks to route a bench call, and what
 * MainKeysCheck's own ESC handler reads to decide whose match it is. */
static int32_t human_sides(void)
{
    int32_t n = 0;
    if (_topTeamData.playerNumber || _topTeamData.playerCoachNumber)       n++;
    if (_bottomTeamData.playerNumber || _bottomTeamData.playerCoachNumber) n++;
    return n;
}

int32_t Challenge_CanSave(void)
{
    /* A match has to be running. */
    if (!_playGame)
        return 0;
    /* One human side against the computer, and only that: a challenge is a position you
     * play out yourself (docs/CHALLENGE.md). The same pair of globals the shooting-practice
     * latch reads, and the same question -- exactly one computer side. */
    if (human_sides() != 1)
        return 0;       /* two players, or a match nobody is playing */
    /* Not in the middle of something that is not a position. */
    if (_inSubstitutesMenu || _replayState || _playHighlightFlag)
        return 0;
    if (_gameState == ST_RESULT_AFTER_THE_GAME || _gameState == ST_RESULT_ON_HALFTIME)
        return 0;
#ifdef SWOS_SDL_BUILD
    {
        /* A live match's position is the other player's too, and writing a file inside a
         * frame would stall them. Its own reason, rather than relying on the rule above --
         * which already excludes it, both sides being human. */
        extern int32_t NetplayMatch_IsLive(void);
        if (NetplayMatch_IsLive())
            return 0;
    }
#endif
    return 1;
}

static void name_for_slot(int32_t slot, uint8_t *out)
{
    /* CHAL01.CHL .. CHAL99.CHL -- an 8.3 name, which the Amiga and the game's own file
     * picker both want. */
    out[0] = 'C'; out[1] = 'H'; out[2] = 'A'; out[3] = 'L';
    out[4] = (uint8_t)('0' + (slot / 10) % 10);
    out[5] = (uint8_t)('0' + slot % 10);
    out[6] = '.'; out[7] = 'C'; out[8] = 'H'; out[9] = 'L'; out[10] = 0;
}

/* TEMPORARY (both targets, but it exists for the Amiga, which writes no log): how far the
 * save key got, in a 16-byte CHALDBG.BIN beside the binary. Remove once saving a position
 * is proven on the m68k target. */
#define CHAL_DBG_KEY    1   /* the key reached the handler */
#define CHAL_DBG_GATE   2   /* refused: a=human sides, b=game state */
#define CHAL_DBG_HOLD   3   /* refused: the per-press hold-off */
#define CHAL_DBG_MEM    4   /* out of memory: a=bytes wanted */
#define CHAL_DBG_PTRS   5   /* a=pointers in the state with no identity */
#define CHAL_DBG_WRITE  6   /* the file would not write: a=slot */
#define CHAL_DBG_OK     7   /* written: a=slot, b=bytes */

static void chal_dbg(uint8_t stage, uint32_t a, uint32_t b)
{
    uint8_t d[16];
    d[0] = 'C'; d[1] = 'D'; d[2] = 'B'; d[3] = 'G';
    d[4] = stage;
    d[5] = (uint8_t)(_playGame != 0);
    d[6] = (uint8_t)human_sides();
    d[7] = (uint8_t)(_gameState & 0xFF);
    BE_WRITE_U32(d + 8, a);
    BE_WRITE_U32(d + 12, b);
    swos_io_save((const uint8_t *)"CHALDBG.BIN", d, sizeof d);
}

/* The first CHALnn slot with no file in it, so a session can keep several positions. */
static int32_t next_free_slot(void)
{
    uint8_t name[12];
    int32_t slot;
    for (slot = 1; slot < 100; slot++) {
        uint8_t  *probe = NULL;
        uint32_t  probeLen = 0;
        name_for_slot(slot, name);
        if (swos_io_load_alloc(name, &probe, &probeLen) != 0)
            break;
        swos_io_free(probe);
    }
    return slot < 100 ? slot : 99;              /* all taken: overwrite the last */
}

/* The encode and the write, for a caller that has already passed the gate and picked
 * the name. Both saving paths -- the in-match key and the generator -- end here. */
static int32_t save_position_as(const uint8_t *name)
{
    uint8_t  *buf;
    uint8_t  *p;
    uint32_t  stateLen, total;
    int32_t   unresolved;

    stateLen = SimState_PortableSize(SIM_TIER_SIM);
    total = CHAL_HEADER_BYTES + (uint32_t)SETUP_BYTES + stateLen;
    buf = (uint8_t *)swos_io_alloc(total);
    if (!buf) {
        CHAL_LOG("no memory for a %u-byte position\n", (unsigned)total);
        chal_dbg(CHAL_DBG_MEM, total, 0);
        return 1;
    }
    memset(buf, 0, total);

    /* The state first: if a pointer in it has no identity the file would be unreadable
     * anywhere, so nothing is written at all. */
    unresolved = SimState_SavePortable(buf + CHAL_HEADER_BYTES + SETUP_BYTES, SIM_TIER_SIM);
    if (unresolved != 0) {
        CHAL_LOG("not saved: %d pointers in the match state have no identity\n", unresolved);
        /* a = how many had no identity, b = the KIND of the last one (SIM_PTR_*) in the
         * top byte and its value in the rest -- enough to name the class responsible. */
        chal_dbg(CHAL_DBG_PTRS, (uint32_t)unresolved,
                 (SimState_LastUnidentifiedKind() << 28) |
                 (SimState_LastUnidentifiedPtr() & 0x0FFFFFFFu));
        swos_io_free(buf);
        return 1;
    }
    setup_write(buf + CHAL_HEADER_BYTES);

    p = buf;
    BE_WRITE_U32(p +  0, CHAL_MAGIC);
    BE_WRITE_U16(p +  4, CHAL_VERSION);
    BE_WRITE_U16(p +  6, 0);
    BE_WRITE_U32(p +  8, (uint32_t)(SimState_LayoutHash() >> 32));
    BE_WRITE_U32(p + 12, (uint32_t)SimState_LayoutHash());
    BE_WRITE_U32(p + 16, (uint32_t)(SimState_Hash(SIM_TIER_SIM) >> 32));
    BE_WRITE_U32(p + 20, (uint32_t)SimState_Hash(SIM_TIER_SIM));
    BE_WRITE_U32(p + 24, (uint32_t)_frameCount);
    BE_WRITE_U32(p + 28, (uint32_t)SETUP_BYTES);
    BE_WRITE_U32(p + 32, stateLen);
    BE_WRITE_U16(p + 36, (uint16_t)_sim_draw_width);
    BE_WRITE_U16(p + 38, _pitchNumberAndType);
    p[40] = rules_here();
    p[41] = (uint8_t)(_team1Computer ? 2 : 1);      /* the side that is NOT the computer */
    BE_WRITE_U16(p + 42, clock_minute());
    BE_WRITE_U16(p + 44, _team1TotalGoals);
    BE_WRITE_U16(p + 46, _team2TotalGoals);
    memcpy(p + 48, _topTeamInGame.teamName, 16);
    memcpy(p + 64, _bottomTeamInGame.teamName, 16);
    {
        const char *stamp = Build_Id();
        uint32_t n = 0;
        while (stamp && stamp[n] && n < 16) n++;
        memcpy(p + 80, stamp ? stamp : "", n);
    }

    if (swos_io_save(name, buf, total) != 0) {
        CHAL_LOG("could not write %s\n", (const char *)name);
        chal_dbg(CHAL_DBG_WRITE, 0, total);
        swos_io_free(buf);
        return 1;
    }
    swos_io_free(buf);
    chal_dbg(CHAL_DBG_OK, 0, total);

    CHAL_LOG("saved %s: frame %u, minute %u, %.16s %u-%u %.16s, %u bytes\n",
             (const char *)name, (unsigned)_frameCount, (unsigned)clock_minute(),
             (const char *)_topTeamInGame.teamName, (unsigned)_team1TotalGoals,
             (unsigned)_team2TotalGoals, (const char *)_bottomTeamInGame.teamName,
             (unsigned)total);
    return 0;
}

int32_t Challenge_SaveNow(void)
{
    uint8_t name[12];

    chal_dbg(CHAL_DBG_KEY, 0, 0);

    if (!Challenge_CanSave()) {
        chal_dbg(CHAL_DBG_GATE, (uint32_t)human_sides(), _gameState);
        return 1;
    }
    /* One file per press, not one per frame the key is held (Challenge_TickNotice runs it
     * down from the match loop). The same shape as _spaceReplayTimer, which holds the
     * SPACE highlight grab off for 0x64 frames. */
    if (s_noticeFrames > 0) {
        chal_dbg(CHAL_DBG_HOLD, (unsigned)s_noticeFrames, 0);
        return 1;
    }

    name_for_slot(next_free_slot(), name);
    if (save_position_as(name) != 0)
        return 1;

    /* Tell the player, on the pitch, for a second and a half. */
    {
        const char *src = "STATE SAVED  ";
        uint32_t i;
        for (i = 0; i < 11 && src[i]; i++) s_notice[i] = src[i];
        s_notice[11] = name[4];
        s_notice[12] = name[5];
        s_notice[13] = 0;
        s_noticeFrames = 75;
    }
    return 0;
}

int32_t Challenge_SaveAs(const uint8_t *name)
{
    uint8_t slotName[12];

    if (!Challenge_CanSave()) {
        CHAL_LOG("not saved: there is no position to take here\n");
        return 1;
    }
    if (!name || !name[0]) {
        name_for_slot(next_free_slot(), slotName);
        name = slotName;
    }
    return save_position_as(name);
}

#ifdef CHAL_AUTOPROBE
/* TEMPORARY, built only with -DCHAL_AUTOPROBE (Makefile.amiga's EXTRA_CFLAGS).
 *
 * The m68k target has no log and no command line, so diagnosing a save there meant asking
 * a person to play a match and press a key for every question. This probes the state from
 * inside ANY match the moment one has run for a while -- the attract demo starts one with
 * no input at all -- and writes what SimState_SavePortable made of it to CHALDBG.BIN.
 * Delete with the rest of the chal_dbg scaffolding. */
void Challenge_AutoProbe(void)
{
    static int32_t s_done;
    uint8_t  *buf;
    uint32_t  stateLen;
    int32_t   unresolved;

    if (s_done || !_playGame || _frameCount < 400)
        return;
    s_done = 1;

    stateLen = SimState_PortableSize(SIM_TIER_SIM);
    buf = (uint8_t *)swos_io_alloc(stateLen);
    if (!buf) {
        chal_dbg(CHAL_DBG_MEM, stateLen, 0);
        return;
    }
    SimState_ClearUnidentified();
    unresolved = SimState_SavePortable(buf, SIM_TIER_SIM);
    swos_io_free(buf);

    if (unresolved == 0) {
        chal_dbg(CHAL_DBG_OK, 0, stateLen);
        return;
    }
    /* a = how many had no identity; b = the first one's kind (top byte) and value. */
    chal_dbg(CHAL_DBG_PTRS, (uint32_t)unresolved,
             (SimState_LastUnidentifiedKind() << 28) |
             (SimState_LastUnidentifiedPtr() & 0x0FFFFFFFu));
    /* And the tally per kind, so one run names every class that failed. */
    {
        uint8_t d[32];
        uint32_t k;
        d[0] = 'C'; d[1] = 'K'; d[2] = 'N'; d[3] = 'D';
        for (k = 0; k < SIM_PTR_KINDS && k < 14; k++)
            BE_WRITE_U16(d + 4 + k * 2, (uint16_t)SimState_UnidentifiedOfKind(k));
        swos_io_save((const uint8_t *)"CHALKIND.BIN", d, sizeof d);
    }
}
#endif

/* TEMPORARY: the pitch as this match actually has it -- the pitch number/type and a
 * sample of the tile-slot map -- so a loaded position can be diffed against a normal
 * match. Written at the same point in both (frame 400), to PITCH.BIN. */
/* TEMPORARY: every SetPitchTypeAndNumber, and whether the pin was in force for it. */
static uint16_t s_pitchCalls, s_pitchRolled, s_pitchPinned, s_pitchArmed;
void Challenge_NotePitch(uint16_t rolled, uint16_t pinned, int32_t armed)
{
    s_pitchCalls++;
    s_pitchRolled = rolled;
    s_pitchPinned = pinned;
    s_pitchArmed  = (uint16_t)armed;
}

void Challenge_DumpPitch(void)
{
    extern uint8_t  _pitchDatBuffer[];
    extern uint16_t _pitchNumberAndType;
    static int32_t s_done;
    uint8_t d[8 + 64 * 4];
    uint32_t i;

    if (s_done || !_playGame || _frameCount < 400)
        return;
    s_done = 1;
    d[0]='P'; d[1]='T'; d[2]='C'; d[3]='H';
    BE_WRITE_U16(d + 4, _pitchNumberAndType);
    BE_WRITE_U16(d + 6, s_pitchCalls);
    /* reuse the tail of the sample for the pin trace */

    /* 64 slots from the middle of the map, where the half-way line and boxes live. */
    for (i = 0; i < 64; i++) {
        uint32_t v;
        memcpy(&v, _pitchDatBuffer + (1000 + i) * 4, 4);
        BE_WRITE_U32(d + 8 + i * 4, v - AnimatedPatterns_TileBase());
    }
    BE_WRITE_U16(d + 8 + 60 * 4, s_pitchRolled);
    BE_WRITE_U16(d + 8 + 61 * 4, s_pitchPinned);
    BE_WRITE_U16(d + 8 + 62 * 4, s_pitchArmed);
    swos_io_save((const uint8_t *)"PITCH.BIN", d, sizeof d);
}

/* TEMPORARY: the camera numbers around a loaded position's pitch refresh. */
void Challenge_DebugCamera(int16_t tx, int16_t ty, int16_t fromX, int16_t camX, int16_t oldCamX)
{
    uint8_t d[16];
    d[0]='C'; d[1]='A'; d[2]='M'; d[3]='!';
    BE_WRITE_U16(d + 4,  (uint16_t)tx);
    BE_WRITE_U16(d + 6,  (uint16_t)ty);
    BE_WRITE_U16(d + 8,  (uint16_t)fromX);
    BE_WRITE_U16(d + 10, (uint16_t)camX);
    BE_WRITE_U16(d + 12, (uint16_t)oldCamX);
    BE_WRITE_U16(d + 14, 0);
    swos_io_save((const uint8_t *)"CHALCAM.BIN", d, sizeof d);
}

int32_t Challenge_NoticeFrames(void) { return s_noticeFrames; }
void    Challenge_TickNotice(void)   { if (s_noticeFrames > 0) s_noticeFrames--; }
const char *Challenge_NoticeText(void) { return s_notice; }

/* ---- loading ---- */

static void forget(void)
{
    if (s_file)
        swos_io_free(s_file);
    s_file = NULL;
    s_fileLen = 0;
    s_setup = s_state = NULL;
    s_setupLen = s_stateLen = 0;
    s_armed = 0;
    s_stateLoaded = 0;
}

static char s_wanted[32];

void Challenge_Request(const char *name)
{
    uint32_t i = 0;
    if (!name) { s_wanted[0] = 0; return; }
    while (name[i] && i < sizeof s_wanted - 1) { s_wanted[i] = name[i]; i++; }
    s_wanted[i] = 0;
}

const char *Challenge_Requested(void)
{
    return s_wanted[0] ? s_wanted : NULL;
}

/* TEMPORARY: does every registry region's field layout tile its struct exactly on THIS
 * target? m68k aligns differently from x86-64, so a layout that is right on the PC can be
 * wrong here -- and the portable decoder then writes fields at the wrong offsets, scribbling
 * past the region. Writes LAYOUT.BIN: the count of bad regions and the first few indices. */
static void layout_check(void)
{
    uint8_t d[32];
    uint32_t bad = 0, i, n = 0;

    memset(d, 0, sizeof d);
    d[0]='L'; d[1]='A'; d[2]='Y'; d[3]='O';
    for (i = 0; i < (uint32_t)g_simStateRegionCount; i++) {
        if (SimState_LayoutError((int32_t)i) != (const char *)0) {
            bad++;
            if (n < 10) { BE_WRITE_U16(d + 8 + n * 2, (uint16_t)i); n++; }
        }
    }
    BE_WRITE_U16(d + 4, (uint16_t)bad);
    BE_WRITE_U16(d + 6, (uint16_t)g_simStateRegionCount);
    swos_io_save((const uint8_t *)"LAYOUT.BIN", d, sizeof d);
}

void Challenge_EnsureSample(void)
{
    static const uint8_t sampleName[] = "SAMPLE.CHL";
#ifdef SWOS_SDL_BUILD
    const char *source = "data/CHALSDL.DAT";
#else
    const char *source = "data/CHALAMI.DAT";
#endif
    uint8_t *buf = NULL;
    uint32_t len = 0;
    uint64_t layout;

    if (swos_io_load_alloc(sampleName, &buf, &len) == 0) {
        swos_io_free(buf);
        return;
    }
    if (swos_io_load_data_alloc(source, &buf, &len) != 0)
        return;
    /* Refuse a stale sample if the state registry or gameplay rules change.
     * The regular loader validates the complete format when it is selected. */
    if (len >= CHAL_HEADER_BYTES && BE_READ_U32(buf) == CHAL_MAGIC &&
        BE_READ_U16(buf + 4) == CHAL_VERSION) {
        layout = ((uint64_t)BE_READ_U32(buf + 8) << 32) | BE_READ_U32(buf + 12);
        if (layout == SimState_LayoutHash() && buf[40] == rules_here())
            (void)swos_io_save(sampleName, buf, len);
    }
    swos_io_free(buf);
}

int32_t Challenge_Arm(const uint8_t *name)
{
    layout_check();
    uint8_t  *buf = NULL;
    uint32_t  len = 0;
    uint64_t  layout;
    uint32_t  setupLen, stateLen;
    uint8_t   rules;

    forget();

    if (swos_io_load_alloc(name, &buf, &len) != 0) {
        CHAL_LOG("%s: cannot be read\n", (const char *)name);
        return 1;
    }
    if (len < CHAL_HEADER_BYTES || BE_READ_U32(buf) != CHAL_MAGIC) {
        CHAL_LOG("%s: not a save state\n", (const char *)name);
        swos_io_free(buf);
        return 1;
    }
    if (BE_READ_U16(buf + 4) != CHAL_VERSION) {
        CHAL_LOG("%s: written by a later version (%u, this build reads %u)\n",
                 (const char *)name, (unsigned)BE_READ_U16(buf + 4), CHAL_VERSION);
        swos_io_free(buf);
        return 1;
    }
    setupLen = BE_READ_U32(buf + 28);
    stateLen = BE_READ_U32(buf + 32);
    if ((uint64_t)CHAL_HEADER_BYTES + setupLen + stateLen != len) {
        CHAL_LOG("%s: its parts do not add up (%u + %u + %u against %u bytes)\n",
                 (const char *)name, CHAL_HEADER_BYTES, (unsigned)setupLen,
                 (unsigned)stateLen, (unsigned)len);
        swos_io_free(buf);
        return 1;
    }
    /* The registry layout is the first thing to check: a build whose match state is laid
     * out differently does not mean the same thing by these bytes. */
    layout = ((uint64_t)BE_READ_U32(buf + 8) << 32) | BE_READ_U32(buf + 12);
    if (layout != SimState_LayoutHash()) {
        CHAL_LOG("%s: REFUSED -- written by a build whose match state is laid out "
                 "differently (%08x%08x, this build %08x%08x)\n", (const char *)name,
                 (unsigned)BE_READ_U32(buf + 8), (unsigned)BE_READ_U32(buf + 12),
                 (unsigned)(SimState_LayoutHash() >> 32), (unsigned)SimState_LayoutHash());
        swos_io_free(buf);
        return 1;
    }
    if (stateLen != SimState_PortableSize(SIM_TIER_SIM)) {
        CHAL_LOG("%s: REFUSED -- %u bytes of state, this build has %u\n",
                 (const char *)name, (unsigned)stateLen,
                 (unsigned)SimState_PortableSize(SIM_TIER_SIM));
        swos_io_free(buf);
        return 1;
    }
    if (setup_check(buf + CHAL_HEADER_BYTES, setupLen) != 0) {
        CHAL_LOG("%s: REFUSED -- its setup block is not one this build reads\n",
                 (const char *)name);
        swos_io_free(buf);
        return 1;
    }
    /* The rules it was played under. Finishing a position under other rules is not
     * finishing the same match, so this is a refusal and not a warning. */
    rules = buf[40];
    if (rules != rules_here()) {
        CHAL_LOG("%s: REFUSED -- saved under other rules (0x%02x, this build plays "
                 "0x%02x: DOS gameplay %s, DOS view %s, no cards %s)\n",
                 (const char *)name, (unsigned)rules, (unsigned)rules_here(),
                 (rules & CHAL_RULE_DOS_GAMEPLAY) ? "on" : "off",
                 (rules & CHAL_RULE_DOS_VIEW) ? "on" : "off",
                 (rules & CHAL_RULE_NO_CARDS) ? "on" : "off");
        swos_io_free(buf);
        return 1;
    }

    s_file = buf;
    s_fileLen = len;
    s_setup = buf + CHAL_HEADER_BYTES;
    s_setupLen = setupLen;
    s_state = s_setup + setupLen;
    s_stateLen = stateLen;

    memset(&s_info, 0, sizeof s_info);
    s_info.frame     = BE_READ_U32(buf + 24);
    s_info.simWidth  = BE_READ_U16(buf + 36);
    s_info.pitch     = BE_READ_U16(buf + 38);
    s_info.rules     = rules;
    s_info.humanSide = buf[41];
    s_info.minute    = BE_READ_U16(buf + 42);
    s_info.goals1    = BE_READ_U16(buf + 44);
    s_info.goals2    = BE_READ_U16(buf + 46);
    memcpy(s_info.team1, buf + 48, 16);
    memcpy(s_info.team2, buf + 64, 16);
    memcpy(s_info.build, buf + 80, 16);
    s_armed = 1;
    s_stateLoaded = 0;

    CHAL_LOG("%s: %.16s %u-%u %.16s, minute %u, frame %u, player %u, pitch %04x, "
             "simulated %u wide, build %.16s\n", (const char *)name, s_info.team1,
             (unsigned)s_info.goals1, (unsigned)s_info.goals2, s_info.team2,
             (unsigned)s_info.minute, (unsigned)s_info.frame, (unsigned)s_info.humanSide,
             (unsigned)s_info.pitch, (unsigned)s_info.simWidth, s_info.build);
    return 0;
}

int32_t Challenge_Armed(void) { return s_armed; }

const ChallengeInfo *Challenge_ArmedInfo(void) { return s_armed ? &s_info : NULL; }

void Challenge_ApplySetup(void)
{
    if (!s_armed)
        return;
    setup_apply(s_setup);
}

int32_t Challenge_PitchOverride(uint16_t *out)
{
    if (!s_armed)
        return 0;
    if (out)
        *out = s_info.pitch;
    return 1;
}

int32_t Challenge_LoadState(void)
{
    uint8_t *mine = NULL;
    uint32_t size;
    uint64_t got;

    if (!s_armed || s_stateLoaded)
        return 0;
    s_stateLoaded = 1;
#ifdef CHAL_NO_STATE
    /* A/B BUILD: set the match up from the file but do NOT load the position. If the
     * pitch and the menus are still corrupted, the damage comes from entering a match
     * this way, not from the state. */
    return 0;
#endif

    /* The init has just built a kick-off. Whatever of the setup it cleared on the way --
     * the cup rules, _isCareer -- goes back now. */
    setup_apply(s_setup);

    /* This display's own fields are kept: the match is the file's, but where the HUD sits
     * is placed for the screen in front of us (sim_state.c, SimState_RestoreLocal). */
    size = SimState_Size(SIM_TIER_SIM);
    mine = (uint8_t *)swos_io_alloc(size);
    if (mine)
        SimState_Save(mine, SIM_TIER_SIM);

    /* EXPERIMENT (2026-10-23): keep THIS run's pitch tile slots for the crowd rows instead
     * of the file's. Everything else about a loaded position is right on the Amiga -- ball,
     * players, clock, names -- while the pitch draws wrong, and these two ranges are the
     * only pitch data the state carries. Putting this run's back after the load leaves the
     * file, the layout and the hash untouched, so it isolates them without a format change. */
    {
        extern uint8_t _pitchDatBuffer[];
        /* STATIC, not on the stack: 1008 bytes of locals inside the match loop is a
         * lot to ask of an AmigaDOS stack, and overflowing it corrupts whatever lies
         * below -- which shows up as garbled graphics and a wrecked menu afterwards,
         * nowhere near this code. */
        static uint8_t keepTop[126 * 4], keepBottom[126 * 4];
        int32_t rc;

        memcpy(keepTop,    _pitchDatBuffer + 42 * 4,   sizeof keepTop);
        memcpy(keepBottom, _pitchDatBuffer + 2142 * 4, sizeof keepBottom);

        rc = SimState_LoadPortable(s_state, SIM_TIER_SIM);
        if (rc != 0) {
            CHAL_LOG("the position names something this build cannot resolve -- "
                     "the match stays at its kick-off\n");
            if (mine)
                swos_io_free(mine);
            return 0;
        }
        memcpy(_pitchDatBuffer + 42 * 4,   keepTop,    sizeof keepTop);
        memcpy(_pitchDatBuffer + 2142 * 4, keepBottom, sizeof keepBottom);
    }
#ifdef CHAL_KEEP_REGIONS
    /* A/B BISECT: put named regions back from the pre-load snapshot, so the position is
     * restored except for those. Names come from the registry (src/sim_state.c). */
    if (mine) {
        static const char *const keep[] = { CHAL_KEEP_REGIONS, (const char *)0 };
        uint32_t off = 0;
        int32_t i, k;
        for (i = 0; i < g_simStateRegionCount; i++) {
            const SimStateRegion *r = &g_simStateRegions[i];
            if (!SimState_RegionActive(i, SIM_TIER_SIM)) continue;
            for (k = 0; keep[k]; k++) {
                const char *a = keep[k], *b = r->name;
                while (*a && *a == *b) { a++; b++; }
                if (*a == 0 && *b == 0) { memcpy(r->addr, mine + off, r->size); break; }
            }
            off += r->size;
        }
    }
#endif
    if (mine) {
        SimState_RestoreLocal(mine, SIM_TIER_SIM);
        swos_io_free(mine);
    }

    /* SELF-CHECK: re-encode what we just loaded and compare with the file, PER REGION.
     * Not just the first differing byte: the clock regions (_currentGameTick and friends)
     * are stepped by the VBlank interrupt between the load and this comparison, so they
     * always differ and would hide everything after them. RTRIP.BIN gets how many regions
     * differ and the first few indices. */
    {
        uint8_t *again = (uint8_t *)swos_io_alloc(s_stateLen);
        uint8_t d[32];
        uint32_t i, off = 0, ndiff = 0, listed = 0;

        memset(d, 0, sizeof d);
        d[0]='R'; d[1]='T'; d[2]='R'; d[3]='P';
        if (again) {
            SimState_SavePortable(again, SIM_TIER_SIM);
            for (i = 0; i < (uint32_t)g_simStateRegionCount; i++) {
                uint32_t sz, k;
                if (!SimState_RegionActive((int32_t)i, SIM_TIER_SIM)) continue;
                sz = SimState_RegionPortableSize((int32_t)i);
                for (k = 0; k < sz; k++) {
                    if (again[off + k] != s_state[off + k]) {
                        ndiff++;
                        if (listed < 10) { BE_WRITE_U16(d + 10 + listed * 2, (uint16_t)i); listed++; }
                        break;
                    }
                }
                off += sz;
            }
            swos_io_free(again);
        }
        BE_WRITE_U16(d + 4, (uint16_t)ndiff);
        BE_WRITE_U16(d + 6, (uint16_t)g_simStateRegionCount);
        BE_WRITE_U16(d + 8, (uint16_t)listed);
        swos_io_save((const uint8_t *)"RTRIP.BIN", d, sizeof d);
    }

    /* Prove it end to end: the state this build now holds must hash to what the file says
     * it would. A mismatch means the bytes decoded into something else here, which is
     * worth a loud line even though the match is playable. */
    got = SimState_Hash(SIM_TIER_SIM);
    {
        uint64_t want = ((uint64_t)BE_READ_U32(s_file + 16) << 32) | BE_READ_U32(s_file + 20);
        if (got != want)
            CHAL_LOG("WARNING: loaded state hashes %08x%08x, the file says %08x%08x\n",
                     (unsigned)(got >> 32), (unsigned)got,
                     (unsigned)(want >> 32), (unsigned)want);
        else
            CHAL_LOG("position loaded: frame %u, minute %u, %u-%u, state hash %08x%08x\n",
                     (unsigned)s_info.frame, (unsigned)s_info.minute,
                     (unsigned)s_info.goals1, (unsigned)s_info.goals2,
                     (unsigned)(got >> 32), (unsigned)got);
    }

    /* The scorer BANNER: its tables, widths and sprite structs were just restored, but
     * the pixels live in sprite-graphics slots the registry deliberately leaves alone,
     * so the banner of a loaded position drew whatever the slots last held. Redraw them
     * from the tables (src/goal_scored.c). */
    {
        extern void RebuildScorerSprites(void);
        RebuildScorerSprites();
    }
    /* The CLOCK is the same shape: _gameTime was just restored, but the digits' pixels
     * are only redrawn when _copyTimeSprite says so -- without this the first frames
     * showed whatever minute the sprite slot last held, until the next minute tick. */
    {
        extern uint16_t _copyTimeSprite;
        _copyTimeSprite = 1;
    }
    return 1;
}

void Challenge_Done(void)
{
    forget();
}

void Challenge_Play(void)
{
    if (!s_armed)
        return;
    /* The simulation width the position was played at. A match is simulated at a width,
     * and the state's sprite and camera coordinates belong to that one. */
#ifdef SWOS_SDL_BUILD
    if (s_info.simWidth >= SCREEN_WIDTH && s_info.simWidth <= PITCH_VIEW_MAX_WIDTH)
        _sim_draw_width = (int32_t)s_info.simWidth;
#endif
    Challenge_ApplySetup();
    /* The menu sprites the STADIUM LINE-UP draws, extracted ONLY when the front end was
     * skipped -- that is, for --challenge=<file> on the command line, which never ran it
     * (the same reason --srp-resim does this, src/srp_resim.c). From the CHALLENGES menu
     * the front end has already filled that sprite pool, and running the stream over it
     * again corrupts it: the match drew wrong pitch tiles, misplaced crowd rows and
     * inverted colours behind the bench, and the MAIN MENU was wrecked after the match
     * returned -- which is what gave it away, since no amount of match state could do
     * that. */
#ifdef SWOS_SDL_BUILD
    if (Challenge_Requested()) {
        extern uint16_t _menuBGImageCmdStream[];
        extern uint8_t *_menuImageStreamBaseEnd;
        extern void     ProcessImageCommandStream(uint16_t *script, uint8_t *dest);
        ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    }
#endif
    InitTeams();            /* as the friendly menu does before a match */
    {   /* keep the menus' own setup globals; put back when the match ends (below) */
        int32_t i;
        for (i = 0; i < SCALAR_COUNT && i < 64; i++) {
            const ChalScalar *s = &s_scalars[i];
            s_menuScalars[i] = (s->width == 1) ? *(const uint8_t *)s->addr
                                               : *(const uint16_t *)s->addr;
        }
    }
    CHAL_LOG("starting: %.16s v %.16s, controls %u/%u, player %u\n",
             (const char *)_selectedTeams[0].team.teamName,
             (const char *)_selectedTeams[1].team.teamName,
             (unsigned)_selectedTeams[0].team.teamControls,
             (unsigned)_selectedTeams[1].team.teamControls, (unsigned)s_info.humanSide);
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);
    CHAL_LOG("the match is over\n");
    {
        /* The menus get their own globals back. The setup this challenge applied also
         * overwrites what the MENUS live by -- _gameType, _isCareer, the cup rules --
         * and a challenge is an excursion, not a career move. Without this the main menu
         * came back showing the career CONTINUE/SAVE rows, blank, because CommonMenuExit
         * believed a competition was under way. */
        int32_t i;
        for (i = 0; i < SCALAR_COUNT; i++) {
            const ChalScalar *s = &s_scalars[i];
            if (s->width == 1) *(uint8_t *)s->addr  = (uint8_t)s_menuScalars[i];
            else               *(uint16_t *)s->addr = s_menuScalars[i];
        }
    }
    Challenge_Done();
}

/* ---- the self-test ---- */

int32_t Challenge_SelfTest(void)
{
#ifdef SWOS_SDL_BUILD
    uint8_t *buf = (uint8_t *)swos_io_alloc((uint32_t)SETUP_BYTES);
    int32_t  i, failed = 0;
    uint8_t *saved = (uint8_t *)swos_io_alloc((uint32_t)SETUP_BYTES);

    printf("[challenge] self-test: the setup survives a round trip\n");
    if (!buf || !saved) {
        printf("  FAIL out of memory\n");
        return 1;
    }

    /* Something recognisable in every scalar, so a field that does not make the trip
     * shows up as itself rather than as whatever this process happened to hold. */
    for (i = 0; i < SCALAR_COUNT; i++) {
        const ChalScalar *s = &s_scalars[i];
        uint16_t v = (uint16_t)(0x0101u * (uint16_t)(i + 1));
        if (s->width == 1) *(uint8_t *)s->addr  = (uint8_t)v;
        else               *(uint16_t *)s->addr = v;
    }
    memcpy(&_selectedTeams[0].team.teamName, "ROUNDTRIP ONE", 13);
    memcpy(&_selectedTeams[1].team.teamName, "ROUNDTRIP TWO", 13);
    _selectedTeams[0].team.globalTeamNumber = 0x1234;
    _selectedTeams[1].team.globalTeamNumber = 0x5678;
    memset(tactic_slot(0), 0xA5, TACTIC_BYTES);
    memset(tactic_slot(NUM_TACTICS - 1), 0x5A, TACTIC_BYTES);

    setup_write(buf);
    memcpy(saved, buf, (uint32_t)SETUP_BYTES);
    printf("  ok   %d scalars, 2 teams, %d tactics -> %d bytes\n",
           (int)SCALAR_COUNT, (int)NUM_TACTICS, (int)SETUP_BYTES);

    /* Scribble over everything it covers, then read it back. */
    for (i = 0; i < SCALAR_COUNT; i++) {
        const ChalScalar *s = &s_scalars[i];
        if (s->width == 1) *(uint8_t *)s->addr  = 0xEE;
        else               *(uint16_t *)s->addr = 0xEEEE;
    }
    memset(&_selectedTeams[0], 0xEE, sizeof(TeamFileEx));
    memset(&_selectedTeams[1], 0xEE, sizeof(TeamFileEx));
    for (i = 0; i < NUM_TACTICS; i++)
        memset(tactic_slot(i), 0xEE, TACTIC_BYTES);

    if (setup_check(buf, (uint32_t)SETUP_BYTES) != 0) {
        printf("  FAIL its own block does not check out\n");
        failed = 1;
    }
    setup_apply(buf);

    /* Writing it again must produce the same bytes: anything the reader dropped shows up
     * here, because the global it left alone now holds 0xEE. */
    setup_write(buf);
    if (memcmp(buf, saved, (uint32_t)SETUP_BYTES) != 0) {
        uint32_t off = 0;
        while (off < (uint32_t)SETUP_BYTES && buf[off] == saved[off]) off++;
        printf("  FAIL byte %u of the setup did not survive", (unsigned)off);
        if (off >= 4 && off < 4 + (uint32_t)SCALAR_COUNT * 2)
            printf(" -- scalar %s", s_scalars[(off - 4) / 2].name);
        else if (off >= 4 + (uint32_t)SCALAR_COUNT * 2)
            printf(" -- in the teams or the tactics");
        printf("\n");
        failed = 1;
    } else {
        printf("  ok   every scalar, both teams and all %d tactics came back\n",
               (int)NUM_TACTICS);
    }

    /* And the state's own encoding, which is what the rollback rig proves in anger: here
     * just that this build can write it and read it back to the same hash. */
    {
        uint32_t n = SimState_PortableSize(SIM_TIER_SIM);
        uint8_t *st = (uint8_t *)swos_io_alloc(n);
        if (!st) {
            printf("  FAIL out of memory for %u bytes of state\n", (unsigned)n);
            failed = 1;
        } else {
            uint64_t before, after;
            int unresolved = SimState_SavePortable(st, SIM_TIER_SIM);
            before = SimState_Hash(SIM_TIER_SIM);
            if (unresolved != 0) {
                printf("  FAIL %d pointers in the match state have no identity\n", unresolved);
                failed = 1;
            }
            if (SimState_LoadPortable(st, SIM_TIER_SIM) != 0) {
                printf("  FAIL the state would not load back\n");
                failed = 1;
            }
            after = SimState_Hash(SIM_TIER_SIM);
            if (before != after) {
                printf("  FAIL the state changed across a round trip (%08x%08x -> %08x%08x)\n",
                       (unsigned)(before >> 32), (unsigned)before,
                       (unsigned)(after >> 32), (unsigned)after);
                failed = 1;
            } else {
                printf("  ok   %u bytes of match state, hash %08x%08x both ways\n",
                       (unsigned)n, (unsigned)(before >> 32), (unsigned)before);
            }
            swos_io_free(st);
        }
    }

    swos_io_free(buf);
    swos_io_free(saved);
    printf("[challenge] self-test %s\n", failed ? "FAILED" : "passed");
    return failed;
#else
    return 0;
#endif
}
