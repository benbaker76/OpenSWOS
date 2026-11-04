/*
 * controls_menu.h -- the CONTROLS menu (SDL builds). See src/controls_menu.c.
 */
#ifndef CONTROLS_MENU_H
#define CONTROLS_MENU_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

/* The JSON-only menu behind OPTIONS' CONTROLS row (docs/MENUS.md §10). */
#define CONTROLS_MENU_NAME     "openswos/controlsMenu"
#define CONTROLS_MENU_ENTRIES  19   /* (the self-test's count: one per item of the JSON) */

/* Register the menu's handlers with the JSON loader. Idempotent; Show calls it. */
void ControlsMenu_Init(void);
void ControlsMenu_Show(void);

#endif /* SWOS_SDL_BUILD */
#endif /* CONTROLS_MENU_H */
