/*
 * challenge_gen.c -- the CHALLENGE GENERATOR (include/challenge_gen.h,
 * docs/CHALLENGE.md "Generating challenges").
 *
 * `SWOS --challenge-gen=spec.json` writes a .CHL nobody had to play into. The spec names
 * the teams, who is human, the goals (each with its scorer's shirt and minute), the
 * minute on the clock, and the SITUATION the position is saved in. No goal is replayed
 * to get there -- the score is ordinary match state, written the way the engine itself
 * writes it (GoalScored) -- but the POSITION is never fabricated: it is a dead-ball
 * line-up the engine builds on its own, because an arbitrary open-play arrangement
 * cannot be kept consistent by hand across the ~355 regions of the state registry.
 *
 *   situation "goal"    the kickoff after the spec's LAST goal. That goal is scored for
 *                       real: the ball is put over the line into the net the detection
 *                       code scores it from (CheckIfBallOutOfPlay's goal window,
 *                       src/game_timers.c) with the spec's scorer as _lastPlayerPlayed,
 *                       and the engine runs its own bookkeeping, celebration and
 *                       restart. Earlier goals are history by then, so they are written
 *                       as state only: GoalScored with the clock set to each one's
 *                       minute, celebration flags cleared before the frame logic sees
 *                       them.
 *   situation "corner"  a corner for the spec'd side: the ball is put just past the end
 *                       line that side attacks, last touched by the defenders, and the
 *                       engine's own out-of-play code awards it and sets it up. Every
 *                       goal is written as state.
 *
 * Both land in the same place: every restart runs the break state machine
 * (src/game_timers.c, _breakCameraMode), whose final phase sets
 * _gameStatePl = ST_WAITING_ON_PLAYER -- line-up complete, game waiting on the taker.
 * That is the capture: a human taker would wait forever (nobody is at the controls of a
 * headless run) and a computer taker waits _gsKickoffStoppage ticks, hundreds of frames
 * more than the save needs. The position is saved through the ordinary challenge
 * encoder (Challenge_SaveAs), so a generated file is the same thing a C-key capture is.
 *
 * A position past minute 45 is reached by letting the engine play its own half-time:
 * EndFirstHalf and the end-half bookkeeping are called directly; the engine runs
 * the end swap, result screen and second-half lineup. Unexpected goals fail the run.
 *
 * Every wait has a frame budget: a headless batch run must never hang, whatever state
 * the match wedges in. Kickoffs that wait on a human (the generating run has none) are
 * taken with pulsed fire input, before the engine decodes button edges.
 */
#include "challenge_gen.h"

#include "challenge.h"
#include "swos_io.h"
#include "goal_scored.h"
#include "play_match_menu.h"    /* TEAM_COMPUTER / TEAM_PLAYER_COACH */
#include "mjson.h"

#include <string.h>

#ifdef SWOS_SDL_BUILD
#include "team_db.h"
#include <stdio.h>
#include <stdlib.h>
#define GEN_LOG(...)    do { printf("[chalgen] " __VA_ARGS__); fflush(stdout); } while (0)
#define GEN_ERROR(...) fprintf(stderr, __VA_ARGS__)
#else
extern size_t strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern int memcmp(const void *a, const void *b, size_t n);
#define snprintf swos_io_snprintf
/* Rich SDL probes stay on SDL; Amiga writes a compact phase/status report below. */
#define GEN_LOG(...) do { } while (0)
#define GEN_ERROR(...) do { } while (0)
#endif

/* ---- the globals a friendly is set up from (same set src/challenge.c snapshots) ---- */

extern uint16_t   _numSelectedTeams;
extern uint8_t    _teamFileBuffer[];
extern int16_t    LoadTeamFile(uint16_t extNumber);

extern uint16_t _pitchTypeOrSeason, _friendlyPitchType, _friendlySeason, _pitchType;
extern uint16_t _gameLength, _autoReplays, _autoSaveHighlights;
extern uint16_t _allPlayerTeamsEqual, _minSubstitutes, _maxSubstitutes;
extern uint16_t _gameType, _trainingGame, _isCareer, _twoPlayers;
extern uint16_t _pl1Coach, _pl2Coach;
extern uint16_t _topTeamPlayerNo, _topTeamCoachNo;
extern uint16_t _bottomTeamPlayerNo, _bottomTeamCoachNo;
extern int16_t  _pl1Tactics, _pl2Tactics;
extern uint16_t _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;

extern void InitUserTactics(void);
extern void InitTeams(void);
extern void InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);
extern uint16_t  _menuBGImageCmdStream[];
extern uint8_t  *_menuImageStreamBaseEnd;
extern void      ProcessImageCommandStream(uint16_t *script, uint8_t *base);

/* ---- the match state the spec is written into ---- */

extern volatile uint16_t _playGame;
extern uint16_t _gameState, _gameStatePl, _breakState;
extern uint16_t _halfNumber;
extern uint8_t  _gameTime[4];
extern uint16_t _gameSeconds, _secondsSwitchAccumulator, _copyTimeSprite;
extern uint16_t _endGameCounter;
extern uint16_t _team1TotalGoals, _team2TotalGoals;
extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t _statsTeam1GoalsCopy, _statsTeam2GoalsCopy;
extern uint16_t _statsTeam1GoalsCopy2, _statsTeam2GoalsCopy2;
extern uint16_t _goalScored, _runSlower;
extern uintptr_t _currentScorer;
extern Sprite   _ballSprite;
extern Sprite  *_lastPlayerPlayed;
extern Sprite  *_lastKeeperPlayed;
extern uint16_t _playerHadBall;
extern TeamGeneralInfo *_lastTeamPlayed, *_lastTeamPlayedBeforeBreak;
extern TeamGeneralInfo  _topTeamData, _bottomTeamData;
extern uint16_t _extraTimeState, _penaltiesState;
extern void BumpPlayersLastPlayedHalfAtHalfStart(void);

/* ---- the spec ---- */

#define GEN_MAX_GOALS   16

typedef struct {
    int32_t side;           /* 1 or 2 */
    int32_t shirt;          /* the scorer's shirt number, as the team sheet shows it */
    int32_t minute;
} GenGoal;

enum { SIT_GOAL, SIT_CORNER };

typedef struct {
    char     team1[40], team2[40];  /* a team name, or a uuid */
    int32_t  human;                 /* 1 or 2 */
    GenGoal  goals[GEN_MAX_GOALS];
    int32_t  numGoals;
    int32_t  minute;                /* the clock the position is saved at, 1..89 */
    int32_t  extraTime;             /* a level score at 90 goes to extra time */
    int32_t  penalties;             /* ...and then penalties */
    int32_t  situation;             /* SIT_* */
    int32_t  ballSide;              /* the side with the ball at the position; 0 = unset */
    int32_t  cornerFor;             /* SIT_CORNER: the side that takes it */
    int32_t  cornerLeftWing;        /* SIT_CORNER: 1 = the left wing as drawn */
    int32_t  pitch;                 /* -1 = the friendly default (random) */
    char     out[40];               /* "" = the next free CHALnn.CHL */
} GenSpec;

static GenSpec s_spec;
static int32_t s_armed;
static int32_t s_finished;
static const char *s_error;

#ifndef SWOS_SDL_BUILD
static int32_t s_fire;
int32_t ChallengeGen_Fire(void) { return s_fire; }

static void amiga_status(const char *status, const char *reason, int32_t phase)
{
    char buf[512];
    int n = swos_io_snprintf(buf, sizeof buf,
        "%s: %s\nphase %d, state %u, player state %u, half %u, score %u:%u\n",
        status, reason, (int)phase, (unsigned)_gameState, (unsigned)_gameStatePl,
        (unsigned)_halfNumber, (unsigned)_team1TotalGoals, (unsigned)_team2TotalGoals);
    swos_io_save((const uint8_t *)"data/CHALGEN.LOG", (const uint8_t *)buf,
                 (uint32_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
}
#endif

static void gen_finish(int32_t code)
{
    s_finished = 1;
#ifdef SWOS_SDL_BUILD
    fflush(stdout);
    exit(code);
#else
    extern uint16_t _quitToDos;
    (void)code;
    s_fire = 0;
    _playGame = 0;
    _quitToDos = 1;   /* unwind normally: startShim restores the Amiga hardware */
#endif
}

int32_t ChallengeGen_Armed(void) { return s_armed; }

/* ---- parsing ---- */

static int32_t spec_fail(const char *why)
{
    s_error = why;
    GEN_ERROR("--challenge-gen: %s\n", why);
    return 1;
}

/* The known top-level keys. An unknown key is fatal, like an unknown flag: a typo that
 * silently did nothing would be read as "the generator ignored my spec". */
static const char *k_specKeys[] = {
    "team1", "team2", "human", "scorers", "score", "minute",
    "extra_time", "penalties", "situation", "ball", "corner", "out", "pitch", NULL
};

static int32_t parse_spec(const char *buf, int32_t len)
{
    long d;
    char tmp[40];
    int koff, klen, voff, vlen, vtype, off;
    int32_t i;

    for (off = 0; (off = mjson_next(buf, len, off, &koff, &klen, &voff, &vlen, &vtype)) != 0; ) {
        const char **k = k_specKeys;
        for (; *k; k++)
            if ((int)strlen(*k) == klen - 2 && memcmp(buf + koff + 1, *k, (size_t)(klen - 2)) == 0)
                break;
        if (!*k) {
            GEN_ERROR("--challenge-gen: unknown key %.*s in the spec\n", klen, buf + koff);
            return 1;
        }
    }

    if (mjson_get_string(buf, len, "$.team1", s_spec.team1, sizeof s_spec.team1) <= 0)
        return spec_fail("the spec needs \"team1\" (a team name or a uuid)");
    if (mjson_get_string(buf, len, "$.team2", s_spec.team2, sizeof s_spec.team2) <= 0)
        return spec_fail("the spec needs \"team2\" (a team name or a uuid)");

    s_spec.human = 1;
    if (mjson_get_long(buf, len, "$.human", &d)) {
        s_spec.human = (int32_t)d;
        if (s_spec.human != 1 && s_spec.human != 2)
            return spec_fail("\"human\" must be 1 or 2");
    }

    if (!mjson_get_long(buf, len, "$.minute", &d))
        return spec_fail("the spec needs \"minute\" (1..89)");
    s_spec.minute = (int32_t)d;
    if (s_spec.minute < 1 || s_spec.minute > 89)
        return spec_fail("\"minute\" must be 1..89 (extra time is not generated yet)");

    s_spec.numGoals = 0;
    for (i = 0; i < GEN_MAX_GOALS; i++) {
        char path[48];
        GenGoal *g = &s_spec.goals[i];
        snprintf(path, sizeof path, "$.scorers[%d].side", (int)i);
        if (!mjson_get_long(buf, len, path, &d))
            break;
        g->side = (int32_t)d;
        snprintf(path, sizeof path, "$.scorers[%d].shirt", (int)i);
        if (!mjson_get_long(buf, len, path, &d))
            return spec_fail("every scorer needs \"shirt\"");
        g->shirt = (int32_t)d;
        snprintf(path, sizeof path, "$.scorers[%d].minute", (int)i);
        if (!mjson_get_long(buf, len, path, &d))
            return spec_fail("every scorer needs \"minute\"");
        g->minute = (int32_t)d;
        if (g->side != 1 && g->side != 2)
            return spec_fail("a scorer's \"side\" must be 1 or 2");
        if (g->shirt < 1 || g->shirt > 99)
            return spec_fail("a scorer's \"shirt\" must be 1..99");
        if (g->minute < 1 || g->minute > s_spec.minute)
            return spec_fail("a goal's \"minute\" must be 1..the spec's minute");
        s_spec.numGoals++;
    }
    {
        char path[48];
        snprintf(path, sizeof path, "$.scorers[%d].side", (int)GEN_MAX_GOALS);
        if (mjson_get_long(buf, len, path, &d))
            return spec_fail("too many scorers (16 is the most a side's table holds)");
    }

    /* "score" is a cross-check, never a second source: the goals ARE the score. */
    if (mjson_get_long(buf, len, "$.score[0]", &d)) {
        int32_t want1 = (int32_t)d, want2 = 0, have1 = 0, have2 = 0;
        if (mjson_get_long(buf, len, "$.score[1]", &d))
            want2 = (int32_t)d;
        for (i = 0; i < s_spec.numGoals; i++) {
            if (s_spec.goals[i].side == 1) have1++; else have2++;
        }
        if (want1 != have1 || want2 != have2) {
            GEN_ERROR("--challenge-gen: \"score\" says %d:%d but the scorers add up "
                            "to %d:%d -- the scorers are the score, fix one of them\n",
                    (int)want1, (int)want2, (int)have1, (int)have2);
            return 1;
        }
    }

    /* A challenge wants deciding: a level score at 90 plays extra time and penalties
     * unless the spec says otherwise. The friendly's own options, not a cup's -- the
     * whistle code reads only these two globals (src/game_state.c, minute 90). */
    s_spec.extraTime = 1;
    s_spec.penalties = 1;
    {
        int b;
        if (mjson_get_bool(buf, len, "$.extra_time", &b)) s_spec.extraTime = b ? 1 : 0;
        if (mjson_get_bool(buf, len, "$.penalties", &b))  s_spec.penalties = b ? 1 : 0;
    }

    s_spec.ballSide = 0;
    if (mjson_get_long(buf, len, "$.ball", &d)) {
        s_spec.ballSide = (int32_t)d;
        if (s_spec.ballSide != 1 && s_spec.ballSide != 2)
            return spec_fail("\"ball\" must be 1 or 2 (the side with the ball)");
    }

    s_spec.situation = s_spec.numGoals ? SIT_GOAL : SIT_CORNER;
    if (mjson_get_string(buf, len, "$.situation", tmp, sizeof tmp) > 0) {
        if (strcmp(tmp, "goal") == 0)         s_spec.situation = SIT_GOAL;
        else if (strcmp(tmp, "corner") == 0)  s_spec.situation = SIT_CORNER;
        else return spec_fail("\"situation\" must be \"goal\" or \"corner\"");
    }
    if (s_spec.situation == SIT_GOAL && s_spec.numGoals == 0)
        return spec_fail("situation \"goal\" needs at least one scorer; a goalless "
                         "position must be a \"corner\" for now");

    /* A kickoff restart always belongs to the side that CONCEDED -- that is the game's
     * own rule -- so "ball" for a side means restarting after the other side's latest
     * goal, which has to exist. */
    if (s_spec.situation == SIT_GOAL && s_spec.ballSide) {
        for (i = 0; i < s_spec.numGoals; i++)
            if (s_spec.goals[i].side != s_spec.ballSide)
                break;
        if (i == s_spec.numGoals)
            return spec_fail("\"ball\" for that side needs a goal by the OTHER side to "
                             "restart after (a kickoff goes to the side that conceded); "
                             "use a \"corner\" instead");
    }

    s_spec.cornerFor = s_spec.ballSide ? s_spec.ballSide : s_spec.human;
    s_spec.cornerLeftWing = 1;
    if (s_spec.situation == SIT_CORNER) {
        if (mjson_get_long(buf, len, "$.corner.for", &d)) {
            if (s_spec.ballSide && (int32_t)d != s_spec.ballSide)
                return spec_fail("\"ball\" and \"corner\".\"for\" disagree -- they are "
                                 "the same choice, give one");
            s_spec.cornerFor = (int32_t)d;
            if (s_spec.cornerFor != 1 && s_spec.cornerFor != 2)
                return spec_fail("\"corner\".\"for\" must be 1 or 2");
        }
        if (mjson_get_string(buf, len, "$.corner.wing", tmp, sizeof tmp) > 0) {
            if (strcmp(tmp, "left") == 0)       s_spec.cornerLeftWing = 1;
            else if (strcmp(tmp, "right") == 0) s_spec.cornerLeftWing = 0;
            else return spec_fail("\"corner\".\"wing\" must be \"left\" or \"right\"");
        }
    } else if (mjson_find(buf, len, "$.corner", NULL, NULL) != MJSON_TOK_INVALID) {
        return spec_fail("\"corner\" only goes with situation \"corner\"");
    }

    s_spec.out[0] = 0;
    if (mjson_get_string(buf, len, "$.out", s_spec.out, sizeof s_spec.out) > 0) {
        size_t n = strlen(s_spec.out);
        if (n < 5 || (strcmp(s_spec.out + n - 4, ".CHL") != 0 &&
                      strcmp(s_spec.out + n - 4, ".chl") != 0))
            return spec_fail("\"out\" must end in .CHL (the CHALLENGES picker lists those)");
    }

    s_spec.pitch = -1;
    if (mjson_get_long(buf, len, "$.pitch", &d))
        s_spec.pitch = (int32_t)d;

    /* The goals are applied in minute order whatever order the spec wrote them in. */
    for (i = 1; i < s_spec.numGoals; i++) {
        GenGoal g = s_spec.goals[i];
        int32_t j = i;
        while (j > 0 && s_spec.goals[j - 1].minute > g.minute) {
            s_spec.goals[j] = s_spec.goals[j - 1];
            j--;
        }
        s_spec.goals[j] = g;
    }
    return 0;
}

int32_t ChallengeGen_Request(const char *specPath)
{
    char *buf;
    uint32_t n;
#ifdef SWOS_SDL_BUILD
    FILE *f = fopen(specPath, "rb");
    long size;
    if (!f) return spec_fail("cannot read the spec file");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        fclose(f);
        return spec_fail("the spec file is empty or implausibly large");
    }
    n = (uint32_t)size;
    buf = swos_io_alloc(n);
    if (!buf || fread(buf, 1, n, f) != n) {
        fclose(f);
        swos_io_free(buf);
        return spec_fail("the spec file would not read");
    }
    fclose(f);
#else
    if (swos_io_load_data_alloc(specPath, (uint8_t **)&buf, &n) != 0)
        return spec_fail("cannot read the spec file");
    if (!n || n > 65536) {
        swos_io_free(buf);
        return spec_fail("the spec file is empty or implausibly large");
    }
#endif
    memset(&s_spec, 0, sizeof s_spec);
    s_error = "invalid spec (unknown key or score mismatch)";
    if (mjson(buf, (int)n, NULL, NULL) < 0 || parse_spec(buf, (int32_t)n) != 0) {
        swos_io_free(buf);
        return 1;
    }
    swos_io_free(buf);
    s_armed = 1;
    return 0;
}

#ifndef SWOS_SDL_BUILD
int32_t ChallengeGen_Boot(void)
{
    uint8_t *buf;
    uint32_t size;
    /* Size/load probes return on absence, unlike LoadFileEx's retry-disk loop. */
    if (swos_io_load_data_alloc("data/CHALGEN.DAT", &buf, &size) != 0)
        return 0;
    swos_io_free(buf);
    if (ChallengeGen_Request("data/CHALGEN.DAT") != 0) {
        amiga_status("FAILED", s_error, -1);
        return -1;
    }
    { extern volatile uint8_t _skipLoaderIntro; _skipLoaderIntro = 1; }
    amiga_status("RUNNING", "spec loaded", -1);
    return 1;
}
#endif

/* ---- resolving the teams ---- */

static int32_t looks_like_uuid(const char *s)
{
    return strlen(s) == 36 && s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-';
}

static int32_t name_eq(const int8_t *teamName, const char *want)
{
    int32_t i;
    for (i = 0; i < 17 && want[i]; i++) {
        char a = (char)teamName[i], b = want[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b)
            return 0;
    }
    return i < 17 && teamName[i] == 0;
}

/* The team a spec string names, resolved against the team database -- and then taken
 * from LoadTeamFile, so the record is the one the game itself would hand the friendly
 * (global numbers set; the pattern of custom_team.c's game_team). */
static int32_t resolve_team(const char *what, TeamFileEx *out)
{
#ifdef SWOS_SDL_BUILD
    const TeamDbEntry *found = NULL;
    int32_t country, index;

    if (looks_like_uuid(what)) {
        found = TeamDb_ByUuid(what);
        if (!found) {
            GEN_ERROR("[chalgen] no team in the database has the id %s\n", what);
            return 1;
        }
    } else {
        for (country = 0; country < 256 && !found; country++) {
            for (index = 0; ; index++) {
                const TeamDbEntry *e = TeamDb_Entry(country, index);
                if (!e)
                    break;
                if (!name_eq(e->team.team.teamName, what))
                    continue;
                if (found) {
                    GEN_ERROR("[chalgen] more than one team is named %s -- say which "
                                    "with its uuid:\n  %s (team file %u, team %u)\n  %s "
                                    "(team file %u, team %u)\n", what,
                            found->ids.uuid, (unsigned)found->team.team.countryNumber,
                            (unsigned)found->team.team.teamOrdinal,
                            e->ids.uuid, (unsigned)e->team.team.countryNumber,
                            (unsigned)e->team.team.teamOrdinal);
                    return 1;
                }
                found = e;
            }
        }
        /* Country numbers above the club files; TeamDb_Entry covered them in the loop
         * above because it is indexed by the same numbers. */
        if (!found) {
            GEN_ERROR("[chalgen] no team in the database is named %s\n", what);
            return 1;
        }
    }

    {
        const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2);
        uint16_t count, i;
        if (LoadTeamFile((uint16_t)found->team.team.countryNumber) != 0) {
            GEN_ERROR("[chalgen] TEAM.%03u would not load\n",
                    (unsigned)found->team.team.countryNumber);
            return 1;
        }
        count = *(uint16_t *)_teamFileBuffer;
        for (i = 0; i < count; i++)
            if (rec[i].team.countryNumber == found->team.team.countryNumber &&
                rec[i].team.teamOrdinal == found->team.team.teamOrdinal) {
                *out = rec[i];
                return 0;
            }
    }
    GEN_ERROR("[chalgen] %s is in the database but not in its team file\n", what);
    return 1;
#else
    int32_t country, found = 0;
    if (looks_like_uuid(what))
        return spec_fail("Amiga teams use names or TEAM.nnn:ordinal, not database UUIDs");
    for (country = 0; country < 256; country++) {
        char path[] = "data/TEAM.000";
        uint8_t *buf;
        uint32_t size;
        uint16_t count, i;
        path[10] = (char)('0' + country / 100);
        path[11] = (char)('0' + country / 10 % 10);
        path[12] = (char)('0' + country % 10);
        if (swos_io_load_data_alloc(path, &buf, &size) != 0)
            continue;
        swos_io_free(buf);
        /* TEAM files may be RNC packed: the raw probe only establishes existence.
         * The ordinary team loader decompresses and assigns global team numbers. */
        if (country == 100) continue;  /* custom teams use CUSTOMS.EDT, not TEAM.100 */
        if (LoadTeamFile((uint16_t)country) != 0)
            return spec_fail("native team could not load");
        count = *(uint16_t *)_teamFileBuffer;
        for (i = 0; i < count; i++) {
            const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2);
            char id[40];
            swos_io_snprintf(id, sizeof id, "TEAM.%s:%u", path + 10,
                             (unsigned)rec[i].team.teamOrdinal);
            if (!name_eq(rec[i].team.teamName, what) && strcmp(id, what) != 0)
                continue;
            if (found) {
                return spec_fail("ambiguous team name; use TEAM.nnn:ordinal");
            }
            *out = rec[i];
            found = 1;
        }
    }
    return found ? 0 : spec_fail("team not found in the Amiga TEAM files");
#endif
}

/* ---- the control assignment (the SetupPlayers bypass) ---- */

void ChallengeGen_SetupPlayers(TeamFileEx *team1, TeamFileEx *team2)
{
    /* What the two ShowPlayMatchMenu prompts would have produced for one human against
     * the computer, hard-wired: the one joystick is player 1, whichever side it has. */
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;
    _twoPlayers = 0;
    _pl1Coach = 0;
    _pl2Coach = 0;
    _topTeamPlayerNo    = (team1->team.teamControls != TEAM_COMPUTER) ? 1 : 0;
    _topTeamCoachNo     = 0;
    _bottomTeamPlayerNo = (team2->team.teamControls != TEAM_COMPUTER) ? 1 : 0;
    _bottomTeamCoachNo  = 0;
    _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
    _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
}

/* ---- the run ---- */

void ChallengeGen_Run(void)
{
    if (resolve_team(s_spec.team1, &_selectedTeams[0]) != 0 ||
        resolve_team(s_spec.team2, &_selectedTeams[1]) != 0)
    {
#ifndef SWOS_SDL_BUILD
        amiga_status("FAILED", s_error, -1);
#endif
        gen_finish(1);
        return;
    }

#ifndef SWOS_SDL_BUILD
    amiga_status("RUNNING", "teams resolved", -2);
#endif
    _selectedTeams[0].team.teamControls =
        (s_spec.human == 1) ? TEAM_PLAYER_COACH : TEAM_COMPUTER;
    _selectedTeams[1].team.teamControls =
        (s_spec.human == 2) ? TEAM_PLAYER_COACH : TEAM_COMPUTER;
    _numSelectedTeams = 2;

    /* The friendly defaults (ShowFriendlyMenu's), spelled out because the front end
     * that would set them was skipped. Auto replays stay off: an instant replay is a
     * loop of its own and the generator wants the frame hook running every frame. */
    _pitchTypeOrSeason   = 0;
    _friendlyPitchType   = (s_spec.pitch >= 0) ? (uint16_t)s_spec.pitch : 0xFFFF;
    _pitchType           = _friendlyPitchType;
    _friendlySeason      = 7;
    _gameLength          = 0;
    _autoReplays         = 0;
    _autoSaveHighlights  = 0;
    _minSubstitutes      = 2;
    _maxSubstitutes      = 5;
    _gameType            = 0;
    _trainingGame        = 0;
    _isCareer            = 0;
    _allPlayerTeamsEqual = 0;
    InitUserTactics();

    /* The front end was skipped, so the menu sprites the stadium line-up draws were
     * never extracted -- the same two lines every rig runs (replay_test.c). */
    ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    InitTeams();

    GEN_LOG("%.16s v %.16s, human side %d, %d goal%s, minute %d, %s\n",
            (const char *)_selectedTeams[0].team.teamName,
            (const char *)_selectedTeams[1].team.teamName,
            (int)s_spec.human, (int)s_spec.numGoals, s_spec.numGoals == 1 ? "" : "s",
            (int)s_spec.minute,
            s_spec.situation == SIT_GOAL ? "saved at the kickoff after the last goal"
                                         : "saved at a corner");

#ifndef SWOS_SDL_BUILD
    amiga_status("RUNNING", "starting friendly", 0);
#endif
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);

    if (s_finished) return;

    /* Only if the match never reached its loop. */
    GEN_LOG("the match never started\n");
#ifndef SWOS_SDL_BUILD
    amiga_status("FAILED", "match ended before capture", -1);
#endif
    gen_finish(2);
}

/* ---- the per-frame hook ---- */

enum {
    PH_WAIT_PLAY,       /* kick-off through to the first ball in play */
    PH_WAIT_HALF2,      /* the engine playing its own half time */
    PH_WAIT_LINEUP,     /* the restart line-up building after the goal / corner */
    PH_SETTLE,
    PH_DONE
};

static int32_t  s_phase = PH_WAIT_PLAY;
static uint32_t s_phaseFrames;
static int32_t  s_settleLeft;

static void gen_fail(const char *why)
{
    GEN_LOG("FAILED: %s (phase %d, gameState %u, gameStatePl %u, half %u, "
            "score %u:%u, %lu frames in this phase)\n",
            why, (int)s_phase, (unsigned)_gameState, (unsigned)_gameStatePl,
            (unsigned)_halfNumber, (unsigned)_team1TotalGoals, (unsigned)_team2TotalGoals,
            (unsigned long)s_phaseFrames);
#ifndef SWOS_SDL_BUILD
    amiga_status("FAILED", why, s_phase);
#endif
    gen_finish(1);
}

static void set_phase(int32_t phase)
{
    s_phase = phase;
    s_phaseFrames = 0;
#ifndef SWOS_SDL_BUILD
    amiga_status("RUNNING", "driving match", phase);
#endif
}

/* A restart waiting on the human is taken for them: nobody is at the controls of a
 * headless run. The press goes in where every real press does -- the fire bit of the
 * joystick register the frame logic reads after this hook (what MatchStep_ApplyInput
 * writes for the rigs) -- and PULSED, so an edge detector sees press and release.
 * Never called in the capture phase, whose whole point is a position WAITING on it. */
static void press_fire_for_waiting_human(void)
{
#ifdef SWOS_SDL_BUILD
    extern uint8_t _ciaa_pra;
#endif
    /* A restart waiting on the taker, and the half-time result screen, which also
     * stays up for the human's button. */
    if (_gameStatePl != ST_WAITING_ON_PLAYER && _gameState != ST_RESULT_ON_HALFTIME)
        return;
#ifndef SWOS_SDL_BUILD
    s_fire = ((s_phaseFrames >> 3) & 1) == 0;
#else
    if (((s_phaseFrames >> 3) & 1) == 0)
        _ciaa_pra &= (uint8_t)~(1u << 7);       /* player 1's button, both sides' human */
#endif
}

static void set_clock(int32_t minute)
{
    _gameTime[0] = 0;
    _gameTime[1] = (uint8_t)(minute / 100);
    _gameTime[2] = (uint8_t)((minute / 10) % 10);
    _gameTime[3] = (uint8_t)(minute % 10);
    _gameSeconds = 0;
    _secondsSwitchAccumulator = 0;
    _copyTimeSprite = 1;        /* the clock sprite is rebuilt from the digits */
}

/* The TeamGeneralInfo of logical team 1 or 2. "Top" and "bottom" are screen positions
 * and can be either team, so the teamNumber field decides (as handleGoalScored does). */
static TeamGeneralInfo *side_data(int32_t side)
{
    return (_topTeamData.teamNumber == side) ? &_topTeamData : &_bottomTeamData;
}

static Sprite *scorer_sprite(const GenGoal *g)
{
    TeamGeneralInfo *td = side_data(g->side);
    TeamGame *tg = (TeamGame *)td->inGameTeamPtr;
    int32_t i;
    for (i = 0; i < 11; i++) {
        Sprite *s = td->spritesTable[i];
        int32_t idx = s->playerOrdinal - 1;
        if (idx >= 0 && idx < 16 && tg->players[idx].shirtNumber == (int8_t)g->shirt &&
            !s->sentAway)
            return s;
    }
    return NULL;
}

/* A goal that is already history when the position is saved: the same bookkeeping the
 * engine runs when the ball crosses the line (GoalScored: totals, stats, digit words,
 * the scorers table with the minute, the name sprites), with the celebration flags it
 * raises taken straight back down -- this hook runs before the frame's logic, so the
 * engine never sees them and no restart is played for it. */
static int32_t apply_past_goal(const GenGoal *g)
{
    Sprite *scorer = scorer_sprite(g);
    if (!scorer)
        return 1;
    set_clock(g->minute);
    GoalScored((int16_t)g->side, scorer);
    _goalScored = 0;
    _runSlower = 0;
    _currentScorer = 0;
    _statsTeam1GoalsCopy  = _statsTeam1Goals;   /* the sync the ESC path does */
    _statsTeam2GoalsCopy  = _statsTeam2Goals;
    _statsTeam1GoalsCopy2 = _statsTeam1Goals;
    _statsTeam2GoalsCopy2 = _statsTeam2Goals;
    return 0;
}

/* The LAST goal is scored for real: the ball into the net CheckIfBallOutOfPlay scores
 * this side from (its goal window: x 302..366, low z, past the end line; a ball above
 * the centerline is a goal for the side _bottomTeamData holds -- src/game_timers.c),
 * the scorer as _lastPlayerPlayed, which is where handleGoalScored reads the scorer
 * from. The engine does everything else: bookkeeping, celebration, the restart. */
static int32_t score_final_goal(const GenGoal *g)
{
    Sprite *scorer = scorer_sprite(g);
    if (!scorer)
        return 1;
    set_clock(g->minute);
    _lastKeeperPlayed = (Sprite *)0;    /* no deflection: the goal is the scorer's */
    _lastPlayerPlayed = scorer;
    _lastTeamPlayed   = side_data(g->side);
    _playerHadBall    = 0;
    _ballSprite.x.s.i = 336; _ballSprite.x.s.frac = 0;
    _ballSprite.y.s.i = (int16_t)((g->side == _bottomTeamData.teamNumber) ? 125 : 773);
    _ballSprite.y.s.frac = 0;
    _ballSprite.z.raw = 0;
    _ballSprite.deltaX.raw = 0;
    _ballSprite.deltaY.raw = 0;
    _ballSprite.deltaZ.raw = 0;
    _ballSprite.speed = 0;
    return 0;
}

/* A corner for cornerFor: the ball just past the end line that side attacks, last
 * touched by the defenders -- CheckIfBallOutOfPlay then awards and places it (ball past
 * the top line, last played by _topTeamData = corner for the bottom side, and the
 * mirror of that past the bottom line; src/game_timers.c:701,743). */
static void arrange_corner(void)
{
    TeamGeneralInfo *taker = side_data(s_spec.cornerFor);
    TeamGeneralInfo *conceding = (taker == &_topTeamData) ? &_bottomTeamData : &_topTeamData;

    _lastTeamPlayed = conceding;
    _lastPlayerPlayed = conceding->spritesTable[2];     /* any defender */
    _playerHadBall = 0;
    _ballSprite.x.s.i = (int16_t)(s_spec.cornerLeftWing ? 100 : 580);
    _ballSprite.x.s.frac = 0;
    _ballSprite.y.s.i = (int16_t)((conceding == &_topTeamData) ? 120 : 778);
    _ballSprite.y.s.frac = 0;
    _ballSprite.z.raw = 0;
    _ballSprite.deltaX.raw = 0;
    _ballSprite.deltaY.raw = 0;
    _ballSprite.deltaZ.raw = 0;
    _ballSprite.speed = 0;
}

/* Everything the position needs once the match is in the right half: the score, then
 * the situation whose line-up it is saved in. One frame, before the frame's logic. */
static void apply_spec(void)
{
    int32_t i, restartAfter = s_spec.numGoals - 1;

    /* The tie rules, set AFTER the init -- which clears them (game_init.c's cup-scalar
     * reset) -- so the setup block saved with the position carries them and a loaded
     * challenge level at 90 goes to extra time and penalties. */
    _extraTimeState = (uint16_t)s_spec.extraTime;
    _penaltiesState = (uint16_t)s_spec.penalties;

    if (s_spec.minute > 45)
        BumpPlayersLastPlayedHalfAtHalfStart();     /* the minute-46 bump */

    /* The goal the position restarts after: the last one -- unless the spec says which
     * side has the ball, which means the OTHER side's latest goal (parse_spec proved
     * one exists), every later goal being history by then anyway. */
    if (s_spec.situation == SIT_GOAL && s_spec.ballSide) {
        for (i = s_spec.numGoals - 1; i >= 0; i--)
            if (s_spec.goals[i].side != s_spec.ballSide) {
                restartAfter = i;
                break;
            }
    }

    for (i = 0; i < s_spec.numGoals; i++) {
        if (s_spec.situation == SIT_GOAL && i == restartAfter)
            continue;
        if (apply_past_goal(&s_spec.goals[i])) {
            gen_fail("a scorer's shirt number is not in that side's eleven");
            return;
        }
    }

    if (s_spec.situation == SIT_GOAL) {
        if (score_final_goal(&s_spec.goals[restartAfter])) {
            gen_fail("the restart goal's shirt number is not in that side's eleven");
            return;
        }
    } else {
        arrange_corner();
    }
    set_phase(PH_WAIT_LINEUP);
}

void ChallengeGen_Frame(void)
{
#ifndef SWOS_SDL_BUILD
    s_fire = 0;
#endif
    if (!s_armed || s_finished || s_phase == PH_DONE)
        return;
    if (++s_phaseFrames > 30000) {
        gen_fail("the match never reached this phase's state");
        return;
    }
#ifndef SWOS_SDL_BUILD
    if ((s_phaseFrames % 500) == 0)
        amiga_status("RUNNING", "waiting for restart/half-time", s_phase);
#endif
    if ((s_phaseFrames % 5000) == 0) {
        extern uint16_t _breakCameraMode, _stoppageEventTimer, _lastFrameTicks;
        extern uint16_t _cameraCoordinatesValid, _whichCard, _inSubstitutesMenu;
        GEN_LOG("waiting (phase %d): playGame %u, gameState %u, gameStatePl %u, half %u, "
                "ball %d,%d, breakCam %u, stoppage %u, ticks %u, camValid %u, card %u, subs %u\n",
                (int)s_phase, (unsigned)_playGame, (unsigned)_gameState,
                (unsigned)_gameStatePl, (unsigned)_halfNumber,
                (int)_ballSprite.x.s.i, (int)_ballSprite.y.s.i,
                (unsigned)_breakCameraMode, (unsigned)_stoppageEventTimer,
                (unsigned)_lastFrameTicks, (unsigned)_cameraCoordinatesValid,
                (unsigned)_whichCard, (unsigned)_inSubstitutesMenu);
    }

    switch (s_phase) {
    case PH_WAIT_PLAY:
        press_fire_for_waiting_human();
        if (!_playGame || _gameStatePl != ST_GAME_IN_PROGRESS ||
            _gameState != ST_GAME_IN_PROGRESS)
            return;
        if (s_spec.minute > 45) {
            /* The half-time whistle, blown HERE rather than left to the clock: the body
             * of UpdateTime's minute-45 branch (src/game_state.c), which leaves the
             * engine to play the whole break itself -- end swap, _halfNumber, walk-out.
             * Waiting for the clock to do it gave the computer open play at minute 45
             * (the last-minute possession extension), long enough to score in. */
            extern void EndFirstHalf(void);
            extern void BumpPlayersLastPlayedHalfAtHalfEnd(void);
            set_clock(45);
            EndFirstHalf();
            BumpPlayersLastPlayedHalfAtHalfEnd();
            set_phase(PH_WAIT_HALF2);
            return;
        }
        apply_spec();
        return;

    case PH_WAIT_HALF2:
        press_fire_for_waiting_human();
        {
            /* Any goal in this window corrupts the spec's score; say WHEN it happened. */
            static uint32_t s_sawGoals;
            uint32_t now = ((uint32_t)_team1TotalGoals << 16) | _team2TotalGoals;
            if (now != s_sawGoals) {
                GEN_LOG("goal during half-time fast-forward: %u:%u at phase frame %lu, "
                        "gameState %u, gameStatePl %u, half %u\n",
                        (unsigned)_team1TotalGoals, (unsigned)_team2TotalGoals,
                        (unsigned long)s_phaseFrames, (unsigned)_gameState,
                        (unsigned)_gameStatePl, (unsigned)_halfNumber);
                s_sawGoals = now;
            }
        }
        if (_halfNumber != 2 || _gameStatePl != ST_GAME_IN_PROGRESS ||
            _gameState != ST_GAME_IN_PROGRESS)
            return;
        if (_team1TotalGoals || _team2TotalGoals) {
            gen_fail("the computer scored during the half-time fast-forward -- run it again");
            return;
        }
        apply_spec();
        return;

    case PH_WAIT_LINEUP:
        /* Every restart's break machine ends here: line-up complete, waiting on the
         * taker. A human taker waits forever and a computer one for hundreds of ticks,
         * so the position is stable for as long as the save needs. */
        if (_gameStatePl != ST_WAITING_ON_PLAYER)
            return;
        set_clock(s_spec.minute);
        s_settleLeft = 3;
        set_phase(PH_SETTLE);
        return;

    case PH_SETTLE:
        if (--s_settleLeft > 0)
            return;
        {
            int32_t rc = Challenge_SaveAs(s_spec.out[0] ? (const uint8_t *)s_spec.out
                                                        : (const uint8_t *)0);
            if (rc != 0) {
                gen_fail("the position would not save (the [challenge] line above says why)");
                return;
            }
            GEN_LOG("done: %s, %u:%u at minute %d, %s\n",
                    s_spec.out[0] ? s_spec.out : "the next free CHALnn.CHL",
                    (unsigned)_team1TotalGoals, (unsigned)_team2TotalGoals,
                    (int)s_spec.minute,
                    s_spec.situation == SIT_GOAL ? "at the restart kickoff" : "at the corner");
            GEN_LOG("position: gameState %u, breakState %u, taker %s (team %u, human %s)\n",
                    (unsigned)_gameState, (unsigned)_breakState,
                    _lastTeamPlayedBeforeBreak == &_topTeamData ? "top" : "bottom",
                    (unsigned)(_lastTeamPlayedBeforeBreak ? _lastTeamPlayedBeforeBreak->teamNumber : 0),
                    (_lastTeamPlayedBeforeBreak && _lastTeamPlayedBeforeBreak->playerNumber)
                        ? "yes" : "no");
            s_phase = PH_DONE;
#ifndef SWOS_SDL_BUILD
            amiga_status("DONE", s_spec.out[0] ? s_spec.out : "CHALnn.CHL", s_phase);
#endif
            gen_finish(0);
            return;
        }

    default:
        return;
    }
}
