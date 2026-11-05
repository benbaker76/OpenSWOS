/*
 * mods_menu.h -- the MODS screen (SDL builds). See src/mods_menu.c.
 */
#ifndef MODS_MENU_H
#define MODS_MENU_H

#ifdef SWOS_SDL_BUILD

/* The JSON-only menu behind OPTIONS' MODS row (docs/MODS.md §15). */
#define MODS_MENU_NAME     "openswos/modsMenu"
#define MODS_MENU_ENTRIES  20   /* (the self-test's count: one per item of the JSON) */
/* A mod's page: its name, what it does, its settings (mod.json "settings"). */
#define MOD_PAGE_MENU_NAME     "openswos/modPageMenu"
#define MOD_PAGE_MENU_ENTRIES  20

/* Register the menu's handlers with the JSON loader. Idempotent; Show calls it. */
void ModsMenu_Init(void);
void ModsMenu_Show(void);

#endif /* SWOS_SDL_BUILD */
#endif /* MODS_MENU_H */
