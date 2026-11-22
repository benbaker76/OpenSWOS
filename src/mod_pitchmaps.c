/* mod_pitchmaps.c -- the pitchmaps mod (bin/mods/pitchmaps; canonical, off by default): a DOS-tier
 * pitch's PICTURE from a Tile Map Editor map, <mod>/grafs_dos/pitch<N>.tmj or <graphics dir>/pitch<N>.tmj
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

#include "kit_palette.h"
#include "mod_api.h"
#include "mjson.h"              /* a package's package.json */
#include "mods.h"               /* Mods_Count, Mods_Dir: maps a mod carries */
#include "swos_io.h"            /* swos_data_path: the mods, the assets folder, the game */
#include "display.h"            /* MAX_FAN_TILE_INDEX */
#include "jag_graphics.h"       /* JagGfx_RemapDonated */
#include "../third_party/tilemapeditor/tilemapeditor.h"
#include "gfx.h"                /* the layers over the players, on either presenter */
#include "game_types.h"         /* Sprite, SpriteGraphics: the spriteDraw hook's */

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
    char     chosen[64];                /* the "Pitch map" setting it was loaded for */
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

static int32_t s_goalsLayer = -1, s_flagsLayer = -1;    /* the map's sorted "goals", "flags" */
static int32_t s_advertsLayer = -1;                     /* ... and "adverts" */
/* The crowd (stage 3): the map's own, picture only. The layer whose swap track makes its fans
 * stand, each cell's state as last drawn and whether the other buffer still needs it. */
static int32_t s_crowdLayer = -1;
static uint16_t s_crowdSeen = 0xFFFF;                   /* the engine's _animPatternsState we last told */
/* The ground, live (stage 4): each cell as the map draws it now -- every visible layer under the
 * players composed through the runtime (its swaps, the crowd's too, its moving and drifting
 * layers, its animated blocks, layers an event shows or hides) -- for the cells around the camera,
 * each frame. A cell that changed is drawn again, in both buffers. */
static uint8_t *s_dyn;                          /* CELLS tiles */
static uint8_t  s_dynHave[CELLS], s_dynAgain[CELLS];
static uint8_t  s_kit[CELLS][4], s_kitHas[CELLS]; /* the fans' kits (10, 11): seated 0-1, standing 2-3 */
static uint8_t *s_canvas;
static size_t   s_canvasCap;
static int32_t  s_checkFrames, s_checkCells, s_checkDiffer;
static uint32_t s_startClock, s_statChanged, s_statSorted;   /* (SWOS_PITCH_MAP_CHECK / _SHOT) */
static uint16_t s_advertSeen;                           /* the engine's _advertIndex we last told the map */
/* Any other sorted layer (stage 4: an author's ball boy, camera tower, ...): its blocks of this
 * frame, by anchor y, drawn among the engine's sprites as their y comes (the next not drawn yet:
 * s_pendNext); s_ox, s_oy: the world point at the frame's 0,0 this frame. */
#define MAX_EXTRA_SORTED 8
#define MAX_PENDING      64
static int32_t  s_extraSorted[MAX_EXTRA_SORTED], s_extraCount;
static ta_blit  s_pend[MAX_PENDING];
static int16_t  s_pendLayer[MAX_PENDING];
static int32_t  s_pendN, s_pendNext, s_ox, s_oy, s_haveO;
static int32_t sorted_layer(const char *name);
static int32_t swap_source(const ta_layer *layer);

static void above_release(void)
{
    s_goalsLayer = s_flagsLayer = s_advertsLayer = s_crowdLayer = -1;
    s_extraCount = s_pendN = s_pendNext = 0;
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
    char path[4096], base[512], error[160], layout[32], mapName[128] = "";
    char *text = NULL;
    size_t len = 0;
    int32_t i, shared = 0, event;
    ta_map *map;

    const char *chosen = ModApi_Setting(SLUG, "map", "off");
    s_pm.baked = 0;
    if (!dir)
        return 0;
    if (!chosen || !chosen[0])
        chosen = "off";
    if (s_pm.map && s_pm.pitch == pitchNumber && strcmp(s_pm.dir, dir) == 0 && strcmp(s_pm.chosen, chosen) == 0)
        return 1;                       /* the same pitch and map as last match */
    release_map();

    /* A map of this pitch alone, or ONE map of every pitch whose `layout-<N>` event
     * shows this one's layers (the game's six share their stands and differ in the
     * mowing). */
    /* Where the game's own files are looked for (swos_data_path): each enabled mod's folder first
     * -- this one's, or a pitch a player made and shipped as a mod (DLC) -- then the assets
     * folder, then beside the game. The map's images are read from the folder it was found in. */
    /* The map chosen in the mod's settings ("Pitch map": its uuid, a folder of the player's
     * moddata/pitchmaps/ -- where the DLC Manager's Pitch Maps install them -- or of pitchmaps/ in
     * this mod or another enabled one):
     * its pitch<N>.tmj, else its pitch.tmj. "off": grafs_dos/ as the game's own files are looked
     * for -- each enabled mod, the assets folder, the game (a developer's map). */
    if (strcmp(chosen, "off") != 0) {
        char where[3][900];
        int32_t n = 0, w, k;
        snprintf(where[n++], sizeof where[0], "%s/pitchmaps/%s", ModApi_ModDir(SLUG), chosen);
        snprintf(where[n++], sizeof where[0], "%s/%s", ModApi_DataDir(SLUG), chosen);   /* (moddata/pitchmaps/<uuid>) */
        for (w = 0; w < n && !text; w++) {
            /* a Tile Map Editor package (its "Export As Zip"): the map its package.json names */
            char mf[128] = "";
            size_t ml = 0;
            char *pkg;
            snprintf(path, sizeof path, "%s/package.json", where[w]);
            if ((pkg = read_text(path, &ml)) != NULL) {
                if (mjson_get_string(pkg, (int)ml, "$.map", mf, (int)sizeof mf) > 0 && !strchr(mf, '/') &&
                    !strchr(mf, '\\') && !strstr(mf, "..")) {
                    snprintf(path, sizeof path, "%s/%s", where[w], mf);
                    text = read_text(path, &len);
                    shared = text != NULL;
                }
                free(pkg);
                if (text)
                    break;
            }
            snprintf(path, sizeof path, "%s/pitch%d.tmj", where[w], (int)pitchNumber);
            text = read_text(path, &len);
            if (!text) {
                snprintf(path, sizeof path, "%s/pitch.tmj", where[w]);
                text = read_text(path, &len);
                shared = text != NULL;
            }
        }
        for (k = 0; k < Mods_Count() && !text; k++) {   /* (a mod that carries maps of its own) */
            snprintf(path, sizeof path, "%s/pitchmaps/%s/pitch.tmj", Mods_Dir(k), chosen);
            text = read_text(path, &len);
            shared = text != NULL;
        }
        if (!text) {
            fprintf(stderr, "[pitch-map] the pitch map \"%s\" is in none of %s, the mod's pitchmaps/ or an "
                            "enabled mod's: the game's own pitch\n", chosen, ModApi_DataDir(SLUG));
            return 0;
        }
    } else {
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
        snprintf(mapName, sizeof mapName, "%.127s", slash ? slash + 1 : path);
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
    snprintf(s_pm.chosen, sizeof s_pm.chosen, "%s", chosen);
    s_pm.pitch = pitchNumber;

    /* The layers over the players, and every tileset in true colour for them (their blocks can
     * come from any layer's): only when the map has one. */
    for (i = 0; i < map->layer_count && s_up.count < MAX_ABOVE; i++)
        if (map->layers[i].depth == TA_DEPTH_ABOVE)
            s_up.layer[s_up.count++] = i;
    s_goalsLayer = sorted_layer("goals");
    s_flagsLayer = sorted_layer("flags");
    s_advertsLayer = sorted_layer("adverts");
    for (i = 0; i < map->layer_count && s_extraCount < MAX_EXTRA_SORTED; i++)
        if (map->layers[i].type == TA_LAYER_TILE && map->layers[i].depth == TA_DEPTH_SORTED &&
            i != s_goalsLayer && i != s_flagsLayer && i != s_advertsLayer)
            s_extraSorted[s_extraCount++] = i;
    for (i = 0; i < map->layer_count && s_crowdLayer < 0; i++)
        if (map->layers[i].type == TA_LAYER_TILE && map->layers[i].depth == TA_DEPTH_BELOW &&
            swap_source(&map->layers[i]) >= 0)
            s_crowdLayer = i;
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
            (int)pitchNumber, base, mapName, map->layer_count, (int)s_up.count,
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
    memset(s_kitHas, 0, sizeof s_kitHas);

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
    KitPalette_RemapPitch(s_pm.sit, (uint32_t)CELLS * TILE_BYTES);     /* (kit_palette.c) */
    KitPalette_RemapPitch(s_pm.stand, (uint32_t)CELLS * TILE_BYTES);
    for (cell = 0; cell < CELLS; cell++) {
        const uint8_t *mine = _s_pitchFullPtrs[cell], *pair;
        int32_t index = engine_index(mine);
        uint8_t c10, c11;
        if (!mine || !(s_pm.have[cell] & 1))
            continue;
        s_kitHas[cell] = 0;
        if (Pitch_FanTileKit(mine, &c10, &c11)) {
            recolour(s_pm.sit + (size_t)cell * TILE_BYTES, c10, c11);
            s_kit[cell][0] = c10, s_kit[cell][1] = c11, s_kitHas[cell] |= 1;
        }
        if (!(s_pm.have[cell] & 2))
            continue;
        /* The standing tile wears what the engine's own standing tile wears: its
         * pair is the next tile for a seated (odd) one, the one before otherwise. */
        pair = index > 0 ? ((index & 1) ? mine + TILE_BYTES : mine - TILE_BYTES) : mine;
        if (Pitch_FanTileKit(pair, &c10, &c11) || Pitch_FanTileKit(mine, &c10, &c11)) {
            recolour(s_pm.stand + (size_t)cell * TILE_BYTES, c10, c11);
            s_kit[cell][2] = c10, s_kit[cell][3] = c11, s_kitHas[cell] |= 2;
        }
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
            if (cell < GRID_COLS)
                continue;               /* (row 0: never on screen, empty in a map -- docs/PITCH_MAPS.md) */
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
                        "%d animated fans' standing tiles differ\n", (int)differ, CELLS - GRID_COLS, (int)differStand);
    }
    fprintf(stderr, "[pitch-map] pitch %d drawn from its map: %d cells, %d of them fans with a "
                    "standing tile\n", (int)s_pm.pitch, (int)drawn, (int)stands);
}

static const uint8_t *PitchMap_Tile(int32_t cell, const uint8_t *engineTile)
{
    int32_t index;
    if (cell < 0 || cell >= CELLS || !(s_pm.have[cell] & 1))
        return NULL;
    if (s_up.showing && s_dyn && s_dynHave[cell])
        return s_dyn + (size_t)cell * TILE_BYTES;   /* the map's ground as it is now (stage 4) */
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
    event = e >= 0 ? &map->events[e] : NULL;
    for (a = 0; event && a < event->action_count; a++) {
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
    /* While a match or a replay shows the map (stage 4): its palette as it is now -- the colour
     * cycles turned and a variant an event has put in force -- for the entries those name. The
     * presenter asks every frame (DosGfx_BuildPalette). Picture only: the palette is no match
     * state. Every entry a cycle turns changes wherever it is used, as on any paletted screen. */
    if (s_up.showing && s_pm.player && map->palette_count > 0) {
        ta_color now[256];
        const int32_t count = ta_player_palette(s_pm.player, now);
        const int32_t choice = ta_player_palette_choice(s_pm.player);
        int32_t c, k;
        for (c = 0; c < map->cycle_count; c++)
            for (k = map->cycles[c].first; k <= map->cycles[c].last && k < count && n < max; k++, n++) {
                entry[n] = (uint8_t)k;
                rgb[n][0] = now[k].r;
                rgb[n][1] = now[k].g;
                rgb[n][2] = now[k].b;
            }
        if (choice >= 0 && choice < map->variant_count)
            for (k = 0; k < map->variants[choice].count && n < max; k++, n++) {
                const int32_t en = map->variants[choice].entries[k];
                entry[n] = (uint8_t)en;
                rgb[n][0] = now[en].r;
                rgb[n][1] = now[en].g;
                rgb[n][2] = now[en].b;
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
    {   /* the seed: from the match as it starts -- the same on two netplay peers, so both see the
         * same crowd and the same clouds (nothing needs them to: it is the picture) */
        extern uint8_t _seed, _randXorIndex;
        ta_player_set_seed(s_pm.player, ((uint32_t)_seed << 8 | _randXorIndex) * 2654435761u +
                                        (uint32_t)s_pm.pitch);
    }
    /* The crowd is the map's own: its swap tracks pick their fans from the map's random numbers,
     * never the match's (the engine's crowd still runs on the match's, unseen -- docs/PITCH_MAPS.md) */
    ta_player_set_auto_swaps(s_pm.player, s_crowdLayer >= 0);
    ta_player_reset(s_pm.player, s_up.clock);       /* ("always" animations: in step with the game's) */
    s_advertSeen = 0;
    s_crowdSeen = 0xFFFF;
    memset(s_dynHave, 0, sizeof s_dynHave);
    memset(s_dynAgain, 0, sizeof s_dynAgain);
    s_checkFrames = s_checkCells = s_checkDiffer = 0;
    s_startClock = (uint32_t)s_up.clock;
    s_statChanged = s_statSorted = 0;
    snprintf(layout, sizeof layout, "layout-%d", (int)s_pm.pitch);
    if ((e = ta_map_event(s_pm.map, layout)) >= 0)
        ta_player_fire(s_pm.player, e, 0);
    e = ta_map_event(s_pm.map, type >= 0 && type <= 3 ? "clouds" : "clear-sky");
    if (e >= 0)
        ta_player_fire(s_pm.player, e, 0);
    {   /* where the map puts the benches (bench_point: drawing only) */
        int px = 0, py = 0, l = -1, qx = 0, qy = 0;
        const ta_object *t = ta_player_object(s_pm.player, "bench-top", &px, &py, &l);
        const ta_object *b = ta_player_object(s_pm.player, "bench-bottom", &qx, &qy, &l);
        if (t || b)
            fprintf(stderr, "[pitch-map] match: benches from the map: top %d,%d, bottom %d,%d\n",
                    t ? px : -1, t ? py : -1, b ? qx : -1, b ? qy : -1);
    }
    fprintf(stderr, "[pitch-map] match: %d layer(s) over the players (%s), goals %s, corner flags %s, "
                    "crowd %s\n",
            (int)s_up.count, type >= 0 && type <= 3 ? "clouds" : "clear sky",
            s_goalsLayer >= 0 ? "from the map" : "the game's", s_flagsLayer >= 0 ? "from the map" : "the game's",
            s_crowdLayer >= 0 ? "the map's own (its random numbers, not the match's)" : "the game's");
}

/* The map's time is the game's frame counter (_frameCount, 16 bits, a tick a game frame, in a
 * match and its replays), unwrapped: the flags wave and the adverts step on the engine's beat.
 * Brought up to date before the sprites are drawn (spritesBegin, replayFrameBegin) and before the
 * layers over them (frameDrawn); a counter that jumps or runs back starts the map again there.
 * 0 when nothing is to be drawn from the map now. */
static int32_t tick(void)
{
    extern uint16_t _frameCount;
    static uint16_t last;
    const int32_t showing = PitchMap_Active() && s_pm.player && (ModApi_InMatch() || ModApi_InReplay());
    if (!showing) {
        s_up.showing = 0;
        return 0;
    }
    {
        const uint16_t d = (uint16_t)(_frameCount - last);
        if (!s_up.showing || d > 1000) {
            s_up.clock = _frameCount;
            last = _frameCount;
            s_up.showing = 1;
            above_start();
        } else if (d) {
            s_up.clock += d;
            last = _frameCount;
        }
    }
    ta_player_advance(s_pm.player, s_up.clock);     /* (at the same frame: nothing moves) */
    {   /* The crowd's mood: the engine's state machine (DrawAnimatedPatterns: what the match is
         * doing -- read, never changed), each change an event of the map's (Tile Map Editor's
         * docs/SWOS_UNITED.md 3.3). A goal: the stand of the team that scored -- "goal-top" when
         * the fans in the top stand are the scorers' -- aged by the goal clock, so arriving in the
         * middle of a celebration lands on its phase; a replay has no clock, so its phase's start. */
        extern uint16_t _animPatternsState, _patternsGoalCounter, _fansSidesSwapped;
        extern TeamGame *_teamScoredGamePtr;
        extern TeamGame _topTeamInGame, _bottomTeamInGame;
        const uint16_t st = _animPatternsState;
        if (s_crowdLayer >= 0 && st != s_crowdSeen) {
            const char *name = NULL;
            int32_t age = 0, e;
            const int32_t wasGoal = s_crowdSeen >= 2 && s_crowdSeen <= 5, isGoal = st >= 2 && st <= 5;
            if (st == 6)
                name = "match-start";
            else if (st == 7)
                name = "play-on";
            else if (st == 1)
                name = "stoppage";
            else if (isGoal && !wasGoal) {
                const TeamGame *topFans = _fansSidesSwapped ? &_bottomTeamInGame : &_topTeamInGame;
                name = _teamScoredGamePtr == topFans ? "goal-top" : "goal-bottom";
                if (ModApi_InReplay())
                    age = st == 3 ? 24 : st == 5 ? 199 : 0;
                else
                    age = _patternsGoalCounter > 0 ? _patternsGoalCounter - 1 : 0;
            }
            if (name && (e = ta_map_event(s_pm.map, name)) >= 0)
                ta_player_fire(s_pm.player, e, age);
            s_crowdSeen = st;
        }
    }
    {   /* The adverts: the engine decides when and which (ManageAdvertisements: its dice, the
         * foul's x, the bail-outs) and the map shows them -- "advert-N" when it starts one, with
         * the head start that puts the map's 8-frame steps on the engine's beat, "adverts-off"
         * when it stops (Tile Map Editor docs/SWOS_UNITED.md 3.4). Live only: a replay has its own. */
        extern uint16_t _advertIndex;
        if (!ModApi_InReplay() && _advertIndex != s_advertSeen) {
            char name[16];
            int32_t e = -1;
            if (_advertIndex >= 1 && _advertIndex <= 3) {
                snprintf(name, sizeof name, "advert-%u", (unsigned)_advertIndex);
                e = ta_map_event(s_pm.map, name);
                if (e >= 0)
                    ta_player_fire(s_pm.player, e, (int32_t)(((uint16_t)(_frameCount - 1) & 7) + 1));
            } else if (s_advertSeen >= 1 && s_advertSeen <= 3) {
                e = ta_map_event(s_pm.map, "adverts-off");
                if (e >= 0)
                    ta_player_fire(s_pm.player, e, 0);
            }
            if (getenv("SWOS_PITCH_MAP_CHECK"))
                fprintf(stderr, "[pitch-map] check: the engine's advert %d -> %d at frame %u: %s\n",
                        (int)(int16_t)s_advertSeen, (int)(int16_t)_advertIndex, (unsigned)_frameCount,
                        e >= 0 ? "the map's event fired" : "no event in the map");
            s_advertSeen = _advertIndex;
        }
    }
    return 1;
}

/* The cells around the camera, composed again from every visible layer under the players (the
 * runtime's own drawing, ta_draw_layer8: swaps, offsets, drift, blocks, as the editor shows them),
 * each as the bake treats it (the Jaguar tier's donated entries, the fans' kits). One that changed
 * since it was last drawn is drawn again now, before the sprites, into the frame and the mask
 * buffer -- and once more next frame, for the other of the two buffers (the engine's own fan flips
 * draw twice the same way). Two cells of margin, so a cell is fresh before it scrolls in (then
 * DrawChunkyPitch draws it through PitchMap_Tile). Row 0 is never on screen (the camera's top
 * stops at 16) and is not composed. */
static void ground_compose(void)
{
    const ta_map *map = s_pm.map;
    ta_image8 img[MAX_SETS];
    int16_t cx = 0, cy = 0;
    int32_t w = 0, h = 0, c0, r0, c1, r1, W, H, l, r, c, i;
    const int check = s_checkFrames < 25 && getenv("SWOS_PITCH_MAP_CHECK") != NULL;
    if (!map || !s_pm.player)
        return;
    if (!s_dyn && !(s_dyn = (uint8_t *)malloc((size_t)CELLS * TILE_BYTES)))
        return;
    ModApi_Camera(&cx, &cy);
    ModApi_ScreenSize(&w, &h);
    c0 = cx / TILE_PX - 2;
    r0 = cy / TILE_PX - 2;
    c1 = (cx + w) / TILE_PX + 2;
    r1 = (cy + h) / TILE_PX + 2;
    if (c0 < 0) c0 = 0;
    if (r0 < 1) r0 = 1;
    if (c1 > GRID_COLS - 1) c1 = GRID_COLS - 1;
    if (r1 > GRID_ROWS - 1) r1 = GRID_ROWS - 1;
    if (c1 < c0 || r1 < r0)
        return;
    W = (c1 - c0 + 1) * TILE_PX;
    H = (r1 - r0 + 1) * TILE_PX;
    if ((size_t)W * H > s_canvasCap) {
        free(s_canvas);
        s_canvas = (uint8_t *)malloc((size_t)W * H);
        s_canvasCap = s_canvas ? (size_t)W * H : 0;
        if (!s_canvas)
            return;
    }
    memset(s_canvas, 0, (size_t)W * H);
    for (i = 0; i < MAX_SETS; i++) {
        img[i].pixels = s_pm.image[i];
        img[i].width = s_pm.imageW[i];
        img[i].height = s_pm.imageH[i];
        img[i].stride = s_pm.imageW[i];
        img[i].transparent = map->transparent;
    }
    {
        const ta_target8 target = { s_canvas, W, H, W, c0 * TILE_PX, r0 * TILE_PX };
        for (l = 0; l < map->layer_count; l++)
            if (map->layers[l].type == TA_LAYER_TILE && map->layers[l].depth == TA_DEPTH_BELOW &&
                ta_player_layer_visible(s_pm.player, l))
                ta_draw_layer8(s_pm.player, l, img, &target, TA_DRAW_ALL);
    }
    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++) {
            const int32_t cell = r * GRID_COLS + c;
            uint8_t tile[TILE_BYTES], *dst = s_dyn + (size_t)cell * TILE_BYTES;
            int32_t y, st;
            if (!(s_pm.have[cell] & 1))
                continue;                       /* (the surround: nothing, as at the bake) */
            for (y = 0; y < TILE_PX; y++)
                memcpy(tile + y * TILE_PX, s_canvas + (size_t)((r - r0) * TILE_PX + y) * W + (c - c0) * TILE_PX, TILE_PX);
            JagGfx_RemapDonated(tile, TILE_BYTES);
            KitPalette_RemapPitch(tile, TILE_BYTES);
            st = s_crowdLayer >= 0 ? ta_player_swap_state(s_pm.player, s_crowdLayer, c, r) : -1;
            if (st == 1 && (s_kitHas[cell] & 2))
                recolour(tile, s_kit[cell][2], s_kit[cell][3]);
            else if (s_kitHas[cell] & 1)
                recolour(tile, s_kit[cell][0], s_kit[cell][1]);
            if (check && !s_dynHave[cell] && !(s_pm.have[cell] & 2)) {
                /* SWOS_PITCH_MAP_CHECK: a cell no fan stands in, as first composed, against the bake
                 * (which is checked against the engine's own tiles at the kick-off) */
                s_checkCells++;
                s_checkDiffer += memcmp(tile, s_pm.sit + (size_t)cell * TILE_BYTES, TILE_BYTES) != 0;
            }
            if (!s_dynHave[cell] || memcmp(tile, dst, TILE_BYTES) != 0) {
                if (s_dynHave[cell])
                    s_statChanged++;
                memcpy(dst, tile, TILE_BYTES);
                s_dynHave[cell] = 1;
                s_dynAgain[cell] = 1;
                ModApi_RedrawPitchCell(cell);
            } else if (s_dynAgain[cell]) {
                s_dynAgain[cell] = 0;
                ModApi_RedrawPitchCell(cell);
            }
        }
    if (check && ++s_checkFrames == 25)
        fprintf(stderr, "[pitch-map] check: the live ground, %d cells as first composed: %d differ from "
                        "the bake\n", (int)s_checkCells, (int)s_checkDiffer);
}

static void sorted_collect(void);

static void sprites_begin(void)
{
    if (tick()) {
        ground_compose();
        sorted_collect();
    }
}

static void replay_frame_begin(int32_t first, int32_t fresh)
{
    (void)first; (void)fresh;
    if (tick()) {
        ground_compose();
        sorted_collect();
    }
}

/* SWOS_PITCH_MAP_SHOT=<n>[,<n>...]: the game's own frame (8 bits, the palette in force: no
 * presenter, no clouds) written as pitchmap_shot_<n>.ppm in the current folder <n> frames into the
 * match -- a test's picture at a known moment, whenever the match starts (the rig presents
 * nothing). With SWOS_PITCH_MAP_CHECK the moving ground and the sorted blocks are counted too. */
static void shot_and_stats(void)
{
    const char *want = getenv("SWOS_PITCH_MAP_SHOT");
    const uint32_t at = (uint32_t)s_up.clock - s_startClock;
    if (want && want[0]) {
        const char *p = want;
        while (*p) {
            if ((uint32_t)strtoul(p, NULL, 10) == at) {
                char name[64];
                int32_t w = 0, h = 0, stride = 0, x, y;
                uint8_t *frame = ModApi_Frame(&stride);
                FILE *f;
                ModApi_ScreenSize(&w, &h);
                snprintf(name, sizeof name, "pitchmap_shot_%u.ppm", (unsigned)at);
                if (frame && (f = fopen(name, "wb")) != NULL) {
                    fprintf(f, "P6\n%d %d\n255\n", (int)w, (int)h);
                    for (y = 0; y < h; y++)
                        for (x = 0; x < w; x++) {
                            uint8_t rgb[3];
                            ModApi_PaletteColour(frame[(size_t)y * stride + x], &rgb[0], &rgb[1], &rgb[2]);
                            fwrite(rgb, 1, 3, f);
                        }
                    fclose(f);
                    fprintf(stderr, "[pitch-map] shot: %s\n", name);
                }
            }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }
    if (getenv("SWOS_PITCH_MAP_CHECK") && at > 0 && at % 500 == 0)
        fprintf(stderr, "[pitch-map] check: %u frames in: %u ground cells changed by the map, %u blocks of "
                        "other sorted layers drawn\n", (unsigned)at, (unsigned)s_statChanged, (unsigned)s_statSorted);
}

static void above_draw(void)
{
    int32_t w = 0, h = 0, i;
    int16_t cx = 0, cy = 0;
    if (!tick())
        return;
    shot_and_stats();
    if (!s_up.count)
        return;
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

/* ---- the goals and the corner flags (stage 2: sorted layers, drawn among the sprites) ----
 * The engine keeps its sprites -- the goals' fronts (images 1091, 1092: the net over a ball that
 * crossed the line) and the corner flag (1070-1073, moved to the corner in view) are match state
 * and stay in its sorted list -- and the map says what they look like: where the engine draws
 * one, the mod draws the block of the map's "goals" or "flags" layer nearest to it instead, in
 * the same place in the drawing order, and the engine's is left out. Tile Map Editor's SWOS map has a
 * block at each sprite's position (docs/SWOS_UNITED.md 3.5, 3.6). Its time is the map's own
 * (above_draw's clock): a waving flag's cel is not the engine's frame for frame. */
enum { IMG_FLAG_FIRST = 1070, IMG_FLAG_LAST = 1073, IMG_GOAL_TOP = 1091, IMG_GOAL_BOTTOM = 1092,
       IMG_ADVERT_FIRST = 113, IMG_ADVERT_LAST = 136 };     /* (adverts.c: three strips of eight) */

static int32_t sorted_layer(const char *name)
{
    int32_t l;
    for (l = 0; s_pm.map && l < s_pm.map->layer_count; l++)
        if (s_pm.map->layers[l].depth == TA_DEPTH_SORTED && s_pm.map->layers[l].name &&
            strcmp(s_pm.map->layers[l].name, name) == 0)
            return l;
    return -1;
}

/* The engine's sprite `image`, its world top-left (wx, wy) drawn at the frame's (sx, sy): 1 when
 * the map's block was drawn in its place. */
static int32_t block_instead(uint16_t image, int32_t wx, int32_t wy, int32_t sx, int32_t sy)
{
    ta_blit b[16];
    ta_image8 img[MAX_SETS];
    int32_t layer, n, i, best = -1, w = 0, h = 0, stride = 0;
    int64_t bestD = 0;
    if (!PitchMap_Active() || !s_pm.player)
        return 0;
    if (image == IMG_GOAL_TOP || image == IMG_GOAL_BOTTOM)
        layer = s_goalsLayer;
    else if (image >= IMG_FLAG_FIRST && image <= IMG_FLAG_LAST)
        layer = s_flagsLayer;
    else if (image >= IMG_ADVERT_FIRST && image <= IMG_ADVERT_LAST) {
        /* the eight strips are one block in the map: drawn at the first, the rest left out */
        if (ModApi_InReplay() || s_advertsLayer < 0)
            return 0;
        layer = s_advertsLayer;
        if ((image - IMG_ADVERT_FIRST) % 8 != 0)
            return ta_player_layer_visible(s_pm.player, layer);
    } else
        return 0;
    if (layer < 0 || !ta_player_layer_visible(s_pm.player, layer))
        return 0;
    n = ta_player_blits(s_pm.player, layer, b, 16);
    if (n > 16)
        n = 16;
    for (i = 0; i < n; i++) {   /* the block whose own top-left is nearest the sprite's */
        const int64_t dx = b[i].x - wx, dy = b[i].y - wy, d = dx * dx + dy * dy;
        if (best < 0 || d < bestD)
            best = i, bestD = d;
    }
    if (best < 0)
        return 0;
    for (i = 0; i < MAX_SETS; i++) {
        img[i].pixels = s_pm.image[i];
        img[i].width = s_pm.imageW[i];
        img[i].height = s_pm.imageH[i];
        img[i].stride = s_pm.imageW[i];
        img[i].transparent = s_pm.map->transparent;
    }
    ModApi_ScreenSize(&w, &h);
    {
        uint8_t *frame = ModApi_Frame(&stride);
        /* the frame's pixel (sx, sy) is the world's (wx, wy) */
        const ta_target8 target = { frame, w, h, stride, wx - sx, wy - sy };
        if (!frame)
            return 0;
        ta_draw_blit8(s_pm.player, layer, &b[best], img, &target);
    }
    return 1;
}

/* SWOS_PITCH_MAP_CHECK=1: the first time each goal or flag image is drawn from the map, its block
 * against the engine's sprite over the sprite's rectangle, pixel for pixel (its transparent 0s
 * are not compared: there the pitch shows either way). */
static void check_block(uint16_t image, const struct SpriteGraphics *gfx, int32_t wx, int32_t wy)
{
    static uint8_t told[8];
    static uint8_t buf[128 * 64];
    ta_blit b[16];
    ta_image8 img[MAX_SETS];
    int32_t slot, layer, n, i, best = -1, differ = 0, total = 0, px, py;
    int64_t bestD = 0;
    if (!getenv("SWOS_PITCH_MAP_CHECK") || !gfx || !gfx->sprData || gfx->pixWidth > 128 || gfx->nlines > 64)
        return;
    static int32_t adTotal, adDiffer, adStrips;
    const int32_t advert = image >= IMG_ADVERT_FIRST && image <= IMG_ADVERT_LAST;
    if (!(advert || image == IMG_GOAL_TOP || image == IMG_GOAL_BOTTOM ||
          (image >= IMG_FLAG_FIRST && image <= IMG_FLAG_LAST)))
        return;
    if (advert) {           /* every strip drawn: a running total (the map's steps against the engine's) */
        if (ModApi_InReplay() || s_advertsLayer < 0 || !ta_player_layer_visible(s_pm.player, s_advertsLayer))
            return;
        layer = s_advertsLayer;
    } else {
        slot = image >= IMG_GOAL_TOP ? 4 + (image - IMG_GOAL_TOP) : image - IMG_FLAG_FIRST;
        if (slot < 0 || slot >= 8 || told[slot])
            return;
        told[slot] = 1;
        layer = image >= IMG_GOAL_TOP ? s_goalsLayer : s_flagsLayer;
    }
    n = ta_player_blits(s_pm.player, layer, b, 16);
    for (i = 0; i < n && i < 16; i++) {
        const int64_t dx = b[i].x - wx, dy = b[i].y - wy, d = dx * dx + dy * dy;
        if (best < 0 || d < bestD)
            best = i, bestD = d;
    }
    if (best < 0)
        return;
    for (i = 0; i < MAX_SETS; i++) {
        img[i].pixels = s_pm.image[i];
        img[i].width = s_pm.imageW[i];
        img[i].height = s_pm.imageH[i];
        img[i].stride = s_pm.imageW[i];
        img[i].transparent = s_pm.map->transparent;
    }
    memset(buf, 0, sizeof buf);
    {
        const ta_target8 target = { buf, gfx->pixWidth, gfx->nlines, 128, wx, wy };
        ta_draw_blit8(s_pm.player, layer, &b[best], img, &target);
    }
    for (py = 0; py < gfx->nlines; py++)
        for (px = 0; px < gfx->pixWidth; px++) {
            const uint8_t e = gfx->sprData[py * gfx->wquads * 16 + px];
            if (!e)
                continue;
            total++;
            differ += buf[py * 128 + px] != e;
        }
    if (advert) {
        adTotal += total;
        adDiffer += differ;
        if (++adStrips % 64 == 0)
            fprintf(stderr, "[pitch-map] check: adverts, %d strips drawn so far: %d of %d pixels differ "
                            "from the game's\n", (int)adStrips, (int)adDiffer, (int)adTotal);
        return;
    }
    fprintf(stderr, "[pitch-map] check: image %u from the map's block at %d,%d: %d of %d pixels differ "
                    "from the game's sprite\n", (unsigned)image, b[best].x, b[best].y, (int)differ, (int)total);
}

/* The frame's blocks of the other sorted layers, by anchor y (spritesBegin, replayFrameBegin). */
static void sorted_collect(void)
{
    int32_t k, n, i, j;
    s_pendN = s_pendNext = 0;
    s_haveO = 0;
    for (k = 0; k < s_extraCount; k++) {
        ta_blit b[16];
        const int32_t l = s_extraSorted[k];
        if (!ta_player_layer_visible(s_pm.player, l))
            continue;
        n = ta_player_blits(s_pm.player, l, b, 16);
        for (i = 0; i < n && i < 16 && s_pendN < MAX_PENDING; i++) {
            for (j = s_pendN; j > 0 && s_pend[j - 1].anchor_y > b[i].anchor_y; j--) {
                s_pend[j] = s_pend[j - 1];
                s_pendLayer[j] = s_pendLayer[j - 1];
            }
            s_pend[j] = b[i];
            s_pendLayer[j] = (int16_t)l;
            s_pendN++;
        }
    }
}

/* Draws the pending blocks whose anchor is at or above `y` (all of them: INT32_MAX). */
static void sorted_flush(int32_t y)
{
    ta_image8 img[MAX_SETS];
    int32_t i, w = 0, h = 0, stride = 0;
    uint8_t *frame;
    if (s_pendNext >= s_pendN || s_pend[s_pendNext].anchor_y > y)
        return;
    for (i = 0; i < MAX_SETS; i++) {
        img[i].pixels = s_pm.image[i];
        img[i].width = s_pm.imageW[i];
        img[i].height = s_pm.imageH[i];
        img[i].stride = s_pm.imageW[i];
        img[i].transparent = s_pm.map->transparent;
    }
    if (!s_haveO) {                     /* (no sprite yet: the camera's) */
        int16_t cx = 0, cy = 0;
        ModApi_Camera(&cx, &cy);
        s_ox = cx;
        s_oy = cy;
    }
    ModApi_ScreenSize(&w, &h);
    frame = ModApi_Frame(&stride);
    if (!frame)
        return;
    {
        const ta_target8 target = { frame, w, h, stride, s_ox, s_oy };
        while (s_pendNext < s_pendN && s_pend[s_pendNext].anchor_y <= y) {
            ta_draw_blit8(s_pm.player, s_pendLayer[s_pendNext], &s_pend[s_pendNext], img, &target);
            s_pendNext++;
            s_statSorted++;
        }
    }
}

static void sprites_end(void)
{
    if (PitchMap_Active() && s_up.showing && s_pm.player)
        sorted_flush(0x7FFFFFFF);
}

static int32_t sprite_draw(const struct Sprite *sprite, const struct SpriteGraphics *gfx, int16_t *x, int16_t *y)
{
    if (sprite && gfx && PitchMap_Active() && s_up.showing && s_pm.player && s_pendNext < s_pendN) {
        extern int _menu_draw_x_offset;
        s_ox = sprite->x.s.i - gfx->centerX - (*x + _menu_draw_x_offset);
        s_oy = sprite->y.s.i - gfx->centerY - sprite->z.s.i - *y;
        s_haveO = 1;
        /* the blocks standing further up the pitch first; all of them before the HUD's sprites */
        sorted_flush(sprite->z.s.i >= 8000 ? 0x7FFFFFFF : sprite->y.s.i);
    }
    extern int _menu_draw_x_offset;
    if (!sprite || !gfx)
        return 0;
    if (PitchMap_Active() && s_pm.player)
        check_block((uint16_t)sprite->imageIndex, gfx, sprite->x.s.i - gfx->centerX,
                    sprite->y.s.i - gfx->centerY - sprite->z.s.i);
    return block_instead((uint16_t)sprite->imageIndex, sprite->x.s.i - gfx->centerX,
                         sprite->y.s.i - gfx->centerY - sprite->z.s.i, *x + _menu_draw_x_offset, *y);
}

static int32_t replay_sprite_draw(uint16_t image, int16_t left, int16_t top, int16_t sx, int16_t sy)
{
    if (PitchMap_Active() && s_up.showing && s_pm.player && s_pendNext < s_pendN) {
        extern uint8_t *_spriteGraphicsPtr;
        const struct SpriteGraphics *g = ((struct SpriteGraphics **)_spriteGraphicsPtr)[image];
        s_ox = left - sx;
        s_oy = top - sy;
        s_haveO = 1;
        sorted_flush(top + (g ? g->centerY : 0));   /* (its feet: the y the live match sorted it by) */
    }
    return block_instead(image, left, top, sx, sy);
}

/* ---- the benches (Tile Map Editor's named points: docs/SWOS_UNITED.md 3.8) ----
 * A map's "bench-top" / "bench-bottom" point, on the topmost shown object layer (each layout can
 * have its own: the training ground's both at 27,456), is the engine's base point of that bench;
 * swos:seat-step, swos:column-seats, swos:column-step, swos:column-dy and swos:arrow-dx its
 * layout. Drawing only (bench_subs.c bench_geom): the camera and the substitutions keep the
 * engine's. No point: the engine's bench. */
static void bench_prop(const ta_object *o, const char *name, int16_t *v)
{
    const ta_property *p = ta_property_find(o->properties, o->property_count, name);
    if (p && p->text && p->text[0])
        *v = (int16_t)p->number;
}

static int32_t bench_point(int32_t top, int16_t *x, int16_t *y, ModBenchSteps *steps)
{
    int px = 0, py = 0, layer = -1;
    const ta_object *o;
    if (!PitchMap_Active() || !s_pm.player)
        return 0;
    o = ta_player_object(s_pm.player, top ? "bench-top" : "bench-bottom", &px, &py, &layer);
    if (!o)
        return 0;
    *x = (int16_t)px;
    *y = (int16_t)py;
    bench_prop(o, "swos:seat-step", &steps->seatStep);
    bench_prop(o, "swos:column-seats", &steps->columnSeats);
    bench_prop(o, "swos:column-step", &steps->columnStep);
    bench_prop(o, "swos:column-dy", &steps->columnDy);
    bench_prop(o, "swos:arrow-dx", &steps->arrowDx);
    return 1;
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
    .spritesBegin   = sprites_begin,
    .spritesEnd     = sprites_end,
    .replayFrameBegin = replay_frame_begin,
    .spriteDraw     = sprite_draw,
    .benchPoint     = bench_point,
    .replaySpriteDraw = replay_sprite_draw,
};

#endif /* SWOS_SDL_BUILD */
