/*
 * cd_image.h -- files read off a CD image: the PC game's disc, for what its installer
 * leaves on it (the commentary, SFX/PIERCE, and the effects, SFX/FX).
 *
 * An ISO 9660 data track in any of the usual dumps: an .iso (2048-byte sectors), a
 * raw .bin (2352-byte sectors, Mode 1 or Mode 2 Form 1), or a .cue naming its .bin --
 * the data track is the first, which the PC game's mixed-mode disc puts first. Names
 * are matched without regard to case, without the ";1" version. Plain C99.
 */
#ifndef SWOSDATA_CD_IMAGE_H
#define SWOSDATA_CD_IMAGE_H

#include <stdint.h>
#include "sg_util.h"

typedef struct CdImage CdImage;

/* Open an image (.iso, .img, .bin or .cue). NULL when it is none; `err` (256) says why. */
CdImage *CdImage_Open(const char *path, char *err);
void     CdImage_Close(CdImage *cd);

/* Whether a file or folder ('/'-separated, any case) is on it: 1 a file, 2 a folder, 0 neither. */
int32_t  CdImage_Has(CdImage *cd, const char *rel);

/* Read a whole file off it. 0 on success. */
int32_t  CdImage_Read(CdImage *cd, const char *rel, SgBuf *out, char *err);

/* Copy every file on it to `dest` (its folders made, names as on the disc without ";1"), but the
 * top-level folders named in `skip` (NULL-terminated, any case; may be NULL). 0 on success. */
int32_t  CdImage_ExtractTree(CdImage *cd, const char *dest, const char *const *skip, char *err);

#endif
