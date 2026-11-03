/*
 * cal_label.h -- a competition's short name fitted to the career fixture list's name column
 * (src/cal_label.c).
 */
#ifndef CAL_LABEL_H
#define CAL_LABEL_H

#include "swos_types.h"   /* (the Amiga build has no libc stdint.h) */

/* The column's room in the small font: x 0 .. 69, where a home-and-away row's "(H)" / "(A)"
 * starts (the Mod's CAL_PX, the same column in its DOS game). */
#define CAL_LABEL_PX 70

/* name if it fits; else the Mod's fitted label for it, written to out (size bytes) and
 * returned. 96/97's names all fit, so they are never changed. */
const uint8_t *CalLabel_Fit(const uint8_t *name, uint8_t *out, int32_t size);

#endif
