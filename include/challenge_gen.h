/*
 * challenge_gen.h -- the CHALLENGE GENERATOR: construct a .CHL save state from a spec
 * instead of playing into the position and pressing the save key. SDL only -- a .CHL is
 * target-specific (the layout hashes differ), so the m68k build neither writes nor needs
 * this. See docs/CHALLENGE.md "Generating challenges" and src/challenge_gen.c.
 *
 * `SWOS --challenge-gen=spec.json` boots headless, kicks off a real friendly between the
 * spec's teams and drives the ENGINE into the wanted position: the score is written as
 * state (no goal is replayed -- score and clock are plain state), but the position
 * itself is a DEAD-BALL line-up the engine builds on its own -- the kickoff after the
 * spec's last goal, or a corner. Every restart funnels through the break state machine
 * (src/game_timers.c), whose ST_WAITING_ON_PLAYER is "line-up complete, waiting on the
 * taker": that is where the position is saved, through the ordinary challenge encoder.
 * The engine is the authority for every byte the spec does not mention, which is the
 * whole point -- a fabricated state blob cannot be kept consistent by hand.
 */
#ifndef CHALLENGE_GEN_H
#define CHALLENGE_GEN_H

#include "game_types.h"

#ifdef SWOS_SDL_BUILD

/* Read, parse and check the spec file now, at argument-parse time: the path is the
 * user's, not a save-folder name, and the process chdirs later. 0 on success; on
 * anything else the reason has been printed and the caller should exit. */
int32_t ChallengeGen_Request(const char *specPath);

int32_t ChallengeGen_Armed(void);

/* Resolve the teams, set up the friendly and play it (SWOS.c Phase 9, like the rigs).
 * Exits the process from inside the match once the file is written -- or with a message
 * when it cannot be. */
void    ChallengeGen_Run(void);

/* The SetupPlayers bypass: the control assignment the spec decided, in place of the two
 * blocking ShowPlayMatchMenu prompts (src/game_init.c, same shape as the arcade's). */
void    ChallengeGen_SetupPlayers(TeamFileEx *team1, TeamFileEx *team2);

/* The per-frame hook in the match loop: a no-op unless armed. Applies the spec once the
 * ball is in play, fast-forwards through half time when the spec's minute needs it,
 * waits for the restart line-up, saves, exits. */
void    ChallengeGen_Frame(void);

#endif /* SWOS_SDL_BUILD */
#endif /* CHALLENGE_GEN_H */
