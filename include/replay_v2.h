/*
 * replay_v2.h -- whole-match replays in the SWRP format (SDL builds).
 * docs/REPLAY_FORMAT_V2.md is the specification.
 *
 * RECORDING taps a live match: every frame's camera, score and crowd state, every world
 * sprite at its WORLD position (so a replay plays at any screen width), and every sound
 * the match starts. When the match ends it is written to LATEST.SRP in the save folder,
 * and with [replays] auto_save also to a dated copy.
 *
 * PLAYBACK hooks into the highlights viewer that already plays .HIL and .RPL: a loaded
 * .SRP is played as one scene, its camera mapped onto this display exactly as a live
 * match maps its own (Render_CameraX), and its recorded sounds fired frame by frame.
 */
#ifndef REPLAY_V2_H
#define REPLAY_V2_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"
#include "srp_writer.h"      /* SrpAudioEvent (ReplayV2_LiveAppend) */

#define REPLAY_V2_EXT          0x2E535250u      /* '.SRP', as the file menus pack it */
#define REPLAY_V2_LATEST       "LATEST.SRP"

/* ---- recording ------------------------------------------------------------- */

/* From main.c. `path`: record every match to this file instead of LATEST.SRP (a test's
 * --record-replay, which also records under the replay rig); NULL for the default.
 * `autoSave`: also keep a dated copy of each match ([replays] auto_save). */
void    ReplayRec_Configure(const char *path, int32_t autoSave);

/* GameLoop's own hooks, in the order a frame calls them. */
void    ReplayRec_MatchStart(void);     /* before the first frame */
void    ReplayRec_FrameBegin(void);     /* the camera has moved for this frame */
void    ReplayRec_CaptureSprites(void); /* DrawSprites has drawn it */
void    ReplayRec_FrameEnd(void);       /* the frame is complete */
void    ReplayRec_MatchEnd(void);       /* the match is over (or quit): write the file */

/* Netplay: the frame about to be stepped. A re-simulated frame replaces what was
 * recorded for it and every frame after, so the file holds the match as finally played. */
void    ReplayRec_SetFrame(uint32_t frame);

int32_t ReplayRec_Recording(void);
/* Record nothing from now on, for a run that plays a match only to check it (--srp-resim):
 * LATEST.SRP is the last match PLAYED, and a re-simulation is not one. */
void    ReplayRec_Suppress(int32_t on);

/* The recording so far, as the parts of a file (include/srp_writer.h): what the match
 * recorder (src/match_record.c) streams, frame by confirmed frame. Pointers into the
 * recorder's own buffers, good until the next frame is recorded. 0 when this match is
 * being recorded, 1 when it is not. */
struct SrpContent;
int32_t ReplayRec_Content(struct SrpContent *out);

/* A RESUMABLE recording: enough to play the match again exactly, from any frame
 * (include/srp_writer.h, SETP/STAT/INPT/HASH/SIMV). The netplay match gives them; any
 * other match leaves them empty and its .SRP is what it always was.
 *   SetSetup    the match setup (NetplaySetup_Encode), from the lobby, BEFORE MatchStart
 *   SetKickoff  the state at frame 0 (a state_wire packet), and what it needs to be
 *               re-simulated: the registry layout, the rules (P2P_RULE_*), the sim width
 *   SetInput    frame `frame`'s input, SRP_INPUT_BYTES; a later call for the same frame
 *               replaces it (a rollback), so a confirmed frame holds what was played
 *   SetHash     frame `frame`'s SIM-tier hash at its start, the same way */
void    ReplayRec_SetSetup(const uint8_t *setup, uint32_t len);
void    ReplayRec_SetKickoff(const uint8_t *packet, uint32_t len, uint64_t layout, uint8_t rules,
                             uint16_t simWidth);
void    ReplayRec_SetInput(uint32_t frame, const uint8_t *input);
void    ReplayRec_SetHash(uint32_t frame, uint64_t hash);

/* What each player SAW (PRED, SEEN -- include/srp_writer.h), in a live match's .SRP.
 *   SetPredictionKept   [netplay] prediction; SWOS_RECORD_PREDICTION=0|1 overrides it
 *   AddPeerPrediction   the other player's half, from the lobby's link after the match:
 *                       the files this match was written to are written again with both
 *                       halves. 0, or 1 when there was nothing to add it to. */
struct SrpPrediction;
void    ReplayRec_SetPredictionKept(int32_t on);
/* Who played an online match (PLYR), player 1 (the host) first: from the lobby, as the
 * match starts; written into its .SRP from kick-off. */
void    ReplayRec_SetPlayers(const char *player1, const char *player2);
int32_t ReplayRec_PredictionKept(void);
int32_t ReplayRec_AddPeerPrediction(const struct SrpPrediction *peer);

/* A resumable .SRP's parts, read back: what --srp-resim plays the match again from. */
typedef struct {
    uint8_t  *setup;        /* NetplaySetup_Encode */
    uint32_t  setupLen;
    uint8_t  *kickoff;      /* a state_wire packet: the state at frame 0 */
    uint32_t  kickoffLen;
    uint8_t  *inputs;       /* SRP_INPUT_BYTES a frame */
    uint32_t *hashes;       /* the low half of each frame's SIM hash, at its start */
    uint32_t  frames;
    uint64_t  layout;       /* SimState_LayoutHash of the build that recorded it */
    uint8_t   rules;        /* P2P_RULE_* it was played by */
    uint8_t   style;        /* GAMEPLAY_* (SIMV byte 3); 0 before XBOX: the rules byte's DOS bit */
    uint16_t  simWidth;
} ReplayResume;
/* 0; 1 no such .SRP; 2 recorded only to be watched (no resume parts); 3 broken. A bare
 * name is looked for in the save folder, like LOAD. */
int32_t ReplayV2_LoadResume(const char *name, ReplayResume *out);
void    ReplayV2_FreeResume(ReplayResume *r);
int32_t ReplayV2_LoadPrediction(const char *name, uint32_t frames, uint8_t **pred, uint8_t **seen,
                                uint32_t *seenLen);

/* ---- playback -------------------------------------------------------------- */

/* Load a .SRP for the highlights viewer. 0 on success; the teams, score and pitch are
 * then in the HIL header the viewer reads, as a .RPL load leaves them. */
int32_t ReplayV2_Load(const uint8_t *name);
void    ReplayV2_Unload(void);
int32_t ReplayV2_Loaded(void);
/* LIVE CAST (src/live_cast.c): frames to play after those loaded; 0 when appended. */
int32_t ReplayV2_LiveAppend(const uint8_t *frms, uint32_t len, uint32_t count,
                            const SrpAudioEvent *audio, uint32_t audioCount);
/* The recording carries its own sounds (so the viewer must not add random crowd chants). */
int32_t ReplayV2_HasAudio(void);

/* A loaded .SRP is shown in the view its match was played in (DOS 320x200 or Amiga):
 * UseRecordedView before the viewer's MatchViewEnter, RestoreView after its
 * MatchViewLeave puts the player's own setting back. No-ops without a recording, or
 * when the recording's view is not known or is the player's own. */
void    ReplayV2_UseRecordedView(void);
void    ReplayV2_RestoreView(void);
/* After the viewer's MatchViewEnter: map the camera from the height the recording was
 * simulated in (META), restored by ReplayV2_RestoreView. */
void    ReplayV2_ApplyRecordedSimView(void);

/* PlayHighlightsLoop, starting the one scene a .SRP is. */
void    ReplayV2_StartScene(void);
/* DrawReplayFrame, for a loaded .SRP: one recorded frame, and its sounds. */
void    ReplayV2_DrawFrame(void);

/* ---- test harness ---------------------------------------------------------- */

/* --view-replay=<file>: play a .SRP headless in place of the front end, then exit.
 * SWOS_REPLAY_TRACE=<file> writes a line per frame both when recording and when playing
 * (the sprites actually drawn, and the sounds), so the two can be compared. */
int32_t ReplayV2_ArmViewTest(const char *path);
/* --replay-player: --view-replay that holds at the end instead of exiting, and takes
 * the playback page's controls -- exported to it on the web build. */
void    ReplayV2_PlayerMode(int32_t on);
int32_t ReplayV2_PlayerFrames(void);
int32_t ReplayV2_PlayerTickRate(void);
int32_t ReplayV2_PlayerFrame(void);         /* the frame on screen */
int32_t ReplayV2_PlayerPaused(void);
void    ReplayV2_PlayerPause(int32_t paused);
void    ReplayV2_PlayerSeek(int32_t frame);
void    ReplayV2_PlayerStep(int32_t delta);   /* +-N frames, then paused */
/* Whose screen is drawn: 0 the match as played, 1 or 2 that player's (SEEN); -1 asks. */
int32_t ReplayV2_PlayerPerspective(int32_t who);
int32_t ReplayV2_ViewTestArmed(void);
void    ReplayV2_ViewTestRun(void);

/* sprites.c DrawQueuedSprites: a world sprite the live match drew, for the trace. */
void    ReplayTrace_LiveSprite(int16_t ordinal, int16_t x, int16_t y);

#endif /* SWOS_SDL_BUILD */
#endif /* REPLAY_V2_H */
