/*
 * assets_ini.h -- ASSETS.ini, what OpenSWOS Setup made in the assets folder.
 *
 * The setup (tools/swosdata) writes ASSETS.ini at the top of the assets folder after
 * every install: for each PART it made, the DATA VERSION of that part, and the folders
 * and files it made them from. The game only reads it (src/assets_check.c).
 *
 *   [data]                      [sources]
 *   amiga_files = 1             amiga_disk1 = D:\ADF\SWOS 96-97 Disk 1.adf
 *   dos_files = 2               amiga_disk2 = ...
 *   ...                         pc = C:\GOG Games\Sensible World of Soccer 96-97
 *
 * A part's data version goes up ONLY when the setup's output for that part changes --
 * not with every release -- so a player is asked to install again only when a part
 * they have was made differently by an older setup. scripts/check_data_versions.py
 * fails when a part's output changes and its number does not.
 *
 * The parts are the setup's steps (SdStep, tools/swosdata/lib/swosdata.h), in the same
 * order: X(NAME, "ini key", data version).
 */
#ifndef ASSETS_INI_H
#define ASSETS_INI_H

#define ASSETS_INI_NAME "ASSETS.ini"

#define ASSETS_PARTS(X)                                                                 \
    X(AMIGA_FILES, "amiga_files", 1)  /* data/, grafs/, sound/ from the Amiga disks */  \
    X(DOS_FILES,   "dos_files",   1)  /* grafs_dos/, sound_dos/ from the PC game */     \
    X(AGA_PC,      "aga_pc",      1)  /* grafs_aga/ backdrops, banks, pitches, credits */\
    X(AGA_AMIGA,   "aga_amiga",   1)  /* grafs_aga/ loading and disk screens */         \
    X(TEAMS,       "teams",       1)  /* data_json/teams/ (and data/ teams) */          \
    X(JAGUAR,      "jaguar",      1)  /* grafs_jag/ */                                  \
    X(SWOS2020,    "swos2020",    1)  /* grafs_2020/, grafs_aga/P2020_n.MAP */          \
    X(WORLD,       "world",       1)  /* data_json/worlds/<tag>/: a world's package */

/* The [sources] keys. */
#define ASSETS_SRC_AMIGA_DISK1 "amiga_disk1"
#define ASSETS_SRC_AMIGA_DISK2 "amiga_disk2"
#define ASSETS_SRC_AMIGA_HDF   "amiga_hdf"      /* a hard disk image with the game on it */
#define ASSETS_SRC_AMIGA_LHA   "amiga_lha"      /* a WHDLoad install packed as .lha */
#define ASSETS_SRC_PC          "pc"
#define ASSETS_SRC_JAGUAR      "jaguar"
#define ASSETS_SRC_SWOS2020    "swos2020"
#define ASSETS_SRC_WORLD       "world"          /* a world's package (OpenSWOS-World-<tag>.zip) */

#endif /* ASSETS_INI_H */
