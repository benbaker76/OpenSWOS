/*
 * replay_test.c — recorded-game replay regression rig (SDL build only).
 *
 * docs/TESTS.md is the design; this is section 5b.
 *
 * A `.swr` file (scripts/rgd_convert.py --emit) carries a match setup, a
 * per-frame field SCHEMA (name/type/level), and the frames themselves as
 * those fields packed in schema order. The rig:
 *
 *   1. binds every schema name to one of OUR globals — a name-keyed table
 *      built from include/game_types.h offsets, never a DOS byte offset and
 *      never an address range (CLAUDE.md, "no address ranges");
 *   2. installs the recorded setup and starts the match with no menus;
 *   3. per frame: inject the recorded joystick state, run one game-loop
 *      iteration, compare each bound field, and stop at the first
 *      divergence naming the frame and the field.
 *
 * Determinism is the whole point, so the rig also takes over the two inputs
 * the SDL host would otherwise supply from a wall clock:
 *
 *   - Rand/Rand2 return the recording's fixedRandomValue (_replayFixedRandom);
 *   - _currentGameTick advances exactly once per game-loop iteration instead
 *     of being bumped by the SDL timer thread (see ReplayTest_OwnsGameTick).
 *
 * Exit codes: 0 every frame matched, 2 divergence, 1 setup failure.
 */

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replay_test.h"
#include "contest_log.h"
#include "dos_mode.h"      /* _gDosGameplay: the DOS timer ISR has no stats gate */
#include "game_types.h"
#include "display.h"    /* _sim_view_width/_height — the simulation view */
#include "game_init.h"
#include "math68k.h"
#include "mjson.h"
#include "rollback_rig.h"
#include "game_loop.h"     /* _gFrameStepInput */
#include "match_step.h"
#include "netplay_match.h"
#include "sim_state.h"
#include "endian_helpers.h"

/* ===================================================================
 * Recorded-field types
 * =================================================================== */

typedef enum {
    RT_U8, RT_S8, RT_U16, RT_S16, RT_U32, RT_S32, RT_PTR, RT_NTYPES
} RtType;

static const uint8_t k_rtSize[RT_NTYPES] = { 1, 1, 2, 2, 4, 4, sizeof(void *) };
static const char * const k_rtName[RT_NTYPES] = { "u8", "s8", "u16", "s16", "u32", "s32", "ptr" };

enum { RC_EQ, RC_BOOL, RC_VIEW, RC_SPR, RC_PTR, RC_NA, RC_IN, RC_META };

/* ptr:<kind> — how to turn a recorded identity into a pointer. */
enum { PK_TEAM, PK_TEAMGAME, PK_STATS, PK_SPRITES_TABLE, PK_SPRITE,
       PK_SHOTCHANCE, PK_NKINDS };
static const char * const k_ptrKind[PK_NKINDS] = {
    "team", "teamgame", "stats", "sprites_table", "sprite", "shotchance" };

/* One of our globals, addressable by the schema's name. */
typedef struct {
    char    *name;
    uint8_t  type;
    void    *addr;
} RtEntry;

/* One field of the recorded frame. */
typedef struct {
    char    *name;
    uint8_t  type;     /* as recorded */
    uint8_t  level;
    uint8_t  cmp;
    uint8_t  ptrKind;  /* RC_PTR only */
    uint32_t off;      /* byte offset inside a packed frame record */
    int32_t  bind;     /* index into s_reg, or -1 when unbound */
} RtField;

/* ===================================================================
 * Loaded file
 * =================================================================== */

static int       s_armed;
static int       s_running;
static int       s_finished;

static uint8_t  *s_file;
static uint32_t  s_fileLen;
static const char *s_setupJson;
static int       s_setupLen;
static const uint8_t *s_blob;
static const uint8_t *s_frames;
static uint32_t  s_frameCount;
static uint32_t  s_frameSize;

static RtField  *s_field;
static uint32_t  s_fieldCount;
static uint32_t  s_compareCount;
static uint32_t  s_viewSkipped;    /* "view" fields muted by a geometry mismatch */
static int       s_viewMatches;

static RtEntry  *s_reg;
static uint32_t  s_regCount, s_regCap;

static int       s_maxLevel = 3;
static int       s_maxErrors = 1;
static const char *s_skipPrefixes;  /* --replay-skip=a,b,c */
static int       s_errors;
static uint32_t  s_frame;          /* index of the record being replayed */
/* The recording frame currently being replayed, or -1 outside a replay.
 * Exported so a throwaway trace anywhere in the game code can print the frame
 * number the divergence report uses — the one thing an ad-hoc printf can
 * never work out for itself. Nothing in the build reads it. */
int32_t g_rtFrameNo = -1;
static const uint8_t *s_rec;       /* the record itself, NULL between frames */
static int       s_firstFrame = 1;
static int       s_warnedEsc, s_warnedBench;

int _replayFixedRandom = -1;

/* The recording's setup, as named values. */
static struct {
    int major, minor;
    int firstFrame, lastFrame;
    int fixedRandomValue;
    int pitchTypeOrSeason, pitchType, gameLength, autoReplays, allPlayerTeamsEqual;
    int season, minSubstitutes, maxSubstitutes;
    int trainingGameCareerSize, inputControls;
    int viewWidth, viewHeight;
    int spriteIndexOffset;
    int extraTime, penaltyShootout;
    int topTeamMarkedPlayer, bottomTeamMarkedPlayer;
    int topTeamCoachNo, topTeamPlayerNo, bottomTeamCoachNo, bottomTeamPlayerNo;
    struct { int blobOff, blobLen, globalTeamNumber, teamControls; char name[20]; } team[2];
    struct { int blobOff, blobLen; } tactics[6];
} s_setup;

/* ===================================================================
 * Globals the rig drives
 * =================================================================== */

extern TeamFileEx        _selectedTeams[SELECTED_TEAMS_MAX];
extern uint16_t          _numSelectedTeams;
extern TeamGame          _topTeamInGame, _bottomTeamInGame;
extern TeamGeneralInfo   _topTeamData, _bottomTeamData;
extern uint16_t          _team1StatsData[7], _team2StatsData[7];
extern FixedPoint        _cameraX, _cameraY;
extern uint16_t          _cameraXVelocity, _cameraYVelocity;
extern uint8_t           _gameTime[4];
extern uint16_t          _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t          _gameState, _gameStatePl;
extern uint16_t          _fireBlocked, _statsTimer, _resultTimer;
extern volatile uint16_t _currentGameTick;
extern volatile uint16_t _currentTick;
extern uint16_t          _lastGameTick;
extern volatile uint16_t _playGame;
extern uint16_t          _gameType, _trainingGame, _isCareer, _exitGameFlag, _twoPlayers;
extern uint16_t          _pitchTypeOrSeason, _friendlyPitchType, _friendlySeason;
extern uint16_t          _pitchType, _gameLength, _autoReplays, _autoSaveHighlights;
extern uint16_t          _allPlayerTeamsEqual, _minSubstitutes, _maxSubstitutes;
extern int16_t           _extraTimeState, _penaltiesState;
extern uint16_t          _pl1Coach, _pl2Coach;
extern uint16_t          _topTeamPlayerNo, _topTeamCoachNo;
extern uint16_t          _bottomTeamPlayerNo, _bottomTeamCoachNo;
extern uint16_t          _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
extern int16_t           _pl1Tactics, _pl2Tactics;
extern uint8_t           _teamTacticsUSERA[370], _teamTacticsUSERB[370];
extern uint8_t           _teamTacticsUSERC[370], _teamTacticsUSERD[370];
extern uint8_t           _teamTacticsUSERE[370], _teamTacticsUSERF[370];
extern int               _gDosGameplay;
extern int16_t           _gMoveDeltaScaleNum, _gMoveDeltaScaleDen;

/* The tables the recording's pointer identities resolve to. */
extern GoalieSkill *_kGoalieSkillTables[8];
extern uint32_t     _teamPlOfs24Table;   /* the outfield shot-chance table */
extern Sprite     **_team1SpritesTable;
extern Sprite     **_team2SpritesTable;

/* Front-end work the rig skips but the match still depends on. */
extern uint16_t  _menuBGImageCmdStream[];
extern uint8_t  *_menuImageStreamBaseEnd;
extern void      ProcessImageCommandStream(uint16_t *script, uint8_t *base);
extern void      InitTeams(void);
extern void      InitUserTactics(void);

/* The per-player match value the recording carries in its own team records
 * (PlayerFile.cachedMatchValue), and what recomputes it. See checkPlayerValues. */
extern uint32_t  UpdatePlayerPositionAndReturnRole(TeamFileEx *team, uint16_t playerIdx);
extern uint8_t   _playerPositionsArray[32];

/* What MainKeysCheck's bench hotkey sets — see MatchStep_ApplyInput. */
extern uint16_t  _bench1Called, _bench2Called;

/* The 33 sprites the recording carries, in ITS order. Indices 0..29 happen
 * to coincide with our own allSpritesArray (sprite_data.c), which is a
 * useful confirmation, but the list is written out by name so a change to
 * that table can't silently re-map the comparison. */
extern Sprite _ballShadowSprite, _ballSprite, _goal1TopSprite, _goal2BottomSprite;
extern Sprite _goalie1Sprite, _goalie2Sprite;
extern Sprite _team1Player2Sprite,  _team1Player3Sprite,  _team1Player4Sprite;
extern Sprite _team1Player5Sprite,  _team1Player6Sprite,  _team1Player7Sprite;
extern Sprite _team1Player8Sprite,  _team1Player9Sprite,  _team1Player10Sprite;
extern Sprite _team1Player11Sprite;
extern Sprite _team2Player2Sprite,  _team2Player3Sprite,  _team2Player4Sprite;
extern Sprite _team2Player5Sprite,  _team2Player6Sprite,  _team2Player7Sprite;
extern Sprite _team2Player8Sprite,  _team2Player9Sprite,  _team2Player10Sprite;
extern Sprite _team2Player11Sprite;
extern Sprite _team1CurPlayerNumSprite, _team2CurPlayerNumSprite;
extern Sprite _playerMarkSprite, _bookedPlayerCardOrNumberSprite;
extern Sprite _refereeSprite, _cornerFlagSprite, _currentPlayerNameSprite;

static Sprite * const k_recordedSprites[33] = {
    &_ballShadowSprite, &_ballSprite, &_goal1TopSprite, &_goal2BottomSprite,
    &_goalie1Sprite,
    &_team1Player2Sprite,  &_team1Player3Sprite,  &_team1Player4Sprite,
    &_team1Player5Sprite,  &_team1Player6Sprite,  &_team1Player7Sprite,
    &_team1Player8Sprite,  &_team1Player9Sprite,  &_team1Player10Sprite,
    &_team1Player11Sprite,
    &_goalie2Sprite,
    &_team2Player2Sprite,  &_team2Player3Sprite,  &_team2Player4Sprite,
    &_team2Player5Sprite,  &_team2Player6Sprite,  &_team2Player7Sprite,
    &_team2Player8Sprite,  &_team2Player9Sprite,  &_team2Player10Sprite,
    &_team2Player11Sprite,
    &_team1CurPlayerNumSprite, &_team2CurPlayerNumSprite,
    &_playerMarkSprite, &_bookedPlayerCardOrNumberSprite,
    &_refereeSprite, &_cornerFlagSprite, &_currentPlayerNameSprite,
};

/* ===================================================================
 * Registry — schema name -> one of our globals
 * =================================================================== */

typedef struct { const char *name; uint8_t type; uint16_t off; } MemberDesc;

#define M(field, type)  { #field, type, (uint16_t)offsetof(Sprite, field) }
static const MemberDesc k_spriteMembers[] = {
    M(teamNumber, RT_S16), M(playerOrdinal, RT_S16), M(frameOffset, RT_S16),
    M(startingDirection, RT_S16), M(playerState, RT_U8), M(playerDownTimer, RT_S8),
    M(field_E, RT_S16), M(field_10, RT_S16), M(frameIndex, RT_S16),
    M(frameDelay, RT_S16), M(cycleFramesTimer, RT_S16), M(frameSwitchCounter, RT_S16),
    M(direction, RT_S16), M(speed, RT_S16), M(destX, RT_S16), M(destY, RT_S16),
    M(visible, RT_S16), M(imageIndex, RT_S16), M(saveSprite, RT_S16),
    M(ballDistance, RT_S32), M(field_4E, RT_S16), M(field_50, RT_S16),
    M(fullDirection, RT_S16), M(onScreen, RT_S16), M(field_56, RT_S16),
    M(field_58, RT_S16), M(field_5A, RT_S16), M(playerDirection, RT_S16),
    M(isMoving, RT_S16), M(tackleState, RT_S16), M(heading, RT_S16),
    M(destReachedState, RT_S16), M(cards, RT_S16), M(injuryLevel, RT_S16),
    M(tacklingTimer, RT_S16), M(sentAway, RT_S16),
};
/* The schema's snake_case name for each of the above, same order. */
static const char * const k_spriteMemberNames[] = {
    "team_number", "player_ordinal", "frame_offset",
    "starting_direction", "player_state", "player_down_timer",
    "field_e", "field_10", "frame_index",
    "frame_delay", "cycle_frames_timer", "frame_switch_counter",
    "direction", "speed", "dest_x", "dest_y",
    "visible", "image_index", "save_sprite",
    "ball_distance", "field_4e", "field_50",
    "full_direction", "on_screen", "field_56",
    "field_58", "field_5a", "player_direction",
    "is_moving", "tackle_state", "heading",
    "dest_reached_state", "cards", "injury_level",
    "tackling_timer", "sent_away",
};
#undef M

#define I(field, type)  { #field, type, (uint16_t)offsetof(TeamGeneralInfo, field) }
static const MemberDesc k_infoMembers[] = {
    /* Pointers, bound by the address of the POINTER FIELD; the comparison
     * turns the recording's identity into the pointer it should hold. */
    I(opponentsTeam, RT_PTR), I(inGameTeamPtr, RT_PTR), I(teamStatsPtr, RT_PTR),
    I(spritesTable, RT_PTR), I(shotChanceTable, RT_PTR), I(controlledPlayer, RT_PTR),
    I(passToPlayerPtr, RT_PTR), I(lastHeadingTacklingPlayer, RT_PTR),
    I(passingKickingPlayer, RT_PTR),
    I(playerNumber, RT_S16), I(playerCoachNumber, RT_S16), I(isPlCoach, RT_S16),
    I(teamNumber, RT_S16), I(tactics, RT_S16), I(updatePlayerIndex, RT_S16),
    I(playerHasBall, RT_S16), I(allowedDirections, RT_S16),
    I(currentAllowedDirection, RT_S16), I(direction, RT_S16),
    I(quickFire, RT_S8), I(normalFire, RT_S8), I(firePressed, RT_S8),
    I(fireThisFrame, RT_S8), I(headerOrTackle, RT_S16), I(fireCounter, RT_S16),
    I(controlledPlDirection, RT_S16), I(shooting, RT_S16), I(field_3C, RT_S8),
    I(plVeryCloseToBall, RT_S8), I(plCloseToBall, RT_S8), I(plNotFarFromBall, RT_S8),
    I(ballLessEqual4, RT_S8), I(ball4To8, RT_S8), I(ball8To12, RT_S8),
    I(ball12To17, RT_S8), I(ballAbove17, RT_S8), I(prevPlVeryCloseToBall, RT_S8),
    I(field_46, RT_S16), I(goalkeeperSavedCommentTimer, RT_S16), I(field_4E, RT_S16),
    I(goalkeeperDivingRight, RT_S16), I(goalkeeperDivingLeft, RT_S16),
    I(ballOutOfPlayOrKeeper, RT_S16), I(goaliePlayingOrOut, RT_S16),
    I(passingBall, RT_S16), I(passingToPlayer, RT_S16), I(playerSwitchTimer, RT_S16),
    I(ballInPlay, RT_S16), I(ballOutOfPlay, RT_S16), I(ballX, RT_S16), I(ballY, RT_S16),
    I(passKickTimer, RT_S16), I(ballControlTurnTimer, RT_S16),
    I(ballCanBeControlled, RT_S16), I(ballControllingPlayerDirection, RT_S16),
    I(field_72, RT_S16), I(field_74, RT_S16), I(spinTimer, RT_S16),
    I(leftSpin, RT_S16), I(rightSpin, RT_S16), I(longPass, RT_S16),
    I(longSpinPass, RT_S16), I(passInProgress, RT_S16), I(AI_timer, RT_S16),
    I(field_84, RT_S16), I(AI_afterTouchStrength, RT_S16),
    I(AI_ballSpinDirection, RT_S16), I(wonTheBallTimer, RT_S16),
    I(goalkeeperPlaying, RT_S16), I(resetControls, RT_S16),
};
static const char * const k_infoMemberNames[] = {
    "opponents_team", "in_game_team_ptr", "team_stats_ptr",
    "sprites_table", "shot_chance_table", "controlled_player",
    "pass_to_player_ptr", "last_heading_tackling_player",
    "passing_kicking_player",
    "player_number", "player_coach_number", "is_pl_coach",
    "team_number", "tactics", "update_player_index",
    "player_has_ball", "allowed_directions",
    "current_allowed_direction", "direction",
    "quick_fire", "normal_fire", "fire_pressed",
    "fire_this_frame", "header_or_tackle", "fire_counter",
    "controlled_pl_direction", "shooting", "field_3c",
    "pl_very_close_to_ball", "pl_close_to_ball", "pl_not_far_from_ball",
    "ball_less_equal4", "ball4_to8", "ball8_to12",
    "ball12_to17", "ball_above17", "prev_pl_very_close_to_ball",
    "field_46", "goalkeeper_saved_comment_timer", "field_4e",
    "goalkeeper_diving_right", "goalkeeper_diving_left",
    "ball_out_of_play_or_keeper", "goalie_playing_or_out",
    "passing_ball", "passing_to_player", "player_switch_timer",
    "ball_in_play", "ball_out_of_play", "ball_x", "ball_y",
    "pass_kick_timer", "ball_control_turn_timer",
    "ball_can_be_controlled", "ball_controlling_player_direction",
    "field_72", "field_74", "spin_timer",
    "left_spin", "right_spin", "long_pass",
    "long_spin_pass", "pass_in_progress", "ai_timer",
    "field_84", "ai_after_touch_strength",
    "ai_ball_spin_direction", "won_the_ball_timer",
    "goalkeeper_playing", "reset_controls",
};
#undef I

#define P(field, type)  { #field, type, (uint16_t)offsetof(PlayerInfo, field) }
static const MemberDesc k_playerMembers[] = {
    P(skip, RT_S8), P(playerIndex, RT_S8), P(goalsScored, RT_S8),
    P(shirtNumber, RT_S8), P(position, RT_S8), P(face, RT_S8),
    P(isInjured, RT_S8), { "field_7", RT_S8, (uint16_t)offsetof(PlayerInfo, blond) },   /* (blond: the recordings' name) */ P(field_8, RT_S8),
    P(previousCards, RT_S8), P(numYellowCards, RT_S8), P(field_B, RT_S8),
    P(passing, RT_S8), P(shooting, RT_S8), P(heading, RT_S8),
    P(tackling, RT_S8), P(ballControl, RT_S8), P(speed, RT_S8),
    P(finishing, RT_S8), P(goalieSkill, RT_S8), P(injuriesBitfield, RT_S8),
    P(halfPlayed, RT_S8), P(face2, RT_S8),
};
static const char * const k_playerMemberNames[] = {
    "skip", "player_index", "goals_scored",
    "shirt_number", "position", "face",
    "is_injured", "field_7", "field_8",
    "previous_cards", "num_yellow_cards", "field_b",
    "passing", "shooting", "heading",
    "tackling", "ball_control", "speed",
    "finishing", "goalie_skill", "injuries_bitfield",
    "half_played", "face2",
};
#undef P

#define G(field, type)  { #field, type, (uint16_t)offsetof(TeamGame, field) }
static const MemberDesc k_teamGameMembers[] = {
    G(prShirtType, RT_S16), G(prShirtCol, RT_S16), G(prStripesCol, RT_S16),
    G(prShortsCol, RT_S16), G(prSocksCol, RT_S16), G(secShirtType, RT_S16),
    G(secShirtCol, RT_S16), G(secStripesCol, RT_S16), G(secShortsCol, RT_S16),
    G(secSocksCol, RT_S16), G(markedPlayer, RT_S16), G(numOwnGoals, RT_S8),
};
static const char * const k_teamGameMemberNames[] = {
    "pr_shirt_type", "pr_shirt_col", "pr_stripes_col",
    "pr_shorts_col", "pr_socks_col", "sec_shirt_type",
    "sec_shirt_col", "sec_stripes_col", "sec_shorts_col",
    "sec_socks_col", "marked_player", "num_own_goals",
};
#undef G

static const char * const k_statsMemberNames[7] = {
    "ball_possession", "corners_won", "fouls_conceded",
    "bookings", "sendings_off", "goal_attempts", "on_target",
};

static void regAdd(const char *name, uint8_t type, void *addr)
{
    if (s_regCount == s_regCap) {
        s_regCap = s_regCap ? s_regCap * 2 : 512;
        s_reg = (RtEntry *)realloc(s_reg, s_regCap * sizeof(*s_reg));
        if (!s_reg) { fprintf(stderr, "[replay] out of memory\n"); exit(1); }
    }
    s_reg[s_regCount].name = strdup(name);
    s_reg[s_regCount].type = type;
    s_reg[s_regCount].addr = addr;
    s_regCount++;
}

static void regAddf(uint8_t type, void *addr, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#include <stdarg.h>
static void regAddf(uint8_t type, void *addr, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    regAdd(buf, type, addr);
}

static void regMembers(const char *prefix, void *base,
                       const MemberDesc *m, const char * const *names, int n)
{
    int i;
    for (i = 0; i < n; i++)
        regAddf(m[i].type, (uint8_t *)base + m[i].off, "%s.%s", prefix, names[i]);
}

static void regTeamGame(const char *prefix, TeamGame *g)
{
    int p, i;
    regMembers(prefix, g, k_teamGameMembers, k_teamGameMemberNames,
               (int)(sizeof k_teamGameMembers / sizeof *k_teamGameMembers));
    for (p = 0; p < 16; p++) {
        PlayerInfo *pi = &g->players[p];
        for (i = 0; i < (int)(sizeof k_playerMembers / sizeof *k_playerMembers); i++)
            regAddf(k_playerMembers[i].type, (uint8_t *)pi + k_playerMembers[i].off,
                    "%s.players[%d].%s", prefix, p, k_playerMemberNames[i]);
    }
}

static void buildRegistry(void)
{
    int i, s;

    regAdd("camera_x",      RT_S32, &_cameraX.raw);
    regAdd("camera_y",      RT_S32, &_cameraY.raw);
    regAdd("camera_x_vel",  RT_S16, &_cameraXVelocity);
    regAdd("camera_y_vel",  RT_S16, &_cameraYVelocity);
    for (i = 0; i < 4; i++)
        regAddf(RT_U8, &_gameTime[i], "game_time[%d]", i);
    regAdd("team1_goals",   RT_S16, &_statsTeam1Goals);
    regAdd("team2_goals",   RT_S16, &_statsTeam2Goals);
    regAdd("game_state",    RT_S16, &_gameState);
    regAdd("game_state_pl", RT_S16, &_gameStatePl);
    regAdd("fire_blocked",  RT_S16, &_fireBlocked);
    regAdd("stats_timer",   RT_S16, &_statsTimer);
    regAdd("result_timer",  RT_S16, &_resultTimer);

    /* The substitutes-menu state machine (BenchDataV1, v1.3+ recordings). Our
     * names differ from swos-port's; the schema's are its. */
    {
        extern uint16_t _subsPl1TapCount, _subsPl2TapCount;
        extern uint16_t _subsPrevPl1Direction, _subsPrevPl2Direction;
        extern uint16_t _pl1BenchBlockWhileHoldingDirection;
        extern uint16_t _pl2BenchBlockWhileHoldingDirection;
        extern uint16_t _subsDirection, _subsState, _goToSubsTimer;
        extern uint16_t _subsBlockDirections, _subsFireTimer, _subsBlockFire;
        extern uint16_t _subsPrevDirection, _subsMovementDelayTimer;
        extern uint16_t _subsTrainingTopTeam, _subsTeamsSwapped;
        extern uint16_t _benchPickTeamCounter;
        extern uint16_t _plToEnterGameIndex, _plToBeSubstitutedPos;
        extern uint16_t _plToBeSubstitutedOrd, _selectedFormationEntry;
        regAdd("bench.pl1_tap_count",        RT_S16, &_subsPl1TapCount);
        regAdd("bench.pl2_tap_count",        RT_S16, &_subsPl2TapCount);
        regAdd("bench.pl1_prev_direction",   RT_S16, &_subsPrevPl1Direction);
        regAdd("bench.pl2_prev_direction",   RT_S16, &_subsPrevPl2Direction);
        regAdd("bench.pl1_block_hold",       RT_S16, &_pl1BenchBlockWhileHoldingDirection);
        regAdd("bench.pl2_block_hold",       RT_S16, &_pl2BenchBlockWhileHoldingDirection);
        regAdd("bench.controls",             RT_S16, &_subsDirection);
        regAdd("bench.state",                RT_S16, &_subsState);
        regAdd("bench.go_to_bench_timer",    RT_S16, &_goToSubsTimer);
        regAdd("bench.bench1_called",        RT_S16, &_bench1Called);
        regAdd("bench.bench2_called",        RT_S16, &_bench2Called);
        regAdd("bench.block_directions",     RT_S16, &_subsBlockDirections);
        regAdd("bench.fire_timer",           RT_S16, &_subsFireTimer);
        regAdd("bench.block_fire",           RT_S16, &_subsBlockFire);
        regAdd("bench.last_direction",       RT_S16, &_subsPrevDirection);
        regAdd("bench.movement_delay_timer", RT_S16, &_subsMovementDelayTimer);
        regAdd("bench.training_top_team",    RT_S16, &_subsTrainingTopTeam);
        regAdd("bench.teams_swapped",        RT_S16, &_subsTeamsSwapped);
        regAdd("bench.alternate_teams_timer", RT_S16, &_benchPickTeamCounter);
        regAdd("bench.player_to_enter",      RT_S16, &_plToEnterGameIndex);
        regAdd("bench.player_to_sub_pos",    RT_S16, &_plToBeSubstitutedPos);
        regAdd("bench.player_to_sub_ord",    RT_S16, &_plToBeSubstitutedOrd);
        regAdd("bench.selected_formation",   RT_S16, &_selectedFormationEntry);
        { extern uint16_t _benchPlayerIndex;
          regAdd("bench.arrow_player_index", RT_S16, &_benchPlayerIndex); }
    }

    regTeamGame("top_game",    &_topTeamInGame);
    regTeamGame("bottom_game", &_bottomTeamInGame);

    for (i = 0; i < 7; i++) {
        regAddf(RT_S16, &_team1StatsData[i], "top_stats.%s",    k_statsMemberNames[i]);
        regAddf(RT_S16, &_team2StatsData[i], "bottom_stats.%s", k_statsMemberNames[i]);
    }

    regMembers("top_info", &_topTeamData, k_infoMembers, k_infoMemberNames,
               (int)(sizeof k_infoMembers / sizeof *k_infoMembers));
    regMembers("bottom_info", &_bottomTeamData, k_infoMembers, k_infoMemberNames,
               (int)(sizeof k_infoMembers / sizeof *k_infoMembers));

    for (s = 0; s < 33; s++) {
        Sprite *sp = k_recordedSprites[s];
        char pre[24];
        snprintf(pre, sizeof pre, "sprite[%d]", s);
        regMembers(pre, sp, k_spriteMembers, k_spriteMemberNames,
                   (int)(sizeof k_spriteMembers / sizeof *k_spriteMembers));
        /* field_3E is a 6-byte run our struct keeps as one array. */
        for (i = 0; i < 6; i++)
            regAddf(RT_U8, &sp->field_3E[i], "%s.field_3e[%d]", pre, i);
        /* FixedPoint members: compare the raw 16.16 word, as recorded. */
        regAddf(RT_S32, &sp->x.raw,      "%s.x", pre);
        regAddf(RT_S32, &sp->y.raw,      "%s.y", pre);
        regAddf(RT_S32, &sp->z.raw,      "%s.z", pre);
        regAddf(RT_S32, &sp->deltaX.raw, "%s.delta_x", pre);
        regAddf(RT_S32, &sp->deltaY.raw, "%s.delta_y", pre);
        regAddf(RT_S32, &sp->deltaZ.raw, "%s.delta_z", pre);
    }
}

static int regFind(const char *name)
{
    uint32_t i;
    for (i = 0; i < s_regCount; i++)
        if (strcmp(s_reg[i].name, name) == 0)
            return (int)i;
    return -1;
}

/* ===================================================================
 * Value access
 * =================================================================== */

static int64_t readTyped(const void *p, uint8_t type)
{
    switch (type) {
    case RT_U8:  { uint8_t  v; memcpy(&v, p, 1); return (int64_t)v; }
    case RT_S8:  { int8_t   v; memcpy(&v, p, 1); return (int64_t)v; }
    case RT_U16: { uint16_t v; memcpy(&v, p, 2); return (int64_t)v; }
    case RT_S16: { int16_t  v; memcpy(&v, p, 2); return (int64_t)v; }
    case RT_U32: { uint32_t v; memcpy(&v, p, 4); return (int64_t)v; }
    case RT_PTR: { void    *v; memcpy(&v, p, sizeof v); return (int64_t)(intptr_t)v; }
    default:     { int32_t  v; memcpy(&v, p, 4); return (int64_t)v; }
    }
}

/* Turn a recorded pointer IDENTITY into the pointer our build should hold.
 * The recording never stores an address: swos-port's recorder writes a small
 * semantic value and its own rig decodes it the same way (verifySpritePointer,
 * verifyShotChanceTable). Returns 1 when `rec` is a value this kind can
 * represent, 0 when it is out of range — which is itself worth reporting. */
static int resolvePointer(int kind, int64_t rec, const void **out)
{
    switch (kind) {
    case PK_TEAM:
        if (rec != 0 && rec != 1) return 0;
        *out = rec ? (const void *)&_bottomTeamData : (const void *)&_topTeamData;
        return 1;
    case PK_TEAMGAME:
        if (rec != 0 && rec != 1) return 0;
        *out = rec ? (const void *)&_bottomTeamInGame : (const void *)&_topTeamInGame;
        return 1;
    case PK_STATS:
        if (rec != 0 && rec != 1) return 0;
        *out = rec ? (const void *)_team2StatsData : (const void *)_team1StatsData;
        return 1;
    case PK_SPRITES_TABLE:
        if (rec != 0 && rec != 1) return 0;
        *out = rec ? (const void *)_team2SpritesTable : (const void *)_team1SpritesTable;
        return 1;
    case PK_SPRITE:
        if (rec == -1) { *out = NULL; return 1; }
        if (rec < 0 || rec >= 33) return 0;
        *out = (const void *)k_recordedSprites[rec];
        return 1;
    case PK_SHOTCHANCE:
        if (rec == -1) { *out = NULL; return 1; }
        /* 0 is the outfield table; anything else is kGoalieSkillTables[n-1]. */
        if (rec == 0) { *out = (const void *)&_teamPlOfs24Table; return 1; }
        if (rec < 1 || rec > 8) return 0;
        *out = (const void *)_kGoalieSkillTables[rec - 1];
        return 1;
    default:
        return 0;
    }
}

/* Name a pointer the way the recording would have, so a mismatch reads
 * "sprite[7]" against "sprite[12]" rather than two hex addresses. */
static void describePointer(int kind, const void *p, char *buf, size_t n)
{
    int i;
    if (!p) { snprintf(buf, n, "null"); return; }
    switch (kind) {
    case PK_TEAM:
        snprintf(buf, n, "%s", p == (const void *)&_topTeamData ? "top" :
                               p == (const void *)&_bottomTeamData ? "bottom" : "?");
        return;
    case PK_TEAMGAME:
        snprintf(buf, n, "%s", p == (const void *)&_topTeamInGame ? "top" :
                               p == (const void *)&_bottomTeamInGame ? "bottom" : "?");
        return;
    case PK_STATS:
        snprintf(buf, n, "%s", p == (const void *)_team1StatsData ? "team1" :
                               p == (const void *)_team2StatsData ? "team2" : "?");
        return;
    case PK_SPRITES_TABLE:
        snprintf(buf, n, "%s", p == (const void *)_team1SpritesTable ? "team1" :
                               p == (const void *)_team2SpritesTable ? "team2" : "?");
        return;
    case PK_SPRITE:
        for (i = 0; i < 33; i++)
            if (p == (const void *)k_recordedSprites[i]) {
                snprintf(buf, n, "sprite[%d]", i);
                return;
            }
        break;
    case PK_SHOTCHANCE:
        if (p == (const void *)&_teamPlOfs24Table) { snprintf(buf, n, "outfield"); return; }
        for (i = 0; i < 8; i++)
            if (p == (const void *)_kGoalieSkillTables[i]) {
                snprintf(buf, n, "goalie[%d]", i);
                return;
            }
        break;
    default:
        break;
    }
    snprintf(buf, n, "unknown(%p)", p);
}

/* Two values are the same when their two's-complement bits agree at the
 * RECORDED field's width. The recorded width is the field's real width in
 * the game's own data; our global may be stored wider (a byte-sized state
 * kept in a uint16_t, say) and may disagree on signedness (their `word`
 * against our int16_t), and neither of those is a divergence. */
static int sameValue(int64_t rec, int64_t ours, uint8_t recType)
{
    uint64_t mask = (k_rtSize[recType] == 1) ? 0xFFull :
                    (k_rtSize[recType] == 2) ? 0xFFFFull : 0xFFFFFFFFull;
    return ((uint64_t)rec & mask) == ((uint64_t)ours & mask);
}

static int typeFromName(const char *s)
{
    int i;
    for (i = 0; i < RT_NTYPES; i++)
        if (strcmp(s, k_rtName[i]) == 0) return i;
    return -1;
}

static int64_t recordField(const uint8_t *rec, const char *name)
{
    uint32_t i;
    for (i = 0; i < s_fieldCount; i++)
        if (strcmp(s_field[i].name, name) == 0)
            return readTyped(rec + s_field[i].off, s_field[i].type);
    return 0;
}

static int64_t recField(const char *name)
{
    return recordField(s_rec, name);
}

/* ===================================================================
 * Loading
 * =================================================================== */

static int jnum(const char *path, int dflt)
{
    double v;
    if (mjson_get_number(s_setupJson, s_setupLen, path, &v)) return (int)v;
    return dflt;
}

static void jstr(const char *path, char *dst, int n)
{
    if (mjson_get_string(s_setupJson, s_setupLen, path, dst, n) <= 0)
        dst[0] = 0;
}

static int parseSchema(const char *text, int len, const char *levelName)
{
    const char *p = text, *end = text + len;
    uint32_t cap = 256, off = 0;
    static const char * const k_levels[] = { "scalars", "teams", "info", "sprites" };
    int i;

    if (levelName) {
        s_maxLevel = -1;
        for (i = 0; i < 4; i++)
            if (strcmp(levelName, k_levels[i]) == 0) s_maxLevel = i;
        if (s_maxLevel < 0) {
            fprintf(stderr, "[replay] unknown level '%s' (scalars|teams|info|sprites)\n", levelName);
            return -1;
        }
    }

    s_field = (RtField *)malloc(cap * sizeof(*s_field));
    if (!s_field) return -1;

    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        char line[192], *tok[4];
        int n = 0, tlen;
        if (!nl) nl = end;
        tlen = (int)(nl - p);
        if (tlen > 0 && tlen < (int)sizeof line) {
            memcpy(line, p, (size_t)tlen);
            line[tlen] = 0;
            {
                char *q = line;
                tok[n++] = q;
                while (*q && n < 4) {
                    if (*q == '\t') { *q = 0; tok[n++] = q + 1; }
                    q++;
                }
            }
            if (n == 4) {
                int ty = typeFromName(tok[1]);
                if (ty < 0) {
                    fprintf(stderr, "[replay] schema: unknown type '%s' for %s\n", tok[1], tok[0]);
                    return -1;
                }
                if (s_fieldCount == cap) {
                    cap *= 2;
                    s_field = (RtField *)realloc(s_field, cap * sizeof(*s_field));
                    if (!s_field) return -1;
                }
                s_field[s_fieldCount].name  = strdup(tok[0]);
                s_field[s_fieldCount].type  = (uint8_t)ty;
                s_field[s_fieldCount].level = (uint8_t)atoi(tok[2]);
                s_field[s_fieldCount].ptrKind = 0;
                if (strncmp(tok[3], "ptr:", 4) == 0) {
                    int k;
                    s_field[s_fieldCount].cmp = RC_PTR;
                    for (k = 0; k < PK_NKINDS; k++)
                        if (strcmp(tok[3] + 4, k_ptrKind[k]) == 0) break;
                    if (k == PK_NKINDS) {
                        fprintf(stderr, "[replay] schema: unknown pointer kind '%s' for %s\n",
                                tok[3] + 4, tok[0]);
                        return -1;
                    }
                    s_field[s_fieldCount].ptrKind = (uint8_t)k;
                } else {
                    s_field[s_fieldCount].cmp =
                        strcmp(tok[3], "in")   == 0 ? RC_IN   :
                        strcmp(tok[3], "meta") == 0 ? RC_META :
                        strcmp(tok[3], "bool") == 0 ? RC_BOOL :
                        strcmp(tok[3], "view") == 0 ? RC_VIEW :
                        strcmp(tok[3], "spr")  == 0 ? RC_SPR  :
                        strcmp(tok[3], "na")   == 0 ? RC_NA   : RC_EQ;
                }
                s_field[s_fieldCount].off   = off;
                s_field[s_fieldCount].bind  = -1;
                off += k_rtSize[ty];
                s_fieldCount++;
            }
        }
        p = nl + 1;
    }
    if (off != s_frameSize) {
        fprintf(stderr, "[replay] schema sums to %u bytes, file says %u\n", off, s_frameSize);
        return -1;
    }
    return 0;
}

/* Does `name` contain any of the comma-separated --replay-skip patterns?
 * Substring, not prefix, so one pattern can mute a member across all 33
 * sprites (".starting_direction") as easily as one whole struct ("top_info."). */
static int isSkipped(const char *name)
{
    const char *p = s_skipPrefixes;
    char pat[128];
    if (!p) return 0;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        if (n && n < sizeof pat) {
            memcpy(pat, p, n);
            pat[n] = 0;
            if (strstr(name, pat)) return 1;
        }
        p = comma ? comma + 1 : p + n;
    }
    return 0;
}

static int bindSchema(void)
{
    uint32_t i;
    int unbound = 0;
    uint32_t skipped = 0;

    for (i = 0; i < s_fieldCount; i++) {
        RtField *f = &s_field[i];
        if (f->cmp == RC_IN || f->cmp == RC_META || f->cmp == RC_NA) continue;
        if (f->level > s_maxLevel) continue;
        if (f->cmp == RC_VIEW && !s_viewMatches) { s_viewSkipped++; continue; }
        if (isSkipped(f->name)) { skipped++; continue; }
        f->bind = regFind(f->name);
        if (f->bind < 0) {
            if (unbound < 12)
                fprintf(stderr, "[replay] schema field has no global: %s\n", f->name);
            unbound++;
            continue;
        }
        s_compareCount++;
    }
    if (skipped)
        fprintf(stderr, "[replay] --replay-skip muted %u field(s)\n", skipped);
    if (unbound) {
        fprintf(stderr, "[replay] %d schema field(s) unbound — the rig would silently\n"
                        "         skip them; fix the registry in src/replay_test.c\n", unbound);
        return -1;
    }
    return 0;
}

int ReplayTest_Load(const char *path, const char *level, int maxErrors,
                    const char *skipPrefixes)
{
    FILE *fp;
    long size;
    uint32_t hdr[10];
    char buf[64];
    int i;

    s_skipPrefixes = (skipPrefixes && skipPrefixes[0]) ? skipPrefixes : NULL;

    /* Refuse to run against a binary whose COMPILE-TIME flags change the
     * simulation. `make` compares timestamps, not CFLAGS, so a plain `make`
     * after a build with one of them relinked those objects and produced a
     * binary that still had the flag baked in. (The worst offender, the tester
     * drops' SWOS_TEST_NO_INJURIES_CARDS, is a setting since 2026-10-11 and the
     * rigs never apply it -- include/test_rules.h -- and both makefiles now
     * rebuild on a flag change; the guard stays for the flags below.) Exit 0, no warning — and the
     * corpus then reports a regression that is not there.
     *
     * That has now cost real time three times, twice in one day, and it always
     * looks the same: four recordings stopping on camera_x/camera_y at roughly
     * half distance. Injuries change match outcomes, so of course the camera
     * diverges. Worse, the obvious check does NOT catch it — stashing a change
     * and re-running compares two builds that share the same contaminated
     * objects, so the A/B comes back identical and reads like proof of
     * innocence.
     *
     * Failing loudly here is the only reliable guard: the flag is baked into
     * this translation unit at the same moment it is baked into the gameplay
     * ones, so it cannot disagree with what is actually linked. Set
     * SWOS_REPLAY_ALLOW_FLAGS=1 to run anyway when the mismatch is deliberate
     * (e.g. deciding what a flag actually costs the corpus). */
    {
        const char *flags[6];
        int nflags = 0;
#ifdef SWOS_ORIGINAL_TACTICS
        flags[nflags++] = "SWOS_ORIGINAL_TACTICS";
#endif
#ifdef SWOS_DOS_TACTICS
        flags[nflags++] = "SWOS_DOS_TACTICS";
#endif
#ifdef SWOS_INJECT_WC
        flags[nflags++] = "SWOS_INJECT_WC";
#endif
        if (nflags > 0 && getenv("SWOS_REPLAY_ALLOW_FLAGS") == NULL) {
            fprintf(stderr,
                "[replay] REFUSING: this binary was built with a flag that changes "
                "the simulation:\n");
            for (i = 0; i < nflags; i++)
                fprintf(stderr, "[replay]   -D%s\n", flags[i]);
            fprintf(stderr,
                "[replay] The corpus was recorded without it, so every result would be\n"
                "[replay] wrong — typically four recordings stopping on camera_x/camera_y\n"
                "[replay] at about half distance, which looks exactly like a regression.\n"
                "[replay] Usually this is a plain `make` relinking objects left by\n"
                "[replay] scripts/build_dist.sh; `make clean && make` fixes it.\n"
                "[replay] Set SWOS_REPLAY_ALLOW_FLAGS=1 to run anyway.\n");
            return 1;
        }
    }

    fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "[replay] cannot open %s\n", path); return 1; }
    fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (size < 64) { fprintf(stderr, "[replay] %s is too small\n", path); fclose(fp); return 1; }
    s_file = (uint8_t *)malloc((size_t)size);
    if (!s_file || fread(s_file, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "[replay] short read on %s\n", path); fclose(fp); return 1;
    }
    fclose(fp);
    s_fileLen = (uint32_t)size;

    if (memcmp(s_file, "SWOSREC\0", 8) != 0) {
        fprintf(stderr, "[replay] %s is not a .swr (bad magic)\n", path);
        return 1;
    }
    memcpy(hdr, s_file + 8, sizeof hdr);
    if (hdr[0] != 1) {
        fprintf(stderr, "[replay] .swr format version %u, expected 1\n", hdr[0]);
        return 1;
    }
    s_setupJson  = (const char *)(s_file + hdr[1]);
    s_setupLen   = (int)hdr[2];
    s_blob       = s_file + hdr[5];
    s_frames     = s_file + hdr[7];
    s_frameCount = hdr[8];
    s_frameSize  = hdr[9];
    if (hdr[7] + (uint64_t)s_frameCount * s_frameSize > s_fileLen) {
        fprintf(stderr, "[replay] %s is truncated\n", path);
        return 1;
    }

    s_setup.major = jnum("$.version[0]", 1);
    s_setup.minor = jnum("$.version[1]", 0);
    s_setup.firstFrame = jnum("$.first_frame", 0);
    s_setup.lastFrame  = jnum("$.last_frame", 0);
    s_setup.fixedRandomValue     = jnum("$.fixed_random_value", 0);
    s_setup.pitchTypeOrSeason    = jnum("$.pitch_type_or_season", 0);
    s_setup.pitchType            = jnum("$.pitch_type", 0);
    s_setup.gameLength           = jnum("$.game_length", 0);
    s_setup.autoReplays          = jnum("$.auto_replays", 0);
    s_setup.allPlayerTeamsEqual  = jnum("$.all_player_teams_equal", 0);
    /* -1 = the recording's header predates these fields (v1.0/1.1). */
    s_setup.season               = jnum("$.season", -1);
    s_setup.minSubstitutes       = jnum("$.min_substitutes", -1);
    s_setup.maxSubstitutes       = jnum("$.max_substitutes", -1);
    s_setup.trainingGameCareerSize = jnum("$.training_game_career_size", 0);
    s_setup.inputControls        = jnum("$.input_controls", 0);
    s_setup.spriteIndexOffset    = jnum("$.sprite_index_offset", 0);
    s_setup.viewWidth            = jnum("$.view_width", 0);
    s_setup.viewHeight           = jnum("$.view_height", 0);
    s_setup.extraTime            = jnum("$.extra_time", 0);
    s_setup.penaltyShootout      = jnum("$.penalty_shootout", 0);
    s_setup.topTeamMarkedPlayer    = jnum("$.top_team_marked_player", -1);
    s_setup.bottomTeamMarkedPlayer = jnum("$.bottom_team_marked_player", -1);
    s_setup.topTeamCoachNo       = jnum("$.top_team_coach_no", 0);
    s_setup.topTeamPlayerNo      = jnum("$.top_team_player_no", 0);
    s_setup.bottomTeamCoachNo    = jnum("$.bottom_team_coach_no", 0);
    s_setup.bottomTeamPlayerNo   = jnum("$.bottom_team_player_no", 0);
    for (i = 0; i < 2; i++) {
        snprintf(buf, sizeof buf, "$.teams[%d].blob_off", i);
        s_setup.team[i].blobOff = jnum(buf, -1);
        snprintf(buf, sizeof buf, "$.teams[%d].blob_len", i);
        s_setup.team[i].blobLen = jnum(buf, 0);
        snprintf(buf, sizeof buf, "$.teams[%d].global_team_number", i);
        s_setup.team[i].globalTeamNumber = jnum(buf, 0);
        snprintf(buf, sizeof buf, "$.teams[%d].team_controls", i);
        s_setup.team[i].teamControls = jnum(buf, 0);
        snprintf(buf, sizeof buf, "$.teams[%d].name", i);
        jstr(buf, s_setup.team[i].name, (int)sizeof s_setup.team[i].name);
        if (s_setup.team[i].blobOff < 0 || s_setup.team[i].blobLen != (int)sizeof(TeamFileEx)) {
            fprintf(stderr, "[replay] team %d record is %d bytes, expected %d\n",
                    i, s_setup.team[i].blobLen, (int)sizeof(TeamFileEx));
            return 1;
        }
    }
    for (i = 0; i < 6; i++) {
        snprintf(buf, sizeof buf, "$.tactics[%d].blob_off", i);
        s_setup.tactics[i].blobOff = jnum(buf, -1);
        snprintf(buf, sizeof buf, "$.tactics[%d].blob_len", i);
        s_setup.tactics[i].blobLen = jnum(buf, 0);
        if (s_setup.tactics[i].blobOff < 0 || s_setup.tactics[i].blobLen != 370) {
            fprintf(stderr, "[replay] tactics %d record is %d bytes, expected 370\n",
                    i, s_setup.tactics[i].blobLen);
            return 1;
        }
    }

    /* SWOS's camera IS the view's top-left in world space and every camera
     * constant is derived from the view size, so a recording made at a
     * different resolution has a legitimately different camera. Compare
     * those fields only when the geometry matches; everything else (sprite
     * positions are WORLD coordinates) is comparable regardless. */
    /* Simulate at the geometry the recording was made in. SWOS reads the view
     * size from inside the simulation — the onScreen cull gates which players
     * auto-selection may pick, and every camera constant is derived from it
     * (display.h) — so replaying a 320x200 recording at 336x272 diverges by
     * construction, not by defect. The renderer still draws at _screen_width. */
    /* Note the cull is NOT this width — it is a fixed 336 in both releases
     * (sprites.c). Only the camera constants and the cull HEIGHT follow the
     * recording's view. */
    if (s_setup.viewWidth > 0 && s_setup.viewHeight > 0) {
        _sim_view_width  = (int16_t)s_setup.viewWidth;
        _sim_view_height = (int16_t)s_setup.viewHeight;
    }
    s_viewMatches = (s_setup.viewWidth == (int)_sim_view_width &&
                     s_setup.viewHeight == (int)_sim_view_height);

    if (parseSchema((const char *)(s_file + hdr[3]), (int)hdr[4], level) != 0)
        return 1;

    buildRegistry();
    if (bindSchema() != 0)
        return 1;

    s_maxErrors = maxErrors > 0 ? maxErrors : 1;
    s_armed = 1;

    fprintf(stderr, "[replay] %s: v%d.%d  %s v %s\n", path, s_setup.major, s_setup.minor,
            s_setup.team[0].name, s_setup.team[1].name);
    fprintf(stderr, "[replay] %u frames (%d..%d), %u fields/frame, %u compared\n",
            s_frameCount, s_setup.firstFrame, s_setup.lastFrame,
            s_fieldCount, s_compareCount);
    if (s_viewSkipped)
        fprintf(stderr, "[replay] view %dx%d recorded vs %dx%d simulated — %u camera/on-screen "
                        "field(s) not compared\n",
                s_setup.viewWidth, s_setup.viewHeight,
                (int)_sim_view_width, (int)_sim_view_height, s_viewSkipped);
    return 0;
}

int ReplayTest_Armed(void)   { return s_armed; }
int ReplayTest_Running(void) { return s_running; }
int ReplayTest_OwnsGameTick(void) { return s_running; }
uint32_t ReplayTest_FrameIndex(void) { return s_frame; }
uint32_t ReplayTest_FrameCount(void) { return s_frameCount; }

/* ===================================================================
 * CalculateDeltaXAndY reference vectors
 *
 * swos-port's RecordedDataTest carries ten vectors for the DOS routine
 * (kCalculateDeltaXAndYTestData). It is the movement primitive every sprite
 * delta comes from, so checking it directly separates "our trig is wrong"
 * from "our trig got different inputs" when a replay diverges on a position.
 * The vectors are the DOS routine's output, i.e. WITH the 41/64 speed_factor
 * (the ratio is visible in them: 92160 -> 59040 exactly), so the run forces
 * that scale on regardless of the current game style.
 * =================================================================== */

static const struct {
    int16_t speed, x, y, destX, destY;
    int32_t angle;                 /* -1 = no movement (0xFFFF) */
    int32_t deltaX, deltaY;
} k_deltaVectors[] = {
    { 1168, 396, 373, 757, 447, 0x46,  0x17048,      0x349d      },
    { 1168, 757, 447, 396, 373, 0xc6, (int32_t)0xfffe8ccd, (int32_t)0xffffc878 },
    {    0, 396, 373, 757, 447, 0x46,  0,            0           },
    {    0, 757, 447, 396, 373, 0xc6,  0,            0           },
    { 1168, 396, 373, 396, 373,   -1,  0,            0           },
    { 1168, 396, 373, 396, 373,   -1,  0,            0           },
    {  500, 200, 300, 200, 100, 0x00,  0, (int32_t)0xffff5fd8    },
    {  500, 200, 100, 200, 300, 0x80,  0,  0x00009ee9            },
    { 1024,   0,   0,   1,   1, 0x60,  0x0000e6a0,   0x0000e6a0  },
    { 1024,   1,   1,   0,   0, 0xe0, (int32_t)0xffff16d0, (int32_t)0xffff16d0 },
};

int ReplayTest_MathSelfTest(void)
{
    int i, bad = 0;
    int16_t savedNum = _gMoveDeltaScaleNum, savedDen = _gMoveDeltaScaleDen;

    _gMoveDeltaScaleNum = 41;      /* the vectors are the DOS (scaled) routine */
    _gMoveDeltaScaleDen = 64;
    for (i = 0; i < (int)(sizeof k_deltaVectors / sizeof *k_deltaVectors); i++) {
        int32_t dx = 0, dy = 0;
        uint16_t dir = CalculateDeltaXAndY(k_deltaVectors[i].destX, k_deltaVectors[i].destY,
                                           k_deltaVectors[i].x, k_deltaVectors[i].y,
                                           k_deltaVectors[i].speed, &dx, &dy);
        int32_t wantDir = k_deltaVectors[i].angle < 0 ? 0xFFFF : k_deltaVectors[i].angle;
        if ((int32_t)dir != wantDir || dx != k_deltaVectors[i].deltaX ||
            dy != k_deltaVectors[i].deltaY) {
            fprintf(stderr, "[replay] CalculateDeltaXAndY vector %d: "
                    "dir %ld (want %ld)  dx %ld (want %ld)  dy %ld (want %ld)\n",
                    i, (long)dir, (long)wantDir, (long)dx, (long)k_deltaVectors[i].deltaX,
                    (long)dy, (long)k_deltaVectors[i].deltaY);
            bad++;
        }
    }
    _gMoveDeltaScaleNum = savedNum;
    _gMoveDeltaScaleDen = savedDen;
    fprintf(stderr, "[replay] CalculateDeltaXAndY: %d/%d reference vectors match\n",
            (int)(sizeof k_deltaVectors / sizeof *k_deltaVectors) - bad,
            (int)(sizeof k_deltaVectors / sizeof *k_deltaVectors));
    return bad ? 2 : 0;
}

/* ===================================================================
 * Setup + run
 * =================================================================== */

static void installTeam(int i, TeamFileEx *dst)
{
    memcpy(dst, s_blob + s_setup.team[i].blobOff, sizeof(TeamFileEx));
    /* The 684-byte record is a fixed-layout binary team record (the .EDT
     * shape), so it copies whole — but its ONE 16-bit field carries a byte
     * order, so restore it from the named scalar rather than trusting the
     * blob on a big-endian host. */
    dst->team.globalTeamNumber = (int16_t)s_setup.team[i].globalTeamNumber;
}

/* ===================================================================
 * The recording's own player values
 * ===================================================================
 *
 * A recorded team record carries `PlayerFile.cachedMatchValue` for all 16
 * players: what the REFERENCE build's UpdatePlayerPositionAndReturnRole
 * computed for that squad in that formation, before the match. For the ten
 * outfield starters that is a whole GetPlayerPrice run — the 35-quadrant
 * walk over the team's tactics coverage, six weight tables, the crowding
 * count and the nearness ratio — reduced to one byte we can check exactly.
 *
 * So this is 32 free assertions per recording, on setup state the frame
 * comparison never reaches: AdjustPlayerSkills scales every skill nibble by
 * this value, so a wrong price is the difference between reproducing a
 * recorded match and not.
 *
 * It is reported, not fatal. The corpus was recorded from a DOS release
 * whose formation COVERAGE differs from the Amiga's in a handful of bytes
 * (docs/TACTICS_POSITION_VALUE.md section 9), so a mismatch here can be data
 * rather than code — the report says which player, and the coverage byte
 * behind it is then a two-line lookup.
 */
static void checkPlayerValues(void)
{
    uint8_t savedPositions[32];
    int total = 0, bad = 0, i;

    memcpy(savedPositions, _playerPositionsArray, sizeof savedPositions);

    for (i = 0; i < 2; i++) {
        TeamFileEx *team = &_selectedTeams[i];
        const TeamFileEx *rec = (const TeamFileEx *)(s_blob + s_setup.team[i].blobOff);
        uint8_t savedDirty = team->team.playerValuesDirty;
        uint8_t saved[16];
        int j;

        for (j = 0; j < 16; j++)
            saved[j] = (uint8_t)team->players[j].cachedMatchValue;

        /* Force the recompute: with the flag CLEAN the routine just hands
         * back the byte we are trying to check. */
        team->team.playerValuesDirty = TEAM_VALUES_DIRTY;
        for (j = 0; j < 16; j++) {
            uint8_t slot = team->team.playerNumbers[j];
            uint8_t got, want;
            /* The routine indexes playerPositionsArray by this slot, so a
             * corrupt lineup entry would write past it — skip rather than
             * follow it. */
            if (slot >= 16) continue;
            (void)UpdatePlayerPositionAndReturnRole(team, (uint16_t)j);
            got  = (uint8_t)team->players[slot].cachedMatchValue;
            want = (uint8_t)rec->players[slot].cachedMatchValue;
            total++;
            if (got == want) continue;
            bad++;
            fprintf(stderr, "[replay]   %s player %d (%.15s): value recorded=%u actual=%u\n",
                    s_setup.team[i].name, j, (const char *)rec->players[slot].playerName,
                    want, got);
        }

        for (j = 0; j < 16; j++)
            team->players[j].cachedMatchValue = (int8_t)saved[j];
        team->team.playerValuesDirty = savedDirty;
    }

    memcpy(_playerPositionsArray, savedPositions, sizeof savedPositions);
    fprintf(stderr, "[replay] player values: %d/%d match%s\n",
            total - bad, total, bad ? " — see above (formation coverage, docs/TESTS.md)" : "");
}

static void applySetup(void)
{
    uint8_t *tac[6] = { _teamTacticsUSERA, _teamTacticsUSERB, _teamTacticsUSERC,
                        _teamTacticsUSERD, _teamTacticsUSERE, _teamTacticsUSERF };
    int i;

    /* The corpus is DOS gameplay; without the DOS constants every physics
     * field diverges on frame 1 and the report says nothing useful. */
    /* The rollback rig uses a recording for its setup and inputs only and compares
     * the game with itself: it plays whichever gameplay the command line chose, and it
     * must NOT pin the RNG -- a Rand() that returns a constant would hide every RNG
     * variable the rollback registry is missing. */
    if (!Rollback_Enabled()) {
        if (!_gDosGameplay) {
            fprintf(stderr, "[replay] forcing --dos-gameplay=on (the corpus is DOS)\n");
            _gDosGameplay = 1;
        }
        _replayFixedRandom = s_setup.fixedRandomValue;
    }


    _pitchTypeOrSeason   = (uint16_t)s_setup.pitchTypeOrSeason;
    _friendlyPitchType   = (uint16_t)s_setup.pitchType;
    _pitchType           = (uint16_t)s_setup.pitchType;
    _gameLength          = (uint16_t)s_setup.gameLength;
    /* An instant replay leaves the loop body the rollback rig hooks (GameLoop's
     * PlayInstantReplayLoop branch), so the rig plays with them off. */
    _autoReplays         = Rollback_Enabled() ? 0 : (uint16_t)s_setup.autoReplays;
    _allPlayerTeamsEqual = (uint16_t)s_setup.allPlayerTeamsEqual;
    /* A v1.0/1.1 header stops before season/substitutes. Forcing 0 there
     * benches players 11..15 (players[].skip) before the first frame, so
     * leave the options' own values and say which run is not fully pinned. */
    if (s_setup.season >= 0)         _friendlySeason = (uint16_t)s_setup.season;
    if (s_setup.minSubstitutes >= 0) _minSubstitutes = (uint16_t)s_setup.minSubstitutes;
    if (s_setup.maxSubstitutes >= 0) _maxSubstitutes = (uint16_t)s_setup.maxSubstitutes;
    if (s_setup.season < 0)
        fprintf(stderr, "[replay] v%d.%d header carries no season/substitute counts — "
                        "using this build's options (season=%u, subs %u-%u)\n",
                s_setup.major, s_setup.minor, _friendlySeason,
                _minSubstitutes, _maxSubstitutes);
    _autoSaveHighlights  = 0;      /* a highlights save would fork the state */
    _gameType            = 0;      /* friendly path */
    _trainingGame        = 0;
    _isCareer            = 0;

    /* Front-end work the rig skips but the tables depend on: InitUserTactics
     * copies the last two player rows of the 5-3-2 over the 4-4-2's and the
     * Sweep's (SWOS2.s:43757 / swos.asm:22569) and seeds USER_A..F from the
     * 4-4-2. The real game does it in InitMainMenuStuff; without it the
     * formations the match plays with are whatever was baked, which for the
     * SWOS_DOS_TACTICS variant is 60 bytes of dead dseg data. The recording's
     * own user tactics are copied over the USER slots afterwards, which is the
     * same order the game loads a .TAC in. */
    InitUserTactics();

    for (i = 0; i < 6; i++)
        memcpy(tac[i], s_blob + s_setup.tactics[i].blobOff, 370);

    installTeam(0, &_selectedTeams[0]);
    installTeam(1, &_selectedTeams[1]);
    _numSelectedTeams = 2;

    /* Before anything recomputes them: the recording's own per-player match
     * values are 32 checkable GetPlayerPrice results. */
    checkPlayerValues();

    /* The front end was skipped, so the menu sprite graphics the stadium
     * line-up draws (the little player figures, sprites 210-213) were never
     * extracted from menus.raw — DrawMenuItem SIGSEGVs on the missing
     * header. Run just the sprite extraction, not PrepareMenuBackground
     * (whose second step overwrites the frontbuffer). Same fix arcade mode
     * needs for the same reason. */
    ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    InitTeams();
}

void ReplayTest_SetupPlayers(TeamFileEx *team1, TeamFileEx *team2)
{
    /* What the two ShowPlayMatchMenu prompts would have produced, taken
     * from the recording instead: which joystick drove which team, and
     * whether either side was a coach. Mirrors the tail of SetupPlayers. */
    _isCareer = 0;
    _exitGameFlag = 0;
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;
    _twoPlayers = (team1->team.teamControls != 1 /* TEAM_COMPUTER */ &&
                   team2->team.teamControls != 1) ? 1 : 0;
    _pl1Coach = 0;
    _pl2Coach = 0;
    if (team1->team.teamControls == 3 /* TEAM_COACH */) *(uint8_t *)&_pl1Coach = 0xFF;
    if (team2->team.teamControls == 3)                  *(uint8_t *)&_pl2Coach = 0xFF;
    _topTeamPlayerNo    = (uint16_t)s_setup.topTeamPlayerNo;
    _topTeamCoachNo     = (uint16_t)s_setup.topTeamCoachNo;
    _bottomTeamPlayerNo = (uint16_t)s_setup.bottomTeamPlayerNo;
    _bottomTeamCoachNo  = (uint16_t)s_setup.bottomTeamCoachNo;
    _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
    _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
}

static void finish(int code)
{
    if (s_finished) return;
    s_finished = 1;
    s_running = 0;
    /* --record-replay: the recording ends here, before the process does. */
    { extern void ReplayRec_MatchEnd(void); ReplayRec_MatchEnd(); }
    {
        /* A netplay match reports on its session, and fails if the session did. */
        int np = NetplayMatch_Finish();
        if (code == 0) code = np;
    }
    if (Rollback_Enabled()) {
        /* Nothing was compared with the recording; the rollback rig decides. A
         * structural replay failure (a frame that skipped the loop body) still fails. */
        int rb;
        fprintf(stderr, "[replay] inputs replayed for %u of %u frames\n", s_frame, s_frameCount);
        rb = Rollback_Finish();
        if (code == 0) code = rb;
        fflush(stderr);
        fflush(stdout);
        exit(code);
    }
    /* A run that kept going past a divergence (--replay-max-errors) still
     * failed, whatever reason it finally stopped for. */
    if (s_errors && code == 0) code = 2;
    if (code == 0)
        fprintf(stderr, "[replay] PASS — %u/%u frames matched (%u fields each)\n",
                s_frame, s_frameCount, s_compareCount);
    else
        fprintf(stderr, "[replay] FAIL — diverged after %u of %u frames\n",
                s_frame, s_frameCount);
    fflush(stderr);
    fflush(stdout);
    exit(code);
}

void ReplayTest_Finish(int32_t code)
{
    finish((int)code);
}

void ReplayTest_Run(void)
{
    if (!s_armed) return;
    applySetup();
    s_running = 1;
    _gFrameStepInput = 1;      /* input a frame at a time (game_loop.h) */
    s_frame = 0;
    s_rec = NULL;
    if (Rollback_Enabled())
        Rollback_MatchStart();
    if (NetplayMatch_Enabled() && NetplayMatch_Start() != 0)
        finish(1);
    fprintf(stderr, "[replay] kick-off\n");
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);
    /* The match ended before the recording did — every remaining frame is a
     * divergence in itself, so say so rather than reporting a pass. (Not for the
     * rollback rig, which plays its own match and only borrows the inputs.) */
    if (s_frame < s_frameCount && !Rollback_Enabled()) {
        fprintf(stderr, "[replay] match ended at frame %u, recording has %u\n",
                s_frame, s_frameCount);
        finish(2);
    }
    finish(0);
}

/* ===================================================================
 * Per-frame hooks
 * =================================================================== */

/* Recorded control bits (swos-port's ControlFlags). The stick and fire bits are
 * MatchPlayerInput's own (match_step.h). */
#define RC_UP     1
#define RC_DOWN   2
#define RC_LEFT   4
#define RC_RIGHT  8
#define RC_FIRE   32
#define RC_BENCH  64   /* per PLAYER; the rig injects the per-TEAM button instead */
#define RC_ESC    128

_Static_assert(RC_UP == MATCH_UP && RC_DOWN == MATCH_DOWN && RC_LEFT == MATCH_LEFT &&
               RC_RIGHT == MATCH_RIGHT && RC_FIRE == MATCH_FIRE,
               "the recorded stick and fire bits are MatchPlayerInput's");

void ReplayTest_RecordedInput(uint32_t frame, MatchPlayerInput in[MATCH_PLAYERS])
{
    const TeamGeneralInfo *team[2] = { &_topTeamData, &_bottomTeamData };
    const int stickAndFire = RC_UP | RC_DOWN | RC_LEFT | RC_RIGHT | RC_FIRE;
    const uint8_t *rec;
    int p1, p2, key, bench[2], i;

    if (frame >= s_frameCount) {
        memset(in, 0, MATCH_PLAYERS * sizeof *in);
        return;
    }
    rec = s_frames + (size_t)frame * s_frameSize;
    p1 = (int)recordField(rec, "p1_controls");
    p2 = (int)recordField(rec, "p2_controls");
    key = (int)recordField(rec, "user_key");
    /* Per TEAM, not per player: see the schema note on top_bench_button. Each is
     * pressed by the player who controls that team, and MatchStep_ApplyInput
     * routes that player's bench call back to it. */
    bench[0] = (int)recordField(rec, "top_bench_button");
    bench[1] = (int)recordField(rec, "bottom_bench_button");

    in[0].controls = (uint8_t)(p1 & stickAndFire);
    in[1].controls = (uint8_t)(p2 & stickAndFire);
    /* The recorded userKey belongs to no player (`S`, the stats screen, is the
     * only one the corpus uses); it travels as player 1's key. */
    in[0].key = (uint8_t)key;
    in[1].key = 0;

    for (i = 0; i < 2; i++) {
        int owner;
        if (!bench[i]) continue;
        owner = MatchStep_TeamOwner(team[i]);
        if (owner) {
            in[owner - 1].controls |= MATCH_BENCH;
        } else if (!s_warnedBench) {
            s_warnedBench = 1;
            fprintf(stderr, "[replay] frame %u: the %s team's bench button is pressed, but "
                            "no human controls that team — not injected\n",
                    frame, i == 0 ? "top" : "bottom");
        }
    }

    /* Not injected yet — say so once, so a divergence downstream is attributed
     * to the missing input and not mistaken for a port bug. */
    if ((p1 | p2) & RC_ESC && !s_warnedEsc) {
        s_warnedEsc = 1;
        fprintf(stderr, "[replay] frame %u presses ESC — not injected yet\n", frame);
    }
}

/* Match state the recording sets up before its first frame. Applied before the rollback
 * rig's first frame boundary, so a window starting at frame 0 saves it and a rewind to
 * frame 0 keeps it -- it is never applied twice. */
static void applyFirstFrameSetup(void)
{
    _extraTimeState = (int16_t)s_setup.extraTime;
    _penaltiesState = (int16_t)s_setup.penaltyShootout;
    _topTeamInGame.markedPlayer    = (int16_t)s_setup.topTeamMarkedPlayer;
    _bottomTeamInGame.markedPlayer = (int16_t)s_setup.bottomTeamMarkedPlayer;
    /* The Amiga VBlank ISR advances both tick counters once per frame in
     * lockstep with this loop; the rig owns them (see the header) and
     * starts them from the kick-off frame. _lastGameTick has to be reset
     * with them: ReadTimerDelta turns the difference into _lastFrameTicks,
     * which every stoppage/referee/clock timer is stepped by, and the
     * value it held coming out of the menus is whatever the wall clock had
     * reached — the one input that made two runs of the same recording
     * diverge at different frames.
     *
     * _lastGameTick starts one BEHIND the tick, not equal to it. GameLoop
     * reads the timer delta once before the loop and then jumps to the
     * vblank wait, so on Amiga a VBlank — one tick — falls between that
     * read and the first pass through the body: frame 0's _lastFrameTicks
     * is 1, like every other frame's. Leaving them equal gives frame 0 a
     * delta of 0, and every timer-driven transition in the match then
     * lands one frame late — three recordings stalled the kick-off at
     * frame 99 with game_state still ST_STARTING_GAME, where the fixed rig
     * runs to 912 and 1253.
     *
     * The TICK ITSELF still starts at 0, so currentGameTick == the frame
     * number and the dozens of `tick & mask` AI decisions keep the phase
     * the reference rig runs at. Starting it at 1 instead gets the same
     * delta but shifts that phase, which costs two recordings a few
     * hundred frames (zkz-10190651 1796 -> 1028) — the two effects look
     * alike and are not. */
    _currentGameTick = 0;
    _currentTick = 0;
    _lastGameTick = 0xFFFF;
    /* No button held and no bench call pending from before kick-off. */
    MatchStep_Reset();
}

void ReplayTest_PrepareFirstFrame(void)
{
    if (s_firstFrame) {
        s_firstFrame = 0;
        applyFirstFrameSetup();
    }
}

void ReplayTest_FrameStart(void)
{
    if (!s_running) return;
    ReplayTest_PrepareFirstFrame();
    CONTEST_LOG(ContestLog_SetFrame(s_frame));     /* --contest-log under the rig too */
    if (Rollback_Enabled()) {
        Rollback_FrameBoundary(s_frame);    /* may rewind s_frame */
        /* The rollback rig plays its own match, which can end before the recorded
         * inputs do. Stop before GameLoop heads for the post-match menu, which would
         * wait for input forever. */
        if (_playGame == 0) {
            fprintf(stderr, "[replay] the match is over at frame %u\n", s_frame);
            finish(0);
            return;
        }
    }
    if (s_frame >= s_frameCount) {
        fprintf(stderr, "[replay] recording exhausted at frame %u — match still running\n",
                s_frame);
        finish(0);
        return;
    }
    if (s_rec) {
        /* The previous iteration took a branch that skipped the compare (a
         * highlights or instant-replay detour). Say so — silently re-using
         * the record would desync input from state. */
        fprintf(stderr, "[replay] frame %u never reached the compare point "
                        "(replay/highlights branch?)\n", s_frame);
        finish(2);
        return;
    }
    s_rec = s_frames + (size_t)s_frame * s_frameSize;
    g_rtFrameNo = (int32_t)s_setup.firstFrame + (int32_t)s_frame;  /* see above */

    /* A netplay match applies the input inside its own frame step, which GGPO
     * repeats (src/netplay_match.c). */
    if (!NetplayMatch_Enabled()) {
        MatchPlayerInput in[MATCH_PLAYERS];
        ReplayTest_RecordedInput(s_frame, in);
        MatchStep_ApplyInput(in, (int32_t)s_frame);
    }
}

static void reportHeader(void)
{
    if (s_errors == 0)
        fprintf(stderr, "[replay] DIVERGENCE at frame %u (recording frame %d)\n",
                s_frame, s_setup.firstFrame + (int)s_frame);
}

static void reportMismatch(const RtField *f, int64_t rec, int64_t ours)
{
    reportHeader();
    fprintf(stderr, "[replay]   %-44s recorded=%lld  actual=%lld\n",
            f->name, (long long)rec, (long long)ours);
    s_errors++;
}

static void reportPointerMismatch(const RtField *f, int64_t rec,
                                  const void *want, const void *got)
{
    char a[48], b[48];
    reportHeader();
    if (want)
        describePointer(f->ptrKind, want, a, sizeof a);
    else
        snprintf(a, sizeof a, "%lld (out of range)", (long long)rec);
    describePointer(f->ptrKind, got, b, sizeof b);
    fprintf(stderr, "[replay]   %-44s recorded=%-14s actual=%s\n", f->name, a, b);
    s_errors++;
}

void ReplayTest_FrameCheck(void)
{
    uint32_t i;

    if (!s_running || !s_rec) return;

    for (i = 0; !Rollback_Enabled() && i < s_fieldCount; i++) {
        const RtField *f = &s_field[i];
        int64_t rec, ours;
        if (f->bind < 0) continue;
        rec  = readTyped(s_rec + f->off, f->type);
        ours = readTyped(s_reg[f->bind].addr, s_reg[f->bind].type);
        if (f->cmp == RC_PTR) {
            const void *want = NULL;
            const void *got  = (const void *)(intptr_t)ours;
            int ok = resolvePointer(f->ptrKind, rec, &want);
            if (ok && want == got)
                continue;
            reportPointerMismatch(f, rec, ok ? want : NULL, got);
            if (s_errors >= s_maxErrors) { s_rec = NULL; finish(2); return; }
            continue;
        }
        if (f->cmp == RC_SPR && rec >= 0)
            rec -= s_setup.spriteIndexOffset;   /* recording's bank -> ours */
        if (f->cmp == RC_BOOL) {
            if (!rec == !ours) continue;
        } else if (sameValue(rec, ours, f->type)) {
            continue;
        }
        reportMismatch(f, rec, ours);
        if (s_errors >= s_maxErrors) {
            s_rec = NULL;
            finish(2);
            return;
        }
    }
}

void ReplayTest_FrameDone(void)
{
    if (!s_running || !s_rec) return;
    s_rec = NULL;
    s_frame++;
    if ((s_frame % 500) == 0)
        fprintf(stderr, "[replay] ... %u/%u frames matched\n", s_frame, s_frameCount);
}

void ReplayTest_FrameEnd(void)
{
    if (!s_running || !s_rec) return;
    ReplayTest_FrameCheck();
    MatchStep_AdvanceClock();
    ReplayTest_FrameDone();
}

/* ===================================================================
 * Rollback rig support (docs/ROLLBACK.md)
 * =================================================================== */

void ReplayTest_GetDriverState(ReplayDriverState *st)
{
    st->frame = s_frame;
}

void ReplayTest_SetDriverState(const ReplayDriverState *st)
{
    s_frame = st->frame;
    s_rec = NULL;
}

void ReplayTest_ForEachObservable(void (*fn)(void *addr, uint32_t size, void *ctx), void *ctx)
{
    uint32_t i;
    for (i = 0; i < s_regCount; i++)
        fn(s_reg[i].addr, k_rtSize[s_reg[i].type], ctx);
}

/* Hashed in the portable encoding (sim_state.h), so runs of different builds -- 32- or
 * 64-bit, another OS -- can be compared: every value big-endian at its recorded width,
 * every pointer as the kind and identity of what it points at. */
uint64_t ReplayTest_ObservableHash(void)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t i;
    for (i = 0; i < s_regCount; i++) {
        const uint8_t *p = (const uint8_t *)s_reg[i].addr;
        uint8_t buf[8];
        uint32_t n = 0, k;
        switch (s_reg[i].type) {
        case RT_U8: case RT_S8:
            buf[0] = p[0];
            n = 1;
            break;
        case RT_U16: case RT_S16: {
            uint16_t v;
            memcpy(&v, p, 2);
            BE_WRITE_U16(buf, v);
            n = 2;
            break;
        }
        case RT_U32: case RT_S32: {
            uint32_t v;
            memcpy(&v, p, 4);
            BE_WRITE_U32(buf, v);
            n = 4;
            break;
        }
        case RT_PTR: {
            void *ptr;
            uint32_t kind = 0, id = 0xFFFFFFFEu;
            memcpy(&ptr, p, sizeof ptr);
            SimState_PointerIdentity(ptr, &kind, &id);
            BE_WRITE_U32(buf, kind);
            BE_WRITE_U32(buf + 4, id);
            n = 8;
            break;
        }
        }
        for (k = 0; k < n; k++)
            h = (h ^ buf[k]) * 0x100000001b3ull;
    }
    return h;
}

#endif /* SWOS_SDL_BUILD */
