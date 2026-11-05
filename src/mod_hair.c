/*
 * mod_hair.c -- the hair mod (canonical, bin/mods/hair; docs/MODS.md §16): players with more than
 * one head of hair. Each outfield player gets a look -- his hair blonde or black, receding, shaved,
 * or a ponytail -- drawn over his sprite as it is drawn. SDL builds.
 *
 * The game draws every outfield player of a team from the same three sets of 101 frames (one per
 * face), recoloured per match from one raw set (player_face3.c says how). The raw set is read once
 * a match (ModHooks_PlayerSetsRaw), before it is recoloured: where its hair is, frame by frame (raw
 * colours 9, 12, 13; skin is 4, 5, 6). When a player is drawn (ModHooks_PlayerDrawn), the pixels of
 * his frame are worked out again for his look, in raw colours, and each one that differs from what
 * his face would make of the raw frame is written through his face's colour table -- only where his
 * sprite is what the frame shows there, so a sprite drawn over him earlier is left alone. A
 * ponytail is a few pixels beside the head, behind it.
 *
 * A player's look comes from looks.csv in the mod's folder when it names him (the SWOS 96/97 Mod's
 * bald-looks format: country,club,name,look[,face]; the looks natural, blonde, black, receding,
 * shaved, ponytail), else from his name: the same player always looks the same. Shaved heads are
 * drawn from shavedart.json in the mod's folder when it is there (the Mod's art, per frame), else
 * the hair simply becomes the skull's colour.
 *
 * Cosmetic: draws into the frame, which is not match state (src/sim_state.c), from the players'
 * names, faces and frames, never changing any of them.
 */
#include "mod_hooks.h"

#ifdef SWOS_SDL_BUILD

#include "game_types.h"
#include "mod_api.h"
#include "player_face3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t  *_spriteGraphicsPtr;
extern Sprite  **_team1SpritesTable;
extern Sprite  **_team2SpritesTable;
extern TeamGame *_topTeamPtr;
extern TeamGame *_bottomTeamPtr;
extern void     *_skinAndHairColorConversionTable[];

enum {
    kSet = 101,
    kW = 16,                        /* a player frame is one quad wide */
    kH = 24,
    kFirstRaw = 227,                /* team 1's first (raw) set */
};
static const int16_t kSetBase[2] = { 227, 530 };

typedef enum { HAIR_NATURAL, HAIR_BLONDE, HAIR_BLACK } HairColour;
typedef enum { SHAPE_FULL, SHAPE_RECEDING, SHAPE_SHAVED, SHAPE_PONYTAIL } HairShape;

typedef struct {
    uint8_t colour, shape;
} Look;

/* ---- the raw frames ---- */

static uint8_t s_raw[kSet][kH][kW];             /* raw colours, 0 transparent */
static uint8_t s_lines[kSet], s_width[kSet];
static int8_t  s_hairTop[kSet], s_hairBottom[kSet], s_hairLeft[kSet], s_hairRight[kSet];
static int8_t  s_centerY[kSet];
static int     s_haveRaw;

static int is_hair(int c) { return c == 9 || c == 12 || c == 13; }

void ModHair_PlayerSetsRaw(void)
{
    SpriteGraphics **tab = (SpriteGraphics **)_spriteGraphicsPtr;
    s_haveRaw = 0;
    if (!tab)
        return;
    memset(s_raw, 0, sizeof s_raw);
    for (int f = 0; f < kSet; f++) {
        const SpriteGraphics *s = tab[kFirstRaw + f];
        if (!s || !s->sprData || s->wquads != 1 || s->nlines > kH)
            return;
        s_lines[f] = (uint8_t)s->nlines;
        s_width[f] = (uint8_t)s->pixWidth;
        s_centerY[f] = (int8_t)s->centerY;
        s_hairTop[f] = s_hairLeft[f] = 99;
        s_hairBottom[f] = s_hairRight[f] = -1;
        for (int y = 0; y < s->nlines; y++)
            for (int x = 0; x < kW; x++) {
                const uint8_t c = s->sprData[y * 16 + x];
                s_raw[f][y][x] = c;
                if (is_hair(c)) {
                    if (y < s_hairTop[f]) s_hairTop[f] = (int8_t)y;
                    if (y > s_hairBottom[f]) s_hairBottom[f] = (int8_t)y;
                    if (x < s_hairLeft[f]) s_hairLeft[f] = (int8_t)x;
                    if (x > s_hairRight[f]) s_hairRight[f] = (int8_t)x;
                }
            }
    }
    s_haveRaw = 1;
}

/* ---- colours ---- */

/* What a face's table makes of a raw colour (the skin and hair entries; the rest unchanged). */
static uint8_t face_colour(int face, uint8_t raw)
{
    const int16_t *t = (const int16_t *)_skinAndHairColorConversionTable[face & 3];
    for (; t && t[0] >= 0; t += 2)
        if (t[0] == raw)
            return (uint8_t)t[1];
    return raw;
}

static uint8_t s_black, s_toneLight, s_toneDark;
static uint32_t s_paletteAge = 0xFFFFFFFFu;

static uint8_t mix(uint8_t a, uint8_t b, int wa, int wb)
{
    uint8_t ar, ag, ab, br, bg, bb;
    ModApi_PaletteColour(a, &ar, &ag, &ab);
    ModApi_PaletteColour(b, &br, &bg, &bb);
    return ModApi_NearestColour((uint8_t)((ar * wa + br * wb) / (wa + wb)), (uint8_t)((ag * wa + bg * wb) / (wa + wb)),
                                  (uint8_t)((ab * wa + bb * wb) / (wa + wb)));
}

static void refresh_colours(void)
{
    /* black hair: the darkest of the palette's near-greys; a shaved skull: a shade between the
     * skin's lightest two, the Mod's new tone, as near as the palette has it */
    s_black = ModApi_NearestColour(14, 12, 12);
    s_toneLight = mix(face_colour(0, 6), face_colour(0, 5), 1, 1);
    s_toneDark = mix(face_colour(2, 6), face_colour(2, 4), 1, 1);
}

/* The colour a raw pixel of the look's frame is drawn in. */
static uint8_t look_colour(int face, const Look *look, uint8_t raw, int skull)
{
    if (skull)
        return face >= 2 ? s_toneDark : s_toneLight;
    if (is_hair(raw)) {
        if (look->colour == HAIR_BLONDE)
            return face_colour(3, raw);             /* (face 3's: yellow 15, orange 6) */
        if (look->colour == HAIR_BLACK)
            return raw == 12 ? s_black : face_colour(2, raw);
    }
    return face_colour(face, raw);
}

/* ---- looks ---- */

typedef struct {
    char club[18], name[24];
    Look look;
} Named;

static Named   *s_named;
static int32_t  s_namedCount;
static int      s_namedRead;

static int parse_look(const char *w, Look *out)
{
    static const struct { const char *word; uint8_t colour, shape; } k[] = {
        { "natural", HAIR_NATURAL, SHAPE_FULL }, { "blonde", HAIR_BLONDE, SHAPE_FULL },
        { "blond", HAIR_BLONDE, SHAPE_FULL },    { "black", HAIR_BLACK, SHAPE_FULL },
        { "receding", HAIR_NATURAL, SHAPE_RECEDING }, { "bald", HAIR_NATURAL, SHAPE_RECEDING },
        { "shaved", HAIR_NATURAL, SHAPE_SHAVED }, { "ponytail", HAIR_NATURAL, SHAPE_PONYTAIL },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (SDL_strcasecmp(w, k[i].word) == 0) {
            out->colour = k[i].colour;
            out->shape = k[i].shape;
            return 1;
        }
    return 0;
}

static const char *mod_dir(void) { return ModApi_ModDir("hair"); }

static void upper_copy(char *dst, size_t cap, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < cap; i++)
        dst[i] = (char)(src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i]);
    dst[i] = '\0';
}

/* looks.csv: country,club,name,look[,face] (a header line, and lines that do not parse, skipped) */
static void read_named(void)
{
    char path[1024], line[256];
    const char *dir = mod_dir();
    s_namedRead = 1;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/looks.csv", dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    int32_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        char *field[5] = { 0 };
        int n = 0;
        line[strcspn(line, "\r\n")] = '\0';
        for (char *p = line; n < 5; n++) {
            field[n] = p;
            p = strchr(p, ',');
            if (!p) { n++; break; }
            *p++ = '\0';
        }
        Look look;
        if (n < 4 || !parse_look(field[3], &look))
            continue;
        if (s_namedCount == cap) {
            cap = cap ? cap * 2 : 64;
            Named *grown = (Named *)realloc(s_named, (size_t)cap * sizeof *s_named);
            if (!grown)
                break;
            s_named = grown;
        }
        Named *e = &s_named[s_namedCount++];
        upper_copy(e->club, sizeof e->club, field[1]);
        upper_copy(e->name, sizeof e->name, field[2]);
        e->look = look;
    }
    fclose(f);
    fprintf(stderr, "[hair] %s: %d players named\n", path, (int)s_namedCount);
}

static uint32_t hash(const char *s, uint32_t h)
{
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* A player's look: looks.csv's for him, else one of his name's. */
static Look player_look(const TeamGame *team, const PlayerInfo *p, int face)
{
    char name[24], club[18];
    Look look = { HAIR_NATURAL, SHAPE_FULL };
    upper_copy(name, sizeof name, (const char *)p->fullName);
    upper_copy(club, sizeof club, (const char *)team->teamName);
    /* (SWOS_HAIR_LOOK=<look>: every player that look -- a test's) */
    const char *force = SDL_getenv("SWOS_HAIR_LOOK");
    if (force && *force && parse_look(force, &look))
        return look;
    if (!s_namedRead)
        read_named();
    for (int32_t i = 0; i < s_namedCount; i++)
        if (strcmp(s_named[i].name, name) == 0 && (!s_named[i].club[0] || strcmp(s_named[i].club, club) == 0))
            return s_named[i].look;
    const uint32_t h = hash(name, 2166136261u), roll = h % 100, roll2 = (h >> 8) % 100;
    /* how many of a hundred: the shapes first, then the colour of what is left */
    if (roll < 7)
        look.shape = SHAPE_RECEDING;
    else if (roll < 13)
        look.shape = SHAPE_SHAVED;
    else if (roll < 17 && face != 2)
        look.shape = SHAPE_PONYTAIL;
    if (face == 0 && roll2 < 14)
        look.colour = HAIR_BLONDE;
    else if ((face == 0 && roll2 < 24) || (face == 1 && roll2 < 30))
        look.colour = HAIR_BLACK;
    return look;
}

/* ---- the shaved heads' art (shavedart.json, when it is there) ---- */

typedef struct {
    int8_t ry, rx, h, w;
    int8_t cells[8][12];            /* raw colour, -1 keep; 16 = the light skull tone, 17 the dark */
} ArtHead;

static ArtHead *s_art[2];           /* [0] light (faces 0, 1), [1] dark (faces 2, 3) */
static int32_t  s_artCount[2];
static int16_t  s_artFrame[kSet][3]; /* the head, its box column dark, light */
static int      s_artRead, s_artOk;

/* a small reader for the one file: numbers and the keys "dark", "light", "frames" */
static const char *skip_to(const char *p, const char *key)
{
    p = strstr(p, key);
    return p ? p + strlen(key) : NULL;
}

static const char *read_heads(const char *p, int skin)
{
    /* p at '[' of the skin's head list: [{"ry":0,"rx":1,"rows":[[..],..]},..] */
    int32_t cap = 0;
    while (p && *p && *p != ']') {
        const char *o = strchr(p, '{');
        if (!o)
            return NULL;
        ArtHead head;
        memset(&head, -1, sizeof head);
        const char *ry = skip_to(o, "\"ry\":"), *rx = skip_to(o, "\"rx\":"), *rows = skip_to(o, "\"rows\":");
        if (!ry || !rx || !rows)
            return NULL;
        head.ry = (int8_t)strtol(ry, NULL, 10);
        head.rx = (int8_t)strtol(rx, NULL, 10);
        head.h = head.w = 0;
        p = rows + 1;                                   /* past the rows' '[' */
        while (*p == '[' || *p == ',') {
            if (*p == ',') { p++; continue; }
            p++;
            int x = 0;
            while (*p && *p != ']') {
                char *end;
                const long v = strtol(p, &end, 10);
                if (end == p) { p++; continue; }
                if (head.h < 8 && x < 12) head.cells[head.h][x] = (int8_t)v;
                x++;
                p = end;
            }
            if (x > head.w) head.w = (int8_t)x;
            head.h++;
            p++;                                        /* past the row's ']' */
        }
        p = strchr(p, '}');
        if (!p)
            return NULL;
        p++;
        if (s_artCount[skin] == cap) {
            cap = cap ? cap * 2 : 96;
            ArtHead *grown = (ArtHead *)realloc(s_art[skin], (size_t)cap * sizeof(ArtHead));
            if (!grown)
                return NULL;
            s_art[skin] = grown;
        }
        s_art[skin][s_artCount[skin]++] = head;
        while (*p == ',' || *p == ' ') p++;
    }
    return p;
}

static void read_art(void)
{
    char path[1024];
    const char *dir = mod_dir();
    s_artRead = 1;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/shavedart.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)n + 1);
    if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(text);
        return;
    }
    fclose(f);
    text[n] = '\0';
    const char *dark = skip_to(text, "\"dark\":"), *light = skip_to(text, "\"light\":"), *frames = skip_to(text, "\"frames\":");
    if (dark && light && frames && read_heads(dark + 1, 1) && read_heads(light + 1, 0)) {
        const char *p = frames + 1;
        int k = 0;
        for (; k < kSet; k++) {
            long v[3];
            for (int j = 0; j < 3; j++) {
                while (*p && (*p < '0' || *p > '9')) p++;
                char *end;
                v[j] = strtol(p, &end, 10);
                p = end;
            }
            s_artFrame[k][0] = (int16_t)v[0];
            s_artFrame[k][1] = (int16_t)v[1];
            s_artFrame[k][2] = (int16_t)v[2];
            if (v[0] < 0 || v[0] >= s_artCount[0] || v[0] >= s_artCount[1])
                break;
        }
        s_artOk = k == kSet;
    }
    free(text);
    fprintf(stderr, "[hair] %s: %s (%d light, %d dark heads)\n", path, s_artOk ? "the shaved art" : "not read",
            (int)s_artCount[0], (int)s_artCount[1]);
}

/* ---- a look's frame ---- */

/* The frame f for a look, in raw colours (16 = a skull pixel), into out. */
static void look_frame(int f, int face, const Look *look, uint8_t out[kH][kW])
{
    const int nl = s_lines[f];
    memcpy(out, s_raw[f], sizeof s_raw[f]);
    const int top = s_hairTop[f];
    if (top > 30)
        return;                                         /* (no hair in this frame) */
    if (look->shape == SHAPE_RECEDING) {
        /* the Mod's 'E' (bald.c): the hair's top row becomes skin, and so does the next row's
         * hair with a pixel on both sides */
        for (int y = top; y <= top + 1 && y < nl; y++)
            for (int x = 0; x < kW; x++)
                if (is_hair(s_raw[f][y][x]) &&
                    (y == top || (x > 0 && x < kW - 1 && s_raw[f][y][x - 1] && s_raw[f][y][x + 1])))
                    out[y][x] = 6;
    } else if (look->shape == SHAPE_SHAVED) {
        if (!s_artRead)
            read_art();
        const int dark = face >= 2;
        if (s_artOk) {
            const ArtHead *h = &s_art[dark][s_artFrame[f][0]];
            const int ox = s_artFrame[f][1 + !dark] + h->rx, oy = h->ry;
            for (int y = 0; y < h->h; y++)
                for (int x = 0; x < h->w && x < 12; x++) {
                    const int v = h->cells[y][x], py = oy + y, px = ox + x;
                    if (v < 0 || py >= nl || px >= kW)
                        continue;
                    /* (light: 6/5/4 skin, 3 black, 16 the tone; dark: 4 <- 6, 3 <- 4, 17 the tone) */
                    out[py][px] = (uint8_t)(v == 16 || v == 17 ? 16 : !dark ? v : v == 4 ? 6 : v == 3 ? 4 : v);
                }
        } else {
            /* no art: the skull, the hair's lowest row left as a shade */
            for (int y = top; y <= s_hairBottom[f]; y++)
                for (int x = 0; x < kW; x++)
                    if (is_hair(s_raw[f][y][x]))
                        out[y][x] = y < s_hairBottom[f] ? 16 : 5;
        }
    }
}

/* ---- drawing ---- */

/* The ponytail (the owner's PONYTAIL.bmp, raw hair colour 9): drawn as it is for a player facing
 * left, mirrored for one facing right, hanging down the back of one facing away; not seen from
 * the front. */
static const int8_t kTail[3][3] = { { 1, 1, 0 }, { 0, 1, 1 }, { 0, 0, 1 } };

static void put(uint8_t *fb, int w, int h, int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < w && y < h)
        fb[y * w + x] = c;
}

void ModHair_PlayerDrawn(const Sprite *sprite, const SpriteGraphics *gfx, int16_t dstX, int16_t dstY)
{
    if (!s_haveRaw || !sprite || !gfx || !_team1SpritesTable || !_team2SpritesTable)
        return;
    int t = -1, idx = -1;
    for (int i = 0; i < 11 && t < 0; i++) {
        if (_team1SpritesTable[i] == sprite) { t = 0; idx = i; }
        else if (_team2SpritesTable[i] == sprite) { t = 1; idx = i; }
    }
    if (t < 0 || idx == 0)                              /* (not a player, or the goalkeeper) */
        return;
    const TeamGame *team = t == 0 ? _topTeamPtr : _bottomTeamPtr;
    if (!team)
        return;
    const PlayerInfo *p = &team->players[idx];
    const int face = p->blond ? 3 : (p->face & 3);
    const int f = gfx->ordinal - kSetBase[t] - sprite->frameOffset;
    if (f < 0 || f >= kSet || gfx->nlines != s_lines[f] || gfx->wquads != 1)
        return;
    const Look look = player_look(team, p, face);
    if (look.colour == HAIR_NATURAL && look.shape == SHAPE_FULL)
        return;
    if (++s_paletteAge >= 50 * 22) {                    /* (a match's palette can fade) */
        refresh_colours();
        s_paletteAge = 0;
    }

    int32_t w, h, sw;
    uint8_t *fb = ModApi_Frame(&w);
    if (!fb)
        return;
    ModApi_ScreenSize(&sw, &h);
    const int x0 = dstX, y0 = dstY;
    static uint8_t frame[kH][kW];
    look_frame(f, face, &look, frame);
    for (int y = 0; y < s_lines[f]; y++)
        for (int x = 0; x < kW; x++) {
            const uint8_t raw = s_raw[f][y][x], now = frame[y][x];
            if (!raw || (now == raw && !is_hair(raw)))
                continue;
            const int sx = x0 + x, sy = y0 + y;
            if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                continue;
            uint8_t *px = fb + sy * w + sx;
            if (*px != gfx->sprData[y * 16 + x])        /* (something else is drawn there) */
                continue;
            const uint8_t c = look_colour(face, &look, now == 16 ? 6 : now, now == 16);
            *px = c;
        }

    if (look.shape == SHAPE_PONYTAIL && s_hairTop[f] - s_centerY[f] <= -11) {
        const int dir = sprite->direction & 7;
        const uint8_t c = look_colour(face, &look, 9, 0);
        const int top = y0 + s_hairTop[f] + 1;
        if (dir == 0) {                                 /* away: down his back */
            const int cx = x0 + (s_hairLeft[f] + s_hairRight[f]) / 2;
            put(fb, w, h, cx, y0 + s_hairBottom[f] + 1, c);
            put(fb, w, h, cx, y0 + s_hairBottom[f] + 2, c);
        } else if (dir >= 5) {                          /* left: behind him, to the right */
            for (int y = 0; y < 3; y++)
                for (int x = 0; x < 3; x++)
                    if (kTail[y][x]) put(fb, w, h, x0 + s_hairRight[f] + 1 + x, top + y, c);
        } else if (dir >= 1 && dir <= 3) {              /* right: mirrored */
            for (int y = 0; y < 3; y++)
                for (int x = 0; x < 3; x++)
                    if (kTail[y][x]) put(fb, w, h, x0 + s_hairLeft[f] - 1 - x, top + y, c);
        }
    }
}

const ModDef ModDef_hair = {
    .slug = "hair",
    .playerSetsRaw = ModHair_PlayerSetsRaw,
    .playerDrawn = (void (*)(const struct Sprite *, const struct SpriteGraphics *, int16_t, int16_t))ModHair_PlayerDrawn,
};

#endif /* SWOS_SDL_BUILD */
