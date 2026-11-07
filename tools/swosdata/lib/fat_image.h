/*
 * fat_image.h -- files read off a PC floppy disk image (.img, .ima: a 1.44 MB or 720 KB disk,
 * FAT12, or a small FAT16 one): the PC game's floppy release, whose two disks carry the game
 * packed as one RNC archive (rnc_archive.h). Names are 8.3, matched without regard to case.
 * Plain C99.
 */
#ifndef SWOSDATA_FAT_IMAGE_H
#define SWOSDATA_FAT_IMAGE_H

#include <stdint.h>
#include "sg_util.h"

typedef struct FatImage FatImage;

/* Open an image (read whole: a floppy is small). NULL when it is not a FAT disk; `err` (256). */
FatImage *FatImage_Open(const char *path, char *err);
void      FatImage_Close(FatImage *img);

/* Read a whole file ('/'-separated path, any case). 0 on success. */
int32_t   FatImage_Read(FatImage *img, const char *rel, SgBuf *out, char *err);
/* Whether a file of that name is on it. */
int32_t   FatImage_Has(FatImage *img, const char *rel);

#endif
