/*
 * endian_helpers.h -- Explicit big-endian / host conversion macros.
 *
 * SWOS data files (team files, RNC headers, save games, audio modules)
 * were authored on the Amiga m68k, which is big-endian. On the Amiga
 * build the host IS big-endian so every macro below compiles to a
 * no-op; on x86 / x86_64 the LE host needs explicit byte swaps.
 *
 * Using these names (rather than raw `__builtin_bswap*` calls or
 * inline byte arithmetic) makes it obvious to the reader — and to
 * anyone back-porting SDL fixes to the Amiga tree — which bytes are
 * coming off disk / struct memory in BE order and which are already
 * in host order.
 *
 * Naming:
 *   BE<N>_TO_HOST(x)   -- value read from BE storage, convert to host
 *   HOST_TO_BE<N>(x)   -- host value, convert to BE for storage
 *   BE_READ_U<N>(p)    -- read N bits from byte pointer p (unaligned OK)
 *   BE_WRITE_U<N>(p,v) -- write N bits to byte pointer p (unaligned OK)
 *   PACK_BE_U16(hi,lo) -- combine two single bytes the way an m68k
 *                        `move.w` would see them. Endian-agnostic; use
 *                        this whenever the source m68k code did
 *                        `*(uint16_t *)&struct.byteField` and the
 *                        following byte was a separate logical field
 *                        (e.g. countryNumber + teamOrdinal).
 */

#ifndef SWOS_ENDIAN_HELPERS_H
#define SWOS_ENDIAN_HELPERS_H

#include "swos_types.h"

#if defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    defined(__m68k__) || defined(__AMIGA__)
#  define SWOS_HOST_IS_BIG_ENDIAN 1
#else
#  define SWOS_HOST_IS_BIG_ENDIAN 0
#endif

#if SWOS_HOST_IS_BIG_ENDIAN
#  define BE16_TO_HOST(x) ((uint16_t)(x))
#  define BE32_TO_HOST(x) ((uint32_t)(x))
#else
#  define BE16_TO_HOST(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#  define BE32_TO_HOST(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))
#endif

#define HOST_TO_BE16(x) BE16_TO_HOST(x)
#define HOST_TO_BE32(x) BE32_TO_HOST(x)

/* Unaligned byte-pointer reads/writes. Always compile to exactly the
 * right thing on both endians — no casts, no alignment assumptions. */
static inline uint16_t BE_READ_U16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}
static inline uint32_t BE_READ_U32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
         | ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}
static inline void BE_WRITE_U16(void *p, uint16_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
}
static inline void BE_WRITE_U32(void *p, uint32_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >>  8);
    b[3] = (uint8_t)v;
}

/* Little-endian reads, for files the PC (DOS) game wrote -- its career saves (src/car_rebuild.c). */
static inline uint16_t LE_READ_U16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)((uint16_t)b[1] << 8 | b[0]);
}
static inline uint32_t LE_READ_U32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[1] <<  8) |  (uint32_t)b[0];
}

/* Build a uint16 from two adjacent single-byte fields the way an m68k
 * `move.w` (big-endian native) would read them. Use this wherever the
 * original asm/C did `*(uint16_t *)&struct.firstByte` and the following
 * byte is a *separate* logical field that the struct still names.
 *
 * Example — team file records pack (countryNumber, teamOrdinal) as
 * bytes 0 and 1 and then use the combined uint16 as a team id:
 *     id = PACK_BE_U16(team.countryNumber, team.teamOrdinal);
 * NOT:
 *     id = *(uint16_t *)&team.countryNumber;   // byte-swapped on x86 */
#define PACK_BE_U16(hi, lo) (((uint16_t)(uint8_t)(hi) << 8) | (uint8_t)(lo))

#endif /* SWOS_ENDIAN_HELPERS_H */
