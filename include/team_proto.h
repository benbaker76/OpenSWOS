/*
 * team_proto.h -- the team check: before an online match, each game asks the
 * matchmaking server whether the two teams are canonical. docs/TEAM_IDS.md §5.
 * The game's half is src/netplay_lobby.c (through src/p2p_link.c), the server's
 * third_party/matchmaker/Server/TeamDesk.cpp.
 *
 * A team is named by its id and held to be certain data by its checksum
 * (src/team_db.c). The server has the list of both that no player can edit. Each game
 * asks about BOTH teams -- its own and the one the other player sent it, each as the
 * (id, checksum) of what it would actually play -- so a player who sends a doctored
 * team is found out by the honest game that received it, whatever their own says.
 *
 * One datagram each way on the server's UDP port, shared with the matchmaker and the
 * relay as the relay's control packets are (told apart by the magic and the exact
 * length). Stateless: the answer is the question with a status per team. Big-endian.
 *
 *   0   4  "SWTC"
 *   4   1  TEAMCHECK_PROTOCOL
 *   5   1  type        TEAMCHECK_ASK / TEAMCHECK_ANSWER
 *   6   1  count       teams that follow, 1 or 2
 *   7   1  0
 *   8   4  nonce       the asker's, echoed
 *  12      per team, 25 bytes:
 *            16  the team's id (a UUID's sixteen bytes)
 *             8  the checksum (the eight bytes its sixteen hex digits spell)
 *             1  status      0 in an ASK; in an ANSWER one of TEAMCHECK_OK / _UNKNOWN / _CHANGED
 *
 * THE PLAYERS 17.. (since 2026-11-13, docs/TEAM_IDS.md §5): a world's long bench brings a club's
 * players 17.. (its "extra_players") on, and the checksum above is of its sixteen. A game whose
 * team has them asks a second packet beside the first, the same layout with byte 4 =
 * TEAMCHECK_PROTOCOL_EXTRAS and each checksum the one of the team's players 17.. in their order
 * (TeamDb_ChecksumPlayers). A server that knows them answers it in kind: OK, CHANGED, or UNKNOWN
 * for a team its list has no players 17.. for (an older list: unverified, never refused). A
 * server from before reads only protocol 1, and drops the second packet unanswered: the game then
 * plays the players 17.. unverified, as it plays any team when there is no server.
 */
#ifndef TEAM_PROTO_H
#define TEAM_PROTO_H

#include <stdint.h>
#include <string.h>

#define TEAMCHECK_MAGIC      "SWTC"
#define TEAMCHECK_PROTOCOL   1
#define TEAMCHECK_PROTOCOL_EXTRAS 2     /* the same packet, of the teams' players 17.. */
#define TEAMCHECK_ASK        1
#define TEAMCHECK_ANSWER     2
#define TEAMCHECK_MAX_TEAMS  2
#define TEAMCHECK_HEAD       12
#define TEAMCHECK_TEAM       25
#define TEAMCHECK_MAX_LEN    (TEAMCHECK_HEAD + TEAMCHECK_MAX_TEAMS * TEAMCHECK_TEAM)

#define TEAMCHECK_OK         0      /* the server knows that id, with that checksum */
#define TEAMCHECK_UNKNOWN    1      /* no team with that id */
#define TEAMCHECK_CHANGED    2      /* the id is known; its data is not what was sent */

typedef struct {
    uint8_t uuid[16];
    uint8_t checksum[8];
    uint8_t status;
} TeamCheckTeam;

typedef struct {
    uint8_t       protocol;     /* TEAMCHECK_PROTOCOL, or _EXTRAS (0 in a Write: TEAMCHECK_PROTOCOL) */
    uint8_t       type;
    uint8_t       count;
    uint32_t      nonce;
    TeamCheckTeam team[TEAMCHECK_MAX_TEAMS];
} TeamCheck;

static inline int32_t TeamCheck_Length(int32_t count) { return TEAMCHECK_HEAD + count * TEAMCHECK_TEAM; }

static inline int32_t TeamCheck_Write(uint8_t *p, const TeamCheck *c)
{
    int32_t i;
    memcpy(p, TEAMCHECK_MAGIC, 4);
    p[4] = c->protocol ? c->protocol : TEAMCHECK_PROTOCOL;
    p[5] = c->type;
    p[6] = c->count;
    p[7] = 0;
    p[8] = (uint8_t)(c->nonce >> 24); p[9] = (uint8_t)(c->nonce >> 16);
    p[10] = (uint8_t)(c->nonce >> 8); p[11] = (uint8_t)c->nonce;
    for (i = 0; i < c->count; i++) {
        uint8_t *t = p + TEAMCHECK_HEAD + i * TEAMCHECK_TEAM;
        memcpy(t, c->team[i].uuid, 16);
        memcpy(t + 16, c->team[i].checksum, 8);
        t[24] = c->team[i].status;
    }
    return TeamCheck_Length(c->count);
}

/* 0 = `p` is a team check packet, now in *c. */
static inline int32_t TeamCheck_Read(const uint8_t *p, int32_t len, TeamCheck *c)
{
    int32_t i;
    if (len < TEAMCHECK_HEAD || memcmp(p, TEAMCHECK_MAGIC, 4) != 0 ||
        (p[4] != TEAMCHECK_PROTOCOL && p[4] != TEAMCHECK_PROTOCOL_EXTRAS) ||
        p[6] < 1 || p[6] > TEAMCHECK_MAX_TEAMS || len != TeamCheck_Length(p[6]))
        return 1;
    c->protocol = p[4];
    c->type = p[5];
    c->count = p[6];
    c->nonce = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    for (i = 0; i < c->count; i++) {
        const uint8_t *t = p + TEAMCHECK_HEAD + i * TEAMCHECK_TEAM;
        memcpy(c->team[i].uuid, t, 16);
        memcpy(c->team[i].checksum, t + 16, 8);
        c->team[i].status = t[24];
    }
    return 0;
}

/* "8-4-4-4-12" and 16 hex digits to their bytes, and back. 0 = well formed. */
static inline int32_t TeamCheck_Hex(const char *text, uint8_t *out, int32_t bytes)
{
    int32_t n = 0, high = -1;
    for (; *text; text++) {
        int32_t v = *text >= '0' && *text <= '9' ? *text - '0' :
                    *text >= 'a' && *text <= 'f' ? *text - 'a' + 10 :
                    *text >= 'A' && *text <= 'F' ? *text - 'A' + 10 : -1;
        if (*text == '-')
            continue;
        if (v < 0 || n >= bytes)
            return 1;
        if (high < 0) high = v;
        else { out[n++] = (uint8_t)(high << 4 | v); high = -1; }
    }
    return n == bytes && high < 0 ? 0 : 1;
}

#endif /* TEAM_PROTO_H */
