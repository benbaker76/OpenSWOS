/*
 * record_proto.h -- a live match's recording stream, from a player to a recorder.
 *
 * Each player that is asked to (SWOS_RECORD, or [netplay] record in SWOS.ini) opens
 * a TCP connection to a recorder when a live match starts and streams it: who is
 * playing what, then the SIM-tier hash of every frame as finally played, then how
 * it ended. The recorder is the matchmaking server run with --record
 * (third_party/matchmaker/Server/Recorder.cpp) -- the same server that relays, or
 * any other one at any address -- so a match can be recorded whether it is played
 * through the relay or peer to peer.
 *
 * WHY HASHES. Both players of a match stream to the same recorder, which pairs
 * the two streams by match id and compares them frame by frame. The first frame
 * whose hashes differ is the frame the match desynced at -- found by a machine that
 * was watching both, with a wall-clock time, the two builds and platforms, and the
 * teams, written to its ledger. Until now a desync between two machines left only
 * what each side printed, every 100 frames.
 *
 * Nothing here may touch the match: the stream reads the hashes the netplay step
 * already keeps (traceSim), only for CONFIRMED frames, and never waits for the
 * network during play. A recorder that is down costs the players nothing.
 *
 * The match id is derived from the GGPO packet key the lobby's START carries, so
 * the two players arrive with the same id without the recorder -- or the
 * matchmaker -- having to hand one out. One-way (FNV-1a over a tag and the key):
 * the id is public, the key is not.
 *
 * TCP, big-endian. The stream is a sequence of messages:
 *
 *   0  1  type
 *   1  3  payload length
 *   4  n  payload
 *
 * RECORD_HELLO, first and once:
 *   0  4  "SWRC"
 *   4  1  RECORD_PROTOCOL
 *   5  1  player (1 = the host, the top team)
 *   6  1  mode (RECORD_MODE_*)
 *   7  1  the rules, as the link's handshake compares them (P2P_RULE_*): 1 DOS, 2 arcade,
 *         4 fire-hold lockout, 8 no bookings / 16 no injuries (a test build)
 *   8  8  match id
 *  16  8  relay token, 0 when played peer to peer
 *  24  2  simulation width
 *  26  1  input delay
 *  27  1  flags: RECORD_HELLO_NO_WATCH (1) this player keeps the match from LIVE CAST's viewers
 *         ([netplay] live_cast = off); 0 from a build before it
 *  28     strings, each a length byte then the bytes: build id, platform, this
 *         player's name, the other player's name, team 1, team 2
 *
 * RECORD_FRAMES:
 *   0  4  first frame
 *   4  2  count
 *   6     count x 8: each frame's SIM-tier hash, at its start, as finally played
 *
 * RECORD_END, last:
 *   0  1  outcome: 0 finished, 1 ended early
 *   1  4  the frame it ended at
 *   5  8  the final SIM-tier hash
 *  13     a string: why it ended, as the player saw it
 *
 * A FULL recording (mode RECORD_MODE_FULL) also carries the match itself -- the .SRP
 * the player's game is writing (src/replay_v2.c), confirmed frame by confirmed frame,
 * so the recorder can keep the same file (include/srp_writer.h lays out both):
 *
 * RECORD_REPLAY_META, once, before the first replay frames:
 *   0  2  tick rate (frames a second)
 *   2  2  quirks (SWRP_QUIRK_*)
 *   4     META, as the .SRP holds it
 *
 * RECORD_REPLAY_FRAMES:
 *   0  4  first frame
 *   4  2  count
 *   6     each frame's bytes as in FRMS (self-sized: a 20-byte head whose first word
 *         is its sprite count, then 6 bytes a sprite)
 *         then a 2-byte count of sound events and the events (8 bytes each, as AUDI)
 *         then a 2-byte count of chapters and the chapters (8 bytes each, as CHAP)
 *         -- the ones those frames played
 *
 *         then a 2-byte count of frame inputs (0, or one per frame above) and the
 *         inputs, SRP_INPUT_BYTES each, as INPT
 *
 * RECORD_REPLAY_SCORE, before END: the final score, 2 + 2 bytes, for META.
 *
 * RECORD_REPLAY_RESUME, once, when the recording is RESUMABLE (replay_v2.h: the match
 * can be played again exactly, from any frame) -- the parts SIMV, SETP and STAT hold:
 *   0  8  the registry layout (SimState_LayoutHash)
 *   8  1  the rules (P2P_RULE_*)
 *   9  1  the gameplay style (GAMEPLAY_*, dos_mode.h; 0 from a build before XBOX: the rules say)
 *  10  2  the simulation draw width
 *  12  4  the setup's length
 *  16     the setup (NetplaySetup_Encode), then the kick-off state (a state_wire packet)
 * The per-frame hashes HASH holds are RECORD_FRAMES', the low half of each.
 *
 * RECORD_REPLAY_PRED, with each RECORD_REPLAY_FRAMES when the player keeps what it SAW
 * ([netplay] prediction) -- its own half of PRED and SEEN (include/srp_writer.h). The
 * recorder puts both players' halves in the file; an older recorder skips the message.
 *   0  4  first frame (the batch's)
 *   4  2  count
 *   6     count PRED bytes
 *         then 4 bytes: the SEEN bytes that follow, and this player's SEEN records for
 *         those frames, as the chunk holds them
 *
 * RECORD_REPLAY_READ, after each RECORD_REPLAY_PRED -- this player's half of READ, what
 * its game read from its controls on those frames and when (include/srp_writer.h). An
 * older recorder skips the message.
 *   0  4  first frame (the batch's)
 *   4  2  count
 *   6  1  the input delay
 *   7  1  1: this player has the prediction balance on
 *   8     count * SRP_READ_BYTES
 */
#ifndef RECORD_PROTO_H
#define RECORD_PROTO_H

#include <stdint.h>
#include <string.h>

#define RECORD_MAGIC         "SWRC"
#define RECORD_PROTOCOL      1
#define RECORD_HEADER_LEN    4
#define RECORD_MAX_PAYLOAD   (1u << 20)

#define RECORD_HELLO         1
#define RECORD_FRAMES        2
#define RECORD_END           3
#define RECORD_REPLAY_META   4
#define RECORD_REPLAY_FRAMES 5
#define RECORD_REPLAY_SCORE  6
#define RECORD_REPLAY_RESUME 7
#define RECORD_REPLAY_PRED   8
#define RECORD_REPLAY_READ   9

/* LIVE CAST (SWOS 2020's "watch live matches"; docs/NETPLAY.md §7 "Watching a match live"): a
 * VIEWER connects to the recorder's port and asks, the first message it sends --
 *
 * RECORD_WATCH_LIST: "SWRV", RECORD_PROTOCOL. The answer, RECORD_WATCH_MATCHES:
 *   0  2  count, then each: match id (8), frames recorded so far (4), then strings (a length
 *         byte, the bytes): player 1, player 2, team 1, team 2
 * and the recorder closes the connection.
 *
 * RECORD_WATCH: "SWRV", RECORD_PROTOCOL, the match id (8; 0 = the one recorded the longest).
 * The answer is the match as a player streams it -- RECORD_REPLAY_META, then
 * RECORD_REPLAY_FRAMES (frames, sounds, chapters; no inputs) from frame 0, then each new batch
 * as it arrives, RECORD_REPLAY_SCORE and RECORD_END at the end -- or RECORD_END at once
 * (outcome 2: no such match). A viewer sends nothing more; the stream is the recorder's copy,
 * so it never touches the players or the match. */
#define RECORD_WATCH_LIST    10
#define RECORD_WATCH_MATCHES 11
#define RECORD_WATCH         12
#define RECORD_VIEWER_MAGIC  "SWRV"

#define RECORD_MODE_HASHES   0     /* the frames' hashes: enough to find a desync */
#define RECORD_MODE_FULL     1     /* ... and the match itself, as a .SRP */

#define RECORD_HELLO_FIXED   28    /* before the strings */
#define RECORD_HELLO_NO_WATCH 0x01 /* HELLO byte 27 */
#define RECORD_BATCH_MAX     1024  /* frames per RECORD_FRAMES */

static inline void Record_Put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void Record_Put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline void Record_Put64(uint8_t *p, uint64_t v)
{
    Record_Put32(p, (uint32_t)(v >> 32));
    Record_Put32(p + 4, (uint32_t)v);
}
static inline uint32_t Record_Get16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static inline uint32_t Record_Get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t Record_Get64(const uint8_t *p)
{
    return ((uint64_t)Record_Get32(p) << 32) | Record_Get32(p + 4);
}

/* A message header: type, then a 24-bit length. */
static inline void Record_PutHeader(uint8_t *p, uint8_t type, uint32_t len)
{
    p[0] = type;
    p[1] = (uint8_t)(len >> 16); p[2] = (uint8_t)(len >> 8); p[3] = (uint8_t)len;
}

/* The match id both players compute from the packet key the lobby's START gave
 * them. FNV-1a 64 over "SWRC" and the key; never 0. */
static inline uint64_t Record_MatchId(const uint8_t key[16])
{
    uint64_t h = 1469598103934665603ULL;
    int i;
    for (i = 0; i < 4; i++) { h ^= (uint8_t)RECORD_MAGIC[i]; h *= 1099511628211ULL; }
    for (i = 0; i < 16; i++) { h ^= key[i]; h *= 1099511628211ULL; }
    return h ? h : 1;
}

#endif /* RECORD_PROTO_H */
