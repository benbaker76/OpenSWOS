/*
 * mod_careerplus.c -- Career+ (benefactorr), one mod (docs/mods/CAREER_PLUS.md): what its fork
 * changed in the engine, on OpenSWOS's career hooks and APIs (docs/MODS.md §27-§31) and its
 * data (bin/mods/careerplus). The SDL build's: its data (career_params.json and the rest) is.
 *
 * This file: the mod's registration, its settings, its random numbers and its section of the
 * career save. Each feature is a file of its own, mod_careerplus_<feature>.c, through
 * include/mod_careerplus.h.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "mjson.h"

#include <stdio.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

/* ---- the match changes (CAREER_PLUS.md §5): the game's own tweaks, each behind a setting ---- */

static const char *const kTweaks[] = { "cpu_cards", "more_fouls", "ai_injury_subs" };

static void apply_tweaks(void)
{
    for (int32_t i = 0; i < (int32_t)(sizeof kTweaks / sizeof kTweaks[0]); i++)
        if (ModApi_SetTweak(CP_SLUG, kTweaks[i], ModApi_SettingInt(CP_SLUG, kTweaks[i], 1)) != 0)
            fprintf(stderr, "[careerplus] the game has no tweak %s to switch\n", kTweaks[i]);
}

/* ---- random numbers (CAREER_PLUS.md §4): the mod's own, so the game's draws are the game's.
 * Seeded when a career starts, from the game's own seeds and clock (so a run of the autopilot
 * repeats), and kept in the career save. xorshift32. ---- */

static uint32_t s_rng = 0x2545F491u;

uint32_t CP_Rand(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s_rng = x;
}

int32_t CP_RandBelow(int32_t n)
{
    return n > 0 ? (int32_t)(CP_Rand() % (uint32_t)n) : 0;
}

static void seed_rng(void)
{
    extern uint8_t _seed, _randXorIndex, _seed2, _randXorIndex2;
    extern volatile uint16_t _currentTick;
    s_rng = 0x9E3779B9u ^ ((uint32_t)_seed << 24 | (uint32_t)_randXorIndex << 16 |
                           (uint32_t)_seed2 << 8 | _randXorIndex2) ^ (_currentTick * 2654435761u);
    if (s_rng == 0)
        s_rng = 1;
}

/* ---- RETIREMENT NEWS (data_json/menus/careerplus/newsMenu.json): entry 0 the title, 1-10 the
 *      rows, 11 NEXT / CONTINUE ---- */

enum { kNewsTitle = 0, kNewsRow = 1, kNewsRows = 10, kNewsButton = 11 };
static int32_t s_newsPage;
static char s_newsTitle[40], s_newsText[kNewsRows][48];

static void news_fill(void)
{
    static const char *const kPos[8] = { "GK", "RB", "LB", "D", "RW", "LW", "M", "A" };
    snprintf(s_newsTitle, sizeof s_newsTitle, "PLAYER RETIREMENTS - SEASON %d", (int)CP_News_Season());
    ModApi_MenuText(kNewsTitle, s_newsTitle);
    for (int32_t r = 0; r < kNewsRows; r++) {
        uint8_t age = 0, pos = 0;
        const char *name = CP_News_Line(s_newsPage * kNewsRows + r, &age, &pos);
        if (name)
            snprintf(s_newsText[r], sizeof s_newsText[r], "%-22.22s  AGE %2u  %s", name, (unsigned)age, kPos[pos & 7]);
        else
            s_newsText[r][0] = 0;
        ModApi_MenuText(kNewsRow + r, s_newsText[r]);
    }
    ModApi_MenuText(kNewsButton, (s_newsPage + 1) * kNewsRows < CP_News_Count() ? "NEXT" : "CONTINUE");
}

static void newsInit(void) { news_fill(); }

static void newsNext(void)
{
    extern void SetExitMenuFlag(void);      /* (what a menu's EXIT calls) */
    if ((s_newsPage + 1) * kNewsRows >= CP_News_Count()) {
        SetExitMenuFlag();
        return;
    }
    s_newsPage++;
    news_fill();
    ModApi_MenuRedraw(kNewsTitle);
    for (int32_t r = 0; r < kNewsRows; r++)
        ModApi_MenuRedraw(kNewsRow + r);
    ModApi_MenuRedraw(kNewsButton);
}

/* ---- the hooks: each feature's, in turn ---- */

static void cp_init(void)
{
    static const ModHandler kHandlers[] = {
        { "newsInit", newsInit },
        { "newsNext", newsNext },
    };
    apply_tweaks();
    ModApi_RegisterHandlers(CP_SLUG, kHandlers, (int32_t)(sizeof kHandlers / sizeof kHandlers[0]));
    CP_Winners_Init();
    CP_AI_Init();
    CP_Search_Init();
}

static void cp_matchStart(void)
{
    apply_tweaks();         /* (a setting changed in OPTIONS is seen at the next match) */
}

static void cp_careerNew(void)
{
    seed_rng();
    CP_Winners_Clear();
    CP_Ages_New();
    CP_AI_SetBatches(0);
    CP_Search_Stale();
}

/* A season over, the next set up: everyone a year older, the retirements, the AI transfers. */
static void cp_careerSeasonEnd(uint16_t season)
{
    if (CP_World_Read() < 0)
        return;
    CP_Ages_SeasonEnd();
    if (ModApi_SettingInt(CP_SLUG, "retirements", 1))
        CP_Ages_Retire(season);
    else
        CP_News_Clear();            /* (no news from a season before) */
    if (ModApi_SettingInt(CP_SLUG, "ai_transfers", 1))
        CP_AI_Batch();
    CP_Search_Fresh();      /* (CP_World changed as the career did) */
}

static void cp_careerBeforeContinue(void)
{
    if (CP_News_Count() > 0) {
        s_newsPage = 0;
        ModApi_ShowMenu("careerplus/newsMenu");
    }
}

static void cp_competitionFinished(const ModCompetitionResult *result)
{
    CP_Winners_Record(result);
}

static void cp_playerMoved(const ModPlayerMove *move)
{
    CP_Ages_Moved(move);
    CP_Search_Stale();
}

static void cp_playerCreated(const ModPlayerCreated *created)
{
    CP_Ages_Created(created);
    CP_Trialists_Created(created);
    CP_Search_Stale();
}

static void cp_careerSave(ModJsonWriter *out)
{
    ModJson_Printf(out, "\"rng\":%u,\"ai_batches\":%u", (unsigned)s_rng, (unsigned)CP_AI_Batches());
    CP_Winners_Save(out);
    CP_Ages_Save(out);
}

static void cp_careerLoad(const char *json, int32_t len)
{
    double v;
    if (json && mjson_get_number(json, len, "$.rng", &v) && v >= 1 && v <= 4294967295.0)
        s_rng = (uint32_t)v;
    else
        seed_rng();         /* (a career Career+ did not save: from here on, the mod's) */
    CP_AI_SetBatches(json && mjson_get_number(json, len, "$.ai_batches", &v) && v >= 0 && v <= 65535
                     ? (uint16_t)v : 0);
    CP_Winners_Load(json, len);
    CP_Ages_Load(json, len);
    CP_Search_Stale();
}

const ModDef ModDef_careerplus = {
    .slug = CP_SLUG,
    .init = cp_init,
    .matchStart = cp_matchStart,
    .careerNew = cp_careerNew,
    .careerSeasonEnd = cp_careerSeasonEnd,
    .careerBeforeContinue = cp_careerBeforeContinue,
    .competitionFinished = cp_competitionFinished,
    .playerMoved = cp_playerMoved,
    .playerCreated = cp_playerCreated,
    .playerRowDrawn = CP_Rows_Drawn,
    .careerSave = cp_careerSave,
    .careerLoad = cp_careerLoad,
};

#endif /* SWOS_SDL_BUILD */
