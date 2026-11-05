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
 * that changes how a match plays plays online only against the same mods (Mods_OnlineSignature).
 */
#ifndef MODS_H
#define MODS_H

#include "swos_types.h"   /* the fixed-width types (the Amiga build has no libc stdint.h) */

#define MODS_MAX     16
#define MODS_ID_MAX  32

/* Two kinds (docs/MODS.md §14). CANONICAL mods are part of OpenSWOS: in the game's own
 * MODS_CANONICAL_DIR (bin/mods/, git-tracked, in every drop), on when their manifest says
 * "default": true unless [mods] disabled names them, or when [mods] enabled does. DLC mods are
 * downloaded: folders in the player's Mods_Root(), on only when [mods] enabled names them. Every
 * mod has a "uuid" in its mod.json (a canonical one: uuid5 of the project's namespace and
 * "mod/<slug>"; a DLC: the author's uuid4), its identity in saves, the handshake and the index;
 * a mod without one, a second mod with a uuid already on, or a DLC with a canonical uuid, is
 * left out. */
#define MODS_CANONICAL_DIR "mods"

/* The mods on, from `enabled` ("a, b c"; slugs are letters, digits, '-' and '_') -- in that
 * order, then the canonical defaults not in `disabled`. SDL: each needs a folder and a mod.json
 * with a uuid and "sdl" in its targets, else it is left out and the log says why. Call once,
 * after the settings and before any data is read. The Amiga's list (both kinds) comes from
 * data/MODS, which src/resload.c reads at its first file read; `disabled` is unused there. */
void        Mods_Init(const char *enabled, const char *disabled);
/* Before Mods_Init: a rig runs (main.c), so the default-on canonical mods that are not cosmetic
 * stay off (a mod the run names is still on). SDL. */
void        Mods_SetRig(int32_t on);

int32_t     Mods_Count(void);
const char *Mods_Id(int32_t i);
const char *Mods_Uuid(int32_t i);           /* "" on the Amiga (it reads no manifest) */
int32_t     Mods_IsCanonical(int32_t i);
/* Whether the mod of that slug is on in this run (a built-in mod's code asks: src/mod_hooks.c). */
int32_t     Mods_IsOn(const char *slug);
/* The mod's folder, no trailing slash: SDL "<Mods_Root()>/<id>", Amiga "mods/<id>". */
const char *Mods_Dir(int32_t i);
/* Where mods are found (SDL): $SWOS_MODS, else "mods" in the player's game folder. */
const char *Mods_Root(void);

#ifdef SWOS_SDL_BUILD
/* Every mod on disk, on or not (the MODS screen): the canonical ones, then the DLC. */
typedef struct {
    char    slug[MODS_ID_MAX];
    char    name[48];
    char    version[24];
    char    uuid[40];
    char    changes[16];        /* "cosmetic", "modes", "career", "gameplay", "data" */
    char    sprites[16];        /* "jaguar": its grafs_jag/ is a sprite set the Jaguar tier draws */
    char    problem[80];        /* why it cannot be on ("" when it can) */
    uint8_t canonical, cosmetic, byDefault, on;
} ModInfo;
int32_t     Mods_Scan(ModInfo *out, int32_t max);
#endif

/* What both players of an online match must have alike (docs/MODS.md §8.6): the mods that are
 * on and change how a match plays or what it plays with -- "changes": "gameplay" or "data" --
 * each by its uuid, its version and a hash of its files (taken at start-up). 0 when there are
 * none (cosmetic, modes and career mods never matter online). `names`, when given, gets them
 * ("Name 1.0, ..."). The handshake refuses a player whose signature differs. */
uint32_t    Mods_OnlineSignature(char *names, uint32_t cap);

/* The first mod that is on whose manifest says "sprites": "jaguar" (its slug), or NULL: its
 * grafs_jag/ sheets are drawn by the Jaguar tier (docs/JAGUAR.md, "Sprite sets as mods"). SDL. */
const char *Mods_JaguarSprites(void);

#endif /* MODS_H */
