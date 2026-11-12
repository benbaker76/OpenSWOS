/*
 * credits_menu.h -- the CREDITS screen (SDL builds). See src/credits_menu.c; the Amiga
 * AGA screen is menus.c CreditsAgaDraw, and both draw include/credits_text.h.
 */
#ifndef CREDITS_MENU_H
#define CREDITS_MENU_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"

/* The JSON-only menu behind the main menu's CREDITS button (docs/MENUS.md §10). */
#define CREDITS_MENU_NAME     "openswos/creditsMenu"
#define CREDITS_MENU_ENTRIES  3
#define FULL_CREDITS_MENU_NAME "openswos/fullCreditsMenu"

/* Register the menu's handlers with the JSON loader. Idempotent; Show calls it. */
void CreditsMenu_Init(void);
/* Lend the photos their palette entries, show the screen, give them back. */
void CreditsMenu_Show(void);
/* FULL CREDITS' scrolling text over the picture: platform_sdl.c draw_overlays, every frame
 * (nothing unless that screen is up). */
void CreditsMenu_DrawOverlay(void);
/* data/CREDITS.txt read (once): how many lines it made, or 0 when it is missing. With
 * SWOS_FULL_CREDITS_DUMP=1 the lines are printed, as kind|role|names (--menu-selftest). */
int32_t CreditsMenu_FullCreditsLines(void);

#endif /* SWOS_SDL_BUILD */
#endif /* CREDITS_MENU_H */
