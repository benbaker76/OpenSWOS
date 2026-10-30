/*
 * swosdata.h -- build OpenSWOS's game data from the player's own copy of the game.
 *
 * OpenSWOS ships no original game data. The player points this at what they
 * own -- the Amiga floppies, the PC version (GOG), the Jaguar cartridge, SWOS 2020 -- and
 * it writes the trees the game reads into an assets folder, laid out as the
 * game's bin/ folder is: data/, grafs/, grafs_aga/, grafs_dos/, grafs_jag/,
 * grafs_2020/, sound/, sound_dos/, data_json/teams/.
 *
 * Each step needs some of the sources and is skipped, with a note, without
 * them; SwosData_Plan says beforehand what will and will not be made.
 */
#ifndef SWOSDATA_H
#define SWOSDATA_H

#include <stdint.h>
#include "sd_sources.h"

typedef struct {
    SdSources   src;
    char        outDir[1024];   /* the assets folder */
    char        resDir[1024];   /* the game's own files, laid out as bin/ is:
                                 * data/credits_icons.bmp, setup/glyphs.txt, ... */
    /* Called for every line worth showing, and for progress. Either may be NULL. */
    void      (*log)(void *user, const char *line);
    void      (*progress)(void *user, int32_t done, int32_t total);
    void       *user;
} SdJob;

typedef enum {
    SD_STEP_AMIGA_FILES,    /* data/, grafs/, sound/ from the Amiga disks */
    SD_STEP_DOS_FILES,      /* grafs_dos/, sound_dos/ from the PC game */
    SD_STEP_AGA_PC,         /* grafs_aga/: backdrops, pitches, credits from the PC game */
    SD_STEP_AGA_AMIGA,      /* grafs_aga/: the loading and disk screens, from the Amiga */
    SD_STEP_TEAMS,          /* data_json/teams/ (and data/ without the Amiga disks) */
    SD_STEP_JAGUAR,         /* grafs_jag/: the Jaguar's sprites, gaps from the PC game */
    SD_STEP_SWOS2020,       /* grafs_2020/ and grafs_aga/P2020_n.MAP: SWOS 2020's pitches */
    SD_STEP_COUNT
} SdStep;

typedef struct {
    const char *name;       /* what it makes, for people */
    int32_t     ready;      /* its sources are there */
    const char *needs;      /* what it needs, for people */
} SdStepInfo;

/* What each step will do with the sources in `job`. */
void    SwosData_Plan(const SdJob *job, SdStepInfo info[SD_STEP_COUNT]);

/* The assets folder the game reads (src/swos_io.c swos_assets_dir): $SWOS_ASSETS,
 * else "assets" in the per-user game folder -- Documents/SWOS on Windows,
 * ~/Library/Application Support/SWOS on macOS, ~/.swos elsewhere. */
void    SwosData_DefaultAssetsDir(char *out, uint32_t cap);

/* What an earlier run left in `assetsDir`: for each step, how many of the
 * files it writes are there, of how many, and the data version ASSETS.ini says it
 * was made at (0: no ASSETS.ini, or not listed -- an older setup's, include/assets_ini.h).
 * A step is installed when all its files are, and up to date when its version is
 * SwosData_PartVersion's. */
typedef struct {
    int32_t present, total;
    int32_t version;
} SdInstalled;
void    SwosData_Installed(const char *assetsDir, SdInstalled out[SD_STEP_COUNT]);

/* Every file SwosData_Installed counts, with the step that makes it, and whether it is
 * there: what scripts/check_data_versions.py fingerprints per part. */
void    SwosData_ListFiles(const char *assetsDir,
                           void (*file)(void *user, SdStep step, const char *rel, int32_t present),
                           void *user);

/* The data version this setup makes each step at, and its ASSETS.ini key. */
int32_t     SwosData_PartVersion(SdStep step);
const char *SwosData_PartKey(SdStep step);

/* Fill in `s` from the folders and files ASSETS.ini in `assetsDir` says the last
 * install was made from -- those still there and still recognised. A source already
 * set is left as it is. Returns how many it filled in. */
int32_t SwosData_LoadSources(const char *assetsDir, SdSources *s);

/* Run every step whose sources are there, then write ASSETS.ini: the steps that ran
 * at this setup's data versions (the others keep what it said), and the sources used
 * (the others keep theirs). 0 when everything that ran succeeded; otherwise `err`
 * (256 bytes) names the first failure. */
int32_t SwosData_Run(SdJob *job, char *err);

/* The Amiga version: copy what the Amiga binary reads (data/, grafs/,
 * grafs_aga/, sound/) from an assets folder SwosData_Run filled into a SWOS
 * drawer unpacked from SWOS-Amiga.zip, which ships the program but no game
 * data. The drawer is recognised by its SWOS2 program. Needs the Amiga disks'
 * files in the assets folder (grafs/, sound/). 0 on success. */
int32_t SwosData_FillAmigaDrawer(const char *assetsDir, const char *drawerDir,
                                 void (*log)(void *user, const char *line), void *user,
                                 char *err);

/* A ready drawer in `destDir`: SWOS.info and SWOS/ copied from `programDir` (where
 * SWOS-Amiga.zip was unpacked; the setup carries one in amiga/), then filled as
 * SwosData_FillAmigaDrawer does. 0 on success. */
int32_t SwosData_MakeAmigaDrawer(const char *assetsDir, const char *programDir, const char *destDir,
                                 void (*log)(void *user, const char *line), void *user, char *err);

/* The WHDLoad package, OpenSWOSAGA-WHDL.lha at `outPath`: OpenSWOS's own Slave
 * (OpenSWOS.slave, from the SWOS drawer `drawerDir` -- unpacked SWOS-Amiga.zip, built
 * with its SWOS2) and the icons in resDir/setup/whdload, the program in the package's
 * data/ (the Slave's folder) with the Amiga game data from the assets folder, as
 * SwosData_FillAmigaDrawer puts it in a drawer. No Kickstart image, no AmigaOS files:
 * the Slave starts SWOS2 directly. 0 on success. */
int32_t SwosData_MakeWhdload(const char *assetsDir, const char *drawerDir, const char *resDir,
                             const char *outPath, void (*log)(void *user, const char *line),
                             void *user, char *err);

/* The game's default world (World 2026-27) and, in an assets folder, where its Amiga files are. */
#define SD_DEFAULT_WORLD   "2627"
#define SD_WORLD_AMIGA_DIR "data_json/worlds/" SD_DEFAULT_WORLD "/amiga"

/* A world's built files (`worldDir`: scripts/world_import.py's bin/data_json/worlds/<tag>/ --
 * teams/, competitions/, data/) into the assets folder's data_json/worlds/<tag>/, where the PC
 * game reads them (--world=<tag> or [general] world), and, given a SWOS drawer, its data/ over
 * the drawer's with data/WORLD naming the world. OpenSWOS ships no world's data: the player
 * brings it. 0 on success. */
int32_t SwosData_InstallWorld(const char *worldDir, const char *tag, const char *assetsDir,
                              const char *drawerDir, void (*log)(void *user, const char *line),
                              void *user, char *err);

#endif
