/*
 * team_uuid.c -- SHA-1, uuid5 and the derived ids of the 96/97 database (include/team_uuid.h,
 * docs/TEAM_IDS.md). Both targets: the team database (src/team_db.c) is the SDL build's, and the
 * Amiga's mods check a team's number against its id with these (ModApi_TeamByUuidNumber).
 */
#include "team_uuid.h"

#include <stdio.h>
#include <string.h>

/* ---- SHA-1 (FIPS 180-1), for uuid5 and the checksum. Not a security boundary: an id
 * scheme and a fingerprint; what guards a match is the comparison, not the hash. ---- */


static uint32_t rol(uint32_t v, int32_t n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(TeamSha1 *s)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f, k, t;
    int32_t i;

    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)s->block[i * 4] << 24 | (uint32_t)s->block[i * 4 + 1] << 16 |
               (uint32_t)s->block[i * 4 + 2] << 8 | s->block[i * 4 + 3];
    for (; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

void TeamSha1_Start(TeamSha1 *s)
{
    s->h[0] = 0x67452301u; s->h[1] = 0xEFCDAB89u; s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u; s->h[4] = 0xC3D2E1F0u;
    s->used = 0;
    s->bits = 0;
}

void TeamSha1_Add(TeamSha1 *s, const uint8_t *data, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        s->block[s->used++] = data[i];
        if (s->used == 64) {
            sha1_block(s);
            s->used = 0;
        }
    }
    s->bits += (uint64_t)count * 8;
}

void TeamSha1_End(TeamSha1 *s, uint8_t out[20])
{
    uint64_t bits = s->bits;
    uint8_t pad = 0x80, zero = 0, length[8];
    int32_t i;

    TeamSha1_Add(s, &pad, 1);
    while (s->used != 56)
        TeamSha1_Add(s, &zero, 1);
    {   /* (as two words: a 64-bit shift by a variable is a libgcc call the Amiga has not got) */
        const uint32_t hi = (uint32_t)(bits >> 32), lo = (uint32_t)bits;
        for (i = 0; i < 4; i++) {
            length[i] = (uint8_t)(hi >> (24 - i * 8));
            length[4 + i] = (uint8_t)(lo >> (24 - i * 8));
        }
    }
    TeamSha1_Add(s, length, 8);
    for (i = 0; i < 20; i++)
        out[i] = (uint8_t)(s->h[i / 4] >> (24 - (i % 4) * 8));
}

void TeamUuid_Hex(const uint8_t *bytes, int32_t count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    int32_t i;
    for (i = 0; i < count; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[count * 2] = '\0';
}

/* ---- ids ---- */

/* uuid5(NAMESPACE_URL, "https://us.swos.eu/team-data") = af19cc5a-6529-537b-935b-
 * 407bd73b75fb. Never change it: every id of the original database follows from it. */
static const uint8_t k_namespace[16] = {
    0xaf, 0x19, 0xcc, 0x5a, 0x65, 0x29, 0x53, 0x7b, 0x93, 0x5b, 0x40, 0x7b, 0xd7, 0x3b, 0x75, 0xfb,
};

static void uuid5(const char *name, char out[TEAM_UUID_TEXT])
{
    TeamSha1 s;
    uint8_t digest[20];
    char text[33];

    TeamSha1_Start(&s);
    TeamSha1_Add(&s, k_namespace, sizeof k_namespace);
    TeamSha1_Add(&s, (const uint8_t *)name, (uint32_t)strlen(name));
    TeamSha1_End(&s, digest);
    digest[6] = (uint8_t)((digest[6] & 0x0F) | 0x50);       /* version 5 */
    digest[8] = (uint8_t)((digest[8] & 0x3F) | 0x80);       /* the RFC 4122 variant */
    TeamUuid_Hex(digest, 16, text);
    snprintf(out, TEAM_UUID_TEXT, "%.8s-%.4s-%.4s-%.4s-%.12s", text, text + 8, text + 12, text + 16, text + 20);
}

void TeamDb_DerivedTeamUuid(int32_t country, int32_t ordinal, char out[TEAM_UUID_TEXT])
{
    char name[48];
    snprintf(name, sizeof name, "96-97/team/%d/%d", (int)country, (int)ordinal);
    uuid5(name, out);
}

void TeamDb_DerivedPlayerUuid(int32_t country, int32_t ordinal, int32_t index, char out[TEAM_UUID_TEXT])
{
    char name[64];
    snprintf(name, sizeof name, "96-97/team/%d/%d/player/%d", (int)country, (int)ordinal, (int)index);
    uuid5(name, out);
}
