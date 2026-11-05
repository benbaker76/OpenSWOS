/*
 * team_packs.c -- teams a mod brings (include/team_packs.h). SDL builds.
 *
 * Every mod that is on is looked in for data_json/teampacks/*.json once, at the first question;
 * each file's number is read from its header. A number outside TEAMPACK_FIRST..TEAMPACK_LAST, or
 * one a pack earlier in the mods' order took, is refused and logged.
 */
#include "team_packs.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"
#include "mods.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_file[TEAMPACK_LAST - TEAMPACK_FIRST + 1][96];
static int  s_scanned;

static void scan(void)
{
    s_scanned = 1;
    for (int32_t m = 0; m < Mods_Count(); m++) {
        char dir[1200];
        snprintf(dir, sizeof dir, "%s/data_json/teampacks", Mods_Dir(m));
        DIR *d = opendir(dir);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const size_t l = strlen(e->d_name);
            if (l <= 5 || strcmp(e->d_name + l - 5, ".json") != 0 || l > 60)
                continue;
            char path[1300];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            FILE *f = fopen(path, "rb");
            if (!f)
                continue;
            char head[4096];
            const int n = (int)fread(head, 1, sizeof head - 1, f);
            fclose(f);
            head[n > 0 ? n : 0] = '\0';
            /* (the header, before the teams: "type", "country") */
            char type[16] = "";
            double country = -1;
            const char *teams = strstr(head, "\"teams\"");
            const int hlen = teams ? (int)(teams - head) : n;
            char probe[4096];
            snprintf(probe, sizeof probe, "%.*s\"end\": 0}", hlen, head);
            mjson_get_string(probe, (int)strlen(probe), "$.type", type, (int)sizeof type);
            mjson_get_number(probe, (int)strlen(probe), "$.country", &country);
            const int c = (int)country;
            if (strcmp(type, "team") != 0 || c < TEAMPACK_FIRST || c > TEAMPACK_LAST) {
                fprintf(stderr, "[team-packs] %s (%s): not a team pack -- \"type\": \"team\" and a country "
                                "number %d..%d\n", e->d_name, Mods_Id(m), TEAMPACK_FIRST, TEAMPACK_LAST);
                continue;
            }
            char *slot = s_file[c - TEAMPACK_FIRST];
            if (slot[0]) {
                fprintf(stderr, "[team-packs] %s (%s): number %d is %s's already -- left out\n", e->d_name,
                        Mods_Id(m), c, slot);
                continue;
            }
            snprintf(slot, sizeof s_file[0], "data_json/teampacks/%s", e->d_name);
            fprintf(stderr, "[team-packs] %d: %s (%s)\n", c, e->d_name, Mods_Id(m));
        }
        closedir(d);
    }
}

const char *TeamPacks_File(int country)
{
    if (!s_scanned)
        scan();
    if (country < TEAMPACK_FIRST || country > TEAMPACK_LAST)
        return NULL;
    return s_file[country - TEAMPACK_FIRST][0] ? s_file[country - TEAMPACK_FIRST] : NULL;
}

#endif /* SWOS_SDL_BUILD */
