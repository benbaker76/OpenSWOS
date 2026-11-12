/*
 * mod_dosvideos.c -- the dosvideos mod, "PC cutscenes" (bin/mods/dosvideos; canonical, on by
 * default; docs/MODS.md §37, docs/DOS_VIDEO.md): the PC game's career cutscenes -- the dressing
 * room's six and the chairmen's scenes -- at the moments the PC game plays them. SDL only (the
 * Amiga never had them). Needs the PC game's cutscenes, which the setup copies from the player's
 * own copy (video_dos/, sound_dos/flc/, sound_dos/chairman/); without them nothing plays.
 *
 * WHEN (swos.asm, the PC game; our side tells the moments through ModDef careerEvent):
 *   WIN / LOSE    a league fixture of the career the player played or watched, won or lost by
 *                 three goals or more (cseg_88A12's end): the first time, then every third such
 *                 result -- dseg_180588, -1 at a new career (InitNewSeason), never saved: a counter
 *                 that counts down from 3 and plays at 0
 *   WCUP / LCUP   a cup's last match with the career team in it, won or lost (cseg_28E76's end)
 *   WCUP / PROM / RELE   the season over, the manager staying: a cup won, else promoted, else
 *                 relegated (PlayNewSeasonAnimations, from ContinueCareerNextSeason)
 *   CHn_nn / CHP_nn   a chairman's (the president's) memo (PlayChairmanScene, ShowChairmanMessage):
 *                 its scene, by the memo's id (dseg_17BB14); ids 24-26 only every fourth time
 *                 (dseg_17BB8E). Which of the three chairmen: chosen again at every welcome memo
 *                 (ShowGreetingMessage / ShowLastSeasonMessage: never the one before) -- the PC
 *                 game with its Rand2, here with a generator of the mod's own
 * Each clip's sound as the PC game's FadeAndPlay*Animation load it: WINMTCH, LOSEMAT, WINCUP,
 * LOSECP, PROMOTE, and LOSECP again for RELEGATED (its RELEGTE.RAW is never played). A chairman's
 * voice is sfx\english\EC<A..C | P><nn>.RAW -- only ENGLISH.EXE names one, so the voice is heard
 * with the game in English (or a language the PC game had no version of); in French, German and
 * Italian the scene is silent, as the PC game's was.
 *
 * HOW (PlayAnimationCommon / PlayAnimation): not while the sound is off or the intro skipped (the
 * PC game's g_soundOff, skipIntro), nor -- ours -- under the career autopilot, a rig, online, or a
 * run with no window (ModApi_CutsceneBlocked, the one rule). The picture fades out; the clip's
 * first frame is drawn and faded in, then its sound starts; a frame every (animationDelay + 1)
 * ticks of the PC's 70 Hz retrace -- 4/70 s, the president's 6/70 s; any key, mouse or pad button
 * or touch skips; after the last frame (not skipped) it waits for the sound. Then the picture
 * fades out and the menu fades in again.
 *
 * Display only: nothing here is career or match state. The counters and the chairman are the
 * mod's own, in memory: the PC game kept the chairman in its career file (chairmanIndex sits in
 * the saved region), the result counter not -- a loaded career starts with the first chairman.
 *
 * SWOS_DOSVIDEO_TEST=<clip>[,<clip>...] (win, lose, wcup, lcup, prom, rele, ch1_05, chp_03 ...)
 * plays those at the 30th main-menu frame, even with no window or the intro skipped, then quits
 * after 60 more menu frames with "DOSVIDEOS TEST PASS"; =rules runs the moments' rules on made-up
 * events (nothing played) and quits with PASS or FAIL; =career lets the career autopilot's moments
 * play (--career-golden: a test of the real moments, the career's own flow around them). --flic-selftest=<dir> decodes every .FLC
 * in a folder (SWOS_FLIC_DUMP=<out>: each file's frames, for scripts/flic_check.py).
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "flic.h"
#include "mod_api.h"
#include "swos_io.h"            /* swos_data_path */
#include "project_identity.h"   /* PROJECT_SETUP_NAME */

#define SLUG "dosvideos"

/* The memos' scene ids, by the memo's number (swos.asm dseg_17BB14: currentMessage is its index;
 * 1-26 the chairmen's CHn_nn, 27-38 the president's CHP_nn). 0: no scene. */
static const uint8_t k_memoScene[39] = {
    0, 4, 16, 17, 5, 6, 7, 2, 8, 9, 10, 1, 11, 18, 19, 20, 21, 22, 23, 3, 14, 12, 13, 15, 24, 25, 26,
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1
};
/* The welcome memos, when the PC game picks a chairman again: WELCOME_TO_OUR_CLUB,
 * LAST_SEASON_BEFORE_RETIREMENT (ShowGreetingMessage / ShowLastSeasonMessage), and the national
 * team's WELCOME_WE_LOOK_FORWARD, WELCOME_ENJOY_YOUR_LAST_SEASON. */
static int32_t welcome_memo(uint16_t m) { return m == 19 || m == 20 || m == 33 || m == 34; }

static uint16_t s_resultCount = 0xFFFF;     /* dseg_180588 */
static uint16_t s_sceneCount;               /* dseg_17BB8E */
static uint16_t s_chairman;                 /* chairmanIndex: 0..2 */
static uint32_t s_rng;
static int32_t  s_test;                     /* 1 clips, 2 rules, 3 career */
static int32_t  s_dry;                      /* (the rules test: decide, never play) */
static char     s_dryLog[256];
static int32_t  s_saidMissing;

static uint32_t own_rand(void)
{
    if (!s_rng)
        s_rng = (uint32_t)SDL_GetTicks() * 2654435761u | 1u;
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void *read_data(const char *rel, uint32_t *size)
{
    char path[1100];
    FILE *f = fopen(swos_data_path(rel, path, (uint32_t)sizeof path), "rb");
    void *buf = NULL;
    long n;
    *size = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n < 64L * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)n)) != NULL) {
        if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
            *size = (uint32_t)n;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    return buf;
}

/* The chairman's voice's folder: the PC game's English release has them all. */
static const char *voice_language(void)
{
    const char *l = ModApi_Language();
    if (!strncmp(l, "fr", 2)) return "french";
    if (!strncmp(l, "de", 2)) return "german";
    if (!strncmp(l, "it", 2)) return "italian";
    return "english";
}

/* Plays video_dos/<clip>.flc with sound_dos/<sound> (NULL or missing: silent), a frame every
 * `delay` + 1 ticks of 70 Hz. `why` for the log. */
static void play(const char *clip, const char *sound, int32_t delay, const char *why)
{
    char rel[96];
    if (s_dry) {
        snprintf(s_dryLog + strlen(s_dryLog), sizeof s_dryLog - strlen(s_dryLog), "%s%s", s_dryLog[0] ? " " : "", clip);
        return;
    }
    const char *blocked = ModApi_CutsceneBlocked(s_test == 1 ? 1 : s_test == 3 ? 2 : 0);
    if (blocked) {
        fprintf(stderr, "[dosvideos] %s: %s -- not played (%s)\n", clip, why, blocked);
        return;
    }
    snprintf(rel, sizeof rel, "video_dos/%s.flc", clip);
    uint32_t size = 0, slen = 0;
    uint8_t *file = (uint8_t *)read_data(rel, &size);
    if (!file) {
        if (!s_saidMissing++)
            fprintf(stderr, "[dosvideos] no %s (the PC game's cutscenes: " PROJECT_SETUP_NAME " copies them from "
                            "the PC game or its CD): nothing plays\n", rel);
        return;
    }
    Flic f;
    if (Flic_Open(&f, file, size) != 0 || f.frames == 0) {
        fprintf(stderr, "[dosvideos] %s is not an FLC the game can play\n", rel);
        free(file);
        return;
    }
    uint8_t *pixels = (uint8_t *)calloc((size_t)f.width * f.height, 1);
    uint8_t pal[768];
    memset(pal, 0, sizeof pal);
    uint8_t *pcm = NULL;
    if (sound) {
        char srel[96];
        snprintf(srel, sizeof srel, "sound_dos/%s", sound);
        pcm = (uint8_t *)read_data(srel, &slen);
    }
    fprintf(stderr, "[dosvideos] %s: %s (%u frames, %ux%u, %s%s)\n", clip, why, (unsigned)f.frames,
            (unsigned)f.width, (unsigned)f.height, sound ? sound : "no sound", sound && !pcm ? " missing" : "");
    if (!pixels || Flic_Next(&f, pixels, pal) != 1) {
        fprintf(stderr, "[dosvideos] %s: its first frame cannot be read\n", rel);
        free(pixels);
        free(pcm);
        free(file);
        return;
    }

    enum { kFadeMs = 450 };
    const uint32_t frameMs = (uint32_t)(delay + 1) * 1000u / 70u;
    int32_t skipped = 0, rc = 1, shown = 1;
    ModApi_CutsceneBegin();
    {   /* the first frame, faded in (FlipTitle, FadeIn), then its sound */
        const uint32_t t0 = ModApi_Ticks();
        for (;;) {
            const uint32_t t = ModApi_Ticks() - t0;
            const int32_t level = t >= kFadeMs ? 256 : (int32_t)(t * 256 / kFadeMs);
            if (ModApi_CutsceneFrame(pixels, f.width, f.height, pal, level)) { skipped = 1; break; }
            if (level >= 256)
                break;
        }
    }
    if (!skipped && pcm)
        ModApi_CutsceneSound(pcm, slen, 22050);
    {
        const uint32_t start = ModApi_Ticks();
        while (!skipped) {
            const uint32_t due = (uint32_t)shown * frameMs;
            if (ModApi_Ticks() - start >= due) {
                rc = Flic_Next(&f, pixels, pal);
                if (rc != 1)
                    break;
                shown++;
            }
            if (ModApi_CutsceneFrame(pixels, f.width, f.height, pal, 256))
                skipped = 1;
        }
    }
    if (rc < 0)
        fprintf(stderr, "[dosvideos] %s: frame %u is damaged (%d): stopped there\n", rel, (unsigned)shown, (int)rc);
    if (!skipped && pcm) {      /* (after the last frame: the sound to its end -- a press cuts it short) */
        const uint32_t until = ModApi_Ticks() + slen / 22 + 1000;
        while (ModApi_CutsceneSoundPlaying() && (int32_t)(until - ModApi_Ticks()) > 0)
            if (ModApi_CutsceneFrame(pixels, f.width, f.height, pal, 256))
                break;
    }
    {   /* fade out (FadeOutToBlack) */
        const uint32_t t0 = ModApi_Ticks();
        for (;;) {
            const uint32_t t = ModApi_Ticks() - t0;
            const int32_t level = t >= kFadeMs / 2 ? 0 : 256 - (int32_t)(t * 512 / kFadeMs);
            ModApi_CutsceneFrame(pixels, f.width, f.height, pal, level);
            if (level <= 0)
                break;
        }
    }
    ModApi_CutsceneSound(NULL, 0, 0);
    ModApi_CutsceneEnd();
    fprintf(stderr, "[dosvideos] %s: %s after %u of %u frames\n", clip, skipped ? "skipped" : "played",
            (unsigned)shown, (unsigned)f.frames);
    free(pixels);
    free(pcm);
    free(file);
}

static int32_t room_on(void) { return ModApi_SettingInt(SLUG, "room", 1); }
static int32_t chairman_on(void) { return ModApi_SettingInt(SLUG, "chairman", 1); }

static void career_event(const ModCareerEvent *e)
{
    char why[96];
    switch (e->kind) {
    case MODCAREER_MATCH: {
        const int32_t d = e->goalDiff;
        if (d > -3 && d < 3)
            return;
        /* (cseg_891B6: -1 plays and starts the count; else down one, playing at 0) */
        if (s_resultCount != 0xFFFF && --s_resultCount != 0) {
            fprintf(stderr, "[dosvideos] %s: a result by %d -- %u more such results to the next\n",
                    d > 0 ? "win" : "lose", (int)(d < 0 ? -d : d), (unsigned)s_resultCount);
            return;
        }
        s_resultCount = 3;
        if (!room_on())
            return;
        snprintf(why, sizeof why, "a league match %s by %d", d > 0 ? "won" : "lost", (int)(d < 0 ? -d : d));
        play(d > 0 ? "win" : "lose", d > 0 ? "flc/winmtch.raw" : "flc/losemat.raw", 3, why);
        return;
    }
    case MODCAREER_CUP_FINAL:
        if (!e->goalDiff || !room_on())
            return;
        snprintf(why, sizeof why, "a cup's last match %s", e->goalDiff > 0 ? "won" : "lost");
        play(e->goalDiff > 0 ? "wcup" : "lcup", e->goalDiff > 0 ? "flc/wincup.raw" : "flc/losecp.raw", 3, why);
        return;
    case MODCAREER_NEW_SEASON:
        if (!room_on())
            return;
        if (e->flags & MODCAREER_WON_CUP)
            play("wcup", "flc/wincup.raw", 3, "the season over: a cup won");
        else if (e->flags & MODCAREER_PROMOTED)
            play("prom", "flc/promote.raw", 3, "the season over: promoted");
        else if (e->flags & MODCAREER_RELEGATED)
            play("rele", "flc/losecp.raw", 3, "the season over: relegated");   /* (LOSECP: the PC game's) */
        return;
    case MODCAREER_CHAIRMAN: {
        if (welcome_memo(e->message)) {     /* (ShowGreetingMessage: (Rand2 & 7) / 3, never the same) */
            uint16_t c;
            do
                c = (uint16_t)((own_rand() & 7) / 3);
            while (c == s_chairman);
            s_chairman = c;
        }
        if (!chairman_on() || e->message >= sizeof k_memoScene)
            return;
        {   /* (PlayChairmanScene: with the sound off, not even the count below) */
            const char *blocked = s_dry ? NULL : ModApi_CutsceneBlocked(1);
            if (blocked && !strcmp(blocked, "no sound"))
                return;
        }
        const uint8_t id = k_memoScene[e->message];
        if (!id)
            return;
        if (id >= 24) {                     /* (only every fourth time) */
            if (++s_sceneCount != 4) {
                fprintf(stderr, "[dosvideos] chairman's scene %u: the %u%s time -- every fourth plays\n",
                        (unsigned)id, (unsigned)s_sceneCount, s_sceneCount == 1 ? "st" : s_sceneCount == 2 ? "nd" : "rd");
                return;
            }
            s_sceneCount = 0;
        }
        char clip[16], sound[64];
        if (e->national) {
            snprintf(clip, sizeof clip, "chp_%02u", (unsigned)id);
            snprintf(sound, sizeof sound, "chairman/%s/ecp%02u.raw", voice_language(), (unsigned)id);
        } else {
            snprintf(clip, sizeof clip, "ch%u_%02u", (unsigned)s_chairman + 1, (unsigned)id);
            snprintf(sound, sizeof sound, "chairman/%s/ec%c%02u.raw", voice_language(), 'a' + s_chairman, (unsigned)id);
        }
        snprintf(why, sizeof why, "memo %u from the %s", (unsigned)e->message, e->national ? "president" : "chairman");
        play(clip, sound, e->national ? 5 : 3, why);
        return;
    }
    default:
        return;
    }
}

static void career_new(void)
{
    s_resultCount = 0xFFFF;                 /* (InitNewSeason) */
}

/* ---- the tests ---- */

/* SWOS_DOSVIDEO_TEST=rules: the moments' rules on made-up events, each checked against what the
 * PC game does. Returns the failures. */
static int32_t rules_test(void)
{
    struct { ModCareerEvent e; const char *want; const char *what; } k[] = {
        { { MODCAREER_MATCH, 0, 2, 0, 0 }, "", "won by 2: nothing, no count" },
        { { MODCAREER_MATCH, 0, 3, 0, 0 }, "win", "won by 3: the first plays" },
        { { MODCAREER_MATCH, 0, -4, 0, 0 }, "", "lost by 4: count 2" },
        { { MODCAREER_MATCH, 0, 5, 0, 0 }, "", "won by 5: count 1" },
        { { MODCAREER_MATCH, 0, -3, 0, 0 }, "lose", "lost by 3: the third after plays" },
        { { MODCAREER_MATCH, 0, 0, 0, 0 }, "", "a draw: nothing" },
        { { MODCAREER_CUP_FINAL, 0, 1, 0, 0 }, "wcup", "a cup final won" },
        { { MODCAREER_CUP_FINAL, 0, -2, 0, 0 }, "lcup", "a cup final lost" },
        { { MODCAREER_CUP_FINAL, 0, 0, 0, 0 }, "", "a cup final drawn (penalties): nothing" },
        { { MODCAREER_NEW_SEASON, 0, 0, 0, MODCAREER_WON_CUP | MODCAREER_PROMOTED }, "wcup", "cup and promotion: the cup's" },
        { { MODCAREER_NEW_SEASON, 0, 0, 0, MODCAREER_PROMOTED }, "prom", "promoted" },
        { { MODCAREER_NEW_SEASON, 0, 0, 0, MODCAREER_RELEGATED }, "rele", "relegated" },
        { { MODCAREER_NEW_SEASON, 0, 0, 0, 0 }, "", "a season with nothing: nothing" },
        { { MODCAREER_CHAIRMAN, 0, 0, 8, 0 }, "ch?_08", "memo 8 (YOURE_SACKED): scene 8" },
        { { MODCAREER_CHAIRMAN, 0, 0, 24, 0 }, "", "memo 24, scene 24: the 1st" },
        { { MODCAREER_CHAIRMAN, 0, 0, 25, 0 }, "", "memo 25, scene 25: the 2nd" },
        { { MODCAREER_CHAIRMAN, 0, 0, 26, 0 }, "", "memo 26, scene 26: the 3rd" },
        { { MODCAREER_CHAIRMAN, 0, 0, 24, 0 }, "ch?_24", "memo 24 again: the 4th plays" },
        { { MODCAREER_CHAIRMAN, 1, 0, 33, 0 }, "chp_08", "the president's memo 33: CHP_08" },
        { { MODCAREER_CHAIRMAN, 0, 0, 0, 0 }, "", "memo 0: no scene" },
    };
    int32_t bad = 0;
    s_dry = 1;
    s_resultCount = 0xFFFF;
    s_sceneCount = 0;
    for (uint32_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        s_dryLog[0] = 0;
        career_event(&k[i].e);
        int32_t ok = strlen(s_dryLog) == strlen(k[i].want);
        for (uint32_t c = 0; ok && k[i].want[c]; c++)
            ok = k[i].want[c] == '?' ? (s_dryLog[c] >= '1' && s_dryLog[c] <= '3') : s_dryLog[c] == k[i].want[c];
        fprintf(stderr, "[dosvideos] rules: %-48s -> %-8s %s\n", k[i].what, s_dryLog[0] ? s_dryLog : "-", ok ? "ok" : "WRONG");
        bad += !ok;
    }
    {   /* the welcome memo: a new chairman every time, never the one before, all three in time */
        int32_t seen[3] = { 0, 0, 0 }, same = 0;
        for (int32_t i = 0; i < 300; i++) {
            const uint16_t before = s_chairman;
            const ModCareerEvent w = { MODCAREER_CHAIRMAN, 0, 0, 19, 0 };
            s_dryLog[0] = 0;
            career_event(&w);
            same += s_chairman == before;
            seen[s_chairman < 3 ? s_chairman : 0]++;
        }
        const int32_t ok = !same && seen[0] && seen[1] && seen[2];
        fprintf(stderr, "[dosvideos] rules: 300 welcome memos: chairmen 1/2/3 %d/%d/%d, the same twice %d -- %s\n",
                (int)seen[0], (int)seen[1], (int)seen[2], (int)same, ok ? "ok" : "WRONG");
        bad += !ok;
    }
    s_dry = 0;
    s_resultCount = 0xFFFF;
    s_sceneCount = 0;
    return bad;
}

static void test_menu(void)
{
    static int32_t frames, done;
    if (!s_test || s_test == 3 || !ModApi_IsMainMenu())
        return;
    frames++;
    if (s_test == 2) {
        const int32_t bad = rules_test();
        fprintf(stdout, "DOSVIDEOS RULES %s (%d wrong)\n", bad ? "FAIL" : "PASS", (int)bad);
        fflush(stdout);
        exit(bad ? 1 : 0);
    }
    if (frames == 30 && !done) {
        char list[256];
        snprintf(list, sizeof list, "%s", getenv("SWOS_DOSVIDEO_TEST"));
        for (char *c = strtok(list, ","); c; c = strtok(NULL, ",")) {
            char sound[64] = "";
            int32_t delay = 3;
            if (!strcmp(c, "win")) snprintf(sound, sizeof sound, "flc/winmtch.raw");
            else if (!strcmp(c, "lose")) snprintf(sound, sizeof sound, "flc/losemat.raw");
            else if (!strcmp(c, "wcup")) snprintf(sound, sizeof sound, "flc/wincup.raw");
            else if (!strcmp(c, "lcup") || !strcmp(c, "rele")) snprintf(sound, sizeof sound, "flc/losecp.raw");
            else if (!strcmp(c, "prom")) snprintf(sound, sizeof sound, "flc/promote.raw");
            else if (!strncmp(c, "chp_", 4)) {
                snprintf(sound, sizeof sound, "chairman/%s/ecp%s.raw", voice_language(), c + 4);
                delay = 5;
            } else if (!strncmp(c, "ch", 2) && c[2] >= '1' && c[2] <= '3' && c[3] == '_')
                snprintf(sound, sizeof sound, "chairman/%s/ec%c%s.raw", voice_language(), 'a' + (c[2] - '1'), c + 4);
            play(c, sound[0] ? sound : NULL, delay, "SWOS_DOSVIDEO_TEST");
        }
        done = frames;
    }
    if (done && frames >= done + 60) {
        fprintf(stdout, "DOSVIDEOS TEST PASS\n");
        fflush(stdout);
        exit(0);
    }
}

static void dosvideos_init(void)
{
    const char *t = getenv("SWOS_DOSVIDEO_TEST");
    s_test = !t || !t[0] ? 0 : !strcmp(t, "rules") ? 2 : !strcmp(t, "career") ? 3 : 1;
}

/* --flic-selftest=<dir>: every .FLC in `dir` decoded to its last frame -- its frame count the
 * header's, nothing out of bounds (the decoder stops at a damaged chunk and says so). With
 * SWOS_FLIC_DUMP=<out>, each file's frames go to <out>/<NAME>.frames (per frame its palette, 768
 * bytes, then its pixels) for scripts/flic_check.py. The number of files that failed. */
typedef struct { char n[64]; } FlicName;
typedef struct { FlicName *names; int32_t count, cap; } FlicNames;

static int32_t flic_name(const char *name, void *ctx)
{
    FlicNames *l = (FlicNames *)ctx;
    const size_t n = strlen(name);
    if (n < 5 || n >= sizeof l->names[0].n || SDL_strcasecmp(name + n - 4, ".flc") != 0)
        return 0;
    if (l->count == l->cap) {
        const int32_t cap = l->cap ? l->cap * 2 : 128;
        FlicName *nn = (FlicName *)realloc(l->names, (size_t)cap * sizeof *nn);
        if (!nn)
            return 1;
        l->names = nn;
        l->cap = cap;
    }
    snprintf(l->names[l->count++].n, sizeof l->names[0].n, "%s", name);
    return 0;
}

static int flic_name_cmp(const void *a, const void *b)
{
    return SDL_strcasecmp(((const FlicName *)a)->n, ((const FlicName *)b)->n);
}

int32_t DosVideos_FlicSelfTest(const char *dir)
{
    const char *dump = getenv("SWOS_FLIC_DUMP");
    FlicNames l = { NULL, 0, 0 };
    int32_t bad = 0;
    uint32_t frames = 0;
    ModApi_ListFiles(dir, flic_name, &l);
    if (l.count)
        qsort(l.names, (size_t)l.count, sizeof l.names[0], flic_name_cmp);
    for (int32_t i = 0; i < l.count; i++) {
        char path[1200];
        uint32_t size = 0;
        snprintf(path, sizeof path, "%s/%s", dir, l.names[i].n);
        uint8_t *file = (uint8_t *)ModApi_ReadFile(path, &size);
        Flic f;
        if (!file || Flic_Open(&f, file, size) != 0) {
            fprintf(stderr, "[flic] %s: %s\n", l.names[i].n, file ? "not an FLC" : "cannot be read");
            free(file);
            bad++;
            continue;
        }
        uint8_t *px = (uint8_t *)calloc((size_t)f.width * f.height, 1);
        uint8_t pal[768];
        memset(pal, 0, sizeof pal);
        FILE *out = NULL;
        if (dump && dump[0]) {
            char o[1200], up[64];
            snprintf(up, sizeof up, "%s", l.names[i].n);
            for (char *c = up; *c; c++)
                if (*c >= 'a' && *c <= 'z')
                    *c = (char)(*c - 32);
            char *dot = strrchr(up, '.');
            if (dot)
                *dot = 0;
            snprintf(o, sizeof o, "%s/%s.frames", dump, up);
            out = fopen(o, "wb");
        }
        int32_t rc = 0, n = 0;
        while (px && (rc = Flic_Next(&f, px, pal)) == 1) {
            n++;
            if (out) {
                fwrite(pal, 1, sizeof pal, out);
                fwrite(px, 1, (size_t)f.width * f.height, out);
            }
        }
        if (out)
            fclose(out);
        const int32_t ok = px && rc == 0 && n == f.frames;
        fprintf(stderr, "[flic] %-12s %ux%u %3d of %3u frames, %u chunks skipped%s%s\n", l.names[i].n,
                (unsigned)f.width, (unsigned)f.height, (int)n, (unsigned)f.frames, (unsigned)f.skipped,
                ok ? "" : " -- FAILED", rc < 0 ? (rc == FLIC_ERR_FRAME ? " (a frame runs past its end)" :
                                                  rc == FLIC_ERR_DATA ? " (a chunk draws outside the picture)" : "") : "");
        frames += (uint32_t)n;
        bad += !ok;
        free(px);
        free(file);
    }
    fprintf(stderr, "[flic] %d files, %u frames: %s\n", (int)l.count, (unsigned)frames,
            !l.count ? "NO FILES" : bad ? "FAIL" : "PASS");
    free(l.names);
    return l.count ? bad : 1;
}

const ModDef ModDef_dosvideos = {
    .slug = SLUG,
    .init = dosvideos_init,
    .menuDrawn = test_menu,
    .careerNew = career_new,
    .careerEvent = career_event,
};

#endif /* SWOS_SDL_BUILD */
