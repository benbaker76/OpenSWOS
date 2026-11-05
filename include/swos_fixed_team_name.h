/* Fixed-width SWOS club/team names are 17 bytes and may have no NUL (a 17-letter name, which a
 * mod's team pack can bring): StringCopy ran on into the next field. Always terminate display
 * strings without touching the game record. No libc: the Amiga has none. (benefactorr's Career+,
 * docs/mods/CAREER_PLUS.md §3.1.) */
#ifndef SWOS_FIXED_TEAM_NAME_H
#define SWOS_FIXED_TEAM_NAME_H
#include "swos_types.h"   /* (the fixed-width types, on the Amiga too: no stdint.h there) */
#define SWOS_TEAM_NAME_BYTES 17u
static inline void SWOS_CopyTeamName18(void *destination, const void *source)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    unsigned i = 0;
    if (src) {
        for (; i < SWOS_TEAM_NAME_BYTES && src[i] != 0; ++i)
            dst[i] = (src[i] < 0x20u) ? (uint8_t)' ' : src[i];
    }
    dst[i] = 0;
}
/* For game structures whose destination is exactly 17 bytes long.
 * Never place a terminator in the neighbouring stats byte. */
static inline void SWOS_CopyTeamName17(void *destination, const void *source)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    unsigned i = 0;
    if (src) {
        for (; i < SWOS_TEAM_NAME_BYTES && src[i] != 0; ++i)
            dst[i] = src[i];
    }
    for (; i < SWOS_TEAM_NAME_BYTES; ++i)
        dst[i] = 0;
}
/* StringCopy's contract (src, dst; returns the NUL written) for a 17-byte name: at most 17. */
static inline uint8_t *SWOS_TeamNameCopy(const void *source, uint8_t *dst)
{
    const uint8_t *src = (const uint8_t *)source;
    unsigned i = 0;
    for (; src && i < SWOS_TEAM_NAME_BYTES && src[i] != 0; ++i)
        dst[i] = src[i];
    dst[i] = 0;
    return dst + i;
}
#endif
