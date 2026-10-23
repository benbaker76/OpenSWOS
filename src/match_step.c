/*
 * match_step.c -- a match played a frame at a time from outside the game loop.
 * See include/match_step.h.
 */
#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <string.h>

#include "match_step.h"

extern TeamGeneralInfo   _topTeamData, _bottomTeamData;
extern uint16_t          _gameStatePl;
extern volatile uint16_t _currentGameTick;
extern volatile uint16_t _currentTick;
extern uint16_t          _showingUserStats, _paused;
extern uint16_t          _joy0Dat, _joy1Dat;
extern uint8_t           _ciaa_pra;
extern int               _gDosGameplay;
extern uint16_t          _bench1Called, _bench2Called;
extern volatile uint16_t _keyCount;
extern volatile uint8_t  _keyBuffer[];
extern uint8_t           _keycodeLut[128];

/* g_matchInputLatch lives in src/swos_globals.c: it is a registry region, so both targets
 * must define it (include/match_step.h). */

static int32_t s_warnedKey;

void MatchStep_PackInput(const MatchPlayerInput *in, uint8_t *out)
{
    out[0] = in->controls;
    out[1] = in->key;
}

void MatchStep_UnpackInput(MatchPlayerInput *in, const uint8_t *src)
{
    in->controls = src[0];
    in->key = src[1];
}

int32_t MatchStep_TeamOwner(const TeamGeneralInfo *team)
{
    int32_t n = team->playerCoachNumber ? team->playerCoachNumber : team->playerNumber;
    return n >= 1 && n <= MATCH_PLAYERS ? n : 0;
}

void MatchStep_Reset(void)
{
    memset(&g_matchInputLatch, 0, sizeof g_matchInputLatch);
}

/* JOY0DAT/JOY1DAT encoding, identical to input_sdl.c's encode_joydat -- the same registers
 * the SDL input path writes, so Player1StatusProc/Player2StatusProc decode the input
 * exactly as they decode a real stick. */
static uint16_t encodeJoy(uint8_t controls)
{
    int up = (controls & MATCH_UP) != 0, down = (controls & MATCH_DOWN) != 0;
    int left = (controls & MATCH_LEFT) != 0, right = (controls & MATCH_RIGHT) != 0;
    uint16_t v = 0;
    if (right)          v |= (1u << 1);
    if (right ^ down)   v |= (1u << 0);
    if (left)           v |= (1u << 9);
    if (left  ^ up)     v |= (1u << 8);
    return v;
}

static void callBench(int32_t team)
{
    if (team == 0) _bench1Called = 1;
    else           _bench2Called = 1;
}

/* THE DOS SECOND FIRE BUTTON, in local play.
 *
 * DOS routes it through TeamGeneralInfo.secondaryFire and BenchCheckControls opens the
 * substitutes menu on it: `mov al, subsSecondaryFire / or al, al / js
 * @@enter_substitutes_menu` (swos-port/swos/swos.asm:92110). The AMIGA HAS NO SECOND
 * BUTTON -- its bench is the 'W' scancode through MainKeysCheck, which this port already
 * has and which is not touched -- so this is DOS-mode only, and the caller checks that.
 *
 * It ends at the same callBench() the recorded MATCH_BENCH bit uses, so local play, a
 * recording and a netplay peer all open the bench by one path. Netplay needs nothing
 * extra: the press travels as MATCH_BENCH, which is bit 6, the same bit DOS reads the
 * second button from.
 *
 * Edge-triggered, and it defers while the ball is in play exactly as the frame step does
 * -- opening the bench mid-move is what that guard is for. */
void MatchStep_LocalBenchButton(int32_t player, int32_t pressed)
{
    static int32_t s_wasDown[MATCH_PLAYERS];
    static int32_t s_pending[2];
    const TeamGeneralInfo *team[2] = { &_topTeamData, &_bottomTeamData };
    int32_t i;

    if (player < 1 || player > MATCH_PLAYERS)
        return;

    for (i = 0; i < 2; i++) {
        if (s_pending[i] && _gameStatePl == 0) {
            callBench(i);
            s_pending[i] = 0;
        }
    }
    if (pressed && !s_wasDown[player - 1]) {
        for (i = 0; i < 2; i++) {
            if (MatchStep_TeamOwner(team[i]) != player)
                continue;
            if (_gameStatePl != 0)
                s_pending[i] = 1;
            else
                callBench(i);
            break;
        }
    }
    s_wasDown[player - 1] = pressed ? 1 : 0;
}

void MatchStep_ApplyInput(const MatchPlayerInput in[MATCH_PLAYERS], int32_t frame)
{
    MatchInputLatch *latch = &g_matchInputLatch;
    const TeamGeneralInfo *team[2] = { &_topTeamData, &_bottomTeamData };
    int32_t i;

    _joy1Dat = encodeJoy(in[0].controls);          /* Amiga port 1 = player 1 */
    _joy0Dat = encodeJoy(in[1].controls);          /* port 0 = player 2 */
    _ciaa_pra = 0xFF;
    if (in[0].controls & MATCH_FIRE) _ciaa_pra &= (uint8_t)~(1u << 7);
    if (in[1].controls & MATCH_FIRE) _ciaa_pra &= (uint8_t)~(1u << 6);

    /* The bench call, on the RISING edge of the bench button.
     *
     * `MainKeysCheck` sets these from a key EVENT, not from the key being
     * held: on the reference the hotkey fires once and the menu stays up
     * (`zkz-11215003` holds it for 17 frames and its camera pans to the bench
     * and stops). Re-asserting a held button would be wrong twice over —
     * inside the menu the SAME flag is what LEAVES it
     * (`BenchMenuHandleControls`), so a level would open and close the bench
     * on consecutive frames.
     *
     * bench1Called is the top team's, bench2Called the bottom's, and a button
     * reaches a team only from the player who controls it — the guard
     * `MainKeysCheck` applies (that team must have a human). */
    /* ESTIMATED, NOT RECOVERED — read this before trusting a bench frame.
     *
     * One key reaches the game down two independent latches. The recording
     * carries the POLLED BITMASK: `Player1StatusProc` splits one input word
     * (`test ax, 64` -> `pl1SecondaryFire`, swos.asm:13033), which is
     * `p1_controls` bit 6, and `UpdateAndApplyTeamControls` copies it into
     * `TeamGeneralInfo.secondaryFire` at GameLoop line 703. But the bench
     * opens off the BUFFERED SCANCODE: `bench1Called` is set nowhere in
     * either listing except `MainKeysCheck` (swos.asm:7984, SWOS2.s:21900),
     * which runs at line 666. A scancode can arrive a frame after the bitmask
     * sees the key held, and nothing in the record times that queue.
     *
     * So the recording under-determines this input by one frame, and the rig
     * has to pick. The corpus has three presses and neither placement is
     * right for all of them:
     *
     *      zkz-11215003   f117     acted on the SAME frame
     *      zkz-11215003   f11479   acted one frame LATER
     *      playa-10090613 f11222   acted one frame LATER
     *
     * f117 is not a misreading — under a lag-1 build that frame diverges on
     * `game_state_pl` and on every player's destination, with the reference
     * walking them to the bench line.
     *
     * The predicate below is fitted to those three, not derived: defer unless
     * `gameStatePl` is 0. It is right on all three and takes the corpus from
     * 87.1% to 98.0% (`zkz-11215003` to 27487 of 27819, `playa-10090613` to
     * its terminal frame), which is what says the bench frame is the ONLY
     * thing those two recordings still disagree with us about.
     *
     * Treat a bench-frame match as evidence of nothing. What would make this
     * a recovered fact rather than an estimate is a recording that carries
     * `bench1Called` itself — which the Amiga dumper (docs/TESTS.md section 8)
     * would, being our own build. Until then every deferral says so in the
     * log. */
    for (i = 0; i < 2; i++) {
        int32_t owner = MatchStep_TeamOwner(team[i]);
        uint8_t held = owner && (in[owner - 1].controls & MATCH_BENCH) != 0;
        if (latch->benchPending[i]) {
            callBench(i);
            if (frame >= 0)
                fprintf(stderr, "[match] frame %d: %s team calls the bench "
                                "(deferred one frame — ESTIMATED, see match_step.c)\n",
                        frame, i == 0 ? "top" : "bottom");
            latch->benchPending[i] = 0;
        }
        if (held && !latch->benchHeld[i]) {
            if (_gameStatePl != 0) {
                /* defer to the next frame */
                latch->benchPending[i] = 1;
            } else {
                callBench(i);
                if (frame >= 0)
                    fprintf(stderr, "[match] frame %d: %s team calls the bench\n",
                            frame, i == 0 ? "top" : "bottom");
            }
        }
        latch->benchHeld[i] = held;
    }

    /* A key goes in as a real keypress when it is first held: find the scancode
     * _keycodeLut maps to it and push that, so GetKey/MainKeysCheck run their own
     * guards. */
    for (i = 0; i < MATCH_PLAYERS; i++) {
        uint8_t key = in[i].key;
        if (key && key != latch->keyHeld[i]) {
            int32_t sc, pushed = 0;
            for (sc = 0; sc < 128; sc++) {
                if (_keycodeLut[sc] != key) continue;
                if (_keyCount < 10) {
                    _keyBuffer[_keyCount] = (uint8_t)sc;
                    _keyCount++;
                    pushed = 1;
                    if (frame >= 0)
                        fprintf(stderr, "[match] frame %d: key '%c' (scancode 0x%02X)\n",
                                frame, key, sc);
                }
                break;
            }
            if (!pushed && frame >= 0 && !s_warnedKey) {
                s_warnedKey = 1;
                fprintf(stderr, "[match] frame %d presses '%c' — no scancode for it, "
                                "not injected\n", frame, key);
            }
        }
        latch->keyHeld[i] = key;
    }
}

void MatchStep_AdvanceClock(void)
{
    /* One tick per frame — the Amiga guarantee the SDL timer thread cannot
     * make. Same gates as the interrupt it stands in for (interrupts.c). */
    _currentTick++;
    if (_paused == 0 && (_gDosGameplay || _showingUserStats == 0))
        _currentGameTick++;
}

#endif /* SWOS_SDL_BUILD */
