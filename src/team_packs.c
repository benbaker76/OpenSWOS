/*
 * team_packs.c -- teams a mod brings (include/team_packs.h). SDL builds.
 *
 * Every mod that is on is looked in for data_json/teampacks/<name>.json once, at the first question;
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

enum { kSlots = TEAMPACK_LAST - TEAMPACK_FIRST + 1 };
static char    s_file[kSlots][96];
static char    s_name[kSlots][32], s_desc[kSlots][64];
static int32_t s_teams[kSlots];
static int     s_scanned;
static int32_t s_chosen;

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
            double country = -1, count = 0;
            const char *teams = strstr(head, "\"teams\"");
            const int hlen = teams ? (int)(teams - head) : n;
            char probe[4096];
            snprintf(probe, sizeof probe, "%.*s\"end\": 0}", hlen, head);
            mjson_get_string(probe, (int)strlen(probe), "$.type", type, (int)sizeof type);
            mjson_get_number(probe, (int)strlen(probe), "$.country", &country);
            mjson_get_number(probe, (int)strlen(probe), "$.count", &count);
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
            {
                const int32_t k = c - TEAMPACK_FIRST;
                if (mjson_get_string(probe, (int)strlen(probe), "$.country_name", s_name[k], (int)sizeof s_name[k]) < 0)
                    snprintf(s_name[k], sizeof s_name[k], "TEAM PACK %d", c);
                mjson_get_string(probe, (int)strlen(probe), "$.description", s_desc[k], (int)sizeof s_desc[k]);
                for (char *q = s_name[k]; *q; q++)
                    if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
                for (char *q = s_desc[k]; *q; q++)
                    if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
                s_teams[k] = (int32_t)count;
            }
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

int32_t TeamPacks_Count(void)
{
    int32_t n = 0;
    if (!s_scanned)
        scan();
    for (int32_t k = 0; k < kSlots; k++)
        n += s_file[k][0] != 0;
    return n;
}

int32_t TeamPacks_Info(int32_t i, int32_t *number, const char **name, const char **description, int32_t *teams)
{
    if (!s_scanned)
        scan();
    for (int32_t k = 0; k < kSlots; k++) {
        if (!s_file[k][0] || i--)
            continue;
        if (number) *number = TEAMPACK_FIRST + k;
        if (name) *name = s_name[k];
        if (description) *description = s_desc[k];
        if (teams) *teams = s_teams[k];
        return 0;
    }
    return -1;
}

void TeamPacks_SetChosen(int32_t number) { s_chosen = number; }
int32_t TeamPacks_Chosen(void) { return s_chosen; }

#include "game_types.h"

extern void *_countriesTable[];
extern void *_competitionsTable[];
extern CompetitionLeague _customsLeague;

void TeamPacks_Install(void)
{
    if (!s_scanned)
        scan();
    for (int32_t k = 0; k < kSlots; k++) {
        const int32_t number = TEAMPACK_FIRST + k;
        if (!s_file[k][0])
            continue;
        if (_countriesTable[number] || _competitionsTable[number]) {
            fprintf(stderr, "[team-packs] %d: the game has that number already -- not offered\n", (int)number);
            continue;
        }
        /* its name, length-prefixed as the table's are */
        const size_t len = strlen(s_name[k]);
        uint8_t *name = (uint8_t *)calloc(len + 2, 1);
        /* the customs league, its team file the pack's, one division of its teams */
        CompetitionLeague *league = (CompetitionLeague *)malloc(sizeof *league);
        QualifyingGroup *table = (QualifyingGroup *)calloc(1, sizeof *table);
        if (!name || !league || !table) {
            free(name);
            free(league);
            free(table);
            continue;
        }
        name[0] = (uint8_t)len;
        memcpy(name + 1, s_name[k], len);
        memcpy(league, &_customsLeague, sizeof *league);
        ((uint8_t *)league)[2] = (uint8_t)number;                                       /* its team file */
        ((uint8_t *)league)[9] = 1;                                                     /* divisions */
        ((uint8_t *)league)[0xD] = (uint8_t)(s_teams[k] > 0 && s_teams[k] < 48 ? s_teams[k] : 48);  /* its teams */
        table->compCup[0] = (Competition *)league;
        table->compCup[1] = (Competition *)(intptr_t)-2;
        table->compCup[2] = (Competition *)(intptr_t)-1;
        _countriesTable[number] = name;
        _competitionsTable[number] = table;
    }
}

#endif /* SWOS_SDL_BUILD */
