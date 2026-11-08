/*
 * mod_backgrounds.c -- the backgrounds mod (bin/mods/backgrounds; canonical): a true-colour picture
 * behind every menu instead of the game's own backdrop (docs/MODS.md, "Menu backgrounds"). SDL
 * only.
 *
 * WHICH PICTURE: [mod.backgrounds] image -- a file in the mod's images/ folder or in the player's
 * game folder's backgrounds/ (Documents\SWOS\backgrounds on Windows), PNG, BMP or JPEG; "off" the
 * game's own backdrop. Not set: the first picture in the mod's own images/ (a fork ships its
 * own there -- SWOS United its SWOS_United.png -- OpenSWOS ships none, so nothing changes).
 * The MODS page offers "off" and every picture it finds (mod.json "choices_from").
 *
 * HOW IT SHOWS: the engine asks at each menu backdrop it loads (ModDef menuBackdrop); with a
 * picture, the backdrop becomes one key colour and the presenter draws the picture under the menus,
 * covering the window: cropped to the window's aspect, centred. Best made 1920 x 1080 (16:9),
 * with what matters inside the centre 1440 x 1080, which a 4:3 window keeps.
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "mod_api.h"
#include "swos_io.h"            /* swos_user_dir */
#include "../third_party/stb_image/stb_image.h"

#define SLUG "backgrounds"

static struct {
    char     path[1200];        /* the picture loaded, "" none */
    uint8_t *rgba;
    int      w, h;
} s_bg;

static int32_t first_image(const char *name, void *ctx)
{
    const char *dot = strrchr(name, '.');
    char *best = (char *)ctx;
    if (!dot || !(SDL_strcasecmp(dot, ".png") == 0 || SDL_strcasecmp(dot, ".bmp") == 0 ||
                  SDL_strcasecmp(dot, ".jpg") == 0 || SDL_strcasecmp(dot, ".jpeg") == 0))
        return 0;
    if (!best[0] || SDL_strcasecmp(name, best) < 0)     /* (the first by name, whatever the order) */
        snprintf(best, 256, "%s", name);
    return 0;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* The picture's path, "" none. */
static void picture_path(char *out, size_t cap)
{
    const char *setting = ModApi_Setting(SLUG, "image", "");
    char own[1100], name[256] = "";
    out[0] = '\0';
    if (SDL_strcasecmp(setting, "off") == 0)
        return;
    snprintf(own, sizeof own, "%s/images", ModApi_ModDir(SLUG));
    if (!setting[0]) {
        ModApi_ListFiles(own, first_image, name);
        if (name[0])
            snprintf(out, cap, "%s/%s", own, name);
        return;
    }
    snprintf(out, cap, "%s/%s", own, setting);
    if (file_exists(out))
        return;
    snprintf(out, cap, "%s/backgrounds/%s", swos_user_dir(), setting);
    if (!file_exists(out)) {
        fprintf(stderr, "[backgrounds] %s: not in %s nor %s/backgrounds -- the game's own backdrop\n",
                setting, own, swos_user_dir());
        out[0] = '\0';
    }
}

static int32_t menu_backdrop(const uint8_t **rgba, int32_t *w, int32_t *h)
{
    char path[1200];
    picture_path(path, sizeof path);
    if (!path[0])
        return 0;
    if (strcmp(path, s_bg.path) != 0) {
        int n = 0;
        stbi_image_free(s_bg.rgba);
        s_bg.rgba = stbi_load(path, &s_bg.w, &s_bg.h, &n, 4);
        snprintf(s_bg.path, sizeof s_bg.path, "%s", path);
        if (!s_bg.rgba)
            fprintf(stderr, "[backgrounds] %s: could not be read (%s)\n", path, stbi_failure_reason());
        else
            fprintf(stderr, "[backgrounds] the menus' backdrop: %s (%d x %d)\n", path, s_bg.w, s_bg.h);
    }
    if (!s_bg.rgba)
        return 0;
    *rgba = s_bg.rgba;
    *w = s_bg.w;
    *h = s_bg.h;
    return 1;
}

const ModDef ModDef_backgrounds = {
    .slug         = SLUG,
    .menuBackdrop = menu_backdrop,
};

#endif /* SWOS_SDL_BUILD */
