/*
 * quick_access.c -- SWOS 2020's QUICK ACCESS after a match (quickAccess.mnu, imported as
 * bin/data_json/menus/openswos/quickAccessMenu.json; replays/replaysMenu.cpp callQuickAccessMenu,
 * menus/menu.cpp ToMainGameLoop). SDL builds.
 *
 * After a friendly the player played -- its REPLAY / EXIT screen left, before the mods' AFTER THE
 * MATCH entries (src/game_loop.c) -- and only with [general] quick_access = true (SWOS 2020's
 * quickAccessMenu_v2, which it shipped switched off): SAVE HIGHLIGHTS (the game's own, as the
 * HIGHLIGHTS screen saves them; SWOS 2020's SAVE AND SHARE, whose sharing needs its server),
 * FULL MATCH REPLAYS (the match just played, LATEST.SRP, in the highlights viewer) and CONTINUE.
 * SWOS 2020's SAVE GAME has no text and no colour in its menu (never shown): the same here.
 * Not after a mod's match, the cabinet's, an online one or training, as SWOS 2020 skipped its
 * speedruns, training and shoot-outs.
 */
#ifdef SWOS_SDL_BUILD
#include "highlights_menu.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "mod_api.h"
#include "platform_sdl.h"

#include <stdio.h>
#include <string.h>

extern PlatformInitOptions *g_arcadeOpts;
extern void SaveHighlightsMenu(void);
extern void ReplayV2_Unload(void);
extern int32_t ReplayV2_Load(const uint8_t *name);

enum { Q_header, Q_saveGame, Q_highlights, Q_matchReplays, Q_desc, Q_exit };

static void QuickAccess_OnInit(void)
{
    ModApi_MenuVisible(Q_saveGame, 0);
}

static void QuickAccess_OnDraw(void)    /* (quickAccessMenu_OnDraw: what the entry does) */
{
    static char desc[48];
    const int32_t m = ModApi_MenuCurrentEntry();
    snprintf(desc, sizeof desc, "%s", m == Q_highlights ? "SAVE THIS MATCH'S HIGHLIGHTS"
                                      : m == Q_matchReplays ? "VIEW THE FULL LENGTH MATCH REPLAY"
                                      : "BACK TO THE PREVIOUS MENU");
    ModApi_MenuText(Q_desc, desc);
}

static void QuickAccess_Highlights(void)
{
    SaveHighlightsMenu();
}

static void QuickAccess_MatchReplays(void)  /* the whole match, in the highlights viewer */
{
    ReplayV2_Unload();
    if (ReplayV2_Load((const uint8_t *)"LATEST.SRP") != 0) {
        ModApi_ShowError("NO RECORDING OF THIS MATCH");
        return;
    }
    ShowHighlights();
    ReplayV2_Unload();                  /* (SAVE HIGHLIGHTS: this match's own again) */
}

void QuickAccess_AfterMatch(void)
{
    static int32_t registered;
    if (!g_arcadeOpts || !g_arcadeOpts->quick_access)
        return;
    if (!registered) {
        static const MenuSymbol kHandlers[] = {
            { "QuickAccess_OnInit", (void *)QuickAccess_OnInit, SYM_FUNC },
            { "QuickAccess_OnDraw", (void *)QuickAccess_OnDraw, SYM_FUNC },
            { "QuickAccess_Highlights", (void *)QuickAccess_Highlights, SYM_FUNC },
            { "QuickAccess_MatchReplays", (void *)QuickAccess_MatchReplays, SYM_FUNC },
        };
        registered = 1;
        MenuJson_RegisterSymbols(kHandlers, (int)(sizeof kHandlers / sizeof kHandlers[0]));
    }
    ModApi_ShowMenu("openswos/quickAccessMenu");
}
#endif
