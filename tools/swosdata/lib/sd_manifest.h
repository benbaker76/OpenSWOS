/*
 * sd_manifest.h -- which files the setup takes from the player's game.
 */
#ifndef SWOSDATA_MANIFEST_H
#define SWOSDATA_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *dest;   /* under the assets folder, e.g. "sound_dos/fx/cheer.raw" */
    const char *src;    /* in the PC game folder, as a GOG install spells it */
} SdCopy;

/* NULL-terminated paths, the same on the disks and in the assets folder. */
extern const char *const g_sdAmigaCopy[];

/* The Amiga game's team data, told apart by a fingerprint of the files (see
 * SdSources_AmigaSeason): the 96/97 release, and the community's later seasons as they
 * are found in the wild. */
typedef struct {
    uint64_t    fingerprint;
    const char *season;     /* "96/97", "24/25" ... */
    const char *where;      /* which copy it was taken from */
} SdAmigaSeason;
extern const SdAmigaSeason g_sdAmigaSeasons[];
/* Ends with { NULL, NULL }. */
extern const SdCopy g_sdDosCopy[];
/* 1 when a g_sdDosCopy file may be missing and the PC data is still whole: the sounds a release
 * may not have (sound_dos/fx, hard, pierce) and the cutscenes and theirs (video_dos/,
 * sound_dos/flc/, sound_dos/chairman/: the dosvideos mod plays them when they are there). */
int32_t SdManifest_DosOptional(const char *dest);
/* 1 for the cutscenes' files (video_dos/, sound_dos/flc/, sound_dos/chairman/). */
int32_t SdManifest_DosVideo(const char *dest);
/* SWOS 2020's pitches, src in its game folder (swos/). Ends with { NULL, NULL }. */
extern const SdCopy g_sd2020Copy[];

#endif
