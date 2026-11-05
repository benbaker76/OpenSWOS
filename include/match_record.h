/*
 * match_record.h -- stream a live match to a recorder (include/record_proto.h).
 *
 * Off unless asked for. Where to, in order:
 *   SWOS_RECORD=host[:port] | server | off     the environment, for a test
 *   [netplay] record = ...                     SWOS.ini, for a player
 * "server" means the matchmaking server this build logs in to (the recorder listens
 * on its TCP port, the same number as its UDP one, default 19602). Anything else is
 * a recorder of its own at that address -- a match can be recorded while it is
 * played peer to peer.
 *
 * Never waits during play: the connection is non-blocking and bytes the recorder
 * has not taken yet are held (up to a bound, then the recording is dropped, never
 * the match). Only the END of a match may wait, briefly, to deliver the tail.
 */
#ifndef MATCH_RECORD_H
#define MATCH_RECORD_H

#include <stdint.h>

typedef struct {
    uint64_t    matchId;        /* Record_MatchId of the lobby's packet key */
    uint64_t    relayToken;     /* 0 = played peer to peer */
    int32_t     player;         /* 1 or 2 */
    int32_t     gameplay;       /* the link's gameplay flags */
    int32_t     simWidth;
    int32_t     delay;
    const char *myName, *peerName;
    const char *team1, *team2;
} MatchRecordInfo;

/* [netplay] record, published by main.c. */
void MatchRecord_SetConfigured(const char *spec);
/* [netplay] live_cast: 1 (the default) lets LIVE CAST's viewers watch this player's matches on the
 * recorder; 0 keeps them off its list (recording for desync checks goes on as before). */
void MatchRecord_SetLiveCast(int32_t on);

/* The next match was arranged by the matchmaker (its START), or not (the direct-IP menu,
 * a test's loopback pair). "server" records only matches the matchmaker arranged -- to that
 * matchmaker -- so recording on by default never sends a direct or test match to the live
 * server. An explicit host[:port] records every match. */
void MatchRecord_SetArrangedByServer(int32_t on);

/* The recorder a match would go to, as "host:port", or "" when recording is off. */
const char *MatchRecord_Target(void);

/* A live match has started. Does nothing when recording is off. */
void MatchRecord_Begin(const MatchRecordInfo *info);

/* Frame `frame` is confirmed and this was its SIM-tier hash. Frames arrive in
 * order, each once. */
void MatchRecord_Frame(uint32_t frame, uint64_t simHash);

/* A FULL recording (the default; SWOS_RECORD_MODE=hashes turns it off) also streams
 * the .SRP the game is recording (src/replay_v2.c): every replay frame up to
 * `confirmed` that has been recorded, with its sounds and chapters. Call it after
 * MatchRecord_Frame, with the same confirmed frame. */
void MatchRecord_Replay(uint32_t confirmed);

/* Send what the recorder will take. Once a frame. */
void MatchRecord_Pump(void);

/* The match is over. Delivers the tail -- waiting at most about a second -- and
 * closes. `finished` 0 = ended early. */
void MatchRecord_End(int32_t finished, int32_t endFrame, uint64_t finalSimHash, const char *why);

#endif /* MATCH_RECORD_H */
