/*
 * save_filename.h -- how long a save's name may be (the SAVE / LOAD screens, src/file_io.c).
 *
 * The original took 8 characters and a 3-letter extension (the Amiga's and DOS's 8.3), and the
 * Amiga target still does. The SDL build takes 38, as SWOS 2020 (its kMaxSaveFilenameChars,
 * selectFiles.mnu): the owner, 2026-11-17. A name is typed without its extension (InputText never
 * takes '.' or '/'), so a buffer holds the name, ".EXT" and the terminator.
 *
 * SAVE_FILENAME_BUF(n): a filename buffer -- the SDL build's size, the original's n on the Amiga
 * (whose BSS keeps its layout).
 */
#ifndef SAVE_FILENAME_H
#define SAVE_FILENAME_H

#ifdef SWOS_SDL_BUILD
#define SAVE_FILENAME_CHARS   38
#define SAVE_FILENAME_BUF(n)  (SAVE_FILENAME_CHARS + 6)
#else
#define SAVE_FILENAME_CHARS   8
#define SAVE_FILENAME_BUF(n)  (n)
#endif

/* The save folder's listing (the SAVE / LOAD screens): entries of SAVE_LIST_ENTRY bytes -- a byte
 * pair, then the name with its extension and a terminator -- after a 4-byte count, at most
 * SAVE_LIST_MAX of them (the Amiga's: 32 bytes, 59 in its 1922). */
#ifdef SWOS_SDL_BUILD
#define SAVE_LIST_ENTRY       64
#define SAVE_LIST_MAX         256
#else
#define SAVE_LIST_ENTRY       32
#define SAVE_LIST_MAX         59
#endif
#ifdef SWOS_SDL_BUILD
#define SAVE_LIST_BYTES       (4 + SAVE_LIST_ENTRY * SAVE_LIST_MAX)
#else
#define SAVE_LIST_BYTES       1922      /* (the original's, its BSS as it was) */
#endif
#ifdef SWOS_SDL_BUILD
#define SAVE_LIST_SKIP        "16384"   /* (macOS's asm label: SAVE_LIST_BYTES - 4, written out) */
#else
#define SAVE_LIST_SKIP        "1918"
#endif

/* The original's: a name of more than this is shown in the SDL build's two wider columns. */
#define SAVE_FILENAME_SHORT   8

#endif
