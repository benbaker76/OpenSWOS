/*
 * sim_state.c -- the match state registry. See include/sim_state.h.
 *
 * Every entry is backed by evidence (docs/ROLLBACK.md), and so is every exclusion:
 *
 *   - the rollback rig's leave-one-out runs over the corpus (--rollback=discover):
 *     leaving the variable out of a rollback changed the game;
 *   - or the variable is written by code reachable from GameLoop that the corpus
 *     never reaches (penalties, cards, injuries, substitutions, instant replays),
 *     and read by the match -- found from the executable, not from the recordings;
 *   - or it is state the random generator's call pattern depends on. The crowd
 *     chants, advertising boards and crowd animation call Rand() in both original
 *     releases, so their state decides what the NEXT gameplay Rand() returns.
 *
 * Deliberately NOT here:
 *
 *   - Per-match constants set before kick-off (ApplyGameStyle, InitGameRestoreTeams:
 *     the gs* timings, ball physics constants, pitch factors, card chance). Equal on
 *     both peers by the match setup, never changed by play.
 *   - Variables no instruction in the executable reads and no data relocation points
 *     at: _ballSpeed, _ballSpeedAfterSave, _cameraSnapToPlayerCount,
 *     _disallowedTurnFlagsCounter, _turnDirCopiedFromCameraCount, _halfNumber,
 *     _timeVar, _strikeDestY/Z, _lastCameraDirection, _lastPlayerTurnFlags,
 *     _goalkeeperDiveAttemptCount, _nearestTeammatePtr, _findClosestBallDist,
 *     _teamNumThatScored, _substitutedPlX/Y, _pendingSampleId3_4 and the dead* /
 *     *WriteOnly* variables of the disassembly.
 *   - Values recomputed from scratch within the frame that reads them (the
 *     prevLastPlayer/prevLastTeamPlayed and freeKickDestX_local statics of
 *     update_players.c).
 *   - Render state describing what is already drawn: scroll offsets and their
 *     Saved0/Saved1 copies, frontbuffers, render stacks, palette, copper list, and
 *     the HUD layer. And audio mixer state. (The draw list is NOT render state: a
 *     render pass sets each listed sprite's onScreen, which the match reads.)
 *   - Raw joystick/keyboard input: netplay supplies it every frame.
 *   - _advert1/2/3Pointers: copied from the sprite graphics once, at load
 *     (CopyAdvertisementsData, from color_sprites.c), and only read after that. The
 *     advert scroll rewrites the graphics' sprData from them every frame an advert is
 *     shown, driven by _currentAnimatedAdLine, which is here.
 *
 * The tiers: SIM may not depend on VIEW, or a peer resynced with SIM alone would
 * checksum differently. `scripts/rollback_test.sh --restore sim` proves it.
 *
 * _s_pitchFullPtrs (pitch.c) mirrors the fan tile slots of _pitchDatBuffer at full
 * pointer width for the renderer; SimState_Load rebuilds it from the slots.
 */
#ifdef SWOS_SDL_BUILD
#include <stdio.h>      /* SimState_DescribePortableByte only; the m68k build has no stdio */
#endif
#include "sim_state.h"
#include "game_types.h"
#include "animated_patterns.h"
#include "sprites.h"
#include "sim_state_ids.h"
#include "match_step.h"
#include "endian_helpers.h"
#include "swos_io.h"        /* swos_io_alloc/free -- the m68k build has no malloc */

#include <string.h>

/* ---- random generators ---- */
extern uint8_t _seed, _randXorIndex, _randXorKey;
extern uint8_t _seed2, _randXorIndex2, _randXorKey2;

/* ---- clock ---- */
extern volatile uint16_t _currentGameTick;
extern volatile uint16_t _currentTick;
extern uint16_t          _lastGameTick;
extern uint16_t          _lastFrameTicks[1];
extern uint16_t          _frameCount[1], _inGameCounter[1];
extern uint16_t          _gameSeconds[1], _secondsSwitchAccumulator[1];
extern uint8_t           _gameTime[4];
extern uint16_t          _playGame[1];

/* ---- match flow ---- */
extern uint16_t _gameState, _gameStatePl;
extern uint16_t _breakState[1], _breakCameraMode[1];
extern uint16_t _extraTimeState[1], _penaltiesState[1];
extern uint16_t _fireBlocked, _statsTimer, _resultTimer;
extern uint16_t _endGameCounter[1], _goalOut[1], _isGoalAttempt[1];
extern uint16_t _stoppageEventTimer[1], _stoppageTimerActive[1], _stoppageTimerTotal[1];
extern uint16_t _AI_maxStoppageTime[1];
extern uint16_t _teamPlayingUp[1], _teamStarting[1];
extern uint16_t _teamSwitchCounter;
extern uint16_t _playRefereeWhistle[1], _endOfHalfWriteOnly[1];
extern uint16_t _fadeState[1], _fadeInitFlag[1];
extern uint16_t _foulXCoordinate[1], _foulYCoordinate[1], _strikeDestX[1];
extern uint16_t _throwInPassOrKick[1], _waitForPlayerToGoInTimer[1];
extern uint16_t _nobodysBallTimer[1], _nobodysBallLastFrame[1], _ballOutOfGameTimer[1];
extern uint16_t _runSlower[1], _hideBall[1], _goalScored[1];
extern uint16_t _showStats[1];
extern int8_t   _statsNumber[6];
extern uint16_t _statsTeam1Goals, _statsTeam2Goals;
extern uint16_t _statsTeam1GoalsCopy[1], _statsTeam2GoalsCopy[1];
extern uint16_t _statsTeam1GoalsCopy2[1], _statsTeam2GoalsCopy2[1];

/* ---- camera ---- */
extern FixedPoint _cameraX, _cameraY;
extern uint16_t   _cameraXVelocity, _cameraYVelocity;
extern uint16_t   _cameraDirection[1], _cameraCoordinatesValid[1];
extern uint16_t   _cameraLeavingSubsTimer[1], _goalCameraMode[1];

/* ---- ball and player bookkeeping ---- */
extern uint16_t _ballQuadrantIndex[1], _playerXQuadrantOffset[1], _playerYQuadrantOffset[1];
extern uint16_t _ballDefensiveX[1], _ballDefensiveY[1], _ballDefensiveZ[1];
extern uint16_t _ballInGoalkeeperArea[1], _ballInLowerPenaltyArea[1], _ballInUpperPenaltyArea[1];
extern uint16_t _ballNextGroundX[1], _ballNextX[1], _ballNextY[1], _ballNextYGroundY[1];
extern uint16_t _ballNotHighX[1], _ballNotHighY[1], _ballNotHighZ[1];
extern uint16_t _playerHadBall[1];
extern uint8_t  _playerTurnFlags;
extern Sprite          *_lastPlayerPlayed, *_lastPlayerBeforeGoalkeeper, *_lastKeeperPlayed;
extern Sprite          *_lastPlayerScored;
extern TeamGeneralInfo *_lastTeamPlayed, *_lastTeamPlayedBeforeBreak, *_lastTeamScored;
extern TeamGeneralInfo *_teamScoredDataPtr;
extern TeamGame        *_teamScoredGamePtr;
extern uintptr_t        _currentScorer[1], _winningTeamPtr[1];
extern uint16_t         _lastTeamScoredNumber[1];

/* ---- AI ---- */
extern uint16_t _aiCounter[1], _aiResumePlayTimer[1], _aiTurnDirection[1];
extern uint16_t _aiRand[1], _aiAttackHalf[1], _totalSkillsFactorSum[1];

/* ---- teams ---- */
extern TeamGame        _topTeamInGame, _bottomTeamInGame;
extern TeamGeneralInfo _topTeamData, _bottomTeamData;
extern uint16_t        _team1StatsData[7], _team2StatsData[7];
extern uint16_t        _pl1Tactics[1], _pl2Tactics[1];

/* ---- sprites the match moves ---- */
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

/* ---- referee and cards ---- */
extern uint16_t _refState[1], _refTimer[1], _whichCard[1];
extern Sprite  *_bookedPlayer;

/* ---- controls: what the game derived from the inputs, including last frame's ---- */
extern uint16_t _pl1Direction[1], _pl2Direction[1];
extern uint8_t  _pl1Fire, _pl2Fire, _pl1ShortFire, _pl2ShortFire;
extern uint8_t  _pl1Up, _pl1Down, _pl1Left, _pl1Right, _pl2Up, _pl2Down, _pl2Left, _pl2Right;
extern uint8_t  _pl1UpRight, _pl1UpLeft, _pl1DownLeft, _pl1DownRight;
extern uint8_t  _pl2UpRight, _pl2UpLeft, _pl2DownLeft, _pl2DownRight;
extern uint8_t  _pl1WasUp, _pl1WasDown, _pl1WasLeft, _pl1WasRight;
extern uint8_t  _pl2WasUp, _pl2WasDown, _pl2WasLeft, _pl2WasRight;
extern uint8_t  _pl1UpPress, _pl1DownPress, _pl1LeftPress, _pl1RightPress;
extern uint8_t  _pl2UpPress, _pl2DownPress, _pl2LeftPress, _pl2RightPress;
extern uint16_t _pl1FireCounter[1], _pl2FireCounter[1];
extern uint8_t  _pl1LastFired, _pl2LastFired, _subsFire;

/* ---- substitutes bench ---- */
extern uint16_t _bench1Called, _bench2Called;
extern uint16_t _subsPl1TapCount, _subsPl2TapCount;
extern uint16_t _subsPrevPl1Direction, _subsPrevPl2Direction;
extern uint16_t _pl1BenchBlockWhileHoldingDirection, _pl2BenchBlockWhileHoldingDirection;
extern uint16_t _subsDirection, _subsState, _goToSubsTimer;
extern uint16_t _subsBlockDirections, _subsFireTimer, _subsBlockFire;
extern uint16_t _subsPrevDirection, _subsMovementDelayTimer;
extern uint16_t _subsTrainingTopTeam, _subsTeamsSwapped, _benchPickTeamCounter;
extern uint16_t _plToEnterGameIndex, _plToBeSubstitutedPos, _plToBeSubstitutedOrd;
extern uint16_t _selectedFormationEntry, _benchPlayerIndex;
extern uint16_t _inSubstitutesMenu[1], _leavingSubsMenu[1], _substituteInProgress[1];
extern uint16_t _substitutedPlDestX[1], _substitutedPlDestY[1];
extern uint16_t _pl1BenchTimeoutCounter[1], _pl2BenchTimeoutCounter[1];
extern uint16_t _benchColors[9], _benchOff[1];
extern uintptr_t _benchTeam[1], _benchTeamGame[1];
extern uint16_t _benchXLimit[1], _benchY[1], _opponentBenchY[1];
extern uint16_t _formationMenuX, _subsMenuX[1];
extern uint16_t _subsMenuBackgroundColor[1], _subsMenuEntryFrameColor[1], _subsMenuHighlightColor[1];
extern uint16_t _plComingX[1], _plComingY[1], _plComingYTmp[1], _plLeavingX[1];
extern uint8_t  _plNamesLengthTableCopy[32], _plOrdinalsTable[32];
extern uintptr_t _plPositions[1], _teamThatSubstitutes[1];
extern uint16_t _plSubstitutedX[1], _plSubstitutedY[1];
extern Sprite  *_substitutedPlSprite;
extern uint16_t _initialSubsState[1], _selectedPlayerInSubs[1], _showingUserStats[1];
extern uint16_t _team1NumSubs[1], _team2NumSubs[1];
extern uint16_t _team1NumAllowedInjuries[1], _team2NumAllowedInjuries[1];
extern uint16_t _team1GoalkeeperReplaced[1], _team2GoalkeeperReplaced[1];
extern uint16_t _penalty[1], _currentKey[1];

/* ---- penalty shoot-out ---- */
extern uint16_t _penaltiesTimer[1], _playingPenalties[1];
extern Sprite  *_penaltyShooterSprite;
extern uint16_t _team1PenaltyAttempts[1], _team1PenaltyGoals[1], _team1PenaltyShooterIndex[1];
extern uint16_t _team2PenaltyAttempts[1], _team2PenaltyGoals[1], _team2PenaltyShooterIndex[1];
extern uint16_t _savedTeam1Goals[1], _savedTeam2Goals[1], _dontShowScorers[1];
extern uint16_t _team1GoalsDigit1[1], _team1GoalsDigit2[1], _team2GoalsDigit1[1], _team2GoalsDigit2[1];

/* ---- instant replay: GameLoop leaves the match loop on these ---- */
extern uint16_t _instantReplayFlag[1], _saveHighlightScene[1], _fadeAndSaveHighlightScene[1];
extern uint16_t _userRequestedReplay, _replayState;
extern uint16_t _spaceReplayTimer[1], _gameCanceled[1], _paused[1], _playHighlightFlag[1];

/* ---- crowd chants (Rand) ---- */
extern uint16_t _nextChantTimer[1], _fadeInChantTimer[1], _fadeOutChantTimer[1];
extern uint16_t _crowdChantVolume[1], _currentCrowdSample[1], _crowdEventFlags[1];
extern uint16_t _sfxQueueCount[1], _sfxQueueWriteIdx[1], _goalCounter[1];
extern uint16_t _lastKickSampleTick[1], _crowdChantsOn[1];

/* ---- advertising boards (Rand) ---- */
extern uint16_t _advertSlideTimer[1], _advertIndex[1], _currentAnimatedAdLine[1];
extern Sprite   _advertInGameSprites[8];
extern uint16_t _spinBigS[1], _bigSIndex[1];
extern Sprite   _big_S_Sprite;

/* ---- crowd animation (Rand) ---- */
extern uint16_t _animPatternsIndex[1], _animPatternsState[1];
extern uint8_t  _animPatternsRandomTable[64];
extern uint16_t _animatedPatterns[8];
extern uint16_t _otherTeamScored[1], _patternStatic[1], _patternsGoalCounter[1], _showFansCounter[1];
extern uint8_t  _pitchDatBuffer[];

/* ---- the draw list: the simulation reads what the renderer derives from it ---- */
extern Sprite  *_sortedSprites[78];
extern uint16_t _numSpritesToRender;

/* ---- VIEW: highlights ---- */
extern char      _goalBase[0x4A38];
extern uint32_t *_currentHilPtr, *_currentGoalPtr;
extern uintptr_t _goalBasePtr[1], _nextGoalPtr[1];
extern uint16_t  _numScenesInHilFile[1], _hilSceneNumber[1];

/* ---- result, scorers, clock: sprites in the draw list, and what builds them ---- */
extern ScorerInfo _team1Scorers[8], _team2Scorers[8];
extern uint16_t   _team1TotalGoals[1], _team2TotalGoals[1];
extern Sprite     _team1ScorerSprite[8], _team2ScorerSprite[8];
extern uint16_t   _team1ScorerSpritesWidths[8], _team2ScorerSpritesWidths[8];
extern Sprite     _team1GoalsDigit1Sprite, _team1GoalsDigit2Sprite;
extern Sprite     _team2GoalsDigit1Sprite, _team2GoalsDigit2Sprite;
extern Sprite     _team1TotalGoalsDigit1Sprite, _team1TotalGoalsDigit2Sprite;
extern Sprite     _team2TotalGoalsDigit1Sprite, _team2TotalGoalsDigit2Sprite;
extern Sprite     _team1NameSprite, _team2NameSprite, _dashSprite;
extern Sprite     _gridUpperRowLeftSprite, _gridUpperRowRightSprite, _gridUpperRowExtraSprite;
extern Sprite     _gridMiddleRowLeftSprite, _gridMiddleRowRightSprite, _gridMiddleRowExtraSprite;
extern Sprite     _gridBottomRowLeftSprite, _gridBottomRowRightSprite, _gridBottomRowExtraSprite;
extern Sprite     _resultAfterTheGameSprite, _resultOnHalftimeSprite;
extern Sprite     _currentTimeSprite, _cornerFlagSprite2;
extern uint16_t   _copyTimeSprite[1];
extern uint16_t   _overlayFlagBackup[1], _currentPlayerIndicatorPosition[1];

/* The crowd rows of the pitch tile map: DrawAnimatedPatterns picks fan tiles only
 * from pitch rows 1-3 and 51-53 (sprite indices 21..83 and 1071..1133, 42 columns
 * per row), including the widescreen mirror, which stays within its row. */
#define FAN_ROWS_TOP_SLOT     42
#define FAN_ROWS_BOTTOM_SLOT  2142
#define FAN_ROWS_SLOTS        126

/* ---- portable layouts (sim_state.h) ----
 * Byte runs are copied, 16- and 32-bit values written big-endian, pointers written as
 * identities. Sprite and TeamGeneralInfo are the structs here with pointer fields;
 * TeamGame and ScorerInfo hold only 8- and 16-bit ones. Each field list tiles its struct
 * in order, and SimState_LayoutError checks that against this build's offsets. */
static const SimField k_bytesFields[]    = { { 0, 1, SIM_FIELD_BYTES, 0 } };
static const SimField k_word16Fields[]   = { { 0, 1, SIM_FIELD_WORD16, 0 } };
static const SimField k_word32Fields[]   = { { 0, 1, SIM_FIELD_WORD32, 0 } };
static const SimField k_tileSlotFields[] = { { 0, 1, SIM_FIELD_TILE_SLOT, 0 } };
static const SimLayout k_bytes    = { 1, 1, k_bytesFields };
static const SimLayout k_word16   = { 2, 1, k_word16Fields };
static const SimLayout k_word32   = { 4, 1, k_word32Fields };
static const SimLayout k_tileSlot = { 4, 1, k_tileSlotFields };

#define POINTER_LAYOUT(var, kind) \
    static const SimField var##Fields[] = { { 0, 1, SIM_FIELD_POINTER, kind } }; \
    static const SimLayout var = { sizeof(void *), 1, var##Fields };
POINTER_LAYOUT(k_ptrSprite,         SIM_PTR_SPRITE)
POINTER_LAYOUT(k_ptrTeamInfo,       SIM_PTR_TEAM_INFO)
POINTER_LAYOUT(k_ptrTeamGame,       SIM_PTR_TEAM_GAME)
POINTER_LAYOUT(k_ptrTactics,        SIM_PTR_TACTICS)
POINTER_LAYOUT(k_ptrGoalRing,       SIM_PTR_GOAL_RING)
#undef POINTER_LAYOUT

#define W16(st, first, n)   { offsetof(st, first), n, SIM_FIELD_WORD16, 0 }
#define W32(st, first, n)   { offsetof(st, first), n, SIM_FIELD_WORD32, 0 }
#define BYT(st, first, n)   { offsetof(st, first), n, SIM_FIELD_BYTES, 0 }
#define PTR(st, field, k)   { offsetof(st, field), 1, SIM_FIELD_POINTER, k }

static const SimField k_spriteFields[] = {
    W16(Sprite, teamNumber, 3),                     /* teamNumber, playerOrdinal, frameOffset */
    PTR(Sprite, animationTable, SIM_PTR_ANIM_TABLE),
    W16(Sprite, startingDirection, 1),
    BYT(Sprite, playerState, 2),                    /* playerState, playerDownTimer */
    W16(Sprite, field_E, 2),                        /* field_E, field_10 */
    PTR(Sprite, frameIndicesTable, SIM_PTR_FRAME_INDICES),
    W16(Sprite, frameIndex, 4),                     /* frameIndex .. frameSwitchCounter */
    W32(Sprite, x, 3),                              /* x, y, z */
    W16(Sprite, direction, 2),                      /* direction, speed */
    W32(Sprite, deltaX, 3),                         /* deltaX, deltaY, deltaZ */
    W16(Sprite, destX, 2),                          /* destX, destY */
    BYT(Sprite, field_3E, 6),
    W16(Sprite, visible, 3),                        /* visible, imageIndex, saveSprite */
    W32(Sprite, ballDistance, 1),
    W16(Sprite, field_4E, 16),                      /* field_4E .. sentAway */
};

/* A sprite of the in-match HUD (z 8000 and up: score panel, clock, scorers, the
 * controlled player's name, the spinning S). Its x is placed for this display's width
 * -- WIDE_MARGIN, HUD_MARGIN, Render_CameraX -- and its onScreen measured from that x,
 * so both differ between two peers on different displays while nothing the match
 * decides reads them. They are left out of the checksum and still carried by a saved
 * or sent state: not every HUD sprite is placed every frame (the result panel only
 * while it shows), so a resync that dropped them left stale positions. Everything
 * else stays shared -- `visible` and `y` decide the draw list's contents and order. */
#define L32(st, first, n)   { offsetof(st, first), n, SIM_FIELD_LOCAL32, 0 }
#define L16(st, first, n)   { offsetof(st, first), n, SIM_FIELD_LOCAL16, 0 }
static const SimField k_hudSpriteFields[] = {
    W16(Sprite, teamNumber, 3),
    PTR(Sprite, animationTable, SIM_PTR_ANIM_TABLE),
    W16(Sprite, startingDirection, 1),
    BYT(Sprite, playerState, 2),
    W16(Sprite, field_E, 2),
    PTR(Sprite, frameIndicesTable, SIM_PTR_FRAME_INDICES),
    W16(Sprite, frameIndex, 4),
    L32(Sprite, x, 1),                              /* this display's */
    W32(Sprite, y, 2),                              /* y, z */
    W16(Sprite, direction, 2),
    W32(Sprite, deltaX, 3),
    W16(Sprite, destX, 2),
    BYT(Sprite, field_3E, 6),
    W16(Sprite, visible, 3),
    W32(Sprite, ballDistance, 1),
    W16(Sprite, field_4E, 3),                       /* field_4E, field_50, fullDirection */
    L16(Sprite, onScreen, 1),                       /* this display's */
    W16(Sprite, field_56, 12),                      /* field_56 .. sentAway */
};
#undef L32
#undef L16

static const SimField k_teamInfoFields[] = {
    PTR(TeamGeneralInfo, opponentsTeam, SIM_PTR_TEAM_INFO),
    W16(TeamGeneralInfo, playerNumber, 3),          /* playerNumber, playerCoachNumber, isPlCoach */
    PTR(TeamGeneralInfo, inGameTeamPtr, SIM_PTR_TEAM_GAME),
    PTR(TeamGeneralInfo, teamStatsPtr, SIM_PTR_TEAM_STATS),
    W16(TeamGeneralInfo, teamNumber, 1),
    PTR(TeamGeneralInfo, spritesTable, SIM_PTR_SPRITES_TABLE),
    PTR(TeamGeneralInfo, shotChanceTable, SIM_PTR_SHOT_CHANCE),
    W16(TeamGeneralInfo, tactics, 2),               /* tactics, updatePlayerIndex */
    PTR(TeamGeneralInfo, controlledPlayer, SIM_PTR_SPRITE),
    PTR(TeamGeneralInfo, passToPlayerPtr, SIM_PTR_SPRITE),
    W16(TeamGeneralInfo, playerHasBall, 4),         /* playerHasBall .. direction */
    BYT(TeamGeneralInfo, quickFire, 4),             /* quickFire .. fireThisFrame */
    W16(TeamGeneralInfo, headerOrTackle, 4),        /* headerOrTackle .. shooting */
    BYT(TeamGeneralInfo, field_3C, 10),             /* field_3C .. prevPlVeryCloseToBall */
    W16(TeamGeneralInfo, field_46, 1),
    PTR(TeamGeneralInfo, lastHeadingTacklingPlayer, SIM_PTR_SPRITE),
    W16(TeamGeneralInfo, goalkeeperSavedCommentTimer, 14),  /* .. passKickTimer */
    PTR(TeamGeneralInfo, passingKickingPlayer, SIM_PTR_SPRITE),
    W16(TeamGeneralInfo, ballControlTurnTimer, 18), /* ballControlTurnTimer .. resetControls */
};

static const SimField k_teamGameFields[] = {
    W16(TeamGame, prShirtType, 11),                 /* the two kits, markedPlayer */
    BYT(TeamGame, teamName, 20 + 16 * 61),          /* teamName .. reserved3, players[16] */
};

#define GOAL(i) W16(ScorerInfo, goals[i].goalType, 1), BYT(ScorerInfo, goals[i].time, 4)
static const SimField k_scorerFields[] = {
    W16(ScorerInfo, shirtNum, 3),                   /* shirtNum, numSpritesTaken, numGoals */
    GOAL(0), GOAL(1), GOAL(2), GOAL(3), GOAL(4), GOAL(5), GOAL(6), GOAL(7), GOAL(8), GOAL(9),
};
#undef GOAL
#undef W16
#undef W32
#undef BYT
#undef PTR

#define STRUCT_LAYOUT(var, type) \
    static const SimLayout var = { sizeof(type), sizeof var##Fields / sizeof var##Fields[0], var##Fields };
STRUCT_LAYOUT(k_sprite, Sprite)
STRUCT_LAYOUT(k_hudSprite, Sprite)
STRUCT_LAYOUT(k_teamInfo, TeamGeneralInfo)
STRUCT_LAYOUT(k_teamGame, TeamGame)
STRUCT_LAYOUT(k_scorer, ScorerInfo)
#undef STRUCT_LAYOUT

#define S  0
#define V  SIM_VIEW
#define REGION(sym, fl, layout)  { #sym, (void *)&sym, (uint32_t)sizeof sym, (fl), &layout }
#define REGION_PART(sym, name, off, len, fl, layout) \
                                 { name, (void *)((uint8_t *)sym + (off)), (uint32_t)(len), (fl) | SIM_PART, &layout }

const SimStateRegion g_simStateRegions[] = {
    /* random generators */
    REGION(_seed, S, k_bytes), REGION(_randXorIndex, S, k_bytes), REGION(_randXorKey, S, k_bytes),
    REGION(_seed2, S, k_bytes), REGION(_randXorIndex2, S, k_bytes), REGION(_randXorKey2, S, k_bytes),

    /* clock */
    REGION(_currentGameTick, S, k_word16), REGION(_currentTick, S, k_word16), REGION(_lastGameTick, S, k_word16),
    REGION(_lastFrameTicks, S, k_word16), REGION(_frameCount, S, k_word16), REGION(_inGameCounter, S, k_word16),
    REGION(_gameSeconds, S, k_word16), REGION(_secondsSwitchAccumulator, S, k_word16), REGION(_gameTime, S, k_bytes),
    REGION(_playGame, S, k_word16),

    /* input edges: what turning the players' inputs into game events remembers (match_step.h) */
    REGION(g_matchInputLatch, S, k_bytes),

    /* match flow */
    REGION(_gameState, S, k_word16), REGION(_gameStatePl, S, k_word16),
    REGION(_breakState, S, k_word16), REGION(_breakCameraMode, S, k_word16),
    REGION(_extraTimeState, S, k_word16), REGION(_penaltiesState, S, k_word16),
    REGION(_fireBlocked, S, k_word16), REGION(_statsTimer, S, k_word16), REGION(_resultTimer, S, k_word16),
    REGION(_endGameCounter, S, k_word16), REGION(_goalOut, S, k_word16), REGION(_isGoalAttempt, S, k_word16),
    REGION(_stoppageEventTimer, S, k_word16), REGION(_stoppageTimerActive, S, k_word16), REGION(_stoppageTimerTotal, S, k_word16),
    REGION(_AI_maxStoppageTime, S, k_word16),
    REGION(_teamPlayingUp, S, k_word16), REGION(_teamStarting, S, k_word16), REGION(_teamSwitchCounter, S, k_word16),
    REGION(_playRefereeWhistle, S, k_word16), REGION(_endOfHalfWriteOnly, S, k_word16),
    REGION(_fadeState, S, k_word16), REGION(_fadeInitFlag, S, k_word16),
    REGION(_foulXCoordinate, S, k_word16), REGION(_foulYCoordinate, S, k_word16), REGION(_strikeDestX, S, k_word16),
    REGION(_throwInPassOrKick, S, k_word16), REGION(_waitForPlayerToGoInTimer, S, k_word16),
    REGION(_nobodysBallTimer, S, k_word16), REGION(_nobodysBallLastFrame, S, k_word16), REGION(_ballOutOfGameTimer, S, k_word16),
    REGION(_runSlower, S, k_word16), REGION(_hideBall, S, k_word16), REGION(_goalScored, S, k_word16),
    REGION(_showStats, S, k_word16), REGION(_statsNumber, S, k_bytes),
    REGION(_statsTeam1Goals, S, k_word16), REGION(_statsTeam2Goals, S, k_word16),
    REGION(_statsTeam1GoalsCopy, S, k_word16), REGION(_statsTeam2GoalsCopy, S, k_word16),
    REGION(_statsTeam1GoalsCopy2, S, k_word16), REGION(_statsTeam2GoalsCopy2, S, k_word16),

    /* camera */
    REGION(_cameraX, S, k_word32), REGION(_cameraY, S, k_word32), REGION(_cameraXVelocity, S, k_word16), REGION(_cameraYVelocity, S, k_word16),
    REGION(_cameraDirection, S, k_word16), REGION(_cameraCoordinatesValid, S, k_word16),
    REGION(_cameraLeavingSubsTimer, S, k_word16), REGION(_goalCameraMode, S, k_word16),

    /* ball and player bookkeeping */
    REGION(_ballQuadrantIndex, S, k_word16), REGION(_playerXQuadrantOffset, S, k_word16), REGION(_playerYQuadrantOffset, S, k_word16),
    REGION(_ballDefensiveX, S, k_word16), REGION(_ballDefensiveY, S, k_word16), REGION(_ballDefensiveZ, S, k_word16),
    REGION(_ballInGoalkeeperArea, S, k_word16), REGION(_ballInLowerPenaltyArea, S, k_word16), REGION(_ballInUpperPenaltyArea, S, k_word16),
    REGION(_ballNextGroundX, S, k_word16), REGION(_ballNextX, S, k_word16), REGION(_ballNextY, S, k_word16), REGION(_ballNextYGroundY, S, k_word16),
    REGION(_ballNotHighX, S, k_word16), REGION(_ballNotHighY, S, k_word16), REGION(_ballNotHighZ, S, k_word16),
    REGION(_playerHadBall, S, k_word16), REGION(_playerTurnFlags, S, k_bytes),
    REGION(_lastPlayerPlayed, S, k_ptrSprite), REGION(_lastPlayerBeforeGoalkeeper, S, k_ptrSprite), REGION(_lastKeeperPlayed, S, k_ptrSprite),
    REGION(_lastPlayerScored, S, k_ptrSprite),
    REGION(_lastTeamPlayed, S, k_ptrTeamInfo), REGION(_lastTeamPlayedBeforeBreak, S, k_ptrTeamInfo), REGION(_lastTeamScored, S, k_ptrTeamInfo),
    REGION(_teamScoredDataPtr, S, k_ptrTeamInfo), REGION(_teamScoredGamePtr, S, k_ptrTeamGame),
    REGION(_currentScorer, S, k_ptrSprite), REGION(_winningTeamPtr, S, k_ptrTeamGame), REGION(_lastTeamScoredNumber, S, k_word16),

    /* AI */
    REGION(_aiCounter, S, k_word16), REGION(_aiResumePlayTimer, S, k_word16), REGION(_aiTurnDirection, S, k_word16),
    REGION(_aiRand, S, k_word16), REGION(_aiAttackHalf, S, k_word16), REGION(_totalSkillsFactorSum, S, k_word16),

    /* teams */
    REGION(_topTeamInGame, S, k_teamGame), REGION(_bottomTeamInGame, S, k_teamGame),
    REGION(_topTeamData, S, k_teamInfo), REGION(_bottomTeamData, S, k_teamInfo),
    REGION(_team1StatsData, S, k_word16), REGION(_team2StatsData, S, k_word16),
    REGION(_pl1Tactics, S, k_word16), REGION(_pl2Tactics, S, k_word16),

    /* sprites the match moves (Sprite holds animation table pointers) */
    REGION(_ballShadowSprite, S, k_sprite), REGION(_ballSprite, S, k_sprite),
    REGION(_goal1TopSprite, S, k_sprite), REGION(_goal2BottomSprite, S, k_sprite),
    REGION(_goalie1Sprite, S, k_sprite), REGION(_goalie2Sprite, S, k_sprite),
    REGION(_team1Player2Sprite, S, k_sprite), REGION(_team1Player3Sprite, S, k_sprite), REGION(_team1Player4Sprite, S, k_sprite),
    REGION(_team1Player5Sprite, S, k_sprite), REGION(_team1Player6Sprite, S, k_sprite), REGION(_team1Player7Sprite, S, k_sprite),
    REGION(_team1Player8Sprite, S, k_sprite), REGION(_team1Player9Sprite, S, k_sprite), REGION(_team1Player10Sprite, S, k_sprite),
    REGION(_team1Player11Sprite, S, k_sprite),
    REGION(_team2Player2Sprite, S, k_sprite), REGION(_team2Player3Sprite, S, k_sprite), REGION(_team2Player4Sprite, S, k_sprite),
    REGION(_team2Player5Sprite, S, k_sprite), REGION(_team2Player6Sprite, S, k_sprite), REGION(_team2Player7Sprite, S, k_sprite),
    REGION(_team2Player8Sprite, S, k_sprite), REGION(_team2Player9Sprite, S, k_sprite), REGION(_team2Player10Sprite, S, k_sprite),
    REGION(_team2Player11Sprite, S, k_sprite),
    REGION(_team1CurPlayerNumSprite, S, k_sprite), REGION(_team2CurPlayerNumSprite, S, k_sprite),
    REGION(_playerMarkSprite, S, k_sprite), REGION(_bookedPlayerCardOrNumberSprite, S, k_sprite),
    REGION(_refereeSprite, S, k_sprite), REGION(_cornerFlagSprite, S, k_sprite), REGION(_currentPlayerNameSprite, S, k_hudSprite),

    /* referee and cards */
    REGION(_refState, S, k_word16), REGION(_refTimer, S, k_word16), REGION(_whichCard, S, k_word16), REGION(_bookedPlayer, S, k_ptrSprite),

    /* controls */
    REGION(_pl1Direction, S, k_word16), REGION(_pl2Direction, S, k_word16),
    REGION(_pl1Fire, S, k_bytes), REGION(_pl2Fire, S, k_bytes), REGION(_pl1ShortFire, S, k_bytes), REGION(_pl2ShortFire, S, k_bytes),
    REGION(_pl1Up, S, k_bytes), REGION(_pl1Down, S, k_bytes), REGION(_pl1Left, S, k_bytes), REGION(_pl1Right, S, k_bytes),
    REGION(_pl2Up, S, k_bytes), REGION(_pl2Down, S, k_bytes), REGION(_pl2Left, S, k_bytes), REGION(_pl2Right, S, k_bytes),
    REGION(_pl1UpRight, S, k_bytes), REGION(_pl1UpLeft, S, k_bytes), REGION(_pl1DownLeft, S, k_bytes), REGION(_pl1DownRight, S, k_bytes),
    REGION(_pl2UpRight, S, k_bytes), REGION(_pl2UpLeft, S, k_bytes), REGION(_pl2DownLeft, S, k_bytes), REGION(_pl2DownRight, S, k_bytes),
    REGION(_pl1WasUp, S, k_bytes), REGION(_pl1WasDown, S, k_bytes), REGION(_pl1WasLeft, S, k_bytes), REGION(_pl1WasRight, S, k_bytes),
    REGION(_pl2WasUp, S, k_bytes), REGION(_pl2WasDown, S, k_bytes), REGION(_pl2WasLeft, S, k_bytes), REGION(_pl2WasRight, S, k_bytes),
    REGION(_pl1UpPress, S, k_bytes), REGION(_pl1DownPress, S, k_bytes), REGION(_pl1LeftPress, S, k_bytes), REGION(_pl1RightPress, S, k_bytes),
    REGION(_pl2UpPress, S, k_bytes), REGION(_pl2DownPress, S, k_bytes), REGION(_pl2LeftPress, S, k_bytes), REGION(_pl2RightPress, S, k_bytes),
    REGION(_pl1FireCounter, S, k_word16), REGION(_pl2FireCounter, S, k_word16),
    REGION(_pl1LastFired, S, k_bytes), REGION(_pl2LastFired, S, k_bytes), REGION(_subsFire, S, k_bytes),

    /* substitutes bench */
    REGION(_bench1Called, S, k_word16), REGION(_bench2Called, S, k_word16),
    REGION(_subsPl1TapCount, S, k_word16), REGION(_subsPl2TapCount, S, k_word16),
    REGION(_subsPrevPl1Direction, S, k_word16), REGION(_subsPrevPl2Direction, S, k_word16),
    REGION(_pl1BenchBlockWhileHoldingDirection, S, k_word16), REGION(_pl2BenchBlockWhileHoldingDirection, S, k_word16),
    REGION(_subsDirection, S, k_word16), REGION(_subsState, S, k_word16), REGION(_goToSubsTimer, S, k_word16),
    REGION(_subsBlockDirections, S, k_word16), REGION(_subsFireTimer, S, k_word16), REGION(_subsBlockFire, S, k_word16),
    REGION(_subsPrevDirection, S, k_word16), REGION(_subsMovementDelayTimer, S, k_word16),
    REGION(_subsTrainingTopTeam, S, k_word16), REGION(_subsTeamsSwapped, S, k_word16), REGION(_benchPickTeamCounter, S, k_word16),
    REGION(_plToEnterGameIndex, S, k_word16), REGION(_plToBeSubstitutedPos, S, k_word16), REGION(_plToBeSubstitutedOrd, S, k_word16),
    REGION(_selectedFormationEntry, S, k_word16), REGION(_benchPlayerIndex, S, k_word16),
    REGION(_inSubstitutesMenu, S, k_word16), REGION(_leavingSubsMenu, S, k_word16), REGION(_substituteInProgress, S, k_word16),
    REGION(_substitutedPlDestX, S, k_word16), REGION(_substitutedPlDestY, S, k_word16),
    REGION(_pl1BenchTimeoutCounter, S, k_word16), REGION(_pl2BenchTimeoutCounter, S, k_word16),
    REGION(_benchColors, S, k_word16), REGION(_benchOff, S, k_word16),
    REGION(_benchTeam, S, k_ptrTeamInfo), REGION(_benchTeamGame, S, k_ptrTeamGame),
    REGION(_benchXLimit, S, k_word16), REGION(_benchY, S, k_word16), REGION(_opponentBenchY, S, k_word16),
    REGION(_formationMenuX, S, k_word16), REGION(_subsMenuX, S, k_word16),
    REGION(_subsMenuBackgroundColor, S, k_word16), REGION(_subsMenuEntryFrameColor, S, k_word16),
    REGION(_subsMenuHighlightColor, S, k_word16),
    REGION(_plComingX, S, k_word16), REGION(_plComingY, S, k_word16), REGION(_plComingYTmp, S, k_word16), REGION(_plLeavingX, S, k_word16),
    REGION(_plNamesLengthTableCopy, S, k_bytes), REGION(_plOrdinalsTable, S, k_bytes),
    REGION(_plPositions, S, k_ptrTactics), REGION(_teamThatSubstitutes, S, k_ptrTeamInfo),
    REGION(_plSubstitutedX, S, k_word16), REGION(_plSubstitutedY, S, k_word16), REGION(_substitutedPlSprite, S, k_ptrSprite),
    REGION(_initialSubsState, S, k_word16), REGION(_selectedPlayerInSubs, S, k_word16), REGION(_showingUserStats, S, k_word16),
    REGION(_team1NumSubs, S, k_word16), REGION(_team2NumSubs, S, k_word16),
    REGION(_team1NumAllowedInjuries, S, k_word16), REGION(_team2NumAllowedInjuries, S, k_word16),
    REGION(_team1GoalkeeperReplaced, S, k_word16), REGION(_team2GoalkeeperReplaced, S, k_word16),
    REGION(_penalty, S, k_word16), REGION(_currentKey, S, k_word16),

    /* penalty shoot-out */
    REGION(_penaltiesTimer, S, k_word16), REGION(_playingPenalties, S, k_word16), REGION(_penaltyShooterSprite, S, k_ptrSprite),
    REGION(_team1PenaltyAttempts, S, k_word16), REGION(_team1PenaltyGoals, S, k_word16), REGION(_team1PenaltyShooterIndex, S, k_word16),
    REGION(_team2PenaltyAttempts, S, k_word16), REGION(_team2PenaltyGoals, S, k_word16), REGION(_team2PenaltyShooterIndex, S, k_word16),
    REGION(_savedTeam1Goals, S, k_word16), REGION(_savedTeam2Goals, S, k_word16), REGION(_dontShowScorers, S, k_word16),
    REGION(_team1GoalsDigit1, S, k_word16), REGION(_team1GoalsDigit2, S, k_word16),
    REGION(_team2GoalsDigit1, S, k_word16), REGION(_team2GoalsDigit2, S, k_word16),

    /* instant replay */
    REGION(_instantReplayFlag, S, k_word16), REGION(_saveHighlightScene, S, k_word16), REGION(_fadeAndSaveHighlightScene, S, k_word16),
    REGION(_userRequestedReplay, S, k_word16), REGION(_replayState, S, k_word16),
    REGION(_spaceReplayTimer, S, k_word16), REGION(_gameCanceled, S, k_word16), REGION(_paused, S, k_word16),
    REGION(_playHighlightFlag, S, k_word16),

    /* crowd chants */
    REGION(_nextChantTimer, S, k_word16), REGION(_fadeInChantTimer, S, k_word16), REGION(_fadeOutChantTimer, S, k_word16),
    REGION(_crowdChantVolume, S, k_word16), REGION(_currentCrowdSample, S, k_word16), REGION(_crowdEventFlags, S, k_word16),
    REGION(_sfxQueueCount, S, k_word16), REGION(_sfxQueueWriteIdx, S, k_word16), REGION(_goalCounter, S, k_word16),
    REGION(_lastKickSampleTick, S, k_word16), REGION(_crowdChantsOn, S, k_word16),

    /* advertising boards */
    REGION(_advertSlideTimer, S, k_word16), REGION(_advertIndex, S, k_word16), REGION(_currentAnimatedAdLine, S, k_word16),
    REGION(_advertInGameSprites, S, k_sprite),
    REGION(_spinBigS, S, k_word16), REGION(_bigSIndex, S, k_word16), REGION(_big_S_Sprite, S, k_hudSprite),

    /* crowd animation */
    REGION(_animPatternsIndex, S, k_word16), REGION(_animPatternsState, S, k_word16),
    REGION(_animPatternsRandomTable, S, k_bytes), REGION(_animatedPatterns, S, k_word16),
    REGION(_otherTeamScored, S, k_word16), REGION(_patternStatic, S, k_word16),
    REGION(_patternsGoalCounter, S, k_word16), REGION(_showFansCounter, S, k_word16),
    REGION_PART(_pitchDatBuffer, "_pitchDatBuffer:top_fans",
                FAN_ROWS_TOP_SLOT * 4, FAN_ROWS_SLOTS * 4, S, k_tileSlot),
    REGION_PART(_pitchDatBuffer, "_pitchDatBuffer:bottom_fans",
                FAN_ROWS_BOTTOM_SLOT * 4, FAN_ROWS_SLOTS * 4, S, k_tileSlot),

    /* The draw list. BuildSpriteBandAndGeneralRenderStacks sets each listed sprite's
     * onScreen, which the match reads (player auto-select, referee, camera, animation
     * gate) -- so which sprites the list holds is simulation state, and the list is not
     * rebuilt every frame. Its order is history too: the depth sort starts from last
     * frame's order, and the highlight recorder writes sprites in that order. A restore
     * of the VIEW tier alone found it: the list stayed stale, an advertising board's
     * onScreen came out different, and the match diverged frames later. */
    REGION(_sortedSprites, S, k_ptrSprite), REGION(_numSpritesToRender, S, k_word16),

    /* Built with the draw list and read by the animation gate one frame later. Empty
     * unless the render is wider than the simulation view -- and always empty in the
     * replay rig, which pins animation to the simulation view, so the corpus cannot
     * exercise it; it is here on the code, like the penalty and card state. */
    REGION(g_spriteAnimVisible, S, k_ptrSprite), REGION(g_spriteAnimVisibleCount, S, k_word16),

    /* VIEW: highlights -- the replay ring and where the next frame goes in it */
    REGION(_goalBase, V, k_bytes), REGION(_currentHilPtr, V, k_ptrGoalRing), REGION(_currentGoalPtr, V, k_ptrGoalRing),
    REGION(_goalBasePtr, V, k_ptrGoalRing), REGION(_nextGoalPtr, V, k_ptrGoalRing),
    REGION(_numScenesInHilFile, V, k_word16), REGION(_hilSceneNumber, V, k_word16),

    /* result, scorers, clock. In the VIEW tier at first; a restore of the SIM tier alone
     * showed they are not: every one of these sprites is in _allSpritesArray, so its
     * `visible` and `y` decide the draw list's contents and order, and the scorer data
     * decides which scorer sprites are visible. SIM may not depend on VIEW. */
    REGION(_team1Scorers, S, k_scorer), REGION(_team2Scorers, S, k_scorer),
    REGION(_team1TotalGoals, S, k_word16), REGION(_team2TotalGoals, S, k_word16),
    REGION(_team1ScorerSprite, S, k_hudSprite), REGION(_team2ScorerSprite, S, k_hudSprite),
    REGION(_team1ScorerSpritesWidths, S, k_word16), REGION(_team2ScorerSpritesWidths, S, k_word16),
    REGION(_team1GoalsDigit1Sprite, S, k_hudSprite), REGION(_team1GoalsDigit2Sprite, S, k_hudSprite),
    REGION(_team2GoalsDigit1Sprite, S, k_hudSprite), REGION(_team2GoalsDigit2Sprite, S, k_hudSprite),
    REGION(_team1TotalGoalsDigit1Sprite, S, k_hudSprite), REGION(_team1TotalGoalsDigit2Sprite, S, k_hudSprite),
    REGION(_team2TotalGoalsDigit1Sprite, S, k_hudSprite), REGION(_team2TotalGoalsDigit2Sprite, S, k_hudSprite),
    REGION(_team1NameSprite, S, k_hudSprite), REGION(_team2NameSprite, S, k_hudSprite), REGION(_dashSprite, S, k_hudSprite),
    REGION(_gridUpperRowLeftSprite, S, k_hudSprite), REGION(_gridUpperRowRightSprite, S, k_hudSprite),
    REGION(_gridUpperRowExtraSprite, S, k_hudSprite),
    REGION(_gridMiddleRowLeftSprite, S, k_hudSprite), REGION(_gridMiddleRowRightSprite, S, k_hudSprite),
    REGION(_gridMiddleRowExtraSprite, S, k_hudSprite),
    REGION(_gridBottomRowLeftSprite, S, k_hudSprite), REGION(_gridBottomRowRightSprite, S, k_hudSprite),
    REGION(_gridBottomRowExtraSprite, S, k_hudSprite),
    REGION(_resultAfterTheGameSprite, S, k_hudSprite), REGION(_resultOnHalftimeSprite, S, k_hudSprite),
    REGION(_currentTimeSprite, S, k_hudSprite), REGION(_cornerFlagSprite2, S, k_sprite),
    REGION(_copyTimeSprite, S, k_word16),
    REGION(_overlayFlagBackup, S, k_word16), REGION(_currentPlayerIndicatorPosition, S, k_word16),
};

#undef S
#undef V
#undef REGION
#undef REGION_PART

const int32_t g_simStateRegionCount = (int32_t)(sizeof g_simStateRegions / sizeof g_simStateRegions[0]);

#define MAX_REGIONS 512
static uint8_t s_disabled[MAX_REGIONS];

_Static_assert(sizeof g_simStateRegions / sizeof g_simStateRegions[0] <= MAX_REGIONS,
               "raise MAX_REGIONS");

int SimState_RegionActive(int32_t i, uint32_t tiers)
{
    return !s_disabled[i] && (SIM_REGION_TIER(&g_simStateRegions[i]) & tiers) != 0;
}

uint32_t SimState_Size(uint32_t tiers)
{
    uint32_t total = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++)
        if (SimState_RegionActive(i, tiers))
            total += g_simStateRegions[i].size;
    return total;
}

void SimState_Save(uint8_t *dst, uint32_t tiers)
{
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        if (!SimState_RegionActive(i, tiers)) continue;
        memcpy(dst, g_simStateRegions[i].addr, g_simStateRegions[i].size);
        dst += g_simStateRegions[i].size;
    }
}

void SimState_Load(const uint8_t *src, uint32_t tiers)
{
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        if (!SimState_RegionActive(i, tiers)) continue;
        memcpy(g_simStateRegions[i].addr, src, g_simStateRegions[i].size);
        src += g_simStateRegions[i].size;
    }
#ifdef SWOS_SDL_BUILD
    /* The SDL renderer keeps its own full-width pointer for every pitch tile, so the fan
     * slots just restored have to be mirrored into it. The m68k renderer draws from
     * _pitchDatBuffer itself and has no second copy to bring back in line. */
    if (tiers & SIM_TIER_SIM) {
        AnimatedPatterns_SyncTileMirror(FAN_ROWS_TOP_SLOT, FAN_ROWS_SLOTS);
        AnimatedPatterns_SyncTileMirror(FAN_ROWS_BOTTOM_SLOT, FAN_ROWS_SLOTS);
    }
#endif
}

/* Put back the LOCAL fields (this display's, not the match's) from a native state `src`
 * that SimState_Save wrote, leaving everything else as it is. For a peer that has just
 * loaded ANOTHER peer's state: the match is theirs, the HUD's placement is ours. */
void SimState_RestoreLocal(const uint8_t *src, uint32_t tiers)
{
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const SimLayout *l = r->layout;
        if (!SimState_RegionActive(i, tiers)) continue;
        if (l && l->fieldCount) {
            uint32_t elems = l->stride ? r->size / l->stride : 1;
            for (uint32_t e = 0; e < elems; e++) {
                for (uint32_t k = 0; k < l->fieldCount; k++) {
                    const SimField *f = &l->fields[k];
                    uint32_t at = e * l->stride + f->offset, w;
                    if (f->type == SIM_FIELD_LOCAL16)      w = 2u * f->count;
                    else if (f->type == SIM_FIELD_LOCAL32) w = 4u * f->count;
                    else continue;
                    memcpy((uint8_t *)r->addr + at, src + at, w);
                }
            }
        }
        src += r->size;
    }
}

/* ======================================================================
 * The portable encoding
 * ====================================================================== */

/* ---- what pointers point into ---- */
extern Sprite      *_teamSpritesTable[];
extern void        *_kGoalieSkillTables[8];
extern uint16_t     _teamPlOfs24Table[30];
extern TeamTactics *_teamTacticsTable[19];
extern uint8_t      _pitchChunkyBuffer[];

#define X(type, name) extern type name;
SIM_ANIM_TABLES(X)
#undef X
#define X(type, name, count) extern type name[count];
SIM_FRAME_ARRAYS(X)
#undef X

#define TEAM_SPRITES_MAX          128   /* _teamSpritesTable, up to its (Sprite *)-1 end */
#define TACTICS_COUNT             19
#define GOALIE_SKILL_TABLES       8
#define PITCH_TILES               2310
#define MAX_PORTABLE_STRIDE       1024

/* A pointer's identity word: its object's position in the kind's list and the element
 * it points at, plus one -- so 0 stays the null pointer and all-ones the -1 sentinel
 * (the "no animation table" every sprite but the players holds). */
#define IDENTITY_NULL             0u
#define IDENTITY_SENTINEL         0xFFFFFFFFu
#define IDENTITY_UNKNOWN          0xFFFFFFFEu   /* no identity: written, never loaded */

typedef struct {
    const void *addr;
    uint32_t    size;       /* bytes */
    uint32_t    element;    /* bytes per element: the unit of the offset in an identity */
} SimObject;

#define X(type, name) { &name, sizeof name, sizeof name },
static const SimObject k_animTables[] = { SIM_ANIM_TABLES(X) };
#undef X
#define X(type, name, count) { name, sizeof name, sizeof name[0] },
static const SimObject k_frameArrays[] = { SIM_FRAME_ARRAYS(X) };
#undef X
static const SimObject k_teamInfos[] = {
    { &_topTeamData, sizeof _topTeamData, sizeof _topTeamData },
    { &_bottomTeamData, sizeof _bottomTeamData, sizeof _bottomTeamData },
};
static const SimObject k_teamGames[] = {
    { &_topTeamInGame, sizeof _topTeamInGame, sizeof _topTeamInGame },
    { &_bottomTeamInGame, sizeof _bottomTeamInGame, sizeof _bottomTeamInGame },
};
static const SimObject k_teamStats[] = {
    { _team1StatsData, sizeof _team1StatsData, sizeof _team1StatsData },
    { _team2StatsData, sizeof _team2StatsData, sizeof _team2StatsData },
};
static const SimObject k_goalRing[] = { { _goalBase, sizeof _goalBase, 1 } };

static uint32_t team_sprites_count(void)
{
    uint32_t n = 0;
    while (n < TEAM_SPRITES_MAX && (uintptr_t)_teamSpritesTable[n] != UINTPTR_MAX)
        n++;
    return n;
}

/* The objects a pointer of `kind` can point into, in identity order. Fixed lists come
 * from the tables above; the others are read from the game's own tables, which both
 * peers set up identically before kick-off. */
static uint32_t objects_of(uint32_t kind, SimObject *buf, const SimObject **list)
{
    uint32_t n;
    *list = buf;
    switch (kind) {
    case SIM_PTR_TEAM_INFO:     *list = k_teamInfos;   return 2;
    case SIM_PTR_TEAM_GAME:     *list = k_teamGames;   return 2;
    case SIM_PTR_TEAM_STATS:    *list = k_teamStats;   return 2;
    case SIM_PTR_ANIM_TABLE:    *list = k_animTables;  return SIM_ANIM_TABLE_COUNT;
    case SIM_PTR_FRAME_INDICES: *list = k_frameArrays; return SIM_FRAME_ARRAY_COUNT;
    case SIM_PTR_GOAL_RING:     *list = k_goalRing;    return 1;
    case SIM_PTR_SPRITE:
        n = team_sprites_count();
        for (uint32_t i = 0; i < n; i++)
            buf[i] = (SimObject){ _teamSpritesTable[i], sizeof(Sprite), sizeof(Sprite) };
        return n;
    case SIM_PTR_SPRITES_TABLE:
        buf[0] = (SimObject){ _teamSpritesTable, (team_sprites_count() + 1) * sizeof(Sprite *),
                              sizeof(Sprite *) };
        return 1;
    case SIM_PTR_SHOT_CHANCE:
        buf[0] = (SimObject){ _teamPlOfs24Table, sizeof _teamPlOfs24Table, sizeof _teamPlOfs24Table };
        for (uint32_t i = 0; i < GOALIE_SKILL_TABLES; i++)
            buf[1 + i] = (SimObject){ _kGoalieSkillTables[i], sizeof _teamPlOfs24Table,
                                      sizeof _teamPlOfs24Table };
        return 1 + GOALIE_SKILL_TABLES;
    case SIM_PTR_TACTICS:
        for (uint32_t i = 0; i < TACTICS_COUNT; i++)
            buf[i] = (SimObject){ _teamTacticsTable[i], sizeof(TeamTactics), 1 };
        return TACTICS_COUNT;
    default:
        return 0;
    }
}

/* Diagnostic: the kind and value of the last pointer that found no identity. The m68k
 * build has no log, so this is how a target that cannot save its state names the pointer
 * class responsible (src/challenge.c). */
static uint32_t s_lastUnidentifiedKind, s_lastUnidentifiedPtr, s_unidentifiedSeen;
static uint32_t s_unidentifiedByKind[SIM_PTR_KINDS];

uint32_t SimState_LastUnidentifiedKind(void) { return s_lastUnidentifiedKind; }
uint32_t SimState_LastUnidentifiedPtr(void)  { return s_lastUnidentifiedPtr; }
uint32_t SimState_UnidentifiedOfKind(uint32_t kind)
{
    return kind < SIM_PTR_KINDS ? s_unidentifiedByKind[kind] : 0;
}

void SimState_ClearUnidentified(void)
{
    uint32_t i;
    s_unidentifiedSeen = 0;
    s_lastUnidentifiedKind = 0;
    s_lastUnidentifiedPtr = 0;
    for (i = 0; i < SIM_PTR_KINDS; i++)
        s_unidentifiedByKind[i] = 0;
}

uint32_t SimState_IdentityListCount(uint32_t kind)
{
    SimObject buf[TEAM_SPRITES_MAX];
    const SimObject *list;
    return objects_of(kind, buf, &list);
}

static int identify(uint32_t kind, uintptr_t p, uint32_t *word)
{
    SimObject buf[TEAM_SPRITES_MAX];
    const SimObject *list;
    uint32_t n;

    if (p == 0)           { *word = IDENTITY_NULL; return 1; }
    if (p == UINTPTR_MAX) { *word = IDENTITY_SENTINEL; return 1; }
    n = objects_of(kind, buf, &list);
    /* Inside an object first. Only the highlight ring is pointed just past its end, and
     * letting any other kind match an end would give the start of the NEXT object an
     * identity that is only right in a build where the two happen to be adjacent. */
    for (int pastEnd = 0; pastEnd < 2; pastEnd++) {
        if (pastEnd && kind != SIM_PTR_GOAL_RING)
            break;
        for (uint32_t i = 0; i < n; i++) {
            uintptr_t base = (uintptr_t)list[i].addr, off;
            if (p < base)
                continue;
            off = p - base;
            if (pastEnd ? off != list[i].size : off >= list[i].size)
                continue;
            if (off % list[i].element != 0 || off / list[i].element > 0xFFFF)
                continue;
            *word = ((i << 16) | (uint32_t)(off / list[i].element)) + 1;
            return 1;
        }
    }
    /* No identity. Keep the FIRST one of a pass (the counters are cleared by the caller),
     * and tally by kind: the m68k build has no log, so this is how it names what it could
     * not write (src/challenge.c). */
    if (!s_unidentifiedSeen) {
        s_unidentifiedSeen = 1;
        s_lastUnidentifiedKind = kind;
        s_lastUnidentifiedPtr  = (uint32_t)p;
    }
    if (kind < SIM_PTR_KINDS)
        s_unidentifiedByKind[kind]++;
    return 0;
}

static int resolve(uint32_t kind, uint32_t word, uintptr_t *p)
{
    SimObject buf[TEAM_SPRITES_MAX];
    const SimObject *list;
    uint32_t n, index, element;
    uint64_t off;

    if (word == IDENTITY_NULL)     { *p = 0; return 1; }
    if (word == IDENTITY_SENTINEL) { *p = UINTPTR_MAX; return 1; }
    if (word == IDENTITY_UNKNOWN)
        return 0;
    index = (word - 1) >> 16;
    element = (word - 1) & 0xFFFF;
    n = objects_of(kind, buf, &list);
    if (index >= n)
        return 0;
    off = (uint64_t)element * list[index].element;
    if (off > list[index].size || (off == list[index].size && kind != SIM_PTR_GOAL_RING))
        return 0;
    *p = (uintptr_t)list[index].addr + (uintptr_t)off;
    return 1;
}

/* A fan tile slot holds pitchChunkyBuffer + tile * 256 (the low 32 bits of it on a
 * 64-bit build); 0 is a slot no tile was mapped to. */
/* A pitch tile slot travels as its tile's INDEX, because the two targets hold their tiles
 * in different buffers at different depths -- the base and the per-tile shift come from
 * animated_patterns.c, which owns that for each (include/animated_patterns.h). Hardcoding
 * the SDL buffer and a 256-byte stride here gave every fan slot on the m68k build no
 * identity: 252 of them, which is what stopped the Amiga saving a position at all. */
static uint32_t tile_identity(uint32_t slot, uint32_t *unidentified)
{
    const uint32_t shift = AnimatedPatterns_TileShift();
    const uint32_t off = slot - AnimatedPatterns_TileBase();
    if (slot == 0)
        return IDENTITY_NULL;
    if ((off & ((1u << shift) - 1u)) != 0 || (off >> shift) >= PITCH_TILES) {
        (*unidentified)++;
        return IDENTITY_UNKNOWN;
    }
    return (off >> shift) + 1;
}

static int tile_slot(uint32_t word, uint32_t *slot)
{
    if (word == IDENTITY_NULL) {
        *slot = 0;
        return 1;
    }
    if (word == IDENTITY_UNKNOWN || word == IDENTITY_SENTINEL || word - 1 >= PITCH_TILES)
        return 0;
    *slot = AnimatedPatterns_TileBase() + ((word - 1) << AnimatedPatterns_TileShift());
    return 1;
}

/* ---- fields ---- */
static uint32_t host_width(const SimField *f)
{
    switch (f->type) {
    case SIM_FIELD_BYTES:     return f->count;
    case SIM_FIELD_WORD16:    return 2u * f->count;
    case SIM_FIELD_WORD32:    return 4u * f->count;
    case SIM_FIELD_POINTER:   return sizeof(void *);
    case SIM_FIELD_TILE_SLOT: return 4;
    case SIM_FIELD_LOCAL16:   return 2u * f->count;
    case SIM_FIELD_LOCAL32:   return 4u * f->count;
    default:                  return 0;
    }
}

static uint32_t portable_width(const SimField *f)
{
    return f->type == SIM_FIELD_POINTER ? 4u : host_width(f);
}

static const char *field_type_name(uint8_t t)
{
    switch (t) {
    case SIM_FIELD_BYTES:     return "bytes";
    case SIM_FIELD_WORD16:    return "word16";
    case SIM_FIELD_WORD32:    return "word32";
    case SIM_FIELD_POINTER:   return "pointer";
    case SIM_FIELD_TILE_SLOT: return "tile";
    case SIM_FIELD_LOCAL16:   return "local16";
    case SIM_FIELD_LOCAL32:   return "local32";
    default:                  return "?";
    }
}

static uint32_t portable_stride(const SimLayout *l)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < l->fieldCount; k++)
        n += portable_width(&l->fields[k]);
    return n;
}

#ifdef SWOS_SDL_BUILD
/* Which FIELD a differing portable byte belongs to. A diff that says "region _ballSprite,
 * byte 42" leaves the reader to redo by hand what this file already knows -- walking the
 * layout to turn that into an element and a field. The native offset is the useful half:
 * it is what game_types.h's struct says, so it names the member. */
int SimState_DescribePortableByte(int32_t index, uint32_t offset, char *out, uint32_t outLen)
{
    const SimStateRegion *r;
    const SimLayout *l;
    uint32_t pstride, elem, within, at = 0, elems, i;

    if (!out || outLen == 0)
        return 0;
    out[0] = 0;
    if (index < 0 || index >= g_simStateRegionCount)
        return 0;
    r = &g_simStateRegions[index];
    l = r->layout;
    if (!l || l->fieldCount == 0 || (pstride = portable_stride(l)) == 0) {
        snprintf(out, outLen, "byte %u", offset);
        return 0;
    }
    elems = l->stride ? r->size / l->stride : 1;
    elem = offset / pstride;
    within = offset % pstride;
    for (i = 0; i < l->fieldCount; i++) {
        const SimField *f = &l->fields[i];
        uint32_t w = portable_width(f);
        if (within < at + w) {
            if (elems > 1)
                snprintf(out, outLen, "[%u of %u] +%u %s%s", elem, elems, f->offset,
                         field_type_name(f->type),
                         f->count > 1 ? " (array)" : "");
            else
                snprintf(out, outLen, "+%u %s%s", f->offset, field_type_name(f->type),
                         f->count > 1 ? " (array)" : "");
            return 1;
        }
        at += w;
    }
    snprintf(out, outLen, "byte %u", offset);
    return 0;
}
#endif /* SWOS_SDL_BUILD */

#ifdef SWOS_SDL_BUILD
/* Mark the bytes of region `index`'s portable encoding that are LOCAL fields -- this
 * display's, not the match's, and zero in the hash. `mask` is the region's portable size;
 * 1 = local. So a diff of two peers' states can skip what their checksum skips. */
void SimState_PortableLocalMask(int32_t index, uint8_t *mask)
{
    const SimStateRegion *r;
    const SimLayout *l;
    uint32_t pstride, elems, e, i, at;

    if (index < 0 || index >= g_simStateRegionCount)
        return;
    r = &g_simStateRegions[index];
    l = r->layout;
    memset(mask, 0, SimState_RegionPortableSize(index));
    if (!l || l->fieldCount == 0 || (pstride = portable_stride(l)) == 0)
        return;
    elems = l->stride ? r->size / l->stride : 1;
    for (e = 0; e < elems; e++) {
        at = e * pstride;
        for (i = 0; i < l->fieldCount; i++) {
            const SimField *f = &l->fields[i];
            uint32_t w = portable_width(f);
            if (f->type == SIM_FIELD_LOCAL16 || f->type == SIM_FIELD_LOCAL32)
                memset(mask + at, 1, w);
            at += w;
        }
    }
}
#endif

/* One element, this build's bytes at `src` to portable bytes at `dst`. `hashing`: for the
 * checksum peers compare, where a LOCAL field (this display's, not the match's) counts as
 * zero; a saved or sent state carries it. */
static uint32_t encode_element(const SimLayout *l, const uint8_t *src, uint8_t *dst,
                               uint32_t *unidentified, int hashing)
{
    uint32_t out = 0;
    for (uint32_t k = 0; k < l->fieldCount; k++) {
        const SimField *f = &l->fields[k];
        const uint8_t *in = src + f->offset;
        switch (f->type) {
        case SIM_FIELD_BYTES:
            memcpy(dst + out, in, f->count);
            out += f->count;
            break;
        case SIM_FIELD_WORD16:
            for (uint32_t c = 0; c < f->count; c++, out += 2) {
                uint16_t v;
                memcpy(&v, in + 2 * c, 2);
                BE_WRITE_U16(dst + out, v);
            }
            break;
        case SIM_FIELD_WORD32:
            for (uint32_t c = 0; c < f->count; c++, out += 4) {
                uint32_t v;
                memcpy(&v, in + 4 * c, 4);
                BE_WRITE_U32(dst + out, v);
            }
            break;
        case SIM_FIELD_POINTER: {
            uintptr_t v;
            uint32_t word;
            memcpy(&v, in, sizeof v);
            if (!identify(f->kind, v, &word)) {
                word = IDENTITY_UNKNOWN;
                (*unidentified)++;
            }
            BE_WRITE_U32(dst + out, word);
            out += 4;
            break;
        }
        case SIM_FIELD_TILE_SLOT: {
            uint32_t v;
            memcpy(&v, in, 4);
            BE_WRITE_U32(dst + out, tile_identity(v, unidentified));
            out += 4;
            break;
        }
        case SIM_FIELD_LOCAL16:
            for (uint32_t c = 0; c < f->count; c++, out += 2) {
                uint16_t v = 0;
                if (!hashing)
                    memcpy(&v, in + 2 * c, 2);
                BE_WRITE_U16(dst + out, v);
            }
            break;
        case SIM_FIELD_LOCAL32:
            for (uint32_t c = 0; c < f->count; c++, out += 4) {
                uint32_t v = 0;
                if (!hashing)
                    memcpy(&v, in + 4 * c, 4);
                BE_WRITE_U32(dst + out, v);
            }
            break;
        }
    }
    return out;
}

/* One element back. Returns 0 if an identity does not resolve in this build. */
static int decode_element(const SimLayout *l, const uint8_t *src, uint8_t *dst)
{
    uint32_t in = 0;
    for (uint32_t k = 0; k < l->fieldCount; k++) {
        const SimField *f = &l->fields[k];
        uint8_t *out = dst + f->offset;
        switch (f->type) {
        case SIM_FIELD_BYTES:
            memcpy(out, src + in, f->count);
            in += f->count;
            break;
        case SIM_FIELD_WORD16:
            for (uint32_t c = 0; c < f->count; c++, in += 2) {
                uint16_t v = BE_READ_U16(src + in);
                memcpy(out + 2 * c, &v, 2);
            }
            break;
        case SIM_FIELD_WORD32:
            for (uint32_t c = 0; c < f->count; c++, in += 4) {
                uint32_t v = BE_READ_U32(src + in);
                memcpy(out + 4 * c, &v, 4);
            }
            break;
        case SIM_FIELD_POINTER: {
            uintptr_t v;
            if (!resolve(f->kind, BE_READ_U32(src + in), &v))
                return 0;
            memcpy(out, &v, sizeof v);
            in += 4;
            break;
        }
        case SIM_FIELD_TILE_SLOT: {
            uint32_t v;
            if (!tile_slot(BE_READ_U32(src + in), &v))
                return 0;
            memcpy(out, &v, 4);
            in += 4;
            break;
        }
        case SIM_FIELD_LOCAL16:
            for (uint32_t c = 0; c < f->count; c++, in += 2) {
                uint16_t v = BE_READ_U16(src + in);
                memcpy(out + 2 * c, &v, 2);
            }
            break;
        case SIM_FIELD_LOCAL32:
            for (uint32_t c = 0; c < f->count; c++, in += 4) {
                uint32_t v = BE_READ_U32(src + in);
                memcpy(out + 4 * c, &v, 4);
            }
            break;
        }
    }
    return 1;
}

const char *SimState_LayoutError(int32_t index)
{
    const SimStateRegion *r = &g_simStateRegions[index];
    const SimLayout *l = r->layout;
    uint32_t at = 0;

    if (!l || l->stride == 0 || r->size % l->stride != 0)
        return "the region is not a whole number of its layout's elements";
    for (uint32_t k = 0; k < l->fieldCount; k++) {
        const SimField *f = &l->fields[k];
        if (f->offset != at)
            return "the layout's fields do not tile the element (a gap or an overlap)";
        if ((f->type == SIM_FIELD_POINTER || f->type == SIM_FIELD_TILE_SLOT) && f->count != 1)
            return "a pointer or tile field with a count other than 1";
        if (f->type == SIM_FIELD_POINTER && (f->kind == 0 || f->kind >= SIM_PTR_KINDS))
            return "a pointer field with no kind";
        at += host_width(f);
    }
    if (at != l->stride)
        return "the layout's fields do not cover the element";
    if (portable_stride(l) > MAX_PORTABLE_STRIDE)
        return "an element too large to encode";
    return NULL;
}

uint32_t SimState_RegionPortableSize(int32_t index)
{
    const SimStateRegion *r = &g_simStateRegions[index];
    return r->size / r->layout->stride * portable_stride(r->layout);
}

uint32_t SimState_PortableSize(uint32_t tiers)
{
    uint32_t total = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++)
        if (SimState_RegionActive(i, tiers))
            total += SimState_RegionPortableSize(i);
    return total;
}

int SimState_SavePortable(uint8_t *dst, uint32_t tiers)
{
    uint32_t unidentified = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const uint8_t *src = (const uint8_t *)r->addr;
        if (!SimState_RegionActive(i, tiers)) continue;
        if (r->layout == &k_bytes) {
            memcpy(dst, src, r->size);
            dst += r->size;
            continue;
        }
        for (uint32_t off = 0; off < r->size; off += r->layout->stride)
            dst += encode_element(r->layout, src + off, dst, &unidentified, 0);
    }
    return (int)unidentified;
}

int SimState_LoadPortable(const uint8_t *src, uint32_t tiers)
{
    /* Decode into a scratch copy first: a buffer that does not resolve must not leave
     * the match half loaded. */
    uint8_t *raw = (uint8_t *)swos_io_alloc(SimState_Size(tiers) + 1);
    uint8_t *out = raw;
    if (!raw)
        return -1;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const uint32_t stride = portable_stride(r->layout);
        if (!SimState_RegionActive(i, tiers)) continue;
        if (r->layout == &k_bytes) {
            memcpy(out, src, r->size);
            src += r->size;
            out += r->size;
            continue;
        }
        for (uint32_t off = 0; off < r->size; off += r->layout->stride) {
            if (!decode_element(r->layout, src, out)) {
                swos_io_free(raw);
                return -1;
            }
            src += stride;
            out += r->layout->stride;
        }
    }
    SimState_Load(raw, tiers);
    swos_io_free(raw);
    return 0;
}

static uint32_t s_hashUnidentified;

uint64_t SimState_Hash(uint32_t tiers)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint8_t el[MAX_PORTABLE_STRIDE];
    uint32_t unidentified = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const uint8_t *p = (const uint8_t *)r->addr;
        if (!SimState_RegionActive(i, tiers)) continue;
        if (r->layout == &k_bytes) {
            for (uint32_t n = 0; n < r->size; n++)
                h = (h ^ p[n]) * 0x100000001b3ull;
            continue;
        }
        for (uint32_t off = 0; off < r->size; off += r->layout->stride) {
            uint32_t len = encode_element(r->layout, p + off, el, &unidentified, 1);
            for (uint32_t n = 0; n < len; n++)
                h = (h ^ el[n]) * 0x100000001b3ull;
        }
    }
    s_hashUnidentified = unidentified;
    return h;
}

uint32_t SimState_HashUnidentified(void)
{
    return s_hashUnidentified;
}

int SimState_PointerIdentity(const void *p, uint32_t *kind, uint32_t *identity)
{
    for (uint32_t k = SIM_PTR_SPRITE; k < SIM_PTR_KINDS; k++)
        if (identify(k, (uintptr_t)p, identity)) {
            *kind = k;
            return 1;
        }
    return 0;
}

/* ---- the fingerprint ---- */
static uint64_t mix_bytes(uint64_t h, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

static uint64_t mix_u32(uint64_t h, uint32_t v)
{
    uint8_t be[4];
    BE_WRITE_U32(be, v);
    return mix_bytes(h, be, 4);
}

#define X(type, name) #name,
static const char *const k_animTableNames[] = { SIM_ANIM_TABLES(X) };
#undef X
#define X(type, name, count) #name,
static const char *const k_frameArrayNames[] = { SIM_FRAME_ARRAYS(X) };
#undef X
#define X(type, name, count) count,
static const uint32_t k_frameArrayCounts[] = { SIM_FRAME_ARRAYS(X) };
#undef X

/* The m68k build has no <string.h> beyond what the toolchain inlines, and these two walk
 * region NAMES -- short literals. Same values as strlen/strcmp would give, which matters
 * for the first one: the layout hash must come out identical on both targets or a save
 * state written on one would be refused by the other. */
static uint32_t name_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

uint64_t SimState_LayoutHash(void)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const SimLayout *l = r->layout;
        h = mix_bytes(h, r->name, name_len(r->name) + 1);
        h = mix_u32(h, SimState_RegionPortableSize(i));
        h = mix_u32(h, r->flags);
        h = mix_u32(h, s_disabled[i]);
        for (uint32_t k = 0; k < l->fieldCount; k++) {
            h = mix_u32(h, l->fields[k].type);
            h = mix_u32(h, l->fields[k].count);
            h = mix_u32(h, l->fields[k].kind);
        }
    }
    for (uint32_t i = 0; i < SIM_ANIM_TABLE_COUNT; i++)
        h = mix_bytes(h, k_animTableNames[i], name_len(k_animTableNames[i]) + 1);
    for (uint32_t i = 0; i < SIM_FRAME_ARRAY_COUNT; i++) {
        h = mix_bytes(h, k_frameArrayNames[i], name_len(k_frameArrayNames[i]) + 1);
        h = mix_u32(h, k_frameArrayCounts[i]);
    }
    h = mix_u32(h, team_sprites_count());
    h = mix_u32(h, TACTICS_COUNT);
    h = mix_u32(h, GOALIE_SKILL_TABLES);
    h = mix_u32(h, PITCH_TILES);
    return h;
}

int SimState_Disable(const char *name)
{
    int found = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++)
        if (name_eq(g_simStateRegions[i].name, name)) {
            s_disabled[i] = 1;
            found = 1;
        }
    return found;
}
