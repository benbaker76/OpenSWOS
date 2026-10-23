/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/*
 * highlight.c — Highlight/Replay save system
 * Ported from SWOS2.s (Chunk 37)
 */

#include "highlight.h"
#include "game_types.h"
#include "deterministic.h"
#include "panic.h"
#include "endian_helpers.h"
#include "display.h"    /* WIDE_MARGIN — centre the replay overlay in widescreen (0 on Amiga) */
#include "dos_mode.h"   /* _gDosView — the overlay's DOS positions */
#include "challenge.h" /* a saved position pins the pitch it was saved on */

/* ---- Sprite constants ---- */
#define SPR_REPLAY_FRAME_00  1095
#define SPR_BIG_0            173
#define SPR_BIG_0_ALT        183   /* IDA: SPR_SCROLL_ARROW_UP — actually right-team big digits */

/* ---- Hil buffer size per scene ---- */
#define HIL_SCENE_LONGS   4750    /* 4750 x 4 = 19000 bytes */
#define HIL_SCENE_BYTES   19000
#define HIL_MAX_SCENES    10
#define K_HIL_FILE_SIG    0x0000000Cu
#define K_HIL_FILE_MAXSZ  192254u

/* ---- External globals ---- */
extern volatile uint16_t _numScenesInHilFile;
extern uint16_t _gamePitchTypeOrSeason;
extern int16_t  _gamePitchType;
extern uint16_t _gameSeason;
extern uint16_t _currentGameTick;
extern uint16_t _plgD0Param;
extern TeamGame _topTeamInGame;
extern TeamGame _bottomTeamInGame;
extern uint16_t _trainingGame;
extern uint16_t _pitchNumberAndType;
extern uint16_t _hilNumGoals;
extern uint16_t _hilPitchNumber;
extern uint8_t  _hilFileBuffer[];
extern uint8_t  _gameName[];
extern uint8_t  _gameRound[];
extern uint8_t  _hilGameName[];
extern uint8_t  _hilGameRound[];
extern uint16_t _hilTeam1Goals;
extern uint16_t _hilTeam2Goals;
extern uint16_t _statsTeam1Goals;
extern uint16_t _statsTeam2Goals;
extern uint16_t _gameMaxSubstitutes;
extern uint16_t _hilMaxSubstitutes;
extern uint16_t _normalReplayRunning;
extern uint8_t *_goalBasePtr;
extern uint8_t *_nextGoalPtr;
extern uint8_t *_currentHilPtr;
extern uint8_t *_currentGoalPtr;
extern volatile uint16_t _goIntoHighlightsLoop;
extern uint16_t _hilSceneNumber;
extern uintptr_t _savedCurrentHilPtr;
extern uintptr_t _savedCurrentGoalPtr;
extern uintptr_t _savedGoalBasePtr;
extern uintptr_t _savedNextGoalPtr;
extern uint8_t  _hilStartOfGoalsBuffer[];
extern uint8_t  _inGameHilBuffer[];
extern uint16_t _throwInDeadVar;
extern uint16_t _team1TotalGoals;
extern uint16_t _team2TotalGoals;
extern uint16_t _animPatternsState;
extern uint16_t _replayState;
extern FixedPoint _cameraX;
extern FixedPoint _cameraY;
extern int16_t  _savedCameraX;
extern int16_t  _savedCameraY;
extern uint16_t _hilLeftTeamGoals;
extern uint16_t _hilRightTeamGoals;
extern uint16_t _paused;

/* ---- External functions ---- */
extern uint16_t Rand(void);
extern void FadeOutToBlack(void);
extern void FadeIn(void);
extern void SetCameraCoordinates(int16_t targetX, int16_t targetY);
extern void InitCameraMoveView(int16_t targetX, int16_t targetY);
extern void SwapBuffersAndCameraViewState(void);
extern void MoveView(int16_t newScreenX, int16_t newScreenY);
extern void DarkenRectangle(uint16_t sprIdx, uint16_t x, uint16_t y);
extern void ResetAnimatedPatternsForBothTeams(void);
extern void DrawAnimatedPatterns(void);
extern uint16_t _panicReasonCode;

/* The camera the CURRENT frame was recorded with, in render space (game_loop.c
 * writes Render_CameraX()). Recorded sprite coordinates are screen-space against
 * it, so `recorded + this` is the sprite's WORLD position -- which is all the
 * decode needs. There is deliberately no "shift" any more: the decode rebuilds
 * the world position and re-projects it through the live playback camera, the
 * same expression live drawing uses, so the two cannot drift apart however the
 * camera mapping changes. Four regressions came from keeping a second, parallel
 * way to get from a recording to the screen (ReportBot 0006 and the three cited
 * in ReplayCameraX below). */
static int16_t s_replayRecCamY = 0;

/* Goal-overlay anchor guard state — see the check in DrawReplayFrame. Both goals
 * are pinned at world x = 300 (sprite_data.c), so a change means the recording
 * and the playback disagree about which camera space they are in. 0 = not seen
 * yet; a real anchor is never 0. `Replay_GoalAnchorDrift()` lets a test assert
 * on it without parsing output. */
#define REPLAY_GOAL_TOP_SPR     1091
#define REPLAY_GOAL_BOTTOM_SPR  1092
static int32_t s_goalAnchorX[2]      = { 0, 0 };
static uint8_t s_goalAnchorWarned[2] = { 0, 0 };
static int32_t s_goalAnchorDrift     = 0;

int32_t Replay_GoalAnchorDrift(void) { return s_goalAnchorDrift; }
void    Replay_ResetGoalAnchor(void)
{
    s_goalAnchorX[0] = s_goalAnchorX[1] = 0;
    s_goalAnchorWarned[0] = s_goalAnchorWarned[1] = 0;
    s_goalAnchorDrift = 0;
}

/* --- Ball-tracked replay camera ------------------------------------------
 * A wide (e.g. SWOS-2020) recording's camera barely scrolls — the wide view
 * already showed the ball — so at a narrower playback width the recorded camera
 * can't follow the ball. With REPLAY_BALL_TRACK_CAMERA on, ReplayCameraX instead
 * centres the BALL's own recorded world position, so ANY recording follows
 * tightly at ANY resolution. Set to 0 to play the recorded camera verbatim
 * (just re-centred). The ball's world X is reconstructed in the sprite decode
 * from the recorded camera + the ball sprite's recorded screen X. */
#define REPLAY_BALL_TRACK_CAMERA 1
/* The in-game ball graphic is height-dependent (it changes sprite as the ball
 * rises/falls), but it's always drawn above a CONSTANT ground SHADOW
 * (SPR_BALL_SHADOW = 1069) at the ball's ground X — present every frame. So we
 * track the shadow's recorded X for the camera. */
#define REPLAY_BALL_SHADOW_SPR 1069
/* The replay camera mirrors the LIVE game camera (MoveCamera): a velocity that
 * accelerates toward the ball's movement so the camera LEADS the ball, then the
 * camera eases a quarter of the remaining gap each frame (>>2), capped at
 * MAX_CAMERA_MOVEMENT. Same constants/maths as move_camera.c, driven by the
 * ball position we reconstruct from the recorded shadow sprite. */
#define REPLAY_CAM_VEL_ACCEL     2           /* camera lead accel per frame */
#define REPLAY_CAM_VEL_MAX       40          /* camera lead velocity cap (px) */
#define REPLAY_CAM_BOUNDS_MARGIN 63          /* open-play target clamp margin (MoveCamera d1=$3F) */
#define REPLAY_CAM_MAX_MOVE      0x00050000L /* 5.0 px/frame, 16.16 (== MAX_CAMERA_MOVEMENT) */
static int16_t s_replayRecCamX    = 0;  /* recorded camera X of the current frame */
static int     s_replayBallWorldX = 0;  /* ball world X = recorded cam + recorded sprite X */
static int     s_replayBallValid  = 0;  /* 1 once a ball sprite has been seen this replay */
static int32_t s_replayCamXraw    = 0;  /* eased camera X, 16.16 fixed point (mirrors _cameraX.raw) */
static int     s_replayCamVelX    = 0;  /* camera lead velocity (mirrors _cameraXVelocity) */
static int     s_replayBallPrevX  = 0;  /* previous ball world X, for velocity reconstruction */
static int     s_replayCamXSet    = 0;  /* 0 -> snap + seed on first frame; 1 -> live follow */

/* Reset ball-tracking at the start of a replay so a previous replay's ball
 * position / velocity doesn't aim the first frame. Called from ViewHighLightsMain. */
__attribute__((used, externally_visible))
void Replay_ResetBallCamera(void) { s_replayBallValid = 0; s_replayCamXSet = 0; s_replayCamVelX = 0; }

/* ---- Replay camera X (SDL widescreen) ----------------------------------
 * Recorded HIL/replay camera positions are in the classic 336-wide coordinate
 * space (scrolled to keep the ball centred in a 336px view). The pitch is 0x2A0
 * (672) wide, so a view of width W has only (672 - W) px of camera travel.
 *   - Near-full-pitch view (ultrawide, tiny pan range): following the recorded
 *     path only SLIDES the static scenery (net, stands) by the pan-range width
 *     — there's nothing off-screen to follow to. Lock to the pan-range centre:
 *     dead-stable, pitch-centred.
 *   - Narrower widescreen: the camera still needs to track play, so re-centre
 *     the recorded 336-space path for the view (the per-mode adjustment SWOS
 *     2020 applies: subtract WIDE_MARGIN = (_screen_width - SCREEN_WIDTH)/2) and
 *     clamp to the pan range.
 * No-op on Amiga / 4:3 (pan range == the classic range, WIDE_MARGIN == 0). */
/* The replay camera is computed in RENDER space — `ballX - _screen_width/2`,
 * clamped to `0x2A0 - _screen_width`. That tuning is what makes a recording play
 * back correctly at any resolution, and it stays exactly as it was; what is gone
 * is the re-anchor that used to sit beside it (the decode rebuilds world
 * positions instead, see s_replayRecCamY).
 *
 * What changed underneath it (2b67c6f) is that `_cameraX` is now the
 * SIMULATION camera and the renderer derives its own origin,
 * `Render_CameraX() = clamp(_cameraX - WIDE_MARGIN, 0, 0x2A0 - _screen_width)`.
 * Handing a render-space value straight to the camera therefore had the margin
 * taken off a second time, and the whole scene — goal overlay included — drew
 * WIDE_MARGIN too far left in attract mode and in any widescreen HIL playback.
 *
 * So convert: hand the camera a simulation X that Render_CameraX() maps back to
 * exactly the render origin this function computed. Folds to identity at 4:3
 * and on Amiga, where WIDE_MARGIN is 0. */
static int16_t ReplaySimCameraX(int16_t outRender)
{
#ifdef SWOS_SDL_BUILD
    /* Exact inverse of Render_CameraX(), so the round trip still lands back on
     * the render origin this function computed — the property the comment above
     * depends on. It used to be `+ WIDE_MARGIN` because the forward map was
     * `- WIDE_MARGIN` then clamp; the forward map now SCALES the sim range onto
     * the render's pan range, so the inverse scales back. Folds to identity at
     * 336 (max == pan), which is what the corpus and the Amiga run at. */
    const int32_t pan = 0x2A0 - SCREEN_WIDTH;
    int32_t max = (int32_t)(0x2A0 - _screen_width);
    int32_t simX;

    if (max <= 0)        simX = 0;
    else if (max >= pan) simX = outRender;
    else                 simX = ((int32_t)outRender * pan + max / 2) / max;

    if (simX < 0) simX = 0;
    else if (simX > pan) simX = pan;
    return (int16_t)simX;
#else
    return outRender;
#endif
}

static int16_t ReplayCameraX(int16_t recordedX)
{
#ifdef SWOS_SDL_BUILD
    extern uint16_t _playHighlightFlag;
    s_replayRecCamX = recordedX;   /* the sprite decode reads this to reconstruct the ball's world X */
    if (_playHighlightFlag == 0) {
        /* In-game instant replay: play the recorded camera path back as it was.
         *
         * This used to return `recordedX` verbatim and was RIGHT to, because
         * game_loop.c stored `_cameraX.s.i` — already a simulation X — and
         * 71ce072 broke it by converting. That is no longer what is stored: the
         * recorder now writes Render_CameraX(), a RENDER origin, so the
         * conversion is exactly what belongs here, the same as the two returns
         * below. It is not a second WIDE_MARGIN either way; the decode no longer
         * re-anchors anything, it re-projects world positions. */
        return ReplaySimCameraX(recordedX);
    }
    /* The DosReplay loader flags a SWOS-2020 widescreen recording (sprites span
     * wider than the 336 DOS/Amiga view). For those the recorded camera already
     * sits in a wide view, so it's used verbatim; only narrow 336 recordings get
     * the WIDE_MARGIN centring. The loader owns the format knowledge — this
     * helper just applies the bit. */
    extern int _replayRecordingIsWide;
    int16_t panRange = (int16_t)(0x2A0 - _screen_width);
    if (panRange < 0) panRange = 0;
    int16_t out;
#if REPLAY_BALL_TRACK_CAMERA
    if (s_replayBallValid) {
        /* Mirror MoveCamera's normal-play follow, driven by the ball position we
         * reconstruct from the recorded shadow. The camera LEADS the ball via a
         * velocity that accelerates toward the ball's movement, then eases 1/4 of
         * the remaining gap per frame, capped at MAX_CAMERA_MOVEMENT — identical
         * feel to the live game (and a ball teleport simply glides via that cap). */
        int ballX = s_replayBallWorldX;
        if (!s_replayCamXSet) {
            /* First frame: snap to the ball, seed the velocity reconstruction. */
            int target = ballX - _screen_width / 2;
            if (target < 0) target = 0; else if (target > panRange) target = panRange;
            s_replayCamXraw   = (int32_t)target << 16;
            s_replayBallPrevX = ballX;
            s_replayCamVelX   = 0;
            s_replayCamXSet   = 1;
            out = (int16_t)target;
        } else {
            int ballDeltaX = ballX - s_replayBallPrevX;
            s_replayBallPrevX = ballX;
            /* Accelerate the lead velocity toward the ball's direction (clamped). */
            if (ballDeltaX > 0)      { if (s_replayCamVelX !=  REPLAY_CAM_VEL_MAX) s_replayCamVelX += REPLAY_CAM_VEL_ACCEL; }
            else if (ballDeltaX < 0) { if (s_replayCamVelX != -REPLAY_CAM_VEL_MAX) s_replayCamVelX -= REPLAY_CAM_VEL_ACCEL; }
            /* Target = ball centred + lead, clamped to the open-play bounds. */
            int target = ballX - _screen_width / 2 + s_replayCamVelX;
            int xMax = panRange - REPLAY_CAM_BOUNDS_MARGIN;
            if (xMax <= REPLAY_CAM_BOUNDS_MARGIN) target = panRange / 2;
            else if (target < REPLAY_CAM_BOUNDS_MARGIN) target = REPLAY_CAM_BOUNDS_MARGIN;
            else if (target > xMax) target = xMax;
            /* Ease 1/4 of the gap, capped at MAX_CAMERA_MOVEMENT (same maths as
             * MoveCamera: scaledDx = dx>>2; fixedDx = 0x4000 * scaledDx). */
            int camInt = (int)(s_replayCamXraw >> 16);
            int32_t step = (int32_t)0x4000 * (int32_t)((target - camInt) >> 2);
            if (step >  REPLAY_CAM_MAX_MOVE) step =  REPLAY_CAM_MAX_MOVE;
            if (step < -REPLAY_CAM_MAX_MOVE) step = -REPLAY_CAM_MAX_MOVE;
            s_replayCamXraw += step;
            camInt = (int)(s_replayCamXraw >> 16);
            if (camInt < 0)             { camInt = 0;        s_replayCamXraw = 0; }
            else if (camInt > panRange) { camInt = panRange; s_replayCamXraw = (int32_t)panRange << 16; }
            out = (int16_t)camInt;
        }
        return ReplaySimCameraX(out);
    }
#endif
    if (_replayRecordingIsWide) {
        /* The recorded camera sits in the SWOS-2020 widescreen space (WIDESCREEN_WIDTH).
         * Re-centre it for the ACTUAL playback width so the action stays centred at any
         * resolution — verbatim only centred when _screen_width == WIDESCREEN_WIDTH, so at
         * 4:3 (362) it landed (480-362)/2 px too far left. Same shape as the narrow path,
         * just with rec width 480 instead of 336. */
        out = (int16_t)(recordedX + (WIDESCREEN_WIDTH - _screen_width) / 2);
        if (out < 0) out = 0;
        else if (out > panRange) out = panRange;
    } else if (panRange <= 64) {
        out = (int16_t)(panRange / 2);                 /* narrow + whole pitch fits -> lock */
    } else {
        out = (int16_t)(recordedX - (int16_t)WIDE_MARGIN);
        if (out < 0) out = 0;
        else if (out > panRange) out = panRange;        /* 336 content: re-centre + follow */
    }
    return ReplaySimCameraX(out);
#else
    return recordedX;
#endif
}

/* Clamp a replay's recorded camera Y to the live vertical range [0x10, 0x260]
 * and record the shift so recorded (screen-space) sprites re-anchor onto the
 * clamped playback camera — the Y counterpart of ReplayCameraX. DOS HILs were
 * captured in a 200px-tall view and reach ~0x295, past our 272px view's limit;
 * clamping keeps the view on the pitch (no green) and the shift keeps the
 * players aligned. No-op for our own 272-tall HILs (camera already in range). */
static int16_t ReplayCameraY(int16_t recordedY)
{
#ifdef SWOS_SDL_BUILD
    extern uint16_t _playHighlightFlag;
    s_replayRecCamY = recordedY;   /* the decode rebuilds world Y from this */
    if (_playHighlightFlag == 0)
        /* In-game instant replay: game_loop.c records the DRAWN origin (Render_CameraY,
         * as it records Render_CameraX), so hand the camera the simulation Y that maps
         * back onto it -- the identity unless the picture is taller than the view. */
        return Render_SimCameraYFromRender(recordedY);

    /* Y is not scaled the way X is, so the playback camera is just the recorded
     * one clamped into the live vertical range. Sprites follow it regardless of
     * how far it is clamped, because the decode re-projects world positions
     * rather than shifting by the clamp -- which is the point of the rewrite.
     *
     * NOTE 0x260 is still a constant while the visible height became runtime in
     * ded927c. That is a FRAMING choice now, not a correctness one, but it is
     * wrong for the DOS view's 200 lines and wants revisiting. */
    /* The picture's own range, [0x10, 880 - its height]: 0x260 at 272 lines, as the
     * literal was, and the 680 a 200-line DOS picture reaches (the NOTE above). Then back
     * to the simulation camera that draws there. */
    int16_t out = recordedY;
    int16_t bottom = (int16_t)(880 - _screen_height);
    if (out < 0x10) out = 0x10;
    else if (out > bottom) out = bottom;
    return Render_SimCameraYFromRender(out);
#else
    return recordedY;
#endif
}

/* ---- Local probability tables (matching asm data) ---- */

/* pitchTypeSeasonalProbabilities: 12 seasons * 7 types = 84 bytes
 * Original at line 10919 in SWOS2.s */
static const uint8_t pitchTypeSeasonalProbs[] = {
    30,20,30,20, 0, 0, 0,
    20,30,20,20,10, 0, 0,
    10,30,10,30,20, 0, 0,
     0,10,10,30,40,10, 0,
     0, 0, 0,10,40,40,10,
     0, 0, 0, 0,40,40,20,
     0, 0, 0, 0,30,30,40,
     0, 0, 0, 0,50,30,20,
     0, 0, 0,20,40,30,10,
     0,20, 0,40,30,10, 0,
    10,30,10,40,10, 0, 0,
    20,30,20,30, 0, 0, 0,
};

/* pitchTypeProbabilities: 8 bytes (standard, non-seasonal)
 * Original at line 10927 in SWOS2.s */
static const uint8_t pitchTypeProbs[] = {
    5, 5, 10, 20, 30, 20, 10, 0
};

/* pitchNumberProbabilities: 16-byte lookup
 * Original at line 11971 in SWOS2.s */
static const uint8_t pitchNumProbs[] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 4, 4, 2, 2, 3, 3
};

/* ============================================================
 * Internal helpers (not exported to asm)
 * ============================================================ */

/* Wrap pointer forward in ring buffer (matches ValidateHilPointerAdd) */
static uint8_t *wrapPointerFwd(uint8_t *p)
{
    if (p == _nextGoalPtr)
        p = _goalBasePtr;
    return p;
}

/* Wrap pointer backward in ring buffer (matches ValidateHilPointerSub) */
static uint8_t *wrapPointerBack(uint8_t *p)
{
    if (p < _goalBasePtr)
        p = _nextGoalPtr;
    return p;
}

/* Wrap a1-style pointer (IncrementHilBufferPointer) — same as wrapPointerFwd */
static uint8_t *wrapHilPtr(uint8_t *p)
{
    if (p == _nextGoalPtr)
        p = _goalBasePtr;
    return p;
}

/* ============================================================
 * SetPitchTypeAndNumber
 * ============================================================ */
__attribute__((used, externally_visible))
void SetPitchTypeAndNumber(void)
{
    _numScenesInHilFile = 0;

    int16_t _pitchType;
    const uint8_t *probs;
    int needChoose = 0;

    if (_gamePitchTypeOrSeason == 0) {
        /* Fixed pitch type mode */
        _pitchType = _gamePitchType;
        if (_pitchType < 0) {
            /* Negative = auto-select from standard probabilities */
            probs = pitchTypeProbs;
            needChoose = 1;
        }
    } else {
        /* Seasonal pitch type mode */
        probs = pitchTypeSeasonalProbs + (uint16_t)_gameSeason * 7;
        needChoose = 1;
    }

#ifdef SWOS_SDL_BUILD
    {
        /* SWOS_PITCH_TYPE=0..6 pins the pitch (frozen, muddy, wet, soft, normal,
         * dry, hard) for a test run: the pitch's colours are what the players are
         * drawn against, and picking one from the menus costs a key script.
         * Reads the same Rand() either way, so a pinned run stays deterministic. */
        const char *pin = getenv("SWOS_PITCH_TYPE");
        if (pin && *pin) {
            int32_t want = (int32_t)strtol(pin, NULL, 10);
            if (want >= 0 && want <= 6) {
                if (needChoose)
                    (void)Rand();
                _pitchType = (int16_t)want;
                needChoose = 0;
            }
        }
    }
#endif

    if (needChoose) {
        /* Random selection based on cumulative probabilities */
        uint16_t r = Rand();
        uint8_t d0 = (uint8_t)((uint32_t)r * 100 / 256);
        _pitchType = 0;
        while (d0 >= *probs) {
            d0 -= *probs++;
            _pitchType++;
        }
    }

    /* Determine pitch number.
     * Note: asm has 3 dead instructions here (currentGameTick & 0x10 << 4)
     * that get immediately overwritten in both branches. Omitted. */
    uint16_t pitchNum;
    if (_plgD0Param != 0) {
        pitchNum = Rand() & 0xF;
    } else {
        /* Derive from team data */
        uint16_t v = *(uint16_t *)_topTeamInGame.teamName;
        v ^= (uint16_t)_topTeamInGame.prShirtCol;
        v ^= (uint16_t)_topTeamInGame.prShortsCol;
        pitchNum = v & 0xF;
    }

    uint16_t d0 = (uint16_t)pitchNumProbs[pitchNum] << 8;

    if (_trainingGame != 0)
        d0 = 0x500;

    _pitchNumberAndType = (uint16_t)_pitchType + d0;

    /* A saved position is played on the pitch it was saved on (src/challenge.c). The
     * override lands HERE, after both Rand() calls above, so the generators are left
     * exactly where a normal kick-off leaves them -- the position's own seeds overwrite
     * them a moment later anyway, but a pin that changed how much randomness the init
     * consumed would be a trap for everything else. The pitch is not in the state
     * registry (it is which art is loaded, not what the match reads), yet the fan rows in
     * the state belong to it -- and a soft pitch plays differently from a dry one. */
    {
        uint16_t pinned;
        extern void Challenge_NotePitch(uint16_t rolled, uint16_t pinned, int32_t armed);
        int32_t armed = Challenge_PitchOverride(&pinned);
        Challenge_NotePitch(_pitchNumberAndType, armed ? pinned : 0, armed);
        if (armed)
            _pitchNumberAndType = pinned;
    }

#ifdef DETERMINISTIC_MODE
    /* Force fixed pitch to ensure ASM and C versions match */
    _pitchNumberAndType = 0x0004;  /* pitch 0, type 4 (normal) */
#endif
}

/* ============================================================
 * CopyTeamsAndHeader
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void CopyTeamsAndHeader(void)
{
    _numScenesInHilFile = _hilNumGoals;
    _pitchNumberAndType = _hilPitchNumber;
#ifdef SWOS_SDL_BUILD
    {
        /* SWOS_PITCH_TYPE=0..6 / SWOS_PITCH_NUMBER=1..6 pin a highlight's pitch too,
         * as they do a match's (ChoosePitchType, dos_load_pitch): one recording
         * filmed on every pitch. Only the picture; the scenes are what they were. */
        const char *t = getenv("SWOS_PITCH_TYPE"), *n = getenv("SWOS_PITCH_NUMBER");
        if (t && t[0] >= '0' && t[0] <= '6' && !t[1])
            _pitchNumberAndType = (uint16_t)((_pitchNumberAndType & 0xFF00) | (uint16_t)(t[0] - '0'));
        if (n && n[0] >= '1' && n[0] <= '6' && !n[1])
            _pitchNumberAndType = (uint16_t)((_pitchNumberAndType & 0x00FF) | (uint16_t)((n[0] - '1') << 8));
    }
#endif

    /* Copy team data from hil file buffer (offset 8) */
    uint8_t *src = &_hilFileBuffer[8];
    uint16_t i;
    for (i = 0; i < 1018; i++) {
        ((uint8_t *)&_bottomTeamInGame)[i] = src[1018 + i];
        ((uint8_t *)&_topTeamInGame)[i] = src[i];
    }

    /* The TeamGame header has 11 int16 fields at offsets 0..21 (kit
     * colours + markedPlayer). They're stored big-endian on disk to
     * match Amiga m68k; byteswap to host order so prShirtType etc.
     * index correctly into the little-player sprite table when the
     * stadium menu draws. The remaining 996 bytes are byte-sized
     * (teamName + per-player fields) and don't need swapping.
     * BE16_TO_HOST is a no-op on the Amiga BE build. */
    {
        int16_t *topHdr = (int16_t *)&_topTeamInGame;
        int16_t *botHdr = (int16_t *)&_bottomTeamInGame;
        for (i = 0; i < 11; i++) {
            topHdr[i] = BE16_TO_HOST((uint16_t)topHdr[i]);
            botHdr[i] = BE16_TO_HOST((uint16_t)botHdr[i]);
        }
    }

    /* Copy game name and round */
    for (i = 0; i < 40; i++) {
        _gameName[i] = _hilGameName[i];
        _gameRound[i] = _hilGameRound[i];
    }
}

/* ============================================================
 * SetHilFileHeader
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void SetHilFileHeader(void)
{
    /* HIL header is big-endian on disk to match Amiga m68k. BE_WRITE_U32
     * is a no-op on Amiga BE host, byteswap on x86 LE — so saves from the
     * SDL build are now Amiga-compatible. */
    BE_WRITE_U32(_hilFileBuffer, K_HIL_FILE_SIG);
    BE_WRITE_U32(&_hilFileBuffer[4], K_HIL_FILE_MAXSZ);

    /* Pack team data into hil file buffer at offset 8. The first 22 bytes
     * of each TeamGame are 11 int16 kit/marker fields — byteswap host→BE
     * as we copy so the on-disk format matches Amiga m68k. The remaining
     * 996 bytes are byte-sized and copy verbatim. */
    uint8_t *dst = &_hilFileBuffer[8];
    uint16_t i;
    {
        int16_t *topHdr = (int16_t *)&_topTeamInGame;
        int16_t *botHdr = (int16_t *)&_bottomTeamInGame;
        for (i = 0; i < 11; i++) {
            ((uint16_t *)dst)[i]            = HOST_TO_BE16((uint16_t)topHdr[i]);
            ((uint16_t *)(dst + 1018))[i]   = HOST_TO_BE16((uint16_t)botHdr[i]);
        }
    }
    for (i = 22; i < 1018; i++) {
        dst[1018 + i] = ((uint8_t *)&_bottomTeamInGame)[i];
        dst[i] = ((uint8_t *)&_topTeamInGame)[i];
    }

    /* Copy game name and round */
    for (i = 0; i < 40; i++) {
        _hilGameName[i] = _gameName[i];
        _hilGameRound[i] = _gameRound[i];
    }

    _hilNumGoals = _numScenesInHilFile;

    _hilTeam1Goals = _statsTeam1Goals;
    _hilTeam2Goals = _statsTeam2Goals;
    _hilPitchNumber = _pitchNumberAndType;
    _hilMaxSubstitutes = _gameMaxSubstitutes;
}

/* ============================================================
 * SaveHighlightsPointers
 * ============================================================ */
__attribute__((used, externally_visible))
void SaveHighlightsPointers(void)
{
    if (_numScenesInHilFile == 0)
        return;

    _normalReplayRunning = 1;
    _savedGoalBasePtr = (uintptr_t)_goalBasePtr;
    _savedNextGoalPtr = (uintptr_t)_nextGoalPtr;
    _savedCurrentHilPtr = (uintptr_t)_currentHilPtr;
    _savedCurrentGoalPtr = (uintptr_t)_currentGoalPtr;
    _goIntoHighlightsLoop = 1;
    _hilSceneNumber = 0xFFFF;
}

/* ============================================================
 * RestoreHighlightPointers
 * ============================================================ */
__attribute__((used, externally_visible))
void RestoreHighlightPointers(void)
{
    _normalReplayRunning = 0;
    _goalBasePtr = (uint8_t *)_savedGoalBasePtr;
    _nextGoalPtr = (uint8_t *)_savedNextGoalPtr;
    _currentHilPtr = (uint8_t *)_savedCurrentHilPtr;
    _currentGoalPtr = (uint8_t *)_savedCurrentGoalPtr;
    _goIntoHighlightsLoop = 0;
}

/* ============================================================
 * FadeAndSaveHighlightScene
 * ============================================================ */
__attribute__((used, externally_visible))
void FadeAndSaveHighlightScene(void)
{
    FadeOutToBlack();
    SaveHighlightScene();
    FadeIn();
}

/* ============================================================
 * SaveHighlightScene
 * Saves current replay ring buffer segment to the hil goals buffer.
 * Maximum HIL_MAX_SCENES scenes. If buffer is full, shifts scenes
 * 1..9 down to 0..8, freeing the last slot.
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void SaveHighlightScene(void)
{
    /* Asm: move.w #10,d0; beq locret — the beq is dead (10 != 0). Omitted. */

    if (_numScenesInHilFile == HIL_MAX_SCENES) {
        /* Buffer full — shift scenes 1..9 into slots 0..8 */
        uint32_t *src = (uint32_t *)_inGameHilBuffer;
        uint32_t *dst = (uint32_t *)_hilStartOfGoalsBuffer;
        int16_t block;
        uint16_t j;
        for (block = 8; block >= 0; block--) {
            for (j = 0; j < HIL_SCENE_LONGS; j++) {
                *dst++ = *src++;
            }
        }
        _numScenesInHilFile--;
    }

    /* Scan forward from currentHilPtr for a frame-start marker (bit 7 of first byte set).
     * Scan at most 19000 bytes (4750 longs). */
    uint32_t count = 0;
    uint8_t *p = _currentHilPtr;

    for (;;) {
        if (count == HIL_SCENE_BYTES)
            return;  /* Not found within range */
        count += 4;
        p += 4;
        if (p == _nextGoalPtr)
            p = _goalBasePtr;
        if (*p & 0x80)
            break;  /* Found frame-start marker */
    }

    /* Copy 19000 bytes from ring buffer (starting at p) into goals buffer slot */
    uint32_t *dest = (uint32_t *)(_hilStartOfGoalsBuffer +
                                   (uint32_t)_numScenesInHilFile * HIL_SCENE_BYTES);
    uint16_t i;
    for (i = 0; i < HIL_SCENE_LONGS; i++) {
        *dest++ = *(uint32_t *)p;
        p += 4;
        if (p == _nextGoalPtr)
            p = _goalBasePtr;
    }
    _numScenesInHilFile++;
}

/* ============================================================
 * ThrowInDeadProc
 * ============================================================ */
__attribute__((used, externally_visible))
void ThrowInDeadProc(void)
{
    _throwInDeadVar = 1;
}

/* ============================================================
 * SaveCoordinatesForHighlights
 * ============================================================ */
__attribute__((used, externally_visible))
void SaveCoordinatesForHighlights(int16_t spriteIndexOrMarker, uint16_t x, uint16_t y)
{
    uint8_t *a1 = _currentHilPtr;

    if (spriteIndexOrMarker >= 0) {
        /* Pack sprite data: (index << 20) | (x << 10) | y, with bit 31 clear.
         * Stored big-endian so the high byte (which holds bit 31, the
         * frame-marker discriminator vs sprite-data) lands at offset 0
         * regardless of host endianness — matching the Amiga byte layout. */
        uint32_t packed = (uint32_t)(uint16_t)spriteIndexOrMarker;
        packed <<= 10;
        packed |= (x & 0x3FF);
        packed <<= 10;
        packed |= (y & 0x3FF);
        packed &= ~(1u << 31);  /* bclr #31 */
        BE_WRITE_U32(a1, packed);
        a1 += 4;
        if (a1 == _nextGoalPtr)
            a1 = _goalBasePtr;
        _currentHilPtr = a1;
        return;
    }

    if ((uint16_t)spriteIndexOrMarker == 0xFFFE) {
        /* End-of-segment marker: write -1 (byte pattern is identical
         * either endian, but BE_WRITE_U32 keeps the policy consistent). */
        BE_WRITE_U32(a1, 0xFFFFFFFFu);
        return;
    }

    /* Frame-start marker (d0 < 0, not -2): write camera coords with bit 31
     * (= bit 15 of high uint16 = marker) set. Two BE_WRITE_U16 calls match
     * the original Amiga byte format exactly — on Amiga m68k the BE_WRITE
     * macros are no-ops so this is identical to the prior asm-to-C port,
     * and on x86 LE the byte-swap puts the 0x80 marker byte at offset 0
     * where HandleInstantReplayStateSwitch's `*p & 0x80` scan and
     * DrawReplayFrame's BE-uint32 bit-31 test both detect it. */
    BE_WRITE_U16(a1,     (uint16_t)x | 0x8000u);
    BE_WRITE_U16(a1 + 2, (uint16_t)y);
    a1 += 4;
    a1 = wrapHilPtr(a1);
    _currentHilPtr = a1;

    /* Write team goals (BE for cross-build .HIL file interchange) */
    BE_WRITE_U16(a1,     _team1TotalGoals);
    BE_WRITE_U16(a1 + 2, _team2TotalGoals);
    a1 += 4;
    a1 = wrapHilPtr(a1);

    /* Write animated patterns state (duplicated in both words) */
    BE_WRITE_U16(a1,     _animPatternsState);
    BE_WRITE_U16(a1 + 2, _animPatternsState);
    a1 += 4;
    a1 = wrapHilPtr(a1);
    _currentHilPtr = a1;

    /* Write end-of-frame marker */
    BE_WRITE_U32(a1, 0xFFFFFFFFu);
}

/* ============================================================
 * DrawReplayFrame
 * Complex replay frame playback state machine.
 * Goto labels map 1:1 to asm labels:
 *   sprite_decode  = loc_10C494
 *   check_next     = loc_10C4D0
 *   end_replay     = loc_10C4C4
 *   after_decode   = loc_10C4E2
 * ============================================================ */
__attribute__((used, externally_visible))
void DrawReplayFrame(void)
{
    uint8_t *a0 = _currentGoalPtr;
    uint32_t d0;

#ifdef SWOS_SDL_BUILD
    /* A loaded .SRP plays from its own frame stream (src/replay_v2.c); the score overlay
     * and the pause handling below are shared. */
    {
        extern uint16_t _playHighlightFlag;
        extern int32_t  ReplayV2_Loaded(void);
        extern void     ReplayV2_DrawFrame(void);
        if (_playHighlightFlag != 0 && ReplayV2_Loaded()) {
            ReplayV2_DrawFrame();
            if (_replayState == 0)
                return;
            goto after_decode;
        }
    }
#endif

    if (_replayState == 1) {
        /* STATE 1 → 2: Initialize replay */
        _replayState = 2;
        _savedCameraX = _cameraX.s.i;
        _savedCameraY = _cameraY.s.i;

        /* Read first frame data (without advancing a0) for camera init.
         * Buffer is stored big-endian — BE_READ_U32 is a no-op on Amiga
         * and a byte-swap on x86 LE. */
        uint32_t val = BE_READ_U32(a0);
        val &= ~(1u << 31);  /* bclr #31 */

        ResetAnimatedPatternsForBothTeams();
        ReplayUpdateCameraAndSwapBuffers(ReplayCameraX((int16_t)(val >> 16)),
                                          ReplayCameraY((int16_t)(val & 0xFFFF)));
    }

    /* loc_10C434: read entry */
    d0 = BE_READ_U32(a0);
    a0 += 4;
    if (!(d0 & (1u << 31)))
        goto sprite_decode;

    /* Frame-start marker processing */
    a0 = wrapPointerFwd(a0);
    d0 &= ~(1u << 31);
    MoveView(ReplayCameraX((int16_t)(d0 >> 16)), ReplayCameraY((int16_t)(d0 & 0xFFFF)));

    /* Read team goals */
    _hilLeftTeamGoals = BE_READ_U16(a0);
    _hilRightTeamGoals = BE_READ_U16(a0 + 2);
    a0 += 4;
    a0 = wrapPointerFwd(a0);

    /* Read animated patterns state (stored as two identical words) */
    _animPatternsState = BE_READ_U16(a0);
    _animPatternsState = BE_READ_U16(a0 + 2);
    a0 += 4;
    a0 = wrapPointerFwd(a0);

    DrawAnimatedPatterns();

    /* Read next entry after frame marker */
    d0 = BE_READ_U32(a0);
    a0 += 4;
    if (d0 == 0xFFFFFFFFu)
        goto end_replay;
    goto check_next;

sprite_decode:
    /* loc_10C494: sprite data processing */
    a0 = wrapPointerFwd(a0);
    {
        /* Unpack: bits 20-31 = index, bits 10-19 = x (signed 10-bit),
         * bits 0-9 = y (signed 10-bit).
         * 68k: lsl.l #12 then asr.l #22 sign-extends the 10-bit field. */
        int32_t sx = (int32_t)(d0 << 12) >> 22;
        int32_t sy = (int32_t)(d0 << 22) >> 22;
        uint32_t sprIdx = d0 >> 20;
#ifdef SWOS_SDL_BUILD
#if REPLAY_BALL_TRACK_CAMERA
        if (sprIdx == REPLAY_BALL_SHADOW_SPR) {
            /* sx is still the recorded (un-re-anchored) screen X here, so
             * recorded-camera + sx is the ball's world X — used to aim the
             * NEXT frame's camera (ReplayCameraX). Ignore off-pitch outliers
             * (out-of-play / between-scene frames) so they don't yank the camera;
             * the pitch is [0, 0x2A0], allow a little margin. */
            int bx = (int)s_replayRecCamX + (int)sx;
            if (bx >= -48 && bx <= 0x2A0 + 48) {
                s_replayBallWorldX = bx;
                s_replayBallValid  = 1;
            }
        }
#endif
        /* Rebuild the WORLD position, then project it through the LIVE playback
         * camera -- byte for byte the expression live drawing uses. Nothing is
         * anchored to a value we merely asked for, so any rounding in the
         * sim<->render conversion above is absorbed here instead of accumulating
         * into a slide: `_menu_draw_x_offset` is cancelled for the same reason
         * drawPatternFromIndex cancels it (the blit adds it, the pitch is drawn
         * without it). */
        {
            extern int _menu_draw_x_offset;
            int32_t worldX = sx + (int32_t)s_replayRecCamX;
            int32_t worldY = sy + (int32_t)s_replayRecCamY;

            /* ---- Goal-overlay anchor guard ------------------------------------
             * The goal overlay is the one recorded sprite whose world position is
             * a CONSTANT -- sprite_data.c pins both goals at world x = 300 -- so
             * it is a free, exact check that the recording and the playback agree
             * about what space they are in. If this drifts, every stationary
             * object is sliding against the pitch and the net will visibly walk
             * out of the goal as the camera pans. That is ReportBot 0006, and the
             * three regressions cited in ReplayCameraX before it; each one was a
             * camera-mapping change that nothing caught because the replay corpus
             * pins 336, where all the mappings coincide. RUN THIS AT A WIDTH THAT
             * IS NOT 336 or it can never fire. */
            if (sprIdx == REPLAY_GOAL_TOP_SPR || sprIdx == REPLAY_GOAL_BOTTOM_SPR) {
                int slot = (sprIdx == REPLAY_GOAL_BOTTOM_SPR);
                if (s_goalAnchorX[slot] == 0)
                    s_goalAnchorX[slot] = worldX;
                else if (worldX != s_goalAnchorX[slot]) {
                    int32_t drift = worldX - s_goalAnchorX[slot];
                    if (drift > s_goalAnchorDrift) s_goalAnchorDrift = drift;
                    else if (-drift > s_goalAnchorDrift) s_goalAnchorDrift = -drift;
                    if (!s_goalAnchorWarned[slot]) {
                        s_goalAnchorWarned[slot] = 1;
                        SWOS_DBG("[replay] GOAL ANCHOR DRIFT: %s goal world x %d, "
                                 "was %d (%+d) at width %d. The recording and the "
                                 "playback camera disagree about their space; every "
                                 "stationary object is sliding against the pitch.\n",
                                 slot ? "bottom" : "top", (int)worldX,
                                 (int)s_goalAnchorX[slot], (int)drift,
                                 (int)_screen_width);
                        SWOS_DBG_FLUSH();
                    }
                }
            }

            sx = worldX - (int32_t)Render_CameraX() - _menu_draw_x_offset;
            sy = worldY - (int32_t)Render_CameraY();
        }
#endif
        DarkenRectangle((uint16_t)sprIdx, (uint16_t)(int16_t)sx,
                          (uint16_t)(int16_t)sy);
    }

    /* Read next entry after sprite */
    d0 = BE_READ_U32(a0);
    a0 += 4;
    if (d0 == 0xFFFFFFFFu)
        goto end_replay;
    /* fall through to check_next */

check_next:
    /* loc_10C4D0: check type of next entry */
    if (!(d0 & (1u << 31)))
        goto sprite_decode;  /* Another sprite — loop */

    /* Next frame-start marker — back up, save pointer for next call */
    a0 -= 4;
    a0 = wrapPointerBack(a0);
    _currentGoalPtr = a0;
    goto after_decode;

end_replay:
    /* loc_10C4C4 */
    _replayState = 0;
    return;

after_decode:
    /* loc_10C4E2: overlay drawing */

    /* The overlay's coordinates are each release's own, against its own screen:
     * swos.asm's against 320 in the DOS view, SWOS2.s's against 336 otherwise. So the
     * margin that centres that screen in ours is measured from the matching width --
     * WIDE_MARGIN is measured from 336 and is -8 in the DOS view, which would put
     * both of these 8 pixels further left than the DOS draws them. */
    const int dosv = (_gDosView != 0);
    /* ...less what the blit itself adds: _menu_draw_x_offset is 0 in a match (an
     * instant replay) but WIDE_MARGIN in the highlights viewer, a menu screen, where
     * adding the margin here as well put the score (W-336)/2 right of its corner --
     * near the middle of a wide screen. */
    extern int _menu_draw_x_offset;
    const int overlayMargin = (_screen_width - (dosv ? DOS_VIEW_WIDTH : SCREEN_WIDTH)) / 2
                              - _menu_draw_x_offset;

    /* Show replay icon (spinning R) when NOT in normal replay: (11, 14) in both
     * releases. */
    if (_normalReplayRunning == 0) {
        uint16_t frame = (_currentGameTick >> 1) & 0x1F;
        DarkenRectangle(SPR_REPLAY_FRAME_00 + frame, (uint16_t)(0xB + overlayMargin), 0xE);
    }

    /* Show score overlay during normal replay (same playfield-relative offset).
     * Suppressed in arcade attract so the demo reels don't show a scoreline. */
    int showScore = (_normalReplayRunning != 0);
#ifdef SWOS_SDL_BUILD
    { extern int _gArcadeMode; if (_gArcadeMode) showScore = 0; }
#endif
    if (showScore) {
        /* The score sits in the TOP-LEFT corner, as both releases draw it: SWOS2.s
         * `move.w #$D,d1 / #$D,d2 / addq.w #4` = (17, 17), swos.asm `mov D1, 9 /
         * D2, 9 / add 4` = (13, 13). It was once centred across the screen, which
         * no release ever did (ReportBot 0010). */
        const uint16_t corner = dosv ? 13 : 17;
        uint16_t by = corner;
        uint16_t bx = (uint16_t)(corner + overlayMargin);

        uint16_t leftGoals  = _hilLeftTeamGoals;
        uint16_t rightGoals = _hilRightTeamGoals;
        uint16_t lTens = leftGoals / 10,  lUnits = leftGoals % 10;
        uint16_t rTens = rightGoals / 10, rUnits = rightGoals % 10;

        /* Left team goals (SPR_BIG_0 digit sprites) */
        if (lTens) { DarkenRectangle(SPR_BIG_0 + lTens, bx, by); bx += 12; }
        DarkenRectangle(SPR_BIG_0 + lUnits, bx, by);
        bx += 15;
        by += 8;

        /* Dash separator (sprite 0xC3 = 195) */
        DarkenRectangle(0xC3, bx, by);
        by -= 8;
        bx += 8;

        /* Right team goals (SPR_BIG_0_ALT digit sprites) */
        if (rTens) { DarkenRectangle(SPR_BIG_0_ALT + rTens, bx, by); bx += 12; }
        DarkenRectangle(SPR_BIG_0_ALT + rUnits, bx, by);
    }

    /* State 3 timing: stretch each replay frame across 2 extra ticks
     * (≈ half-speed slomo). On Amiga the busy-spin on `currentGameTick`
     * was harmless because the VBlank ISR ticked it AND drove audio
     * pre-emptively. On SDL the tick is advanced by the SDL_Timer
     * thread, but audio production runs on the *main* thread inside
     * Platform_VBlank → AudioSDL_Tick. A pure busy-spin on the main
     * thread starves the audio queue for 40 ms per replay frame,
     * which is exactly the stutter testers hear during R-then-R
     * slomo. Use Platform_VBlank instead so we both pace at 50 Hz AND
     * keep AudioSDL_Tick firing on every wait iteration. */
    if (_replayState == 3 && _paused == 0) {
#ifdef SWOS_SDL_BUILD
        extern int Platform_VBlank(void);
        Platform_VBlank();
        Platform_VBlank();
#else
        uint16_t i;
        for (i = 0; i < 2; i++) {
            volatile uint16_t *tickPtr = &_currentGameTick;
            uint16_t tick = *tickPtr;
            while (*tickPtr == tick)
                ;  /* busy-wait for next tick */
        }
#endif
    }
}

/* ============================================================
 * ReplayUpdateCameraAndSwapBuffers
 * ============================================================ */
__attribute__((used, externally_visible))
void ReplayUpdateCameraAndSwapBuffers(int16_t targetX, int16_t targetY)
{
    /* AGA: populate the 2x-tall frontbuffer's mirror half after each full fill,
     * so the copper's block A displays with no block-B reload. No-op elsewhere. */
    extern void MirrorFrontbuffer(void);
    /* targetX/Y are SIMULATION camera coordinates. Two of the three callers already
     * hold them -- the match start (SIM_CAMERA_CENTRE_X) and RestoreCameraPosition
     * (_cameraX saved before the replay) -- so a recorded value is converted by
     * its one caller, DrawReplayFrame, and never here. Converting here treated the
     * kick-off centre as a render origin: away from 336 the camera started in the
     * wrong place and slid back over the first second, jumped again after every
     * instant replay, and left a width-dependent fraction in `_cameraX`. Caught by
     * scripts/replay_v2_test.sh at 484, broken since 7bf1b37. */
    SetCameraCoordinates(targetX, targetY);
    InitCameraMoveView(targetX, targetY);
    MirrorFrontbuffer();
    SwapBuffersAndCameraViewState();
    InitCameraMoveView(targetX, targetY);
    MirrorFrontbuffer();
}

/* ============================================================
 * HandleInstantReplayStateSwitch
 * ============================================================ */
__attribute__((used, externally_visible))
void HandleInstantReplayStateSwitch(void)
{
    if (_replayState == 0) {
        /* Search for frame-start marker in ring buffer.
         * Entries are stored big-endian (see frame-marker write in
         * SaveCoordinatesForHighlights), so the high byte of the uint32
         * — which carries bit 31 — sits at byte offset 0 regardless of
         * host endian. `*p & 0x80` therefore detects the marker on both
         * BE (Amiga) and LE (SDL) hosts. */
        uint32_t count = 0;
        uint8_t *p = _currentHilPtr;

        for (;;) {
            if (count == HIL_SCENE_BYTES)
                return;  /* Not found */
            count += 4;
            p += 4;
            if (p == _nextGoalPtr)
                p = _goalBasePtr;
            if (*p & 0x80) {
                /* Found frame start */
                _currentGoalPtr = p;
                _replayState = 1;
                return;
            }
        }
    }

    if (_replayState == 3) {
        _replayState = 2;
        return;
    }
    if (_replayState == 2) {
        _replayState = 3;
        return;
    }
}

/* ============================================================
 * RestoreCameraPosition
 * ============================================================ */
__attribute__((used, externally_visible))
void RestoreCameraPosition(void)
{
    _replayState = 0;
    int16_t x = _savedCameraX;
    int16_t y = _savedCameraY;
    ResetAnimatedPatternsForBothTeams();
    ReplayUpdateCameraAndSwapBuffers(x, y);
}
