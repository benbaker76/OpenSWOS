/*
 * netplay_setup.c -- the match setup one peer sends the other before kick-off.
 *
 * THE SETUP, NOT THE STATE. It would be possible to start a match on both peers
 * and then copy the host's whole kick-off state across (state_wire.c can do
 * that, and the resync path does). This sends the much smaller thing the state
 * is DERIVED from -- the two teams, the user tactics, the match options and the
 * random seed -- and lets both peers run the same deterministic init over it.
 *
 * That is exactly how the corpus two-peer test already gets two identical
 * machines: both load the same recording, both run applySetup() and
 * InitAndPlayGame(), and every frame agrees. Copying post-init state instead
 * would mean each peer first built a state of its own for the other to
 * overwrite, and would leave the two ways of starting a match (recording, live)
 * not sharing the code that makes them agree.
 *
 * BUT THE INIT DOES NOT WRITE EVERYTHING. Whatever it leaves alone -- the game
 * state, the stoppage timers, where the ball and players stood, the bench and crowd
 * -- is what the machine last had: after a match that ended out of step (or at
 * different frames on the two machines), a rematch started out of step at frame 0
 * (2026-09-19). So the lobby's START also carries the host's state as it was when it
 * pressed START (a state_wire packet, SIM tier), and BOTH peers load it before this
 * setup and the init run (NetplayLobby_ApplyMatch): the same state through the same
 * steps, whatever either did before. The setup still does the work; the state only
 * makes sure the init starts from one place.
 *
 * ~3.6 KB: two TeamFileEx at 684 bytes, six tactics at 370, and the scalars.
 * p2p_link chunks it (include/p2p_link.h, "the match setup").
 *
 * ENDIANNESS AND BITNESS. The scalars are big-endian through endian_helpers,
 * like every other wire format here. The team and tactics records go across as
 * bytes, which is correct because of what they are, not by luck:
 *
 *   - no pointer fields, packed, and size-asserted (TeamFileEx 684,
 *     TeamTactics 370), so a 32-bit peer and a 64-bit one lay them out
 *     identically -- the assert fails the build if that ever changes;
 *   - PlayerFile is entirely single bytes, and so is TeamTactics, so neither
 *     has anything to swap;
 *   - TeamFile has exactly ONE multi-byte field, int16_t globalTeamNumber at
 *     offset 2, and that one is written and read in wire order below.
 *
 * Netplay is SDL-only and every SDL target is little-endian (the Amiga, the one
 * big-endian target, is excluded by SDL_ONLY_SRCS), so a mixed-endian session
 * cannot arise today -- but the one field costs nothing to get right, and a
 * blanket "it is all bytes" would be wrong the first time a field grows.
 */
#include "netplay_setup.h"
#include "pitch.h"            /* the crowd (version 4) */

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endian_helpers.h"
#include "game_types.h"

#ifdef SWOS_NETPLAY

/* ---- the globals a match is started from ---- */
extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
extern uint16_t   _numSelectedTeams;

extern uint8_t _teamTacticsUSERA[], _teamTacticsUSERB[], _teamTacticsUSERC[];
extern uint8_t _teamTacticsUSERD[], _teamTacticsUSERE[], _teamTacticsUSERF[];

extern uint16_t _pitchTypeOrSeason, _friendlyPitchType, _friendlySeason, _pitchType;
extern uint16_t _gameLength, _autoReplays, _autoSaveHighlights;
extern uint16_t _allPlayerTeamsEqual, _minSubstitutes, _maxSubstitutes;
extern uint16_t _gameType, _trainingGame, _isCareer, _exitGameFlag, _twoPlayers;
extern uint16_t _pl1Coach, _pl2Coach;
extern uint16_t _topTeamPlayerNo, _topTeamCoachNo;
extern uint16_t _bottomTeamPlayerNo, _bottomTeamCoachNo;
extern int16_t  _pl1Tactics, _pl2Tactics;
extern uint16_t _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;

/* The primary and secondary generators. They are in the rollback registry
 * (src/sim_state.c) so they roll back correctly once a match is running -- but
 * the value they START from still has to be the same on both peers, and nothing
 * else in this blob would make it so. */
extern uint8_t _seed, _randXorIndex, _randXorKey;
extern uint8_t _seed2, _randXorIndex2, _randXorKey2;

/* What the generators are RE-seeded from during match init, and it is not a
 * generator's state, so the registry does not carry it either. Randomize2 XORs its
 * argument with _randomSeed, and ApplyTeamTactics reseeds per player from
 * GetPlayerRandomNum, which mixes in _newRandomSeedCopy and _currentSeasonShowing --
 * so every player's skill roll depends on them. All three are taken from the tick
 * counter when the front end starts (InitCareerVariables), i.e. a different number on
 * every machine. Two peers that came through the menus kicked off with different
 * skills and desynced on frame 1; the live test, which skips the front end, never
 * saw it because both of its processes still had the defaults. _viewRandomPhase feeds
 * CalcTeamViewOffset the same way. ApplyTeamTactics also runs mid-match, on a
 * substitution, so these must match for the whole match, not just at kick-off. */
extern uint16_t _randomSeed, _currentSeasonShowing;
extern uint8_t  _newRandomSeedCopy, _viewRandomPhase;

/* How a draw is settled: the lobby's cup rules (LobbyRules, netplay_lobby.h). The
 * friendly init clears all six (InitAndPlayGame), so Apply keeps a copy and the init
 * puts it back (NetplaySetup_TakeTieRules) -- once, for the match it was armed for. */
extern uint16_t _extraTimeState, _penaltiesState, _secondLeg, _playing2ndGame;
extern uint16_t _team1GoalsFirstLeg, _team2GoalsFirstLeg;

#define TACTIC_BYTES  370
#define NUM_TACTICS   6

#define SETUP_MAGIC   0x53555020u    /* "SUP " */
#define SETUP_VERSION 4     /* 2: the match-init seeds (_randomSeed and friends); 3: the tie rules;
                             * 4: the host's crowd (include/pitch.h) */

/* magic, version, then the scalars, then (version 3) the tie rules, then the teams,
 * then the tactics. A version-2 setup -- a recording made before the cup rules --
 * has no tie rules and is played as the friendly it was. */
#define SCALAR_COUNT  27
#define TIE_WORDS     6
#define HEADER_BYTES  (4 + 2 + 2)
#define SCALAR_BYTES  (SCALAR_COUNT * 2 + TIE_WORDS * 2)
#define TEAM_BYTES    ((uint32_t)sizeof(TeamFileEx) * 2)
#define TACTIC_TOTAL  (TACTIC_BYTES * NUM_TACTICS)
/* (version 4) the count of pitch files, then each one's crowd cells as the host's game
 * loads them: what both games play the crowd animation on (include/pitch.h). */
#define CROWD_BYTES   (2u + PITCH_CROWD_FILES * PITCH_CROWD_CELLS * 2u)

uint32_t NetplaySetup_MaxBytes(void)
{
    return HEADER_BYTES + SCALAR_BYTES + TEAM_BYTES + TACTIC_TOTAL + CROWD_BYTES + 8;
}

/* The bytes a setup of `version` holds. */
static uint32_t setup_bytes(uint16_t version)
{
    return HEADER_BYTES + SCALAR_COUNT * 2u + (version >= 3 ? TIE_WORDS * 2u : 0u) +
           TEAM_BYTES + TACTIC_TOTAL + (version >= 4 ? CROWD_BYTES : 0u);
}

/* The crowd of the last setup applied, and whether the next pitch loaded takes it. */
static uint16_t s_crowd[PITCH_CROWD_FILES][PITCH_CROWD_CELLS];
static int32_t  s_haveCrowd, s_crowdArmed;

/* TeamFileEx is packed and size-asserted at 684 bytes with no pointer fields, so a
 * 32-bit peer and a 64-bit one lay it out identically and it can go across as bytes.
 *
 * ENDIANNESS: PlayerFile (608 of the 684) is entirely single bytes, and so is the
 * rest of TeamFile -- except ONE field. `int16_t globalTeamNumber` at offset 2 is
 * the only multi-byte value in the whole record, so it is the only thing that needs
 * putting in wire order. */
void NetplaySetup_WriteTeam(uint8_t *out, const TeamFileEx *team)
{
    memcpy(out, team, sizeof(TeamFileEx));
    BE_WRITE_U16(out + 2, (uint16_t)team->team.globalTeamNumber);
}

void NetplaySetup_ReadTeam(TeamFileEx *team, const uint8_t *in)
{
    memcpy(team, in, sizeof(TeamFileEx));
    team->team.globalTeamNumber = (int16_t)BE_READ_U16(in + 2);
}

static uint8_t *tactic_slot(int32_t i)
{
    switch (i) {
    case 0: return _teamTacticsUSERA;
    case 1: return _teamTacticsUSERB;
    case 2: return _teamTacticsUSERC;
    case 3: return _teamTacticsUSERD;
    case 4: return _teamTacticsUSERE;
    default: return _teamTacticsUSERF;
    }
}

int32_t NetplaySetup_Encode(uint8_t *out, uint32_t cap)
{
    uint8_t *p = out;
    int32_t i;

    if (!out || cap < NetplaySetup_MaxBytes())
        return -1;

    BE_WRITE_U32(p, SETUP_MAGIC);            p += 4;
    BE_WRITE_U16(p, SETUP_VERSION);          p += 2;
    BE_WRITE_U16(p, (uint16_t)SCALAR_COUNT); p += 2;

    /* The order here and in Apply must match, so they are written once each,
     * in one place, rather than as a struct whose padding would differ. */
#define PUT(v) do { BE_WRITE_U16(p, (uint16_t)(v)); p += 2; } while (0)
    PUT(_pitchTypeOrSeason);
    PUT(_friendlyPitchType);
    PUT(_friendlySeason);
    PUT(_pitchType);
    PUT(_gameLength);
    PUT(_autoReplays);
    PUT(_autoSaveHighlights);
    PUT(_allPlayerTeamsEqual);
    PUT(_minSubstitutes);
    PUT(_maxSubstitutes);
    PUT(_gameType);
    PUT(_trainingGame);
    PUT(_isCareer);
    PUT(_twoPlayers);
    PUT(_pl1Coach);
    PUT(_pl2Coach);
    PUT(_topTeamPlayerNo);
    PUT(_topTeamCoachNo);
    PUT(_bottomTeamPlayerNo);
    PUT(_bottomTeamCoachNo);
    PUT((uint16_t)_pl1Tactics);
    PUT((uint16_t)_pl2Tactics);
    /* Both generators, two bytes at a time: seed/xorIndex then xorKey/seed2 and
     * xorIndex2/xorKey2 -- six bytes in three words. */
    PUT(((uint16_t)_seed << 8) | _randXorIndex);
    PUT(((uint16_t)_randXorKey << 8) | _seed2);
#undef PUT
    /* The last two generator bytes ride in a word of their own so SCALAR_COUNT
     * stays the count of words above. */
    BE_WRITE_U16(p, ((uint16_t)_randXorIndex2 << 8) | _randXorKey2); p += 2;
    BE_WRITE_U16(p, _randomSeed);                                      p += 2;
    BE_WRITE_U16(p, _currentSeasonShowing);                            p += 2;
    BE_WRITE_U16(p, ((uint16_t)_newRandomSeedCopy << 8) | _viewRandomPhase); p += 2;

    BE_WRITE_U16(p, _extraTimeState);     p += 2;
    BE_WRITE_U16(p, _penaltiesState);     p += 2;
    BE_WRITE_U16(p, _secondLeg);          p += 2;
    BE_WRITE_U16(p, _playing2ndGame);     p += 2;
    BE_WRITE_U16(p, _team1GoalsFirstLeg); p += 2;
    BE_WRITE_U16(p, _team2GoalsFirstLeg); p += 2;

    /* The two teams (NetplaySetup_WriteTeam says why bytes are right here). */
    NetplaySetup_WriteTeam(p, &_selectedTeams[0]);
    p += sizeof(TeamFileEx);
    NetplaySetup_WriteTeam(p, &_selectedTeams[1]);
    p += sizeof(TeamFileEx);

    for (i = 0; i < NUM_TACTICS; i++) {
        memcpy(p, tactic_slot(i), TACTIC_BYTES);
        p += TACTIC_BYTES;
    }

    /* The crowd: this game's, for every pitch file the match might be played on (which one
     * is chosen at the match's init, from its seeds). A file this game cannot read leaves
     * its cells KEEP: each game plays its own there, as before. */
    BE_WRITE_U16(p, PITCH_CROWD_FILES); p += 2;
    for (i = 0; i < PITCH_CROWD_FILES; i++) {
        uint16_t cells[PITCH_CROWD_CELLS];
        int32_t c, ok = Pitch_CrowdLayout((uint16_t)i, cells);
        for (c = 0; c < PITCH_CROWD_CELLS; c++, p += 2)
            BE_WRITE_U16(p, ok ? cells[c] : (uint16_t)PITCH_CROWD_KEEP);
    }

    return (int32_t)(p - out);
}

/* The last setup applied's tie rules, and whether the next match init takes them. */
static uint16_t s_tie[TIE_WORDS];
static int32_t  s_tieArmed;

static void put_tie_rules(void)
{
    _extraTimeState     = s_tie[0];
    _penaltiesState     = s_tie[1];
    _secondLeg          = s_tie[2];
    _playing2ndGame     = s_tie[3];
    _team1GoalsFirstLeg = s_tie[4];
    _team2GoalsFirstLeg = s_tie[5];
}

void NetplaySetup_ArmTieRules(int32_t armed)
{
    s_tieArmed = armed != 0;
    s_crowdArmed = armed != 0 && s_haveCrowd;   /* (the crowd goes with them: one match) */
}

const uint16_t *NetplaySetup_TakeCrowd(uint16_t mapIndex)
{
    if (!s_crowdArmed || mapIndex >= PITCH_CROWD_FILES)
        return NULL;
    s_crowdArmed = 0;
    return s_crowd[mapIndex];
}

void NetplaySetup_TakeTieRules(void)
{
    if (!s_tieArmed)
        return;
    s_tieArmed = 0;
    put_tie_rules();
    if (s_tie[0] || s_tie[1] || s_tie[2])
        fprintf(stderr, "[match] cup rules: extra time %s, penalties %s%s\n",
                s_tie[0] ? "yes" : "no", s_tie[1] ? "yes" : "no",
                s_tie[2] ? ", second leg" : "");
    if (s_tie[2])
        fprintf(stderr, "[match] the first leg ended %u-%u\n", (unsigned)s_tie[4], (unsigned)s_tie[5]);
}

int32_t NetplaySetup_Apply(const uint8_t *in, uint32_t len)
{
    const uint8_t *p = in;
    uint16_t version, scalars;
    int32_t i;

    if (!in || len < HEADER_BYTES)
        return 1;
    if (BE_READ_U32(p) != SETUP_MAGIC)
        return 1;
    p += 4;
    version = BE_READ_U16(p); p += 2;
    scalars = BE_READ_U16(p); p += 2;
    if (version < 2 || version > SETUP_VERSION || scalars != SCALAR_COUNT)
        return 1;
    if (len < setup_bytes(version))
        return 1;

#define GET() (p += 2, BE_READ_U16(p - 2))
    _pitchTypeOrSeason   = GET();
    _friendlyPitchType   = GET();
    _friendlySeason      = GET();
    _pitchType           = GET();
    _gameLength          = GET();
    _autoReplays         = GET();
    _autoSaveHighlights  = GET();
    _allPlayerTeamsEqual = GET();
    _minSubstitutes      = GET();
    _maxSubstitutes      = GET();
    _gameType            = GET();
    _trainingGame        = GET();
    _isCareer            = GET();
    _twoPlayers          = GET();
    _pl1Coach            = GET();
    _pl2Coach            = GET();
    _topTeamPlayerNo     = GET();
    _topTeamCoachNo      = GET();
    _bottomTeamPlayerNo  = GET();
    _bottomTeamCoachNo   = GET();
    _pl1Tactics          = (int16_t)GET();
    _pl2Tactics          = (int16_t)GET();
    {
        uint16_t w0 = GET(), w1 = GET(), w2;
        w2 = BE_READ_U16(p); p += 2;
        _seed          = (uint8_t)(w0 >> 8);
        _randXorIndex  = (uint8_t)(w0 & 0xFF);
        _randXorKey    = (uint8_t)(w1 >> 8);
        _seed2         = (uint8_t)(w1 & 0xFF);
        _randXorIndex2 = (uint8_t)(w2 >> 8);
        _randXorKey2   = (uint8_t)(w2 & 0xFF);
    }
    _randomSeed           = BE_READ_U16(p); p += 2;
    _currentSeasonShowing = BE_READ_U16(p); p += 2;
    _newRandomSeedCopy    = p[0];
    _viewRandomPhase      = p[1];           p += 2;
    memset(s_tie, 0, sizeof s_tie);
    if (version >= 3) {
        for (i = 0; i < TIE_WORDS; i++)
            s_tie[i] = GET();
    }
    put_tie_rules();
#undef GET

    NetplaySetup_ReadTeam(&_selectedTeams[0], p);
    p += sizeof(TeamFileEx);
    NetplaySetup_ReadTeam(&_selectedTeams[1], p);
    p += sizeof(TeamFileEx);
    if (_numSelectedTeams < 2)
        _numSelectedTeams = 2;

    for (i = 0; i < NUM_TACTICS; i++) {
        memcpy(tactic_slot(i), p, TACTIC_BYTES);
        p += TACTIC_BYTES;
    }

    /* (version 4) the host's crowd; before it, each game played its own. */
    s_haveCrowd = 0;
    if (version >= 4 && BE_READ_U16(p) == PITCH_CROWD_FILES) {
        int32_t f, c;
        p += 2;
        for (f = 0; f < PITCH_CROWD_FILES; f++)
            for (c = 0; c < PITCH_CROWD_CELLS; c++, p += 2)
                s_crowd[f][c] = BE_READ_U16(p);
        s_haveCrowd = 1;
    }

    /* Not sent, because they are the same on both sides by definition. */
    _exitGameFlag = 0;
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;
    return 0;
}

/* Encode the live setup, scribble over every global it covers, apply it back,
 * and require what came back to equal what went out. The scribble is the point:
 * a field that Encode writes but Apply forgets to read would otherwise keep its
 * original value and the round trip would pass while the joiner silently played
 * a different match. */
int32_t NetplaySetup_SelfTest(void)
{
    uint8_t *buf = NULL;
    int32_t n, failed = 0;
    uint16_t savedLen, savedPitch, savedSubs;
    int16_t savedTactics;
    uint8_t savedSeed, savedKey2, savedSeedCopy;
    uint16_t savedRandomSeed;
    TeamFileEx savedTeam0;
    uint8_t savedTacticA[TACTIC_BYTES];

    printf("[netplay-setup] self-test: the setup survives a round trip\n");

    buf = (uint8_t *)malloc(NetplaySetup_MaxBytes());
    if (!buf) {
        printf("  FAIL out of memory\n");
        return 1;
    }

    /* Something recognisable to send. */
    _gameLength = 2; _pitchType = 3; _maxSubstitutes = 5; _pl1Tactics = 4;
    _seed = 0xA5; _randXorKey2 = 0x3C; _randomSeed = 0xBEEF; _newRandomSeedCopy = 0x5D;
    _penaltiesState = 1; _secondLeg = 1; _team2GoalsFirstLeg = 3;
    memset(&_selectedTeams[0].team.teamName, 'Q', 4);
    memset(_teamTacticsUSERA, 0x5A, 16);

    savedLen = _gameLength; savedPitch = _pitchType; savedSubs = _maxSubstitutes;
    savedTactics = _pl1Tactics; savedSeed = _seed; savedKey2 = _randXorKey2;
    savedRandomSeed = _randomSeed; savedSeedCopy = _newRandomSeedCopy;
    savedTeam0 = _selectedTeams[0];
    memcpy(savedTacticA, _teamTacticsUSERA, TACTIC_BYTES);

    n = NetplaySetup_Encode(buf, NetplaySetup_MaxBytes());
    if (n <= 0) {
        printf("  FAIL encode returned %d\n", n);
        free(buf);
        return 1;
    }
    printf("  ok   encoded %d bytes\n", n);

    /* Now make every one of them wrong. */
    _gameLength = 0; _pitchType = 0; _maxSubstitutes = 0; _pl1Tactics = 0;
    _seed = 0; _randXorKey2 = 0; _randomSeed = 0; _newRandomSeedCopy = 0;
    _penaltiesState = 0; _secondLeg = 0; _team2GoalsFirstLeg = 0;
    memset(&_selectedTeams[0], 0, sizeof _selectedTeams[0]);
    memset(_teamTacticsUSERA, 0, TACTIC_BYTES);

    if (NetplaySetup_Apply(buf, (uint32_t)n) != 0) {
        printf("  FAIL apply refused its own packet\n");
        free(buf);
        return 1;
    }

#define CHECK(cond, what) do { \
        printf("  %-4s %s\n", (cond) ? "ok" : "FAIL", what); \
        if (!(cond)) failed++; \
    } while (0)
    CHECK(_gameLength == savedLen,            "game length");
    CHECK(_pitchType == savedPitch,           "pitch type");
    CHECK(_maxSubstitutes == savedSubs,       "substitutes");
    CHECK(_pl1Tactics == savedTactics,        "player 1 tactics");
    CHECK(_seed == savedSeed,                 "primary random seed");
    CHECK(_randXorKey2 == savedKey2,          "secondary random key");
    CHECK(_randomSeed == savedRandomSeed,     "the seed match init reseeds from");
    CHECK(_newRandomSeedCopy == savedSeedCopy, "the seed player skill rolls mix in");
    CHECK(_penaltiesState == 1 && _secondLeg == 1 && _team2GoalsFirstLeg == 3, "the tie rules");
    CHECK(memcmp(&_selectedTeams[0], &savedTeam0, sizeof savedTeam0) == 0, "team 1, every byte");
    CHECK(memcmp(_teamTacticsUSERA, savedTacticA, TACTIC_BYTES) == 0,      "user tactics A");
#undef CHECK

    /* A packet this build cannot read must be refused, not half-applied. */
    buf[5] ^= 0xFF;     /* the version word */
    printf("  %-4s %s\n", NetplaySetup_Apply(buf, (uint32_t)n) != 0 ? "ok" : "FAIL",
           "a wrong version is refused");
    if (NetplaySetup_Apply(buf, (uint32_t)n) == 0)
        failed++;

    free(buf);
    printf("[netplay-setup] self-test %s\n", failed ? "FAILED" : "passed");
    return failed != 0;
}

#else  /* !SWOS_NETPLAY */

uint32_t NetplaySetup_MaxBytes(void) { return 0; }
void     NetplaySetup_WriteTeam(uint8_t *out, const TeamFileEx *team) { (void)out; (void)team; }
void     NetplaySetup_ReadTeam(TeamFileEx *team, const uint8_t *in) { (void)team; (void)in; }
int32_t  NetplaySetup_Encode(uint8_t *out, uint32_t cap) { (void)out; (void)cap; return -1; }
int32_t  NetplaySetup_Apply(const uint8_t *in, uint32_t len) { (void)in; (void)len; return 1; }
int32_t  NetplaySetup_SelfTest(void) { return 0; }
void     NetplaySetup_ArmTieRules(int32_t armed) { (void)armed; }
void     NetplaySetup_TakeTieRules(void) {}
const uint16_t *NetplaySetup_TakeCrowd(uint16_t mapIndex) { (void)mapIndex; return NULL; }

#endif /* SWOS_NETPLAY */
#endif /* SWOS_SDL_BUILD */
