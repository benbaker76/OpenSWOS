/*
 * netplay_match.h -- a match played through the netplay frame step and GGPO (SDL builds).
 * docs/NETPLAY.md, "Playing a match through GGPO".
 *
 * GGPO re-simulates frames from inside its own calls, so a netplay match runs each frame
 * as a step -- the players' inputs, GameLoop_Frame, the clock, the buffer swap and the
 * VBlank interrupt -- that GGPO can call as often as it needs, and shows only the frame it
 * ends on.
 *
 * For now the inputs come from a recording (--replay-test), which makes it a test: the
 * match played through the step and through GGPO must be the match the replay rig plays,
 * frame for frame.
 *
 *   --netplay-match=direct          the netplay step, without GGPO
 *   --netplay-match=synctest[:N]    GGPO's synctest session: every N frames (default 1, at
 *                                   most 8) it loads the state of N frames back, plays
 *                                   them again and compares their checksums
 *   --netplay-match=p2p             one of two peers, each a process with one player's
 *                                   recorded input: GGPO predicts the other player's and
 *                                   rolls back when the prediction was wrong. With
 *     --netplay-player=1|2          the local player
 *     --netplay-port=<port>         the local UDP port
 *     --netplay-peer=<IPv4>:<port>  the other peer
 *     --netplay-delay=<frames>      input delay (default 0)
 *     --netplay-stall=<ms>          pause now and then, up to this long, like a busy
 *                                   machine: the other peer plays ahead on predicted input
 *                                   and rolls back more (a test knob; default 0)
 *   --netplay-trace=<file>          every frame's state hashes as finally played, in the
 *                                   rollback rig's trace format (--rollback-trace)
 *   --netplay-sound-trace=<file>    every sound effect played, one line per effect: frame,
 *                                   kind, channel, id, and "late" for one a rollback first
 *                                   started (a re-simulated frame never repeats one)
 *   --netplay-timeout=<seconds>     give up after this long without progress (default 30)
 *
 * A peer plays frames ahead of its confirmed input, so its state at a frame boundary can
 * still change. It ends the match only at a frame nothing will roll back again, and its
 * trace holds every frame's last save. It plays in the rollback rig's environment
 * (--rollback=trace, and no other --rollback option).
 */
#ifndef NETPLAY_MATCH_H
#define NETPLAY_MATCH_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

/* A --netplay-* option other than --netplay-selftest: 1 = consumed, -1 = bad value. */
int32_t     NetplayMatch_ParseArg(const char *arg);
/* NULL, or what is wrong with the options given together. */
const char *NetplayMatch_ArgError(void);
int32_t     NetplayMatch_Enabled(void);
/* Whether the frame step advances the match clocks (_currentTick, _currentGameTick) --
 * then the VBlank interrupt's work, which the step also runs, must not (interrupts.c).
 * A recording from before that was so plays with the old double clock
 * (NetplayMatch_SetLegacyDoubleClock, from --srp-resim). */
int32_t     NetplayMatch_OwnsClock(void);
void        NetplayMatch_SetLegacyDoubleClock(int32_t on);
int32_t     NetplayMatch_IsP2P(void);

/* ReplayTest_Run, before kick-off: open the session. 0, or non-zero on failure. */
int32_t     NetplayMatch_Start(void);

/* Start a LIVE match against a peer found by the matchmaking menu (or by the
 * direct-IP one): the same p2p session the corpus tests play, with this
 * machine's pad as the local input instead of a recording.
 *
 * `delay` must be the delay BOTH peers agreed -- P2PLink_AgreedDelay() -- and
 * `simWidth` the simulation draw width they agreed (P2PLink_AgreedSimWidth): the
 * match is played at it (display.h, _sim_draw_width) and the display's own width comes
 * back when it ends. Close the p2p link first: GGPO opens its own socket on the same
 * local port. */
int32_t     NetplayMatch_StartLive(int32_t localPlayer, const char *peerIp, uint16_t peerPort,
                                   uint16_t localPort, int32_t delay, int32_t simWidth);

/* The 16-byte key the NEXT p2p session checksums its packets with, or NULL for none
 * (include/netplay.h, NetplayConfig.packetKey). Both peers must set the same one; the
 * lobby does, at kick-off. Kept until changed. */
void        NetplayMatch_SetPacketKey(const uint8_t *key);

/* THE HOST'S SWITCHES: how an online match is played where the builds since 7d9d6d9
 * changed it, each ON or OFF in the lobby by the host alone and played that way by
 * both (report 0042, docs/NETPLAY.md "The host's switches"). ON is how this build plays.
 *
 *   SHORT PRESSES   the press latch (input_sdl.c): a fire tap between two reads counts
 *   KICK-OFF SYNC   GGPO-X predicts 1 frame until the other player's first input (4e7553f3)
 *   CARDS/INJURIES  bookings and injuries; OFF is the old tester builds' online game
 *
 * The first two only change when each game reads and plays its frames; CARDS/INJURIES
 * changes the match itself, and goes into the recording's rules (P2P_RULES_BUILD), so
 * --srp-resim replays it by the same rule. Bits 0x02 and 0x08 were CATCH UP and BALANCE,
 * removed 2026-10-30 (they caused netplay problems: Playa's tests): never set, and taken out
 * of an older host's switches (netplay_lobby.c clamp_rules). */
enum {
    NETPLAY_SW_SHORT_PRESSES  = 0x01,
    NETPLAY_SW_KICKOFF_SYNC   = 0x04,
    NETPLAY_SW_CARDS_INJURIES = 0x10,
    NETPLAY_SW_ALL            = NETPLAY_SW_SHORT_PRESSES | NETPLAY_SW_KICKOFF_SYNC | NETPLAY_SW_CARDS_INJURIES,
    NETPLAY_SW_DEFAULT        = NETPLAY_SW_ALL,
};
/* The switches the NEXT live match plays by, set at kick-off by the lobby on both
 * machines; -1 lets go (this machine's own settings, and the rest as this build plays). */
void        NetplayMatch_SetHostSwitches(int32_t switches);
/* What the match being played (or the next) goes by, -1 for none set. */
int32_t     NetplayMatch_HostSwitches(void);

/* GameLoop, in place of one pass of its loop: at most one frame, ended and shown. Returns a
 * GameFrameResult. */
int32_t     NetplayMatch_Frame(void);

/* A LIVE match against a real opponent (as opposed to a corpus recording played
 * through the same session). The pre-match prompts are skipped for one, the way
 * they are for the replay rig and for arcade mode. */
int32_t     NetplayMatch_IsLive(void);

/* Play a recorded match again (src/srp_resim.c): the state at frame 0 (a state_wire
 * packet), every frame's input and the SIM hash each frame started with, from a
 * resumable .SRP. The pointers must outlive the match. Then InitAndPlayGame; each frame's
 * hash is checked before it is played, and the first that differs ends it. */
/* A recording's PRED (2 bytes a frame) and SEEN, set before NetplayMatch_StartResim: with
 * the contest log on, each player's own view of every guessed frame is replayed too
 * (docs/CONTEST_LOG.md, "Views"). The stats: frames replayed for each player, how many
 * put every man and the ball where the recording says that screen drew them, and how
 * many could not be replayed. */
void        NetplayMatch_SetResimViews(uint8_t *pred, uint8_t *seen, uint32_t seenLen);
void        NetplayMatch_ResimViewStats(uint32_t frames[2], uint32_t same[2], uint32_t skipped[2]);
int32_t     NetplayMatch_StartResim(const uint8_t *kickoff, uint32_t kickoffLen, const uint8_t *inputs,
                                    const uint32_t *hashes, uint32_t frames, int32_t simWidth);
/* After it: frames played, how many started as recorded, the first that did not (-1:
 * none), and that frame's hash here and in the recording. */
void        NetplayMatch_ResimResult(uint32_t *played, uint32_t *matched, int32_t *firstDiff,
                                     uint32_t *have, uint32_t *want);

/* Replace where a live match reads its LOCAL input, for testing. NULL restores
 * the pad. The returned value is MATCH_* control bits (include/match_step.h). */
void        NetplayMatch_SetInputSource(uint8_t (*fn)(void));

/* Called when a live session finishes with every frame confirmed -- the moment
 * the state is final. A headless test reports and exits from here rather than
 * returning into the post-match screens, which wait for input. */
void        NetplayMatch_SetFinishedCallback(void (*fn)(void));

/* How the last LIVE match ended. A live match hands the machine back to the menus the
 * moment it is over -- the session closed, the pad and the timer thread back -- so the
 * fade and the screens after it run as they do after any match; this says whether it
 * finished or was cut short, and `why` (never NULL) is a line for the player then. */
typedef enum {
    NETPLAY_LIVE_NONE,
    NETPLAY_LIVE_FINISHED,          /* played to the end, every frame confirmed */
    NETPLAY_LIVE_ENDED_EARLY,       /* the other player left, the link failed, or a desync */
} NetplayLiveOutcome;
int32_t     NetplayMatch_LiveOutcome(const char **why);

/* The end of the run: report what the session did, write the trace, close the session.
 * The exit code: 0, or 2. After a live match (which has already done all that) it forgets
 * the outcome above. */
int32_t     NetplayMatch_Finish(void);

#endif /* SWOS_SDL_BUILD */
#endif /* NETPLAY_MATCH_H */
