/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "frontend.h"
#include "game_utils.h"
#include "audio.h"
#include "menus.h"
#include "world.h"           /* the football world, before the competition tables */
#include "world_cups.h"      /* its cups into the country tables */
#include "display.h"
#include <string.h>   /* memcpy/memset — used by both the SDL decode and the Amiga overlay re-stride */

/* FrontEndMainLoop */
extern uint16_t _quitToDos;

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
extern void MenuProc(void);

extern uint16_t _menuMusic;
extern uint16_t _menuFade;
extern uint8_t _frontbufferMaskBuffer[];
extern volatile uint16_t _currentGameTick;
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern uint8_t _rawAssetLoadBuffer[];   /* staging buffer for both builds' overlay load */

/* ================================================================
 * LoadOverlayToFrontbuffer  (SWOS2.s line 42675)
 *
 * Loads a raw image file into the frontbuffer mask buffer.
 *
 * Amiga: loads the bitplane-format file straight into
 *   `frontbufferMaskBuffer`, which stays in bitplane form.
 * SDL:   stages the bitplane file into `rawAssetLoadBuffer`, then
 *   decodes the first SCREEN_HEIGHT scanlines into the chunky 8bpp
 *   `frontbufferMaskBuffer`. Subsequent draw/blit code reads the
 *   mask buffer expecting chunky palette indices.
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadOverlayToFrontbuffer(const uint8_t *path)
{
#ifdef SWOS_SDL_BUILD
    uint32_t ovlSize = 0;
    LoadFileEx((uint8_t *)path, _rawAssetLoadBuffer, &ovlSize);

    /* Decode interleaved bitplane data → 8bpp chunky. Full-screen overlay
     * images are stored at the *frontbuffer* stride: PLANE_STRIDE bytes per
     * plane row, N planes per scanline. Detect N per-bank from the
     * decompressed size (MENUBG.RAW = 47872 for 4bpp OCS, 95744 for 8bpp
     * AGA) rather than assuming NUM_PLANES, so mixed-depth grafs_aga banks
     * decode correctly. We decode SCREEN_WIDTH pixels (the visible portion
     * of PLANE_STRIDE*8 = 352 source bits). Sprite-source images use a
     * smaller stride and go through `blitDispatch`, not this path. */
    uint16_t ovlPlanes    = DetectBankPlanes(ovlSize, PLANE_STRIDE, SCREEN_HEIGHT);
    uint32_t ovlRowStride = (uint32_t)PLANE_STRIDE * ovlPlanes;
    memset(_frontbufferMaskBuffer, 0, FRONTBUFFER_SIZE_TOTAL_BYTES);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *rowSrc = _rawAssetLoadBuffer + (size_t)y * ovlRowStride;
        uint8_t       *rowDst = _frontbufferMaskBuffer + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES + WIDE_MARGIN;
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            int bit = 7 - (px & 7);
            int byte = px >> 3;
            uint8_t idx = 0;
            for (int p = 0; p < ovlPlanes; p++) {
                uint8_t planeByte = rowSrc[p * PLANE_STRIDE + byte];
                idx |= (uint8_t)(((planeByte >> bit) & 1u) << p);
            }
            rowDst[px] = idx;
        }
    }
#else
    /* Amiga: load the raw interleaved-bitplane overlay, then RE-STRIDE it into
     * the mask buffer. The overlay FILES are authored at a 44-byte plane row
     * (352px — the pre-FMODE frontbuffer width); the AGA FMODE widen made the
     * runtime mask/frontbuffer PLANE_STRIDE 52, so the old verbatim
     * `LoadFileEx(..., _frontbufferMaskBuffer, 0)` misaligned every plane row:
     * BlitterCopyPanelToScreen2 reads the mask at PLANE_STRIDE, so a 44-byte
     * source row bled 8 bytes into the next plane every row -> horizontal
     * streaks across the whole menu backdrop. Copy each interleaved plane row
     * into its PLANE_STRIDE slot instead. On OCS (PLANE_STRIDE == 44) the
     * strides match and this is a straight copy. Plane count comes from the
     * decompressed size so 4bpp and 8bpp grafs banks both load. docs/AGA.md §11. */
    enum { OVERLAY_SRC_PLANE_STRIDE = 44 };
    uint32_t ovlSize = 0;
    LoadFileEx((uint8_t *)path, _rawAssetLoadBuffer, &ovlSize);
    uint32_t filePlanes = ovlSize / ((uint32_t)OVERLAY_SRC_PLANE_STRIDE * SCREEN_HEIGHT);
    if (filePlanes == 0 || filePlanes > (uint32_t)NUM_PLANES)
        filePlanes = NUM_PLANES;
    memset(_frontbufferMaskBuffer, 0, FRONTBUFFER_SIZE_TOTAL_BYTES);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *src = _rawAssetLoadBuffer +
                             (size_t)y * OVERLAY_SRC_PLANE_STRIDE * filePlanes;
        uint8_t *dst = _frontbufferMaskBuffer + (size_t)y * ROW_STRIDE_BYTES;
        for (uint32_t p = 0; p < filePlanes; p++)
            memcpy(dst + (size_t)p * PLANE_STRIDE,
                   src + (size_t)p * OVERLAY_SRC_PLANE_STRIDE,
                   OVERLAY_SRC_PLANE_STRIDE);
    }
#endif
}

/* ================================================================
 * FrontEndPreLoopSetup  (SWOS2.s line 42642)
 *
 * Initializes menu music, prepares menu background, sets up menu
 * data structures, optionally starts music playback, and enables
 * the menu fade-in flag.
 * ================================================================ */
__attribute__((used, externally_visible, noinline))
void FrontEndPreLoopSetup(void)
{
    /* The football world (include/world.h): the Amiga's drawer names it in data/WORLD;
     * its baked competition records go over 96/97's on both targets, before the JSON. */
#ifndef SWOS_SDL_BUILD
    World_ReadFromData();
#endif
    World_InstallBaked();
#ifndef SWOS_SDL_BUILD
    WorldCups_Install();        /* its cups into the country tables (the SDL: after the JSON's) */
#endif
#ifdef SWOS_SDL_BUILD
    /* SWOS_DATA_JSON: rebuild normal-country competition tables from data_json/
       before any menu reads competitionsTable[]. No-op unless the env var is set. */
    extern void DataJson_FitModTeamFiles(void);
    DataJson_FitModTeamFiles();         /* (a mod's bigger team file: its country's team numbers) */
    extern void DataJson_InstallCompetitionGraph(void);
    DataJson_InstallCompetitionGraph();
    WorldCups_Install();
    /* career_params.json: override the compiled career-mode balance knobs (starting
       balance, gate receipts, wages, board expectations) so testers can tune them
       without a rebuild. Absent file -> compiled defaults. */
    extern void DataJson_LoadCareerParams(void);
    DataJson_LoadCareerParams();
    extern void DataJson_LoadNationalTeams(void);
    DataJson_LoadNationalTeams();       /* (a mod's national teams: include/national_teams.h) */
    /* SWOS_MENU_JSON: load every menu from bin/data_json/menus/*.json and overwrite
       the compiled static menu bytes IN PLACE (docs/MENUS.md §2d), so editing the
       JSON changes the running game. Bounded by each menu's compiled size; a menu
       grown beyond it stays compiled and is flagged. Default-ON; SWOS_MENU_JSON=0
       disables. */
    { const char *v = getenv("SWOS_MENU_JSON");
      if (!(v && v[0] == '0' && v[1] == '\0')) {
        extern int MenuJson_InstallAll(void);
        MenuJson_InstallAll();
      } }
    /* SWOS 2020's online menus (docs/MENUS.md §10) exist only as JSON and load on
       first use; register their handlers with the loader before anything opens one. */
    extern void OnlineMenus_Init(void);
    OnlineMenus_Init();
    /* SWOS_TACTICS_JSON: load the 12 built-in formations from
       bin/data_json/tactics/*.json and overwrite the compiled TeamTactics
       structs IN PLACE (src/tactics_load_json.c), so editing a formation's JSON
       changes the running game. Default-ON; SWOS_TACTICS_JSON=0 disables. */
    { const char *vt = getenv("SWOS_TACTICS_JSON");
      if (!(vt && vt[0] == '0' && vt[1] == '\0')) {
        extern int TacticsJson_InstallAll(void);
        TacticsJson_InstallAll();
      } }
#else
    /* Amiga: no runtime JSON parse, so install the BAKED Copa Libertadores
       (competition_data_copa_baked.c) here — the analog of the SDL dynamic
       DataJson_InstallCompetitionGraph above — so the CONMEBOL career qualifier
       (career_copa.c) finds Copa by compId. See docs/COMPETITION_DYNAMIC_REFACTOR.md §8. */
    extern void CopaBaked_Install(void);
    CopaBaked_Install();
#endif
    /* Copa Libertadores is registered by compId above (dynamic on SDL, baked on
       Amiga); splice its record into the South America region's preset-competition
       list so it appears in "CHOOSE PRESET COMPETITION - SOUTH AMERICA". Inert if
       Copa isn't installed. Shared code path (career_copa.c) — same on both ports. */
    extern void Career_InstallCopaLibertadoresPresetEntry(void);
    Career_InstallCopaLibertadoresPresetEntry();
    InitMenuMusic();
    PrepareMenuBackground();
    InitMainMenuStuff();

    if (_menuMusic != 0)
    {
        TryPlayMusic();
    }

    _menuFade = 1;
}

/* ================================================================
 * FrontEndMainLoop  (SWOS2.s line 42901)
 *
 * Main front-end event loop. Initializes timer and menu system,
 * then repeatedly calls MenuProc until _quitToDos is set.
 * ================================================================ */
__attribute__((used, externally_visible, noinline))
void FrontEndMainLoop(void)
{
    ReadTimerDelta();
    FrontEndPreLoopSetup();
#ifdef SWOS_SDL_BUILD
    /* --menu=<name>: open a JSON-only menu first; leaving it lands on the main menu --
     * re-unpacked from its packed bytes, so put it back the way every main-menu button
     * does (CommonMenuExit). Without it CONTINUE and SAVE came back as two blank, live
     * rows under MULTIPLAYER. */
    extern int32_t OnlineMenus_ShowStartMenu(void);
    extern int32_t OnlineMenus_QuitAfterStartMenu(void);
    if (OnlineMenus_ShowStartMenu()) {
        if (OnlineMenus_QuitAfterStartMenu())
            _quitToDos = 1;             /* the online client: leaving the log-in is leaving */
        else
            CommonMenuExit();
    }
#endif

    do
    {
        MenuProc();
    } while (_quitToDos == 0);
}
