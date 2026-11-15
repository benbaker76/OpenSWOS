/* tileagent.h - a Tile Agent map in a game: load it, run it, draw it.
 *
 * Plain C99. No floating point, no 64-bit division, no wall clock, and nothing
 * from the C library but memcpy, memset, memcmp, strcmp and strlen (and malloc/free,
 * unless you pass an allocator and define TA_NO_STDLIB). It plays a map
 * exactly as the editor previews it: tests/runtime_tests.cpp runs this and the
 * editor's own player side by side, frame by frame.
 *
 * docs/FORMAT.md is the format, docs/RUNTIME.md is how to use this.
 *
 *     ta_map *map = ta_map_load(text, length, NULL, error, sizeof error);
 *     ta_player *player = ta_player_new(map, NULL);
 *     ta_player_reset(player, 0);
 *     int goal = ta_map_event(map, "goal");
 *     ...
 *     each frame:  ta_player_advance(player, frame);
 *                  if (scored) ta_player_fire(player, goal, 0);
 *                  for each layer: ta_draw_layer8(player, layer, images, &target, TA_DRAW_ALL);
 *
 * Everything a ta_map points at belongs to the map and is read-only.
 */
#ifndef TILEAGENT_H
#define TILEAGENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TA_FORMAT_VERSION 2

/* The top bits of a tile id are flags, as in Tiled. */
#define TA_FLIP_H 0x80000000u
#define TA_FLIP_V 0x40000000u
#define TA_FLIP_D 0x20000000u
#define TA_GID_MASK 0x0FFFFFFFu

#define TA_ONE 65536 /* 1.0 in 16.16 */

typedef struct ta_allocator {
    void *(*alloc)(void *user, size_t size);
    void (*release)(void *user, void *pointer);
    void *user;
} ta_allocator;

typedef struct ta_options {
    const ta_allocator *allocator; /* NULL: malloc and free */
    /* Layer data Tiled saved compressed (zlib or gzip) is handed to this to be
     * inflated into `out`, which holds exactly `out_size` bytes. Answer nonzero
     * on success. NULL: such a map is refused; save it with CSV layer data. */
    int (*inflate)(void *user, const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);
    void *inflate_user;
} ta_options;

typedef struct ta_color {
    uint8_t r, g, b, a;
} ta_color;

typedef struct ta_property {
    const char *name;
    const char *type;   /* "string", "int", "bool", ... */
    const char *text;   /* the value as text */
    int32_t number;     /* the value as a whole number, when it is one */
} ta_property;

/* What starts an animation or a cycle: an event's index, or one of these. */
#define TA_TRIGGER_ALWAYS (-1)
#define TA_TRIGGER_NEVER (-2)

typedef struct ta_cycle {
    const char *name;
    int first, last; /* palette entries, inclusive */
    int frames;      /* frames between steps */
    int reverse;
    int trigger;
} ta_cycle;

/* A named variant of the map's palette: the entries it changes and their
 * colors in it, in order - the file's swatches laid out an entry at a time. A
 * later one wins where two name the same entry. */
typedef struct ta_variant {
    const char *name;
    int count;
    const uint8_t *entries;  /* which palette entries it replaces ... */
    const ta_color *colors;  /* ... and with what */
} ta_variant;

typedef struct ta_tileset {
    const char *name;
    const char *image;   /* as written in the map: relative to it */
    const char *source;  /* an external tileset file not loaded yet, else NULL */
    uint32_t first_gid;
    int tile_width, tile_height, margin, spacing, columns, tile_count;
    int image_width, image_height;
    int has_transparent;
    ta_color transparent; /* Tiled's "transparentcolor"; a true-color map's color key */
} ta_tileset;

typedef struct ta_key {
    int f;
    int x, y, alpha, cel;
    int rate, period, on;
    uint8_t ease;
    uint8_t tween;
} ta_key;

enum { TA_TRACK_BLIT = 0, TA_TRACK_LAYER = 1, TA_TRACK_SWAP = 2 };

typedef struct ta_track {
    const char *name;
    int kind;
    int muted;
    int source;  /* blit: the layer the tiles come from. swap: the layer swapped in. -1: gone */
    int src_x, src_y, src_w, src_h; /* tiles. swap: the region */
    int frames, stride, cycle;
    int dst_x, dst_y;               /* tiles */
    int mask_x, mask_y, mask_w, mask_h; /* map pixels; no width = no mask */
    int anchor_x, anchor_y;         /* pixels */
    int wrap;
    int drift_speed, drift_change; /* layer: a wind that changes; speed 0 = none */
    uint32_t seed;
    int key_count;
    const ta_key *keys;
} ta_track;

typedef struct ta_animation {
    const char *id;
    int trigger;
    int delay, duration, loop, pingpong, ease;
    int then; /* event index, or -1 */
    int track_count;
    const ta_track *tracks;
} ta_animation;

/* An object on an object layer: a named point a game asks for ("where the
 * substitutes sit"), or a rectangle. Map pixels, rounded down. */
typedef struct ta_object {
    int id;
    const char *name;
    const char *class_name;  /* the file's "type" */
    int x, y;                /* the layer's offset NOT included */
    int width, height;       /* 0 for a point */
    int point;
    int visible;
    int property_count;
    const ta_property *properties;
} ta_object;

enum { TA_TILESET_NONE = -1, TA_TILESET_MIXED = -2 };
enum { TA_LAYER_TILE = 0, TA_LAYER_OBJECT = 1, TA_LAYER_IMAGE = 2, TA_LAYER_GROUP = 3 };
enum { TA_DEPTH_BELOW = 0, TA_DEPTH_SORTED = 1, TA_DEPTH_ABOVE = 2 };

typedef struct ta_layer {
    const char *name;
    const char *class_name;
    int type;
    int visible;
    int depth;
    int opacity;              /* 0..255 */
    int offset_x, offset_y;   /* pixels */
    int width, height;        /* tiles; the map's */
    const uint32_t *data;     /* width * height tile ids, flags included; NULL unless a tile layer */
    /* The one tileset every tile of the layer comes from - its own, and the
     * ones its swap tracks bring in - so it can be drawn from one tile sheet.
     * TA_TILESET_NONE: it has no tiles of its own (its blocks come from their
     * source layers, which have theirs). TA_TILESET_MIXED: a map made
     * elsewhere mixed several; look each tile up with ta_map_tileset_of. */
    int tileset;
    int animation_count;
    const ta_animation *animations;
    int property_count;
    const ta_property *properties;
    int object_count;          /* an object layer's objects, in the file's order */
    const ta_object *objects;
} ta_layer;

enum {
    TA_DO_PLAY, TA_DO_STOP, TA_DO_PAUSE, TA_DO_REWIND, TA_DO_SET,
    TA_DO_SHOW, TA_DO_HIDE, TA_DO_TOGGLE, TA_DO_RESTORE, TA_DO_PALETTE, TA_DO_FIRE,
    TA_DO_NOTHING
};
enum { TA_SET_DELAY, TA_SET_DURATION, TA_SET_LOOP, TA_SET_PINGPONG, TA_SET_EASE, TA_SET_NOTHING };

/* A named set of layers, which an action can be carried out on as one. */
typedef struct ta_group {
    const char *name;
    int count;
    const int *layers; /* indices, ascending */
} ta_group;

typedef struct ta_action {
    int kind;
    int layer;             /* index, -1 = no such layer */
    int group;             /* or a layer group's index: every layer in it; -1 = none */
    const char *animation; /* an id, or "*" */
    int attribute;
    int has_number;        /* set: the value was a number */
    int32_t number;        /* set: delay / duration */
    int flag;              /* set: loop / pingpong */
    int ease;              /* set: ease, -1 = not a curve's name */
    int palette;           /* palette: the variant's index, -1 = the map's own */
    int event;             /* fire: event index, -1 = none */
} ta_action;

typedef struct ta_event {
    const char *name;
    int action_count;
    const ta_action *actions;
} ta_event;

typedef struct ta_map {
    int width, height;            /* tiles */
    int tile_width, tile_height;  /* pixels */
    int fps;
    int version;                  /* of the "tileagent" object; 0 = a plain Tiled map */
    int layer_count;
    const ta_layer *layers;       /* bottom first */
    int tileset_count;
    const ta_tileset *tilesets;
    int event_count;
    const ta_event *events;
    int group_count;
    const ta_group *groups;
    int property_count;
    const ta_property *properties;
    /* An indexed map has ONE palette, and every tileset image holds entries of
     * it. palette_count is 0 for a true-color map. */
    int palette_count;
    const ta_color *palette;      /* alpha 255, but 0 for the transparent entry */
    int transparent;              /* the entry that is not drawn; -1 = none */
    int variant_count;
    const ta_variant *variants;
    int cycle_count;
    const ta_cycle *cycles;
    void *internal;
} ta_map;

/* ------------------------------------------------------------------ the map */

/* Reads a .tmj. NULL on failure, with the reason in `error` (may be NULL). */
ta_map *ta_map_load(const char *json, size_t length, const ta_options *options, char *error, size_t error_size);
/* Fills in a tileset the map only named ("source": "tiles.tsj") from that
 * file's text. Nonzero on success. */
int ta_map_load_tileset(ta_map *map, int tileset, const char *json, size_t length, char *error, size_t error_size);
/* Once the image is loaded: fits `columns` and `tile_count` to its real size,
 * as the editor does. */
void ta_map_set_image_size(ta_map *map, int tileset, int width, int height);
void ta_map_free(ta_map *map);

/* By name; -1 when there is none. */
int ta_map_event(const ta_map *map, const char *name);
int ta_map_layer(const ta_map *map, const char *name);
int ta_map_tileset(const ta_map *map, const char *name);
int ta_map_group(const ta_map *map, const char *name);
/* A property by name in a list of them (a map's, a layer's, an object's); NULL when there is none. */
const ta_property *ta_property_find(const ta_property *properties, int count, const char *name);
/* The tileset a tile id belongs to (-1: none), and the tile's place in its image. */
int ta_map_tileset_of(const ta_map *map, uint32_t gid);
void ta_tileset_tile(const ta_tileset *tileset, uint32_t gid, int *x, int *y);

/* --------------------------------------------------------------- the player */

typedef struct ta_player ta_player;

ta_player *ta_player_new(const ta_map *map, const ta_allocator *allocator);
void ta_player_free(ta_player *player);

/* A fresh start. `clock` is the frame your own frame counter reads now (0 if
 * you keep none). "always" animations and cycles have been running since frame
 * 0 of that clock. Nothing is drawn right until the first ta_player_advance. */
void ta_player_reset(ta_player *player, int32_t clock);
/* Mixed into every random sequence the map draws from (swap tracks, drifting
 * layers): give each match its own and its clouds blow their own way; give
 * two machines the same and they see the same. Call it before ta_player_reset.
 * 0 unless set, which is what the editor previews. */
void ta_player_set_seed(ta_player *player, uint32_t seed);
/* Moves time on to `frame`, a frame at a time. Time does not run backwards. */
void ta_player_advance(ta_player *player, int32_t frame);
int32_t ta_player_frame(const ta_player *player);
/* Fires an event. `age` > 0 says it happened that many frames ago: whatever it
 * starts is already that far in. */
void ta_player_fire(ta_player *player, int event, int32_t age);

/* --- what to draw --- */

typedef struct ta_layer_pose {
    int offset_x, offset_y; /* pixels: the layer's own offset plus its layer tracks */
    int alpha;              /* 0..255, the layer's opacity included */
    int wrap;
} ta_layer_pose;

typedef struct ta_blit {
    int source_layer;
    int src_x, src_y, w, h;  /* tiles, cel applied */
    int x, y;                /* map pixels, the layer's offset NOT included */
    int alpha;               /* 0..255, the layer's NOT included */
    int mask_x, mask_y, mask_w, mask_h;
    int anchor_x, anchor_y;  /* map pixels: sort by this */
    int animation, track;
} ta_blit;

int ta_player_layer_visible(const ta_player *player, int layer);
/* The object called `name` on the topmost shown object layer that has a shown
 * one; NULL when there is none. `x`, `y` (may be NULL) get where it is: its
 * own x, y plus that layer's offset. `layer` (may be NULL) gets which layer.
 * Layer groups and events decide which layer is shown, so each layout of a
 * map can put the same name somewhere else. */
const ta_object *ta_player_object(const ta_player *player, const char *name, int *x, int *y, int *layer);
void ta_player_layer_pose(const ta_player *player, int layer, ta_layer_pose *out);
/* The layer's blocks, in drawing order. Answers how many there are; writes at most `max`. */
int ta_player_blits(const ta_player *player, int layer, ta_blit *out, int max);
/* The tile shown at a cell: the layer's own, or the one a swap track put there. */
uint32_t ta_player_gid(const ta_player *player, int layer, int cx, int cy);
/* Which palette variant an event has put in force; -1 = the map's own. */
int ta_player_palette_choice(const ta_player *player);
/* The map's palette this frame - the variant in force, the cycles turned -
 * into `out`, which has room for 256. Answers how many entries; 0 for a
 * true-color map. */
int ta_player_palette(const ta_player *player, ta_color *out);

enum { TA_STOPPED = 0, TA_PLAYING = 1, TA_PAUSED = 2 };
int ta_player_status(const ta_player *player, int layer, int animation);
/* Frames since the animation started (its delay included); -1 while stopped. */
int32_t ta_player_clock(const ta_player *player, int layer, int animation);

/* --- swap tracks, when the game does the picking ---
 *
 * By default a swap track picks its own cells from its own random numbers. A
 * game whose crowd is part of its simulation turns that off and picks cells
 * itself; the map still says which cells may swap, with which tiles, how many
 * a frame and to what rhythm. */
void ta_player_set_auto_swaps(ta_player *player, int enabled);

typedef struct ta_swap_rhythm {
    int rate;        /* cells a frame */
    int period, on;  /* stand for the last `on` frames of every `period` */
} ta_swap_rhythm;

/* The rate asked of a region of cells: the sum over the playing swap tracks
 * on `layer` whose region touches it. 0 when none is playing. */
int ta_player_swap_rate(const ta_player *player, int layer, int x, int y, int w, int h);
/* The rhythm that applies to one cell. Answers 0 when no playing track holds it. */
int ta_player_swap_rhythm(const ta_player *player, int layer, int cx, int cy, ta_swap_rhythm *out);
/* 1 while `tick` is in the "swapped in" part of the rhythm. */
int ta_swap_wanted(const ta_swap_rhythm *rhythm, int32_t tick);
/* -1: the cell cannot swap (no tile on one of the two layers, or no swap track
 * covers it). Otherwise 1 swapped in, 0 not. */
int ta_player_swap_state(const ta_player *player, int layer, int cx, int cy);
/* Swaps the cell in or back. Answers 1 if that changed it, 0 if it was so
 * already, -1 if the cell cannot swap. */
int ta_player_swap_set(ta_player *player, int layer, int cx, int cy, int in);
void ta_player_swap_restore(ta_player *player, int layer);

/* --- state, for rollback and replays ---
 *
 * Everything the player knows is one block of plain bytes with no pointers in
 * it. Copy it out and back to return to a frame. */
size_t ta_player_state_size(const ta_player *player);
void ta_player_save(const ta_player *player, void *out);
void ta_player_load(ta_player *player, const void *state);

/* ------------------------------------------------------------------ drawing */

/* Optional. A game with its own tile renderer needs none of this. */

/* A tileset's pixels, a byte each: palette entries. */
typedef struct ta_image8 {
    const uint8_t *pixels;
    int width, height, stride;
    int transparent; /* the entry that is not drawn; -1 = none */
} ta_image8;

/* Where to draw: a byte-a-pixel buffer showing the map from (origin_x, origin_y). */
typedef struct ta_target8 {
    uint8_t *pixels;
    int width, height, stride;
    int origin_x, origin_y;
} ta_target8;

/* TA_DRAW_OPAQUE draws everything solid, whatever its alpha: for a host that
 * draws a layer into a buffer of its own and blends that itself. */
enum { TA_DRAW_TILES = 1, TA_DRAW_BLITS = 2, TA_DRAW_ALL = 3, TA_DRAW_OPAQUE = 4 };

/* Draws one layer if it is visible: its tiles (swapped cells, offset and wrap
 * applied), then its blocks. `images` has one entry per tileset of the map. In
 * eight bits nothing blends: a layer or block that is partly transparent is
 * drawn through a 4 x 4 ordered dither, fixed on the screen. */
void ta_draw_layer8(const ta_player *player, int layer, const ta_image8 *images, const ta_target8 *target, int what);
/* Draws one block from ta_player_blits - for a "sorted" layer, whose blocks a
 * game draws in among its own sprites. */
void ta_draw_blit8(const ta_player *player, int layer, const ta_blit *blit, const ta_image8 *images,
                   const ta_target8 *target);

/* The same in true color: 32-bit pixels, R G B A bytes, blended. */
typedef struct ta_image32 {
    const uint8_t *pixels;
    int width, height, stride;
} ta_image32;
typedef struct ta_target32 {
    uint8_t *pixels;
    int width, height, stride;
    int origin_x, origin_y;
} ta_target32;
void ta_draw_layer32(const ta_player *player, int layer, const ta_image32 *images, const ta_target32 *target, int what);
void ta_draw_blit32(const ta_player *player, int layer, const ta_blit *blit, const ta_image32 *images,
                    const ta_target32 *target);

/* ------------------------------------------------------------------- easing */

/* The 31 curves, 16.16 in and out. Identical to SWOS United's src/easing.c;
 * define TA_EASE(kind, t) before compiling tileagent.c to use another. */
int32_t ta_ease(int kind, int32_t t);
int ta_ease_from_name(const char *name); /* -1 = unknown */

#ifdef __cplusplus
}
#endif

#endif
