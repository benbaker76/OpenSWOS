/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "match_setup.h"
#include "dos_sound.h"
#include "jag_graphics.h"
#include "dos_gameplay.h"   /* ApplyGameStyle — the pitch factors below latch its tables */
#include "game_types.h"
#include "display.h"        /* single source of `#define AGA` — without it the #ifdef AGA
                             * pitch-palette block below is silently compiled out (ran the
                             * OCS 16-colour path on AGA). See docs/AGA.md §9a. */
#include "dos_mode.h"      /* _gDosGameplay: GameOver drops the training-camera test */
#include "deterministic.h"
#include "shooting_practice.h"

/* ---- External globals (in SWOS2.s code/BSS sections) ---- */
extern Sprite             _ballSprite;
extern TeamGeneralInfo    _topTeamData;
extern TeamGeneralInfo    _bottomTeamData;
extern Sprite             _refereeSprite;
extern Sprite **_team1SpritesTable;
extern Sprite **_team2SpritesTable;
extern TeamGame           _topTeamInGame;
extern TeamGame           _bottomTeamInGame;
extern TeamGame           _topTeamSaved;
extern TeamGame           _bottomTeamSaved;
extern TeamStatisticsData _team1StatsData;
extern TeamStatisticsData _team2StatsData;

/* Match state */
extern uint16_t _gameState;
extern uint16_t _gameStatePl;
extern uint16_t _breakCameraMode;
extern uint16_t _gameNotInProgressCounterWriteOnly;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern uint16_t _stoppageTimerTotal;
extern uint16_t _stoppageTimerActive;
extern uint16_t _stoppageEventTimer;
extern int16_t _gsStoppageShowerPan;   /* stoppageEventTimer reloads — dos_gameplay.c */
extern int16_t _gsStoppageHalfEnded;
extern int16_t _gsStoppageHalftime;
extern int16_t _gsStoppageGameOver;
extern uint16_t _halfNumber;
extern uint16_t _teamStarting;
extern uint16_t _teamPlayingUp;
extern uint16_t _cameraDirection;
extern uint8_t  _playerTurnFlags;
extern uint16_t _lastCameraDirection;
extern uint8_t  _lastPlayerTurnFlags;
extern uint16_t _cameraXVelocity;
extern uint16_t _cameraYVelocity;
extern uint16_t _cameraMoveToShowerLatch;
extern uint16_t _hideBall;
extern volatile uint16_t _playGame;
extern uint16_t _trainingGame;
extern uint16_t _goalCounter;
extern uint16_t _endOfHalfWriteOnly;
extern uint16_t _sfxQueueWriteIdx;
extern uint16_t _showFansCounter;
extern uint16_t _resultTimer;
extern uint16_t _timeVar;
extern uint16_t _refState;
extern uint16_t _foulXCoordinate;
extern uint16_t _foulYCoordinate;
extern uint16_t _playRefereeWhistle;

/* InitGameRestoreTeams globals */
extern uint16_t _gameLengthInGame;
extern uint16_t _playerCardChance;
extern uint16_t _overlayActiveFlag;
extern uint16_t _gameRandValue;
extern uint16_t _paletteIndex;
extern volatile uint16_t _playHighlightFlag;
extern uint8_t  _gamePalette[];
extern void *_paletteTable[];
extern uint16_t _pitchBallSpeedFactor;
extern uint16_t _ballSpeedBounceFactor;
extern uint16_t _ballBounceFactor;
extern uint16_t _pitchNumberAndType;
extern uint16_t _playingPenalties;
extern uint16_t _dontShowScorers;
extern uint16_t _statsTimer;
extern uint16_t _showStats;
extern uint16_t _showingUserStats;
extern uint16_t _subsTeamsSwapped;
extern uint16_t _inSubstitutesMenu;
extern uint16_t _cameraLeavingSubsTimer;
extern uint16_t _benchOff;
extern uint16_t _leavingSubsMenu;
extern uint16_t _waitForPlayerToGoInTimer;
extern uint16_t _substituteInProgress;
extern uint16_t _statsTeam1Goals;
extern uint16_t _team1GoalsDigit1;
extern uint16_t _team1GoalsDigit2;
extern uint16_t _statsTeam2Goals;
extern uint16_t _team2GoalsDigit1;
extern uint16_t _team2GoalsDigit2;
extern uint16_t _team1TotalGoals;
extern uint16_t _team2TotalGoals;
extern uint16_t _secondLeg;
extern uint16_t _team1GoalsFirstLeg;
extern uint16_t _team2GoalsFirstLeg;
extern uint16_t _team1NumSubs;
extern uint16_t _team2NumSubs;
extern uint16_t _team1GoalkeeperReplaced;
extern uint16_t _team2GoalkeeperReplaced;

/* StartPenalties globals */
extern uint16_t _penaltiesState;
extern uint16_t _savedTeam1Goals;
extern uint16_t _savedTeam2Goals;
extern uint16_t _team1PenaltyGoals;
extern uint16_t _team2PenaltyGoals;
extern uint16_t _team1PenaltyShooterIndex;
extern uint16_t _team2PenaltyShooterIndex;
extern uint16_t _team1PenaltyAttempts;
extern uint16_t _team2PenaltyAttempts;

/* InitAnimatedPatterns */
extern int16_t  _animatedPatterns[];
extern uint16_t _animPatternsIndex;
extern uint16_t _patternsGoalCounter;

/* Data tables (in SWOS2.s code section, PC-relative) */
extern void *_playerCardChancesPerGameLength[];
extern int16_t  _gameLenSecondsTable[];
extern const uint16_t *_pitchPalettePtrs[];
extern int16_t  _kPitchBallSpeedInfluence[];
extern int16_t  _kBallSpeedBounceFactorTable[];
extern int16_t  _kBallBounceFactorTable[];
extern uint16_t _timeDelta;
extern TeamGame *_topTeamPtr;
extern TeamGame *_bottomTeamPtr;
extern uint16_t _pl1Tactics;
extern uint16_t _pl2Tactics;
extern AnimTable_12 _refWaitingAnimTable;
extern AnimTable _playerNormalStandingAnimTable;

/* ---- External C functions (already ported) ---- */
extern void InitTeamsData(void);
extern void InitDisplaySprites(void);
extern void InitCurrentGoalHighlight(void);
extern void ResetTime(void);
extern void ResetResultSprites(void);
extern void ClearSaveFlagForGoalSprites(void);
extern void ResetGoalAttempt(void);
extern void CrowdResetMatchState(void);
extern void CopyPlayerNameLengths(void);
extern void InitializePlayerSpriteFrameIndices(void);
extern void InitAdvertisements(void);
extern void PlayPenaltySample(void);
extern void PlaySample1IfNotTraining(void);
extern void FadeOutToBlack(void);
extern void UpdatePlanePtrsAndBlitParams(void);
extern uint16_t Rand(void);

/* ---- C-to-asm bridges (for unported asm functions) ---- */
extern void SetPlayerAnimationTable(AnimTable *animTable, Sprite *sprite);
extern void InitRefereeAnimationTable(uint8_t *animTable);
extern void ApplyTeamTactics(uint8_t *playerInfos);
extern void NextPenalty(void);
extern void ReplayUpdateCameraAndSwapBuffers(int16_t x, int16_t y);

/* ================================================================
 * SetBallPosition  (SWOS2.s line 10505)
 *
 * Stop ball and set position to (ballX, ballY), z=0.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetBallPosition(int16_t ballX, int16_t ballY)
{
    _ballSprite.speed      = 0;
    _ballSprite.x.s.i      = ballX;
    _ballSprite.x.s.frac   = 0;
    _ballSprite.y.s.i      = ballY;
    _ballSprite.y.s.frac   = 0;
    _ballSprite.destX      = ballX;
    _ballSprite.destY      = ballY;
    /* `clr.w SPRITE_Z(a0)` then `clr.l SPRITE_DELTAZ(a0)` — the width
     * difference is deliberate: placing the ball zeroes only the INTEGER half
     * of z and leaves its fraction alone, while deltaZ is cleared whole.
     * Zeroing z.raw here made every set-piece placement start from a different
     * sub-pixel height than the reference (docs/TESTS.md). */
    _ballSprite.z.s.i      = 0;
    _ballSprite.deltaZ.raw = 0;
}

/* ================================================================
 * resetPlayerPassingKicking  (SWOS2.s line 22797)
 *
 * Clears team passing/kicking state fields.
 * ================================================================ */
static void resetPlayerPassingKicking(TeamGeneralInfo *team)
{
    team->ballInPlay           = 0;
    team->ballOutOfPlay        = 0;
    team->controlledPlayer     = 0;
    team->passToPlayerPtr      = 0;
    team->passingBall          = 0;
    team->passingToPlayer      = 0;
    team->playerSwitchTimer    = 0;
    team->passingKickingPlayer = 0;
}

/* ================================================================
 * ResetBothTeamsPlayerPassingKicking  (SWOS2.s line 22781)
 *
 * Saves camera direction and turn flags, resets passing/kicking
 * for both teams, clears goalkeeperPlaying on bottom team.
 * ================================================================ */
__attribute__((used, externally_visible))
void ResetBothTeamsPlayerPassingKicking(void)
{
    _lastCameraDirection = _cameraDirection;
    _lastPlayerTurnFlags = _playerTurnFlags;
    resetPlayerPassingKicking(&_topTeamData);
    resetPlayerPassingKicking(&_bottomTeamData);
    _bottomTeamData.goalkeeperPlaying = 0;
}

/* ================================================================
 * stopPlayers  (SWOS2.s line 22842)
 *
 * For each of the 11 players on a team, if the player is in
 * NORMAL/HAPPY/SAD state and not sent away, set dest = current pos.
 * ================================================================ */
static void stopPlayers(TeamGeneralInfo *team)
{
    Sprite * const *spritesTable = team->spritesTable;
    int16_t i;
    for (i = 10; i >= 0; i--) {
        Sprite *sprite = spritesTable[i];
        uint8_t state = sprite->playerState;
        int stops;

        /* WHICH states get stopped is a real difference between the releases.
         * The Amiga takes three:
         *
         *      cmpi.b  #PL_HAPPY,SPRITE_PLAYERSTATE(a2)     ; SWOS2.s:28265
         *      beq.s   set_dest_to_current_pos
         *      cmpi.b  #PL_SAD,SPRITE_PLAYERSTATE(a2)
         *      beq.s   set_dest_to_current_pos
         *      cmpi.b  #PL_NORMAL,SPRITE_PLAYERSTATE(a2)
         *      bne.s   continue_player_loop
         *
         * and the DOS build takes one:
         *
         *      cmp     [esi+Sprite.playerState], PL_NORMAL  ; swos.asm:104106
         *      jnz     short @@next
         *
         * A recompiler does not delete two compares, and both states exist in
         * the DOS build — so this is a hand edit. What it means in play: at
         * the final whistle the Amiga freezes celebrating and dejected players
         * where they stand, and DOS lets them walk on to wherever they were
         * already headed.
         *
         * Found by the replay rig. zkz-11215003 ends with most of both squads
         * in PL_HAPPY and PL_SAD, and on the frame `PlayersLeavingPitch` runs
         * the reference keeps their destinations while we reset every one of
         * them to the player's own position (docs/TESTS.md). */
        if (_gDosGameplay)
            stops = (state == PL_NORMAL);
        else
            stops = (state == PL_HAPPY || state == PL_SAD || state == PL_NORMAL);

        if (stops) {
            if ((uint16_t)sprite->sentAway == 0) {
                sprite->destX = sprite->x.s.i;
                sprite->destY = sprite->y.s.i;
            }
        }
    }
}

/* ================================================================
 * StopAllPlayers  (SWOS2.s line 22810)
 *
 * Saves camera direction and turn flags, stops both teams' players,
 * resets passing/kicking for both teams, clears goalkeeperPlaying.
 * ================================================================ */
__attribute__((used, externally_visible))
void StopAllPlayers(void)
{
    _lastCameraDirection = _cameraDirection;
    _lastPlayerTurnFlags = _playerTurnFlags;

    stopPlayers(&_topTeamData);
    resetPlayerPassingKicking(&_topTeamData);

    stopPlayers(&_bottomTeamData);
    resetPlayerPassingKicking(&_bottomTeamData);
    _bottomTeamData.goalkeeperPlaying = 0;
}

/* ================================================================
 * RemoveReferee  (SWOS2.s line 11441)
 *
 * Makes referee invisible, resets position and animation state.
 * ================================================================ */
__attribute__((used, externally_visible))
void RemoveReferee(void)
{
    _refState                    = 0;
    _refereeSprite.visible       = 0;
    _refereeSprite.x.s.i         = 276;
    _refereeSprite.y.s.i         = 439;
    _refereeSprite.destX         = _refereeSprite.x.s.i;
    _refereeSprite.destY         = _refereeSprite.y.s.i;
    _refereeSprite.z.s.i         = 0;   /* clr.w — integer half only, as above */
    _refereeSprite.speed         = 0;
    _refereeSprite.playerDownTimer = 0;
    _refereeSprite.frameIndex    = (int16_t)0xFFFF;
    _refereeSprite.cycleFramesTimer = 1;
    _refereeSprite.imageIndex    = (int16_t)0xFFFF;
    _refereeSprite.direction     = 0;
    _refereeSprite.onScreen      = 1;
    InitRefereeAnimationTable((uint8_t *)&_refWaitingAnimTable); /* TODO: update when InitRefereeAnimationTable takes AnimTable* */
}

/* ================================================================
 * InitAnimatedPatterns  (SWOS2.s line 56110)
 *
 * Clears the 8-entry animated patterns array and resets counters.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitAnimatedPatterns(void)
{
    int16_t i;
    for (i = 0; i < 8; i++) {
        _animatedPatterns[i] = (int16_t)0xFFFF;
    }
    _patternsGoalCounter = 0;
    _animPatternsIndex = 0;
}

/* ================================================================
 * InitPlayersBeforeEnteringPitch  (SWOS2.s line 23194)
 *
 * Positions all 22 players at their starting coordinates, resets
 * sprite state, and sets up animation tables.
 * ================================================================ */
static const int16_t kTeamsStartingCoordinates[44] = {
    0x012C, 0x0045,  /* element[0]  */
    0x0118, 0x002E,  /* element[1]  */
    0x0104, 0x0022,  /* element[2]  */
    0x00F0, 0x0018,  /* element[3]  */
    0x00DC, 0x0010,  /* element[4]  */
    0x00C8, 0x0009,  /* element[5]  */
    0x00B4, 0x0003,  /* element[6]  */
    0x00A0, -2,      /* element[7]  */
    0x008C, -8,      /* element[8]  */
    0x0078, -9,      /* element[9]  */
    0x0064, -11,     /* element[10] */
    0x012C, -65,     /* element[11] */
    0x0118, -42,     /* element[12] */
    0x0104, -30,     /* element[13] */
    0x00F0, -20,     /* element[14] */
    0x00DC, -12,     /* element[15] */
    0x00C8, -5,      /* element[16] */
    0x00B4, 1,       /* element[17] */
    0x00A0, 6,       /* element[18] */
    0x008C, 12,      /* element[19] */
    0x0078, 13,      /* element[20] */
    0x0064, 15       /* element[21] */
};

__attribute__((used, externally_visible))
void InitPlayersBeforeEnteringPitch(void)
{
    RemoveReferee();
    /* nullsub_21 — no-op */

    Sprite **spritesTableA;
    if (_teamPlayingUp == 1) {
        spritesTableA = _team1SpritesTable;
    } else {
        spritesTableA = _team2SpritesTable;
    }

    const int16_t *coords = kTeamsStartingCoordinates;
    int16_t pass;
    for (pass = 1; pass >= 0; pass--) {
        int16_t idx;
        for (idx = 10; idx >= 0; idx--) {
            Sprite *sprite = *spritesTableA++;
            sprite->x.s.i  = *coords++ + 591;
            sprite->y.s.i  = *coords++ + 449;

            /* Add random jitter 0..7 to X */
            uint8_t r = Rand();
            sprite->x.s.i += (int16_t)(r & 7);

            sprite->destX            = sprite->x.s.i;
            sprite->destY            = sprite->y.s.i;
            sprite->z.s.i            = 0;  /* ASM: clr.w SPRITE_Z — only clears integer part */
            sprite->speed            = 0;
            sprite->playerState      = 0;
            sprite->playerDownTimer  = 0;
            sprite->frameIndex       = (int16_t)0xFFFF;
            sprite->cycleFramesTimer = 1;
            sprite->imageIndex       = (int16_t)0xFFFF;
            sprite->direction        = 0;
            sprite->onScreen         = 1;

            if (_gameState == ST_STARTING_GAME) {
                sprite->sentAway    = 0;
                sprite->cards       = 0;
                sprite->injuryLevel = 0;
            }

            SetPlayerAnimationTable(
                &_playerNormalStandingAnimTable,
                sprite
            );
        }

        /* Second pass: other team (coords continues from where it left off) */
        if (_teamPlayingUp == 2) {
            spritesTableA = _team1SpritesTable;
        } else {
            spritesTableA = _team2SpritesTable;
        }
    }

    /* SHOOTING PRACTICE: both teams just had sentAway reset to 0 above (the
     * ST_STARTING_GAME branch), so this is the one place that needs to
     * re-apply the strip -- and it runs again every time teams walk back
     * out (second half), which is exactly when it needs to. */
    ShootingPractice_StripCpuOutfieldPlayers();
}

/* ================================================================
 * Common helper: set "break" state with standard field assignments
 * ================================================================ */
static void setBreakState(uint16_t newGameState, uint16_t newGameStatePl,
                          uint16_t stoppageTimer)
{
    _hideBall = 0;
    _stoppageEventTimer = stoppageTimer;
    _gameState = newGameState;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = newGameStatePl;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
}

static void clearCameraVelocity(void)
{
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}

/* ================================================================
 * StartingMatch  (SWOS2.s line 22465)
 * ================================================================ */
__attribute__((used, externally_visible))
void StartingMatch(void)
{
    _halfNumber = 1;
    SetBallPosition(1672, 449);
    _hideBall = 0;
    InitTeamsData();
    _stoppageEventTimer = 100;
    _gameState = ST_STARTING_GAME;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = 101;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
    _cameraMoveToShowerLatch = 1;
    InitPlayersBeforeEnteringPitch();
}

/* ================================================================
 * SetCameraMovingToShowerState  (SWOS2.s line 22490)
 * ================================================================ */
__attribute__((used, externally_visible))
void SetCameraMovingToShowerState(void)
{
    _halfNumber = 2;
    /* Invert teamPlayingUp: neg + add 3 (1->2, 2->1) */
    _teamPlayingUp = (uint16_t)(3 - _teamPlayingUp);
    _teamStarting = (uint16_t)(3 - _teamStarting);

    SetBallPosition(1672, 449);
    _hideBall = 0;
    InitTeamsData();
    _stoppageEventTimer = (uint16_t)_gsStoppageShowerPan;   /* Amiga 100 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_CAMERA_GOING_TO_SHOWERS;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = 101;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
    _cameraMoveToShowerLatch = 1;

    if (_playGame != 0) {
        PlayPenaltySample();
        _sfxQueueWriteIdx = 350;
    }

    InitPlayersBeforeEnteringPitch();
}

/* ================================================================
 * EndFirstHalf  (SWOS2.s line 22523)
 * ================================================================ */
__attribute__((used, externally_visible))
void EndFirstHalf(void)
{
    setBreakState(ST_FIRST_HALF_ENDED, ST_STOPPED, 100);
    ResetBothTeamsPlayerPassingKicking();
    clearCameraVelocity();
    if (_playGame != 0 && _goalCounter == 0) {
        _endOfHalfWriteOnly = 25;
    }
}

/* ================================================================
 * FirstHalfJustEnded  (SWOS2.s line 22547)
 * ================================================================ */
__attribute__((used, externally_visible))
void FirstHalfJustEnded(void)
{
    _hideBall = 0;
    _stoppageEventTimer = (uint16_t)_gsStoppageHalfEnded;   /* Amiga 250 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_GOING_TO_HALFTIME;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * EndOfGame  (SWOS2.s line 22565)
 * ================================================================ */
__attribute__((used, externally_visible))
void EndOfGame(void)
{
    setBreakState(ST_GAME_ENDED, ST_STOPPED, 150);
    ResetBothTeamsPlayerPassingKicking();
    clearCameraVelocity();
    if (_playGame != 0 && _goalCounter == 0) {
        _endOfHalfWriteOnly = 25;
    }
}

/* ================================================================
 * PlayersLeavingPitch  (SWOS2.s line 22589)
 * ================================================================ */
__attribute__((used, externally_visible))
void PlayersLeavingPitch(void)
{
    _hideBall = 0;
    _stoppageEventTimer = (uint16_t)_gsStoppageHalfEnded;   /* Amiga 250 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_PLAYERS_GOING_TO_SHOWER;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = 0x65;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * GoToHalftime  (SWOS2.s line 22607)
 * ================================================================ */
__attribute__((used, externally_visible))
void GoToHalftime(void)
{
    SetBallPosition(1672, 449);
    _resultTimer = 30000;
    _timeVar = 32000;
    _stoppageEventTimer = (uint16_t)_gsStoppageHalftime;   /* Amiga 700 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_RESULT_ON_HALFTIME;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * GameOver  (SWOS2.s line 22629)
 * ================================================================ */
__attribute__((used, externally_visible))
void GameOver(void)
{
    FadeOutToBlack();

    /* 168 at the port's own 336-wide view, 176 at the DOS release's 320 —
     * `move.w #168,d1` (SWOS2.s:28049) against `mov word ptr D1, 176`
     * (swos.asm:103859). Y is a literal in both. */
    int16_t viewX = SIM_CAMERA_CENTRE_X;
    /* The camera Y is 80 for a training game and 16 for a match:
     *
     *      move.w  #80,d2
     *      tst.w   (trainingGame).l
     *      bne.s   set_for_training_or_regular_game
     *      move.w  #16,d2
     *
     * (SWOS2.s:28050). The DOS build has no such test — `mov word ptr D2, 80`
     * and straight on (swos.asm:103860) — so on DOS the final whistle parks
     * the camera 64 lines lower whether or not it was a training game. That is
     * a dropped branch, not a recompiler artifact, and the recording settles
     * which way round it goes: playa-10095214 jumps to 80.75 at f21888 on a
     * plain league match, where this test would have said 16. */
    int16_t viewY = 80;
    if (!_gDosGameplay && _trainingGame == 0) {
        viewY = 16;
    }
    ReplayUpdateCameraAndSwapBuffers(viewX, viewY);
    UpdatePlanePtrsAndBlitParams();

    SetBallPosition(1672, 449);
    _resultTimer = 30000;
    _timeVar = 32000;
    _stoppageEventTimer = (uint16_t)_gsStoppageGameOver;   /* Amiga 1500 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_RESULT_AFTER_THE_GAME;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;
    _lastTeamPlayedBeforeBreak = &_topTeamData;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
    /* The PC game's LoadAndPlayEndGameComment: the crowd, and a line about the match. */
    DOS_SOUND(DOS_EV_GAME_OVER, (_statsTeam1Goals & 0xFF) << 8 | (_statsTeam2Goals & 0xFF));
}

/* ================================================================
 * StartFirstExtraTime  (SWOS2.s line 22660)
 * ================================================================ */
__attribute__((used, externally_visible))
void StartFirstExtraTime(void)
{
    _halfNumber = 1;

    /* Random teamPlayingUp and teamStarting */
    _teamPlayingUp = (uint16_t)((Rand() & 1) + 1);
    _teamStarting = (uint16_t)((Rand() & 1) + 1);

    _hideBall = 0;
    InitTeamsData();
    _stoppageEventTimer = (uint16_t)_gsStoppageShowerPan;   /* Amiga 100 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_FIRST_EXTRA_STARTING;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;

    /* Select kickoff team: if teamStarting == teamPlayingUp, use top, else bottom */
    if (_teamStarting == _teamPlayingUp) {
        _lastTeamPlayedBeforeBreak = &_topTeamData;
    } else {
        _lastTeamPlayedBeforeBreak = &_bottomTeamData;
    }

    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * EndFirstExtraTime  (SWOS2.s line 22694)
 * ================================================================ */
__attribute__((used, externally_visible))
void EndFirstExtraTime(void)
{
    _halfNumber = 2;
    _teamPlayingUp = (uint16_t)(3 - _teamPlayingUp);
    _teamStarting = (uint16_t)(3 - _teamStarting);

    _hideBall = 0;
    InitTeamsData();
    _stoppageEventTimer = (uint16_t)_gsStoppageShowerPan;   /* Amiga 100 / DOS x1.1 — dos_gameplay.c */
    _gameState = ST_FIRST_EXTRA_ENDED;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _cameraDirection = 0xFFFF;

    if (_teamStarting == _teamPlayingUp) {
        _lastTeamPlayedBeforeBreak = &_topTeamData;
    } else {
        _lastTeamPlayedBeforeBreak = &_bottomTeamData;
    }

    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * PrepareForInitialKick  (SWOS2.s line 22724)
 * ================================================================ */
__attribute__((used, externally_visible))
void PrepareForInitialKick(void)
{
    /* SHOOTING PRACTICE: the computer side is down to its goalkeeper, who cannot
     * take a kickoff -- he walks to the centre circle and the match sits there.
     * This handles match start and period changes. Goals have their own restart
     * path in game_timers.c handleGoalScored, which applies the same rule.
     * The direction/turn flags below derive from the chosen team. */
    {
        int16_t forced = ShootingPractice_ForcedKickoffTeamNum();
        if (forced)
            _teamStarting = (uint16_t)forced;
    }

    SetBallPosition(336, 449);

    TeamGeneralInfo *kickoffTeam = &_topTeamData;
    int16_t direction = 4;             /* south */
    uint8_t turnFlags = 0x7C;          /* only allow E, SE, S, SW, W */

    if (_teamStarting != _teamPlayingUp) {
        kickoffTeam = &_bottomTeamData;
        direction = 0;                 /* north */
        turnFlags = 0xC7;              /* only allow E, NE, N, NW, W */
    }

    _gameState = ST_PLAYERS_TO_INITIAL_POSITIONS;
    _breakCameraMode = 0xFFFF;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _foulXCoordinate = 0x150;
    _foulYCoordinate = 0x1C1;
    _cameraDirection = direction;
    _playerTurnFlags = turnFlags;
    _lastTeamPlayedBeforeBreak = kickoffTeam;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    clearCameraVelocity();
}

/* ================================================================
 * StartPenalties  (SWOS2.s line 19467)
 * ================================================================ */
__attribute__((used, externally_visible))
void StartPenalties(void)
{
    _penaltiesState = 0xFFFF;
    _savedTeam1Goals = _statsTeam1Goals;
    _savedTeam2Goals = _statsTeam2Goals;
    _statsTeam1Goals = 0;
    _team1GoalsDigit1 = 0;
    _team1GoalsDigit2 = 0;
    _statsTeam2Goals = 0;
    _team2GoalsDigit1 = 0;
    _team2GoalsDigit2 = 0;
    _team1PenaltyGoals = 0;
    _team2PenaltyGoals = 0;

    _teamPlayingUp = (uint16_t)((Rand() & 1) + 1);
    _teamStarting = (uint16_t)((Rand() & 1) + 1);

    _team1PenaltyShooterIndex = 11;
    _team2PenaltyShooterIndex = 11;
    _team1PenaltyAttempts = 0;
    _team2PenaltyAttempts = 0;
    _playingPenalties = 1;
    _dontShowScorers = 1;

    /* Clear cards/sentaway for both teams */
    int16_t i;
    for (i = 10; i >= 0; i--) {
        Sprite *sprite = _team1SpritesTable[i];
        sprite->cards   = 0;
        sprite->sentAway = 0;
    }
    for (i = 10; i >= 0; i--) {
        Sprite *sprite = _team2SpritesTable[i];
        sprite->cards   = 0;
        sprite->sentAway = 0;
    }

    NextPenalty();
}

/* ================================================================
 * ApplyPitchPaletteAndFactors
 *
 * InitGameRestoreTeams' apply_palette_and_pitch_tables (SWOS2.s), on its own: the pitch
 * type's colours into gamePalette, the Jaguar shadow re-made from that grass, and the
 * pitch's ball physics. The highlights viewer needs it too (ViewHighLightsMain) -- the
 * original reached it by falling through into InitGameRestoreTeams.
 * ================================================================ */
void ApplyPitchPaletteAndFactors(void)
    {
        /* Apply pitch palette */
        uint16_t pitchIdx = _pitchNumberAndType & 0xFF;

        /* paletteTable[2] = &gamePalette (offset 8 in paletteTable is index 2) */
        _paletteTable[2] = _gamePalette;

#ifdef AGA
        {
            /* AGA: apply 9 pitch entries via paletteIndices, write full 256-color palette to copper */
            extern uint8_t _paletteIndices[];
            const uint16_t *palPtr = _pitchPalettePtrs[pitchIdx];
            int16_t pi;
            for (pi = 0; pi < 9; pi++)
            {
                uint16_t idx = _paletteIndices[pi];
                ((uint16_t *)_gamePalette)[idx] = palPtr[pi];
            }
        }
#else
        {
            /* OCS: copy 3 palette entries to fixed positions */
            uint16_t *palPtr = (uint16_t *)_pitchPalettePtrs[pitchIdx];
            *(uint16_t *)(_gamePalette) = palPtr[0];
            *(uint16_t *)(_gamePalette + 0xE) = palPtr[1];
            *(uint16_t *)(_gamePalette + 0x12) = palPtr[2];
        }
#endif

        /* The Jaguar sprites' shadow is the PITCH in shade, so it is re-made from
         * the grass this match is played on (jag_graphics.c). No-op otherwise. */
        JagGfx_PitchApplied();

        /* Pitch physics factors (pitchIdx >> 1 for word-indexed tables) */
        uint16_t physIdx = pitchIdx;
        _pitchBallSpeedFactor = _kPitchBallSpeedInfluence[physIdx];
        _ballSpeedBounceFactor = _kBallSpeedBounceFactorTable[physIdx];
        _ballBounceFactor = _kBallBounceFactorTable[physIdx];
    }

/* ================================================================
 * InitGameRestoreTeams  (SWOS2.s line 11915)
 *
 * Master match initialization: restores team data from snapshots,
 * applies tactics, sets up palette/pitch factors, clears all
 * per-match stats/state, then calls StartingMatch.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InitGameRestoreTeams(void)
{
    /* DOS-vs-Amiga tuning has to be in the tables BEFORE this routine reads
     * them. It LATCHES three pitch physics factors out of
     * _kPitchBallSpeedInfluence / _kBallSpeedBounceFactorTable /
     * _kBallBounceFactorTable below, and the only other ApplyGameStyle call
     * site is InitTeamsData, which StartingMatch reaches from INSIDE GameLoop
     * — after this. So DOS mode was latching the AMIGA ball-speed influence
     * and using it for the whole match: with the pitch the replay corpus rolls,
     * 2 instead of 4, i.e. two units per frame less ground friction on every
     * loose ball. ApplyGameStyle is a pure write of constants, so calling it
     * twice is free. (Found by the rig: the kick-off ball decelerated 2208 ->
     * 2193 where the recording has 2191 — docs/TESTS.md.) */
    ApplyGameStyle();

    InitAdvertisements();
    CopyPlayerNameLengths();

    /* Pick player card chance from table based on game length */
    uint16_t lenIdx = _gameLengthInGame;
    uint16_t *cardChanceTable = (uint16_t *)_playerCardChancesPerGameLength[lenIdx];
    uint8_t r = Rand();
    _playerCardChance = cardChanceTable[(r & 0x1E) >> 1];

    _overlayActiveFlag = 0;

    /* Restore team data from snapshots (1018 bytes each) */
    uint8_t *src1 = (uint8_t *)&_topTeamSaved;
    uint8_t *dst1 = (uint8_t *)&_topTeamInGame;
    uint8_t *src2 = (uint8_t *)&_bottomTeamSaved;
    uint8_t *dst2 = (uint8_t *)&_bottomTeamInGame;
    int16_t i;
    for (i = 1017; i >= 0; i--) {
        *dst1++ = *src1++;
        *dst2++ = *src2++;
    }

    InitializePlayerSpriteFrameIndices();

#ifdef DETERMINISTIC_MODE
    {
        extern uint16_t currentGameTick;
        extern uint16_t currentTick;
        currentGameTick = 0;
        currentTick = 0;
        InitDeterministicMode();
    }
#endif
    _gameRandValue = Rand();

    /* Apply tactics for both teams */
    ApplyTeamTactics((uint8_t *)&_topTeamInGame);
    ApplyTeamTactics((uint8_t *)&_bottomTeamInGame);
#ifdef SWOS_SDL_BUILD
    {
        extern void MatchSim_LogSkillRoll(void);
        MatchSim_LogSkillRoll();        /* each side's form for this match, in SWOS.log */
    }
#endif

    _paletteIndex = 2;
    /* nullsub_5 — no-op */

    CrowdResetMatchState();

    /* Random teamPlayingUp and teamStarting */
    _teamPlayingUp = (uint16_t)((Rand() & 1) + 1);
    _teamStarting = (uint16_t)((Rand() & 1) + 1);

    /* Set time delta from game length table */
    _timeDelta = *(uint16_t *)((uint8_t *)_gameLenSecondsTable + (_gameLengthInGame << 1));

    if (_playHighlightFlag != 0) {
        goto apply_palette_and_pitch;
    }
    /* bne.w *+4 — when playHighlightFlag != 0, skip to palette setup */

apply_palette_and_pitch:
    ApplyPitchPaletteAndFactors();

    /* Clear match state */
    _playingPenalties = 0;
    _dontShowScorers = 0;
    _statsTimer = 0;
    _showStats = 0;
    _showingUserStats = 0;
    _subsTeamsSwapped = 0;
    _inSubstitutesMenu = 0;
    _cameraLeavingSubsTimer = 0;
    _benchOff = 0;
    _leavingSubsMenu = 0;
    _waitForPlayerToGoInTimer = 0;
    _substituteInProgress = 0;

    /* Clear goal scores */
    _statsTeam1Goals = 0;
    _team1GoalsDigit1 = 0;
    _team1GoalsDigit2 = 0;
    _statsTeam2Goals = 0;
    _team2GoalsDigit1 = 0;
    _team2GoalsDigit2 = 0;
    _team1TotalGoals = 0;
    _team2TotalGoals = 0;

    if (_secondLeg != 0) {
        _team1TotalGoals = _team1GoalsFirstLeg;
        _team2TotalGoals = _team2GoalsFirstLeg;
    }

    /* Clear substitution and goalkeeper replacement counts */
    _team1NumSubs = 0;
    _team2NumSubs = 0;
    _team1GoalkeeperReplaced = 0;
    _team2GoalkeeperReplaced = 0;

    /* Clear team stats */
    _team1StatsData.ballPossession = 0;
    _team1StatsData.cornersWon     = 0;
    _team1StatsData.foulsConceded  = 0;
    _team1StatsData.bookings       = 0;
    _team1StatsData.sendingsOff    = 0;
    _team1StatsData.goalAttempts   = 0;
    _team1StatsData.onTarget       = 0;

    _team2StatsData.ballPossession = 0;
    _team2StatsData.cornersWon     = 0;
    _team2StatsData.foulsConceded  = 0;
    _team2StatsData.bookings       = 0;
    _team2StatsData.sendingsOff    = 0;
    _team2StatsData.goalAttempts   = 0;
    _team2StatsData.onTarget       = 0;

    InitDisplaySprites();
    InitCurrentGoalHighlight();
    ResetTime();
    ResetResultSprites();
    ClearSaveFlagForGoalSprites();
    ResetGoalAttempt();
    InitAnimatedPatterns();
    StartingMatch();
    PlaySample1IfNotTraining();

    if (_playGame != 0) {
        PlayPenaltySample();
        _sfxQueueWriteIdx = 500;
        if (_trainingGame == 0) {
            _showFansCounter = 100;
        }
    }
}
