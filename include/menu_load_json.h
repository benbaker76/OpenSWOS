/*
 * menu_load_json.h -- the JSON menu loader (src/menu_load_json.c, SDL builds).
 * See docs/MENUS.md: §2d for the compiled menus it installs, §10 for the menus
 * that exist only as JSON (the imported SWOS 2020 menus).
 */
#ifndef MENU_LOAD_JSON_H
#define MENU_LOAD_JSON_H

#include "swos_types.h"
#include "menu_symbols.h"

/* JSON menu object -> packed menu stream (docs/MENUS.md §0). Length, or -1. */
int      MenuJson_BuildPacked(const char *json, int len, uint8_t *out, int outcap);

/* The 47 compiled menus: byte-compare against JSON / overwrite in place. */
int      MenuJson_VerifyAll(void);
int      MenuJson_InstallAll(void);

/* Add a name -> address table searched after the generated menu_symbols registry.
 * The table must outlive every menu loaded from it. */
void     MenuJson_RegisterSymbols(const MenuSymbol *syms, int count);

/* A menu with no compiled counterpart, by path under data_json/menus/ without
 * ".json" (e.g. "swos2020/logInMenu"). Built once and cached; NULL if it cannot be. */
uint8_t *MenuJson_Load(const char *name);

/* The name of a packed menu -- a compiled one ("swosMainMenu") or a JSON-only one
 * ("swos2020/logInMenu") -- for the [menu] log; NULL if it is neither. */
const char *MenuJson_NameOf(const uint8_t *menu);

/* Unpack such a menu into scratch memory and check the result against its JSON.
 * Mismatch count, or -1 if it would not load. */
int      MenuJson_VerifyDynamic(const char *name, int expectedEntries);

#endif /* MENU_LOAD_JSON_H */
