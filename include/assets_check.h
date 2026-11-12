/*
 * assets_check.h -- tell the player when their game data was made by an older OpenSWOS
 * Setup (src/assets_check.c, ASSETS.ini: include/assets_ini.h). SDL builds only.
 */
#ifndef ASSETS_CHECK_H
#define ASSETS_CHECK_H

#include "platform_sdl.h"   /* PlatformInitOptions */

/* After SDL_Init: log the parts of the assets folder an older setup made, and once a set
 * of data versions note the reminder the front end shows (AssetsCheck_ShowNotice; DON'T
 * REMIND ME saved to iniPath). */
void AssetsCheck_Run(PlatformInitOptions *opts, const char *iniPath);

/* The front end, before the main menu: the notice noted at start-up, as a dialog of the game's
 * (MenuDialog_Show) -- 1 when one was shown. QuitForSetup: 1 when its LAUNCH SETUP started
 * OpenSWOS Setup, and the game should close so it can update the data. */
int32_t AssetsCheck_ShowNotice(void);
int32_t AssetsCheck_QuitForSetup(void);

/* OpenSWOS Setup beside the game (1 with its path), and starting it (1 when it started):
 * the notice's LAUNCH SETUP and OPTIONS -> LAUNCH SETUP. Never on a phone or the web. */
int32_t AssetsCheck_FindSetup(char *out, uint32_t cap);
int32_t AssetsCheck_LaunchSetup(void);
/* The same with --headless: the data made with no window when the setup finds the player's copy
 * of the game, its window only when it does not -- the "no game data" box's Launch Setup. */
int32_t AssetsCheck_LaunchSetupHeadless(void);

/* The world a run plays (include/world.h) and its data package's state, found before SDL_Init
 * (main.c): 0 fine, 1 its package is not installed (96/97 played instead), 2 the installed
 * package is of other data than this build's (`have` / `want`: their fingerprints). After
 * SDL_Init, AssetsCheck_World notes it for the same screen (not headless, in the arcade cabinet
 * or the web build; the game data's reminder first, when both). */
void AssetsCheck_SetWorld(int32_t state, const char *tag, const char *have, const char *want);
void AssetsCheck_World(PlatformInitOptions *opts);

/* 1 when OpenSWOS Setup made the PC data from SWOS 2020's install, there being no PC game and no
 * Amiga version (ASSETS.ini [sources] pc_swos2020): SWOS 2020's art and pitches, which only the
 * PC drawing path holds (its pitches do not fit the Amiga's), no commentary, its menu.mp3 for
 * menu.xmi. Read once; callable before SDL_Init. */
int32_t AssetsCheck_PcFromSwos2020(void);

#endif /* ASSETS_CHECK_H */
