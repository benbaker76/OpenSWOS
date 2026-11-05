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

#include "game_types.h"
#include "mod_hooks.h"
#include "play_match_menu.h"
#include "team_uuid.h"      /* (the Amiga: a team's number checked against its id) */
#include "world.h"
#ifdef SWOS_SDL_BUILD
#include "team_db.h"
#include <SDL.h>
#endif

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
    uint8_t       ended;                /* ModApi_EndMatch / FinalWhistle */
    uint8_t       whistled;             /* ModApi_FinalWhistle (once) */
    uint8_t       shootoutDone;
} s_mm;

static TeamFileEx s_chosenKept[2];      /* the last chosen pair, as chosen (ModApi_ChosenAgain) */
static int32_t    s_chosenHave;
static uint32_t s_frames;               /* from kick-off, every match */
static uint8_t  s_kickedOff;
static uint8_t  s_endTold;              /* this kick-off's end told the mods */
static int32_t  s_testHooks;            /* SWOS_MOD_MATCH_TEST=hooks: PR #3's H1-H7, G6 */
static int32_t  s_testTraining;         /* SWOS_MOD_MATCH_TEST=training (ModMatch_SelfTest) */
static const Sprite *s_headerBy;        /* the last header: who (ModMatch_Header), when */
static uint32_t      s_headerAt;

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
    for (int32_t i = 0; i < 8; i++)
        setup->scorers[i].team = -1;
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
        memcpy(s_chosenKept, &_selectedTeams[0], sizeof s_chosenKept);   /* (for ModApi_ChosenAgain) */
        s_chosenHave = 1;
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
#ifdef SWOS_SDL_BUILD
        if (getenv("SWOS_MOD_MATCH_TEAMS")) {   /* (the test: would the online rule take them?) */
            extern int32_t CustomTeam_Allowed(const TeamFileEx *team);
            for (int32_t t = 0; t < 2; t++)
                fprintf(stderr, "[mods] team %04x %.16s: online rule %s\n", (unsigned)setup->team[t],
                        (const char *)_selectedTeams[t].team.teamName,
                        CustomTeam_Allowed(&_selectedTeams[t]) ? "accepts it" : "REFUSES it");
        }
#endif
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
    {
        /* SWOS_MOD_CHOOSE_TEAMS=<hex id>,<hex id>[,<0|1: the player's>] answers the team screen, for
         * driving a mod's flow in a test (the screen itself is the friendly's, tested as that). */
        const char *t = getenv("SWOS_MOD_CHOOSE_TEAMS");
        if (t && strchr(t, ',')) {
            const char *b = strchr(t, ',') + 1, *c = strchr(b, ',');
            const int32_t yours = c ? atoi(c + 1) : 0;
            _numSelectedTeams = 0;
            if (load_team((uint16_t)strtoul(t, NULL, 16), 0, (uint8_t)(yours == 0 ? TEAM_PLAYER_COACH : TEAM_COMPUTER)) ||
                load_team((uint16_t)strtoul(b, NULL, 16), 1, (uint8_t)(yours == 1 ? TEAM_PLAYER_COACH : TEAM_COMPUTER)))
                return 0;
            _numSelectedTeams = 2;
            return 1;
        }
    }
    GoGetTeamsForPlay(_aFriendly_1, _aPlayFriendly, 2, 1, 1, 1);
    return _chooseTeamsResult != 0 && _numSelectedTeams >= 2;
}

#ifndef SWOS_SDL_BUILD
/* (team packs are the SDL build's: the Amiga has none to offer) */
int32_t ModApi_PackCount(void) { return 0; }
int32_t ModApi_PackInfo(int32_t i, int32_t *number, const char **name, const char **description)
{
    (void)i; (void)number; (void)name; (void)description;
    return 0;
}
int32_t ModApi_ChooseFromPack(int32_t number) { (void)number; return 0; }
#else
int32_t ModApi_PackCount(void)
{
    extern int32_t TeamPacks_Count(void);
    return TeamPacks_Count();
}

int32_t ModApi_PackInfo(int32_t i, int32_t *number, const char **name, const char **description)
{
    extern int32_t TeamPacks_Info(int32_t i, int32_t *number, const char **name, const char **description,
                                  int32_t *teams);
    return TeamPacks_Info(i, number, name, description, NULL);
}

int32_t ModApi_ChooseFromPack(int32_t number)
{
    extern void GoGetTeamsFromPack(uint8_t *gameType, uint8_t *playWhat, uint16_t numTeams, int32_t pack);
    extern uint16_t _chooseTeamsResult;
    extern uint8_t _aFriendly_1[], _aPlayFriendly[];
    GoGetTeamsFromPack(_aFriendly_1, _aPlayFriendly, 2, number);
    return _chooseTeamsResult != 0 && _numSelectedTeams >= 2;
}
#endif

#ifdef SWOS_SDL_BUILD
uint16_t ModApi_TeamByUuid(const char *uuid)
{
    const TeamDbEntry *e = uuid ? TeamDb_ByUuid(uuid) : NULL;
    return e ? (uint16_t)(e->team.team.countryNumber << 8 | e->team.team.teamOrdinal) : 0;
}

uint16_t ModApi_TeamByUuidNumber(const char *uuid, uint16_t number)
{
    return uuid && uuid[0] ? ModApi_TeamByUuid(uuid) : number;
}
#else
/* (the Amiga has no team database, so no ids: a mod there names teams by number) */
uint16_t ModApi_TeamByUuid(const char *uuid) { (void)uuid; return 0; }

uint16_t ModApi_TeamByUuidNumber(const char *uuid, uint16_t number)
{
    char derived[TEAM_UUID_TEXT];
    if (!uuid || !uuid[0])
        return number;
    if (!number)
        return 0;
    TeamDb_DerivedTeamUuid(number >> 8, number & 0xFF, derived);
    return strcmp(derived, uuid) == 0 ? number : 0;
}

/* A team by its number, from its team file (the Amiga's, for want of the database). */
static const TeamFileEx *file_team(uint16_t teamId)
{
    if (LoadTeamFile((uint8_t)(teamId >> 8)) != 0)
        return NULL;
    const uint16_t count = *(uint16_t *)_teamFileBuffer;
    const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2);
    for (uint16_t i = 0; i < count; i++)
        if (rec[i].team.teamOrdinal == (teamId & 0xFF) && rec[i].team.countryNumber == (teamId >> 8))
            return &rec[i];
    return NULL;
}
#endif

static int32_t team_player(const TeamFileEx *t, int32_t slot, ModTeamPlayer *out)
{
    if (!t || !out || slot < 0 || slot >= 16 || t->team.playerNumbers[slot] >= 16)
        return 0;
    const PlayerFile *p = &t->players[t->team.playerNumbers[slot]];
    memset(out, 0, sizeof *out);
    memcpy(out->name, p->playerName, sizeof p->playerName < sizeof out->name - 1 ? sizeof p->playerName
                                                                                 : sizeof out->name - 1);
    out->position = (uint8_t)((p->positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT);
    out->shirt = (uint8_t)p->shirtNumber;
    out->passing = (uint8_t)(p->passing & 0x0F);
    out->shooting = (uint8_t)((uint8_t)p->velocityHeading >> 4);
    out->heading = (uint8_t)(p->velocityHeading & 0x0F);
    out->tackling = (uint8_t)((uint8_t)p->tacklingControl >> 4);
    out->control = (uint8_t)(p->tacklingControl & 0x0F);
    out->speed = (uint8_t)((uint8_t)p->speedFinishing >> 4);
    out->finishing = (uint8_t)(p->speedFinishing & 0x0F);
    return 1;
}

int32_t ModApi_TeamPlayer(uint16_t teamId, int32_t slot, ModTeamPlayer *out)
{
#ifdef SWOS_SDL_BUILD
    const TeamDbEntry *e = TeamDb_Team(teamId >> 8, teamId & 0xFF);
    return e ? team_player(&e->team, slot, out) : 0;
#else
    return team_player(file_team(teamId), slot, out);
#endif
}

int32_t ModApi_ChosenTeamPlayer(int32_t team, int32_t slot, ModTeamPlayer *out)
{
    if (team < 0 || team > 1 || _numSelectedTeams < 2)
        return 0;
    return team_player(&_selectedTeams[team], slot, out);
}

/* After ModApi_ChooseTeams: whether a chosen team is a player's (1) or the CPU's (0). */
int32_t ModMatch_ChosenSide(int32_t team)
{
    return team >= 0 && team < 2 && _numSelectedTeams >= 2 && _selectedTeams[team].team.teamControls != TEAM_COMPUTER;
}
int32_t ModApi_ChosenIsPlayer(int32_t team) { return ModMatch_ChosenSide(team); }

int32_t ModApi_ChosenTeamUuid(int32_t team, char *out, int32_t cap)
{
    if (!out || cap <= 0 || team < 0 || team > 1 || _numSelectedTeams < 2)
        return 0;
#ifdef SWOS_SDL_BUILD
    const TeamDbEntry *e = TeamDb_Team(_selectedTeams[team].team.countryNumber, _selectedTeams[team].team.teamOrdinal);
    if (!e || !e->ids.uuid[0])
        return 0;
    snprintf(out, (size_t)cap, "%s", e->ids.uuid);
    return 1;
#else
    /* (the Amiga has no database: a 96/97 team's id is derived from its number -- a world's are
     *  stated in its data, which the Amiga does not read) */
    if (World_Current() != WORLD_9697)
        return 0;
    if (cap < TEAM_UUID_TEXT)
        return 0;
    TeamDb_DerivedTeamUuid(_selectedTeams[team].team.countryNumber, _selectedTeams[team].team.teamOrdinal, out);
    return 1;
#endif
}

uint16_t ModApi_ChosenTeamNumber(int32_t team)
{
    if (team < 0 || team > 1 || _numSelectedTeams < 2)
        return 0;
    return (uint16_t)(_selectedTeams[team].team.countryNumber << 8 | _selectedTeams[team].team.teamOrdinal);
}

const char *ModApi_ChosenName(int32_t team)
{
    return team >= 0 && team < 2 && _numSelectedTeams >= 2 ? (const char *)_selectedTeams[team].team.teamName : "";
}

int32_t ModApi_ChosenAgain(void)
{
    if (!s_chosenHave)
        return 0;
    memcpy(&_selectedTeams[0], &s_chosenKept[0], sizeof s_chosenKept);
    _numSelectedTeams = 2;
    return 1;
}

int32_t ModApi_FrameRate(void)
{
    extern int _gGameplayStyle;
    return _gGameplayStyle == 1 ? 70 : _gGameplayStyle == 2 ? 60 : 50;   /* (GAMEPLAY_DOS, _XBOX, _AMIGA) */
}

uint16_t ModApi_RandomTeam(void)
{
    static uint32_t state;
    if (!state)
#ifdef SWOS_SDL_BUILD
        state = (uint32_t)SDL_GetPerformanceCounter() | 1u;
#else
        state = ModApi_Ticks() * 2654435761u | 1u;
#endif
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

void ModApi_FinalWhistle(int32_t winner)
{
    extern TeamGame *_winningTeamPtr;
    extern TeamGame _topTeamInGame, _bottomTeamInGame;
    extern void EndOfGame(void);
    if (!_playGame || !s_mm.active || s_mm.whistled)
        return;
    if (winner != 0 && winner != 1)
        winner = _team2TotalGoals > _team1TotalGoals ? 1 : 0;
    s_mm.whistled = 1;
    s_mm.ended = 1;
    _winningTeamPtr = winner == 0 ? &_topTeamInGame : &_bottomTeamInGame;
    EndOfGame();
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
    if (!s_mm.active || s_mm.setup.playMatchScreen)
        return 0;           /* (the friendly's own: its PLAY MATCH screen, by the teams' controls) */
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

static int32_t s_restartDone;   /* (the restart, H6: once a match) */
static int32_t s_quietGoals;    /* (preloaded scorers, H7: no goal events) */
static void preload_scorers(void);

/* InitGameRestoreTeams' end (and again for a REPLAY): the score and the clock at kick-off. */
void ModMatch_KickOff(void)
{
    s_headerBy = NULL;
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
        if (m > 89) m = 89;                     /* (extra time is its own flow) */
        if (m == 45) m = 46;                    /* (45 is half time's minute: UpdateTime tests it) */
        if (m > 45) {                           /* the second half (G2): its minutes, its half */
            extern uint16_t _halfNumber;
            _halfNumber = 2;
        }
        uint8_t *t = (uint8_t *)&_gameTime;
        t[0] = 0;
        t[1] = (uint8_t)(m / 100);
        t[2] = (uint8_t)(m / 10 % 10);
        t[3] = (uint8_t)(m % 10);
        _gameSeconds = 0;
        s_restartDone = 0;
        preload_scorers();
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
    if (!s_mm.active || !(s_mm.setup.linesOut[0] | s_mm.setup.linesOut[1] | s_mm.setup.sentOff[0] |
                          s_mm.setup.sentOff[1]))
        return;
    for (int32_t t = 0; t < 2; t++) {
        Sprite **tab = t == 0 ? _team1SpritesTable : _team2SpritesTable;
        const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
        const uint8_t out = s_mm.setup.linesOut[t];
        const uint32_t off = s_mm.setup.sentOff[t];
        if ((!out && !off) || !tab || !team)
            continue;
        for (int32_t i = 1; i < 11; i++) {          /* (never the goalkeeper) */
            const int8_t pos = team->players[i].position;
            const uint8_t line = pos == POS_ATTACKER ? MODLINE_ATTACK
                               : (pos == POS_RIGHT_WING || pos == POS_LEFT_WING || pos == POS_MIDFIELDER) ? MODLINE_MIDFIELD
                               : MODLINE_DEFENCE;
            Sprite *sp = tab[i];
            const int32_t shirt = team->players[i].shirtNumber;
            const int32_t sent = shirt > 0 && shirt < 32 && (off >> shirt & 1);
            if ((!(out & line) && !sent) || !sp)
                continue;
            sp->cards = -1;
            sp->sentAway = 1;
            sp->x.s.i = sp->destX = -20;
            sp->y.s.i = sp->destY = 449;
        }
    }
}

int32_t ModMatch_NoInjuries(void) { return s_mm.active && s_mm.setup.noInjuries; }

/* kickOffAlways (H1): the team number (1, 2) that takes every kick-off, 0 the game's own. Asked by
 * PrepareForInitialKick (each period) and the restart after a goal (game_timers.c). */
int16_t ModMatch_KickOffTeam(void)
{
    if (!s_mm.active || !s_mm.setup.kickOffAlways || (s_mm.setup.kickOff != 0 && s_mm.setup.kickOff != 1))
        return 0;
    return (int16_t)(s_mm.setup.kickOff + 1);
}

/* shootoutOrder (H5): the line-up slot of `teamNumber`'s (1, 2) next penalty taker, its
 * `attempt`-th (0..) -- or 0, the game's own order. Asked by NextPenalty. */
int16_t ModMatch_ShootoutSlot(int16_t teamNumber, int16_t attempt)
{
    if (!s_mm.active || (teamNumber != 1 && teamNumber != 2) || attempt < 0)
        return 0;
    const uint8_t *order = s_mm.setup.shootoutOrder[teamNumber - 1];
    for (int32_t i = 0; i < 10; i++)
        if (order[i] < 1 || order[i] > 10)
            return 0;
    if (getenv("SWOS_MOD_EVENT_LOG") || getenv("SWOS_MOD_MATCH_TEST"))
        fprintf(stderr, "[mods] shoot-out: team %d kick %d by slot %u\n", (int)teamNumber, (int)attempt + 1,
                (unsigned)order[attempt % 10]);
    return order[attempt % 10];
}

/* manualKeeper (H4; the5ar's ManualKeeper_DiveInput, 6f423e3b): at a penalty, what the
 * defending player's goalkeeper of `team` does -- -1 the game decides, 0 he stands, else the
 * direction held (2 E, 3 SE, 5 SW, 6 W: a dive). Asked by UpdatePlayers' goalkeeper. */
int16_t ModMatch_ManualKeeper(const TeamGeneralInfo *team)
{
    extern int16_t _penalty;          /* (_playingPenalties: uint16_t, above) */
    extern Sprite *_penaltyShooterSprite;
    extern TeamGeneralInfo *_lastTeamPlayed;
    if (!s_mm.active || !s_mm.setup.manualKeeper || !team || team->playerNumber == 0)
        return -1;                      /* (the CPU's side keeps the game's keeper) */
    if (_playingPenalties) {
        /* only the DEFENDING keeper: the shooting side's runs the same update, far away */
        if (_penaltyShooterSprite && _penaltyShooterSprite->teamNumber == team->teamNumber)
            return -1;
    } else if (_penalty) {
        if (_lastTeamPlayed == team)    /* (the kicker's side: set beside _penalty at the strike) */
            return -1;
    } else {
        return -1;                      /* open play: the game's, as ever */
    }
    switch (team->direction) {
    case 2: case 3: case 5: case 6:
        return team->direction;
    default:
        return s_mm.setup.manualKeeper == 2 ? 0 : -1;
    }
}

/* pitchNumber (H7): SetPitchTypeAndNumber's end, after its random numbers -- the pitch number
 * to use (1..6), 0 the one drawn. */
int16_t ModMatch_PitchNumber(void)
{
    return s_mm.active && s_mm.setup.pitchNumber >= 1 && s_mm.setup.pitchNumber <= 6
               ? s_mm.setup.pitchNumber : 0;
}

int32_t ModApi_Restart(int32_t kind, int32_t team, int32_t wing, int16_t x, int16_t y)
{
    extern void GameTimers_ModRestart(int32_t kind, int32_t teamNumber, int32_t wing, int16_t x, int16_t y);
    if (!s_mm.active || _gameStatePl != ST_GAME_IN_PROGRESS || (team != 0 && team != 1) ||
        kind < MODRESTART_CORNER || kind > MODRESTART_PENALTY)
        return -1;
    GameTimers_ModRestart(kind, team + 1, wing, x, y);
    return 0;
}

/* restart (H6): PrepareForInitialKick's end, the FIRST kick-off only -- the break the match
 * starts in instead (game_timers.c GameTimers_ModRestart). */
void ModMatch_InitialKick(void)
{
    extern void GameTimers_ModRestart(int32_t kind, int32_t teamNumber, int32_t wing, int16_t x, int16_t y);
    if (!s_mm.active || s_restartDone || s_mm.setup.restart.kind == MODRESTART_KICK_OFF)
        return;
    s_restartDone = 1;
    GameTimers_ModRestart(s_mm.setup.restart.kind, s_mm.setup.restart.team + 1, s_mm.setup.restart.wing,
                          s_mm.setup.restart.x, s_mm.setup.restart.y);
}

/* scorers (H7): written into the scorer lists as GoalScored writes a goal, at the minute given,
 * then every flag a goal raises put back -- the score is setup.score's, set already. */
static void preload_scorers(void)
{
    extern void GoalScored(int16_t scoringTeam, Sprite *scorerSprite);
    extern Sprite **_team1SpritesTable, **_team2SpritesTable;
    extern TeamGame *_topTeamPtr, *_bottomTeamPtr;
    extern uint16_t _goalScored, _runSlower, _lastTeamScoredNumber;   /* (uint16_t[1] storage) */
    extern Sprite *_lastPlayerScored;
    extern uintptr_t _currentScorer;
    extern int16_t _penalty;
    extern TeamGeneralInfo *_lastTeamScored;
    const uint16_t goalScored = _goalScored, runSlower = _runSlower, lastNum = _lastTeamScoredNumber;
    Sprite *const lastScorer = _lastPlayerScored;
    const uintptr_t current = _currentScorer;
    TeamGeneralInfo *const lastTeam = _lastTeamScored;
    const int16_t penalty = _penalty;
    uint8_t keepTime[4];
    memcpy(keepTime, &_gameTime, 4);

    s_quietGoals = 1;                       /* (no MODEVENT_GOAL for these) */
    for (int32_t g = 0; g < 8; g++) {
        const int32_t t = s_mm.setup.scorers[g].team;
        if (t != 0 && t != 1)
            continue;
        Sprite **tab = t == 0 ? _team1SpritesTable : _team2SpritesTable;
        const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
        Sprite *scorer = NULL;
        for (int32_t i = 0; tab && team && i < 11 && !scorer; i++)
            if (tab[i] && team->players[tab[i]->playerOrdinal - 1].shirtNumber == s_mm.setup.scorers[g].shirt)
                scorer = tab[i];
        if (!scorer) {
            fprintf(stderr, "[mods] scorer: team %d has no shirt %u on the pitch\n", (int)t,
                    (unsigned)s_mm.setup.scorers[g].shirt);
            continue;
        }
        const int32_t m = s_mm.setup.scorers[g].minute > 120 ? 120 : s_mm.setup.scorers[g].minute;
        uint8_t *tm = (uint8_t *)&_gameTime;
        tm[0] = 0;
        tm[1] = (uint8_t)(m / 100);
        tm[2] = (uint8_t)(m / 10 % 10);
        tm[3] = (uint8_t)(m % 10);
        _penalty = s_mm.setup.scorers[g].penalty ? 1 : 0;
        GoalScored((int16_t)scorer->teamNumber, scorer);
    }
    s_quietGoals = 0;
    if (getenv("SWOS_MOD_MATCH_TEST") || getenv("SWOS_MOD_EVENT_LOG")) {
        extern ScorerInfo _team1Scorers[], _team2Scorers[];
        for (int32_t t = 0; t < 2; t++)
            for (int32_t i = 0; i < 8; i++) {
                const ScorerInfo *si = &(t ? _team2Scorers : _team1Scorers)[i];
                if (si->shirtNum)
                    fprintf(stderr, "[mods] scorer list %d: shirt %d, %d goal(s), first at %c%c%c\n", (int)t + 1,
                            (int)si->shirtNum, (int)si->numGoals, si->goals[0].time[1], si->goals[0].time[2],
                            si->goals[0].time[3]);
            }
    }
    memcpy(&_gameTime, keepTime, 4);
    _goalScored = goalScored;
    _runSlower = runSlower;
    _lastTeamScoredNumber = lastNum;
    _lastPlayerScored = lastScorer;
    _currentScorer = current;
    _lastTeamScored = lastTeam;
    _penalty = penalty;
}

/* G9's rules, asked where the engine decides them. */
int32_t ModMatch_QuickRestarts(void) { return s_mm.active && s_mm.setup.quickRestarts; }
int32_t ModMatch_StraightReds(void) { return s_mm.active && s_mm.setup.straightReds; }
int32_t ModMatch_NoTackles(void) { return s_mm.active && s_mm.setup.noTackles; }

/* hideHud, ghosts (G13, G5): DrawQueuedSprites skips the sprite -- the picture only, nothing the
 * match reads (its sprites stay as the match set them). */
int32_t ModMatch_SpriteHidden(const Sprite *spr)
{
    if (!s_mm.active || !(s_mm.setup.hideHud | s_mm.setup.ghosts[0] | s_mm.setup.ghosts[1]))
        return 0;
    if (s_mm.setup.hideHud & MODHUD_RESULT) {
        extern Sprite _dashSprite, _gridBottomRowLeftSprite, _gridBottomRowRightSprite,
            _gridMiddleRowLeftSprite, _gridMiddleRowRightSprite, _gridUpperRowLeftSprite,
            _gridUpperRowRightSprite, _resultAfterTheGameSprite, _resultOnHalftimeSprite,
            _team1GoalsDigit1Sprite, _team1GoalsDigit2Sprite, _team1NameSprite,
            _team1TotalGoalsDigit1Sprite, _team1TotalGoalsDigit2Sprite, _team2GoalsDigit1Sprite,
            _team2GoalsDigit2Sprite, _team2NameSprite, _team2TotalGoalsDigit1Sprite,
            _team2TotalGoalsDigit2Sprite;
        const Sprite *const kResult[] = {
            &_dashSprite, &_gridBottomRowLeftSprite, &_gridBottomRowRightSprite, &_gridMiddleRowLeftSprite,
            &_gridMiddleRowRightSprite, &_gridUpperRowLeftSprite, &_gridUpperRowRightSprite,
            &_resultAfterTheGameSprite, &_resultOnHalftimeSprite, &_team1GoalsDigit1Sprite,
            &_team1GoalsDigit2Sprite, &_team1NameSprite, &_team1TotalGoalsDigit1Sprite,
            &_team1TotalGoalsDigit2Sprite, &_team2GoalsDigit1Sprite, &_team2GoalsDigit2Sprite,
            &_team2NameSprite, &_team2TotalGoalsDigit1Sprite, &_team2TotalGoalsDigit2Sprite,
        };
        for (size_t i = 0; i < sizeof kResult / sizeof kResult[0]; i++)
            if (spr == kResult[i])
                return 1;
        /* ... the wide screen's third column of the banner's grid, and the scorer lines under it */
        extern Sprite _team1ScorerSprite[], _team2ScorerSprite[];
#ifdef SWOS_SDL_BUILD   /* (the widescreen tiles: the SDL build's) */
        extern Sprite _gridUpperRowExtraSprite, _gridMiddleRowExtraSprite, _gridBottomRowExtraSprite;
        if (spr == &_gridUpperRowExtraSprite || spr == &_gridMiddleRowExtraSprite ||
            spr == &_gridBottomRowExtraSprite)
            return 1;
#endif
        for (int32_t i = 0; i < 8; i++)
            if (spr == &_team1ScorerSprite[i] || spr == &_team2ScorerSprite[i])
                return 1;
    }
    if (s_mm.setup.hideHud & MODHUD_PLAYER_NAME) {
        extern Sprite _currentPlayerNameSprite;
        if (spr == &_currentPlayerNameSprite)
            return 1;
    }
    if ((s_mm.setup.ghosts[0] | s_mm.setup.ghosts[1]) && (spr->teamNumber == 1 || spr->teamNumber == 2)) {
        extern Sprite **_team1SpritesTable, **_team2SpritesTable;
        Sprite **tab = spr->teamNumber == 1 ? _team1SpritesTable : _team2SpritesTable;
        const uint16_t bits = s_mm.setup.ghosts[spr->teamNumber - 1];
        for (int32_t i = 0; bits && tab && i < 11; i++)
            if (tab[i] == spr)
                return bits >> i & 1;
    }
    return 0;
}

/* hideClock (H2): UpdateTime shows no clock. */
int32_t ModMatch_HideClock(void) { return s_mm.active && s_mm.setup.hideClock; }

/* A re-simulated frame (netplay): the match hooks are told only frames as first played. */
#ifdef SWOS_SDL_BUILD
int32_t ModMatch_Quiet(void) { return NetplayMatch_Resimulating(); }
#else
int32_t ModMatch_Quiet(void) { return 0; }      /* (no netplay on the Amiga: every frame is shown) */
#endif

/* Each frame of a match, after the clock. */
void ModMatch_Frame(void)
{
    if (!s_kickedOff && _gameStatePl == ST_GAME_IN_PROGRESS)
        s_kickedOff = 1;
    if (s_kickedOff && !ModMatch_Quiet())
        s_frames++;
    if (s_mm.active && s_mm.setup.clockStopped) {   /* (G7: the clock back to its start each frame) */
        int16_t m = s_mm.setup.minute < 0 ? 0 : s_mm.setup.minute > 89 ? 89 : s_mm.setup.minute;
        if (m == 45) m = 46;
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
    {
        static int32_t given;
        if (s_testHooks && s_mm.active && s_frames < 1500)
            given = 0;
        if (s_testHooks && s_mm.active && s_frames >= 1500 && !given &&
            ModApi_Restart(MODRESTART_FREE_KICK, 0, 0, 300, 640) == 0) {
            given = 1;
            fprintf(stderr, "[mods] hooks test: a free kick at frame %u\n", (unsigned)s_frames);
        }
    }
    if (s_testHooks && s_mm.active && s_frames == 3000)
        ModApi_FinalWhistle(-1);
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
    if (s_quietGoals && kind <= 2)      /* (a preloaded scorer, H7: not a goal of this match) */
        return;
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

/* A header (headers.c): who, when -- a goal of his soon after is a header (MODGOAL_HEADER). */
int32_t ModMatch_PlayerIndex(const Sprite *s);
void ModMatch_Header(const Sprite *s)
{
    if (getenv("SWOS_MOD_EVENT_LOG"))
        fprintf(stderr, "[mods] header by player %d frame %u\n", (int)ModMatch_PlayerIndex(s), (unsigned)s_frames);
    s_headerBy = s;
    s_headerAt = s_frames;
}

int32_t ModMatch_WasHeader(const Sprite *s)
{
    return s && s == s_headerBy && s_frames - s_headerAt <= 75 ? MODGOAL_HEADER : 0;
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
#ifndef SWOS_SDL_BUILD
void ModMatch_SelfTest(void) { }     /* (the tests are the SDL build's: SWOS_MOD_MATCH_TEST) */
#else
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
    if (strncmp(how, "challenge:", 10) == 0) {  /* (the challenges mod's, src/mod_challenges.c) */
        extern void ModChallenges_Test(const char *id);
        ModChallenges_Test(how + 10);
        return;
    }
    if (strncmp(how, "speedrun:", 9) == 0) {    /* (the speedrun mod's run, src/mod_speedrun.c) */
        extern void ModSpeedrun_Test(int32_t target);
        ModSpeedrun_Test(atoi(how + 9));
        return;
    }
    if (strcmp(how, "live") == 0) {           /* (LIVE CAST from a menu: src/live_cast.c) */
        ModLiveMatch m[8];
        const int32_t n = ModApi_LiveList(m, 8);
        const int32_t rc = n > 0 ? ModApi_LiveWatch(m[0].id) : -1;
        fprintf(stdout, "LIVE TEST: %d live, watched %016llx: rc %d (back in the menu)\n", (int)n,
                n > 0 ? (unsigned long long)m[0].id : 0ull, (int)rc);
        fflush(stdout);
        exit(rc == 0 ? 0 : 1);
    }
    if (strcmp(how, "entry") == 0) {
        /* (=entry: a MODES entry that plays it, for trying the hooks by hand) */
        ModApi_AddEntry(MODPOINT_MODES, "selftest", "mod match test", test_entry);
        return;
    }
    s_testShootout = strcmp(how, "shootout") == 0 || strcmp(how, "order") == 0;
    s_testTraining = strcmp(how, "training") == 0;
    s_testHooks = strcmp(how, "hooks") == 0;
    play_test_match(1);
}

static void play_test_match(int32_t thenQuit)
{
    ModMatchSetup setup;
    ModMatchResult r;
    ModApi_DefaultSetup(&setup);
    setup.team[0] = 0x5020;                 /* Norway */
    setup.team[1] = 0x5502;                 /* New Zealand */
    {   /* (SWOS_MOD_MATCH_TEAMS=<hex id>,<hex id>: others -- a team pack's, say) */
        const char *t = getenv("SWOS_MOD_MATCH_TEAMS");
        if (t && strchr(t, ',')) {
            setup.team[0] = (uint16_t)strtoul(t, NULL, 16);
            setup.team[1] = (uint16_t)strtoul(strchr(t, ',') + 1, NULL, 16);
        }
    }
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
    if (s_testHooks) {                      /* a challenge's moment: H1, H2, H5-H7 (shootout too) */
        setup.kickOffAlways = 1;
        setup.kickOff = 0;
        setup.hideClock = 1;
        setup.pitchNumber = 3;
        setup.restart.kind = MODRESTART_CORNER;
        setup.restart.team = 1;
        setup.restart.wing = 1;
        setup.scorers[0].team = 0; setup.scorers[0].shirt = 9;  setup.scorers[0].minute = 12;
        setup.scorers[1].team = 1; setup.scorers[1].shirt = 10; setup.scorers[1].minute = 30;
        setup.scorers[2].team = 0; setup.scorers[2].shirt = 7;  setup.scorers[2].minute = 35;
        setup.scorers[2].penalty = 1;
        setup.hideHud = MODHUD_RESULT | MODHUD_PLAYER_NAME;
        setup.ghosts[0] = (1u << 2) | (1u << 3);    /* (two of team 1's play, not drawn) */
    }
    if (strcmp(getenv("SWOS_MOD_MATCH_TEST"), "order") == 0)   /* takers 1..10 (the game's: 10, 9, ...) */
        for (int32_t i = 0; i < 10; i++)
            setup.shootoutOrder[0][i] = setup.shootoutOrder[1][i] = (uint8_t)(i + 1);
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
                        : s_testTraining ? r.minute == 0 && r.endedByMod && r.frames >= 3000
                        : s_testHooks ? r.endedByMod && r.frames >= 3000 && r.frames < 4500 && r.minute < 90
                        : r.minute >= 90);
    fprintf(stdout, "MOD MATCH TEST %s%s\n", s_testShootout ? "(shootout) " : s_testTraining ? "(training) " : "",
            ok ? "PASS" : "FAIL");
    fflush(stdout);
    exit(ok ? 0 : 1);
}

#endif /* SWOS_SDL_BUILD: the self-test */
