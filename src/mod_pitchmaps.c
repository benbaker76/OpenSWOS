/* mod_pitchmaps.c -- the pitchmaps mod (bin/mods/pitchmaps; canonical, off by default): a DOS-tier
 * pitch's PICTURE from a Tile Agent map, <mod>/grafs_dos/pitch<N>.tmj or <graphics dir>/pitch<N>.tmj
 * (docs/PITCH_MAPS.md). SDL only. It reaches the pitch through the engine's pitch hooks
 * (include/mod_api.h): pitchLoaded, pitchColoured, pitchDropped, pitchTiles, pitchCondition.
 *
 * The match is not played from it: the crowd's slot table, its random picks and everything else
 * simulated stay on the game's own PITCHn.DAT/.BLK, so a match, a replay and a netplay peer are
 * bit for bit what they are with the mod off. The map decides what each cell LOOKS like.
 *
 * WHAT IS THE MAP'S AND WHAT IS NOT. The engine draws 2310 cells, each a 16 x 16
 * chunky tile. With a map in force each cell's tile is BAKED from the map's
 * layers -- every visible tile layer drawn before the sprites, bottom first, the
 * map's transparent entry skipped -- so the lines, the stands, the hoardings and
 * the grass are whatever the map's author drew.
 *
 * The crowd is simulation: which fan stands is picked from the match's own
 * Rand(), the slot table that holds it is hashed, rolled back and recorded, and
 * two netplay peers must agree on it whatever each has set here. So the engine
 * keeps its own PITCHn.DAT/.BLK and plays the crowd on them exactly as before,
 * and this file only answers "what does this cell look like now": a cell whose
 * engine tile is a fan's STANDING variant (an even tile index up to
 * MAX_FAN_TILE_INDEX) shows the map's standing tile -- the same cell baked with
 * the swap source layer's tile in place of the swapped layer's -- and every
 * other cell its ordinary one.
 *
 * The fans' kits (palette entries 10 and 11) are the engine's knowledge too:
 * each baked tile takes the colours the engine gave ITS tile for that cell. */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "swos_io.h"            /* swos_data_path: the mods, the assets folder, the game */
#include "display.h"            /* MAX_FAN_TILE_INDEX */
#include "jag_graphics.h"       /* JagGfx_RemapDonated */
#include "../third_party/tileagent/tileagent.h"
#include "gfx.h"                /* the layers over the players, on either presenter */

#define CELLS       2310        /* pitch.c PITCH_POINTER_COUNT: 42 x 55 */
#define GRID_COLS   42
#define GRID_ROWS   55
#define TILE_PX     16
#define TILE_BYTES  (TILE_PX * TILE_PX)
#define MAX_SETS    16
#define IMAGE_CAP   (1 << 21)   /* a tileset image, a byte a pixel */

extern uint8_t  _pitchChunkyBuffer[];
extern uint8_t *_s_pitchFullPtrs[];
extern int Platform_LoadIndexedBMP(const char *path, uint8_t *out8, int out_cap,
                                   uint32_t *outPal, int *outW, int *outH);

#define SLUG "pitchmaps"

/* pitch.c: the kit colours the engine gave a tile of its own bank, 0 if none. */
extern int32_t Pitch_FanTileKit(const uint8_t *tile, uint8_t *slot10, uint8_t *slot11);

static struct {
    ta_map  *map;
    ta_player *player;                  /* which layers this pitch shows (its layout event) */
    char     dir[256];
    int32_t  pitch;                     /* the pitch number the map is for */
    uint8_t *image[MAX_SETS];
    int32_t  imageW[MAX_SETS], imageH[MAX_SETS];
    int32_t  baked;                     /* the tiles below are this match's */
    uint8_t *sit;                       /* CELLS tiles: each cell as the map draws it */
    uint8_t *stand;                     /* CELLS tiles: the same with the swap source in */
    uint8_t  have[CELLS];               /* bit 0: a tile is drawn there; bit 1: a standing one too */
} s_pm;

static int32_t PitchMap_Active(void)    { return s_pm.map != NULL && s_pm.baked; }

/* ---- the layers over the players (stage 2: depth "above" -- the example's clouds) ----
 * Drawn over the finished frame (frameDrawn, gfx.h: either presenter, blended at the layer's
 * opacity), from the camera, while a match or a replay is shown. The runtime runs them on a clock
 * of its own (presented frames), seeded at each match: drawing only, so nothing here is match
 * state and a match, a replay and a netplay peer are what they are without it. */
#define MAX_ABOVE 8
static struct {
    int32_t   layer[MAX_ABOVE], count;
    uint8_t  *rgba[MAX_SETS];           /* each tileset in R G B A, the map's palette */
    ta_image32 image[MAX_SETS];
    uint8_t  *buf;                      /* the picture's size: one layer drawn from the camera */
    int32_t   bufW, bufH;
    GfxTexture *tex;
    int32_t   texW, texH;
    int32_t   clock, showing;
} s_up;

static void above_release(void)
{
    int32_t i;
    for (i = 0; i < MAX_SETS; i++) {
        free(s_up.rgba[i]);
        s_up.rgba[i] = NULL;
        memset(&s_up.image[i], 0, sizeof s_up.image[i]);
    }
    s_up.count = 0;
    s_up.showing = 0;
}

static void release_map(void)
{
    int32_t i;
    above_release();
    for (i = 0; i < MAX_SETS; i++) {
        free(s_pm.image[i]);
        s_pm.image[i] = NULL;
    }
    if (s_pm.player)
        ta_player_free(s_pm.player);
    s_pm.player = NULL;
    if (s_pm.map)
        ta_map_free(s_pm.map);
    s_pm.map = NULL;
    s_pm.baked = 0;
}

static void PitchMap_Drop(void)
{
    s_pm.baked = 0;
}

static char *read_text(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long size;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    text = (char *)malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        text = NULL;
    }
    fclose(f);
    if (text) {
        text[size] = '\0';
        *len = (size_t)size;
    }
    return text;
}

static int32_t PitchMap_Load(const char *dir, int32_t pitchNumber)
{
    char path[1100], base[512], error[160], layout[32];
    char *text = NULL;
    size_t len = 0;
    int32_t i, shared = 0, event;
    ta_map *map;

    s_pm.baked = 0;
    if (!dir)
        return 0;
    if (s_pm.map && s_pm.pitch == pitchNumber && strcmp(s_pm.dir, dir) == 0)
        return 1;                       /* the same pitch as last match */
    release_map();

    /* A map of this pitch alone, or ONE map of every pitch whose `layout-<N>` event
     * shows this one's layers (the game's six share their stands and differ in the
     * mowing). */
    /* Where the game's own files are looked for (swos_data_path): each enabled mod's folder first
     * -- this one's, or a pitch a player made and shipped as a mod (DLC) -- then the assets
     * folder, then beside the game. The map's images are read from the folder it was found in. */
    {
        char rel[160];
        snprintf(rel, sizeof rel, "%s/pitch%d.tmj", dir, (int)pitchNumber);
        swos_data_path(rel, path, (uint32_t)sizeof path);
        text = read_text(path, &len);
        if (!text) {
            snprintf(rel, sizeof rel, "%s/pitch.tmj", dir);
            swos_data_path(rel, path, (uint32_t)sizeof path);
            text = read_text(path, &len);
            shared = text != NULL;
        }
    }
    if (!text)
        return 0;                       /* no map for this pitch: the game's own files */
    {   /* the map's folder: its images are beside it */
        char *slash = strrchr(path, '/');
        char *bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash))
            slash = bslash;
        snprintf(base, sizeof base, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
    }
    error[0] = '\0';
    map = ta_map_load(text, len, NULL, error, sizeof error);
    free(text);
    if (!map) {
        fprintf(stderr, "[pitch-map] %s: %s -- drawing the game's own pitch\n", path, error);
        return 0;
    }
    if (map->width != GRID_COLS || map->height != GRID_ROWS ||
        map->tile_width != TILE_PX || map->tile_height != TILE_PX ||
        map->palette_count == 0 || map->tileset_count > MAX_SETS) {
        fprintf(stderr, "[pitch-map] %s: a pitch map is %d x %d tiles of %d x %d pixels, indexed, "
                        "with at most %d tilesets (this one: %d x %d of %d x %d, %d palette entries, "
                        "%d tilesets) -- drawing the game's own pitch\n", path,
                GRID_COLS, GRID_ROWS, TILE_PX, TILE_PX, MAX_SETS, map->width, map->height,
                map->tile_width, map->tile_height, map->palette_count, map->tileset_count);
        ta_map_free(map);
        return 0;
    }
    snprintf(layout, sizeof layout, "layout-%d", (int)pitchNumber);
    event = ta_map_event(map, layout);
    if (shared && event < 0) {
        fprintf(stderr, "[pitch-map] %s has no \"%s\" event, so it is not a map of pitch %d -- "
                        "drawing the game's own pitch\n", path, layout, (int)pitchNumber);
        ta_map_free(map);
        return 0;
    }
    s_pm.map = map;
    /* The layers in force are the player's, once the layout's event has run: the
     * file's own `visible` flags are whichever pitch it was saved showing. */
    s_pm.player = ta_player_new(map, NULL);
    if (!s_pm.player) {
        release_map();
        return 0;
    }
    ta_player_set_auto_swaps(s_pm.player, 0);
    ta_player_reset(s_pm.player, 0);
    if (event >= 0)
        ta_player_fire(s_pm.player, event, 0);     /* show/hide take effect inside the call */
    for (i = 0; i < map->tileset_count; i++) {
        uint32_t pal[256];
        int w = 0, h = 0;
        const char *name = map->tilesets[i].image;
        if (!name || !name[0])
            continue;
        s_pm.image[i] = (uint8_t *)malloc(IMAGE_CAP);
        snprintf(path, sizeof path, "%s/%s", base, name);
        if (!s_pm.image[i] || Platform_LoadIndexedBMP(path, s_pm.image[i], IMAGE_CAP, pal, &w, &h) != 0) {
            fprintf(stderr, "[pitch-map] pitch %d: tileset image %s did not load (an 8-bit BMP is "
                            "what is read) -- drawing the game's own pitch\n", (int)pitchNumber, path);
            release_map();
            return 0;
        }
        s_pm.imageW[i] = w;
        s_pm.imageH[i] = h;
        ta_map_set_image_size(map, i, w, h);
    }
    if (!s_pm.sit)
        s_pm.sit = (uint8_t *)malloc((size_t)CELLS * TILE_BYTES);
    if (!s_pm.stand)
        s_pm.stand = (uint8_t *)malloc((size_t)CELLS * TILE_BYTES);
    if (!s_pm.sit || !s_pm.stand) {
        release_map();
        return 0;
    }
    snprintf(s_pm.dir, sizeof s_pm.dir, "%s", dir);
    s_pm.pitch = pitchNumber;

    /* The layers over the players, and every tileset in true colour for them (their blocks can
     * come from any layer's): only when the map has one. */
    for (i = 0; i < map->layer_count && s_up.count < MAX_ABOVE; i++)
        if (map->layers[i].depth == TA_DEPTH_ABOVE)
            s_up.layer[s_up.count++] = i;
    for (i = 0; s_up.count && i < map->tileset_count; i++) {
        const size_t n = (size_t)s_pm.imageW[i] * (size_t)s_pm.imageH[i];
        size_t p;
        if (!s_pm.image[i] || !(s_up.rgba[i] = (uint8_t *)malloc(n * 4))) {
            above_release();
            break;
        }
        for (p = 0; p < n; p++) {
            const int32_t v = s_pm.image[i][p];
            uint8_t *o = s_up.rgba[i] + p * 4;
            if (v == map->transparent || v >= map->palette_count) {
                o[0] = o[1] = o[2] = o[3] = 0;
            } else {
                o[0] = map->palette[v].r;
                o[1] = map->palette[v].g;
                o[2] = map->palette[v].b;
                o[3] = 255;
            }
        }
        s_up.image[i].pixels = s_up.rgba[i];
        s_up.image[i].width = s_pm.imageW[i];
        s_up.image[i].height = s_pm.imageH[i];
        s_up.image[i].stride = s_pm.imageW[i] * 4;
    }
    fprintf(stderr, "[pitch-map] pitch %d from %s/%s: %d layers (%d over the players), %d tilesets, "
                    "%d events%s%s\n",
            (int)pitchNumber, base, shared ? "pitch.tmj" : "its own map", map->layer_count, (int)s_up.count,
            map->tileset_count, map->event_count, event >= 0 ? ", layout event " : "",
            event >= 0 ? layout : "");
    return 1;
}

/* The layer a swap track on `layer` swaps in, or -1: the fans standing. */
static int32_t swap_source(const ta_layer *layer)
{
    int32_t a, t;
    for (a = 0; a < layer->animation_count; a++)
        for (t = 0; t < layer->animations[a].track_count; t++) {
            const ta_track *track = &layer->animations[a].tracks[t];
            /* The runtime's own test (swap_usable): a source, another layer, of tiles. */
            if (track->kind == TA_TRACK_SWAP && track->source >= 0 &&
                track->source < s_pm.map->layer_count &&
                &s_pm.map->layers[track->source] != layer &&
                s_pm.map->layers[track->source].type == TA_LAYER_TILE)
                return track->source;
        }
    return -1;
}

/* One tile over `out`, the transparent entry skipped. 1 if the tile exists. */
static int32_t put_tile(uint8_t *out, uint32_t gid)
{
    const ta_map *map = s_pm.map;
    int32_t set, x = 0, y = 0, px, py;
    if (!(gid & TA_GID_MASK))
        return 0;
    set = ta_map_tileset_of(map, gid);
    if (set < 0 || set >= MAX_SETS || !s_pm.image[set])
        return 0;
    ta_tileset_tile(&map->tilesets[set], gid, &x, &y);
    if (x < 0 || y < 0 || x + TILE_PX > s_pm.imageW[set] || y + TILE_PX > s_pm.imageH[set])
        return 0;
    for (py = 0; py < TILE_PX; py++)
        for (px = 0; px < TILE_PX; px++) {
            /* Tiled's flips, undone to find the source pixel. */
            int32_t sx = px, sy = py;
            uint8_t v;
            if (gid & TA_FLIP_H) sx = TILE_PX - 1 - sx;
            if (gid & TA_FLIP_V) sy = TILE_PX - 1 - sy;
            if (gid & TA_FLIP_D) { int32_t t = sx; sx = sy; sy = t; }
            v = s_pm.image[set][(size_t)(y + sy) * s_pm.imageW[set] + x + sx];
            if ((int32_t)v != map->transparent)
                out[py * TILE_PX + px] = v;
        }
    return 1;
}

static void recolour(uint8_t *tile, uint8_t slot10, uint8_t slot11)
{
    int32_t i;
    for (i = 0; i < TILE_BYTES; i++) {
        if (tile[i] == 10)      tile[i] = slot10;
        else if (tile[i] == 11) tile[i] = slot11;
    }
}

/* The engine's tile index for a cell (0 = none, or not in its bank). */
static int32_t engine_index(const uint8_t *tile)
{
    if (!tile || tile < _pitchChunkyBuffer)
        return 0;
    return (int32_t)((size_t)(tile - _pitchChunkyBuffer) / TILE_BYTES);
}

static void PitchMap_Bake(void)
{
    const ta_map *map = s_pm.map;
    int32_t cell, l, stands = 0, drawn = 0, differ = 0, differStand = 0;
    const int check = getenv("SWOS_PITCH_MAP_CHECK") != NULL;

    s_pm.baked = 0;
    if (!map || !s_pm.player)
        return;
    memset(s_pm.sit, 0, (size_t)CELLS * TILE_BYTES);
    memset(s_pm.stand, 0, (size_t)CELLS * TILE_BYTES);
    memset(s_pm.have, 0, sizeof s_pm.have);

    for (cell = 0; cell < CELLS; cell++) {
        uint8_t *sit = s_pm.sit + (size_t)cell * TILE_BYTES;
        uint8_t *stand = s_pm.stand + (size_t)cell * TILE_BYTES;
        int32_t any = 0, standing = 0;
        for (l = 0; l < map->layer_count; l++) {
            const ta_layer *layer = &map->layers[l];
            int32_t source;
            uint32_t gid, other = 0;
            if (layer->type != TA_LAYER_TILE || layer->depth != TA_DEPTH_BELOW || !layer->data ||
                !ta_player_layer_visible(s_pm.player, l))
                continue;
            gid = layer->data[cell];
            source = swap_source(layer);
            if (source >= 0 && source < map->layer_count && map->layers[source].data)
                other = map->layers[source].data[cell];
            any |= put_tile(sit, gid);
            /* A fan cell has a tile on both layers of the pair. */
            if ((gid & TA_GID_MASK) && (other & TA_GID_MASK)) {
                put_tile(stand, other);
                standing = 1;
            } else {
                put_tile(stand, gid);
            }
        }
        if (any)
            s_pm.have[cell] |= 1, drawn++;
        if (any && standing)
            s_pm.have[cell] |= 2, stands++;
    }

    /* As the game's own bank is treated when it loads (dos_load_pitch): the Jaguar
     * tier's donated palette entries, then the kits. */
    JagGfx_RemapDonated(s_pm.sit, (uint32_t)CELLS * TILE_BYTES);
    JagGfx_RemapDonated(s_pm.stand, (uint32_t)CELLS * TILE_BYTES);
    for (cell = 0; cell < CELLS; cell++) {
        const uint8_t *mine = _s_pitchFullPtrs[cell], *pair;
        int32_t index = engine_index(mine);
        uint8_t c10, c11;
        if (!mine || !(s_pm.have[cell] & 1))
            continue;
        if (Pitch_FanTileKit(mine, &c10, &c11))
            recolour(s_pm.sit + (size_t)cell * TILE_BYTES, c10, c11);
        if (!(s_pm.have[cell] & 2))
            continue;
        /* The standing tile wears what the engine's own standing tile wears: its
         * pair is the next tile for a seated (odd) one, the one before otherwise. */
        pair = index > 0 ? ((index & 1) ? mine + TILE_BYTES : mine - TILE_BYTES) : mine;
        if (Pitch_FanTileKit(pair, &c10, &c11) || Pitch_FanTileKit(mine, &c10, &c11))
            recolour(s_pm.stand + (size_t)cell * TILE_BYTES, c10, c11);
    }
    s_pm.baked = 1;

    /* SWOS_PITCH_MAP_CHECK=1: is this the game's own pitch, entry for entry? Each
     * cell against the engine's tile, and each animated fan against its pair. */
    if (check) {
        for (cell = 0; cell < CELLS; cell++) {
            const uint8_t *mine = _s_pitchFullPtrs[cell];
            int32_t index = engine_index(mine);
            static const uint8_t none[TILE_BYTES];
            const uint8_t *shown = (s_pm.have[cell] & 1) ? s_pm.sit + (size_t)cell * TILE_BYTES : none;
            if (memcmp(shown, mine ? mine : none, TILE_BYTES) != 0) {
                if (++differ <= 8)
                    fprintf(stderr, "[pitch-map] check: cell %d (row %d col %d) is not the engine's "
                                    "tile %d\n", (int)cell, (int)(cell / GRID_COLS),
                            (int)(cell % GRID_COLS), (int)index);
            }
            /* Row 0 is the engine's fan pointer table, not a stand: never on screen,
             * never animated. */
            /* A map with no fans at all is the training ground, whose tiles 1..22 are
             * ordinary ground: the engine animates no crowd in a training game. */
            if (stands && mine && cell >= GRID_COLS && index >= 1 && index <= MAX_FAN_TILE_INDEX) {
                const uint8_t *pair = (index & 1) ? mine + TILE_BYTES : mine - TILE_BYTES;
                if (!(s_pm.have[cell] & 2) ||
                    memcmp(s_pm.stand + (size_t)cell * TILE_BYTES, pair, TILE_BYTES) != 0) {
                    if (++differStand <= 8)
                        fprintf(stderr, "[pitch-map] check: cell %d (row %d col %d): the standing "
                                        "fan is not the engine's tile %d%s\n", (int)cell,
                                (int)(cell / GRID_COLS), (int)(cell % GRID_COLS),
                                (int)engine_index(pair), (s_pm.have[cell] & 2) ? "" : " (the map has none)");
                }
            }
        }
        /* Every condition's palette too (DosGfx_BuildPalette reports each once). */
        {
            extern void DosGfx_BuildPalette(int pitchType, uint8_t *rgb888Out);
            uint8_t rgb[768];
            int32_t type;
            for (type = 0; type < 7; type++)
                DosGfx_BuildPalette((int)type, rgb);
        }
        fprintf(stderr, "[pitch-map] check: %d of %d cells differ from the engine's pitch; "
                        "%d animated fans' standing tiles differ\n", (int)differ, CELLS, (int)differStand);
    }
    fprintf(stderr, "[pitch-map] pitch %d drawn from its map: %d cells, %d of them fans with a "
                    "standing tile\n", (int)s_pm.pitch, (int)drawn, (int)stands);
}

static const uint8_t *PitchMap_Tile(int32_t cell, const uint8_t *engineTile)
{
    int32_t index;
    if (cell < 0 || cell >= CELLS || !(s_pm.have[cell] & 1))
        return NULL;
    index = engine_index(engineTile);
    if ((s_pm.have[cell] & 2) && index >= 1 && index <= MAX_FAN_TILE_INDEX && !(index & 1))
        return s_pm.stand + (size_t)cell * TILE_BYTES;
    return s_pm.sit + (size_t)cell * TILE_BYTES;
}

static int32_t PitchMap_Condition(int32_t pitchType, uint8_t *entry, uint8_t (*rgb)[3], int32_t max)
{
    /* kPitchPalettes' order (src/dos_graphics.c). */
    static const char *const names[7] = { "pitch-frozen", "pitch-muddy", "pitch-wet", "pitch-soft",
                                          "pitch-normal", "pitch-dry", "pitch-hard" };
    const ta_map *map = s_pm.map;
    const ta_event *event;
    int32_t e, a, n = 0;

    if (!PitchMap_Active() || pitchType < 0 || pitchType > 6)
        return 0;
    e = ta_map_event(map, names[pitchType]);
    if (e < 0)
        return 0;
    event = &map->events[e];
    for (a = 0; a < event->action_count; a++) {
        const ta_action *action = &event->actions[a];
        const ta_variant *v;
        int32_t i;
        if (action->kind != TA_DO_PALETTE || action->palette < 0 || action->palette >= map->variant_count)
            continue;
        v = &map->variants[action->palette];
        for (i = 0; i < v->count && n < max; i++, n++) {
            entry[n] = v->entries[i];
            rgb[n][0] = v->colors[i].r;
            rgb[n][1] = v->colors[i].g;
            rgb[n][2] = v->colors[i].b;
        }
    }
    return n;
}

/* A match or a replay begins to be shown: the runtime from its start, the layout's layers, and
 * the sky by the pitch condition -- the map's "clouds" event on a frozen, muddy, wet or soft
 * pitch, "clear-sky" on a normal, dry or hard one (the Weather mod's rain and snow go with the
 * first four). Each its own seed: its own wind. */
static void above_start(void)
{
    char layout[32];
    int32_t e;
    const int32_t type = ModApi_PitchType();
    ta_player_set_seed(s_pm.player, ModApi_Ticks() * 2654435761u + (uint32_t)s_pm.pitch);
    ta_player_reset(s_pm.player, 0);
    snprintf(layout, sizeof layout, "layout-%d", (int)s_pm.pitch);
    if ((e = ta_map_event(s_pm.map, layout)) >= 0)
        ta_player_fire(s_pm.player, e, 0);
    e = ta_map_event(s_pm.map, type >= 0 && type <= 3 ? "clouds" : "clear-sky");
    if (e >= 0)
        ta_player_fire(s_pm.player, e, 0);
    s_up.clock = 0;
}

static void above_draw(void)
{
    int32_t w = 0, h = 0, i;
    int16_t cx = 0, cy = 0;
    const int32_t showing = PitchMap_Active() && s_up.count && (ModApi_InMatch() || ModApi_InReplay());
    if (showing && !s_up.showing)
        above_start();
    s_up.showing = showing;
    if (!showing)
        return;
    ta_player_advance(s_pm.player, ++s_up.clock);
    ModApi_ScreenSize(&w, &h);
    ModApi_Camera(&cx, &cy);
    if (w <= 0 || h <= 0)
        return;
    if (w != s_up.bufW || h != s_up.bufH) {
        free(s_up.buf);
        s_up.buf = (uint8_t *)malloc((size_t)w * h * 4);
        s_up.bufW = s_up.buf ? w : 0;
        s_up.bufH = s_up.buf ? h : 0;
    }
    if (!s_up.buf)
        return;
    if (!s_up.tex || s_up.texW != w || s_up.texH != h) {
        if (s_up.tex)
            Gfx_DestroyTexture(s_up.tex);
        s_up.tex = Gfx_CreateTexture(w, h, 0);
        s_up.texW = w;
        s_up.texH = h;
    }
    if (!s_up.tex)
        return;
    for (i = 0; i < s_up.count; i++) {
        const int32_t l = s_up.layer[i];
        ta_layer_pose pose;
        if (!ta_player_layer_visible(s_pm.player, l))
            continue;
        ta_player_layer_pose(s_pm.player, l, &pose);
        if (pose.alpha <= 0)
            continue;
        {   /* drawn solid into a clear buffer, then blended at the layer's opacity */
            const ta_target32 target = { s_up.buf, w, h, w * 4, cx, cy };
            memset(s_up.buf, 0, (size_t)w * h * 4);
            ta_draw_layer32(s_pm.player, l, s_up.image, &target, TA_DRAW_ALL | TA_DRAW_OPAQUE);
        }
        Gfx_UpdateTexture(s_up.tex, s_up.buf, w * 4);
        {
            GfxVertex v[4];
            static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            int k;
            memset(v, 0, sizeof v);
            v[1].position.x = v[2].position.x = (float)w;
            v[2].position.y = v[3].position.y = (float)h;
            v[1].tex_coord.x = v[2].tex_coord.x = 1.0f;
            v[2].tex_coord.y = v[3].tex_coord.y = 1.0f;
            for (k = 0; k < 4; k++) {
                v[k].color.r = v[k].color.g = v[k].color.b = 255;
                v[k].color.a = (Uint8)(pose.alpha > 255 ? 255 : pose.alpha);
            }
            Gfx_SetSpace(GFX_SPACE_PICTURE);
            Gfx_SetBlend(GFX_BLEND_ALPHA);
            Gfx_Geometry(s_up.tex, v, 4, idx, 6);
        }
    }
}

/* ---- the hooks ---- */

static void pitch_loaded(const char *dir, int32_t pitchNumber) { PitchMap_Load(dir, pitchNumber); }
static ModPitchTileFn pitch_tiles(void) { return PitchMap_Active() ? PitchMap_Tile : NULL; }

const ModDef ModDef_pitchmaps = {
    .slug           = SLUG,
    .pitchLoaded    = pitch_loaded,
    .pitchColoured  = PitchMap_Bake,
    .pitchDropped   = PitchMap_Drop,
    .pitchTiles     = pitch_tiles,
    .pitchCondition = PitchMap_Condition,
    .frameDrawn     = above_draw,
};

#endif /* SWOS_SDL_BUILD */
