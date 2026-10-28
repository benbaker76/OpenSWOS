/*
 * challenge.h -- SAVE STATE: a match position written to a file, and played on from
 * there later. See docs/CHALLENGE_WIP.md and docs/AMIGA_DEBUGGING.md.
 *
 * Press the save key mid-match and the position -- the ball, every player, the clock,
 * the score, the scorers, the bookings, the substitutes left, the crowd -- goes to a
 * `.CHL` file in the save folder. Load it and the match carries on from exactly there,
 * with the same two teams and the same rules. A position handed to somebody else is a
 * CHALLENGE: 3-2 down with ten minutes left, see if you can rescue it.
 *
 * Both targets, provided the registry layout and rules match. The bundled samples
 * are separate for Amiga and SDL; their current sprite identity lists differ.
 *
 *   the STATE    SimState_SavePortable (src/sim_state.c), the SIM tier: everything the
 *                match reads, in the PORTABLE encoding -- typed fields big-endian, every
 *                pointer written as the IDENTITY of what it points at. The same 14,994
 *                bytes in every build, 32- or 64-bit, Amiga or PC. This is the same
 *                state netplay resyncs with and a .SRP carries, and loading one into a
 *                fresh process and playing on is proven by the rollback rig
 *                (docs/ROLLBACK.md: 75 of 75 loads identical to the end of the match).
 *   the SETUP    the handful of globals a match is set up from that are NOT in the state
 *                registry, plus the two teams and the six user tactics. The registry has
 *                no room for them because they are not what a match reads frame to frame
 *                -- but a substitution re-rolls player skills from _randomSeed and
 *                friends, and the final whistle needs _gameLength, so a resumed match
 *                that lacked them would play on differently.
 *
 * The state carries both random generators, so the file does not: whatever the seeds were
 * when the position was saved is what they are when it is loaded.
 *
 * NOT compressed. A whole file is about 18 KB, and leaving the deflater out of it keeps
 * the m68k build clear of miniz.
 */
#ifndef CHALLENGE_H
#define CHALLENGE_H

#include "swos_types.h"

/* The rules the position was played under. A file is refused unless this build plays by
 * the same ones -- a save state is a match in progress, and finishing it under other
 * rules is not finishing the same match. */
#define CHAL_RULE_DOS_GAMEPLAY  0x01    /* the DOS gameplay, not the Amiga's */
#define CHAL_RULE_DOS_VIEW      0x02    /* the DOS view: the camera sees 320x200 */
#define CHAL_RULE_NO_CARDS      0x04    /* the testers' no-cards-no-injuries rule */

/* What a file says about itself, read without starting anything: enough for a list, and
 * for a refusal to say what is wrong. */
typedef struct {
    uint32_t frame;         /* the frame of the match the position was taken at */
    uint16_t minute;        /* the clock, as the scoreboard showed it */
    uint16_t goals1;
    uint16_t goals2;
    uint16_t simWidth;      /* the width the match was SIMULATED at (not watched at) */
    uint16_t pitch;         /* _pitchNumberAndType: the pitch it is played on */
    uint8_t  rules;         /* CHAL_RULE_* */
    uint8_t  humanSide;     /* 1 or 2: the side the player had */
    char     team1[17];
    char     team2[17];
    char     build[33];     /* the build stamp that wrote it */
} ChallengeInfo;

/* ---- saving ---- */

/* Whether a position can be taken right now: a match under way, one human side against
 * the computer, and not in the middle of something that is not a position (the
 * substitutes menu, a replay, after the final whistle). */
int32_t Challenge_CanSave(void);

/* Write the position to the next free CHALnn.CHL in the save folder. 0 on success. Safe
 * to call unconditionally -- it asks Challenge_CanSave first. */
int32_t Challenge_SaveNow(void);

/* The same save under a caller-chosen name in the save folder (NULL or "" picks the
 * next free CHALnn.CHL), without the key path's hold-off and on-pitch notice. The
 * challenge generator's writer (src/challenge_gen.c). */
int32_t Challenge_SaveAs(const uint8_t *name);

/* Make the bundled target-specific sample available as SAMPLE.CHL in save/.
 * Existing saves are never overwritten. Called before opening CHALLENGES. */
void Challenge_EnsureSample(void);

/* Frames to keep showing "STATE SAVED" for; the match draws it while this is non-zero
 * and ticks it down itself. */
#ifdef CHAL_AUTOPROBE
/* TEMPORARY diagnostic (-DCHAL_AUTOPROBE): probe the state from inside any running match
 * and write the result to CHALDBG.BIN. See src/challenge.c. */
void Challenge_AutoProbe(void);
#endif

void    Challenge_DumpPitch(void);   /* TEMPORARY diagnostic */
int32_t Challenge_NoticeFrames(void);
void    Challenge_TickNotice(void);
const char *Challenge_NoticeText(void);

/* ---- loading ---- */

/* Remember a file to play, by name, without reading it yet: the command line is parsed
 * before the save folder is known, and a file read then would look in the wrong place.
 * Challenge_Requested() is what the boot path checks. */
void        Challenge_Request(const char *name);
const char *Challenge_Requested(void);

/* Read and check a file, and keep it for the match about to start. 0 on success; on
 * anything else nothing is armed and the reason has been logged. */
int32_t Challenge_Arm(const uint8_t *name);
/* Load a packaged position from data/, leaving user saves untouched. */
int32_t Challenge_ArmBundled(const char *path);
int32_t Challenge_Armed(void);
const ChallengeInfo *Challenge_ArmedInfo(void);

/* Put the armed file's setup globals, teams and tactics in place. Called twice: once
 * before the match init runs (so the init derives from them) and once after it (because
 * the init clears some of them -- the cup rules, _isCareer -- and a resumed match must
 * keep what it had). */
void    Challenge_ApplySetup(void);

/* Set up and play the armed position: the whole match, from the globals to
 * InitAndPlayGame. Returns when the match is over. */
void    Challenge_Play(void);

/* The one-shot inside the match loop: load the state over everything the init just built,
 * before the first frame runs. Returns 1 if a position was loaded. */
int32_t Challenge_LoadState(void);

/* Replay the armed position: restore its setup and re-arm its one-shot state
 * load. Returns 1 for a challenge, 0 for an ordinary match. */
int32_t Challenge_Restart(void);

/* The pitch the position is played on, which must not be rolled again (the fan rows in
 * the state belong to it). 1 and *out set while a position is armed. */
int32_t Challenge_PitchOverride(uint16_t *out);

/* Forget the armed position. Every way out of a match comes through here. */
void    Challenge_Done(void);

/* Encode the setup, scribble over every global it covers, decode it back and require
 * what came back to equal what went out -- so a field this file writes but does not read
 * back cannot pass unnoticed. 0 on success. */
int32_t Challenge_SelfTest(void);

#endif /* CHALLENGE_H */
