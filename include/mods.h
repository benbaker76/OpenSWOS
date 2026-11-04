/*
 * mods.h -- mods: folders of files over the game's data (docs/MODS.md §8.1).
 *
 * A mod is a folder, mods/<id>/, holding the files it replaces at the paths the game asks for
 * (data_json/menus/x.json, grafs/..., sound_dos/...) and a manifest, mod.json:
 *
 *   { "id": "classics", "name": "Historic tournaments", "version": "1.0", "author": "...",
 *     "targets": ["sdl", "amiga"], "changes": "cosmetic" }
 *
 * `changes` is "cosmetic" (pictures, sound, text: two players may differ), "gameplay" (how a
 * match plays) or "career" (competitions, teams, the season). The enabled mods are listed in
 * SWOS.ini, [mods] enabled = a, b (or --mods=a,b / SWOS_MODS_ENABLED for one run); on the Amiga
 * in the drawer's data/MODS, one id a line, written by the setup. Every data READ looks in each
 * enabled mod, in that order, before the game's own files; writes never do. The Amiga's list
 * (an id followed by '*': not cosmetic) is read by src/resload.c at its first file read.
 *
 * Online: until the handshake carries the mod list (MODS.md §8.6), a build with a mod that is
 * not cosmetic does not play online (Mods_OnlineBlocker).
 */
#ifndef MODS_H
#define MODS_H

#include "swos_types.h"   /* the fixed-width types (the Amiga build has no libc stdint.h) */

#define MODS_MAX     16
#define MODS_ID_MAX  32

/* The enabled list, from `list` ("a, b c"; ids are letters, digits, '-' and '_'). SDL: each
 * must be a folder under Mods_Root() with a mod.json whose targets has "sdl", else it is left
 * out and the log says why. Call once, after the settings and before any data is read. The
 * Amiga's list comes from data/MODS, which src/resload.c reads at its first file read. */
void        Mods_Init(const char *list);

int32_t     Mods_Count(void);
const char *Mods_Id(int32_t i);
/* The mod's folder, no trailing slash: SDL "<Mods_Root()>/<id>", Amiga "mods/<id>". */
const char *Mods_Dir(int32_t i);
/* Where mods are found (SDL): $SWOS_MODS, else "mods" in the player's game folder. */
const char *Mods_Root(void);

/* The first enabled mod that is not "cosmetic" (its name), or NULL when online play is fine. */
const char *Mods_OnlineBlocker(void);

#endif /* MODS_H */
