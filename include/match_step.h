/*
 * match_step.h -- a match played a frame at a time from outside the game loop (SDL builds).
 *
 * The replay rig (src/replay_test.c) and a netplay match (src/netplay_match.c) both hand
 * the match its players' inputs one frame at a time and own its clock. What they share
 * is here: what one frame of one player's input is, how it reaches the game, and the ticks
 * a frame advances. The frame itself is GameLoop_Frame (include/game_loop.h).
 */
#ifndef MATCH_STEP_H
#define MATCH_STEP_H

#include "swos_types.h"

/* What turning inputs into game events remembers from one frame to the next. It is match
 * state -- registered in src/sim_state.c -- so a rollback puts it back with everything
 * else and a resync sends it.
 *
 * The latch and its storage are OUTSIDE the SDL guard below, and defined in
 * src/swos_globals.c rather than here, because the registry is shared: the m68k build has
 * no frame-stepping at all and never writes a byte of this, but it must hold the same
 * region as the SDL build or the two would compute different SimState_LayoutHash values --
 * and then no save state written on one target would load on the other (docs/CHALLENGE.md). */
typedef struct {
    uint8_t benchHeld[2];       /* per team, top then bottom: its bench button last frame */
    uint8_t benchPending[2];    /* per team: a bench call deferred to the next frame */
    uint8_t keyHeld[2];         /* per player: the key held last frame */
} MatchInputLatch;

extern MatchInputLatch g_matchInputLatch;

#ifdef SWOS_SDL_BUILD

#include "game_types.h"

#define MATCH_PLAYERS     2
#define MATCH_INPUT_SIZE  2     /* bytes of one player's input for one frame, on the wire */

/* MatchPlayerInput.controls. The stick and fire bits are the recordings' own (swos-port's
 * ControlFlags), so a recorded control word is an input as it stands. */
#define MATCH_UP     0x01
#define MATCH_DOWN   0x02
#define MATCH_LEFT   0x04
#define MATCH_RIGHT  0x08
#define MATCH_FIRE   0x20
#define MATCH_BENCH  0x40      /* call the bench of the team this player controls */

/* One player's input for one frame. */
typedef struct {
    uint8_t controls;   /* MATCH_* */
    uint8_t key;        /* the key held this frame, as its _keycodeLut value; 0 for none */
} MatchPlayerInput;

void    MatchStep_PackInput(const MatchPlayerInput *in, uint8_t *out);
void    MatchStep_UnpackInput(MatchPlayerInput *in, const uint8_t *src);

/* The player (1 or 2) whose bench call reaches this team -- its coach, or with none its
 * player -- or 0 when no human controls it. */
int32_t MatchStep_TeamOwner(const TeamGeneralInfo *team);

/* Kick-off: no button held, no bench call pending. */
void    MatchStep_Reset(void);

/* The DOS second fire button in local play: opens the subs menu of the team this player
 * controls, which is what DOS's secondaryFire does (swos.asm:92110). DOS mode only -- the
 * Amiga has no second button, its bench is the 'W' scancode. Netplay carries the same
 * press as MATCH_BENCH (bit 6, the bit DOS reads it from) and needs nothing here. */
void    MatchStep_LocalBenchButton(int32_t player, int32_t pressed);

/* Put both players' input where the game reads it: the joystick registers, the fire bits,
 * the bench calls and the key buffer. `frame` numbers the log lines; -1 keeps a frame
 * being re-simulated quiet. */
void    MatchStep_ApplyInput(const MatchPlayerInput in[MATCH_PLAYERS], int32_t frame);

/* The ticks one frame advances. On the Amiga the VBlank interrupt advances them; while a
 * match is played a frame at a time nothing else may (ReplayTest_OwnsGameTick). */
void    MatchStep_AdvanceClock(void);

#endif /* SWOS_SDL_BUILD */
#endif /* MATCH_STEP_H */
