/*
 * dos_replay_load.c — load DOS / SWOS-2020 .HIL replays into our engine.
 *
 * The DOS/2020 .HIL (and .RPL) format is little-endian with a 3626-byte header
 * (1704-byte teams) and a different sprite numbering than our Amiga-derived
 * engine. Rather than teach the replay engine a second format, we convert a
 * loaded DOS .HIL in place into our big-endian Amiga HIL layout, so the existing
 * (now widescreen-correct) replay path plays it unchanged.
 *
 * Frame stream is structurally identical to ours — per frame: a camera-marker
 * dword (bit31 set), a goals dword, an anim-state dword, then sprite dwords
 * (bit31 clear), with 0xFFFFFFFF ending each scene. The only differences:
 *   - endianness (LE vs our BE)
 *   - camera marker word order: DOS = Y<<16|X, ours = X<<16|Y
 *   - goals word order:         DOS = t2<<16|t1, ours = t1<<16|t2
 *   - sprite index: DOS gameplay index = Amiga index + 114
 *     (see docs/AMIGA_DOS_DIFF.md; matches color_sprites.c::_DosOverlaySprites)
 *
 * SDL-only: the DOS replays are a PC-extras feature.
 */
#include "game_types.h"     /* provides uintN_t on both SDL and Amiga (no <stdint.h>:
                             * the m68k toolchain can't resolve it and swos_platform.h
                             * deliberately keeps it out to avoid typedef conflicts) */
#include "endian_helpers.h"
#include <string.h>

#ifdef SWOS_SDL_BUILD

#include <stdio.h>          /* fprintf(stderr, …) → SWOS.log: log each reel's gameStyle→fps */

extern uint8_t  _hilFileBuffer[];
extern uint16_t _hilNumGoals, _hilTeam1Goals, _hilTeam2Goals;
extern uint16_t _hilPitchNumber, _hilMaxSubstitutes;
extern uint8_t  _hilGameName[];
extern uint8_t  _hilGameRound[];

/* ---- DOS header layout (little-endian) ---- */
#define DOS_HDR_SIZE        0x0E2A   /* 3626 */
#define DOS_TEAM1_OFF       8
#define DOS_TEAM2_OFF       1712     /* 8 + 1704 */
#define DOS_TEAM_SIZE       1704
#define DOS_LEAGUE_NAME_OFF 0x0D58
#define DOS_NUMSCENES_OFF   0x0E20
#define DOS_T1GOALS_OFF     0x0E22
#define DOS_T2GOALS_OFF     0x0E24
#define DOS_PITCH_OFF       0x0E26
#define DOS_MAXSUBS_OFF     0x0E28

/* ---- our HIL layout ---- */
#define OUR_TEAM1_OFF       8
#define OUR_TEAM2_OFF       1026     /* 8 + 1018 */
#define OUR_TEAM_SIZE       1018
#define OUR_SCENE_OFF       2254     /* header size; scenes follow */
#define OUR_SCENE_BYTES     19000
#define TEAM_HDR_INT16S     11       /* kit/marker fields, byteswapped */
#define TEAM_NAME_OFF       22       /* team + player name region (byte-sized) */

#define DOS_TO_AMIGA_SPR    114
#define K_HIL_SIG           0x0000000Cu
#define K_HIL_MAXSZ         192254u

/* Detect a DOS/2020 replay: first u32 is the constant 8 (LE), unlike our
 * big-endian sig (0x0C at byte 3). */
int DosReplay_IsDosHil(const uint8_t *buf, uint32_t size)
{
    return size >= DOS_HDR_SIZE &&
           buf[0] == 8 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0;
}

/* Convert one team: DOS LE 11-int16 kit header -> ours (host-native in buffer,
 * stored BE on the way out by SetHilFileHeader's reader path is N/A here — the
 * replay's CopyTeamsAndHeader does BE16_TO_HOST, so we write BE). Name region
 * (team + player names, byte-sized) copies verbatim. */
static void convert_team(const uint8_t *dosTeam, uint8_t *ourTeam)
{
    for (int i = 0; i < TEAM_HDR_INT16S; i++) {
        uint16_t v = (uint16_t)(dosTeam[i*2] | (dosTeam[i*2+1] << 8)); /* LE read */
        ((uint16_t *)ourTeam)[i] = HOST_TO_BE16(v);                    /* BE out  */
    }
    /* byte-sized remainder (team name + per-player fields). Copy what fits our
     * smaller struct; players whose layout differs are not rendered from here. */
    int n = OUR_TEAM_SIZE - TEAM_NAME_OFF;
    if (n > DOS_TEAM_SIZE - TEAM_NAME_OFF) n = DOS_TEAM_SIZE - TEAM_NAME_OFF;
    memcpy(ourTeam + TEAM_NAME_OFF, dosTeam + TEAM_NAME_OFF, (size_t)n);
}

/* Convert a DOS frame stream (one or more scenes) into our format. Reads LE from
 * `src`, writes BE into `dst`. Returns bytes written. The DOS scene region sits
 * above our scene region in the same buffer, so a forward in-place copy keeps
 * the write cursor behind the read cursor (1:1 dwords). */
/* Set by convert_frames: 1 if the recording's sprites span wider than the 336px
 * DOS/Amiga view (a SWOS-2020 widescreen capture), 0 otherwise. The SDL
 * widescreen camera helper (highlight.c ReplayCameraX) reads this to decide
 * whether to centre a 336 recording or use the wide recorded camera verbatim.
 * The recorded camera markers are the same regardless; only the playback
 * mapping differs, so the core replay/render path is untouched. */
int _replayRecordingIsWide = 0;

/* Native playback framerate (Hz) of the currently-loaded replay. The host paces
 * LOADED-replay playback (Platform_ActivePacingFps in platform_sdl.c) at THIS
 * rate rather than the live-match gameplay setting, so a replay plays at its
 * AUTHORED speed whether the game is in Amiga-50 or DOS-70 gameplay mode.
 *
 * DOS / SWOS-2020 recordings store the authored rate PER FILE as a "gameStyle"
 * byte at file offset 5 (see replay_fps_from_gamestyle); the DOS loaders read it
 * and set this. Native Amiga recordings have no such byte and run at 50 Hz — the
 * default, which the load entry points (file_io LoadHilFile / arcade
 * arc_load_reel) also reset before dispatch so a native Amiga .HIL after a DOS
 * reel doesn't inherit a stale per-file rate. */
int _replayPlaybackFps = 50;

extern int DosGameplay_TargetFps(void);   /* current gameplay-mode rate (70 DOS / 50 Amiga) */

/* Map a SWOS-2020 replay gameStyle byte (file offset 5) to its playback Hz,
 * mirroring swos-2020 frameDelayForReplay() (video/render.cpp): 0 = PC DOS 70,
 * 1 = Amiga 50, 2 = Moon Soccer 50, >=3 = Amiga-60 -> 60. The 0xF4 (244)
 * sentinel means "no per-file rate — use the current game style", which for us
 * is the live gameplay setting. These reels are recorded per-style, NOT all at
 * 70: the arcade attract_mode/{dos,amiga} folders mix gameStyle 0 and 1, so the
 * rate MUST come from the file, not the folder or the format. */
static int replay_fps_from_gamestyle(uint8_t gs)
{
    switch (gs) {
    case 0:    return 70;   /* PC DOS */
    case 1:    return 50;   /* Amiga */
    case 2:    return 50;   /* Moon Soccer */
    case 0xF4: return DosGameplay_TargetFps();   /* 244 = use current game style */
    case 3: case 4: return 60;                   /* Amiga 60 FPS (beta styles) */
    /* SWOS 2020's .HIL files carry 30 or 31 here: every one recorded in its PC (70)
     * style has 30, every one in its Amiga (50) style 31 -- the RPLs' 0 and 1 in the
     * low bit. Its styles stop at 4, so anything above is that: 60 was wrong for both. */
    default:   return (gs & 1) ? 50 : 70;
    }
}

/* The goals' nets stand still: their sprites (our numbers) at world 300, 106 and 300, 751. */
#define GOAL_TOP_SPR        1091
#define GOAL_BOTTOM_SPR     1092
#define GOAL_WORLD_X        300
#define GOAL_TOP_WORLD_Y    106
#define GOAL_BOTTOM_WORLD_Y 751

static uint32_t convert_frames(const uint8_t *src, uint32_t srcLen, uint8_t *dst)
{
    uint32_t si = 0, di = 0;
    uint32_t cameraAt = 0xFFFFFFFFu;    /* the frame's camera marker, as written */
    int afterMarker = 0;  /* next two dwords after a marker are goals + anim */
    int isWide = 0;       /* recorded sprite span exceeds the 336 DOS/Amiga view */
    while (si + 4 <= srcLen) {
        uint32_t d = (uint32_t)src[si] | ((uint32_t)src[si+1] << 8) |
                     ((uint32_t)src[si+2] << 16) | ((uint32_t)src[si+3] << 24);
        si += 4;
        uint32_t out;
        if (d == 0xFFFFFFFFu) {
            out = 0xFFFFFFFFu;            /* scene terminator */
            afterMarker = 0;
            cameraAt = 0xFFFFFFFFu;
        } else if (afterMarker == 1) {     /* goals: DOS t2<<16|t1 -> ours t1<<16|t2 */
            out = ((d & 0xFFFFu) << 16) | (d >> 16);
            afterMarker = 2;
        } else if (afterMarker == 2) {     /* anim state (duplicated word): value only */
            out = d;
            afterMarker = 0;
        } else if (d & 0x80000000u) {      /* camera marker: DOS Y<<16|X -> ours X<<16|Y */
            uint32_t v = d & 0x7FFFFFFFu;
            out = (((v & 0xFFFFu) << 16) | (v >> 16)) | 0x80000000u;
            afterMarker = 1;
            cameraAt = di;
        } else {                           /* sprite: [idx:11][x:10][y:10], idx -= 114 */
            uint32_t idx = d >> 20;
            int32_t  x   = (int32_t)(d << 12) >> 22;
            int32_t  y   = (int32_t)(d << 22) >> 22;
            if (x < -40 || x > 360) isWide = 1;   /* sprite beyond the 336-wide DOS view */
            if (idx >= DOS_TO_AMIGA_SPR) idx -= DOS_TO_AMIGA_SPR;
            /* SWOS 2020's recordings have a frame, where the camera jumps (a speedrun's end),
             * whose marker is still the old camera while its sprites are drawn for the new one:
             * played as it is, the whole picture leaps for that frame. A net is where it is: its
             * sprite says which camera the frame was drawn for. */
            if ((idx == GOAL_TOP_SPR || idx == GOAL_BOTTOM_SPR) && cameraAt != 0xFFFFFFFFu) {
                const uint32_t cam = BE_READ_U32(dst + cameraAt);
                const int32_t camX = (int16_t)((cam >> 16) & 0x7FFFu);
                const int32_t newX = GOAL_WORLD_X - x;
                const int32_t newY = ((idx == GOAL_TOP_SPR ? GOAL_TOP_WORLD_Y : GOAL_BOTTOM_WORLD_Y) - y) & 0x3FF;
                if (camX != newX && newX >= 0 && newX <= 0x2A0 && newY <= 880)
                    BE_WRITE_U32(dst + cameraAt, 0x80000000u | ((uint32_t)newX << 16) | (uint32_t)newY);
                cameraAt = 0xFFFFFFFFu;     /* (the frame's first net decides) */
            }
            out = ((idx & 0x7FF) << 20) |
                  (((uint32_t)x & 0x3FF) << 10) | ((uint32_t)y & 0x3FF);
            out &= ~0x80000000u;
        }
        BE_WRITE_U32(dst + di, out);
        di += 4;
    }
    _replayRecordingIsWide = isWide;
    /* _replayPlaybackFps is set by the caller from the file's gameStyle byte
     * (offset 5) — see DosReplay_LoadHil / DosReplay_LoadRpl — because
     * this converter no longer has the header (src already points past it). */
    return di;
}

/* Teams and scalars of a DOS header into _hilFileBuffer's header (CopyTeamsAndHeader
 * reads the teams there; the scalars are the symbols overlaid on it). `b` is the DOS
 * file, untouched until the header has been read. */
static void write_hil_header(const uint8_t *b, uint16_t numScenes)
{
    uint16_t t1g     = (uint16_t)(b[DOS_T1GOALS_OFF] | (b[DOS_T1GOALS_OFF+1] << 8));
    uint16_t t2g     = (uint16_t)(b[DOS_T2GOALS_OFF] | (b[DOS_T2GOALS_OFF+1] << 8));
    uint16_t pitch   = (uint16_t)(b[DOS_PITCH_OFF]   | (b[DOS_PITCH_OFF+1]   << 8));
    uint16_t maxSubs = (uint16_t)(b[DOS_MAXSUBS_OFF] | (b[DOS_MAXSUBS_OFF+1] << 8));

    BE_WRITE_U32(_hilFileBuffer,     K_HIL_SIG);
    BE_WRITE_U32(_hilFileBuffer + 4, K_HIL_MAXSZ);
    convert_team(b + DOS_TEAM1_OFF, _hilFileBuffer + OUR_TEAM1_OFF);
    convert_team(b + DOS_TEAM2_OFF, _hilFileBuffer + OUR_TEAM2_OFF);
    memcpy(_hilGameName, b + DOS_LEAGUE_NAME_OFF, 40);
    memset(_hilGameRound, 0, 40);
    _hilNumGoals       = numScenes;
    _hilTeam1Goals     = t1g;
    _hilTeam2Goals     = t2g;
    _hilPitchNumber    = pitch;
    _hilMaxSubstitutes = maxSubs;
}

/* ---- RPL: a whole-match replay in the same DOS format, one ~1 MB scene ----
 * Too big for _hilFileBuffer (192 KB) or the 19000-B scene slots, so load it
 * into a dedicated buffer, convert the frame stream in place, and hand that
 * buffer to the replay engine as a single long scene (game_loop.c picks it up
 * via _rplBufferPtr). The 2020 widescreen RPLs reach ~2.4 MB; size for those. */
#define RPL_BUFFER_SIZE   (2700u * 1024u)
static uint8_t s_rplBuffer[RPL_BUFFER_SIZE];

uint8_t  *_rplBufferPtr   = (uint8_t *)0;   /* non-NULL while an RPL is loaded */
uint32_t  _rplStreamBytes = 0;              /* converted frame-stream length */

extern int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param,
                                          void *name, void *dest,
                                          uint32_t *outFileSize);
#define DOS_ASSET_OP_INIT_SOURCE0   2
extern volatile uint16_t _numScenesInHilFile;

/* Load + convert a DOS/2020 .RPL. Returns 0 on success. Bypasses LoadFileEx
 * (which panics past the ~91 KB frontbuffer stage) with the direct asset
 * dispatch — the same path dos_load_pitch uses for the big .BLK banks. */
int DosReplay_LoadRpl(uint8_t *filename)
{
    uint32_t size = 0;
    CAssetSys_DispatchGetSize(DOS_ASSET_OP_INIT_SOURCE0, 0, filename,
                               s_rplBuffer, &size);
    if (size <= DOS_HDR_SIZE || size > RPL_BUFFER_SIZE)
        return 1;
    if (!DosReplay_IsDosHil(s_rplBuffer, size))
        return 1;

    uint8_t *b = s_rplBuffer;

    /* Authored playback rate from the gameStyle byte at file offset 5. */
    _replayPlaybackFps = replay_fps_from_gamestyle(b[5]);
    fprintf(stderr, "[replay] DOS .RPL gameStyle=%u -> %d fps\n",
            (unsigned)b[5], _replayPlaybackFps);

    /* 1. the teams and scalars into _hilFileBuffer, before the header is overwritten */
    write_hil_header(b, 1);            /* one whole-match scene */

    /* 2. convert the frame stream in place: DOS @DOS_HDR_SIZE -> our @0. Forward
     *    1:1, write cursor stays behind the read cursor. */
    _rplStreamBytes = convert_frames(b + DOS_HDR_SIZE, size - DOS_HDR_SIZE, b);

    /* 3. hand the buffer to the replay engine */
    _rplBufferPtr     = b;
    _numScenesInHilFile = 1;
    return 0;
}

/* ---- HIL: the goal scenes of a DOS or SWOS 2020 .HIL ----
 * A DOS .HIL is a 3626-byte header and numScenes scenes of one size: 19000 bytes, our
 * slot, in the PC game's files, but 61000 in SWOS 2020's (a widescreen frame holds
 * more). The file is read whole into s_rplBuffer -- never through LoadFileEx, which
 * stages a file in the frontbuffer and panics (0xE000) on one bigger than the screen:
 * a SWOS 2020 .HIL of two scenes is 125 KB, of ten 613 KB. Scenes our 19000-byte slots
 * hold are copied into them, exactly as the in-place conversion did, so a PC .HIL
 * still saves again; any other size plays from s_rplBuffer at its own stride
 * (game_loop.c takes each scene from _dosHilScenesPtr). */
uint8_t  *_dosHilScenesPtr  = (uint8_t *)0;  /* non-NULL while such a .HIL is loaded */
uint32_t  _dosHilSceneBytes = 0;             /* its scene size */

void DosReplay_ClearRpl(void);

/* 0 loaded, 1 a DOS .HIL that cannot be played, -1 not a DOS .HIL (or no such file):
 * the caller reads it as our own .HIL. */
int DosReplay_LoadHil(uint8_t *filename)
{
    uint32_t size = 0;
    DosReplay_ClearRpl();
    CAssetSys_DispatchGetSize(DOS_ASSET_OP_INIT_SOURCE0, 0, filename, s_rplBuffer, &size);
    if (size == 0 || size > RPL_BUFFER_SIZE || !DosReplay_IsDosHil(s_rplBuffer, size))
        return -1;

    uint8_t *b = s_rplBuffer;
    uint16_t numScenes = (uint16_t)(b[DOS_NUMSCENES_OFF] | (b[DOS_NUMSCENES_OFF+1] << 8));
    uint32_t stream = size - DOS_HDR_SIZE;
    uint32_t stride = numScenes ? stream / numScenes : 0;
    if (numScenes == 0 || stride == 0 || (stride & 3) || stride * numScenes != stream) {
        fprintf(stderr, "[replay] DOS .HIL %s: %u scenes do not divide its %u bytes\n",
                (const char *)filename, (unsigned)numScenes, (unsigned)stream);
        return 1;
    }

    _replayPlaybackFps = replay_fps_from_gamestyle(b[5]);
    fprintf(stderr, "[replay] DOS .HIL gameStyle=%u -> %d fps, %u scenes of %u bytes\n",
            (unsigned)b[5], _replayPlaybackFps, (unsigned)numScenes, (unsigned)stride);

    write_hil_header(b, numScenes);
    convert_frames(b + DOS_HDR_SIZE, stream, b);
    if (stride == OUR_SCENE_BYTES && OUR_SCENE_OFF + stream <= K_HIL_MAXSZ) {
        memcpy(_hilFileBuffer + OUR_SCENE_OFF, b, stream);
    } else {
        _dosHilScenesPtr  = b;
        _dosHilSceneBytes = stride;
    }
    return 0;
}

/* Clear the RPL and DOS .HIL state so a subsequent .HIL load uses the normal scene buffer. */
void DosReplay_ClearRpl(void)
{
    _rplBufferPtr = (uint8_t *)0;
    _rplStreamBytes = 0;
    _dosHilScenesPtr = (uint8_t *)0;
    _dosHilSceneBytes = 0;
}

#endif /* SWOS_SDL_BUILD */
