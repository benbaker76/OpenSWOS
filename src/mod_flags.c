/*
 * mod_flags.c -- the flags mod (canonical, bin/mods/flags; docs/MODS.md §32): each team's flag beside
 * its name on the result banner -- the score panel SWOS shows at kick-off, after a goal and at half
 * and full time. SDL builds (it draws through the renderer, frameDrawn).
 *
 * A team's flag (flags.json): teams[its database id] (the national teams: one renamed, or a custom
 * team made from one, keeps its flag), else names[its name] when SWOS spells the country unlike the
 * flag's file (HOLLAND, REPUBLIC IRELAND, WEST GERMANY...), else the file whose name has its name's
 * letters (GREECE: flags/Greece.bmp); a club with none of those takes the flag of the country it
 * plays for, countries[] (World_FileCountry: a world's second file is its country's; World 2026-27's
 * new countries have their numbers there too).
 * A team with none (SOVIET UNION, a club of a competition's own) gets no flag. The art is
 * GoSquared's 24-pixel set (MIT: LICENSE_Flags.txt), each flag the icon's 22 x 16 middle; a mod
 * that wants others puts its own BMP of the same name in its flags/ folder (the overlay).
 *
 * Cosmetic: drawn over the finished frame, after the HUD, so nothing of the match is touched.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "flags"

enum { kSrcX = 1, kSrcY = 4, kW = 22, kH = 16 };   /* the flag in the 24 x 24 icon */
enum { kMargin = 6 };                               /* from the banner's edge */

static char *s_json;                /* flags.json */
static uint32_t s_jsonLen;
static int32_t s_read;

typedef struct {
    uint16_t     team;              /* whose (0: none yet) */
    SDL_Texture *tex;               /* NULL: no flag for him */
    SDL_Renderer *renderer;         /* the renderer it was made on */
} Cached;
static Cached s_cache[2];

static int32_t each_file(const char *name, void *ctx);

static void read_json(void)
{
    char path[1024];
    s_read = 1;
    const char *dir = ModApi_ModDir(SLUG);
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/flags.json", dir);
    s_json = (char *)ModApi_ReadFile(path, &s_jsonLen);
    if (!s_json)
        fprintf(stderr, "[flags] no %s: no flags\n", path);
    snprintf(path, sizeof path, "%s/flags", dir);
    ModApi_ListFiles(path, each_file, NULL);
}

/* The flag files, by their letters alone (Greece: GREECE; Cote-dIvoire: COTEDIVOIRE). */
enum { kFiles = 320, kFileName = 48 };
static char s_files[kFiles][kFileName], s_letters[kFiles][kFileName];
static int32_t s_fileCount;

static void letters(const char *in, char *out, int cap)
{
    int n = 0;
    for (; *in && n < cap - 1; in++) {
        char c = *in;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if (c >= 'A' && c <= 'Z')
            out[n++] = c;
    }
    out[n] = 0;
}

static int32_t each_file(const char *name, void *ctx)
{
    (void)ctx;
    const size_t n = strlen(name);
    if (s_fileCount == kFiles || n < 5 || n - 4 >= kFileName || SDL_strcasecmp(name + n - 4, ".bmp") != 0)
        return 0;
    memcpy(s_files[s_fileCount], name, n - 4);
    s_files[s_fileCount][n - 4] = 0;
    letters(s_files[s_fileCount], s_letters[s_fileCount], kFileName);
    s_fileCount++;
    return 0;
}

/* The flag file's name for a team, "" when there is none. */
static void flag_name(const ModMatchTeam *t, char *out, int cap)
{
    out[0] = 0;
    if (!s_json)
        return;
    if (t->uuid[0]) {   /* teams[its database id]: a national team, however it is named now */
        char q[64];
        snprintf(q, sizeof q, "$.teams.%s", t->uuid);
        if (mjson_get_string(s_json, (int)s_jsonLen, q, out, cap) > 0)
            return;
    }
    {   /* names[] (walked: a name may hold a '.', which a path cannot) */
        const char *obj;
        int olen;
        if (mjson_find(s_json, (int)s_jsonLen, "$.names", &obj, &olen) == MJSON_TOK_OBJECT) {
            int off = 0, ko, kl, vo, vl, vt;
            while ((off = mjson_next(obj, olen, off, &ko, &kl, &vo, &vl, &vt)) != 0) {
                /* (ko/kl: the key with its quotes) */
                if (vt == MJSON_TOK_STRING && kl - 2 == (int)strlen(t->name) &&
                    strncmp(obj + ko + 1, t->name, (size_t)(kl - 2)) == 0) {
                    const int n = vl - 2 < cap - 1 ? vl - 2 : cap - 1;
                    memcpy(out, obj + vo + 1, (size_t)n);
                    out[n] = 0;
                    return;
                }
            }
        }
    }
    {   /* the file with the name's letters */
        char want[kFileName];
        letters(t->name, want, kFileName);
        for (int32_t i = 0; i < s_fileCount; i++)
            if (strcmp(s_letters[i], want) == 0) {
                snprintf(out, (size_t)cap, "%s", s_files[i]);
                return;
            }
    }
    if (t->country < 80 || t->country > 85) {   /* a club: the country it plays for */
        char q[40];
        snprintf(q, sizeof q, "$.countries.%u", (unsigned)t->country);
        mjson_get_string(s_json, (int)s_jsonLen, q, out, cap);
    }
}

static SDL_Texture *make_texture(SDL_Renderer *renderer, const char *name)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/flags/%s.bmp", ModApi_ModDir(SLUG), name);
    SDL_Surface *icon = SDL_LoadBMP(path);
    if (!icon) {
        fprintf(stderr, "[flags] %s: %s\n", path, SDL_GetError());
        return NULL;
    }
    SDL_Surface *flag = SDL_CreateRGBSurfaceWithFormat(0, kW, kH, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Texture *tex = NULL;
    if (flag) {
        SDL_Rect src = { kSrcX, kSrcY, kW, kH };
        SDL_BlitSurface(icon, &src, flag, NULL);
        tex = SDL_CreateTextureFromSurface(renderer, flag);
        SDL_FreeSurface(flag);
    }
    SDL_FreeSurface(icon);
    return tex;
}

static SDL_Texture *flag_for(SDL_Renderer *renderer, int32_t side, const ModMatchTeam *t)
{
    Cached *c = &s_cache[side];
    if (c->team == t->team + 1 && c->renderer == renderer)
        return c->tex;
    if (c->tex)
        SDL_DestroyTexture(c->tex);
    char name[64];
    flag_name(t, name, (int)sizeof name);
    c->tex = name[0] ? make_texture(renderer, name) : NULL;
    c->team = (uint16_t)(t->team + 1);
    c->renderer = renderer;
    fprintf(stderr, "[flags] %s: %s\n", t->name, name[0] ? name : "no flag");
    return c->tex;
}

static void flags_frame(SDL_Renderer *renderer)
{
    ModResultBanner b;
    if (ModApi_InReplay() || !ModApi_ResultBanner(&b))
        return;
    if (!s_read)
        read_json();
    const int y = b.nameTop + (b.nameHeight - kH) / 2;
    for (int32_t side = 0; side < 2; side++) {
        ModMatchTeam t;
        if (!ModApi_MatchTeam(side, &t))
            continue;
        SDL_Texture *tex = flag_for(renderer, side, &t);
        if (!tex)
            continue;
        const SDL_Rect dst = { side == 0 ? b.left + kMargin : b.right - kMargin - kW, y, kW, kH };
        SDL_RenderCopy(renderer, tex, NULL, &dst);
    }
}

const ModDef ModDef_flags = {
    .slug = SLUG,
    .frameDrawn = flags_frame,
};

#endif /* SWOS_SDL_BUILD */
