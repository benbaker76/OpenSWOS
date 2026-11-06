/*
 * menu_baked.h -- a menu baked for the Amiga (src/menu_load_json.c, tools/menu_bake.c).
 *
 * The Amiga parses no JSON (docs/DATA_BSON.md). The menus of the mods it runs are built from
 * their JSON at BUILD time, by the same builder the SDL game runs: the packed menu stream,
 * big-endian with 4-byte pointer slots, every slot zero, and beside it what goes in each slot --
 * a handler or a string by its symbol name, or a literal's text. Makefile.amiga compiles them
 * (obj/menus_baked.c); MenuJson_Load there copies one and fills its slots.
 */
#ifndef MENU_BAKED_H
#define MENU_BAKED_H

#include <stdint.h>

enum {
    MENU_BAKED_FUNC = 1,        /* a handler, by name (unregistered: a no-op, logged) */
    MENU_BAKED_DATA = 2,        /* a string / table, by name */
    MENU_BAKED_LITERAL = 3,     /* text: the slot points at it */
};

typedef struct {
    uint32_t    offset;         /* of the 4-byte slot in the menu */
    uint8_t     kind;
    const char *text;
} MenuBakedReloc;

typedef struct {
    const char           *name;         /* as MenuJson_Load is asked: "practice/practiceMenu" */
    const uint8_t        *bytes;
    uint32_t              size;
    const MenuBakedReloc *relocs;
    uint32_t              relocCount;
} MenuBaked;

extern const MenuBaked g_bakedMenus[];
extern const int32_t   g_bakedMenuCount;

/* (the bake tool: the relocs of the menu MenuJson_BuildPacked just built; -1 = too many) */
int MenuJson_BakeRelocs(const MenuBakedReloc **relocs);

#endif /* MENU_BAKED_H */
