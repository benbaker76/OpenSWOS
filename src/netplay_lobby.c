/*
 * netplay_lobby.c -- where two connected players set their match up.
 * include/netplay_lobby.h has the model; docs/NETPLAY.md, "The lobby", the story.
 *
 * THREE MESSAGES, each on a p2p_link channel of its own (include/p2p_link.h):
 *
 *   STATE   each player's own side: team, tactic, ready, name. The host's also
 *           carries the match rules and their generation; the joiner's echoes the
 *           rules generation it has seen. Re-sent on every change; the link keeps
 *           only the newest.
 *   START   host -> joiner: the whole match setup (netplay_setup.c), built from
 *           both sides, plus WHICH of the joiner's STATE messages it was built from.
 *   ANSWER  joiner -> host: yes or no to that START.
 *
 * WHY START HAS AN ANSWER. A START is built from the joiner's side as the host last
 * saw it, and the joiner may have changed it since -- un-readied, picked another
 * team -- with the change still in flight. So the joiner accepts a START only if it
 * names the joiner's newest STATE, and says no otherwise; the host goes back to the
 * lobby and says why. Without the answer the host would kick off a match the joiner
 * never agreed to, and the two would desync on frame 0 at best.
 *
 * WHEN EACH SIDE KICKS OFF. The host when the ANSWER arrives; the joiner when the
 * link reports its ANSWER delivered. Both then close the link, and the joiner's BYE
 * cannot beat the ANSWER to the host, because delivery means the host already has
 * it. Messages stay readable after a link fails, so the lobby reads them before it
 * looks at the link state.
 *
 * READY GOES DOWN when what you agreed to changes: your own team or tactic, the
 * OTHER player's team or tactic, or (for the joiner) the host's rules. A match only
 * starts from two ready flags set after the last change either of them could see.
 *
 * THE USER TACTICS. Six slots, USER A-F, shared by both teams. Each player's USER
 * tactic travels with their STATE, and the match puts player 1's in USER A and
 * player 2's in USER B -- SWOS 2020's convention -- so two players who both use
 * their own "USER A" do not overwrite each other. Both machines keep a copy of
 * their six slots from before the match and put it back afterwards
 * (NetplayLobby_RestoreTactics): playing online never changes a player's library.
 */
#include "netplay_lobby.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endian_helpers.h"
#include "netplay_match.h"
#include "netplay_setup.h"
#include "p2p_link.h"
#include "sim_state.h"
#include "state_wire.h"
#include "replay_v2.h"     /* ReplayRec_SetSetup: the resumable recording */
#include "custom_team.h"   /* CustomTeam_Allowed: what a team from the other player may be */
#include "team_proto.h"    /* the server's word on both teams */
#include "test_rules.h"     /* the host's CARDS/INJURIES switch */

#include <SDL.h>                        /* SDL_GetPerformanceCounter: key entropy fallback */
#include <time.h>

#ifdef SWOS_NETPLAY

extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
extern uint16_t   _numSelectedTeams;
extern uint8_t    _teamTacticsUSERA[], _teamTacticsUSERB[], _teamTacticsUSERC[];
extern uint8_t    _teamTacticsUSERD[], _teamTacticsUSERE[], _teamTacticsUSERF[];
extern uint16_t   _gameLength, _minSubstitutes, _maxSubstitutes;
extern uint16_t   _pitchTypeOrSeason, _friendlyPitchType, _pitchType;
extern uint16_t   _gameType, _trainingGame, _isCareer;
extern uint16_t   _extraTimeState, _penaltiesState, _secondLeg, _playing2ndGame;
extern uint16_t   _team1GoalsFirstLeg, _team2GoalsFirstLeg;

enum { CH_STATE = 0, CH_START = 1, CH_ANSWER = 2 };

enum {
    TACTIC_BYTES   = 370,
    TEAM_PLAYER_CONTROLLED = 2,         /* TEAM_PLAYER_COACH (data/SWOS2_IDA.h TeamControls) */
    RANDOM_PITCH   = 0xFFFF,
};

#define STATE_MAGIC   0x53554C42u       /* "SULB" */
#define START_MAGIC   0x53555354u       /* "SUST" */
#define ANSWER_MAGIC  0x5355414Eu       /* "SUAN" */
#define LOBBY_VERSION 5     /* 2: START carries the match's packet key; 3: and the kick-off state;
                               4: the cup rules; 5: the host's switches */

/* STATE: magic, version, player, ready, activity, hasTeam, tactic, rulesGen, the ten
 * rules, the name, the team, the USER tactic. */
enum { RULE_WORDS = 10 };
enum { STATE_BYTES = 4 + 2 + 4 + 2 + 2 + RULE_WORDS * 2 + LOBBY_NAME_MAX + NETPLAY_TEAM_BYTES +
                     TACTIC_BYTES };
/* START: magic, version, the joiner STATE generation it was built on, rulesGen, the
 * packet key; then the setup's length and the setup; then the host's state to kick off
 * from (a state_wire packet, the rest of the message -- see NetplayLobby_ApplyMatch). */
enum { KEY_BYTES = 16, START_HEADER = 4 + 2 + 2 + 2 + KEY_BYTES };
/* ANSWER: magic, version, the START generation, accepted, pad. */
enum { ANSWER_BYTES = 4 + 2 + 2 + 1 + 1 };

static struct {
    LobbyState  state;
    int32_t     me;                     /* 1 host, 2 joiner */
    LobbyPlayer players[2];
    LobbyRules  rules;
    uint16_t    rulesGen;               /* host: bumped per change. joiner: the host's, as seen */
    uint16_t    peerRulesSeen;          /* host: what the joiner last echoed */
    int32_t     dirty;                  /* our STATE needs sending */

    uint16_t    peerStateGen;           /* the peer's STATE we last applied */
    uint16_t    startGen;               /* host: the START on offer */
    uint16_t    startHandled;           /* joiner: newest START answered */
    uint16_t    answerHandled;          /* host: newest ANSWER read */
    int32_t     accepted;               /* joiner: said yes to the START on offer */

    char        peerIp[24];
    uint16_t    peerPort, localPort;
    int32_t     delay;
    int32_t     simWidth;               /* the simulation draw width both agreed */

    int32_t     tacticsSaved;
    uint8_t     savedTactics[6 * TACTIC_BYTES];

    uint8_t    *setup;                  /* the agreed match setup, both sides (ApplyMatch) */
    uint32_t    setupLen;
    uint8_t    *kickoff;                /* the host's state it was offered from, both sides */
    uint32_t    kickoffLen;
    uint8_t     packetKey[KEY_BYTES];   /* ...and the key its GGPO packets are checksummed with */

    char        notice[64];
} s_lobby;

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

static LobbyPlayer *mine(void)   { return &s_lobby.players[s_lobby.me - 1]; }
static LobbyPlayer *theirs(void) { return &s_lobby.players[2 - s_lobby.me]; }

static void set_notice(const char *text)
{
    snprintf(s_lobby.notice, sizeof s_lobby.notice, "%s", text ? text : "");
}

static void clamp_rules(LobbyRules *r)
{
    r->switches &= NETPLAY_SW_ALL;      /* (an older host's CATCH UP / BALANCE: gone) */
    if (r->gameLength > 3)
        r->gameLength = 0;
    if (r->pitchType != RANDOM_PITCH && r->pitchType > 6)
        r->pitchType = RANDOM_PITCH;
    if (r->maxSubs > 5)
        r->maxSubs = 5;
    if (r->maxSubs == 0)
        r->minSubs = 0;
    else if (r->minSubs < 1 || r->minSubs > r->maxSubs)
        r->minSubs = 1;
    if (r->legs > 2)
        r->legs = 0;
    if (r->legs == 0)
        r->tieBreak = 0;
    else if (r->tieBreak != LOBBY_TIE_ET_PEN && r->tieBreak != LOBBY_TIE_PEN_ONLY)
        r->tieBreak = LOBBY_TIE_ET_PEN;
    if (r->legs != 2 || r->leg != 2) {
        r->leg = 1;
        r->firstLeg1 = r->firstLeg2 = 0;
    }
    if (r->firstLeg1 > 99)
        r->firstLeg1 = 99;
    if (r->firstLeg2 > 99)
        r->firstLeg2 = 99;
    r->switches &= NETPLAY_SW_ALL;
#ifndef SWOS_TEST_RULES
    /* A release has the no-cards rule compiled out: as the host it always plays with them.
     * As the joiner it keeps what the host sent -- forcing it on here would play a tester
     * host's no-cards match by other rules, a desync at the first booking -- and
     * rules_playable() keeps it from readying up instead. */
    if (s_lobby.me != 2)
        r->switches |= NETPLAY_SW_CARDS_INJURIES;
#endif
}

/* Whether this build can play the host's rules: a release cannot play without cards and
 * injuries (a tester build's host switch, test_rules.h). */
#define NOT_IN_YOUR_BUILD "HOST PLAYS WITHOUT CARDS: NOT IN YOUR BUILD"
static int32_t rules_playable(const LobbyRules *r)
{
#ifdef SWOS_TEST_RULES
    (void)r;
    return 1;
#else
    return (r->switches & NETPLAY_SW_CARDS_INJURIES) != 0;
#endif
}

/* ---- the STATE message ---- */

static int32_t encode_state(uint8_t *out)
{
    LobbyPlayer *p = mine();
    uint8_t *q = out;

    /* The USER tactic is read out of its slot now, not when it was chosen: the slot
     * is the player's own and may have been edited since. */
    if (p->tactic >= TACTIC_USER_A)
        memcpy(&p->userTactic, tactic_slot(p->tactic - TACTIC_USER_A), TACTIC_BYTES);

    BE_WRITE_U32(q, STATE_MAGIC);                 q += 4;
    BE_WRITE_U16(q, LOBBY_VERSION);               q += 2;
    q[0] = (uint8_t)s_lobby.me;
    q[1] = (uint8_t)(p->ready ? 1 : 0);
    q[2] = (uint8_t)p->activity;
    q[3] = (uint8_t)(p->hasTeam ? 1 : 0);         q += 4;
    BE_WRITE_U16(q, (uint16_t)p->tactic);         q += 2;
    BE_WRITE_U16(q, s_lobby.rulesGen);            q += 2;
    BE_WRITE_U16(q, s_lobby.rules.gameLength);    q += 2;
    BE_WRITE_U16(q, s_lobby.rules.pitchType);     q += 2;
    BE_WRITE_U16(q, s_lobby.rules.minSubs);       q += 2;
    BE_WRITE_U16(q, s_lobby.rules.maxSubs);       q += 2;
    BE_WRITE_U16(q, s_lobby.rules.legs);          q += 2;
    BE_WRITE_U16(q, s_lobby.rules.tieBreak);      q += 2;
    BE_WRITE_U16(q, s_lobby.rules.leg);           q += 2;
    BE_WRITE_U16(q, s_lobby.rules.firstLeg1);     q += 2;
    BE_WRITE_U16(q, s_lobby.rules.firstLeg2);     q += 2;
    BE_WRITE_U16(q, s_lobby.rules.switches);      q += 2;
    memset(q, 0, LOBBY_NAME_MAX);
    memcpy(q, p->name, strnlen(p->name, LOBBY_NAME_MAX)); q += LOBBY_NAME_MAX;
    NetplaySetup_WriteTeam(q, &p->team);          q += NETPLAY_TEAM_BYTES;
    memcpy(q, &p->userTactic, TACTIC_BYTES);      q += TACTIC_BYTES;
    return (int32_t)(q - out);
}

static void send_state(void)
{
    uint8_t buf[STATE_BYTES];
    int32_t n = encode_state(buf);
    if (P2PLink_Send(CH_STATE, buf, (uint32_t)n) == 0)
        s_lobby.dirty = 0;
}

/* The other player's side is what they will play; ours stops being "agreed" when it
 * changes in any way that matters to the match. */
static int32_t side_differs(const LobbyPlayer *a, const LobbyPlayer *b)
{
    if (a->hasTeam != b->hasTeam || a->tactic != b->tactic)
        return 1;
    if (a->hasTeam && memcmp(&a->team, &b->team, sizeof a->team) != 0)
        return 1;
    if (a->tactic >= TACTIC_USER_A && memcmp(&a->userTactic, &b->userTactic, TACTIC_BYTES) != 0)
        return 1;
    return 0;
}

static void team_check(void);      /* the matchmaking server's word on both teams */
static int32_t team_blocks(const LobbyPlayer *p);

static void drop_my_ready(void)
{
    if (mine()->ready) {
        mine()->ready = 0;
        s_lobby.dirty = 1;
    }
}

static void read_peer_state(void)
{
    uint32_t len = 0;
    uint16_t gen = 0;
    const uint8_t *m = P2PLink_Received(CH_STATE, &len, &gen);
    const uint8_t *q;
    LobbyPlayer got;
    LobbyRules rules;
    uint16_t rulesGen;

    if (!m || gen == s_lobby.peerStateGen)
        return;
    s_lobby.peerStateGen = gen;
    if (len < (uint32_t)STATE_BYTES || BE_READ_U32(m) != STATE_MAGIC ||
        BE_READ_U16(m + 4) != LOBBY_VERSION || m[6] != (uint8_t)(3 - s_lobby.me))
        return;                                 /* not the other player's, in our format */

    memset(&got, 0, sizeof got);
    q = m + 6;
    got.known    = 1;
    got.ready    = q[1] ? 1 : 0;
    got.activity = q[2];
    got.hasTeam  = q[3] ? 1 : 0;                  q += 4;
    got.tactic   = BE_READ_U16(q);                q += 2;
    rulesGen     = BE_READ_U16(q);                q += 2;
    rules.gameLength = BE_READ_U16(q);            q += 2;
    rules.pitchType  = BE_READ_U16(q);            q += 2;
    rules.minSubs    = BE_READ_U16(q);            q += 2;
    rules.maxSubs    = BE_READ_U16(q);            q += 2;
    rules.legs       = BE_READ_U16(q);            q += 2;
    rules.tieBreak   = BE_READ_U16(q);            q += 2;
    rules.leg        = BE_READ_U16(q);            q += 2;
    rules.firstLeg1  = BE_READ_U16(q);            q += 2;
    rules.firstLeg2  = BE_READ_U16(q);            q += 2;
    rules.switches   = BE_READ_U16(q);            q += 2;
    memcpy(got.name, q, LOBBY_NAME_MAX);          q += LOBBY_NAME_MAX;
    got.name[LOBBY_NAME_MAX] = '\0';
    NetplaySetup_ReadTeam(&got.team, q);          q += NETPLAY_TEAM_BYTES;
    memcpy(&got.userTactic, q, TACTIC_BYTES);
    if (got.tactic < 0 || got.tactic > LOBBY_TACTIC_MAX)
        got.tactic = 0;
    /* A team is a team of the database, with at most its names, faces, kits and line-up
     * changed (a custom team, src/custom_team.c). One with different players or skills
     * is not played: the other player has no team here until they pick one that is, so
     * a match cannot start. Their own game says the same about what it was given. */
    if (got.hasTeam && !CustomTeam_Allowed(&got.team)) {
        got.hasTeam = 0;
        got.ready = 0;
        got.refused = 1;
    }

    if (theirs()->known && side_differs(theirs(), &got))
        drop_my_ready();
    got.verified = theirs()->verified;      /* the server's word stands until the team changes (team_check) */
    *theirs() = got;

    if (s_lobby.me == 2) {
        /* The host's rules. A change after the first sighting means the joiner has
         * not agreed to these yet. */
        if (rulesGen != s_lobby.rulesGen) {
            clamp_rules(&rules);
            if (s_lobby.rulesGen != 0 && memcmp(&rules, &s_lobby.rules, sizeof rules) != 0)
                drop_my_ready();
            s_lobby.rules    = rules;
            s_lobby.rulesGen = rulesGen;
            s_lobby.dirty    = 1;               /* echo the generation we now have */
            if (!rules_playable(&rules)) {
                drop_my_ready();
                set_notice(NOT_IN_YOUR_BUILD);
                fprintf(stderr, "[lobby] the host's rules have cards and injuries off, which this "
                        "release build cannot play: not ready until the host turns them on\n");
            }
        }
    } else {
        s_lobby.peerRulesSeen = rulesGen;
    }
}

/* ---- the USER tactics a match borrows ---- */

static void save_tactics(void)
{
    int32_t i;
    if (s_lobby.tacticsSaved)
        return;
    for (i = 0; i < 6; i++)
        memcpy(s_lobby.savedTactics + i * TACTIC_BYTES, tactic_slot(i), TACTIC_BYTES);
    s_lobby.tacticsSaved = 1;
}

/* The host's switches in force on this machine for one match (ApplyMatch), and this
 * player's own SHORT PRESSES to give back afterwards. */
static int32_t s_switchesHeld, s_ownLatch;

static void apply_switches(uint16_t sw)
{
    extern int  InputSDL_PressLatch(void);
    extern void InputSDL_SetPressLatch(int on);
    if (!s_switchesHeld)
        s_ownLatch = InputSDL_PressLatch();
    s_switchesHeld = 1;
    NetplayMatch_SetHostSwitches(sw);
    InputSDL_SetPressLatch((sw & NETPLAY_SW_SHORT_PRESSES) != 0);
    /* CARDS/INJURIES off: the old tester builds' rule, for this match, on both machines,
     * from the kick-off on (test_rules.h; the recording says so -- netplay_match.c). */
    TestRules_Force((sw & NETPLAY_SW_CARDS_INJURIES) ? -1 : 1);
    fprintf(stderr, "[lobby] the host's switches: short presses %s, kick-off sync %s, cards/injuries %s\n",
            (sw & NETPLAY_SW_SHORT_PRESSES) ? "ON" : "OFF", (sw & NETPLAY_SW_KICKOFF_SYNC) ? "ON" : "OFF",
            (sw & NETPLAY_SW_CARDS_INJURIES) ? "ON" : "OFF");
}

static void release_switches(void)
{
    extern void InputSDL_SetPressLatch(int on);
    if (!s_switchesHeld)
        return;
    s_switchesHeld = 0;
    NetplayMatch_SetHostSwitches(-1);
    InputSDL_SetPressLatch(s_ownLatch);
    TestRules_Force(-1);
}

void NetplayLobby_RestoreTactics(void)
{
    int32_t i;
    release_switches();
    if (!s_lobby.tacticsSaved)
        return;
    for (i = 0; i < 6; i++)
        memcpy(tactic_slot(i), s_lobby.savedTactics + i * TACTIC_BYTES, TACTIC_BYTES);
    s_lobby.tacticsSaved = 0;
}

/* ---- START and ANSWER ---- */

/* The match's packet key (ggpo_set_packet_key): 16 bytes nobody off the path can guess,
 * so nobody off the path can make a packet the session accepts -- before this, one
 * forged packet with the disconnect bit set ended a match. It crosses the link in the
 * clear, so it is no defence against somebody who can see this traffic; but they can
 * drop packets anyway, which no checksum prevents. */
static void make_packet_key(uint8_t *key)
{
    FILE *f = fopen("/dev/urandom", "rb");      /* Linux, macOS, and Cygwin all have it */
    size_t got = f ? fread(key, 1, KEY_BYTES, f) : 0;
    if (f)
        fclose(f);
    if (got != KEY_BYTES) {
        /* No entropy source: still different on every START, just not secret. */
        uint64_t x = SDL_GetPerformanceCounter() ^ ((uint64_t)time(NULL) << 32) ^
                     (uint64_t)(uintptr_t)&x;
        for (int32_t i = 0; i < KEY_BYTES; i++) {
            x += 0x9E3779B97F4A7C15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            key[i] = (uint8_t)((z ^ (z >> 31)) >> 24);
        }
    }
}

/* Host: put both sides and the rules where a match starts from. */
static void assemble_match(void)
{
    int32_t p;

    save_tactics();
    for (p = 0; p < 2; p++) {
        const LobbyPlayer *side = &s_lobby.players[p];
        TeamFileEx team = side->team;
        int32_t tactic = side->tactic;

        if (tactic >= TACTIC_USER_A) {
            memcpy(tactic_slot(p), &side->userTactic, TACTIC_BYTES);   /* A, or B */
            tactic = TACTIC_USER_A + p;
        }
        team.team.teamControls = TEAM_PLAYER_CONTROLLED;
        team.team.tactics      = (uint8_t)tactic;
        _selectedTeams[p] = team;
    }
    /* THE SAME TEAM ON BOTH SIDES. A match finds a team by its NUMBER -- country and
     * ordinal -- whenever it wants a player (InitInGameTeamStructure -> GetPlayerAtIndex
     * -> GetTeamPointer, which takes the first selected team with that number), so two
     * copies of one team were both player 1's: his line-up, his eleven, on both sides
     * (ReportBot 0019). The game itself never meets this -- a friendly cannot pick one
     * team twice -- so the second copy is given an ordinal of its own for the match.
     * Only the number: its name, kit, players and line-up are that player's. It is in
     * the setup the host sends, so both machines play the same two teams. */
    {
        extern uint16_t _currentTeamNumber, _selTeamNumber;
        TeamFile *a = &_selectedTeams[0].team, *b = &_selectedTeams[1].team;
        if (a->countryNumber == b->countryNumber && a->teamOrdinal == b->teamOrdinal) {
            uint8_t ordinal = 0xFE;
            while (ordinal == a->teamOrdinal ||
                   PACK_BE_U16(b->countryNumber, ordinal) == _currentTeamNumber ||
                   PACK_BE_U16(b->countryNumber, ordinal) == _selTeamNumber)
                ordinal--;
            fprintf(stderr, "[lobby] both players picked %.16s: player 2's copy plays as team "
                            "%02X:%02X so the match can tell them apart\n",
                    (const char *)a->teamName, b->countryNumber, ordinal);
            b->teamOrdinal = ordinal;
        }
    }
    _numSelectedTeams = 2;

    _gameLength        = s_lobby.rules.gameLength;
    _pitchTypeOrSeason = 0;
    _friendlyPitchType = s_lobby.rules.pitchType;
    _minSubstitutes    = s_lobby.rules.minSubs;
    _maxSubstitutes    = s_lobby.rules.maxSubs;
    _gameType          = 0;                     /* a friendly */
    _trainingGame      = 0;
    _isCareer          = 0;

    /* The cup rules, as SWOS 2020 plays them: a single leg, or a second one, is
     * settled by extra time and penalties or by penalties alone; a first leg of two
     * may end in a draw. The two players keep their sides for both legs -- nobody is
     * ever the away team -- so a second leg goes to the aggregate and then straight
     * to the tie-break, with no away goals (_playing2ndGame 0; game_state.c). The
     * friendly init clears these: the setup carries them back (NetplaySetup_TakeTieRules). */
    {
        const LobbyRules *r = &s_lobby.rules;
        int32_t second = r->legs == 2 && r->leg == 2;
        int32_t decide = r->legs == 1 || second;
        _extraTimeState     = (uint16_t)(decide && r->tieBreak == LOBBY_TIE_ET_PEN);
        _penaltiesState     = (uint16_t)decide;
        _secondLeg          = (uint16_t)second;
        _playing2ndGame     = 0;
        _team1GoalsFirstLeg = second ? r->firstLeg1 : 0;
        _team2GoalsFirstLeg = second ? r->firstLeg2 : 0;
    }
}

static void keep_setup(const uint8_t *setup, uint32_t len)
{
    free(s_lobby.setup);
    s_lobby.setup = (uint8_t *)malloc(len);
    s_lobby.setupLen = s_lobby.setup ? len : 0;
    if (s_lobby.setup)
        memcpy(s_lobby.setup, setup, len);
}

static void keep_kickoff(const uint8_t *packet, uint32_t len)
{
    free(s_lobby.kickoff);
    s_lobby.kickoff = len ? (uint8_t *)malloc(len) : NULL;
    s_lobby.kickoffLen = s_lobby.kickoff ? len : 0;
    if (s_lobby.kickoff)
        memcpy(s_lobby.kickoff, packet, len);
}

static void send_answer(uint16_t startGen, int32_t accepted)
{
    uint8_t buf[ANSWER_BYTES];
    BE_WRITE_U32(buf, ANSWER_MAGIC);
    BE_WRITE_U16(buf + 4, LOBBY_VERSION);
    BE_WRITE_U16(buf + 6, startGen);
    buf[8] = (uint8_t)(accepted ? 1 : 0);
    buf[9] = 0;
    (void)P2PLink_Send(CH_ANSWER, buf, sizeof buf);
}

/* Joiner. */
static void read_start(void)
{
    uint32_t len = 0;
    uint16_t gen = 0;
    const uint8_t *m = P2PLink_Received(CH_START, &len, &gen);
    int32_t ok;

    if (!m || gen == s_lobby.startHandled)
        return;
    s_lobby.startHandled = gen;
    if (s_lobby.state != LOBBY_OPEN)
        return;

    ok = len > (uint32_t)START_HEADER + 4 &&
         BE_READ_U32(m) == START_MAGIC && BE_READ_U16(m + 4) == LOBBY_VERSION &&
         /* built on our newest side, which we have not changed since */
         BE_READ_U16(m + 6) == P2PLink_SentGeneration(CH_STATE) && !s_lobby.dirty &&
         BE_READ_U16(m + 8) == s_lobby.rulesGen &&
         mine()->ready && mine()->hasTeam &&
         /* and the server has vouched for both teams, or is not there (team_check) */
         !team_blocks(mine()) && !team_blocks(theirs());
    {
        uint32_t setupLen = ok ? BE_READ_U32(m + START_HEADER) : 0;
        const uint8_t *setup = m + START_HEADER + 4;
        const uint8_t *kick = setup + setupLen;
        uint32_t kickLen = 0;
        StateWireInfo info;

        if (ok && (setupLen == 0 || setupLen > len - START_HEADER - 4))
            ok = 0;
        if (ok) {
            kickLen = len - START_HEADER - 4 - setupLen;
            /* A kick-off state this build could not load would start the two of us
             * somewhere different; better to say no now. */
            if (kickLen && StateWire_Peek(kick, kickLen, &info) != STATE_WIRE_OK) {
                fprintf(stderr, "[lobby] the host's kick-off state does not fit this build\n");
                ok = 0;
            }
        }
        if (ok) {
            save_tactics();
            if (NetplaySetup_Apply(setup, setupLen) != 0) {
                NetplayLobby_RestoreTactics();
                ok = 0;
            } else {
                keep_setup(setup, setupLen);
                keep_kickoff(kick, kickLen);
                memcpy(s_lobby.packetKey, m + 10, KEY_BYTES);
            }
        }
    }
    send_answer(gen, ok);
    if (ok) {
        s_lobby.accepted = 1;
        s_lobby.state = LOBBY_STARTING;
        set_notice("STARTING");
    } else {
        set_notice("THE HOST STARTED BEFORE YOUR CHANGE ARRIVED");
    }
}

/* Host. */
static void read_answer(void)
{
    uint32_t len = 0;
    uint16_t gen = 0;
    const uint8_t *m = P2PLink_Received(CH_ANSWER, &len, &gen);

    if (!m || gen == s_lobby.answerHandled)
        return;
    s_lobby.answerHandled = gen;
    if (s_lobby.state != LOBBY_STARTING || len < (uint32_t)ANSWER_BYTES ||
        BE_READ_U32(m) != ANSWER_MAGIC || BE_READ_U16(m + 4) != LOBBY_VERSION ||
        BE_READ_U16(m + 6) != s_lobby.startGen)
        return;
    if (m[8]) {
        s_lobby.state = LOBBY_KICKOFF;
        set_notice("");
    } else {
        NetplayLobby_RestoreTactics();
        s_lobby.state = LOBBY_OPEN;
        set_notice("THE OTHER PLAYER CHANGED THEIR SIDE - TRY AGAIN");
    }
}

/* ---- the lobby ---- */

/* Where the match will be played: the connected peer, and our end of the link. */
static void take_peer_address(void)
{
    const char *peer = P2PLink_PeerText();
    char *colon;

    snprintf(s_lobby.peerIp, sizeof s_lobby.peerIp, "%s", peer);
    colon = strchr(s_lobby.peerIp, ':');
    if (colon) {
        *colon = '\0';
        s_lobby.peerPort = (uint16_t)atoi(colon + 1);
    }
    s_lobby.localPort = P2PLink_LocalPort();
    s_lobby.delay     = P2PLink_AgreedDelay();
    if (s_lobby.delay < 0)
        s_lobby.delay = 2;                      /* both sides fall back the same way */
    s_lobby.simWidth  = P2PLink_AgreedSimWidth();
    if (s_lobby.simWidth < 0)
        s_lobby.simWidth = 336;                 /* ditto: SCREEN_WIDTH */
}

void NetplayLobby_Begin(int32_t localPlayer, const char *localName)
{
    free(s_lobby.setup);
    free(s_lobby.kickoff);
    memset(&s_lobby, 0, sizeof s_lobby);
    s_lobby.me    = localPlayer == 2 ? 2 : 1;
    s_lobby.state = LOBBY_OPEN;
    mine()->known = 1;
    snprintf(mine()->name, sizeof mine()->name, "%s",
             (localName && localName[0]) ? localName : (s_lobby.me == 1 ? "PLAYER 1" : "PLAYER 2"));

    /* Where the match will be played. Read now, while the link is certainly up: the
     * other side closes its link as soon as it kicks off, and the BYE that follows
     * clears the peer's address. */
    take_peer_address();

    if (s_lobby.me == 1) {
        /* What the friendly menu would start from (FriendlyMenu, menus.c): the game
         * length and a fixed pitch from OPTIONS, otherwise a random pitch, and two
         * substitutes from five. */
        s_lobby.rules.gameLength = _gameLength;
        s_lobby.rules.pitchType  = _pitchType <= 6 ? _pitchType : RANDOM_PITCH;
        s_lobby.rules.minSubs    = 2;
        s_lobby.rules.maxSubs    = 5;
        /* The switches as this build plays, with the host's own SHORT PRESSES setting:
         * what the host played by before there were switches. */
        {
            extern int InputSDL_PressLatch(void);
            uint16_t sw = NETPLAY_SW_DEFAULT & ~NETPLAY_SW_SHORT_PRESSES;
            if (InputSDL_PressLatch())
                sw |= NETPLAY_SW_SHORT_PRESSES;
            s_lobby.rules.switches = sw;
        }
        clamp_rules(&s_lobby.rules);
        s_lobby.rulesGen = 1;
    }
    s_lobby.dirty = 1;
}

void NetplayLobby_End(void)
{
    s_lobby.state = LOBBY_CLOSED;
}

/* An online session is open: from here until NetplayLobby_End everything that goes
 * into a match -- the squads, who may be picked -- is the online match's
 * (test_rules.c: the testers' rule is off for all of it, not only the frames). */
int32_t NetplayLobby_IsOpen(void)
{
    return s_lobby.state != LOBBY_CLOSED;
}

int32_t NetplayLobby_Rematch(const char *notice)
{
    char addr[32];
    const char *list[1] = { addr };

    NetplayLobby_RestoreTactics();
    free(s_lobby.setup);
    s_lobby.setup    = NULL;
    s_lobby.setupLen = 0;
    keep_kickoff(NULL, 0);

    /* Message generations start again at 1 with every connection (p2p_link.h), so the
     * ones this lobby has already handled would hide the new link's first messages. */
    s_lobby.peerStateGen  = 0;
    s_lobby.startGen      = 0;
    s_lobby.startHandled  = 0;
    s_lobby.answerHandled = 0;
    s_lobby.peerRulesSeen = 0;          /* the joiner echoes the rules again */
    s_lobby.accepted      = 0;

    /* Both sides stay; neither is ready for a match nobody has agreed to yet. */
    mine()->ready      = 0;
    mine()->activity   = LOBBY_ACTIVITY_NONE;
    theirs()->ready    = 0;
    theirs()->activity = LOBBY_ACTIVITY_NONE;
    s_lobby.dirty      = 1;
    set_notice(notice);

    if (!s_lobby.peerIp[0]) {
        s_lobby.state = LOBBY_FAILED;
        set_notice("THE OTHER PLAYER'S ADDRESS WAS LOST");
        return 1;
    }
    snprintf(addr, sizeof addr, "%s:%u", s_lobby.peerIp, (unsigned)s_lobby.peerPort);
    if (P2PLink_Punch(list, 1, s_lobby.localPort) != 0) {
        s_lobby.state = LOBBY_FAILED;
        set_notice("COULD NOT RECONNECT");
        return 1;
    }
    s_lobby.state = LOBBY_RECONNECTING;
    return 0;
}

void NetplayLobby_Poll(void)
{
    LobbyState st = s_lobby.state;

    if (st == LOBBY_CLOSED || st == LOBBY_FAILED || st == LOBBY_KICKOFF)
        return;
    P2PLink_Poll();

    if (st == LOBBY_RECONNECTING) {
        if (P2PLink_State() == P2P_FAILED) {
            s_lobby.state = LOBBY_FAILED;
            set_notice("YOUR OPPONENT DID NOT COME BACK");
            return;
        }
        /* The same wait mpWaitingMenu makes before a first lobby: linked, and the
         * input delay agreed between the two. */
        if (P2PLink_State() != P2P_CONNECTED || P2PLink_AgreedDelay() < 0)
            return;
        take_peer_address();                /* a NAT may have moved the peer's port */
        s_lobby.state = LOBBY_OPEN;
    }

    /* Messages before the link state: they stay readable after the link goes. */
    read_peer_state();
    if (s_lobby.me == 2)
        read_start();
    else
        read_answer();

    if (s_lobby.state == LOBBY_STARTING && s_lobby.me == 2 && s_lobby.accepted &&
        P2PLink_Delivered(CH_ANSWER))
        s_lobby.state = LOBBY_KICKOFF;
    /* The same, when the link ends before our answer's ack arrived. The host kicks
     * off the moment it READS the answer and hangs up straight away (link_close's
     * BYE), so on a link with any latency the BYE can overtake the ack -- and the
     * joiner used to read it as "the other player left", while the host waited in
     * its session for a peer that never came. Found by the relay's network
     * emulator, the first to disturb this link (netplay_netem only touches GGPO's).
     *
     * Having said yes, a host that hangs up (or goes quiet: both BYEs lost) has
     * kicked off -- in STARTING the only other way out is quitting, and then the
     * match's own "never arrived" timeout ends it cleanly. */
    if (s_lobby.state == LOBBY_STARTING && s_lobby.me == 2 && s_lobby.accepted &&
        P2PLink_State() == P2P_FAILED &&
        (P2PLink_Failure() == P2P_FAIL_PEER_LEFT || P2PLink_Failure() == P2P_FAIL_LOST))
        s_lobby.state = LOBBY_KICKOFF;
    if (s_lobby.state == LOBBY_KICKOFF)
        return;

    if (P2PLink_State() != P2P_CONNECTED) {
        P2PLinkFailure why = P2PLink_State() == P2P_FAILED ? P2PLink_Failure() : P2PLink_Notice();
        NetplayLobby_RestoreTactics();          /* a start that was under way borrowed them */
        s_lobby.state = LOBBY_FAILED;
        set_notice(why == P2P_FAIL_PEER_LEFT ? "THE OTHER PLAYER LEFT" :
                   why == P2P_FAIL_LOST      ? "LOST THE CONNECTION" :
                                               "THE CONNECTION FAILED");
        return;
    }

    if (s_lobby.state == LOBBY_OPEN)
        team_check();
    if (s_lobby.state == LOBBY_OPEN && s_lobby.dirty)
        send_state();
}

LobbyState         NetplayLobby_State(void)       { return s_lobby.state; }
int32_t            NetplayLobby_LocalPlayer(void) { return s_lobby.me; }
int32_t            NetplayLobby_IsHost(void)      { return s_lobby.me == 1; }
const LobbyRules  *NetplayLobby_Rules(void)       { return &s_lobby.rules; }
const char        *NetplayLobby_Notice(void)      { return s_lobby.notice; }

const LobbyPlayer *NetplayLobby_Player(int32_t player)
{
    if (player != 1 && player != 2)
        return NULL;
    return &s_lobby.players[player - 1];
}

void NetplayLobby_SetTeam(const TeamFileEx *team)
{
    LobbyPlayer *p = mine();
    if (s_lobby.state != LOBBY_OPEN || !team)
        return;
    p->team    = *team;
    p->hasTeam = 1;
    /* The team's own formation, as the friendly menu would start it. A team record
     * naming a USER slot keeps it only when that slot is ours to send. */
    p->tactic  = team->team.tactics <= LOBBY_TACTIC_MAX ? team->team.tactics : 0;
    p->ready   = 0;
    s_lobby.dirty = 1;
    set_notice("");
}

void NetplayLobby_SetTactic(int32_t tactic)
{
    LobbyPlayer *p = mine();
    if (s_lobby.state != LOBBY_OPEN || tactic < 0 || tactic > LOBBY_TACTIC_MAX)
        return;
    p->tactic = tactic;
    p->ready  = 0;
    s_lobby.dirty = 1;
    set_notice("");
}

void NetplayLobby_SetReady(int32_t ready)
{
    LobbyPlayer *p = mine();
    if (s_lobby.state != LOBBY_OPEN || (ready && !p->hasTeam))
        return;
    if (ready && s_lobby.me == 2 && !rules_playable(&s_lobby.rules)) {
        set_notice(NOT_IN_YOUR_BUILD);
        return;
    }
    if (p->ready != (ready ? 1 : 0)) {
        p->ready = ready ? 1 : 0;
        s_lobby.dirty = 1;
    }
    set_notice("");
}

void NetplayLobby_SetActivity(int32_t activity)
{
    if (s_lobby.state != LOBBY_OPEN || mine()->activity == activity)
        return;
    mine()->activity = activity;
    s_lobby.dirty = 1;
}

void NetplayLobby_MatchPlayed(int32_t completed, int32_t goals1, int32_t goals2)
{
    LobbyRules *r = &s_lobby.rules;
    /* Both machines, each on its copy: they played the same match, so the joiner has
     * the host's next rules before they arrive, and a GO pressed meanwhile stands
     * (read_peer_state takes ready down only for rules that differ). */
    if (r->legs != 2 || !completed)
        return;
    if (r->leg == 1) {
        r->leg = 2;
        r->firstLeg1 = (uint16_t)(goals1 < 0 ? 0 : goals1);
        r->firstLeg2 = (uint16_t)(goals2 < 0 ? 0 : goals2);
    } else {
        r->leg = 1;
    }
    clamp_rules(r);
    if (s_lobby.me == 1)
        s_lobby.rulesGen++;
    s_lobby.dirty = 1;
    if (r->leg == 2)
        fprintf(stderr, "[lobby] cup: the second leg is next; the first ended %u-%u\n",
                (unsigned)r->firstLeg1, (unsigned)r->firstLeg2);
    else
        fprintf(stderr, "[lobby] cup: the tie is over; the next match is a first leg\n");
}

void NetplayLobby_SetRules(const LobbyRules *rules)
{
    LobbyRules r;
    if (s_lobby.state != LOBBY_OPEN || s_lobby.me != 1 || !rules)
        return;
    r = *rules;
    /* Another cup format is another tie: it starts at its first leg. */
    if (r.legs != s_lobby.rules.legs || r.tieBreak != s_lobby.rules.tieBreak)
        r.leg = 1;
    clamp_rules(&r);
    if (memcmp(&r, &s_lobby.rules, sizeof r) == 0)
        return;
    s_lobby.rules = r;
    s_lobby.rulesGen++;
    s_lobby.dirty = 1;
    set_notice("");
}

/* ---- the team check ----
 *
 * In a match the matchmaking server arranged, this game asks the server about BOTH
 * teams -- its own and the one the other player sent -- each as the id and checksum of
 * what it would actually play (CustomTeam_Claim). A team the server refuses stops the
 * match being started here, whatever the other game says of it: that is what makes a
 * doctored team the honest player's to refuse.
 *
 * One datagram, sent again every 400 ms until it is answered, eight times at most. A
 * server that never answers (an old one, or one that is down) leaves the teams
 * unverified and the match playable: the local checks still stand, and a player cannot
 * silence the OTHER game's question. */
enum { kCheckResendMs = 400, kCheckTries = 8 };

static struct {
    TeamCheckTeam asked[2];         /* by player, what was last asked of each */
    int32_t       has[2];
    uint32_t      nonce;
    uint32_t      sentAt;
    int32_t       tries;            /* > 0 while an answer is awaited */
} s_check;

static int32_t refused(const LobbyPlayer *p)
{
    return p->verified == LOBBY_TEAM_REFUSED_UNKNOWN || p->verified == LOBBY_TEAM_REFUSED_CHANGED;
}

/* A match may not start on this player's team: the server refused it, or -- where
 * there is a server to ask -- has not answered about it yet. (An answer that never
 * comes ends as LOBBY_TEAM_NO_ANSWER after kCheckTries, about three seconds.) A start
 * that did not wait would begin on a team whose refusal was still on its way. */
static int32_t team_blocks(const LobbyPlayer *p)
{
    return refused(p) || (P2PLink_HasServer() && p->hasTeam && p->verified == LOBBY_TEAM_UNASKED);
}

static void team_check_send(void)
{
    uint8_t packet[TEAMCHECK_MAX_LEN];
    TeamCheck c;
    int32_t p;

    memset(&c, 0, sizeof c);
    c.type = TEAMCHECK_ASK;
    c.nonce = s_check.nonce;
    for (p = 0; p < 2; p++)
        if (s_check.has[p])
            c.team[c.count++] = s_check.asked[p];
    if (c.count)
        P2PLink_AskServer(packet, TeamCheck_Write(packet, &c));
    s_check.sentAt = SDL_GetTicks();
}

static void team_check(void)
{
    uint8_t packet[TEAMCHECK_MAX_LEN];
    TeamCheck c;
    int32_t p, i, n, changed = 0;

    if (!P2PLink_HasServer()) {
        memset(&s_check, 0, sizeof s_check);
        return;
    }
    /* What each side would play, now. A team that has changed is asked about afresh. */
    for (p = 0; p < 2; p++) {
        LobbyPlayer *pl = &s_lobby.players[p];
        TeamCheckTeam now;
        int32_t has = pl->hasTeam && (p == s_lobby.me - 1 || pl->known);

        memset(&now, 0, sizeof now);
        if (has)
            CustomTeam_Claim(&pl->team, now.uuid, now.checksum);
        /* For a test (TEAMCHECK_REFUSE=1 scripts/relay_test.sh): the other player's
         * team as if it had been doctored -- a checksum that is not its data's. */
        if (has && p != s_lobby.me - 1 && getenv("SWOS_TEAMCHECK_CORRUPT"))
            now.checksum[0] ^= 0x5A;
        if (has != s_check.has[p] || (has && memcmp(&now, &s_check.asked[p], sizeof now) != 0)) {
            s_check.has[p] = has;
            s_check.asked[p] = now;
            pl->verified = LOBBY_TEAM_UNASKED;
            changed = 1;
        }
    }
    if (changed) {
        s_check.nonce = s_check.nonce * 1664525u + 1013904223u + SDL_GetTicks();
        s_check.tries = (s_check.has[0] || s_check.has[1]) ? 1 : 0;
        if (s_check.tries)
            team_check_send();
    }

    n = P2PLink_ServerAnswer(TEAMCHECK_MAGIC, packet, (int32_t)sizeof packet);
    if (n > 0 && TeamCheck_Read(packet, n, &c) == 0 && c.type == TEAMCHECK_ANSWER && c.nonce == s_check.nonce &&
        s_check.tries > 0) {
        for (i = 0; i < c.count; i++)
            for (p = 0; p < 2; p++) {
                LobbyPlayer *pl = &s_lobby.players[p];
                if (!s_check.has[p] || memcmp(c.team[i].uuid, s_check.asked[p].uuid, 16) != 0 ||
                    memcmp(c.team[i].checksum, s_check.asked[p].checksum, 8) != 0)
                    continue;
                pl->verified = c.team[i].status == TEAMCHECK_OK      ? LOBBY_TEAM_CANONICAL :
                               c.team[i].status == TEAMCHECK_UNKNOWN ? LOBBY_TEAM_REFUSED_UNKNOWN :
                                                                       LOBBY_TEAM_REFUSED_CHANGED;
                fprintf(stderr, "[lobby] team check: player %d's %.16s is %s\n", (int)p + 1,
                        (const char *)pl->team.team.teamName,
                        pl->verified == LOBBY_TEAM_CANONICAL ? "canonical" :
                        pl->verified == LOBBY_TEAM_REFUSED_UNKNOWN ? "REFUSED: the server has no team with that id" :
                        "REFUSED: the server's copy of that team is other data");
                /* Nobody stays ready to play a team the server refuses. */
                if (refused(pl))
                    drop_my_ready();
            }
        s_check.tries = 0;
    } else if (s_check.tries > 0 && SDL_GetTicks() - s_check.sentAt >= (uint32_t)kCheckResendMs) {
        if (s_check.tries >= kCheckTries) {
            for (p = 0; p < 2; p++)
                if (s_check.has[p] && s_lobby.players[p].verified == LOBBY_TEAM_UNASKED)
                    s_lobby.players[p].verified = LOBBY_TEAM_NO_ANSWER;
            fprintf(stderr, "[lobby] team check: the server did not answer; the teams are unverified\n");
            s_check.tries = 0;
        } else {
            s_check.tries++;
            team_check_send();
        }
    }
}

int32_t NetplayLobby_CanStart(void)
{
    const LobbyPlayer *host = &s_lobby.players[0], *joiner = &s_lobby.players[1];
    return s_lobby.me == 1 && s_lobby.state == LOBBY_OPEN &&
           !team_blocks(host) && !team_blocks(joiner) &&     /* the server has vouched for both, or is not there */
           P2PLink_State() == P2P_CONNECTED &&
           host->hasTeam && host->ready && joiner->known && joiner->hasTeam && joiner->ready &&
           s_lobby.peerRulesSeen == s_lobby.rulesGen &&
           /* the joiner has seen our side as it is now */
           !s_lobby.dirty && P2PLink_Delivered(CH_STATE);
}

int32_t NetplayLobby_Start(void)
{
    uint8_t *buf;
    int32_t n;

    uint32_t cap;
    int32_t k;

    if (!NetplayLobby_CanStart())
        return 1;
    cap = START_HEADER + 4 + NetplaySetup_MaxBytes() + StateWire_MaxBytes(SIM_TIER_SIM);
    buf = (uint8_t *)malloc(cap);
    if (!buf)
        return 1;

    assemble_match();
    make_packet_key(s_lobby.packetKey);
    BE_WRITE_U32(buf, START_MAGIC);
    BE_WRITE_U16(buf + 4, LOBBY_VERSION);
    BE_WRITE_U16(buf + 6, s_lobby.peerStateGen);
    BE_WRITE_U16(buf + 8, s_lobby.rulesGen);
    memcpy(buf + 10, s_lobby.packetKey, KEY_BYTES);
    n = NetplaySetup_Encode(buf + START_HEADER + 4, NetplaySetup_MaxBytes());
    if (n <= 0) {
        free(buf);
        NetplayLobby_RestoreTactics();
        return 1;
    }
    BE_WRITE_U32(buf + START_HEADER, (uint32_t)n);
    /* The match state to kick off from -- this machine's, as it stands now. Both of us
     * load it before the setup and the match's own init (NetplayLobby_ApplyMatch), so the
     * two kick-offs cannot differ by anything either of us did before: a previous match
     * that ended out of step, or at different frames on the two machines, used to leave
     * a rematch out of step from frame 0 (2026-09-19). Without it (a pointer with no
     * identity, say), both peers start as before. */
    k = StateWire_Encode(buf + START_HEADER + 4 + n, cap - START_HEADER - 4 - (uint32_t)n,
                         SIM_TIER_SIM, 0, NULL, 0);
    if (k < 0) {
        fprintf(stderr, "[lobby] no kick-off state to offer (%s); each side kicks off from its own\n",
                StateWire_ErrorName(k));
        k = 0;
    }
    if (P2PLink_Send(CH_START, buf, (uint32_t)(START_HEADER + 4 + n + k)) != 0) {
        free(buf);
        NetplayLobby_RestoreTactics();
        return 1;
    }
    keep_setup(buf + START_HEADER + 4, (uint32_t)n);
    keep_kickoff(buf + START_HEADER + 4 + n, (uint32_t)k);
    free(buf);
    s_lobby.startGen = P2PLink_SentGeneration(CH_START);
    s_lobby.state = LOBBY_STARTING;
    set_notice("STARTING");
    return 0;
}

int32_t NetplayLobby_ApplyMatch(void)
{
    extern void InitTeams(void);
    int32_t rc;

    if (s_lobby.state != LOBBY_KICKOFF && s_lobby.state != LOBBY_CLOSED)
        return 1;
    if (!s_lobby.setup)
        return 1;
    /* The host's switches, before anything of the match is set up: CARDS/INJURIES
     * decides what the init below does with injured and booked players. */
    apply_switches(s_lobby.rules.switches);
    /* Both of us from the host's state, then the setup, then the match's own init: the
     * same state through the same steps. This machine keeps its own LOCAL fields -- the
     * HUD is placed for this display, not the host's (sim_state.c). */
    if (s_lobby.kickoff) {
        uint32_t size = SimState_Size(SIM_TIER_SIM);
        uint8_t *mine = (uint8_t *)malloc(size);
        int err;
        if (mine)
            SimState_Save(mine, SIM_TIER_SIM);
        err = StateWire_Decode(s_lobby.kickoff, s_lobby.kickoffLen, NULL, NULL);
        if (err != STATE_WIRE_OK)
            fprintf(stderr, "[lobby] the kick-off state did not load: %s\n", StateWire_ErrorName(err));
        else if (mine)
            SimState_RestoreLocal(mine, SIM_TIER_SIM);
        free(mine);
        keep_kickoff(NULL, 0);
    }
    rc = NetplaySetup_Apply(s_lobby.setup, s_lobby.setupLen);
    /* The recording keeps it: with the kick-off state and every frame's input it plays
     * this match again exactly (replay_v2.h). */
    if (rc == 0)
        ReplayRec_SetSetup(s_lobby.setup, s_lobby.setupLen);
    free(s_lobby.setup);
    s_lobby.setup = NULL;
    s_lobby.setupLen = 0;
    if (rc != 0)
        return rc;
    NetplayMatch_SetPacketKey(s_lobby.packetKey);
    InitTeams();            /* as the friendly menu does before PlayFriendly (ChooseTeamsMenu) */
    NetplaySetup_ArmTieRules(1);    /* the friendly init clears them; it takes these back */
    return 0;
}

void NetplayLobby_Peer(char *ip, uint32_t ipCap, uint16_t *peerPort, uint16_t *localPort,
                       int32_t *delay, int32_t *simWidth)
{
    if (ip && ipCap)
        snprintf(ip, ipCap, "%s", s_lobby.peerIp);
    if (peerPort)  *peerPort  = s_lobby.peerPort;
    if (localPort) *localPort = s_lobby.localPort;
    if (delay)     *delay     = s_lobby.delay;
    if (simWidth)  *simWidth  = s_lobby.simWidth;
}

#else  /* !SWOS_NETPLAY -- no link to set a match up over */

static LobbyRules  s_noRules;

void               NetplayLobby_Begin(int32_t localPlayer, const char *localName) { (void)localPlayer; (void)localName; }
void               NetplayLobby_Poll(void) {}
void               NetplayLobby_End(void) {}
int32_t            NetplayLobby_Rematch(const char *notice) { (void)notice; return 1; }
LobbyState         NetplayLobby_State(void) { return LOBBY_CLOSED; }
int32_t            NetplayLobby_LocalPlayer(void) { return 0; }
const LobbyPlayer *NetplayLobby_Player(int32_t player) { (void)player; return NULL; }
const LobbyRules  *NetplayLobby_Rules(void) { return &s_noRules; }
const char        *NetplayLobby_Notice(void) { return ""; }
void               NetplayLobby_SetTeam(const TeamFileEx *team) { (void)team; }
void               NetplayLobby_SetTactic(int32_t tactic) { (void)tactic; }
void               NetplayLobby_SetReady(int32_t ready) { (void)ready; }
void               NetplayLobby_SetActivity(int32_t activity) { (void)activity; }
void               NetplayLobby_SetRules(const LobbyRules *rules) { (void)rules; }
void               NetplayLobby_MatchPlayed(int32_t completed, int32_t goals1, int32_t goals2)
{ (void)completed; (void)goals1; (void)goals2; }
int32_t            NetplayLobby_IsHost(void) { return 0; }
int32_t            NetplayLobby_CanStart(void) { return 0; }
int32_t            NetplayLobby_Start(void) { return 1; }
void               NetplayLobby_Peer(char *ip, uint32_t ipCap, uint16_t *peerPort,
                                     uint16_t *localPort, int32_t *delay, int32_t *simWidth)
{
    if (ip && ipCap) ip[0] = '\0';
    if (peerPort) *peerPort = 0;
    if (localPort) *localPort = 0;
    if (delay) *delay = 0;
    if (simWidth) *simWidth = 0;
}
void               NetplayLobby_RestoreTactics(void) {}
int32_t            NetplayLobby_ApplyMatch(void) { return 1; }

#endif /* SWOS_NETPLAY */
#endif /* SWOS_SDL_BUILD */
