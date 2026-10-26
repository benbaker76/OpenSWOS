/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* game_loop.c — Game Loop, MainKeysCheck, Highlights Playback, AssignFakeGoalsToScorers
 *
 * Chunk 41: 10 functions (7 exported + 2 static + 1 helper)
 */

#include "raster_bars.h"     /* -DRASTER_BARS: the frame's phases as colour bands */
#include "game_loop.h"
#include "dos_sound.h"
#include "game_types.h"
#include "display.h"
#include "deterministic.h"
#include "challenge.h"       /* the 'C' key, and a saved position loaded before frame 1 */
#include "fade.h"            /* ApplyFadeStep, FADE_MAX — see Challenge_LoadState's caller */

/* ---- Extern globals (data section, exported from asm/Data.c) ---- */
extern uint16_t _gameCanceled;
extern uint16_t _instantReplayFlag;
extern volatile uint16_t _saveHighlightScene;
extern volatile uint16_t _fadeAndSaveHighlightScene;
extern uint16_t _spaceReplayTimer;
extern uint16_t _subsWriteOnly;
extern volatile uint16_t _playGame;
extern volatile uint16_t _playHighlightFlag;
extern uint16_t _normalReplayRunning;
extern uint16_t _userRequestedReplay;
extern uint16_t _overlayFlagBackup;
extern volatile uint16_t _paused;
extern volatile uint16_t _panicReasonCode;
extern void PanicHaltLoop(void);
extern uint16_t _overlayActiveFlag;
extern volatile uint16_t _currentTick;
extern volatile uint16_t _currentGameTick;
extern volatile uint16_t _currentKey;
extern uint16_t _frameCount;
extern volatile uint16_t _fadeState;
extern volatile uint16_t _fadeInitFlag;
extern volatile uint16_t _goIntoHighlightsLoop;
extern uint16_t _replayState;
extern uint16_t _showingUserStats;
extern uint16_t _trainingGame;
extern uint16_t _inSubstitutesMenu;
extern uint16_t _gameState;
extern uint16_t _gameStatePl;
extern uint16_t _crowdChantsOn;
extern uint16_t _spinBigS;
extern uint16_t _currentPlayerIndicatorPosition;
extern uint16_t _bench1Called;
extern uint16_t _bench2Called;
extern uint16_t _fireBlocked;
extern volatile uint16_t _goalCameraMode;
extern uint16_t _isGameFriendly;
extern uint16_t _replaySelected;
extern uint16_t _team1NumAllowedInjuries;
extern uint16_t _team2NumAllowedInjuries;
extern uint16_t _extraTimeState;
extern uint16_t _penaltiesState;
extern uint16_t _writeOnly01;
extern uint32_t _gameTime;
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern int16_t  _oldCameraX, _oldCameraY;
extern uint16_t _cameraCoordinatesValid;

/* The Amiga AGA match frame overlaps the undraw with the logic (see runFrame's body). */
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
#define MATCH_BACKGROUND_UNDRAW 1
#else
#define MATCH_BACKGROUND_UNDRAW 0
#endif
extern FixedPoint _cameraY;
extern volatile int16_t _waitingInterrupt;
extern uint32_t _irqCookieObfuscated;
extern uint16_t _quitToDos;

extern uint8_t _pl1Fire;
extern uint8_t _pl2Fire;
extern TeamGeneralInfo *_teamScoredDataPtr;

extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint16_t _statsTeam1GoalsCopy;
extern uint16_t _statsTeam2GoalsCopy;
extern uint16_t _team1TotalGoals;
extern uint16_t _team2TotalGoals;
extern uint16_t _pl1Tactics;
extern uint16_t _pl2Tactics;
extern void *_playersPositionsByTactics[18];

extern uint16_t _hilSceneNumber;
extern volatile uint16_t _numScenesInHilFile;
extern uint8_t _hilStartOfGoalsBuffer[];
extern uint8_t *_goalBasePtr;
extern uint8_t *_currentHilPtr;
extern uint8_t *_nextGoalPtr;

/* kAbortLossScorerWeightTable is in code section of SWOS2.s — 12 bytes */
extern int8_t _kAbortLossScorerWeightTable[12];

/* ---- Extern function declarations (already ported or asm) ---- */
extern void LoadSfxResources(void);
extern void EnsureSfxLoadedAndStart(void);
extern void SetPitchTypeAndNumber(void);
extern void InitGameSaveTeams(TeamGame *topTeam, TeamGame *bottomTeam);
extern void InitGameRestoreTeams(void);
extern void ApplyPitchPaletteAndFactors(void);
extern uint16_t Rand(void);
extern uint16_t Rand2(void);
extern void ReplayUpdateCameraAndSwapBuffers(int16_t x, int16_t y);
extern void UpdatePlanePtrsAndBlitParams(void);
extern uint16_t ReadTimerDelta(void);   /* real return type — wasm traps on a void-signature indirect call */
extern uint16_t GetKey(void);
extern void ProcessSpriteBlitQueue(void);
extern void BlitterCopyPanelToScreen(void);
extern void FadeOutToBlack(void);
extern void FadeOutAndIn(void);
extern void StopAudioAndClearFlags(void);
extern void SaveHighlightsPointers(void);
extern void RestoreHighlightPointers(void);
extern void FadeAndSaveHighlightScene(void);
extern void SaveHighlightScene(void);
extern void DrawReplayFrame(void);
extern void HandleInstantReplayStateSwitch(void);
extern void RestoreCameraPosition(void);
extern void SaveCoordinatesForHighlights(int16_t marker, uint16_t camX, uint16_t camY);
extern void PlayEnqueuedSamples(void);
extern void UpdateTime(void);
extern void MovePlayers(void);
extern void UpdateReferee(void);
extern void UpdateCornerFlagSprite(void);
extern void SpinBig_S_Sprite(void);
extern void ManageAdvertisements(void);
extern void UpdateControlledPlayerNumbers(void);
extern void MarkPlayer(void);
extern void SetCurrentPlayerName(void);
extern void UpdateBookedPlayerNumberSprite(void);
extern void ShowResult(void);
extern void DrawAnimatedPatterns(void);
extern void BenchCheckControls(void);
extern void DrawBenchAndSubsMenu(void);
#ifdef SWOS_NETPLAY
extern int32_t NetplayMatch_IsLive(void);     /* netplay_match.c */
extern int32_t NetplayMatch_IsP2P(void);
#endif
extern void UpdateAndDrawStatistics(void);
extern void AllowStatistics(void);
extern void StatisticsOff(void);
extern void TurnCrowdChantsOn(void);
extern void TurnCrowdChantsOff(void);
extern void Player1StatusProc(void);
extern void Player2StatusProc(void);
extern void SetHilFileHeader(void);
extern void AudioFadeCrowdDownThenPulseMusic(void);
extern void ClearAudioActiveFlag(void);
extern void ReplayExitMenuAfterFriendly(void);
extern void CopyTeamsAndHeader(void);
extern void ScrollToCurrent(void);

/* Camera (ported to C) */
extern void MoveCamera(void);
/* Asm functions NOT ported — called via bsr.w in original */
extern void UpdateAndApplyTeamControls(void);
extern void UpdateBall(void);
extern void InjuryUpdateTickGuard(void);
extern void DoGoalkeeperSprites(void);
extern void DrawSprites(void);

/* ---- Helper: VBlank sync with anti-tamper cookie check ----
 * Each loop has its own XOR magic and expected value.
 * The anti-tamper target labels are in asm dead code and never taken.
 */
static void waitVBlankSyncPresent(uint32_t xorMagic, uint32_t expected, int32_t present)
{
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY()) {}
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;

    /* Anti-tamper cookie check — always passes in normal execution */
    {
        uint32_t d0 = _irqCookieObfuscated ^ xorMagic;
        (void)d0;
        (void)expected;
    }

#ifdef SWOS_SDL_BUILD
    /* The game loop draws the full frame, then enters waitVBlankSync.
     * On Amiga this spun on `waitingInterrupt` until the VBlank ISR
     * cleared it after a buffer-swap. On SDL we run that swap inline
     * (matching `WaitForVSync` in game_utils.c) and hand control to
     * Platform_VBlank, which blits the just-finished frontbuffer to
     * the SDL texture + paces to 50 Hz. Without this, the game-loop
     * branch of `waitVBlankSync` never called Platform_VBlank — the
     * SDL timer thread was the only path that presented frames and it
     * read the frontbuffer mid-draw, tearing sprites every frame. */
    {
        extern void SwapBuffersAndCameraViewState(void);
        extern void SetCameraCoordinates(int16_t x, int16_t y);
        extern int Platform_VBlank(void);
        int16_t cx = _cameraX.s.i;
        int16_t cy = _cameraY.s.i;
        SwapBuffersAndCameraViewState();
        SetCameraCoordinates(cx, cy);
        /* A frame netplay re-simulates still swaps and sets the camera,
         * which the next frame reads, but is never shown. */
        if (present)
            Platform_VBlank();
    }
#else
    (void)present;
#if defined(AGA)
    /* Each blit waits for the one before it, so the frame's LAST blit can still be
     * running here: let it finish before the buffer it draws into is put on screen. */
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
        ;
#endif
    RASTER(RASTER_IDLE);
    _waitingInterrupt = 1;
    while (_waitingInterrupt != 0) {}
#endif
}

static void waitVBlankSync(uint32_t xorMagic, uint32_t expected)
{
    waitVBlankSyncPresent(xorMagic, expected, 1);
}

/* ---- Pause spin-wait: waits 1 tick (dbf d1 with d1=0 → 1 iteration) ----
 *
 * Original Amiga: busy-spin on `currentTick`. The Amiga's VBlank ISR
 * advances currentTick AND services keyboard input natively, so a
 * busy spin works.
 *
 * On SDL the `currentTick` advance happens on the SDL_Timer thread
 * but keyboard events are dispatched only when something pumps the
 * SDL event queue. The timer thread CAN do a full event pump when
 * the main thread looks idle (>60ms since last render) but on
 * Linux some setups schedule the busy main thread aggressively
 * enough that the timer thread can starve, locking us in the pause
 * loop forever (Linux tester report: "pause hangs, can't unpause").
 *
 * Replace the busy spin with Platform_VBlank() — it does the
 * 50 Hz pacing via SDL_Delay (which yields to the OS), pumps SDL
 * events on the main thread (so keypresses reliably reach
 * keyBuffer), and runs the throttled ISR housekeeping. The
 * Platform_VBlank path also re-renders the same frontbuffer so
 * the paused frame stays on screen. */
#ifdef SWOS_SDL_BUILD
extern int Platform_VBlank(void);
#endif

static void pauseSpinWaitOneTick(void)
{
#ifdef SWOS_SDL_BUILD
    Platform_VBlank();
#else
    uint16_t d0;
    /* dbf d1 with d1=0 means 1 iteration */
    d0 = _currentTick;
    while (_currentTick == d0) {}
#endif
}

/* ====================================================================
 * MainKeysCheck — key dispatch during game
 * ==================================================================== */
void __attribute__((used, externally_visible))
MainKeysCheck(uint16_t key)
{
    if (key == 0)
        return;

    /* If paused, only 'P' can unpause */
    if (_paused != 0)
        goto key_p_toggle_pause;

    /* If stats overlay is showing, only 'S' can close it */
    if (_showingUserStats != 0)
        goto key_s_close_statistics;

    /* If viewing highlights, only 'P' works */
    if (_playHighlightFlag != 0)
        goto key_p_toggle_pause;

    /* If normal replay running, only 'P' works */
    if (_normalReplayRunning != 0)
        goto key_p_toggle_pause;

    /* 'R' — instant replay request */
    if (key == 0x52) {
        if (_fadeState == 0)
            return;
        if (_inSubstitutesMenu != 0)
            return;
        if (_gameState == ST_RESULT_AFTER_THE_GAME)
            return;
        if (_gameState == ST_RESULT_ON_HALFTIME)
            return;
        _instantReplayFlag = 1;
        if (_replayState != 0)
            return;
        _userRequestedReplay = 1;
        return;
    }

    /* 'C' — save the position to a file, to be played on from later
     * (src/challenge.c). Challenge_SaveNow decides whether this is a position worth
     * keeping: a match under way, one human side against the computer, and not in the
     * middle of the substitutes menu or a replay. It writes the file from here, on the
     * frame the key was read, which is a coherent start-of-frame state -- GetKey has run
     * and nothing of the frame's logic has. */
    if (key == 0x43) {
        Challenge_SaveNow();
        return;
    }

    /* 'H' — normal replay after match */
    if (key == 0x48) {
        if (_normalReplayRunning != 0)
            return;
        if (_replayState != 0)
            return;
        if (_gameState != ST_RESULT_AFTER_THE_GAME)
            return;
        SaveHighlightsPointers();
        return;
    }

    /* 0x88 — toggle big S spin */
    if (key == 0x88) {
        _spinBigS ^= 1;
        return;
    }

    /* 0x87 — cycle player indicator position */
    if (key == 0x87) {
        _currentPlayerIndicatorPosition += 1;
        if (_currentPlayerIndicatorPosition == 5)
            _currentPlayerIndicatorPosition = 0;
        return;
    }

    /* 0x89 — fade and camera switch, toggle crowd chants */
    if (key == 0x89) {
        FadeOutAndIn();
        if (_crowdChantsOn != 0)
            TurnCrowdChantsOff();
        else
            TurnCrowdChantsOn();
        return;
    }

    /* 0x10 (Amiga 'Q') — request bottom team substitutions */
    if (key == 0x10) {
        if (_replayState != 0)
            return;
        if (_bottomTeamData.playerNumber == 0 &&
            _bottomTeamData.playerCoachNumber == 0)
            return;
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_DBG("[bench] HOTKEY 0x10 (Q) -> bench2Called\n");
#endif
        _bench2Called = 1;
        return;
    }

    /* 0x11 (Amiga 'W') — request top team substitutions */
    if (key == 0x11) {
        if (_replayState != 0)
            return;
        if (_topTeamData.playerNumber == 0 &&
            _topTeamData.playerCoachNumber == 0)
            return;
#ifdef SWOS_DEBUG_AUTOSKIP
        SWOS_DBG("[bench] HOTKEY 0x11 (W) -> bench1Called\n");
#endif
        _bench1Called = 1;
        return;
    }

    /* 'S' — open statistics */
    if (key == 0x53) {
        if (_gameStatePl == ST_GAME_IN_PROGRESS)
            return;
        if (_inSubstitutesMenu != 0)
            return;
        if (_replayState != 0)
            return;
        /* bcs: gameState < ST_STARTING_GAME → allow stats */
        if (_gameState >= ST_STARTING_GAME && _gameState <= ST_GAME_ENDED)
            return;
        AllowStatistics();
        return;
    }

    /* 'W' — no-op */
    if (key == 0x57)
        return;

    /* ESC — handle game exit */
    if (key == 0x1B) {
        TeamGeneralInfo *a1, *a2;
        if (_replayState != 0)
            return;
        if (_playGame == 0)
            goto stop_highlight_playback;

        _playGame = 0;
        _overlayActiveFlag = 0;
        _writeOnly01 = 1;

        if (_gameStatePl != ST_GAME_IN_PROGRESS) {
            if (_gameState == ST_PLAYERS_GOING_TO_SHOWER)
                return;
            if (_gameState == ST_RESULT_AFTER_THE_GAME)
                return;
        }

        /* Choose primary team: prefer team with teamNumber == 1 */
        a1 = &_topTeamData;
        a2 = &_bottomTeamData;
        if (a1->teamNumber != 1) {
            TeamGeneralInfo *tmp = a1;
            a1 = a2;
            a2 = tmp;
        }

        /* Check team presence (playerNumber / playerCoachNumber) */
        if (a1->playerNumber == 0) {
            if (a1->playerCoachNumber != 0) {
                /* a1 has coach only — if a2 has no player or coach, assign fake goals to a2's team */
                if (a2->playerNumber != 0)
                    goto esc_cancel_game_clear_extras;
                if (a2->playerCoachNumber != 0)
                    goto esc_cancel_game_clear_extras;
                goto esc_assign_fake_goals_team2;
            }
            /* a1 has no player, no coach */
            if (a2->playerNumber != 0)
                goto esc_assign_fake_goals_team1;
            if (a2->playerCoachNumber != 0)
                goto esc_assign_fake_goals_team1;
            goto esc_cancel_game_clear_extras;
        }

        /* a1 has players */
        if (a2->playerNumber != 0)
            goto esc_cancel_game_clear_extras;
        if (a2->playerCoachNumber != 0)
            goto esc_cancel_game_clear_extras;

    esc_assign_fake_goals_team2:
        if (_gameTime == 0)
            goto esc_cancel_game_clear_extras;
        {
            uint16_t d0_cmp = _team1TotalGoals;
            _statsTeam2Goals += 5;
            _team2TotalGoals += 5;
            while (!(d0_cmp < _team2TotalGoals)) {
                _statsTeam2Goals += 1;
                _team2TotalGoals += 1;
            }
            {
                int16_t goalDiff = (int16_t)(_team2TotalGoals - d0_cmp);
                /* negate: asm does sub + neg, but sub(d0, team2TotalGoals) = team2TotalGoals - d0, neg = d0 - team2TotalGoals...
                 * Actually asm: sub.w team2TotalGoals(pc),d0 → d0 = d0 - team2TotalGoals; neg.w d0 → d0 = team2TotalGoals - d0_original
                 * d0 was loaded as team1TotalGoals originally. So goalDiff = team2TotalGoals - team1TotalGoals */
                goalDiff = (int16_t)(_team2TotalGoals - d0_cmp);
                /* Wait - re-reading the asm: d0 was loaded with team1TotalGoals initially.
                 * After the increment loop: sub.w team2TotalGoals, d0 → d0 = old_d0_value - team2TotalGoals
                 * But d0 changed during the loop? No, d0 was set once before the loop and never changed.
                 * Actually, d0 is team1TotalGoals (loaded at line 15391).
                 * sub.w team2TotalGoals(pc),d0 → d0 = team1TotalGoals_orig - team2TotalGoals_new
                 * neg.w d0 → d0 = team2TotalGoals_new - team1TotalGoals_orig = goalDiff */
                AssignFakeGoalsToScorers(2, (int16_t)(_team2TotalGoals - d0_cmp));
            }
        }
        goto esc_finalize;

    esc_assign_fake_goals_team1:
        if (_gameTime == 0)
            goto esc_cancel_game_clear_extras;
        {
            uint16_t d0_cmp = _team2TotalGoals;
            _statsTeam1Goals += 5;
            _team1TotalGoals += 5;
            while (!(d0_cmp < _team1TotalGoals)) {
                _statsTeam1Goals += 1;
                _team1TotalGoals += 1;
            }
            {
                int16_t goalDiff = (int16_t)(_team1TotalGoals - d0_cmp);
                AssignFakeGoalsToScorers(1, goalDiff);
            }
        }

    esc_finalize:
        _statsTeam1GoalsCopy = _statsTeam1Goals;
        _statsTeam2GoalsCopy = _statsTeam2Goals;
        _extraTimeState = 0;
        _penaltiesState = 0;
        return;

    esc_cancel_game_clear_extras:
        _extraTimeState = 0;
        _penaltiesState = 0;
        _gameCanceled = 1;
        return;
    }

    /* SPACE — capture highlight scene */
    if (key == 0x20) {
        if (_playHighlightFlag != 0)
            goto key_p_toggle_pause;
        if (_normalReplayRunning != 0)
            goto key_p_toggle_pause;
        if (_spaceReplayTimer != 0)
            return;
        _fadeAndSaveHighlightScene = 1;
        _spaceReplayTimer = 0x64;
        return;
    }

key_p_toggle_pause:
    if (key != 0x50)
        return;
    if (_inSubstitutesMenu != 0)
        return;
    _paused ^= 1;
    if (_paused != 0) {
        _overlayFlagBackup = _overlayActiveFlag;
        _overlayActiveFlag = 1;
    } else {
        _overlayActiveFlag = _overlayFlagBackup;
    }
    return;

key_s_close_statistics:
    if (key != 0x53)
        return;
    StatisticsOff();
    return;

stop_highlight_playback:
    if (_playHighlightFlag == 0)
        return;
    _playHighlightFlag = 0;
    return;
}

/* ====================================================================
 * AssignFakeGoalsToScorers — assigns fake goals when ESC-aborting
 * teamId: 1 = top team, 2 = bottom team
 * numGoals: number of goals to assign
 * ==================================================================== */
void __attribute__((used, externally_visible))
AssignFakeGoalsToScorers(uint16_t teamId, int16_t numGoals)
{
    TeamGame *teamInGame;
    uint16_t tactics;
    uint8_t *posTable;
    int16_t remaining = numGoals - 1;

    if (remaining < 0)
        return;

    if (teamId == 1) {
        teamInGame = &_topTeamInGame;
        tactics = _pl1Tactics;
    } else {
        teamInGame = &_bottomTeamInGame;
        tactics = _pl2Tactics;
    }

    posTable = (uint8_t *)_playersPositionsByTactics[tactics];

    while (remaining >= 0) {
        uint8_t rval;
        uint16_t playerSlot;

        rval = (uint8_t)Rand2();
        if (rval >= 248) {
            /* Own goal */
            teamInGame->numOwnGoals += 1;
            remaining--;
            continue;
        }

        rval &= 0x7F;
        playerSlot = 10;

        /* Walk through weight table: 11 entries for outfield positions (10..0) */
        {
            int i;
            for (i = 0; i < 11; i++) {
                uint8_t weight = (uint8_t)_kAbortLossScorerWeightTable[i];
                if (rval < weight)
                    goto found_scorer;
                rval -= weight;
                playerSlot--;
            }
        }

        /* Fell through all 11 weights — second random for goalkeeper chances */
        rval = (uint8_t)Rand2();
        rval &= 0x7F;
        playerSlot = 1;
        {
            uint8_t weight = (uint8_t)_kAbortLossScorerWeightTable[10];
            if (rval < weight)
                goto found_scorer;
            rval -= weight;
            playerSlot = 0;
        }

    found_scorer:
        {
            uint8_t posIdx = posTable[playerSlot];
            teamInGame->players[posIdx].goalsScored += 1;
        }
        remaining--;
    }
}

int32_t _gFrameStepInput = 0;

/* ====================================================================
 * GameLoop_Frame — one frame of the match
 *
 * The body of GameLoop's loop, from the timer read at its top to the
 * highlight bookkeeping at its end; the vblank wait that ends a pass is
 * GameLoop_EndFrame.
 *
 * All of it runs on every frame, drawing included — the render calls write
 * match state too (DrawSprites sets `onScreen`, DrawAnimatedPatterns calls
 * Rand) — so a frame netplay re-simulates is this whole function, and only
 * showing it is left out (src/netplay_match.c).
 * ==================================================================== */
GameFrameResult GameLoop_Frame(void)
{
    ReadTimerDelta();

    /* Pause spin loop */
    while (_paused != 0) {
        pauseSpinWaitOneTick();
        GetKey();
        MainKeysCheck(_currentKey);
        /* nullsub_10, nullsub_5, nullsub_4 — no-ops */
        PlayEnqueuedSamples();
    }

    /* Handle user stats overlay */
    if (_showingUserStats != 0) {
        KeepStatisticsOnScreen();
        /* Input a frame at a time: the overlay stays up into the next frame. */
        if (_showingUserStats != 0)
            return GAME_FRAME_HELD;
    }

    /* Frame counters */
    _frameCount += 1;
    if (_spaceReplayTimer != 0)
        _spaceReplayTimer -= 1;
    /* The save-position key's own hold-off, beside the one SPACE uses: a key is read on
     * every frame it is held, so one press of 'C' wrote a file per frame (four of them,
     * first time it was tried). */
    Challenge_TickNotice();
    Challenge_DumpPitch();      /* TEMPORARY diagnostic */
#ifdef CHAL_AUTOPROBE
    Challenge_AutoProbe();      /* TEMPORARY: see include/challenge.h */
#endif

    /* Panel update block */
    RASTER(RASTER_UNDRAW);
#if MATCH_BACKGROUND_UNDRAW
    /* The bobs of two frames ago are wiped by the blitter on its own (display.c) while
     * the CPU reads the keys, moves the camera and runs the frame's logic. Anything that
     * may draw or swap first waits for it: the panel copy (its blitter waits do), a key
     * (pause, the crowd-chant fade, ESC), a frame that leaves the loop. */
    ProcessSpriteBlitQueueAsync();
#else
    ProcessSpriteBlitQueue();
#endif
    if (_subsWriteOnly != 0) {
#if defined(AGA) && !defined(SWOS_SDL_BUILD)
        { extern void RestoreWindowFromTwins(void); RestoreWindowFromTwins(); }
#else
        BlitterCopyPanelToScreen();
#endif
        _subsWriteOnly -= 1;
    }

    /* Read inputs and dispatch */
    GetKey();
#if MATCH_BACKGROUND_UNDRAW
    if (_currentKey != 0)
        BlitQueueDrain();
#endif
    MainKeysCheck(_currentKey);
    /* nullsub_10, nullsub_9 — no-ops */

#if MATCH_BACKGROUND_UNDRAW
    if (_playGame == 0 || _goIntoHighlightsLoop != 0 || _replayState != 0)
        BlitQueueDrain();
#endif
    /* Check end of match */
    if (_playGame == 0)
        return GAME_FRAME_MATCH_OVER;

    /* Fade state management */
    if (_fadeState == 0 && _fadeInitFlag == 0x3E7)
        _fadeInitFlag = 0xFFFF;

    /* Handle active highlights loop */
    if (_goIntoHighlightsLoop != 0) {
        FadeOutToBlack();
        PlayHighlightsLoop();
        return GAME_FRAME_DETOUR;
    }

    /* Handle replay state change */
    if (_replayState != 0) {
        FadeOutToBlack();
        PlayInstantReplayLoop();
        return GAME_FRAME_DETOUR;
    }

    /* Run gameplay update */
    RASTER(RASTER_SCROLL);
    MoveCamera();
#if MATCH_BACKGROUND_UNDRAW
    /* The scroll draws into the frame being undrawn, so it waits for the undraw, after
     * the logic below. The logic reads one thing the scroll writes: whether the camera
     * stood still (the break timers wait for it). The scroll leaves it 1 exactly when
     * neither axis moved since this buffer was last scrolled (scroll_blit.c), and the
     * logic does not move the camera, so it is set here to the value the scroll will
     * write -- the logic reads what it always read. */
    _cameraCoordinatesValid = (uint16_t)(_cameraX.s.i == _oldCameraX && _cameraY.s.i == _oldCameraY);
#else
    ScrollToCurrent();
#endif
    /* Record the camera in the SAME space the per-sprite coordinates are written
     * in. Those come off the draw path (sprites.c), which measures from
     * Render_CameraX() -- the RENDER camera. `_cameraX` is the SIMULATION one,
     * and since 2b67c6f the two are not a shift apart but a SCALE, so storing
     * `_cameraX` here made `sprite_x + cam_x` mean nothing: it was a world
     * position plus cam * (1 - max/pan), drifting further the more the camera had
     * moved. Every stationary object -- the goal overlay most visibly -- slid
     * against the pitch as the camera panned.
     *
     * Measured in a tester's own .HIL (ReportBot 0006, recorded at ~485 wide):
     * the bottom goal is pinned at world x = 300 (sprite_data.c) yet
     * `sprite_x + cam_x` walked 415 -> 379 across one scene, fitting
     * 300 + cam * 0.443 on every frame -- exactly 1 - max/pan at that width.
     *
     * With both in render space, `sprite_x + cam_x` IS the world position, which
     * is what DrawReplayFrame now reconstructs and re-projects through the
     * playback camera. Identity at 336 and on the Amiga, where max == pan. */
    SaveCoordinatesForHighlights((int16_t)0xFFFF,
#ifdef SWOS_SDL_BUILD
                                 (uint16_t)Render_CameraX(),
#else
                                 _cameraX.s.i,
#endif
#ifdef SWOS_SDL_BUILD
                                 (uint16_t)Render_CameraY());
#else
                                 _cameraY.s.i);
#endif
#ifdef SWOS_SDL_BUILD
    { extern void ReplayRec_FrameBegin(void); ReplayRec_FrameBegin(); }
#endif
    PlayEnqueuedSamples();
    UpdateTime();
#ifdef SWOS_SDL_BUILD
    /* Arcade in-match rules (deterministic frame count): golden goal / time limit /
     * penalties. No-op unless _gArcadeMode (checked inside). */
    { extern void Arcade_MatchTick(void); Arcade_MatchTick(); }
#endif
    /* PENALTY SHOOTOUT (the PRACTICE submenu): on the match's first frame, hand
     * it to the engine's own shootout. No-op unless latched at kick-off
     * (checked inside). Both targets, so outside the block above. */
    { extern void PenaltyShootout_MatchTick(void); PenaltyShootout_MatchTick(); }
    /* nullsub_8 — no-op */
    RASTER(RASTER_SIM);
    UpdateAndApplyTeamControls();
    UpdateBall();
    MovePlayers();
    UpdateReferee();
    InjuryUpdateTickGuard();
    UpdateCornerFlagSprite();
    SpinBig_S_Sprite();
    /* Widescreen: the in-match world (ads, animated patterns, players/ball) draws at
     * offset 0 — the pitch fills the whole frame. Menu/HUD centering is re-established
     * by DrawMenu when the frontend draws again. */
    _menu_draw_x_offset = 0;
    ManageAdvertisements();
    DoGoalkeeperSprites();
    UpdateControlledPlayerNumbers();
    MarkPlayer();
    SetCurrentPlayerName();
    UpdateBookedPlayerNumberSprite();
    ShowResult();
#if MATCH_BACKGROUND_UNDRAW
    RASTER(RASTER_SCROLL);
    BlitQueueDrain();
    {
        /* a crossing's twin rows go to the head of this buffer's undraw queue
         * (scroll_blit.c), which the next frame of this buffer runs first */
        extern uint16_t g_scrollTwinQueue;
        g_scrollTwinQueue = 1;
        ScrollToCurrent();
        g_scrollTwinQueue = 0;
    }
#endif
    RASTER(RASTER_CROWD);
    DrawAnimatedPatterns();   /* also ticks _showFansCounter / goal-celebration state */
    RASTER(RASTER_SPRITES);
    DrawSprites();
#ifdef SWOS_SDL_BUILD
    { extern void ReplayRec_CaptureSprites(void); ReplayRec_CaptureSprites(); }
#endif
    RASTER(RASTER_HUD);
    DrawBenchAndSubsMenu();
    BenchCheckControls();
    UpdateAndDrawStatistics();

    /* The window was closed. Not out of a LIVE match from here: this frame may be one
     * GGPO is re-simulating, and "a re-simulated frame left the match loop" was an
     * exit(2) -- the game gone with nothing saved and nothing sent, which is what closing
     * the window at the final whistle did (2026-10-12: a 7-1 neither player's game ended).
     * A live match is left through liveEnd, at the top of its next frame
     * (netplay_match.c), which saves it and tells the recorder and the other player. */
    if (_quitToDos != 0
#ifdef SWOS_NETPLAY
        && !(NetplayMatch_IsLive() && NetplayMatch_IsP2P())   /* two players, not a resim */
#endif
        )
        return GAME_FRAME_QUIT;

    /* Post-frame: save coordinates end marker */
    SaveCoordinatesForHighlights((int16_t)0xFFFE, 0, 0);
#ifdef SWOS_SDL_BUILD
    { extern void ReplayRec_FrameEnd(void); ReplayRec_FrameEnd(); }
#endif

    if (_saveHighlightScene != 0) {
        SaveHighlightScene();
        _saveHighlightScene = 0;
    }
    if (_fadeAndSaveHighlightScene != 0) {
        FadeAndSaveHighlightScene();
        _fadeAndSaveHighlightScene = 0;
    }
    if (_instantReplayFlag != 0) {
        HandleInstantReplayStateSwitch();
        _instantReplayFlag = 0;
        _goalCameraMode = 0;
    }

    return GAME_FRAME_DONE;
}

/* The vblank wait that ends a pass of the match loop. On SDL: swap the
 * buffers, set the camera and show the frame — or, with `present` 0, all of
 * it but the showing. The Amiga always waits for its VBlank. */
void GameLoop_EndFrame(int32_t present)
{
    waitVBlankSyncPresent(0xCFD1F9BDu, 0x165Au, present);
#ifdef DETERMINISTIC_DUMP_FRAME
    RngDumpCheck();
#endif
}

/* One pass of GameLoop's loop, ended by its vblank wait unless the match is
 * leaving the loop. */
static GameFrameResult runFrame(void)
{
    GameFrameResult r;

#ifdef SWOS_SDL_BUILD
    /* A netplay match runs its frames as steps GGPO can repeat, and ends
     * them itself (docs/NETPLAY.md). */
    {
        extern int32_t NetplayMatch_Enabled(void);
        extern int32_t NetplayMatch_Frame(void);
        if (NetplayMatch_Enabled())
            return (GameFrameResult)NetplayMatch_Frame();
    }
    /* Recorded-game replay: take this frame's record and inject its stick
     * state before anything reads input (docs/TESTS.md 5b). No-op unless the
     * rig is running. */
    { extern void ReplayTest_FrameStart(void); ReplayTest_FrameStart(); }
#endif
    r = GameLoop_Frame();
    if (r == GAME_FRAME_MATCH_OVER || r == GAME_FRAME_QUIT)
        return r;
#ifdef SWOS_SDL_BUILD
    /* ... and compare the state this frame produced against the record. A
     * detour into the highlights or an instant replay deliberately skips
     * this — the rig reports such a frame rather than silently reusing its
     * record. */
    if (r == GAME_FRAME_DONE || r == GAME_FRAME_HELD) {
        extern void ReplayTest_FrameEnd(void);
        ReplayTest_FrameEnd();
    }
#endif
    GameLoop_EndFrame(1);
    return r;
}

/* ====================================================================
 * GameLoop — main per-frame game loop
 * ==================================================================== */
void __attribute__((used, externally_visible))
GameLoop(void)
{
    int16_t d1, d2;
    int32_t loadedPosition = 0;

    _gameCanceled = 0;
    _instantReplayFlag = 0;
    _saveHighlightScene = 0;
    _fadeAndSaveHighlightScene = 0;
    _spaceReplayTimer = 0;
#ifdef SWOS_SDL_BUILD
    /* Record the whole match (src/replay_v2.c): written to LATEST.SRP when it ends. */
    { extern void ReplayRec_MatchStart(void); ReplayRec_MatchStart(); }
#endif
    /* The PC game's LoadSoundEffects, LoadIntroChant and PlayCrowdNoiseSample, here in its
     * GameLoop too -- and after the recorder is on, so that a recording has it. */
    DOS_SOUND(DOS_EV_MATCH_START, (_topTeamInGame.prShirtCol & 0x0F) | (_trainingGame ? 0x100 : 0));

    /* Training starts at a fixed (168, 313) in both releases. A match starts
     * centred horizontally on one of the two goal ends, and both of those are
     * functions of the SIMULATION view (display.h): 168/592 at the port's own
     * 336x272, and 176/664 — swos-port's kCenterX / kBottomStartLocationY — at
     * the DOS release's 320x200. */
    d1 = 168;
    d2 = 313;
    if (_trainingGame == 0) {
        uint16_t r = Rand();
        if (r & 1)
            d2 = SIM_CAMERA_MAX_Y;   /* bottom end: 592 at 272 lines */
        else
            d2 = 16;                 /* top end: the same in both */
        d1 = SIM_CAMERA_CENTRE_X;    /* 168 at 336 wide */
    }

    ReplayUpdateCameraAndSwapBuffers(d1, d2);
    UpdatePlanePtrsAndBlitParams();
    /* nullsub_2 — no-op */
    ReadTimerDelta();

    /* Poll keys until clear */
    do {
        GetKey();
    } while (_currentKey != 0);

    /* A SAVED POSITION, if one is armed (src/challenge.c): loaded HERE, over everything
     * the init just built, and before a single frame runs.
     *
     * After the init, not before it -- the opposite of what the netplay lobby does with a
     * kick-off state. The lobby makes both peers' PRE-init state identical so the
     * deterministic init derives the same kick-off on each; a saved position is the state
     * the init's output has to be REPLACED by. The camera is then put where the position
     * had it, because the renderer has already been pointed at the kick-off view above. */
    int16_t renderCameraX = _cameraX.s.i;
    int16_t renderCameraY = _cameraY.s.i;
    if (Challenge_LoadState()) {
        int16_t loadedCameraX = _cameraX.s.i;
        int16_t loadedCameraY = _cameraY.s.i;
        /* The camera has teleported from the kick-off to wherever the position was saved.
         * That is the jump the end of an instant replay makes, so it is undone the same
         * way (RestoreCameraPosition, highlight.c): the animated pitch tiles go back to
         * static, then both buffers are refilled at the new camera.
         *
         * Then the FADE is handed back to the loop. A position is saved mid-match, where
         * the kick-off fade has long finished, so it restores _fadeState = 1 and the
         * screen would stay black -- nothing else writes the palette. Setting it back to
         * "no fade yet" makes GameLoop_Frame start its own fade-in a few lines below,
         * through the game's own machinery.
         *
         * Writing the palette directly (ApplyFadeStep) and panning the camera by hand to
         * force a column refresh is what the first attempt did: on the Amiga it corrupted
         * the pitch, the crowd rows and the menus after the match -- the m68k display is
         * hardware-scrolled bitplanes with a copper palette, and it does not take kindly
         * to either. The SDL build redraws the whole pitch every frame, which is why it
         * never showed any of this. */
        extern void ResetAnimatedPatternsForBothTeams(void);
        ResetAnimatedPatternsForBothTeams();
        /* The portable state restores the world camera, but deliberately leaves
         * the circular-buffer scroll offsets alone. Start the repaint from the
         * camera those offsets still describe, so SetCameraCoordinates advances
         * them by the loaded position's delta on the first buffer too. Otherwise
         * only the second buffer gets that delta, and their scroll phases differ.
         * Keep the loaded fractional words: only the integer camera is rewound. */
        _cameraX.s.i = renderCameraX;
        _cameraY.s.i = renderCameraY;
        ReplayUpdateCameraAndSwapBuffers(loadedCameraX, loadedCameraY);
        UpdatePlanePtrsAndBlitParams();
        _fadeState    = 0;
        _fadeInitFlag = FADE_SENTINEL;
        loadedPosition = 1;
    }

    /* The first pass starts at the vblank wait, as the original's does -- but a loaded
     * position does not SHOW that pass. The picture in the buffer was drawn for the
     * kick-off, before the position replaced it, so presenting it flashes a frame of the
     * wrong match: the clock read the 88th minute for an instant before the real 20th.
     * The wait still happens, only the showing is skipped. */
    GameLoop_EndFrame(loadedPosition ? 0 : 1);
    for (;;) {
        GameFrameResult r = runFrame();
        if (r == GAME_FRAME_MATCH_OVER)
            goto end_of_match_flow;
        if (r == GAME_FRAME_QUIT)
            goto cleanup_and_return;
    }

end_of_match_flow:
#ifdef SWOS_SDL_BUILD
    { extern void ReplayRec_MatchEnd(void); ReplayRec_MatchEnd(); }
#endif
    FadeOutToBlack();
    /* nullsub_3 — no-op */
    SetHilFileHeader();
    AudioFadeCrowdDownThenPulseMusic();
    ClearAudioActiveFlag();

    int arcadeNoReplayMenu = 0;
#ifdef SWOS_SDL_BUILD
    /* Arcade returns straight to attract after the match — skip the friendly
     * "watch replay / exit" menu (it would block on cabinet input). */
    { extern int _gArcadeMode; arcadeNoReplayMenu = _gArcadeMode; }
    /* So does an online match. Its REPLAY would kick off a second match on this
     * machine alone, with no session and nobody at the other end; playing again is
     * the lobby's START, where both players agree to it (docs/NETPLAY.md). */
    {
        extern int32_t NetplayMatch_LiveOutcome(const char **why);
        if (NetplayMatch_LiveOutcome(NULL) != 0)
            arcadeNoReplayMenu = 1;
    }
#endif
    if (_isGameFriendly != 0 && _trainingGame == 0 && !arcadeNoReplayMenu) {
        ReplayExitMenuAfterFriendly();
        if (_replaySelected != 0) {
            _team1NumAllowedInjuries = 4;
            _team2NumAllowedInjuries = 4;
            _playGame = 1;
            /* This path never re-enters InitializeInGameTeamsAndStartGame, so
             * PENALTY SHOOTOUT's one-shot must be re-armed here or the replay
             * of a shootout plays as a plain friendly (penalty_shootout.c). */
            { extern void PenaltyShootout_MatchRestarted(void); PenaltyShootout_MatchRestarted(); }
            EnsureSfxLoadedAndStart();
            SetPitchTypeAndNumber();
            InitGameRestoreTeams();
            GameLoop();
            return;
        }
    }

cleanup_and_return:
#ifdef SWOS_SDL_BUILD
    { extern void ReplayRec_MatchEnd(void); ReplayRec_MatchEnd(); }   /* quit mid-match: keep it */
#endif
    StopAudioAndClearFlags();
    return;
}

/* ====================================================================
 * MatchViewEnter / MatchViewLeave — the DOS view is per scene
 *
 * The DOS release draws 320x200 and so must a DOS-accurate match. The menus
 * cannot: they are Amiga artwork, 272 lines tall, and a 200-line view cuts
 * their bottom rows off (which is what it did when the view was set once at
 * startup). So the view changes at kick-off and changes back after.
 *
 * The SIMULATION view moves with it, because it must: the onScreen cull reads
 * the view size and decides which players auto-selection may pick, so drawing
 * 200 lines while the simulation believed 272 would be a third thing that
 * matches neither original (GAMEPLAY.md §25.4, docs/PALETTE_V2.md's sibling
 * argument). Every camera constant is f(view) and lands on the DOS numbers at
 * 200: SIM_CAMERA_MAX_Y = 664, which is swos.asm:7539.
 * ==================================================================== */
#ifdef SWOS_SDL_BUILD
extern void Platform_ViewChanged(void);

static int s_viewSavedW, s_viewSavedH;

void MatchViewEnter(void)
{
    extern int _gDosView;
    if (!_gDosView) return;
    s_viewSavedW = _screen_width;      /* whatever the menus are using, including
                                        * an adaptive widescreen width */
    s_viewSavedH = _screen_height;
    { extern int _dos_view_width; _screen_width = _dos_view_width; }
    { extern int _dos_view_height; _screen_height = _dos_view_height; }
    _sim_view_width  = DOS_VIEW_WIDTH;   /* pinned: the game is DOS at any width */
    _sim_view_height = DOS_VIEW_HEIGHT;
    Platform_ViewChanged();
}

void MatchViewLeave(void)
{
    extern int _gDosView;
    if (!_gDosView) return;
    if (!s_viewSavedW) return;         /* never entered */
    _screen_width    = s_viewSavedW;
    _screen_height   = s_viewSavedH;
    _sim_view_width  = SCREEN_WIDTH;
    _sim_view_height = SCREEN_HEIGHT;
    Platform_ViewChanged();
}
#else
#define MatchViewEnter()  ((void)0)
#define MatchViewLeave()  ((void)0)
#endif

/* ====================================================================
 * StartMatchSequence — sets up and starts a match
 * ==================================================================== */
void __attribute__((used, externally_visible))
StartMatchSequence(TeamGame *topTeam, TeamGame *bottomTeam)
{
    LoadSfxResources();
    _playGame = 1;
    EnsureSfxLoadedAndStart();
    SetPitchTypeAndNumber();
    InitGameSaveTeams(topTeam, bottomTeam);
    /* AFTER the fade, not before: InitGameSaveTeams fades the pre-match team
     * line-up out, and that line-up is menu-sized. Switching the view while it is
     * still on screen resizes the window and re-letterboxes it mid-fade. The
     * screen is black here, so the change is invisible -- the same rule the
     * palette learned on this exact transition. */
    MatchViewEnter();
    /* NO palette push here.
     *
     * InitGameSaveTeams has just faded the screen to black, and nothing has
     * drawn the pitch yet — the frontbuffer still holds the pre-match team
     * line-up screen. A WriteCopperPitchPalette() here (added with AGA mode,
     * 1c48b69) wrote the full-brightness gamePalette straight into the copper,
     * which un-blacks the display and shows that stale menu image in the
     * PITCH palette for the few frames until GameLoop draws and fades in:
     * colour 0 goes dark-blue -> green and the whole upper 240 entries change,
     * so the menu backdrop comes back as concentric rings of green/red/blue.
     * Reported as "right before the match engine starts, the colours get
     * distorted for a split second".
     *
     * Nothing is lost by dropping it. The 9 AGA pitch-condition entries are
     * applied to gamePalette by InitGameRestoreTeams below (match_setup.c,
     * apply_palette_and_pitch), and the copper gets the palette from the fade
     * itself: paletteIndex is already 2 and SetFadeLevel writes
     * paletteTable[2] (= gamePalette) scaled by the fade level, including the
     * AGA colour 0-15 -> 16-31 sprite mirror. GameLoop kicks that fade off on
     * the first frame after the pitch is drawn (fadeState == 0 &&
     * fadeInitFlag == FADE_SENTINEL). This is also exactly what the friendly
     * "watch the replay" re-entry above already does, and what the asm does
     * (SWOS2.s:20591 StartMatchSequence pushes no palette). */
    InitGameRestoreTeams();
    /* Wipe the bitplane frontbuffers before the first pitch draw so leftover
     * menu/intro high-plane (4-7) data can't bleed onto the white pitch lines.
     * No-op on SDL / OCS (see ClearFrontbuffers). */
    ClearFrontbuffers();
    GameLoop();
    MatchViewLeave();
}

/* ====================================================================
 * ViewMatchSequence — entry point from menu system
 * ==================================================================== */
void __attribute__((used, externally_visible))
ViewMatchSequence(void)
{
    StartMatchSequence(&_topTeamInGame, &_bottomTeamInGame);
}

/* ====================================================================
 * ViewHighlights — entry point for highlight viewing
 * ==================================================================== */
void __attribute__((used, externally_visible))
ViewHighlights(void)
{
    ViewHighLightsMain(&_topTeamInGame, &_bottomTeamInGame);
}

/* ====================================================================
 * KeepStatisticsOnScreen — spin-loop while stats overlay is showing
 * ==================================================================== */
void __attribute__((used, externally_visible))
KeepStatisticsOnScreen(void)
{
    do {
        pauseSpinWaitOneTick();
        GetKey();
        MainKeysCheck(_currentKey);
        /* nullsub_10 — no-op */
        PlayEnqueuedSamples();
        /* nullsub_5, nullsub_4 — no-ops */
        UpdateAndDrawStatistics();
        /* With input a frame at a time (_gFrameStepInput) nothing can close the overlay
         * before the next frame: one pass, and GameLoop_Frame ends the frame. Every
         * recording that opens it closes it on the first pass or before the loop (zkz-12065730
         * frame 1240, zkz-12080203 frame 3040), so for them this is the loop exactly; a netplay peer holding only a prediction
         * of the input that closes it would otherwise spin here for ever. */
    } while (_showingUserStats != 0 && !_gFrameStepInput);
}

/* ====================================================================
 * ViewHighLightsMain — highlights viewer
 * ==================================================================== */
void __attribute__((used, externally_visible))
ViewHighLightsMain(TeamGame *leftTeam, TeamGame *rightTeam)
{
    CopyTeamsAndHeader();
    InitGameSaveTeams(leftTeam, rightTeam);
    /* The recorded pitch's colours. In SWOS2.s InitGameSaveTeams ends in `bra.w *+4`
     * and FALLS THROUGH into InitGameRestoreTeams, whose highlight test skips nothing,
     * so the original viewer ran all of it -- apply_palette_and_pitch_tables included.
     * The C InitGameSaveTeams returns, and the viewer never called the rest: every
     * replay drew its grass in whatever pitch colours were left over (the recordings
     * player, which skips the front end: a dry-pitch match in muddy brown, and its
     * shadows looking wrong on it). Not the whole of InitGameRestoreTeams: the rest
     * works on the MATCH's team files (_gameTeam1/2, ApplyTeamTactics), which a viewer
     * has none of -- the original ran it on whatever the last match left there, and
     * here that is NULL. */
    ApplyPitchPaletteAndFactors();
    /* The DOS view, on the same seam StartMatchSequence uses: AFTER
     * InitGameSaveTeams has faded out, because the stadium "VS" line-up above is
     * menu-sized and resizing while it is on screen re-letterboxes it mid-fade.
     * Without this the viewer played a DOS recording in the Amiga's 336x272 --
     * reported as "the pitch view is not in DOS mode anymore, and the score is
     * centered at the top" (ReportBot 0008), the score being the HUD at its
     * Amiga coordinates. The camera constants just below are already f(view)
     * and only land on the DOS numbers once the view is set.
     * MatchViewLeave is in viewAndRestoreMenu, so every exit path is covered.
     * A .SRP is shown in the view ITS match was played in, whatever this player's is. */
#ifdef SWOS_SDL_BUILD
    { extern void ReplayV2_UseRecordedView(void); ReplayV2_UseRecordedView(); }
#endif
    MatchViewEnter();
#ifdef SWOS_SDL_BUILD
    { extern void ReplayV2_ApplyRecordedSimView(void); ReplayV2_ApplyRecordedSimView(); }
#endif
    /* Set the highlight flag AFTER InitGameSaveTeams' fade-out. The flag is what
     * flips refresh_palette_from_copper to the DOS pitch palette; setting it
     * earlier made the (Amiga-graphics) stadium "VS" line-up fade out through DOS
     * colours. The DOS palette is only correct once the pitch is on screen, which
     * begins with the swap below. */
    _playHighlightFlag = 1;
#ifdef SWOS_SDL_BUILD
    { extern void Replay_ResetBallCamera(void); Replay_ResetBallCamera(); }
#endif
    /* Both are f(view), not literals — see display.h. 168/313 at the port's
     * own 336x272, 176/349 at the DOS release's 320x200. */
    ReplayUpdateCameraAndSwapBuffers(SIM_CAMERA_CENTRE_X, SIM_CAMERA_BENCH_Y);
    UpdatePlanePtrsAndBlitParams();
    ReadTimerDelta();
#ifdef SWOS_SDL_BUILD
    /* Load + start the SFX bank so the replay isn't silent. Unlike the live-match
     * path (StartMatchSequence), the non-arcade highlights/replay viewer never
     * loaded it, so RPL/HIL playback had no crowd ambience or recorded SFX. The
     * arcade reels manage their own audio (HERO music vs crowd-SFX) in
     * arc_music_attract, so leave _gArcadeMode alone. */
    { extern int _gArcadeMode;
      extern void LoadSfxResources(void);
      extern void EnsureSfxLoadedAndStart(void);
      extern void TurnCrowdChantsOn(void);
      extern int32_t ReplayV2_HasAudio(void);
      extern void CrowdResetMatchState(void);
      if (!_gArcadeMode) { LoadSfxResources(); EnsureSfxLoadedAndStart(); }
      /* A .SRP brings the match's own sounds, crowd chants included (src/replay_v2.c):
       * the random chants would play over them. Leftover sample countdowns from the last
       * match (a goal cheer, a whistle) would too, so reset those as a match does. */
      if (ReplayV2_HasAudio()) {
          extern uint16_t _crowdChantsOn;
          CrowdResetMatchState();
          _crowdChantsOn = 0;
      } else {
          /* The RPL/HIL stream carries no 0x89 crowd-chant marker, so PlayCrowdChants
           * (ticked every frame by PlayEnqueuedSamples) stays disabled and the replay
           * is silent. A live match also resets the crowd state via CrowdResetMatchState
           * (match setup), which a fresh-loaded replay never runs — leaving
           * _currentCrowdSample at the BSS default 0, so PlayCrowdChants' "keep current
           * sample" path fires wave 0 (= channel-off) and the chant is silent until it
           * randomly re-picks. Reset it to 0xFFFF so the first chant picks a real sample,
           * then enable. Gated downstream by _gArcadeSfxSuppressed. */
          extern uint16_t _currentCrowdSample;
          _currentCrowdSample = 0xFFFF;
          TurnCrowdChantsOn();
      } }
#endif

    /* Drain the keyBuffer — original Amiga waits here while the user
     * holds the fire button they just pressed (keyboard ISR pushes
     * auto-repeats so currentKey stays non-zero). On Amiga this also
     * gave the stadium "VS" screen its visible duration. On SDL the
     * actual VS-duration wait happens just upstream in
     * InitGameSaveTeams' "wait for fire release" loop (which is
     * properly pumped via WaitForVSync there), so by the time we
     * reach this drain the user has already released and the buffer
     * is empty. Drain anyway to match Amiga semantics. */
    do {
        GetKey();
    } while (_currentKey != 0);

    SaveHighlightsPointers();
    goto sync_to_vblank;

frame_loop:
    ReadTimerDelta();
    _frameCount += 1;
    ProcessSpriteBlitQueue();
    GetKey();
    MainKeysCheck(_currentKey);
    /* nullsub_10, nullsub_9 — no-ops */

    if (_playHighlightFlag == 0) {
        FadeOutToBlack();
        return;
    }

    if (_goIntoHighlightsLoop != 0) {
        PlayHighlightsLoop();
        _playHighlightFlag = 0;
        /* bra.w *+4 → falls through to sync */
    }

sync_to_vblank:
    waitVBlankSync(0xCFD1F96Bu, 0x168Cu);
    goto frame_loop;
}

/* ====================================================================
 * PlayInstantReplayLoop — instant replay playback
 * ==================================================================== */
void __attribute__((used, externally_visible))
PlayInstantReplayLoop(void)
{
    ReadTimerDelta();

    for (;;) {
        /* Paused poll loop */
        while (_paused != 0) {
            pauseSpinWaitOneTick();
            GetKey();
            if (_fadeState == 0)
                break;
            MainKeysCheck(_currentKey);
            PlayEnqueuedSamples();
            /* nullsub_5, nullsub_4 — no-ops */
        }

        /* Input and update block */
        ProcessSpriteBlitQueue();
        GetKey();

        if (_fadeState != 0) {
            MainKeysCheck(_currentKey);
            /* nullsub_10, nullsub_9 — no-ops */
        }

        /* Check replay continue */
        if (_replayState == 0)
            return;

        PlayEnqueuedSamples();

        /* Fire-to-abort check */
        if (_fadeState != 0) {
            if (_userRequestedReplay != 0) {
                /* Multiplayer: check both joysticks */
                Player1StatusProc();
                if (_pl1Fire != 0)
                    goto abort_replay;
                Player2StatusProc();
                if (_pl2Fire != 0)
                    goto abort_replay;
            } else {
                if (_teamScoredDataPtr != 0) {
                    uint16_t playerNum = (uint16_t)_teamScoredDataPtr->playerNumber;
                    uint16_t coachNum  = (uint16_t)_teamScoredDataPtr->playerCoachNumber;
                    if (playerNum == 1) {
                        Player1StatusProc();
                        if (_pl1Fire != 0) goto abort_replay;
                    } else if (playerNum == 2) {
                        Player2StatusProc();
                        if (_pl2Fire != 0) goto abort_replay;
                    } else if (coachNum == 1) {
                        Player1StatusProc();
                        if (_pl1Fire != 0) goto abort_replay;
                    } else if (coachNum == 2) {
                        Player2StatusProc();
                        if (_pl2Fire != 0) goto abort_replay;
                    } else {
                        /* Fall through to multiplayer check */
                        Player1StatusProc();
                        if (_pl1Fire != 0) goto abort_replay;
                        Player2StatusProc();
                        if (_pl2Fire != 0) goto abort_replay;
                    }
                }
            }
        }

        /* Draw replay frame and handle fade */
        DrawReplayFrame();
#ifdef SWOS_SDL_BUILD
        { extern int _gArcadeMode; extern int Arcade_DrawAttractOverlay(void);
          if (_gArcadeMode && Arcade_DrawAttractOverlay()) goto abort_replay; }
#endif
        if (_fadeState == 0 && _fadeInitFlag == 0x3E7)
            _fadeInitFlag = 0xFFFF;

        if (_replayState == 0) {
            /* Fade out and cleanup */
            FadeOutToBlack();
            RestoreCameraPosition();
            return;
        }

        /* Handle post-frame flags */
        if (_fadeAndSaveHighlightScene != 0) {
            FadeAndSaveHighlightScene();
            _fadeAndSaveHighlightScene = 0;
        }
        if (_instantReplayFlag != 0) {
            HandleInstantReplayStateSwitch();
            _instantReplayFlag = 0;
        }

        /* VBlank sync and loop back */
        waitVBlankSync(0xCFD1F959u, 0x16BEu);
        ReadTimerDelta();
        continue;

    abort_replay:
        _fireBlocked = 1;
        _replayState = 0;
        FadeOutToBlack();
        RestoreCameraPosition();
        return;
    }
}

/* ====================================================================
 * PlayHighlightsLoop — multi-scene highlight playback
 * ==================================================================== */
void __attribute__((used, externally_visible))
PlayHighlightsLoop(void)
{
    ReadTimerDelta();

    for (;;) {
#ifdef SWOS_SDL_BUILD
        /* Window-close (X) during replay: the SDL event pump (DrawReplayFrame's
         * vblank) sets _quitToDos, but this loop never polled it, so the process
         * kept running after the window closed. Exit cleanly via the normal stop
         * path — the recursion unwinds and the caller propagates the quit. */
        if (_quitToDos != 0)
            goto stop_replay_and_return;
#endif
        /* Paused poll loop */
        while (_paused != 0
#ifdef SWOS_SDL_BUILD
               && _quitToDos == 0
#endif
              ) {
            pauseSpinWaitOneTick();
            GetKey();
            MainKeysCheck(_currentKey);
            PlayEnqueuedSamples();
        }

        /* Input and update block */
        ProcessSpriteBlitQueue();
        GetKey();
        MainKeysCheck(_currentKey);
        /* nullsub_10, nullsub_9 — no-ops */

        if (_replayState == 0)
            goto advance_to_next_highlight;

        PlayEnqueuedSamples();

        /* Fire-to-abort check */
        if (_fadeState != 0) {
            Player1StatusProc();
            if (_pl1Fire != 0)
                goto abort_replay_on_fire;
            Player2StatusProc();
            if (_pl2Fire != 0)
                goto abort_replay_on_fire;
        }

        /* Draw replay and handle fade */
        DrawReplayFrame();
#ifdef SWOS_SDL_BUILD
        { extern int _gArcadeMode; extern int Arcade_DrawAttractOverlay(void);
          if (_gArcadeMode && Arcade_DrawAttractOverlay()) goto abort_replay_on_fire; }
#endif
        if (_fadeState == 0 && _fadeInitFlag == 0x3E7)
            _fadeInitFlag = 0xFFFF;

        if (_replayState == 0) {
            FadeOutToBlack();
            /* Restore the pre-replay camera only once the whole reel is done.
             * Between scenes, RestoreCameraPosition snaps the camera to the
             * pre-replay default (≈ pitch centre) and swaps it onscreen for a
             * frame before the next scene's first marker re-aims it — that's
             * the "jump to centre" glitch. The next scene's DrawReplayFrame
             * re-inits the camera + patterns, so skipping it between scenes is
             * safe; only the final scene needs the real restore. */
            if ((uint16_t)(_hilSceneNumber + 1) >= _numScenesInHilFile)
                RestoreCameraPosition();
            goto advance_to_next_highlight;
        }

        /* VBlank sync */
        waitVBlankSync(0xCFD1F917u, 0x16F0u);
        ReadTimerDelta();
        continue;

    abort_replay_on_fire:
        _fireBlocked = 1;
        _replayState = 0;
        FadeOutToBlack();
        RestoreCameraPosition();
        goto stop_replay_and_return;
    }

advance_to_next_highlight:
    _hilSceneNumber += 1;
    if (_numScenesInHilFile == _hilSceneNumber)
        goto stop_replay_and_return;
#ifdef SWOS_SDL_BUILD
    /* A .SRP is one scene with its own frame stream (src/replay_v2.c). */
    {
        extern int32_t ReplayV2_Loaded(void);
        extern void    ReplayV2_StartScene(void);
        if (ReplayV2_Loaded()) {
            ReplayV2_StartScene();
            waitVBlankSync(0xCFD1F917u, 0x16F0u);
            ReadTimerDelta();
            PlayHighlightsLoop();
            return;
        }
    }
#endif

    {
        uint32_t offset = (uint32_t)_hilSceneNumber * 19000u;
        /* `hilStartOfGoalsBuffer + offset` is a pointer (8 bytes on 64-bit).
         * Storing into uint32_t truncated the upper 32 bits, then the
         * `(uint8_t *)baseAddr` re-cast produced a half-pointer that
         * SIGSEGVed in HandleInstantReplayStateSwitch's `*p & 0x80`.
         * Carry the value as a real pointer instead. */
        uint8_t *baseAddr = _hilStartOfGoalsBuffer + offset;
        uint8_t *endAddr  = baseAddr + 19000u;
#ifdef SWOS_SDL_BUILD
        /* An RPL is a whole match in one ~1 MB scene that can't be addressed by
         * the 19000-B scene math — dos_replay_load.c loaded it into a dedicated
         * buffer; stream that as the single scene. */
        extern uint8_t *_rplBufferPtr;
        extern uint32_t _rplStreamBytes;
        if (_rplBufferPtr) {
            baseAddr = _rplBufferPtr;
            endAddr  = _rplBufferPtr + _rplStreamBytes;
        }
        /* A SWOS 2020 .HIL's scenes are 61000 bytes, not 19000: dos_replay_load.c
         * keeps them in its own buffer at the file's stride. */
        extern uint8_t *_dosHilScenesPtr;
        extern uint32_t _dosHilSceneBytes;
        if (_dosHilScenesPtr) {
            baseAddr = _dosHilScenesPtr + (uint32_t)_hilSceneNumber * _dosHilSceneBytes;
            endAddr  = baseAddr + _dosHilSceneBytes;
        }
#endif
        _goalBasePtr = baseAddr;
        _currentHilPtr = baseAddr - 4;
        _nextGoalPtr = endAddr;
    }
    HandleInstantReplayStateSwitch();

    /* VBlank sync and restart loop */
    waitVBlankSync(0xCFD1F917u, 0x16F0u);
    ReadTimerDelta();
    PlayHighlightsLoop();
    return;

stop_replay_and_return:
    RestoreHighlightPointers();
    return;
}
