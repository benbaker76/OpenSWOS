/*
 * mod_match.c -- a match a mod plays (include/mod_api.h ModApi_PlayMatch; docs/MODS.md §16), and
 * the match events the mods are told. SDL builds.
 *
 * A mod's match is a friendly played without its menus, as the arcade cabinet plays one
 * (src/arcade.c arc_start_game): the two teams loaded into _selectedTeams, the friendly's
 * globals set, InitAndPlayGame. The engine asks this file at the points where the friendly
 * would have asked the player or done its own thing:
 *   SetupPlayers           who controls each side (no line-up prompts)    ModMatch_SetupPlayers
 *   InitAndPlayGame        the match's name, extra time / penalties       ModMatch_Name, _TieRules
 *   the stadium screen     shown for its 3 seconds or not                 ModMatch_SkipStadium
 *   InitGameRestoreTeams   the score and the clock at kick-off            ModMatch_KickOff
 *   each frame             the frame count, a shoot-out straight away     ModMatch_Frame
 *   the final whistle      the REPLAY / EXIT menu or not; the result      ModMatch_SkipReplayMenu,
 *                                                                         ModMatch_Ended
 * Each is a no-op when no mod's match is being played, except the events and the result,
 * which every mod that is on is told of every match.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "game_types.h"
#include "mod_hooks.h"
#include "play_match_menu.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern TeamFileEx _selectedTeams[];
extern uint16_t   _numSelectedTeams;
extern uint8_t    _teamFileBuffer[];
extern int16_t    LoadTeamFile(uint16_t extNumber);
extern void       InitTeams(void);
extern void       InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);
extern void       StartPenalties(void);
extern volatile uint16_t _quitToDos;
extern uint16_t _gameType, _friendlyPitchType, _pitchTypeOrSeason, _maxSubstitutes, _minSubstitutes;
extern uint16_t _gameLength, _autoReplays;
extern uint16_t _team1TotalGoals, _team2TotalGoals, _team1PenaltyGoals, _team2PenaltyGoals;
extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t _team1GoalsDigit1, _team1GoalsDigit2, _team2GoalsDigit1, _team2GoalsDigit2;
extern uint16_t _gameStatePl, _playingPenalties, _gameCanceled;
extern volatile uint16_t _playGame;
extern uint32_t _gameTime;
extern uint16_t _gameSeconds;
extern int16_t  _extraTimeState, _penaltiesState;
extern int16_t  _isCareer, _exitGameFlag;
extern uint16_t _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
extern uint16_t _twoPlayers, _pl1Coach, _pl2Coach;
extern uint16_t _topTeamPlayerNo, _topTeamCoachNo, _bottomTeamPlayerNo, _bottomTeamCoachNo;
extern int16_t  _pl1Tactics, _pl2Tactics;
extern int32_t  NetplayMatch_Resimulating(void);

static struct {
    int32_t       active;               /* a mod's match is on */
    char          slug[40];
    ModMatchSetup setup;
    char          name[32];
    uint8_t       ended;                /* ModApi_EndMatch */
    uint8_t       shootoutDone;
} s_mm;

static uint32_t s_frames;               /* from kick-off, every match */
static uint8_t  s_kickedOff;
static uint8_t  s_endTold;              /* this kick-off's end told the mods */
static int32_t  s_testTraining;         /* SWOS_MOD_MATCH_TEST=training (ModMatch_SelfTest) */

int32_t ModMatch_Active(void) { return s_mm.active; }

/* ---- the setup ---- */

void ModApi_DefaultSetup(ModMatchSetup *setup)
{
    memset(setup, 0, sizeof *setup);
    setup->side[0] = setup->side[1] = MODSIDE_CPU;
    setup->pitchType = -1;
    setup->subsMin = 2;
    setup->subsMax = 5;
    setup->autoReplays = 1;
    setup->stadium = 1;
    setup->replayMenu = 1;
    setup->playingUp = setup->kickOff = -1;
}

/* the team of that id (file << 8 | ordinal) into _selectedTeams[slot] */
static int32_t load_team(uint16_t id, int32_t slot, uint8_t controls)
{
    const uint8_t file = (uint8_t)(id >> 8), ord = (uint8_t)id;
    if (LoadTeamFile(file) != 0) {
        fprintf(stderr, "[mods] a match: team file %u cannot be loaded\n", (unsigned)file);
        return -1;
    }
    const uint16_t count = *(uint16_t *)_teamFileBuffer;
    const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2);
    for (uint16_t i = 0; i < count; i++)
        if (rec[i].team.countryNumber == file && rec[i].team.teamOrdinal == ord) {
            memcpy(&_selectedTeams[slot], &rec[i], sizeof(TeamFileEx));
            _selectedTeams[slot].team.teamControls = controls;
            return 0;
        }
    fprintf(stderr, "[mods] a match: team %u.%u is not in its file\n", (unsigned)file, (unsigned)ord);
    return -1;
}

static int16_t clock_minute(void)
{
    const uint8_t *t = (const uint8_t *)&_gameTime;
    return (int16_t)(t[1] * 100 + t[2] * 10 + t[3]);
}

int32_t ModApi_PlayMatch(const char *slug, const ModMatchSetup *setup, ModMatchResult *result)
{
    if (s_mm.active || !setup)
        return -1;
    memset(&s_mm, 0, sizeof s_mm);
    snprintf(s_mm.slug, sizeof s_mm.slug, "%s", slug ? slug : "");
    s_mm.setup = *setup;
    if (setup->name) {
        size_t i;
        for (i = 0; setup->name[i] && i + 1 < sizeof s_mm.name; i++)
            s_mm.name[i] = (char)(setup->name[i] >= 'a' && setup->name[i] <= 'z' ? setup->name[i] - 32 : setup->name[i]);
        s_mm.name[i] = '\0';
    }
    if (setup->chosen) {
        /* the team screen's: who plays each as the player set it there */
        if (_numSelectedTeams < 2)
            return -1;
        int32_t players = 0;
        for (int32_t t = 0; t < 2; t++) {
            const int32_t cpu = _selectedTeams[t].team.teamControls == TEAM_COMPUTER;
            s_mm.setup.side[t] = (uint8_t)(cpu ? MODSIDE_CPU : players++ ? MODSIDE_PLAYER2 : MODSIDE_PLAYER1);
            _selectedTeams[t].team.teamControls = (uint8_t)(cpu ? TEAM_COMPUTER : TEAM_PLAYER_COACH);
        }
    } else {
        _numSelectedTeams = 0;
        for (int32_t t = 0; t < 2; t++)
            if (load_team(setup->team[t], t, setup->side[t] == MODSIDE_CPU ? TEAM_COMPUTER : TEAM_PLAYER_COACH) != 0)
                return -1;
        _numSelectedTeams = 2;
    }

    /* the friendly's own settings, kept to be put back: the mod's match is not the player's */
    const uint16_t keep[6] = { _friendlyPitchType, _maxSubstitutes, _minSubstitutes, _gameLength, _autoReplays,
                               _pitchTypeOrSeason };
    _gameType = 0;
    _pitchTypeOrSeason = 0;
    _friendlyPitchType = setup->pitchType < 0 ? 0xFFFF : (uint16_t)setup->pitchType;
    _minSubstitutes = setup->subsMin;
    _maxSubstitutes = setup->subsMax;
    _gameLength = setup->length > 3 ? 3 : setup->length;
    _autoReplays = setup->autoReplays;
    fprintf(stderr, "[mods] %s plays a match: %04x v %04x\n", s_mm.slug, (unsigned)setup->team[0],
            (unsigned)setup->team[1]);

    extern uint16_t _cardsDisallowed[];
    const uint16_t keepCards = _cardsDisallowed[0];
    if (setup->noCards)
        _cardsDisallowed[0] = 1;
    InitTeams();
    s_mm.active = 1;
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);
    s_mm.active = 0;
    _cardsDisallowed[0] = keepCards;

    _friendlyPitchType = keep[0];
    _maxSubstitutes = keep[1];
    _minSubstitutes = keep[2];
    _gameLength = keep[3];
    _autoReplays = keep[4];
    _pitchTypeOrSeason = keep[5];
    if (result) {
        memset(result, 0, sizeof *result);
        result->goals[0] = (int16_t)_team1TotalGoals;
        result->goals[1] = (int16_t)_team2TotalGoals;
        result->penaltyGoals[0] = (int16_t)_team1PenaltyGoals;
        result->penaltyGoals[1] = (int16_t)_team2PenaltyGoals;
        const int32_t d = (_team1TotalGoals - _team2TotalGoals) * 100 + (_team1PenaltyGoals - _team2PenaltyGoals);
        result->winner = (int8_t)(d > 0 ? 0 : d < 0 ? 1 : -1);
        result->quit = (uint8_t)(_quitToDos || _gameCanceled);
        result->endedByMod = s_mm.ended;
        result->modMatch = 1;
        result->minute = clock_minute();
        result->frames = s_frames;
    }
    return 0;
}

int32_t ModApi_ChooseTeams(void)
{
    extern void GoGetTeamsForPlay(uint8_t *gameType, uint8_t *playWhat, uint16_t numTeams, uint16_t showClub,
                                  uint16_t showNational, uint16_t showOrigCustom);
    extern uint16_t _chooseTeamsResult;
    extern uint8_t _aFriendly_1[], _aPlayFriendly[];
    GoGetTeamsForPlay(_aFriendly_1, _aPlayFriendly, 2, 1, 1, 1);
    return _chooseTeamsResult != 0 && _numSelectedTeams >= 2;
}

/* After ModApi_ChooseTeams: whether a chosen team is a player's (1) or the CPU's (0). */
int32_t ModMatch_ChosenSide(int32_t team)
{
    return team >= 0 && team < 2 && _numSelectedTeams >= 2 && _selectedTeams[team].team.teamControls != TEAM_COMPUTER;
}

uint16_t ModApi_RandomTeam(void)
{
    static uint32_t state;
    if (!state)
        state = (uint32_t)SDL_GetPerformanceCounter() | 1u;
    for (int32_t tries = 0; tries < 8; tries++) {
        state = state * 1664525u + 1013904223u;
        const uint8_t file = (uint8_t)(0x50 + (state >> 16) % 6);  /* the national teams' files */
        if (LoadTeamFile(file) != 0)
            continue;
        const uint16_t count = *(uint16_t *)_teamFileBuffer;
        if (!count)
            continue;
        state = state * 1664525u + 1013904223u;
        const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2) + (state >> 16) % count;
        return (uint16_t)(rec->team.countryNumber << 8 | rec->team.teamOrdinal);
    }
    return 0;
}

int32_t ModApi_ModMatch(const char *slug)
{
    return s_mm.active && slug && strcmp(s_mm.slug, slug) == 0;
}

void ModApi_EndMatch(void)
{
    if (!_playGame)
        return;
    s_mm.ended = 1;
    _playGame = 0;
}

void ModApi_StartShootout(void)
{
    if (_playGame && !_playingPenalties)
        StartPenalties();
}

void ModApi_Score(int16_t goals[2])
{
    goals[0] = (int16_t)_team1TotalGoals;
    goals[1] = (int16_t)_team2TotalGoals;
}

uint32_t ModApi_MatchFrames(void) { return s_frames; }

/* ---- the engine asks (include/mod_hooks.h) ---- */

/* SetupPlayers: the sides, as the arcade cabinet sets them -- no prompts. 1 when it did. */
int32_t ModMatch_SetupPlayers(TeamFileEx *team1, TeamFileEx *team2)
{
    if (!s_mm.active)
        return 0;
    const uint8_t a = s_mm.setup.side[0], b = s_mm.setup.side[1];
    _isCareer = 0;
    _exitGameFlag = 0;
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;
    _twoPlayers = (a != MODSIDE_CPU && b != MODSIDE_CPU) ? 1 : 0;
    _pl1Coach = 0;
    _pl2Coach = 0;
    _topTeamPlayerNo = a == MODSIDE_CPU ? 0 : a == MODSIDE_PLAYER2 ? 2 : 1;
    _topTeamCoachNo = 0;
    _bottomTeamPlayerNo = b == MODSIDE_CPU ? 0 : b == MODSIDE_PLAYER1 ? 1 : 2;
    _bottomTeamCoachNo = 0;
    _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
    _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
    return 1;
}

const char *ModMatch_Name(void)
{
    return s_mm.active && s_mm.name[0] ? s_mm.name : NULL;
}

void ModMatch_TieRules(void)
{
    if (!s_mm.active)
        return;
    _extraTimeState = s_mm.setup.extraTime ? 1 : 0;
    _penaltiesState = (s_mm.setup.penalties || s_mm.setup.shootoutOnly) ? 1 : 0;
}

int32_t ModMatch_SkipStadium(void) { return s_mm.active && !s_mm.setup.stadium; }
int32_t ModMatch_SkipReplayMenu(void) { return s_mm.active && !s_mm.setup.replayMenu; }

/* InitGameRestoreTeams' end (and again for a REPLAY): the score and the clock at kick-off. */
void ModMatch_KickOff(void)
{
    s_frames = 0;
    s_kickedOff = 0;
    s_endTold = 0;
    if (s_mm.active) {
        s_mm.shootoutDone = 0;
        const int16_t g1 = s_mm.setup.score[0] > 0 ? s_mm.setup.score[0] : 0;
        const int16_t g2 = s_mm.setup.score[1] > 0 ? s_mm.setup.score[1] : 0;
        _team1TotalGoals = _statsTeam1Goals = (uint16_t)g1;
        _team2TotalGoals = _statsTeam2Goals = (uint16_t)g2;
        _team1GoalsDigit1 = (uint16_t)(g1 / 10);
        _team1GoalsDigit2 = (uint16_t)(g1 % 10);
        _team2GoalsDigit1 = (uint16_t)(g2 / 10);
        _team2GoalsDigit2 = (uint16_t)(g2 % 10);
        int16_t m = s_mm.setup.minute;
        if (m < 0) m = 0;
        if (m > 44) m = 44;                     /* (the first half: half time is its own flow) */
        uint8_t *t = (uint8_t *)&_gameTime;
        t[0] = 0;
        t[1] = (uint8_t)(m / 100);
        t[2] = (uint8_t)(m / 10 % 10);
        t[3] = (uint8_t)(m % 10);
        _gameSeconds = 0;
    }
    ModHooks_MatchStart();
}

/* InitGameRestoreTeams, the sides just drawn (G1): the mod's, where it said. */
void ModMatch_Sides(void)
{
    extern uint16_t _teamPlayingUp, _teamStarting;
    if (!s_mm.active)
        return;
    if (s_mm.setup.playingUp == 0 || s_mm.setup.playingUp == 1)
        _teamPlayingUp = (uint16_t)(s_mm.setup.playingUp + 1);
    if (s_mm.setup.kickOff == 0 || s_mm.setup.kickOff == 1)
        _teamStarting = (uint16_t)(s_mm.setup.kickOff + 1);
}

/* InitPlayersBeforeEnteringPitch's end (G4): the lines left out, gone as a sent-off man goes --
 * the engine already plays around one (sentAway) -- but in the tunnel already. */
void ModMatch_PlayersEntered(void)
{
    extern Sprite **_team1SpritesTable;
    extern Sprite **_team2SpritesTable;
    extern TeamGame *_topTeamPtr;
    extern TeamGame *_bottomTeamPtr;
    if (!s_mm.active || !(s_mm.setup.linesOut[0] | s_mm.setup.linesOut[1]))
        return;
    for (int32_t t = 0; t < 2; t++) {
        Sprite **tab = t == 0 ? _team1SpritesTable : _team2SpritesTable;
        const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
        const uint8_t out = s_mm.setup.linesOut[t];
        if (!out || !tab || !team)
            continue;
        for (int32_t i = 1; i < 11; i++) {          /* (never the goalkeeper) */
            const int8_t pos = team->players[i].position;
            const uint8_t line = pos == POS_ATTACKER ? MODLINE_ATTACK
                               : (pos == POS_RIGHT_WING || pos == POS_LEFT_WING || pos == POS_MIDFIELDER) ? MODLINE_MIDFIELD
                               : MODLINE_DEFENCE;
            Sprite *sp = tab[i];
            if (!(out & line) || !sp)
                continue;
            sp->cards = -1;
            sp->sentAway = 1;
            sp->x.s.i = sp->destX = -20;
            sp->y.s.i = sp->destY = 449;
        }
    }
}

int32_t ModMatch_NoInjuries(void) { return s_mm.active && s_mm.setup.noInjuries; }

/* A re-simulated frame (netplay): the match hooks are told only frames as first played. */
int32_t ModMatch_Quiet(void) { return NetplayMatch_Resimulating(); }

/* Each frame of a match, after the clock. */
void ModMatch_Frame(void)
{
    if (!s_kickedOff && _gameStatePl == ST_GAME_IN_PROGRESS)
        s_kickedOff = 1;
    if (s_kickedOff && !ModMatch_Quiet())
        s_frames++;
    if (s_mm.active && s_mm.setup.clockStopped) {   /* (G7: the clock back to its start each frame) */
        int16_t m = s_mm.setup.minute < 0 ? 0 : s_mm.setup.minute > 44 ? 44 : s_mm.setup.minute;
        uint8_t *t = (uint8_t *)&_gameTime;
        t[1] = (uint8_t)(m / 100);
        t[2] = (uint8_t)(m / 10 % 10);
        t[3] = (uint8_t)(m % 10);
        _gameSeconds = 0;
    }
    if (s_mm.active && s_mm.setup.shootoutOnly && !s_mm.shootoutDone && s_kickedOff) {
        s_mm.shootoutDone = 1;
        ModApi_StartShootout();
    }
    if (s_testTraining && s_mm.active && s_frames >= 3000)
        ModApi_EndMatch();                  /* (the training test: a minute of play, then ended) */
    ModHooks_MatchFrame();
}

/* An event, from its site in the match code: team 1..2 as the engine numbers them (0: none),
 * player his sprite's index in his team (-1: none). */
void ModMatch_Event(int32_t kind, int32_t team, int32_t player, int32_t arg)
{
    ModMatchEvent e;
    static int logEvents = -1;
    if (logEvents < 0)
        logEvents = getenv("SWOS_MOD_EVENT_LOG") || getenv("SWOS_MOD_MATCH_TEST");
    e.kind = (uint8_t)kind;
    e.team = (int8_t)(team >= 1 && team <= 2 ? team - 1 : -1);
    e.player = (int8_t)player;
    e.arg = (int8_t)arg;
    e.minute = clock_minute();
    e.frame = s_frames;
    if (logEvents && !ModMatch_Quiet())
        fprintf(stderr, "[mods] event %d team %d player %d arg %d minute %d frame %u\n", (int)e.kind, (int)e.team,
                (int)e.player, (int)e.arg, (int)e.minute, (unsigned)e.frame);
    ModHooks_MatchEvent(&e);
}

/* NextPenalty: a kick was taken -- by the team whose attempts went up, in if its goals did. */
void ModMatch_ShootoutKick(void)
{
    extern uint16_t _team1PenaltyAttempts, _team2PenaltyAttempts;
    static uint16_t att[2], goals[2];
    if (_team1PenaltyAttempts + _team2PenaltyAttempts <= 1) {   /* (the first kick: a new shoot-out) */
        att[0] = att[1] = goals[0] = goals[1] = 0;
    }
    const int32_t team = _team1PenaltyAttempts != att[0] ? 1 : _team2PenaltyAttempts != att[1] ? 2 : 0;
    const int32_t in = _team1PenaltyGoals != goals[0] || _team2PenaltyGoals != goals[1];
    att[0] = _team1PenaltyAttempts;
    att[1] = _team2PenaltyAttempts;
    goals[0] = _team1PenaltyGoals;
    goals[1] = _team2PenaltyGoals;
    if (team)
        ModMatch_Event(MODEVENT_SHOOTOUT_KICK, team, -1, in);
}

/* The index of a player's sprite in its team's table, or -1. */
int32_t ModMatch_PlayerIndex(const Sprite *s)
{
    extern Sprite **_team1SpritesTable;
    extern Sprite **_team2SpritesTable;
    for (int32_t i = 0; s && i < 11; i++)
        if ((_team1SpritesTable && _team1SpritesTable[i] == s) || (_team2SpritesTable && _team2SpritesTable[i] == s))
            return i;
    return -1;
}

/* The final whistle (and any replay menu): every mod is told; after a friendly the player's
 * AFTER THE MATCH entries, when a mod has one. */
void ModMatch_Ended(void)
{
    ModMatchResult r;
    if (s_endTold)
        return;
    s_endTold = 1;
    memset(&r, 0, sizeof r);
    r.goals[0] = (int16_t)_team1TotalGoals;
    r.goals[1] = (int16_t)_team2TotalGoals;
    r.penaltyGoals[0] = (int16_t)_team1PenaltyGoals;
    r.penaltyGoals[1] = (int16_t)_team2PenaltyGoals;
    const int32_t d = (_team1TotalGoals - _team2TotalGoals) * 100 + (_team1PenaltyGoals - _team2PenaltyGoals);
    r.winner = (int8_t)(d > 0 ? 0 : d < 0 ? 1 : -1);
    r.quit = (uint8_t)(_quitToDos || _gameCanceled);
    r.endedByMod = s_mm.ended;
    r.modMatch = (uint8_t)s_mm.active;
    r.minute = clock_minute();
    r.frames = s_frames;
    ModMatch_Event(MODEVENT_FINAL_WHISTLE, 0, -1, 0);
    ModHooks_MatchEnded(&r);
}

/* SWOS_MOD_MATCH_TEST=1: at the first main menu, a mod's match through ModApi_PlayMatch --
 * Norway v New Zealand, CPU v CPU, 2-1 at minute 40, no stadium or replay menu -- its events
 * logged ([mods] event) and its result, then the game quits: the match hooks proved without a
 * mod (with SWOS_HARNESS_TURBO it runs as fast as it can). */
static void play_test_match(int32_t thenQuit);
static int32_t s_testShootout;          /* SWOS_MOD_MATCH_TEST=shootout */

static void test_entry(void) { play_test_match(0); }

void ModMatch_SelfTest(void)
{
    static int done;
    const char *how = getenv("SWOS_MOD_MATCH_TEST");
    if (done || !how)
        return;
    done = 1;
    if (strcmp(how, "entry") == 0) {
        /* (=entry: a MODES entry that plays it, for trying the hooks by hand) */
        ModApi_AddEntry(MODPOINT_MODES, "selftest", "mod match test", test_entry);
        return;
    }
    s_testShootout = strcmp(how, "shootout") == 0;
    s_testTraining = strcmp(how, "training") == 0;
    play_test_match(1);
}

static void play_test_match(int32_t thenQuit)
{
    ModMatchSetup setup;
    ModMatchResult r;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = 0x5020;                 /* Norway */
    setup.team[1] = 0x5502;                 /* New Zealand */
    setup.score[0] = 2;
    setup.score[1] = 1;
    setup.minute = 40;
    setup.stadium = 0;
    setup.replayMenu = 0;
    setup.autoReplays = 0;
    setup.name = "mod match test";
    if (s_testTraining) {                   /* training's rules: the clock at 0, lines out, ended by us */
        setup.clockStopped = setup.noCards = setup.noInjuries = 1;
        setup.score[0] = setup.score[1] = 0;
        setup.minute = 0;
        setup.linesOut[0] = MODLINE_DEFENCE | MODLINE_ATTACK;
        setup.linesOut[1] = MODLINE_MIDFIELD;
        setup.playingUp = 0;
        setup.kickOff = 1;
    }
    if (s_testShootout) {                   /* straight to penalties: a shoot-out's result */
        setup.shootoutOnly = 1;
        setup.score[0] = setup.score[1] = 0;
        setup.minute = 0;
    }
    const int32_t rc = ModApi_PlayMatch("selftest", &setup, &r);
    fprintf(stderr, "[mods] mod match test: rc %d, %d-%d (pens %d-%d), winner %d, minute %d, %u frames, quit %d\n",
            (int)rc, (int)r.goals[0], (int)r.goals[1], (int)r.penaltyGoals[0], (int)r.penaltyGoals[1],
            (int)r.winner, (int)r.minute, (unsigned)r.frames, (int)r.quit);
    if (!thenQuit)
        return;
    const int32_t ok = rc == 0 && r.frames > 0 &&
                       (s_testShootout ? r.penaltyGoals[0] != r.penaltyGoals[1]
                        : s_testTraining ? r.minute == 0 && r.endedByMod && r.frames >= 3000 : r.minute >= 90);
    fprintf(stdout, "MOD MATCH TEST %s%s\n", s_testShootout ? "(shootout) " : s_testTraining ? "(training) " : "",
            ok ? "PASS" : "FAIL");
    fflush(stdout);
    exit(ok ? 0 : 1);
}

#endif /* SWOS_SDL_BUILD */
