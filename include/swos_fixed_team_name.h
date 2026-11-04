/* Fixed-width SWOS club/team names are 17 bytes and may have no NUL.
 * Always terminate display strings without touching the game record.
 * Keep this helper free of libc dependencies for the original builds. */
#ifndef SWOS_FIXED_TEAM_NAME_H
#define SWOS_FIXED_TEAM_NAME_H
#include <stdint.h>
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
#endif
