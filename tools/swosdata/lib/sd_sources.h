/*
 * sd_sources.h -- the player's own copy of the game, wherever it is.
 *
 * Five kinds of source, any combination:
 *   - the Amiga version: its two floppy images (.adf), told apart by their volume
 *     names, "SWOS" (disk 1) and "SWOS2" (disk 2), never by file name -- or a hard
 *     disk image (.hdf) with the game installed on it (ASWOS's, sensiblesoccer.de's,
 *     a WinUAE disk), which stands in for both. Its team data says which SEASON it
 *     is (SdSources_AmigaSeason): the 96/97 release or a community update.
 *   - the PC version: the installed game folder (GOG's, or any copy of one) --
 *     found by SPRITE.DAT, any case.
 *   - the Atari Jaguar version: the cartridge image (.jag, .j64, .rom or .bin, 2 MB).
 *   - a world's data (the game's include/world.h): OpenSWOS-World-<tag>.zip, the package
 *     scripts/make_world_package.sh makes (hosted apart from OpenSWOS: World 2026-27's squads
 *     are its authors'), or the folder it was unpacked to -- found by its WORLD.ini.
 *   - SWOS 2020: its install folder, for the pitches its launcher has put in the
 *     game's own folder (swos/) -- whichever set the player picked. Found by the
 *     program in that folder, swos-port-Release.exe; that folder also looks like
 *     the PC version's (SPRITE.DAT, DATA/TEAM.000), and must never be taken for it.
 *
 * The caller names folders to search (the setup app's "Choose a folder"),
 * or a file or folder for one source; SdSources_Scan fills in what it finds.
 */
#ifndef SWOSDATA_SOURCES_H
#define SWOSDATA_SOURCES_H

#include <stdint.h>
#include "adf.h"
#include "lha.h"
#include "sg_util.h"

typedef struct {
    char  adf1[1024];       /* Amiga disk 1 (volume "SWOS"), "" when not found */
    char  adf2[1024];       /* Amiga disk 2 (volume "SWOS2") */
    char  hdf[1024];        /* an Amiga hard disk image with the game on it */
    int32_t hdfVolume;      /* which of its volumes (set by SdSources_Open) */
    char  hdfDir[256];      /* the game's folder on it, "" = the top, else "SWOS/" */
    char  lha[1024];        /* a WHDLoad install packed .lha with the game in it */
    char  lhaDir[256];      /* the game's folder in it ("SWOS2425/data/") */
    char  dosDir[1024];     /* the PC game folder */
    char  jagRom[1024];     /* the Jaguar cartridge image */
    char  s2020Dir[1024];   /* SWOS 2020's game folder: <install>/swos */
    char  world[1024];      /* a world's package (.zip) or the folder with its WORLD.ini */
    Adf  *disk1, *disk2;    /* open while SdSources_Open .. Close */
    Adf  *hdfDisk;
    LhaArchive *lhaArc;
} SdSources;

/* Look through `dir` and every folder inside it, however deep, for any of the
 * four. A source already set is left as it is. Links are not followed, hidden
 * and system folders are skipped, a game folder once recognised is not looked
 * inside, and the search ends when all four are found. Returns how many are
 * known in all. */
int32_t SdSources_Scan(SdSources *s, const char *dir);

/* The same, telling `progress` (may be NULL) about every folder as it goes:
 * how many so far and which. A non-zero return stops the search; then -1. */
typedef int32_t (*SdScanProgress)(void *user, int32_t folders, const char *dir);
int32_t SdSources_ScanEx(SdSources *s, const char *dir, SdScanProgress progress, void *user);

/* Check one file the user picked: an .adf (either disk), the cartridge, a file
 * inside the PC game folder (its folder is taken), or a SWOS 2020 install (its
 * folder, its swos/ folder or a file in either). 0 when recognised. */
int32_t SdSources_AddFile(SdSources *s, const char *path);

/* The Amiga program, SWOS-Amiga.zip, unpacked whole to a folder of the setup's own under
 * the system's temporary folder; that folder (the one with SWOS.info and the SWOS drawer)
 * in `dir`. 0 when it holds SWOS/SWOS2. */
int32_t SdSources_UnpackAmigaProgram(const char *zip, char *dir, uint32_t cap);

int32_t SdSources_HaveAmiga(const SdSources *s);   /* both disks, a hard disk image or a WHDLoad .lha */

/* The season of the Amiga source's team data: "96/97", "24/25" ... from its
 * fingerprint (sd_manifest.c), "an unknown season" when it is none of those, NULL when
 * there is no Amiga source or it cannot be read. Opens and closes it itself. */
const char *SdSources_AmigaSeason(SdSources *s);
/* Why the last SdSources_AmigaSeason gave NULL: "data/TEAM.067: its blocks on the disk
 * are damaged" -- "" when it did not. */
const char *SdSources_AmigaProblem(void);
int32_t SdSources_HaveDos(const SdSources *s);
int32_t SdSources_HaveJaguar(const SdSources *s);
int32_t SdSources_HaveSwos2020(const SdSources *s);
int32_t SdSources_HaveWorld(const SdSources *s);

/* A world's package or folder: its WORLD.ini read -- the world's tag ("2627"), its name, the
 * fingerprint of its data (scripts/world_import.py) -- and, for a .zip, unpacked to a folder of
 * the setup's own under the system's temporary folder; that folder (with WORLD.ini) in `dir`.
 * 0 when it is one. */
typedef struct { char tag[16], name[64], fingerprint[24]; } SdWorldInfo;
int32_t SdSources_OpenWorld(const char *path, SdWorldInfo *info, char *dir, uint32_t cap);

/* Open the disks for reading. 0 on success. */
int32_t SdSources_Open(SdSources *s, char *err);
void    SdSources_Close(SdSources *s);

/* Read a file from the Amiga disks: `rel` as on the disk, e.g. "grafs/MENUS.RAW";
 * disk 1 is tried first, then disk 2 -- or the hard disk image's game folder. */
int32_t SdSources_ReadAmiga(SdSources *s, const char *rel, SgBuf *out, char *err);

/* Read a file from the PC game folder, any case. */
int32_t SdSources_ReadDos(SdSources *s, const char *rel, SgBuf *out, char *err);

#endif
