/*
 * swosdata.c -- build OpenSWOS's game data from the player's own copy of the game.
 */
#include "swosdata.h"
#include "sd_manifest.h"
#include "amiga_raw.h"
#include "credits_aga.h"
#include "dos_picture.h"
#include "dos_pitch.h"
#include "team_json.h"
#include "dos2amiga.h"
#include "font_extend.h"
#include "jag_tier.h"
#include "../../../include/assets_ini.h"   /* the parts, their data versions, ASSETS.ini */
#include "rnc_pack.h"
#include "lha.h"
#include "sg_dir.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static void logf_(SdJob *job, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (job->log)
        job->log(job->user, line);
}

static int32_t count_amiga(void)
{
    int32_t n = 0;
    while (g_sdAmigaCopy[n])
        n++;
    return n;
}

static int32_t count_dos(void)
{
    int32_t n = 0;
    while (g_sdDosCopy[n].dest)
        n++;
    return n;
}

void SwosData_Plan(const SdJob *job, SdStepInfo info[SD_STEP_COUNT])
{
    info[SD_STEP_AMIGA_FILES].name = "Amiga data, graphics and music";
    info[SD_STEP_AMIGA_FILES].needs = "both Amiga disks";
    info[SD_STEP_AMIGA_FILES].ready = SdSources_HaveAmiga(&job->src);

    info[SD_STEP_DOS_FILES].name = "PC graphics, sound and commentary";
    info[SD_STEP_DOS_FILES].needs = "the PC game";
    info[SD_STEP_DOS_FILES].ready = SdSources_HaveDos(&job->src);

    info[SD_STEP_AGA_PC].name = "AGA menus, pitches and credits";
    info[SD_STEP_AGA_PC].needs = "the PC game";
    info[SD_STEP_AGA_PC].ready = SdSources_HaveDos(&job->src);

    info[SD_STEP_AGA_AMIGA].name = "AGA loading screens";
    info[SD_STEP_AGA_AMIGA].needs = "both Amiga disks";
    info[SD_STEP_AGA_AMIGA].ready = SdSources_HaveAmiga(&job->src);

    info[SD_STEP_TEAMS].name = "Teams and players";
    info[SD_STEP_TEAMS].needs = "the Amiga disks or the PC game";
    info[SD_STEP_TEAMS].ready = SdSources_HaveAmiga(&job->src) || SdSources_HaveDos(&job->src);

    info[SD_STEP_JAGUAR].name = "Atari Jaguar sprites";
    info[SD_STEP_JAGUAR].needs = "the Jaguar cartridge and the PC game";
    info[SD_STEP_JAGUAR].ready = SdSources_HaveJaguar(&job->src) && SdSources_HaveDos(&job->src);

    info[SD_STEP_SWOS2020].name = "SWOS 2020 pitches";
    info[SD_STEP_SWOS2020].needs = "a SWOS 2020 install";
    info[SD_STEP_SWOS2020].ready = SdSources_HaveSwos2020(&job->src);

    info[SD_STEP_WORLD].name = "World 2026-27";
    info[SD_STEP_WORLD].needs = "the World 2026-27 package";
    info[SD_STEP_WORLD].ready = SdSources_HaveWorld(&job->src);
}

static int32_t write_out(SdJob *job, const char *rel, const SgBuf *b, char *err)
{
    char path[1100];
    Sg_PathJoin(path, sizeof path, job->outDir, rel);
    return Sg_WriteFile(path, b->data, b->size, err);
}

static void tick(SdJob *job, int32_t *done, int32_t total)
{
    if (job->progress)
        job->progress(job->user, ++*done, total);
}

static void glyphs_path(const SdJob *job, char *out, uint32_t cap)
{
    Sg_PathJoin(out, cap, job->resDir, "setup/glyphs.txt");
}

static int32_t write_packed(SdJob *job, const char *rel, const SgBuf *b, char *err);

static int32_t step_amiga_files(SdJob *job, int32_t *done, int32_t total, char *err)
{
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++) {
        SgBuf b;
        if (SdSources_ReadAmiga(&job->src, g_sdAmigaCopy[i], &b, err) != 0)
            return -1;
        int32_t rc = write_out(job, g_sdAmigaCopy[i], &b, err);
        Sg_BufFree(&b);
        if (rc != 0)
            return -1;
        if (job->progress)
            job->progress(job->user, ++*done, total);
    }
    /* The font, with the extra letters drawn in (written over the disk's own). */
    {
        SgBuf raw, un, out;
        SgImage img;
        char glyphs[1100];
        glyphs_path(job, glyphs, sizeof glyphs);
        if (SdSources_ReadAmiga(&job->src, "grafs/CHARSET.RAW", &raw, err) != 0)
            return -1;
        int32_t rc = Sg_RncUnpack(&raw, &un, err);
        Sg_BufFree(&raw);
        if (rc == 0 && (rc = Sg_AmigaRawDecode(&un, 320, 256, 4, &img, err)) == 0) {
            if ((rc = Sg_FontExtend(&img, glyphs, err)) == 0 &&
                (rc = Sg_AmigaRawEncode(&img, 4, &out, err)) == 0) {
                rc = write_packed(job, "grafs/CHARSET.RAW", &out, err);
                Sg_BufFree(&out);
            }
            SgImage_Free(&img);
        }
        Sg_BufFree(&un);
        if (rc != 0)
            return -1;
        tick(job, done, total);
    }
    logf_(job, "Amiga: %d files", count_amiga() + 1);
    return 0;
}

static int32_t count_2020(void)
{
    int32_t n = 0;
    while (g_sd2020Copy[n].dest)
        n++;
    return n;
}

static int32_t step_dos_files(SdJob *job, int32_t *done, int32_t total, char *err)
{
    for (int32_t i = 0; g_sdDosCopy[i].dest; i++) {
        SgBuf b;
        if (SdSources_ReadDos(&job->src, g_sdDosCopy[i].src, &b, err) != 0)
            return -1;
        int32_t rc = write_out(job, g_sdDosCopy[i].dest, &b, err);
        Sg_BufFree(&b);
        if (rc != 0)
            return -1;
        if (job->progress)
            job->progress(job->user, ++*done, total);
    }
    logf_(job, "PC: %d files", count_dos());
    return 0;
}

/* The full-screen pictures the PC game has no version of: the Amiga's own,
 * remapped to the AGA build's 256 colours (SwosGfx !DOS2AGA.bat step 1). */
static const char *const kAmigaPictures[] = {
    "LOADER00", "LOADER01", "LOADER1", "LOADER10", "LOADER2", "LOADER3", "LOADER4",
    "LOADER5", "LOADER6", "LOADER7", "LOADER8A", "LOADER8B", "LOADER9", "DISK22", "OLDDISK1",
};
#define AGA_PC_COUNT (SG_BANK_COUNT + 2 + 6 + 1)   /* banks, MENUBG, MENUBG2, SWCPICH1-6, CREDITS */

/* Packed as SwosGfx packed them (RNC method 2). Not optional: the game reads a
 * file whole into its screen buffer before unpacking it, so a 352x272 8-plane
 * picture (95,744 bytes) only fits packed. */
static int32_t write_packed(SdJob *job, const char *rel, const SgBuf *b, char *err)
{
    SgBuf packed;
    if (Sg_RncPackLikeSwosGfx(b, &packed, err) != 0)
        return -1;
    int32_t rc = write_out(job, rel, &packed, err);
    Sg_BufFree(&packed);
    return rc;
}

/* The ball on the title screen and on the menus' logo, redrawn to say AGA: the
 * project's own drawings in setup/, pasted over the converted picture, in its
 * 256 colours. MENUS2 becomes 8 planes deep with it, and the game stores the
 * logo cut from it (sprite 0xE0) 8 deep on the AGA Amiga (spriteNumPlanes).
 *
 * For the AGA AMIGA only: the PC builds read grafs_aga too, and must show the
 * game's own 96-97 ball. So grafs_aga/ gets the plain picture and the AGA one goes
 * to amiga/grafs_aga/ (AGA_ONLY_DIR), which no PC build reads and
 * SwosData_FillAmigaDrawer copies over the drawer's grafs_aga/ last. */
#define AGA_ONLY_DIR "amiga"
static const struct {
    const char *name, *file;
    int32_t     x, y;
} kAgaPatches[] = {
    { "LOADER1", "setup/LOADER1_AGA.bmp", 200, 72 },
    { "MENUS2",  "setup/MENUS2_AGA.bmp",   72, 24 },
};

/* 1 when it drew the AGA ball into img, 0 when name has none, -1 on an error. */
static int32_t apply_aga_patch(SdJob *job, const char *name, SgImage *img, char *err)
{
    for (uint32_t i = 0; i < sizeof kAgaPatches / sizeof kAgaPatches[0]; i++) {
        if (strcmp(kAgaPatches[i].name, name) != 0)
            continue;
        char path[1100];
        SgImage patch;
        Sg_PathJoin(path, sizeof path, job->resDir, kAgaPatches[i].file);
        if (SgImage_ReadBmp(&patch, path, err) != 0)
            return -1;
        const int32_t x = kAgaPatches[i].x, y = kAgaPatches[i].y;
        int32_t rc = 0;
        if (x + patch.w > img->w || y + patch.h > img->h) {
            snprintf(err, 256, "%s does not fit %s at %d,%d", kAgaPatches[i].file, name, (int)x, (int)y);
            rc = -1;
        }
        for (int32_t row = 0; row < patch.h && rc == 0; row++)
            memcpy(img->px + (y + row) * img->w + x, patch.px + row * patch.w, (size_t)patch.w);
        SgImage_Free(&patch);
        return rc == 0 ? 1 : rc;
    }
    return 0;
}

static int32_t write_raw(SdJob *job, const char *rel, const SgImage *img, char *err)
{
    SgBuf raw;
    if (Sg_AmigaRawEncode(img, 0, &raw, err) != 0)
        return -1;
    int32_t rc = write_packed(job, rel, &raw, err);
    Sg_BufFree(&raw);
    return rc;
}

/* grafs_aga/<name>.RAW as the game drew it, and -- when name has an AGA ball --
 * AGA_ONLY_DIR/grafs_aga/<name>.RAW with it, for the Amiga drawer (kAgaPatches).
 * img gets the ball drawn in. */
static int32_t write_aga_picture(SdJob *job, const char *name, SgImage *img, char *err)
{
    char rel[96];
    snprintf(rel, sizeof rel, "grafs_aga/%s.RAW", name);
    int32_t rc = write_raw(job, rel, img, err);
    if (rc == 0 && (rc = apply_aga_patch(job, name, img, err)) == 1) {
        snprintf(rel, sizeof rel, AGA_ONLY_DIR "/grafs_aga/%s.RAW", name);
        rc = write_raw(job, rel, img, err);
    }
    return rc < 0 ? -1 : 0;
}

/* A PC picture, enlarged to the Amiga screen (SWTITLE -> MENUBG, STAD -> MENUBG2). */
static int32_t make_backdrop(SdJob *job, const char *dosName, SgImage *out, char *err)
{
    char path[1100];
    SgImage pic;
    if (Sg_FindNoCase(job->src.dosDir, dosName, path, sizeof path) != 0) {
        snprintf(err, 256, "%s is not in the PC game folder", dosName);
        return -1;
    }
    if (Sg_DosPictureLoad(path, &pic, err) != 0)
        return -1;
    int32_t rc = Sg_DosPictureScaleToAmiga(&pic, out, err);
    SgImage_Free(&pic);
    return rc;
}

/* The sprite, menu and font banks: the PC game's sprites laid out where the
 * Amiga's banks keep them, and the extra letters drawn into the font. */
static int32_t make_banks(SdJob *job, int32_t *done, int32_t total, char *err)
{
    SgImage banks[SG_BANK_COUNT];
    char glyphs[1100];
    if (Sg_DosToAmigaBanks(job->src.dosDir, banks, err) != 0)
        return -1;
    glyphs_path(job, glyphs, sizeof glyphs);
    int32_t rc = Sg_FontExtend(&banks[SG_BANK_CHARSET], glyphs, err);
    for (int32_t b = 0; b < SG_BANK_COUNT; b++) {
        if (rc == 0 && (rc = write_aga_picture(job, Sg_BankNames[b], &banks[b], err)) == 0)
            tick(job, done, total);
        SgImage_Free(&banks[b]);
    }
    return rc;
}

static int32_t step_aga_pc(SdJob *job, int32_t *done, int32_t total, char *err)
{
    SgImage menubg, menubg2, icons;
    if (make_banks(job, done, total, err) != 0)
        return -1;
    if (make_backdrop(job, "SWTITLE.256", &menubg, err) != 0)
        return -1;
    int32_t rc = write_raw(job, "grafs_aga/MENUBG.RAW", &menubg, err);
    tick(job, done, total);
    if (rc == 0 && (rc = make_backdrop(job, "STAD.256", &menubg2, err)) == 0) {
        rc = write_raw(job, "grafs_aga/MENUBG2.RAW", &menubg2, err);
        SgImage_Free(&menubg2);
        tick(job, done, total);
    }
    for (int32_t n = 1; n <= 6 && rc == 0; n++) {
        SgBuf map;
        char rel[64];
        if ((rc = Sg_DosPitchToAgaMap(job->src.dosDir, n, &map, err)) != 0)
            break;
        snprintf(rel, sizeof rel, "grafs_aga/SWCPICH%d.MAP", n);
        rc = write_packed(job, rel, &map, err);
        Sg_BufFree(&map);
        tick(job, done, total);
    }
    /* The credits backdrop: MENUBG with the project's own photos on it. */
    if (rc == 0) {
        char path[1100];
        Sg_PathJoin(path, sizeof path, job->resDir, "data/credits_icons.bmp");
        if ((rc = SgImage_ReadBmp(&icons, path, err)) == 0) {
            SgBuf raw;
            if ((rc = Sd_MakeCreditsAga(&menubg, &icons, &raw, err)) == 0) {
                rc = write_packed(job, "grafs_aga/CREDITS.RAW", &raw, err);
                Sg_BufFree(&raw);
            }
            SgImage_Free(&icons);
        }
        tick(job, done, total);
    }
    SgImage_Free(&menubg);
    if (rc == 0)
        logf_(job, "AGA from the PC game: %d files", AGA_PC_COUNT);
    return rc;
}

static int32_t step_aga_amiga(SdJob *job, int32_t *done, int32_t total, char *err)
{
    int32_t count = (int32_t)(sizeof kAmigaPictures / sizeof kAmigaPictures[0]);
    for (int32_t i = 0; i < count; i++) {
        char rel[64];
        SgBuf raw;
        SgImage img;
        snprintf(rel, sizeof rel, "grafs/%s.RAW", kAmigaPictures[i]);
        if (SdSources_ReadAmiga(&job->src, rel, &raw, err) != 0)
            return -1;
        int32_t rc = Sg_AmigaPictureTo256(kAmigaPictures[i], &raw, &img, err);
        Sg_BufFree(&raw);
        if (rc != 0)
            return -1;
        rc = write_aga_picture(job, kAmigaPictures[i], &img, err);
        SgImage_Free(&img);
        if (rc != 0)
            return -1;
        tick(job, done, total);
    }
    logf_(job, "AGA from the Amiga: %d files", count);
    return 0;
}

/* The team files: the "data/" entries of the Amiga list. The PC game holds the
 * same files, unpacked, plus a few countries the Amiga's never had -- left out,
 * or the game would load them as extra countries. */
static int32_t count_teams(void)
{
    int32_t n = 0;
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++)
        n += strncmp(g_sdAmigaCopy[i], "data/", 5) == 0;
    return n;
}

static int32_t step_teams(SdJob *job, int32_t *done, int32_t total, char *err)
{
    /* The teams of 96/97, the season OpenSWOS plays online: an Amiga copy of another
     * season (24/25 and the like) gives them only when there is no PC game to give
     * 96/97's. Its data/ is then made again from the PC game, over the copy's. */
    const char *season = SdSources_HaveAmiga(&job->src) ? SdSources_AmigaSeason(&job->src) : NULL;
    int32_t fromAmiga = SdSources_HaveAmiga(&job->src) &&
                        (!SdSources_HaveDos(&job->src) || (season && strcmp(season, "96/97") == 0));
    if (SdSources_HaveAmiga(&job->src) && !fromAmiga)
        logf_(job, "Teams: your Amiga copy is %s; the teams are 96/97's, from the PC game",
              season ? season : "not the 96/97 release");
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++) {
        const char *rel = g_sdAmigaCopy[i];
        if (strncmp(rel, "data/", 5) != 0)
            continue;
        SgBuf raw, json;
        char dosRel[64], name[64], out[96];
        snprintf(dosRel, sizeof dosRel, "DATA/%s", rel + 5);
        int32_t rc = fromAmiga ? SdSources_ReadAmiga(&job->src, rel, &raw, err)
                               : SdSources_ReadDos(&job->src, dosRel, &raw, err);
        if (rc != 0)
            return -1;
        /* Without the disks the game's own data/ comes from the PC files too,
         * packed the way the Amiga's are -- which gives the Amiga's files back
         * byte for byte: team files method 1, the cup pools and the player pool
         * method 2. */
        if (!fromAmiga) {
            SgBuf packed = { NULL, 0 };
            int32_t method = strncmp(rel + 5, "TEAM.", 5) == 0 ? 1 : 2;
            rc = Sg_RncPack(&raw, method, &packed, err);
            if (rc == 0)
                rc = write_out(job, rel, &packed, err);
            Sg_BufFree(&packed);
            if (rc != 0) {
                Sg_BufFree(&raw);
                return -1;
            }
        }
        rc = Sd_TeamFileToJson(rel + 5, &raw, &json, err);
        Sg_BufFree(&raw);
        if (rc != 0)
            return -1;
        Sd_TeamJsonName(rel + 5, name, sizeof name);
        snprintf(out, sizeof out, "data_json/teams/%s", name);
        rc = write_out(job, out, &json, err);
        Sg_BufFree(&json);
        if (rc != 0)
            return -1;
        tick(job, done, total);
    }
    logf_(job, "Teams: %d files, from the %s", count_teams(), fromAmiga ? "Amiga disks" : "PC game");
    return 0;
}

/* The Jaguar redrew the gameplay sprites at 256 colours in the Amiga's bank
 * layout; what it lacks comes from the PC game, and the goal celebration from
 * the project's own drawings in setup/. */
static int32_t step_jaguar(SdJob *job, int32_t *done, int32_t total, char *err)
{
    SdJagFile files[SD_JAG_FILE_COUNT];
    uint32_t celebrations = 0;
    char setup[1100];
    Sg_PathJoin(setup, sizeof setup, job->resDir, "setup");
    if (Sd_JagBuild(job->src.jagRom, job->src.dosDir, setup, files, &celebrations, err) != 0)
        return -1;
    int32_t rc = 0;
    for (int32_t i = 0; i < SD_JAG_FILE_COUNT && rc == 0; i++) {
        char rel[96];
        snprintf(rel, sizeof rel, "grafs_jag/%.40s", files[i].name);
        rc = write_out(job, rel, &files[i].data, err);
        tick(job, done, total);
    }
    Sd_JagFilesFree(files);
    if (rc == 0)
        logf_(job, "Jaguar: %d files", SD_JAG_FILE_COUNT);
    return rc;
}

/* SWOS 2020's pitches as its launcher left them in its game folder -- the set the
 * player picked there -- and the AGA Amiga's maps of them, P2020_n.MAP. Those are
 * written unpacked: the game reads them straight into the pitch buffer, as they
 * do not fit the screen buffer even packed (src/pitch.c loadPitchMap2020). */
static int32_t step_swos2020(SdJob *job, int32_t *done, int32_t total, char *err)
{
    for (int32_t i = 0; g_sd2020Copy[i].dest; i++) {
        char path[1100];
        SgBuf b;
        if (Sg_FindNoCase(job->src.s2020Dir, g_sd2020Copy[i].src, path, sizeof path) != 0) {
            snprintf(err, 256, "%s is not in SWOS 2020's game folder", g_sd2020Copy[i].src);
            return -1;
        }
        if (Sg_ReadFile(path, &b, err) != 0)
            return -1;
        int32_t rc = write_out(job, g_sd2020Copy[i].dest, &b, err);
        Sg_BufFree(&b);
        if (rc != 0)
            return -1;
        tick(job, done, total);
    }
    for (int32_t n = 1; n <= 6; n++) {
        SgBuf map;
        char rel[64];
        if (Sg_DosPitchToAgaMap(job->src.s2020Dir, n, &map, err) != 0)
            return -1;
        snprintf(rel, sizeof rel, "grafs_aga/P2020_%d.MAP", n);
        int32_t rc = write_out(job, rel, &map, err);
        Sg_BufFree(&map);
        if (rc != 0)
            return -1;
        tick(job, done, total);
    }
    logf_(job, "SWOS 2020: %d files", count_2020() + 6);
    return 0;
}

static int32_t write_assets_ini(SdJob *job, const SdStepInfo info[SD_STEP_COUNT], char *err);

/* A world's package (OpenSWOS-World-<tag>.zip, or the folder it was unpacked to) into the assets
 * folder (SwosData_InstallWorld). */
static int32_t step_world(SdJob *job, int32_t *done, int32_t total, char *err)
{
    SdWorldInfo w;
    char dir[1400];
    if (SdSources_OpenWorld(job->src.world, &w, dir, sizeof dir) != 0) {
        snprintf(err, 256, "%.200s is not a world package (no WORLD.ini)", job->src.world);
        return -1;
    }
    if (job->log) {
        char line[256];
        snprintf(line, sizeof line, "%s (%s), data %s", w.name[0] ? w.name : "a world", w.tag,
                 w.fingerprint[0] ? w.fingerprint : "?");
        job->log(job->user, line);
    }
    int32_t rc = SwosData_InstallWorld(dir, w.tag, job->outDir, NULL, job->log, job->user, err);
    tick(job, done, total);
    return rc;
}

int32_t SwosData_Run(SdJob *job, char *err)
{
    SdStepInfo info[SD_STEP_COUNT];
    SwosData_Plan(job, info);

    int32_t total = 0, done = 0;
    if (info[SD_STEP_AMIGA_FILES].ready)
        total += count_amiga() + 1;
    if (info[SD_STEP_DOS_FILES].ready)
        total += count_dos();
    if (info[SD_STEP_AGA_PC].ready)
        total += AGA_PC_COUNT;
    if (info[SD_STEP_AGA_AMIGA].ready)
        total += (int32_t)(sizeof kAmigaPictures / sizeof kAmigaPictures[0]);
    if (info[SD_STEP_TEAMS].ready)
        total += count_teams();
    if (info[SD_STEP_JAGUAR].ready)
        total += SD_JAG_FILE_COUNT;
    if (info[SD_STEP_SWOS2020].ready)
        total += count_2020() + 6;
    if (info[SD_STEP_WORLD].ready)
        total += 1;

    if (SdSources_Open(&job->src, err) != 0)
        return -1;
    int32_t rc = 0;
    for (int32_t s = 0; s < SD_STEP_COUNT && rc == 0; s++) {
        if (!info[s].ready) {
            logf_(job, "Skipped: %s (needs %s)", info[s].name, info[s].needs);
            continue;
        }
        switch ((SdStep)s) {
        case SD_STEP_AMIGA_FILES: rc = step_amiga_files(job, &done, total, err); break;
        case SD_STEP_DOS_FILES:   rc = step_dos_files(job, &done, total, err); break;
        case SD_STEP_AGA_PC:      rc = step_aga_pc(job, &done, total, err); break;
        case SD_STEP_AGA_AMIGA:   rc = step_aga_amiga(job, &done, total, err); break;
        case SD_STEP_TEAMS:       rc = step_teams(job, &done, total, err); break;
        case SD_STEP_JAGUAR:      rc = step_jaguar(job, &done, total, err); break;
        case SD_STEP_SWOS2020:    rc = step_swos2020(job, &done, total, err); break;
        case SD_STEP_WORLD:       rc = step_world(job, &done, total, err); break;
        default: break;
        }
    }
    SdSources_Close(&job->src);
    if (rc == 0)
        rc = write_assets_ini(job, info, err);
    return rc;
}

/* ---- ASSETS.ini ----------------------------------------------------------- */

#define SD_PART_KEY(name, key, ver) key,
#define SD_PART_VER(name, key, ver) ver,
static const char *const kPartKey[SD_STEP_COUNT]  = { ASSETS_PARTS(SD_PART_KEY) };
static const int32_t     kPartVersion[SD_STEP_COUNT] = { ASSETS_PARTS(SD_PART_VER) };
#undef SD_PART_KEY
#undef SD_PART_VER

int32_t     SwosData_PartVersion(SdStep step) { return step < SD_STEP_COUNT ? kPartVersion[step] : 0; }
const char *SwosData_PartKey(SdStep step)     { return step < SD_STEP_COUNT ? kPartKey[step] : ""; }

/* The [sources] keys, beside the SdSources field each one is. */
enum { SRC_ADF1, SRC_ADF2, SRC_HDF, SRC_LHA, SRC_DOS, SRC_JAG, SRC_2020, SRC_WORLD, SRC_COUNT };
static const char *const kSourceKey[SRC_COUNT] = {
    ASSETS_SRC_AMIGA_DISK1, ASSETS_SRC_AMIGA_DISK2, ASSETS_SRC_AMIGA_HDF, ASSETS_SRC_AMIGA_LHA,
    ASSETS_SRC_PC, ASSETS_SRC_JAGUAR, ASSETS_SRC_SWOS2020, ASSETS_SRC_WORLD
};
static char *source_field(SdSources *s, int32_t i)
{
    switch (i) {
    case SRC_ADF1: return s->adf1;
    case SRC_ADF2: return s->adf2;
    case SRC_HDF:  return s->hdf;
    case SRC_LHA:  return s->lha;
    case SRC_DOS:  return s->dosDir;
    case SRC_JAG:  return s->jagRom;
    case SRC_WORLD: return s->world;
    default:       return s->s2020Dir;
    }
}

typedef struct {
    int32_t version[SD_STEP_COUNT];     /* 0: not listed */
    char    source[SRC_COUNT][1024];    /* "": not listed */
} SdAssetsIni;

static char *trim(char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    char *e = t + strlen(t);
    while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return t;
}

/* ASSETS.ini in assetsDir, if it is there: 0 when read. */
static int32_t ini_read(const char *assetsDir, SdAssetsIni *ini)
{
    char path[1100], line[1200], section[32] = "";
    FILE *f;
    memset(ini, 0, sizeof *ini);
    Sg_PathJoin(path, sizeof path, assetsDir, ASSETS_INI_NAME);
    if ((f = fopen(path, "r")) == NULL)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line), *eq;
        if (*t == ';' || *t == '#' || *t == '\0')
            continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) { *e = '\0'; snprintf(section, sizeof section, "%s", t + 1); }
            continue;
        }
        if ((eq = strchr(t, '=')) == NULL)
            continue;
        *eq = '\0';
        char *key = trim(t), *val = trim(eq + 1);
        if (strcmp(section, "data") == 0) {
            for (int32_t i = 0; i < SD_STEP_COUNT; i++)
                if (strcmp(key, kPartKey[i]) == 0)
                    ini->version[i] = (int32_t)atoi(val);
        } else if (strcmp(section, "sources") == 0) {
            for (int32_t i = 0; i < SRC_COUNT; i++)
                if (strcmp(key, kSourceKey[i]) == 0)
                    snprintf(ini->source[i], sizeof ini->source[i], "%s", val);
        }
    }
    fclose(f);
    return 0;
}

static int32_t ini_write(const char *assetsDir, const SdAssetsIni *ini, char *err)
{
    char path[1100], tmp[1110];
    FILE *f;
    Sg_PathJoin(path, sizeof path, assetsDir, ASSETS_INI_NAME);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    if ((f = fopen(tmp, "w")) == NULL) {
        snprintf(err, 256, "cannot write %.200s", path);
        return -1;
    }
    fprintf(f, "; " ASSETS_INI_NAME " -- written by OpenSWOS Setup: what it made in this folder.\n"
               "; [data]: the data version of each part made (OpenSWOS Setup asks you to install\n"
               "; again when a newer setup makes a part differently). [sources]: what it was made\n"
               "; from, filled in for you next time. Your own paths: do not share this file.\n"
               "\n[data]\n");
    for (int32_t i = 0; i < SD_STEP_COUNT; i++)
        if (ini->version[i] > 0)
            fprintf(f, "%s = %d\n", kPartKey[i], (int)ini->version[i]);
    fprintf(f, "\n[sources]\n");
    for (int32_t i = 0; i < SRC_COUNT; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s", ini->source[i]);
#ifdef _WIN32
        for (char *c = p; *c; c++)      /* the scan joins with '/': show Windows paths as such */
            if (*c == '/') *c = '\\';
#endif
        fprintf(f, "%s = %s\n", kSourceKey[i], p);
    }
    if (fclose(f) != 0) {
        snprintf(err, 256, "cannot write %.200s", path);
        remove(tmp);
        return -1;
    }
    remove(path);                       /* Windows will not rename over a file */
    if (rename(tmp, path) != 0) {
        snprintf(err, 256, "cannot write %.200s", path);
        return -1;
    }
    return 0;
}

/* After a run: the steps that ran at this setup's versions, the sources it used. */
static int32_t write_assets_ini(SdJob *job, const SdStepInfo info[SD_STEP_COUNT], char *err)
{
    SdAssetsIni ini;
    ini_read(job->outDir, &ini);
    for (int32_t i = 0; i < SD_STEP_COUNT; i++)
        if (info[i].ready)
            ini.version[i] = kPartVersion[i];
    for (int32_t i = 0; i < SRC_COUNT; i++) {
        const char *used = source_field(&job->src, i);
        if (used[0])
            snprintf(ini.source[i], sizeof ini.source[i], "%s", used);
    }
    return ini_write(job->outDir, &ini, err);
}

int32_t SwosData_LoadSources(const char *assetsDir, SdSources *s)
{
    SdAssetsIni ini;
    int32_t n = 0;
    if (!assetsDir || !assetsDir[0] || ini_read(assetsDir, &ini) != 0)
        return 0;
    for (int32_t i = 0; i < SRC_COUNT; i++) {
        const char *p = ini.source[i];
        char probe[1100];
        if (!p[0] || source_field(s, i)[0])
            continue;
        if (i == SRC_DOS) {
            /* The PC game folder: recognised by its SPRITE.DAT, like the scan does. */
            if (Sg_FindNoCase(p, "SPRITE.DAT", probe, sizeof probe) == 0 &&
                SdSources_AddFile(s, probe) == 0 && s->dosDir[0])
                n++;
        } else if (SdSources_AddFile(s, p) == 0 && source_field(s, i)[0]) {
            n++;
        }
    }
    return n;
}

/* ---- what is installed already ------------------------------------------ */

/* SwosData_ListFiles: every file SwosData_Installed counts, with its step. */
static void (*s_listFile)(void *user, SdStep step, const char *rel, int32_t present);
static void *s_listUser;
static const SdInstalled *s_listBase;

static void count_file(const char *assetsDir, const char *rel, SdInstalled *n)
{
    char path[1100];
    FILE *f;
    const int32_t before = n->present;
    n->total++;
    Sg_PathJoin(path, sizeof path, assetsDir, rel);
    if ((f = fopen(path, "rb")) != NULL) {
        fclose(f);
        n->present++;
    } else if (Sg_FindNoCase(assetsDir, rel, path, sizeof path) == 0) {
        n->present++;   /* the game finds its files in any case, and so do we */
    }
    if (s_listFile)
        s_listFile(s_listUser, (SdStep)(n - s_listBase), rel, n->present > before);
}

/* The files each step writes, from the same lists the steps use. */
void SwosData_Installed(const char *assetsDir, SdInstalled out[SD_STEP_COUNT])
{
    char rel[160];
    memset(out, 0, sizeof(SdInstalled) * SD_STEP_COUNT);
    if (!assetsDir || !assetsDir[0])
        return;

    /* The team files in the Amiga list are the teams step's: it writes them from
     * the PC game too, so they say nothing about the Amiga disks. */
    SdInstalled *n = &out[SD_STEP_AMIGA_FILES];
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++)
        if (strncmp(g_sdAmigaCopy[i], "data/", 5) != 0)
            count_file(assetsDir, g_sdAmigaCopy[i], n);

    n = &out[SD_STEP_DOS_FILES];
    for (int32_t i = 0; g_sdDosCopy[i].dest; i++)
        count_file(assetsDir, g_sdDosCopy[i].dest, n);

    n = &out[SD_STEP_AGA_PC];
    for (int32_t b = 0; b < SG_BANK_COUNT; b++) {
        snprintf(rel, sizeof rel, "grafs_aga/%s.RAW", Sg_BankNames[b]);
        count_file(assetsDir, rel, n);
    }
    count_file(assetsDir, "grafs_aga/MENUBG.RAW", n);
    count_file(assetsDir, "grafs_aga/MENUBG2.RAW", n);
    for (int32_t p = 1; p <= 6; p++) {
        snprintf(rel, sizeof rel, "grafs_aga/SWCPICH%d.MAP", (int)p);
        count_file(assetsDir, rel, n);
    }
    count_file(assetsDir, "grafs_aga/CREDITS.RAW", n);

    n = &out[SD_STEP_AGA_AMIGA];
    for (uint32_t i = 0; i < sizeof kAmigaPictures / sizeof kAmigaPictures[0]; i++) {
        snprintf(rel, sizeof rel, "grafs_aga/%s.RAW", kAmigaPictures[i]);
        count_file(assetsDir, rel, n);
    }

    /* The AGA Amiga's own copies (kAgaPatches), with the step that makes each. */
    for (uint32_t i = 0; i < sizeof kAgaPatches / sizeof kAgaPatches[0]; i++) {
        int32_t amiga = 0;
        for (uint32_t k = 0; k < sizeof kAmigaPictures / sizeof kAmigaPictures[0]; k++)
            amiga = amiga || strcmp(kAmigaPictures[k], kAgaPatches[i].name) == 0;
        snprintf(rel, sizeof rel, AGA_ONLY_DIR "/grafs_aga/%s.RAW", kAgaPatches[i].name);
        count_file(assetsDir, rel, &out[amiga ? SD_STEP_AGA_AMIGA : SD_STEP_AGA_PC]);
    }

    n = &out[SD_STEP_TEAMS];
    for (int32_t i = 0; g_sdAmigaCopy[i]; i++) {
        char name[64];
        if (strncmp(g_sdAmigaCopy[i], "data/", 5) != 0)
            continue;
        count_file(assetsDir, g_sdAmigaCopy[i], n);
        Sd_TeamJsonName(g_sdAmigaCopy[i] + 5, name, sizeof name);
        snprintf(rel, sizeof rel, "data_json/teams/%s", name);
        count_file(assetsDir, rel, n);
    }

    n = &out[SD_STEP_JAGUAR];
    for (int32_t b = 0; b < SG_JAG_BANK_COUNT; b++) {
        snprintf(rel, sizeof rel, "grafs_jag/%s.RAW", Sg_JagBankNames[b]);
        count_file(assetsDir, rel, n);
    }
    count_file(assetsDir, "grafs_jag/JAG.PAL", n);
    count_file(assetsDir, "grafs_jag/JAGKITS.BIN", n);

    n = &out[SD_STEP_WORLD];                /* (the world the Amiga plays when asked: its package's) */
    count_file(assetsDir, "data_json/worlds/" SD_AMIGA_WORLD "/WORLD.ini", n);
    count_file(assetsDir, "data_json/worlds/" SD_AMIGA_WORLD "/competitions/leagues.json", n);

    n = &out[SD_STEP_SWOS2020];
    for (int32_t i = 0; g_sd2020Copy[i].dest; i++)
        count_file(assetsDir, g_sd2020Copy[i].dest, n);
    for (int32_t p = 1; p <= 6; p++) {
        snprintf(rel, sizeof rel, "grafs_aga/P2020_%d.MAP", (int)p);
        count_file(assetsDir, rel, n);
    }

    SdAssetsIni ini;
    if (ini_read(assetsDir, &ini) == 0)
        for (int32_t s = 0; s < SD_STEP_COUNT; s++)
            out[s].version = ini.version[s];
}

void SwosData_ListFiles(const char *assetsDir,
                        void (*file)(void *user, SdStep step, const char *rel, int32_t present),
                        void *user)
{
    SdInstalled n[SD_STEP_COUNT];
    s_listFile = file;
    s_listUser = user;
    s_listBase = n;
    SwosData_Installed(assetsDir, n);
    s_listFile = NULL;
}

void SwosData_DefaultAssetsDir(char *out, uint32_t cap)
{
    const char *env = getenv("SWOS_ASSETS");
    if (env && *env) {
        snprintf(out, cap, "%s", env);
        return;
    }
#ifdef _WIN32
    const char *profile = getenv("USERPROFILE");
    snprintf(out, cap, "%s/Documents/SWOS/assets", profile ? profile : ".");
    for (char *p = out; *p; p++)
        if (*p == '\\')
            *p = '/';
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/Library/Application Support/SWOS/assets", home ? home : ".");
#else
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/.swos/assets", home ? home : ".");
#endif
}

/* Copy every file under src/rel to dst/rel. Returns how many, or -1. */
static int32_t copy_tree(const char *src, const char *dst, const char *rel, char *err)
{
    char dir[1100];
    int32_t count = 0;
    Sg_PathJoin(dir, sizeof dir, src, rel);
#ifdef _WIN32
    char pattern[1100];
    Sg_PathJoin(pattern, sizeof pattern, dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const char *name = fd.cFileName;
        int32_t isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        char full[1100];
        struct stat st;
        Sg_PathJoin(full, sizeof full, dir, name);
        int32_t isDir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        if (name[0] == '.')
            continue;
        char sub[1100];
        Sg_PathJoin(sub, sizeof sub, rel, name);
        if (isDir) {
            int32_t n = copy_tree(src, dst, sub, err);
            if (n < 0) {
                count = -1;
                break;
            }
            count += n;
        } else {
            char from[1100], to[1100];
            SgBuf b;
            Sg_PathJoin(from, sizeof from, src, sub);
            Sg_PathJoin(to, sizeof to, dst, sub);
            if (Sg_ReadFile(from, &b, err) != 0 || Sg_WriteFile(to, b.data, b.size, err) != 0) {
                Sg_BufFree(&b);
                count = -1;
                break;
            }
            Sg_BufFree(&b);
            count++;
        }
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
    return count;
}

/* A ready drawer: the program (`programDir` holds SWOS.info and the SWOS drawer, as
 * SWOS-Amiga.zip unpacks) copied into `destDir`, then filled as above. */
int32_t SwosData_MakeAmigaDrawer(const char *assetsDir, const char *programDir, const char *destDir,
                                 int32_t withWorld, void (*log)(void *user, const char *line), void *user,
                                 char *err)
{
    char from[1100], to[1100], drawer[1100];
    SgBuf b;
    Sg_PathJoin(from, sizeof from, programDir, "SWOS/SWOS2");
    if (!Sg_IsFile(from)) {
        snprintf(err, 256, "%s has no SWOS drawer in it", programDir);
        return -1;
    }
    int32_t n = copy_tree(programDir, destDir, "SWOS", err);
    if (n < 0)
        return -1;
    /* The drawer's icon, and the sidecars an emulator keeps the Amiga's file flags in. */
    static const char *const kBeside[] = { "SWOS.info", "SWOS.info.uaem", "SWOS.uaem" };
    for (uint32_t i = 0; i < sizeof kBeside / sizeof kBeside[0]; i++) {
        Sg_PathJoin(from, sizeof from, programDir, kBeside[i]);
        Sg_PathJoin(to, sizeof to, destDir, kBeside[i]);
        if (!Sg_IsFile(from))
            continue;
        if (Sg_ReadFile(from, &b, err) != 0)
            return -1;
        int32_t rc = Sg_WriteFile(to, b.data, b.size, err);
        Sg_BufFree(&b);
        if (rc != 0)
            return -1;
    }
    if (log) {
        char line[256];
        snprintf(line, sizeof line, "Amiga drawer: the program (%d files)", n);
        log(user, line);
    }
    Sg_PathJoin(drawer, sizeof drawer, destDir, "SWOS");
    return SwosData_FillAmigaDrawer(assetsDir, drawer, withWorld, log, user, err);
}

/* ---- the WHDLoad package --------------------------------------------------- */

static int32_t strcasecmp_portable(const char *a, const char *b)
{
    for (;; a++, b++) {
        int32_t x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y || !x)
            return x - y;
    }
}

/* What goes into the archive: its path there, and the file it is read from. A later
 * entry with the same path replaces the earlier (the AGA-only pictures). */
typedef struct {
    char rel[300];
    char from[1100];
} PkgFile;

typedef struct {
    PkgFile *f;
    int32_t  n, cap;
} Pkg;

static void pkg_add(Pkg *p, const char *rel, const char *from)
{
    for (int32_t i = 0; i < p->n; i++) {
        if (strcasecmp_portable(p->f[i].rel, rel) == 0) {
            snprintf(p->f[i].from, sizeof p->f[i].from, "%s", from);
            return;
        }
    }
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->f = realloc(p->f, sizeof(PkgFile) * (size_t)p->cap);
    }
    snprintf(p->f[p->n].rel, sizeof p->f[p->n].rel, "%s", rel);
    snprintf(p->f[p->n].from, sizeof p->f[p->n].from, "%s", from);
    p->n++;
}

/* Every file under src/rel into the package as prefix + rel. Returns how many. */
static int32_t pkg_tree(Pkg *p, const char *src, const char *rel, const char *prefix)
{
    char dir[1100];
    int32_t count = 0;
    Sg_PathJoin(dir, sizeof dir, src, rel);
#ifdef _WIN32
    char pattern[1100];
    Sg_PathJoin(pattern, sizeof pattern, dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const char *name = fd.cFileName;
        int32_t isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        char full[1100];
        struct stat st;
        Sg_PathJoin(full, sizeof full, dir, name);
        int32_t isDir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        if (name[0] == '.')
            continue;
        char sub[600], from[1100], to[700];
        snprintf(sub, sizeof sub, "%s/%s", rel, name);
        if (isDir) {
            count += pkg_tree(p, src, sub, prefix);
        } else {
            Sg_PathJoin(from, sizeof from, src, sub);
            snprintf(to, sizeof to, "%s%s", prefix, sub);
            pkg_add(p, to, from);
            count++;
        }
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
    return count;
}

/* The folder name WHDLoad users already have it under, so a new package unpacks over
 * an old one. */
#define WHD_TOP   "OpenSWOSAGA-NE"

int32_t SwosData_MakeWhdload(const char *assetsDir, const char *drawerDir, const char *resDir,
                             const char *outPath, int32_t withWorld, void (*log)(void *user, const char *line),
                             void *user, char *err)
{
    static const char *const kTrees[] = { "data", "grafs", "grafs_aga", "sound" };
    /* The icons (scripts/amiga/make_whdload_icons.py): the package's drawer, and the
     * Slave's, whose default tool is WHDLoad. */
    static const char *const kTemplate[][2] = {
        { WHD_TOP ".info",               "setup/whdload/" WHD_TOP ".info" },
        { WHD_TOP "/OpenSWOS.info",      "setup/whdload/OpenSWOS.info" },
    };
    /* From the drawer: the Slave beside its icon, the ReadMe; the program in data/,
     * the Slave's ws_CurrentDir, with the trees it opens. */
    static const char *const kProgram[][2] = {
        { "OpenSWOS.slave", WHD_TOP "/OpenSWOS.slave" },
        { "SWOS2",          WHD_TOP "/data/SWOS2" },
        { "ReadMe",         WHD_TOP "/ReadMe" },
        { "ReadMe.info",    WHD_TOP "/ReadMe.info" },
    };
    char probe[1100], line[300];
    Pkg pkg = { NULL, 0, 0 };
    int32_t rc = -1;

    if (Sg_FindNoCase(drawerDir, "SWOS2", probe, sizeof probe) != 0) {
        snprintf(err, 256, "%.200s is not a SWOS drawer (no SWOS2 in it)", drawerDir);
        return -1;
    }
    if (Sg_FindNoCase(drawerDir, "OpenSWOS.slave", probe, sizeof probe) != 0) {
        snprintf(err, 256, "%.180s has no OpenSWOS.slave: it is from an older SWOS-Amiga.zip",
                 drawerDir);
        return -1;
    }
    if (Sg_FindNoCase(assetsDir, "grafs/MENUS.RAW", probe, sizeof probe) != 0 ||
        Sg_FindNoCase(assetsDir, "grafs_aga/MENUS.RAW", probe, sizeof probe) != 0) {
        snprintf(err, 256, "the game data is not complete: the Amiga version needs both the "
                           "Amiga game and the PC game");
        return -1;
    }
    for (uint32_t i = 0; i < sizeof kTemplate / sizeof kTemplate[0]; i++) {
        Sg_PathJoin(probe, sizeof probe, resDir, kTemplate[i][1]);
        FILE *f = fopen(probe, "rb");
        if (!f) {
            snprintf(err, 256, "the setup's own file %s is missing", kTemplate[i][1]);
            goto done;
        }
        fclose(f);
        pkg_add(&pkg, kTemplate[i][0], probe);
    }
    for (uint32_t i = 0; i < sizeof kProgram / sizeof kProgram[0]; i++)
        if (Sg_FindNoCase(drawerDir, kProgram[i][0], probe, sizeof probe) == 0)
            pkg_add(&pkg, kProgram[i][1], probe);
    /* The drawer's own files in those folders first (OpenSWOS's: the credits photos,
     * the AGA screens it ships), then the game data over them -- as SwosData_FillAmigaDrawer
     * leaves a drawer. */
    for (uint32_t i = 0; i < sizeof kTrees / sizeof kTrees[0]; i++)
        pkg_tree(&pkg, drawerDir, kTrees[i], WHD_TOP "/data/");
    for (uint32_t i = 0; i < sizeof kTrees / sizeof kTrees[0]; i++) {
        int32_t n = pkg_tree(&pkg, assetsDir, kTrees[i], WHD_TOP "/data/");
        if (log) {
            snprintf(line, sizeof line, "WHDLoad package: data/%s/ (%d files)", kTrees[i], n);
            log(user, line);
        }
    }
    {   /* The AGA Amiga's own pictures over the shared ones, as in the drawer. */
        char amigaOnly[1100];
        Sg_PathJoin(amigaOnly, sizeof amigaOnly, assetsDir, AGA_ONLY_DIR);
        pkg_tree(&pkg, amigaOnly, "grafs_aga", WHD_TOP "/data/");
    }
    if (withWorld) {   /* Asked for: the world's team files and data/WORLD, as in the drawer. */
        char world[1100];
        Sg_PathJoin(world, sizeof world, assetsDir, SD_WORLD_AMIGA_DIR);
        if (Sg_FindNoCase(world, "data/WORLD", probe, sizeof probe) == 0)
            pkg_tree(&pkg, world, "data", WHD_TOP "/");
    }

    {
        LhaWriter *w = Lha_Create(outPath, err);
        if (!w)
            goto done;
        for (int32_t i = 0; i < pkg.n; i++) {
            SgBuf b;
            if (Sg_ReadFile(pkg.f[i].from, &b, err) != 0 ||
                Lha_AddFile(w, pkg.f[i].rel, b.data, b.size, 0, err) != 0) {
                Sg_BufFree(&b);
                Lha_Close(w, line);
                goto done;
            }
            Sg_BufFree(&b);
        }
        /* The program writes saves there but will not create the folder. */
        if (Lha_AddDir(w, WHD_TOP "/data/save/", 0, err) != 0) {
            Lha_Close(w, line);
            goto done;
        }
        if (Lha_Close(w, err) != 0)
            goto done;
    }
    if (log) {
        snprintf(line, sizeof line, "WHDLoad package: %d files in %.200s", (int)pkg.n + 1, outPath);
        log(user, line);
    }
    rc = 0;
done:
    free(pkg.f);
    return rc;
}

int32_t SwosData_FillAmigaDrawer(const char *assetsDir, const char *drawerDir, int32_t withWorld,
                                 void (*log)(void *user, const char *line), void *user,
                                 char *err)
{
    static const char *const kTrees[] = { "data", "grafs", "grafs_aga", "sound" };
    char probe[1100];
    if (Sg_FindNoCase(drawerDir, "SWOS2", probe, sizeof probe) != 0) {
        snprintf(err, 256, "%s is not a SWOS drawer (no SWOS2 in it): unpack SWOS-Amiga.zip first",
                 drawerDir);
        return -1;
    }
    if (Sg_FindNoCase(assetsDir, "grafs/MENUS.RAW", probe, sizeof probe) != 0 ||
        Sg_FindNoCase(assetsDir, "grafs_aga/MENUS.RAW", probe, sizeof probe) != 0) {
        snprintf(err, 256, "the game data is not complete: the Amiga version needs both the "
                           "Amiga disks and the PC game");
        return -1;
    }
    for (uint32_t i = 0; i < sizeof kTrees / sizeof kTrees[0]; i++) {
        int32_t n = copy_tree(assetsDir, drawerDir, kTrees[i], err);
        if (n < 0)
            return -1;
        if (log) {
            char line[256];
            snprintf(line, sizeof line, "Amiga drawer: %s/ (%d files)", kTrees[i], n);
            log(user, line);
        }
    }
    /* Then the AGA Amiga's own pictures over the shared ones: the ball that says AGA
     * (kAgaPatches; the PC builds keep the game's 96-97 ball). */
    {
        char amigaOnly[1100];
        Sg_PathJoin(amigaOnly, sizeof amigaOnly, assetsDir, AGA_ONLY_DIR);
        if (Sg_FindNoCase(amigaOnly, "grafs_aga", probe, sizeof probe) == 0 &&
            copy_tree(amigaOnly, drawerDir, "grafs_aga", err) < 0)
            return -1;
    }
    /* Then, asked for, the world's team files and data/WORLD (SwosData_InstallWorld); not asked
     * for, no data/WORLD an earlier drawer left (96/97's team files were just copied over its). */
    if (!withWorld) {
        Sg_PathJoin(probe, sizeof probe, drawerDir, "data/WORLD");
        remove(probe);
    } else {
        char world[1100];
        Sg_PathJoin(world, sizeof world, assetsDir, SD_WORLD_AMIGA_DIR);
        if (Sg_FindNoCase(world, "data/WORLD", probe, sizeof probe) == 0) {
            int32_t n = copy_tree(world, drawerDir, "data", err);
            if (n < 0)
                return -1;
            if (log) {
                char line[256];
                snprintf(line, sizeof line, "Amiga drawer: the world %s (%d files)", SD_AMIGA_WORLD, n);
                log(user, line);
            }
        }
    }
    /* The program writes saves there but will not create the folder. */
    Sg_PathJoin(probe, sizeof probe, drawerDir, "save");
    Sg_MakeDirs(probe);
    return 0;
}

/* A world (the game's include/world.h): a world's built files -- what scripts/world_import.py
 * writes into bin/data_json/worlds/<tag>/ from the SWOS 96/97 Mod's data -- into the assets
 * folder's data_json/worlds/<tag>/: teams/ and competitions/, which the PC game reads there, and
 * amiga/data/ (the world's binary team files and WORLD, naming it), which SwosData_FillAmigaDrawer
 * and SwosData_MakeWhdload put in the Amiga's data/. Given a SWOS drawer, that too at once. */
int32_t SwosData_InstallWorld(const char *worldDir, const char *tag, const char *assetsDir,
                              const char *drawerDir, void (*log)(void *user, const char *line),
                              void *user, char *err)
{
    static const char *const kTrees[] = { "teams", "competitions" };
    char dst[1100], amiga[1100], rel[64], probe[1100], line[256];
    if (!tag || !tag[0] || strlen(tag) > 8 || strchr(tag, '/') || strchr(tag, '\\')) {
        snprintf(err, 256, "a world's tag is a short name (2627)");
        return -1;
    }
    if (Sg_FindNoCase(worldDir, "competitions/leagues.json", probe, sizeof probe) != 0) {
        snprintf(err, 256, "%.200s is not a built world (no competitions/leagues.json)", worldDir);
        return -1;
    }
    snprintf(rel, sizeof rel, "data_json/worlds/%s", tag);
    Sg_PathJoin(dst, sizeof dst, assetsDir, rel);
    Sg_MakeDirs(dst);
    for (uint32_t i = 0; i < sizeof kTrees / sizeof kTrees[0]; i++) {
        int32_t n = copy_tree(worldDir, dst, kTrees[i], err);
        if (n < 0)
            return -1;
        if (log) {
            snprintf(line, sizeof line, "world %s: %s/ (%d files)", tag, kTrees[i], n);
            log(user, line);
        }
    }
    {   /* its WORLD.ini (the tag, the data's fingerprint: the game checks it) */
        char from[1100], to[1100];
        SgBuf b;
        if (Sg_FindNoCase(worldDir, "WORLD.ini", from, sizeof from) == 0) {
            Sg_PathJoin(to, sizeof to, dst, "WORLD.ini");
            if (Sg_ReadFile(from, &b, err) != 0 || Sg_WriteFile(to, b.data, b.size, err) != 0) {
                Sg_BufFree(&b);
                return -1;
            }
            Sg_BufFree(&b);
        }
    }
    Sg_PathJoin(amiga, sizeof amiga, dst, "amiga");
    int32_t n = copy_tree(worldDir, amiga, "data", err);
    if (n < 0)
        return -1;
    Sg_PathJoin(probe, sizeof probe, amiga, "data/WORLD");
    if (n && Sg_WriteFile(probe, (const uint8_t *)tag, (uint32_t)strlen(tag), err) != 0)
        return -1;
    if (log) {
        snprintf(line, sizeof line, "world %s: the Amiga's data/ (%d files)", tag, n);
        log(user, line);
    }
    if (drawerDir) {
        if (Sg_FindNoCase(drawerDir, "SWOS2", probe, sizeof probe) != 0) {
            snprintf(err, 256, "%.200s is not a SWOS drawer (no SWOS2 in it)", drawerDir);
            return -1;
        }
        if (copy_tree(amiga, drawerDir, "data", err) < 0)
            return -1;
        if (log) {
            snprintf(line, sizeof line, "world %s: the drawer's data/", tag);
            log(user, line);
        }
    }
    return 0;
}

int32_t SwosData_HaveAmigaWorld(const char *assetsDir)
{
    char world[1100], probe[1100];
    Sg_PathJoin(world, sizeof world, assetsDir, SD_WORLD_AMIGA_DIR);
    return Sg_FindNoCase(world, "data/WORLD", probe, sizeof probe) == 0;
}
