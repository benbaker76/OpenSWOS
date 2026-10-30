/*
 * swosdata -- build OpenSWOS's game data from the player's own copy of the game,
 * from the command line. The setup app does the same through the library.
 *
 *   swosdata [--scan DIR]... [--adf FILE]... [--dos DIR] [--jag FILE] [--swos2020 DIR]
 *            [--out DIR [--res DIR]]    (no --out: only report what was found)
 *   --scan looks through DIR and every folder inside it
 *   swosdata --compare OUT REF     every file in OUT equals REF's (RNC unpacked)
 *   swosdata --installed DIR       what an earlier run left in the assets folder DIR
 *   --res DIR: the setup's own files (setup/, data/credits_icons.bmp); found
 *   without it in the current folder, else beside this program or above it.
 */
#define _POSIX_C_SOURCE 200809L     /* readlink */
#define _DARWIN_C_SOURCE

#include "swosdata.h"
#include "sg_dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static void list_part_file(void *user, SdStep step, const char *rel, int32_t present)
{
    (void)user;
    if (present)
        printf("%s\t%d\t%s\n", SwosData_PartKey(step), (int)SwosData_PartVersion(step), rel);
}

static void on_log(void *user, const char *line)
{
    (void)user;
    printf("%s\n", line);
}

/* --scan's progress: the count of folders looked through, every 500 to stderr. */
static int32_t s_scanned;
static int32_t on_scan(void *user, int32_t folders, const char *dir)
{
    (void)user;
    s_scanned++;
    if (folders % 500 == 0)
        fprintf(stderr, "  ... %d folders (%s)\n", (int)folders, dir);
    return 0;
}

static int32_t usage(void)
{
    fprintf(stderr,
            "usage: swosdata [--scan DIR]... [--adf FILE]... [--dos DIR] [--jag FILE]\n"
            "                [--swos2020 DIR]\n"
            "                [--out DIR [--res DIR]]   (no --out: report what was found;\n"
            "                                           no sources: the last install's, from --out's ASSETS.ini)\n"
            "       swosdata --compare OUT REF\n"
            "       swosdata --whdload OUT.lha --drawer SWOSDRAWER --assets ASSETSDIR [--res DIR]\n"
            "       swosdata --world WORLDDIR --tag TAG --assets ASSETSDIR [--drawer SWOSDRAWER]\n"
            "       swosdata --installed ASSETSDIR\n"
            "       swosdata --list-parts ASSETSDIR   (part, data version, file: check_data_versions.py)\n");
    return 2;
}

#ifndef _WIN32
/* Compare every file under `out` with the same path under `ref`, both unpacked. */
static int32_t compare_tree(const char *out, const char *ref, const char *rel, int32_t *same, int32_t *diff)
{
    char dir[1100];
    Sg_PathJoin(dir, sizeof dir, out, rel);
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char sub[1100], po[1100], pr[1100];
        Sg_PathJoin(sub, sizeof sub, rel, e->d_name);
        Sg_PathJoin(po, sizeof po, out, sub);
        struct stat st;
        if (stat(po, &st) == 0 && S_ISDIR(st.st_mode)) {
            compare_tree(out, ref, sub, same, diff);
            continue;
        }
        Sg_PathJoin(pr, sizeof pr, ref, sub);
        char err[256];
        SgBuf a, b, ua, ub;
        if (Sg_ReadFile(po, &a, err) != 0 || Sg_ReadFile(pr, &b, err) != 0) {
            printf("MISSING %s\n", sub);
            (*diff)++;
            continue;
        }
        if (Sg_RncUnpack(&a, &ua, err) != 0 || Sg_RncUnpack(&b, &ub, err) != 0) {
            printf("BAD %s: %s\n", sub, err);
            (*diff)++;
        } else if (ua.size == ub.size && memcmp(ua.data, ub.data, ua.size) == 0) {
            (*same)++;
        } else {
            printf("DIFFERS %s (%u vs %u bytes unpacked)\n", sub, ua.size, ub.size);
            (*diff)++;
        }
        Sg_BufFree(&a);
        Sg_BufFree(&b);
        Sg_BufFree(&ua);
        Sg_BufFree(&ub);
    }
    closedir(d);
    return 0;
}
#endif

/* This program's own path, or argv[0] when the system will not say. */
static void exe_path(const char *argv0, char *out, uint32_t cap)
{
    snprintf(out, cap, "%s", argv0 ? argv0 : "");
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, cap);
    if (n == 0 || n >= cap)
        snprintf(out, cap, "%s", argv0 ? argv0 : "");
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0)
        snprintf(out, cap, "%s", buf);
#else
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        snprintf(out, cap, "%s", buf);
    }
#endif
}

/* Cut the last path component off `p`; 0 when there was none. */
static int32_t parent_dir(char *p)
{
    char *slash = strrchr(p, '/');
#ifdef _WIN32
    char *back = strrchr(p, '\\');
    if (back && (!slash || back > slash))
        slash = back;
#endif
    if (!slash || slash == p)
        return 0;
    *slash = 0;
    return 1;
}

/* The setup's own files: in the current folder as before, else in the folder
 * this program is in or one above it (OpenSWOS-Setup.zip keeps them above the
 * Windows/Linux/macOS folders), or that folder's bin/ (the repo). Left empty
 * -- relative to here -- when none has them, so the error names the file. */
static void find_res_dir(const char *argv0, char *out, uint32_t cap)
{
    char dir[1024], probe[1100];
    out[0] = 0;
    if (Sg_IsFile("setup/glyphs.txt"))
        return;
    exe_path(argv0, dir, sizeof dir);
    while (parent_dir(dir)) {
        Sg_PathJoin(probe, sizeof probe, dir, "setup/glyphs.txt");
        if (Sg_IsFile(probe)) {
            snprintf(out, cap, "%s", dir);
            return;
        }
        Sg_PathJoin(probe, sizeof probe, dir, "bin/setup/glyphs.txt");
        if (Sg_IsFile(probe)) {
            Sg_PathJoin(out, cap, dir, "bin");
            return;
        }
    }
}

int main(int argc, char **argv)
{
    static SdJob job;
    char err[256];

    if (argc == 3 && strcmp(argv[1], "--installed") == 0) {
        SdInstalled inst[SD_STEP_COUNT];
        SdStepInfo info[SD_STEP_COUNT];
        SdJob empty;
        memset(&empty, 0, sizeof empty);
        SwosData_Plan(&empty, info);
        SwosData_Installed(argv[2], inst);
        for (int32_t s = 0; s < SD_STEP_COUNT; s++) {
            const int32_t want = SwosData_PartVersion((SdStep)s);
            const char *state = inst[s].present == 0 ? ""
                              : inst[s].present < inst[s].total ? "  incomplete"
                              : inst[s].version < want ? "  update needed"
                              : "  installed";
            printf("%-36s %4d of %4d  data v%d (this setup: v%d)%s\n", info[s].name,
                   (int)inst[s].present, (int)inst[s].total, (int)inst[s].version, (int)want, state);
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--list-parts") == 0) {
        /* part key, data version, file: the files of each part that are there. */
        SwosData_ListFiles(argv[2], list_part_file, NULL);
        return 0;
    }
    /* --whdload OUT.lha --drawer DIR --assets DIR [--res DIR]: the WHDLoad package from
     * an assets folder made already and a SWOS drawer (SwosData_MakeWhdload). */
    if (argc >= 2 && strcmp(argv[1], "--whdload") == 0) {
        const char *out = argc > 2 ? argv[2] : NULL, *drawer = NULL, *assets = NULL;
        char res[1024] = "";
        for (int i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "--drawer") == 0) drawer = argv[i + 1];
            else if (strcmp(argv[i], "--assets") == 0) assets = argv[i + 1];
            else if (strcmp(argv[i], "--res") == 0) snprintf(res, sizeof res, "%s", argv[i + 1]);
            else return usage();
        }
        if (!out || !drawer || !assets)
            return usage();
        if (!res[0])
            find_res_dir(argv[0], res, sizeof res);
        if (SwosData_MakeWhdload(assets, drawer, res, out, on_log, NULL, err) != 0) {
            fprintf(stderr, "failed: %s\n", err);
            return 1;
        }
        return 0;
    }
    /* --world DIR --tag TAG --assets DIR [--drawer DIR]: a world's built files into the
     * assets folder (and a SWOS drawer) -- SwosData_InstallWorld. */
    if (argc >= 2 && strcmp(argv[1], "--world") == 0) {
        const char *world = argc > 2 ? argv[2] : NULL, *tag = NULL, *assets = NULL, *drawer = NULL;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "--tag") == 0) tag = argv[i + 1];
            else if (strcmp(argv[i], "--assets") == 0) assets = argv[i + 1];
            else if (strcmp(argv[i], "--drawer") == 0) drawer = argv[i + 1];
            else return usage();
        }
        if (!world || !tag || !assets)
            return usage();
        if (SwosData_InstallWorld(world, tag, assets, drawer, on_log, NULL, err) != 0) {
            fprintf(stderr, "failed: %s\n", err);
            return 1;
        }
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--compare") == 0) {
#ifdef _WIN32
        fprintf(stderr, "--compare is not built on Windows\n");
        return 2;
#else
        int32_t same = 0, diff = 0;
        compare_tree(argv[2], argv[3], "", &same, &diff);
        printf("%d identical, %d different\n", same, diff);
        return diff ? 1 : 0;
#endif
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v)
            return usage();
        if (strcmp(a, "--scan") == 0)
            SdSources_ScanEx(&job.src, v, on_scan, NULL);
        else if (strcmp(a, "--adf") == 0 || strcmp(a, "--jag") == 0) {
            if (SdSources_AddFile(&job.src, v) != 0) {
                fprintf(stderr, "not recognised: %s\n", v);
                return 1;
            }
        } else if (strcmp(a, "--dos") == 0) {
            if (SdSources_AddFile(&job.src, v) != 0 || !SdSources_HaveDos(&job.src)) {
                fprintf(stderr, "not a PC Sensible World of Soccer folder: %s\n", v);
                return 1;
            }
        } else if (strcmp(a, "--swos2020") == 0) {
            if (SdSources_AddFile(&job.src, v) != 0 || !SdSources_HaveSwos2020(&job.src)) {
                fprintf(stderr, "not a SWOS 2020 install: %s\n", v);
                return 1;
            }
        } else if (strcmp(a, "--out") == 0)
            snprintf(job.outDir, sizeof job.outDir, "%s", v);
        else if (strcmp(a, "--res") == 0)
            snprintf(job.resDir, sizeof job.resDir, "%s", v);
        else
            return usage();
        i++;
    }

    if (s_scanned)
        printf("Searched %d folders\n", (int)s_scanned);
    /* Nothing named on the command line: what the last install used (ASSETS.ini in --out). */
    if (job.outDir[0] && !job.src.adf1[0] && !job.src.adf2[0] && !job.src.hdf[0] && !job.src.lha[0] &&
        !job.src.dosDir[0] &&
        !job.src.jagRom[0] && !job.src.s2020Dir[0] && SwosData_LoadSources(job.outDir, &job.src) > 0)
        printf("Sources: from %s/%s, the last install's\n", job.outDir, "ASSETS.ini");
    printf("Amiga disk 1: %s\n", job.src.adf1[0] ? job.src.adf1 : "(not found)");
    printf("Amiga disk 2: %s\n", job.src.adf2[0] ? job.src.adf2 : "(not found)");
    printf("Amiga HDF:    %s\n", job.src.hdf[0] ? job.src.hdf : "(not found)");
    printf("Amiga .lha:   %s\n", job.src.lha[0] ? job.src.lha : "(not found)");
    if (SdSources_HaveAmiga(&job.src)) {
        const char *season = SdSources_AmigaSeason(&job.src);
        if (SdSources_AmigaProblem()[0])
            printf("Amiga note:   %s\n", SdSources_AmigaProblem());
        printf("Amiga season: %s%s%s\n", season ? season : "(cannot read the team data)",
               job.src.hdf[0] && !(job.src.adf1[0] && job.src.adf2[0]) && job.src.hdfDir[0] ? ", game in " : "",
               job.src.hdf[0] && !(job.src.adf1[0] && job.src.adf2[0]) ? job.src.hdfDir : "");
    }
    printf("PC game:      %s\n", job.src.dosDir[0] ? job.src.dosDir : "(not found)");
    printf("Jaguar:       %s\n", job.src.jagRom[0] ? job.src.jagRom : "(not found)");
    printf("SWOS 2020:    %s\n", job.src.s2020Dir[0] ? job.src.s2020Dir : "(not found)");
    if (!job.outDir[0])
        return 0;       /* no --out: only say what was found */
    if (!job.resDir[0])
        find_res_dir(argv[0], job.resDir, sizeof job.resDir);

    job.log = on_log;
    if (SwosData_Run(&job, err) != 0) {
        fprintf(stderr, "failed: %s\n", err);
        return 1;
    }
    return 0;
}
