/*
 * netplay.h -- rollback netplay for OpenSWOS (docs/NETPLAY.md).
 *
 * Built on GGPO-X (third_party/ggpo-x), the library SWOS 2020 plays online with.
 * This module is the one place the game talks to it. A session owns a GGPO session;
 * the game supplies its state save/load and a single-frame step through
 * NetplayCallbacks, and drives the session once per frame:
 *
 *     Netplay_Idle(s);
 *     if (Netplay_AddLocalInput(s, localPlayer, &input) == NETPLAY_OK &&
 *         Netplay_SyncInput(s, inputs, &disconnectFlags) == NETPLAY_OK) {
 *         step the game with inputs[];
 *         Netplay_AdvanceFrame(s, checksum);
 *     }
 *
 * During a rollback GGPO calls loadState, then advanceFrame once per frame to
 * re-simulate; advanceFrame must do the SyncInput / step / AdvanceFrame triple.
 *
 * Compiled into SDL targets with SWOS_NETPLAY (make NETPLAY=1; default on for
 * 64-bit Linux and Cygwin). Without it Netplay_Available() is 0 and every session
 * call returns NETPLAY_ERR_UNAVAILABLE.
 */
#ifndef NETPLAY_H
#define NETPLAY_H

#include "swos_types.h"

#define NETPLAY_MAX_PLAYERS 2

/* Results are GGPO's own error codes (ggponet.h, GGPOErrorCode), plus: */
#define NETPLAY_OK               0
#define NETPLAY_ERR_UNAVAILABLE  (-100)   /* built without SWOS_NETPLAY */

/* Same values as ggponet.h's GGPOEventCode. */
typedef enum {
    NETPLAY_EVENT_CONNECTED     = 1000,
    NETPLAY_EVENT_SYNCHRONIZING = 1001,
    NETPLAY_EVENT_SYNCHRONIZED  = 1002,
    NETPLAY_EVENT_RUNNING       = 1003,
    NETPLAY_EVENT_DISCONNECTED  = 1004,
    NETPLAY_EVENT_TIMESYNC      = 1005,
    NETPLAY_EVENT_INTERRUPTED   = 1006,
    NETPLAY_EVENT_RESUMED       = 1007,
    NETPLAY_EVENT_CHAT          = 1008,
    NETPLAY_EVENT_DESYNC        = 1009,
    NETPLAY_EVENT_NETWORK_ERROR = 1010,
    NETPLAY_EVENT_BALANCE       = 1011,  /* GGPO-X's prediction balance: this peer should wait */
} NetplayEventCode;

typedef struct {
    int32_t     code;            /* NetplayEventCode */
    int32_t     player;          /* player handle, for the per-player events */
    int32_t     count, total;    /* SYNCHRONIZING progress */
    int32_t     framesAhead;     /* TIMESYNC: frames this peer is ahead, rounded to nearest */
    int32_t     framesAheadMilli;/* ... in thousandths of a frame; negative when behind */
    int32_t     timeSyncPeriod;  /* ... and the frames until the next TIMESYNC */
    int32_t     desyncFrame;     /* DESYNC */
    uint16_t    localChecksum;   /* DESYNC */
    uint16_t    remoteChecksum;  /* DESYNC */
    int32_t     errorCode;       /* NETWORK_ERROR */
    const char *chat;            /* CHAT */
    int32_t     balanceWaitMilli;/* BALANCE: thousandths of a frame to wait ... */
    int32_t     balanceSpread;   /* ... spread over this many frames */
    int32_t     balanceLocalX100, balanceRemoteX100;  /* ... the two peers' average prediction depth, hundredths */
} NetplayEvent;

/* Return 0 from saveState/loadState/advanceFrame to report failure to GGPO. */
typedef struct {
    int32_t (*saveState)(void *ctx, uint8_t **state, int32_t *len, int32_t *checksum, int32_t frame);
    int32_t (*loadState)(void *ctx, const uint8_t *state, int32_t len, int32_t framesToRollback);
    void    (*freeState)(void *ctx, uint8_t *state);
    int32_t (*advanceFrame)(void *ctx);
    void    (*onEvent)(void *ctx, const NetplayEvent *event);   /* may be NULL */
    void    *ctx;
} NetplayCallbacks;

typedef struct {
    uint16_t localPort;
    int32_t  localPlayer;           /* 1 or 2 */
    char     remoteAddress[32];     /* dotted IPv4 */
    uint16_t remotePort;
    int32_t  inputSize;             /* bytes of input per player per frame */
    int32_t  frameDelay;            /* local input delay in frames */
    int32_t  maxPrediction;         /* rollback window in frames, at most 8 */
    int32_t  fps;                   /* the match's frame rate (50 Amiga, 70 DOS gameplay):
                                     * GGPO turns round trips into frames with it. 0 = 60 */
    int32_t  disconnectTimeoutMs;   /* 0 keeps GGPO's default */
    int32_t  disconnectNotifyMs;    /* 0 keeps GGPO's default */
    /* The key both peers' packets are checksummed with (ggpo_set_packet_key). With
     * hasPacketKey 0 the key is all zero: damaged packets are still dropped, forged
     * ones are not. A live match takes it from the lobby's START. */
    int32_t  hasPacketKey;
    uint8_t  packetKey[16];
} NetplayConfig;

typedef struct NetplaySession NetplaySession;

int32_t     Netplay_Available(void);
int32_t     Netplay_StartP2P(NetplaySession **out, const NetplayConfig *cfg, const NetplayCallbacks *cb);
/* Local determinism check: every frame is re-simulated checkDistance frames later
 * and the saved checksums compared. Both players are local. */
int32_t     Netplay_StartSyncTest(NetplaySession **out, int32_t inputSize, int32_t checkDistance,
                                  const NetplayCallbacks *cb);
void        Netplay_Close(NetplaySession *s);
int32_t     Netplay_Idle(NetplaySession *s);
int32_t     Netplay_AddLocalInput(NetplaySession *s, int32_t player, const void *input);
/* inputs receives inputSize bytes for each of NETPLAY_MAX_PLAYERS players. */
int32_t     Netplay_SyncInput(NetplaySession *s, void *inputs, int32_t *disconnectFlags);
int32_t     Netplay_AdvanceFrame(NetplaySession *s, uint16_t checksum);
/* The frame GGPO simulates next. */
int32_t     Netplay_CurrentFrame(NetplaySession *s, int32_t *frame);
/* The last frame whose input from every player is known, so that nothing up to it will be
 * rolled back again; -1 before any. */
int32_t     Netplay_ConfirmedFrame(NetplaySession *s, int32_t *frame);
/* How many of this peer's inputs `remotePlayer` has not acknowledged yet. */
int32_t     Netplay_UnackedInputs(NetplaySession *s, int32_t remotePlayer, int32_t *count);
/* GGPO's view of the link to `remotePlayer`: its round-trip ping in ms, and how many
 * frames this peer is ahead of the other, in tenths (negative = behind). For the log. */
/* GGPO-X predicting 1 frame until the other player's first input is confirmed (the
 * kick-off fix, ggpo_set_kickoff_prediction); on by default. Before the first input,
 * and the same on both peers. */
int32_t     Netplay_SetKickoffPrediction(NetplaySession *s, int32_t on);
int32_t     Netplay_NetStats(NetplaySession *s, int32_t remotePlayer, int32_t *pingMs,
                             int32_t *framesAheadX10);
/* Whether a refused call is only to be tried again next tick: the session is not
 * synchronized yet, or this peer is as far ahead of the other's input as GGPO allows. */
int32_t     Netplay_ShouldWait(int32_t code);
const char *Netplay_ErrorName(int32_t code);

/* Runs the library for real, without the game: a deterministic toy game through a
 * two-peer session over loopback (forcing rollbacks), then through a synctest
 * session. Prints a report; returns 0 on success. `--netplay-selftest`. */
int32_t     Netplay_SelfTest(void);

/* Hand the latency/loss emulator to GGPO's transport filter. Called from
 * Netplay_StartP2P when the environment configures one. */
void Netplay_InstallTransportFilter(void);

#endif /* NETPLAY_H */
