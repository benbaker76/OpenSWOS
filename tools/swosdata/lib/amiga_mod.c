/*
 * amiga_mod.c -- a mod made ready for the Amiga (SwosData_AmigaMod, swosdata.h).
 *
 * The Amiga parses no JSON (docs/DATA_BSON.md): what a mod brings as JSON for the PC, the Amiga
 * reads as binary written here, when the setup puts the mod in an Amiga drawer:
 *   data_json/challenges/<name>.json -> challenges/<NAME>.BIN         (include/challenge_pack.h)
 *   classic/<group>/index.json       -> classic/<group>/INDEX.BIN     (include/classics_index.h)
 * The JSON readers are the game's own (sd_json.c), so the two cannot part. A mod's menus are not
 * here: the Amiga program has them baked in (include/menu_baked.h).
 */
#include "swosdata.h"

#include "../../../third_party/swosgfx/lib/sg_dir.h"
#include "../../../third_party/swosgfx/lib/sg_util.h"
#include "../../../include/challenge_pack.h"
#include "../../../include/classics_index.h"
#include "../../../include/mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t ChallengePack_FromJson(const char *json, int len, ChallengeDef *defs, int32_t max);

enum { kMaxChallenges = 256 };

static void say(void (*log)(void *, const char *), void *user, const char *line)
{
    if (log)
        log(user, line);
}

static int32_t challenges(const char *modDir, int32_t removeJson, void (*log)(void *, const char *), void *user,
                          char *err)
{
    char dir[1100], outDir[1100], line[2200];
    Sg_PathJoin(dir, sizeof dir, modDir, "data_json/challenges");
    Sg_PathJoin(outDir, sizeof outDir, modDir, "challenges");
    char **names = NULL;
    int32_t count = 0;
    if (!Sg_IsDir(dir) || Sg_ListDir(dir, &names, &count) != 0)
        return 0;
    int32_t rc = 0;
    for (int32_t i = 0; i < count && rc == 0; i++) {
        if (!Sg_EndsWithNoCase(names[i], ".json"))
            continue;
        char path[1400], out[1700], stem[256];
        Sg_PathJoin(path, sizeof path, dir, names[i]);
        snprintf(stem, sizeof stem, "%s", names[i]);
        stem[strlen(stem) - 5] = 0;
        for (char *c = stem; *c; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 32);
        snprintf(out, sizeof out, "%s/%s.BIN", outDir, stem);
        SgBuf b = { 0 };
        if (Sg_ReadFile(path, &b, err) != 0) {
            rc = -1;
            break;
        }
        static ChallengeDef defs[kMaxChallenges];
        const int32_t n = ChallengePack_FromJson((const char *)b.data, (int)b.size, defs, kMaxChallenges);
        Sg_BufFree(&b);
        if (n < 0) {
            snprintf(line, sizeof line, "Amiga: %s is not a challenges file -- left out", path);
            say(log, user, line);
            continue;
        }
        const uint32_t cap = CHALLENGE_PACK_HEADER + (uint32_t)n * CHALLENGE_PACK_RECORD;
        uint8_t *bin = (uint8_t *)malloc(cap);
        const int32_t size = bin ? ChallengePack_Write(defs, n, bin, cap) : -1;
        if (size < 0 || Sg_WriteFile(out, bin, (uint32_t)size, err) != 0) {
            if (size < 0)
                snprintf(err, 256, "could not write %s", out);
            free(bin);
            rc = -1;
            break;
        }
        free(bin);
        if (removeJson)
            remove(path);
        snprintf(line, sizeof line, "Amiga: %s -> challenges/%s.BIN (%d challenges)", names[i], stem, (int)n);
        say(log, user, line);
    }
    Sg_ListDirFree(names, count);
    return rc;
}

static int32_t classics_index(const char *modDir, const char *group, int32_t removeJson,
                              void (*log)(void *, const char *), void *user, char *err)
{
    char dir[1100], path[1200], out[1200], line[1400];
    snprintf(dir, sizeof dir, "%s/classic/%s", modDir, group);
    Sg_PathJoin(path, sizeof path, dir, "index.json");
    Sg_PathJoin(out, sizeof out, dir, CLASSICS_INDEX_FILE);
    if (!Sg_IsFile(path))
        return 0;
    SgBuf b = { 0 };
    if (Sg_ReadFile(path, &b, err) != 0)
        return -1;
    const char *json = (const char *)b.data;
    const int len = (int)b.size;
    uint32_t count = 0, cap = CLASSICS_INDEX_HEADER + 64u * CLASSICS_INDEX_RECORD;
    uint8_t *bin = (uint8_t *)calloc(1, cap);
    if (!bin) {
        Sg_BufFree(&b);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    for (int32_t i = 0;; i++) {
        char q[48], file[CLASSICS_INDEX_FILELEN], name[CLASSICS_INDEX_NAMELEN];
        snprintf(q, sizeof q, "$.competitions[%d].file", (int)i);
        if (mjson_get_string(json, len, q, file, (int)sizeof file) <= 0)
            break;
        snprintf(q, sizeof q, "$.competitions[%d].name", (int)i);
        if (mjson_get_string(json, len, q, name, (int)sizeof name) <= 0)
            continue;
        if (CLASSICS_INDEX_HEADER + (count + 1) * CLASSICS_INDEX_RECORD > cap) {
            uint8_t *more = (uint8_t *)realloc(bin, cap * 2);
            if (!more)
                break;
            memset(more + cap, 0, cap);
            bin = more;
            cap *= 2;
        }
        uint8_t *r = bin + CLASSICS_INDEX_HEADER + count * CLASSICS_INDEX_RECORD;
        memcpy(r, file, strlen(file));                              /* (zero-padded: calloc) */
        memcpy(r + CLASSICS_INDEX_FILELEN, name, strlen(name));
        count++;
    }
    Sg_BufFree(&b);
    memcpy(bin, CLASSICS_INDEX_MAGIC, 7);
    bin[7] = CLASSICS_INDEX_VERSION;
    bin[8] = (uint8_t)(count >> 8);
    bin[9] = (uint8_t)count;
    const int32_t rc = Sg_WriteFile(out, bin, CLASSICS_INDEX_HEADER + count * CLASSICS_INDEX_RECORD, err);
    free(bin);
    if (rc != 0)
        return -1;
    if (removeJson)
        remove(path);
    snprintf(line, sizeof line, "Amiga: classic/%s/index.json -> %s (%u competitions)", group, CLASSICS_INDEX_FILE,
             (unsigned)count);
    say(log, user, line);
    return 0;
}

int32_t SwosData_AmigaMod(const char *modDir, int32_t removeJson, void (*log)(void *user, const char *line),
                          void *user, char *err)
{
    static const char *const kGroups[] = { "uefa", "worldcup", "club", "continental" };   /* (mod_classics.c) */
    if (challenges(modDir, removeJson, log, user, err) != 0)
        return -1;
    for (uint32_t g = 0; g < sizeof kGroups / sizeof kGroups[0]; g++)
        if (classics_index(modDir, kGroups[g], removeJson, log, user, err) != 0)
            return -1;
    return 0;
}
