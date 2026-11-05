/*
 * assets_check.h -- tell the player when their game data was made by an older OpenSWOS
 * Setup (src/assets_check.c, ASSETS.ini: include/assets_ini.h). SDL builds only.
 */
#ifndef ASSETS_CHECK_H
#define ASSETS_CHECK_H

#include "platform_sdl.h"   /* PlatformInitOptions */

/* After SDL_Init: log the parts of the assets folder an older setup made, and once a
 * set of data versions show the reminder (with "Don't remind me", saved to iniPath). */
void AssetsCheck_Run(PlatformInitOptions *opts, const char *iniPath);

/* The world a run plays (include/world.h) and its data package's state, found before SDL_Init
 * (main.c): 0 fine, 1 its package is not installed (96/97 played instead), 2 the installed
 * package is of other data than this build's (`have` / `want`: their fingerprints). After
 * SDL_Init, AssetsCheck_World says so in a message box (not headless, in the arcade cabinet or
 * the web build). */
void AssetsCheck_SetWorld(int32_t state, const char *tag, const char *have, const char *want);
void AssetsCheck_World(PlatformInitOptions *opts);

#endif /* ASSETS_CHECK_H */
