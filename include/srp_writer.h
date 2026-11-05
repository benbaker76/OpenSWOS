/*
 * srp_writer.h -- a whole .SRP file from its parts (docs/REPLAY_FORMAT_V2.md).
 *
 * Two things write .SRP files: the game (src/replay_v2.c, LATEST.SRP at the end of
 * every match) and the recorder (third_party/matchmaker/Server/Recorder.cpp, from the
 * frames a player streams it). They share this, so a match the server recorded is the
 * same file the player's own game wrote -- the test holds them to it byte for byte.
 *
 * Plain C, no game state: the caller hands over the parts, this lays out the file.
 */
#ifndef SRP_WRITER_H
#define SRP_WRITER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWRP_VERSION        1
#define SWRP_HEADER_BYTES   32
#define SWRP_DIR_BYTES      16
#define SWRP_FLAG_AUDIO     0x0001u
#define SWRP_FLAG_SEEK      0x0002u
#define SWRP_FLAG_CHAPTERS  0x0004u
#define SWRP_QUIRK_ORIGINAL_FIRE_HOLD_LOCKOUT 0x0001u

#define FOURCC(a, b, c, d)  ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define CHUNK_META          FOURCC('M', 'E', 'T', 'A')
#define CHUNK_FRMS          FOURCC('F', 'R', 'M', 'S')
#define CHUNK_AUDI          FOURCC('A', 'U', 'D', 'I')
#define CHUNK_SEEK          FOURCC('S', 'E', 'E', 'K')
#define CHUNK_CHAP          FOURCC('C', 'H', 'A', 'P')
/* A RESUMABLE recording (docs/REPLAY_FORMAT_V2.md §4, "Playing on from a recording"):
 * what the match was set up from, the state it kicked off in, and every frame's inputs --
 * enough to play it again exactly, not only to watch it. */
#define CHUNK_SIMV          FOURCC('S', 'I', 'M', 'V')     /* which builds can re-simulate it */
#define CHUNK_SETP          FOURCC('S', 'E', 'T', 'P')     /* the match setup (netplay_setup.c) */
#define CHUNK_STAT          FOURCC('S', 'T', 'A', 'T')     /* the state at frame 0 (state_wire.c) */
#define CHUNK_INPT          FOURCC('I', 'N', 'P', 'T')     /* both players' input, every frame */
#define CHUNK_HASH          FOURCC('H', 'A', 'S', 'H')     /* each frame's SIM-tier hash, at its start */
/* What each player SAW of a netplay match (docs/REPLAY_FORMAT_V2.md §4, "Prediction"):
 * optional, and a file plays the same without them. */
#define CHUNK_PLYR          FOURCC('P', 'L', 'Y', 'R')     /* who played: both players' names */
#define CHUNK_PRED          FOURCC('P', 'R', 'E', 'D')     /* per frame, per player: the lag, rollbacks */
#define CHUNK_SEEN          FOURCC('S', 'E', 'E', 'N')     /* where players were drawn, where they were */
#define CHUNK_READ          FOURCC('R', 'E', 'A', 'D')     /* per frame, per player: the input as read, and when */

#define FRAME_HEAD_BYTES    20      /* count, camera x/y (16.16), goals, crowd, minute, flags */
#define SPRITE_BYTES        6       /* image, world x, world y */
#define AUDIO_EVENT_BYTES   8       /* frame, kind, channel, id */
#define CHAPTER_BYTES       8       /* frame, kind, team, value */
#define SEEK_INTERVAL       50
#define SRP_INPUT_BYTES     4       /* a frame's input: two players x (controls, key) */
#define SRP_HASH_BYTES      4       /* the low half of the frame's 64-bit SIM hash */
#define SRP_SIMV_BYTES      16      /* version, rules, style, layout hash, sim width, pad */
#define SRP_SIMV_VERSION    1

/* PRED: a byte per player per frame. The low four bits are the LAG -- how many of the
 * other player's inputs up to this frame were guesses when this player first played it
 * (0: it was drawn from real input). GGPO-X guesses by repeating the last input it had,
 * so the guess for frame f is that player's input at frame f - lag, and INPT says what it
 * really was. */
#define SRP_PRED_VERSION    1
#define SRP_PRED_HEAD       8       /* version, 0, frame count */
#define SRP_PRED_LAG        0x0Fu
#define SRP_PRED_STALLED    0x10u   /* held at GGPO's prediction limit before it was played */
#define SRP_PRED_REPLAYED   0x20u   /* a rollback played it again */
#define SRP_PRED_UNKNOWN    0xFFu   /* this player's data never reached the file */
/* SEEN: records sorted by frame, then player -- frame (4), player (1), count (1), then
 * `count` objects of SRP_SEEN_OBJ_BYTES: which (0-10 the top team's players, 11-21 the
 * bottom team's, 22 the ball), then drawn x, drawn y, final x, final y (int16, world
 * pixels; the ball's y is where it is seen, less its height). Only objects drawn
 * somewhere other than where the match finally had them. */
#define SRP_SEEN_VERSION    1
#define SRP_SEEN_HEAD       8       /* version, 0, record count */
#define SRP_SEEN_REC_HEAD   6
#define SRP_SEEN_OBJ_BYTES  9
#define SRP_SEEN_OBJECTS    23
#define SRP_SEEN_BALL       22

/* READ: what each player's game READ from its controls on each frame, and when -- beside
 * INPT, which is what was APPLIED (the same input, `delay` frames later, if the input
 * delay held). Per frame, per player, SRP_READ_BYTES: the controls byte as read, the
 * milliseconds since that player's read for the frame before (255 = that or more), and
 * flags: which wait that game was taking when it played the frame. */
#define SRP_READ_VERSION    1
#define SRP_READ_HEAD       8       /* version u16, the input delay u8, SRP_READ_HEAD_* u8, frame count u32 */
#define SRP_READ_HEAD_BALANCE1  0x01u   /* player 1 had [netplay] balance on */
#define SRP_READ_HEAD_BALANCE2  0x02u   /* player 2 had: it works only when both did */
#define SRP_READ_BYTES      3
#define SRP_READ_BALANCE    0x01u   /* slowed by a prediction balance wait (docs/NETPLAY_BALANCE.md) */
#define SRP_READ_TIMESYNC   0x02u   /* slowed by a time sync wait */
/* What the fire button did between this read and the one before (input_sdl.c, 2026-10-13):
 * the match reads the button's LEVEL once a frame, so these say what the level cannot.
 * Absent (0) in recordings made before, which is also "no press arrived". */
#define SRP_READ_FIRE_EVENT   0x04u /* a fire key or button went DOWN since the last read */
#define SRP_READ_FIRE_LATCHED 0x08u /* ... and was up again already: read as pressed by the latch */
#define SRP_READ_UNKNOWN    0xFFu   /* all three bytes: this player's half never reached the file */

/* One player's half of PRED, SEEN and READ, as that player's game knows it: a byte for
 * each of frames 0..predFrames-1, its own SEEN records laid out as in the chunk, by frame,
 * and SRP_READ_BYTES for each of frames 0..readFrames-1 (read NULL: a build without it). */
typedef struct SrpPrediction {
    const uint8_t *pred;
    uint32_t       predFrames;
    const uint8_t *seen;
    uint32_t       seenLen;
    const uint8_t *read;
    uint32_t       readFrames;
    uint16_t       delay;           /* the match's input delay, as this player played it */
    uint8_t        balance;         /* this player had the prediction balance on */
} SrpPrediction;

/* Where in META the final score sits: filled in when the match is over. */
#define SRP_META_SCORE_OFF  (2 + 2 + 2 + 2 + 4 + 32)

typedef struct {
    uint32_t frame;
    uint8_t  kind, channel;
    uint16_t id;
} SrpAudioEvent;

typedef struct {
    uint32_t frame;
    uint8_t  kind, team;
    uint16_t value;
} SrpChapter;

/* Everything a file holds. `offsets[i]` is where frame i starts in `frms`. */
typedef struct SrpContent {
    const uint8_t       *meta;
    uint32_t             metaLen;
    const uint8_t       *frms;
    uint32_t             frmsLen;
    const uint32_t      *offsets;
    uint32_t             frameCount;
    const SrpAudioEvent *audio;
    uint32_t             audioCount;
    const SrpChapter    *chapters;
    uint32_t             chapterCount;
    uint16_t             tickRate;      /* frames a second the match was played at */
    uint16_t             quirks;        /* SWRP_QUIRK_* */

    /* A resumable recording, or all zero. SETP, STAT, INPT, HASH and SIMV are written
     * only when setup, kickoff and inputs are all present. */
    const uint8_t       *setup;         /* NetplaySetup_Encode */
    uint32_t             setupLen;
    const uint8_t       *kickoff;       /* a state_wire packet: the state at frame 0 */
    uint32_t             kickoffLen;
    const uint8_t       *inputs;        /* SRP_INPUT_BYTES a frame, for frames 0..frameCount-1 */
    const uint32_t      *hashes;        /* one a frame, for frames 0..frameCount-1 */
    uint64_t             layout;        /* SimState_LayoutHash of the recording build */
    uint8_t              rules;         /* P2P_RULE_*: what it was played by */
    uint8_t              style;         /* the gameplay style, GAMEPLAY_* (SIMV byte 3; 0 in a
                                         * file from before XBOX: the rules byte's DOS bit says) */
    uint16_t             simWidth;      /* the simulation draw width it was played at */

    /* Who played an online match (PLYR): player 1's (the host's) name and player 2's, as
     * the lobby had them; both NULL for a match with no players to name. */
    const char          *players[2];

    /* What each player saw (PRED, SEEN): [0] player 1's, [1] player 2's, NULL for a half
     * this file does not have. Both NULL: neither chunk. */
    const SrpPrediction *prediction[2];
} SrpContent;

/* The whole file, malloc'd, or NULL when memory ran out. The parts are copied, never
 * taken. */
uint8_t *Srp_Build(const SrpContent *c, uint32_t *outLen);

/* A .SRP built by Srp_Build, with its PRED and SEEN replaced by these halves (either may
 * be NULL) and every other chunk kept byte for byte: the same file Srp_Build would have
 * made had it been given them. How a player's game adds the other player's half after the
 * match (the lobby's link carries it), so its LATEST.SRP stays the recorder's copy. NULL
 * when `file` is not one, or memory ran out. */
uint8_t *Srp_WithPrediction(const uint8_t *file, uint32_t fileLen, const SrpPrediction *p1,
                            const SrpPrediction *p2, uint32_t *outLen);

#ifdef __cplusplus
}
#endif

#endif /* SRP_WRITER_H */
