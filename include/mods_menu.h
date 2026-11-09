/*
 * mods_menu.h -- the MODS screen (SDL builds). See src/mods_menu.c.
 */
#ifndef MODS_MENU_H
#define MODS_MENU_H

#ifdef SWOS_SDL_BUILD

/* The JSON-only menu behind OPTIONS' MODS row (docs/MODS.md §15). */
#define MODS_MENU_NAME     "openswos/modsMenu"
#define MODS_MENU_ENTRIES  22   /* (the self-test's count: one per item of the JSON) */
/* A mod's page: its name, what it does, its settings (mod.json "settings"). */
#define MOD_PAGE_MENU_NAME     "openswos/modPageMenu"
#define MOD_PAGE_MENU_ENTRIES  21   /* (the author line, 2026-11-08: appended) */
/* A setting's choices as a scrolling list: more than a page's rows (a gallery's pictures). */
#define CHOICE_MENU_NAME       "openswos/choiceMenu"
#define CHOICE_MENU_ENTRIES    13

/* Register the menu's handlers with the JSON loader. Idempotent; Show calls it. */
void ModsMenu_Init(void);
void ModsMenu_Show(void);
/* --menu=openswos/choiceMenu:<slug>:<key>: that mod setting's list, as its button opens it (a
 * look at the screen; 0 when the mod or the setting is not there). */
int32_t ModsMenu_ShowChoices(const char *slug, const char *key);

#endif /* SWOS_SDL_BUILD */
#endif /* MODS_MENU_H */
