/* team_uuid.h -- SHA-1, uuid5 and the derived ids of the 96/97 database (src/team_uuid.c,
 * docs/TEAM_IDS.md). Both targets. */
#ifndef TEAM_UUID_H
#define TEAM_UUID_H

#include "swos_types.h"

#define TEAM_UUID_TEXT      37      /* 8-4-4-4-12 and its NUL */
#define TEAM_CHECKSUM_TEXT  17      /* 16 hex digits and its NUL */

/* SHA-1 (FIPS 180-1). Not a security boundary: an id scheme and a fingerprint. */
typedef struct TeamSha1 {
    uint32_t h[5];
    uint8_t  block[64];
    uint32_t used;
    uint64_t bits;
} TeamSha1;
void TeamSha1_Start(TeamSha1 *s);
void TeamSha1_Add(TeamSha1 *s, const uint8_t *data, uint32_t count);
void TeamSha1_End(TeamSha1 *s, uint8_t out[20]);
void TeamUuid_Hex(const uint8_t *bytes, int32_t count, char *out);   /* count bytes -> 2*count digits + NUL */

/* The ids of the original 96/97 database, which are derived rather than stored:
 * uuid5 of "96-97/team/<country>/<ordinal>" and ".../player/<index>" in the project's
 * namespace (scripts/assign_team_uuids.py computes the same). */
void TeamDb_DerivedTeamUuid(int32_t country, int32_t ordinal, char out[TEAM_UUID_TEXT]);
void TeamDb_DerivedPlayerUuid(int32_t country, int32_t ordinal, int32_t index, char out[TEAM_UUID_TEXT]);

#endif
