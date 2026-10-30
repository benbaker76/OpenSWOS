/*
 * netplay.c -- rollback netplay on GGPO-X. See include/netplay.h and docs/NETPLAY.md.
 */
#include "netplay.h"
#include "netplay_netem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_NETPLAY

#include <SDL.h>        /* SDL_Delay in the self-test */
#include "ggponet.h"

#include <unistd.h>         /* close -- the self-test's port probe */
#include <sys/socket.h>
#include <netinet/in.h>

_Static_assert(NETPLAY_EVENT_RUNNING == (int)GGPO_EVENTCODE_RUNNING &&
               NETPLAY_EVENT_NETWORK_ERROR == (int)GGPO_EVENTCODE_NETWORK_ERROR,
               "NetplayEventCode must mirror GGPOEventCode");

struct NetplaySession {
    GGPOSession          *ggpo;
    GGPOSessionCallbacks  ggpoCallbacks;    /* context = this session */
    NetplayCallbacks      callbacks;
    GGPOPlayerHandle      handles[NETPLAY_MAX_PLAYERS];
    int32_t               inputSize;
};

/* ---- GGPO callbacks -> NetplayCallbacks. GGPO copies the callbacks struct into
 *      its backend, so `context` must be the heap session, not a local. ---- */
static bool __cdecl begin_game(void *context, const char *game)
{
    (void)context; (void)game;   /* deprecated by GGPO, still called */
    return true;
}

static bool __cdecl save_game_state(void *context, unsigned char **buffer, int *len,
                                    int *checksum, int frame)
{
    NetplaySession *s = (NetplaySession *)context;
    uint8_t *state = NULL;
    int32_t stateLen = 0, sum = 0;
    if (!s->callbacks.saveState(s->callbacks.ctx, &state, &stateLen, &sum, frame))
        return false;
    *buffer = state;
    *len = stateLen;
    *checksum = sum;
    return true;
}

static bool __cdecl load_game_state(void *context, unsigned char *buffer, int len, int framesToRollback)
{
    NetplaySession *s = (NetplaySession *)context;
    return s->callbacks.loadState(s->callbacks.ctx, buffer, len, framesToRollback) != 0;
}

static bool __cdecl log_game_state(void *context, char *filename, unsigned char *buffer, int len)
{
    (void)context; (void)filename; (void)buffer; (void)len;
    return true;
}

static void __cdecl free_buffer(void *context, void *buffer)
{
    NetplaySession *s = (NetplaySession *)context;
    s->callbacks.freeState(s->callbacks.ctx, (uint8_t *)buffer);
}

static bool __cdecl advance_frame(void *context, int flags)
{
    NetplaySession *s = (NetplaySession *)context;
    (void)flags;
    return s->callbacks.advanceFrame(s->callbacks.ctx) != 0;
}

static bool __cdecl on_event(void *context, GGPOEvent *info)
{
    NetplaySession *s = (NetplaySession *)context;
    NetplayEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.code = (int32_t)info->code;
    switch (info->code) {
    case GGPO_EVENTCODE_CONNECTED_TO_PEER:     ev.player = info->u.connected.player; break;
    case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
        ev.player = info->u.synchronizing.player;
        ev.count = info->u.synchronizing.count;
        ev.total = info->u.synchronizing.total;
        break;
    case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER: ev.player = info->u.synchronized.player; break;
    case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER: ev.player = info->u.disconnected.player; break;
    case GGPO_EVENTCODE_TIMESYNC: {
        /* Round to nearest both ways: +0.5 then truncating sent -0.7 to 0. */
        float ahead = info->u.timesync.frames_ahead;
        ev.framesAhead = (int32_t)(ahead < 0 ? ahead - 0.5f : ahead + 0.5f);
        ev.framesAheadMilli = (int32_t)(ahead * 1000.0f);
        ev.timeSyncPeriod = info->u.timesync.timeSyncPeriodInFrames;
        break;
    }
    case GGPO_EVENTCODE_CONNECTION_INTERRUPTED: ev.player = info->u.connection_interrupted.player; break;
    case GGPO_EVENTCODE_CONNECTION_RESUMED:     ev.player = info->u.connection_resumed.player; break;
    case GGPO_EVENTCODE_CHAT:                  ev.chat = info->u.chat.msg; break;
    case GGPO_EVENTCODE_PREDICTION_BALANCE:
        ev.balanceWaitMilli = (int32_t)(info->u.prediction_balance.frames_to_wait * 1000.0f);
        ev.balanceSpread = info->u.prediction_balance.spread_in_frames;
        ev.balanceLocalX100 = (int32_t)(info->u.prediction_balance.local_depth * 100.0f + 0.5f);
        ev.balanceRemoteX100 = (int32_t)(info->u.prediction_balance.remote_depth * 100.0f + 0.5f);
        break;
    case GGPO_EVENTCODE_DESYNC:
        ev.desyncFrame = info->u.desync.nFrameOfDesync;
        ev.localChecksum = info->u.desync.ourCheckSum;
        ev.remoteChecksum = info->u.desync.remoteChecksum;
        break;
    case GGPO_EVENTCODE_NETWORK_ERROR:         ev.errorCode = info->u.network_error.errorCode; break;
    default: break;
    }
    if (s->callbacks.onEvent)
        s->callbacks.onEvent(s->callbacks.ctx, &ev);
    return true;
}

static NetplaySession *session_new(const NetplayCallbacks *cb, int32_t inputSize)
{
    NetplaySession *s = (NetplaySession *)calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->callbacks = *cb;
    s->inputSize = inputSize;
    s->ggpoCallbacks.begin_game = begin_game;
    s->ggpoCallbacks.save_game_state = save_game_state;
    s->ggpoCallbacks.load_game_state = load_game_state;
    s->ggpoCallbacks.log_game_state = log_game_state;
    s->ggpoCallbacks.free_buffer = free_buffer;
    s->ggpoCallbacks.advance_frame = advance_frame;
    s->ggpoCallbacks.on_event = on_event;
    s->ggpoCallbacks.context = s;
    return s;
}

int32_t Netplay_Available(void)
{
    return 1;
}

int32_t Netplay_StartP2P(NetplaySession **out, const NetplayConfig *cfg, const NetplayCallbacks *cb)
{
    /* Latency / packet-loss emulation, when the environment asks for it
     * (include/netplay_netem.h). Installed through GGPO's generic transport
     * filter, so the library carries no dependency on us -- see
     * third_party/ggpo-x/OPENSWOS.md. No cost when it is off. */
    Netem_Init();
    if (Netem_Enabled())
        Netplay_InstallTransportFilter();

    *out = NULL;
    if (cfg->localPlayer < 1 || cfg->localPlayer > NETPLAY_MAX_PLAYERS)
        return GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE;
    NetplaySession *s = session_new(cb, cfg->inputSize);
    if (!s)
        return GGPO_ERRORCODE_GENERAL_FAILURE;

    GGPOErrorCode rc = ggpo_start_session(&s->ggpo, &s->ggpoCallbacks, "openswos",
                                          NETPLAY_MAX_PLAYERS, cfg->inputSize,
                                          cfg->localPort, cfg->maxPrediction,
                                          cfg->fps > 0 ? (float)cfg->fps : 60.0f);
    if (!GGPO_SUCCEEDED(rc)) {
        free(s);
        return rc;
    }
    /* Before any player is added: adding the remote one starts the sync, and every
     * packet from then on is checksummed with the key. */
    if (cfg->hasPacketKey)
        ggpo_set_packet_key(s->ggpo, cfg->packetKey);
    if (cfg->disconnectTimeoutMs)
        ggpo_set_disconnect_timeout(s->ggpo, cfg->disconnectTimeoutMs);
    if (cfg->disconnectNotifyMs)
        ggpo_set_disconnect_notify_start(s->ggpo, cfg->disconnectNotifyMs);

    for (int32_t n = 1; n <= NETPLAY_MAX_PLAYERS; n++) {
        GGPOPlayer p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.player_num = n;
        if (n == cfg->localPlayer) {
            p.type = GGPO_PLAYERTYPE_LOCAL;
        } else {
            p.type = GGPO_PLAYERTYPE_REMOTE;
            snprintf(p.u.remote.ip_address, sizeof p.u.remote.ip_address, "%s", cfg->remoteAddress);
            p.u.remote.port = cfg->remotePort;
        }
        rc = ggpo_add_player(s->ggpo, &p, &s->handles[n - 1]);
        if (!GGPO_SUCCEEDED(rc)) {
            Netplay_Close(s);
            return rc;
        }
    }
    if (cfg->frameDelay)
        ggpo_set_frame_delay(s->ggpo, s->handles[cfg->localPlayer - 1], cfg->frameDelay);

    *out = s;
    return NETPLAY_OK;
}

int32_t Netplay_StartSyncTest(NetplaySession **out, int32_t inputSize, int32_t checkDistance,
                              const NetplayCallbacks *cb)
{
    *out = NULL;
    NetplaySession *s = session_new(cb, inputSize);
    if (!s)
        return GGPO_ERRORCODE_GENERAL_FAILURE;

    char game[] = "openswos";
    GGPOErrorCode rc = ggpo_start_synctest(&s->ggpo, &s->ggpoCallbacks, game,
                                           NETPLAY_MAX_PLAYERS, inputSize, checkDistance);
    if (!GGPO_SUCCEEDED(rc)) {
        free(s);
        return rc;
    }
    for (int32_t n = 1; n <= NETPLAY_MAX_PLAYERS; n++) {
        GGPOPlayer p;
        memset(&p, 0, sizeof p);
        p.size = sizeof p;
        p.player_num = n;
        p.type = GGPO_PLAYERTYPE_LOCAL;
        rc = ggpo_add_player(s->ggpo, &p, &s->handles[n - 1]);
        if (!GGPO_SUCCEEDED(rc)) {
            Netplay_Close(s);
            return rc;
        }
    }
    *out = s;
    return NETPLAY_OK;
}

void Netplay_Close(NetplaySession *s)
{
    if (!s)
        return;
    if (s->ggpo)
        ggpo_close_session(s->ggpo);
    free(s);
}

int32_t Netplay_Idle(NetplaySession *s)
{
    return ggpo_idle(s->ggpo);
}

int32_t Netplay_AddLocalInput(NetplaySession *s, int32_t player, const void *input)
{
    if (player < 1 || player > NETPLAY_MAX_PLAYERS)
        return GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE;
    return ggpo_add_local_input(s->ggpo, s->handles[player - 1], (void *)input, s->inputSize);
}

int32_t Netplay_SyncInput(NetplaySession *s, void *inputs, int32_t *disconnectFlags)
{
    int flags = 0;
    GGPOErrorCode rc = ggpo_synchronize_input(s->ggpo, inputs, s->inputSize * NETPLAY_MAX_PLAYERS, &flags);
    if (disconnectFlags)
        *disconnectFlags = flags;
    return rc;
}

int32_t Netplay_AdvanceFrame(NetplaySession *s, uint16_t checksum)
{
    return ggpo_advance_frame(s->ggpo, checksum);
}

int32_t Netplay_CurrentFrame(NetplaySession *s, int32_t *frame)
{
    int f = 0;
    GGPOErrorCode rc = ggpo_get_current_frame(s->ggpo, &f);
    *frame = f;
    return rc;
}

int32_t Netplay_ConfirmedFrame(NetplaySession *s, int32_t *frame)
{
    int f = -1;
    GGPOErrorCode rc = ggpo_get_confirmed_frame(s->ggpo, &f);
    *frame = f;
    return rc;
}

int32_t Netplay_UnackedInputs(NetplaySession *s, int32_t remotePlayer, int32_t *count)
{
    GGPONetworkStats stats;
    GGPOErrorCode rc;
    *count = 0;
    if (remotePlayer < 1 || remotePlayer > NETPLAY_MAX_PLAYERS)
        return GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE;
    rc = ggpo_get_network_stats(s->ggpo, s->handles[remotePlayer - 1], &stats);
    if (GGPO_SUCCEEDED(rc))
        *count = stats.network.send_queue_len;
    return rc;
}

int32_t Netplay_SetKickoffPrediction(NetplaySession *s, int32_t on)
{
    if (!s || !s->ggpo)
        return NETPLAY_ERR_UNAVAILABLE;
    return (int32_t)ggpo_set_kickoff_prediction(s->ggpo, on);
}

int32_t Netplay_NetStats(NetplaySession *s, int32_t remotePlayer, int32_t *pingMs,
                         int32_t *framesAheadX10)
{
    GGPONetworkStats stats;
    GGPOErrorCode rc;
    *pingMs = -1;
    *framesAheadX10 = 0;
    if (remotePlayer < 1 || remotePlayer > NETPLAY_MAX_PLAYERS)
        return GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE;
    rc = ggpo_get_network_stats(s->ggpo, s->handles[remotePlayer - 1], &stats);
    if (GGPO_SUCCEEDED(rc)) {
        *pingMs = stats.network.ping;
        *framesAheadX10 = (int32_t)(stats.timesync.local_frames_behind * -10.0f);
    }
    return rc;
}

int32_t Netplay_ShouldWait(int32_t code)
{
    return code == GGPO_ERRORCODE_NOT_SYNCHRONIZED || code == GGPO_ERRORCODE_PREDICTION_THRESHOLD;
}

const char *Netplay_ErrorName(int32_t code)
{
    switch (code) {
    case GGPO_OK:                                  return "OK";
    case GGPO_ERRORCODE_GENERAL_FAILURE:           return "GENERAL_FAILURE";
    case GGPO_ERRORCODE_INVALID_SESSION:           return "INVALID_SESSION";
    case GGPO_ERRORCODE_INVALID_PLAYER_HANDLE:     return "INVALID_PLAYER_HANDLE";
    case GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE:       return "PLAYER_OUT_OF_RANGE";
    case GGPO_ERRORCODE_PREDICTION_THRESHOLD:      return "PREDICTION_THRESHOLD";
    case GGPO_ERRORCODE_UNSUPPORTED:               return "UNSUPPORTED";
    case GGPO_ERRORCODE_NOT_SYNCHRONIZED:          return "NOT_SYNCHRONIZED";
    case GGPO_ERRORCODE_IN_ROLLBACK:               return "IN_ROLLBACK";
    case GGPO_ERRORCODE_INPUT_DROPPED:             return "INPUT_DROPPED";
    case GGPO_ERRORCODE_PLAYER_DISCONNECTED:       return "PLAYER_DISCONNECTED";
    case GGPO_ERRORCODE_TOO_MANY_SPECTATORS:       return "TOO_MANY_SPECTATORS";
    case GGPO_ERRORCODE_INVALID_REQUEST:           return "INVALID_REQUEST";
    case GGPO_ERRORCODE_NETWORK_ERROR:             return "NETWORK_ERROR (could not open the UDP port)";
    case GGPO_CHAT_MESSAGE_TOO_LONG:               return "CHAT_MESSAGE_TOO_LONG";
    case NETPLAY_ERR_UNAVAILABLE:                  return "UNAVAILABLE";
    default:                                       return "?";
    }
}

/* ======================================================================
 * Self-test: a toy deterministic game. Its whole state is a frame counter and a
 * hash of every input it has consumed, so two peers agree on frame N exactly when
 * they simulated it from the same inputs -- which is what rollback must ensure.
 * ====================================================================== */
enum { TOY_FRAMES = 600 };

typedef struct {
    int32_t  frame;
    uint32_t hash;
} ToyState;

typedef struct {
    const char     *name;
    NetplaySession *session;
    int32_t         player;
    ToyState        state;
    int32_t         running;
    int32_t         saves, loads, rolledBack, desyncs, disconnects;
    uint32_t        history[TOY_FRAMES];   /* hash after each frame; a rollback rewrites it */
} ToyGame;

/* Changes every few frames, differently per player, so the other peer's "repeat
 * the last input" prediction keeps being wrong and rollbacks keep happening. */
static uint8_t toy_input(int32_t player, int32_t frame)
{
    return (uint8_t)(((frame / (2 + player)) * 37 + player * 11) & 0x3F);
}

static uint16_t toy_checksum(const ToyState *st)
{
    return (uint16_t)(st->hash ^ (st->hash >> 16) ^ (uint32_t)st->frame);
}

static void toy_step(ToyGame *g)
{
    uint8_t inputs[NETPLAY_MAX_PLAYERS];
    int32_t flags = 0;
    Netplay_SyncInput(g->session, inputs, &flags);
    g->state.hash = g->state.hash * 2654435761u + (uint32_t)inputs[0] * 7u
                  + (uint32_t)inputs[1] * 131u + (uint32_t)g->state.frame;
    g->state.frame++;
    if (g->state.frame <= TOY_FRAMES)
        g->history[g->state.frame - 1] = g->state.hash;
    Netplay_AdvanceFrame(g->session, toy_checksum(&g->state));
}

static int32_t toy_save(void *ctx, uint8_t **state, int32_t *len, int32_t *checksum, int32_t frame)
{
    ToyGame *g = (ToyGame *)ctx;
    uint8_t *copy = (uint8_t *)malloc(sizeof g->state);
    (void)frame;
    if (!copy)
        return 0;
    memcpy(copy, &g->state, sizeof g->state);
    *state = copy;
    *len = (int32_t)sizeof g->state;
    *checksum = toy_checksum(&g->state);
    g->saves++;
    return 1;
}

static int32_t toy_load(void *ctx, const uint8_t *state, int32_t len, int32_t framesToRollback)
{
    ToyGame *g = (ToyGame *)ctx;
    if (len != (int32_t)sizeof g->state)
        return 0;
    memcpy(&g->state, state, sizeof g->state);
    g->loads++;
    g->rolledBack += framesToRollback;
    return 1;
}

static void toy_free(void *ctx, uint8_t *state)
{
    (void)ctx;
    free(state);
}

static int32_t toy_advance(void *ctx)
{
    toy_step((ToyGame *)ctx);
    return 1;
}

static void toy_event(void *ctx, const NetplayEvent *ev)
{
    ToyGame *g = (ToyGame *)ctx;
    switch (ev->code) {
    case NETPLAY_EVENT_RUNNING:
        g->running = 1;
        break;
    case NETPLAY_EVENT_DESYNC:
        g->desyncs++;
        printf("  %s: DESYNC at frame %d (local %04X, remote %04X)\n",
               g->name, ev->desyncFrame, ev->localChecksum, ev->remoteChecksum);
        break;
    case NETPLAY_EVENT_DISCONNECTED:
        g->disconnects++;
        break;
    default:
        break;
    }
}

/* One frame of the game loop: pump the network, then advance if GGPO allows it. */
static void toy_tick(ToyGame *g, int32_t bothPlayersLocal)
{
    Netplay_Idle(g->session);
    if (!g->running || g->state.frame >= TOY_FRAMES)
        return;
    for (int32_t p = 1; p <= NETPLAY_MAX_PLAYERS; p++) {
        if (!bothPlayersLocal && p != g->player)
            continue;
        uint8_t in = toy_input(p, g->state.frame);
        if (Netplay_AddLocalInput(g->session, p, &in) != NETPLAY_OK)
            return;    /* e.g. PREDICTION_THRESHOLD: too far ahead of the peer, wait */
    }
    toy_step(g);
}

static void toy_init(ToyGame *g, const char *name, int32_t player, NetplayCallbacks *cb)
{
    memset(g, 0, sizeof *g);
    g->name = name;
    g->player = player;
    cb->saveState = toy_save;
    cb->loadState = toy_load;
    cb->freeState = toy_free;
    cb->advanceFrame = toy_advance;
    cb->onEvent = toy_event;
    cb->ctx = g;
}

/* Two UDP ports nothing is using right now, from the system. Not fixed ones: the
 * 47901/47902 this used to take sit inside the 44620-48715 block that WSL2's mirrored
 * networking reserves on the Windows side, so there GGPO could not bind either. Both
 * probes stay open until both have a port, so the two differ. */
static int32_t free_udp_ports(uint16_t *portA, uint16_t *portB)
{
    int32_t socks[2] = { -1, -1 }, ok = 1;
    uint16_t ports[2] = { 0, 0 };
    for (int32_t i = 0; i < 2 && ok; i++) {
        struct sockaddr_in sin;
        socklen_t len = (socklen_t)sizeof sin;
        memset(&sin, 0, sizeof sin);
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        socks[i] = (int32_t)socket(AF_INET, SOCK_DGRAM, 0);
        ok = socks[i] >= 0 &&
             bind(socks[i], (struct sockaddr *)&sin, (socklen_t)sizeof sin) == 0 &&
             getsockname(socks[i], (struct sockaddr *)&sin, &len) == 0;
        if (ok)
            ports[i] = ntohs(sin.sin_port);
    }
    for (int32_t i = 0; i < 2; i++)
        if (socks[i] >= 0)
            close(socks[i]);
    *portA = ports[0];
    *portB = ports[1];
    return ok;
}

static int32_t selftest_p2p(void)
{
    static ToyGame a, b;
    NetplayCallbacks cbA, cbB;
    toy_init(&a, "peer A", 1, &cbA);
    toy_init(&b, "peer B", 2, &cbB);

    uint16_t portA, portB;
    if (!free_udp_ports(&portA, &portB)) {
        printf("  FAIL: the system has no free UDP port for the loopback peers\n");
        return 1;
    }

    NetplayConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.inputSize = 1;
    cfg.maxPrediction = 8;
    cfg.hasPacketKey = 1;                       /* the same key on both: they must play */
    memcpy(cfg.packetKey, "toy-game-key-16b", sizeof cfg.packetKey);
    snprintf(cfg.remoteAddress, sizeof cfg.remoteAddress, "127.0.0.1");

    cfg.localPort = portA; cfg.localPlayer = 1; cfg.remotePort = portB;
    int32_t rc = Netplay_StartP2P(&a.session, &cfg, &cbA);
    if (rc != NETPLAY_OK) {
        printf("  start peer A: %s\n", Netplay_ErrorName(rc));
        return 1;
    }
    cfg.localPort = portB; cfg.localPlayer = 2; cfg.remotePort = portA;
    rc = Netplay_StartP2P(&b.session, &cfg, &cbB);
    if (rc != NETPLAY_OK) {
        printf("  start peer B: %s\n", Netplay_ErrorName(rc));
        Netplay_Close(a.session);
        return 1;
    }

    /* Tick the two peers alternately, ~1 ms apart. A always simulates first and
     * so has to predict B's input; B's real input arrives on A's next idle. Keep
     * idling after both finish so the last predicted frames get corrected. */
    int32_t settle = 0, iter;
    for (iter = 0; iter < 30000 && settle < 300; iter++) {
        toy_tick(&a, 0);
        toy_tick(&b, 0);
        if (a.state.frame >= TOY_FRAMES && b.state.frame >= TOY_FRAMES)
            settle++;
        SDL_Delay(1);
    }

    int32_t mismatches = 0, firstBad = -1;
    for (int32_t f = 0; f < TOY_FRAMES; f++) {
        if (a.history[f] != b.history[f]) {
            if (firstBad < 0) firstBad = f;
            mismatches++;
        }
    }
    printf("  p2p over loopback (UDP %u/%u): A frame %d, B frame %d after %d ticks; "
           "rollbacks A %d (%d frames) / B %d (%d frames); desyncs %d\n",
           (unsigned)portA, (unsigned)portB,
           a.state.frame, b.state.frame, iter, a.loads, a.rolledBack, b.loads, b.rolledBack,
           a.desyncs + b.desyncs);
    int32_t fail = 0;
    if (a.state.frame < TOY_FRAMES || b.state.frame < TOY_FRAMES) {
        printf("  FAIL: a peer did not reach frame %d (running A=%d B=%d)\n",
               TOY_FRAMES, a.running, b.running);
        fail = 1;
    }
    if (mismatches) {
        printf("  FAIL: %d of %d frames differ between the peers, first at %d\n",
               mismatches, TOY_FRAMES, firstBad);
        fail = 1;
    }
    if (a.loads + b.loads == 0) {
        printf("  FAIL: no rollback happened, so rollback was not tested\n");
        fail = 1;
    }
    if (a.desyncs + b.desyncs || a.disconnects + b.disconnects) {
        printf("  FAIL: %d desync / %d disconnect events\n",
               a.desyncs + b.desyncs, a.disconnects + b.disconnects);
        fail = 1;
    }
    Netplay_Close(a.session);
    Netplay_Close(b.session);
    return fail;
}

/* Two peers with DIFFERENT packet keys: every packet each one sends fails the other's
 * checksum, so they must never even connect. This is what stops a forged packet -- one
 * made without the key -- from being read at all. */
static int32_t selftest_wrong_key(void)
{
    static ToyGame a, b;
    NetplayCallbacks cbA, cbB;
    NetplayConfig cfg;
    uint16_t portA, portB;
    int32_t rc, iter;

    toy_init(&a, "keyed A", 1, &cbA);
    toy_init(&b, "keyed B", 2, &cbB);
    if (!free_udp_ports(&portA, &portB)) {
        printf("  FAIL: no free UDP port for the key test\n");
        return 1;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.inputSize = 1;
    cfg.maxPrediction = 8;
    cfg.hasPacketKey = 1;
    snprintf(cfg.remoteAddress, sizeof cfg.remoteAddress, "127.0.0.1");

    memcpy(cfg.packetKey, "key-of-player-A!", sizeof cfg.packetKey);
    cfg.localPort = portA; cfg.localPlayer = 1; cfg.remotePort = portB;
    rc = Netplay_StartP2P(&a.session, &cfg, &cbA);
    if (rc != NETPLAY_OK) {
        printf("  start keyed A: %s\n", Netplay_ErrorName(rc));
        return 1;
    }
    memcpy(cfg.packetKey, "key-of-player-B!", sizeof cfg.packetKey);
    cfg.localPort = portB; cfg.localPlayer = 2; cfg.remotePort = portA;
    rc = Netplay_StartP2P(&b.session, &cfg, &cbB);
    if (rc != NETPLAY_OK) {
        printf("  start keyed B: %s\n", Netplay_ErrorName(rc));
        Netplay_Close(a.session);
        return 1;
    }
    /* Long enough for several sync retries, which with one key would connect in ms. */
    for (iter = 0; iter < 1500; iter++) {
        toy_tick(&a, 0);
        toy_tick(&b, 0);
        SDL_Delay(1);
    }
    printf("  peers with different packet keys: running A=%d B=%d (both must be 0)\n",
           a.running, b.running);
    Netplay_Close(a.session);
    Netplay_Close(b.session);
    if (a.running || b.running) {
        printf("  FAIL: a packet made with the wrong key was accepted\n");
        return 1;
    }
    return 0;
}

static int32_t selftest_synctest(void)
{
    static ToyGame g;
    NetplayCallbacks cb;
    toy_init(&g, "synctest", 1, &cb);
    int32_t rc = Netplay_StartSyncTest(&g.session, 1, 1, &cb);
    if (rc != NETPLAY_OK) {
        printf("  start synctest: %s\n", Netplay_ErrorName(rc));
        return 1;
    }
    for (int32_t i = 0; i < TOY_FRAMES * 2 && g.state.frame < TOY_FRAMES; i++)
        toy_tick(&g, 1);
    /* A checksum mismatch never gets here: GGPO's RaiseSyncError aborts the process. */
    printf("  synctest: %d frames, %d saves, %d re-simulations\n", g.state.frame, g.saves, g.loads);
    int32_t fail = g.state.frame < TOY_FRAMES || g.loads == 0;
    if (fail)
        printf("  FAIL: synctest did not run to frame %d with re-simulation\n", TOY_FRAMES);
    Netplay_Close(g.session);
    return fail;
}

int32_t Netplay_SelfTest(void)
{
    printf("[netplay] self-test: GGPO-X, 2 players, %d frames\n", TOY_FRAMES);
    int32_t fail = selftest_p2p();
    fail |= selftest_wrong_key();
    fail |= selftest_synctest();
    printf("[netplay] self-test %s\n", fail ? "FAILED" : "passed");
    fflush(stdout);
    return fail;
}

#else /* !SWOS_NETPLAY */

int32_t Netplay_Available(void) { return 0; }

int32_t Netplay_StartP2P(NetplaySession **out, const NetplayConfig *cfg, const NetplayCallbacks *cb)
{
    (void)cfg; (void)cb;
    *out = NULL;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_StartSyncTest(NetplaySession **out, int32_t inputSize, int32_t checkDistance,
                              const NetplayCallbacks *cb)
{
    (void)inputSize; (void)checkDistance; (void)cb;
    *out = NULL;
    return NETPLAY_ERR_UNAVAILABLE;
}

void Netplay_Close(NetplaySession *s) { (void)s; }
int32_t Netplay_Idle(NetplaySession *s) { (void)s; return NETPLAY_ERR_UNAVAILABLE; }

int32_t Netplay_AddLocalInput(NetplaySession *s, int32_t player, const void *input)
{
    (void)s; (void)player; (void)input;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_SyncInput(NetplaySession *s, void *inputs, int32_t *disconnectFlags)
{
    (void)s; (void)inputs; (void)disconnectFlags;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_AdvanceFrame(NetplaySession *s, uint16_t checksum)
{
    (void)s; (void)checksum;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_CurrentFrame(NetplaySession *s, int32_t *frame)
{
    (void)s;
    *frame = 0;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_ConfirmedFrame(NetplaySession *s, int32_t *frame)
{
    (void)s;
    *frame = -1;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_UnackedInputs(NetplaySession *s, int32_t remotePlayer, int32_t *count)
{
    (void)s; (void)remotePlayer;
    *count = 0;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_SetKickoffPrediction(NetplaySession *s, int32_t on) { (void)s; (void)on; return NETPLAY_ERR_UNAVAILABLE; }

int32_t Netplay_NetStats(NetplaySession *s, int32_t remotePlayer, int32_t *pingMs,
                         int32_t *framesAheadX10)
{
    (void)s; (void)remotePlayer;
    *pingMs = -1;
    *framesAheadX10 = 0;
    return NETPLAY_ERR_UNAVAILABLE;
}

int32_t Netplay_ShouldWait(int32_t code) { (void)code; return 0; }

const char *Netplay_ErrorName(int32_t code)
{
    return code == NETPLAY_ERR_UNAVAILABLE ? "UNAVAILABLE" : "?";
}

int32_t Netplay_SelfTest(void)
{
    printf("[netplay] not built into this binary (make NETPLAY=1)\n");
    return 1;
}

#endif /* SWOS_NETPLAY */
