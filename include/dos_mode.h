/*
 * dos_mode.h — runtime flags for the optional DOS-version graphics/gameplay.
 *
 * The Amiga version is the definitive SWOS; these are PC-only extras. Set from
 * the [dos] section of SWOS.ini and the --dos-graphics / --dos-gameplay launch
 * options (see main.c / swos_settings.c). Game code reads these globals to pick
 * the DOS asset path; default 0 = the normal Amiga graphics/gameplay.
 *
 * gDosGraphics: load + render the DOS graphics from bin/grafs_dos.
 * gDosGameplay: use the DOS gameplay tuning instead of the Amiga one — 70 Hz
 *               game tick (DosGameplay_TargetFps) plus the DOS goalkeeper /
 *               ball-physics constants written by ApplyGameStyle (dos_gameplay.c).
 * gDosView:     render the match at the DOS release's 320x200 instead of the
 *               Amiga's 336x272 (game_loop.c MatchViewEnter). It is the SCREEN
 *               SHAPE, not the assets or the tuning, so HUD placement keys off
 *               this one: the two releases put the clock and the player-name
 *               indicator in measurably different places.
 */
#ifndef SWOS_DOS_MODE_H
#define SWOS_DOS_MODE_H

/* The gameplay style ([gameplay] style, OPTIONS -> GAMEPLAY): the tuning a match is played
 * by. AMIGA and DOS are the two releases; XBOX is SWOS 2020's (its style 4, docs/
 * DOS_GAMEPLAY.md section 9): 60 fps, the Amiga's ball physics, the DOS goalkeepers.
 * _gDosGameplay is (style == DOS) -- the DOS release's code paths, most of them nothing
 * to do with keepers; _gDosKeepers is the DOS goalkeeper's code (DOS and XBOX). All
 * three are set together by Gameplay_SetStyle (dos_gameplay.c). The Amiga build plays
 * AMIGA and DOS only: it has no 60 Hz tick. */
enum { GAMEPLAY_AMIGA = 0, GAMEPLAY_DOS = 1, GAMEPLAY_XBOX = 2, GAMEPLAY_STYLES = 3 };
extern int _gGameplayStyle;
extern int _gDosKeepers;
void Gameplay_SetStyle(int style);
const char *Gameplay_StyleName(int style);    /* "AMIGA", "DOS", "XBOX" */

extern int _gDosGraphics;
extern int _gDosGameplay;
extern int _gDosView;      /* 1 = 320x200 DOS-shaped match view; 0 on Amiga */
extern int _gWidescreen;   /* 1 = wider 16:9 pitch view (SDL); 0 on Amiga */

/* Directory the DOS assets are loaded from, relative to the working dir (bin/).
 * Default "grafs_dos"; override via [dos] directory=... or --dos-dir=... to test
 * alternate sets (e.g. a SWOS-2020 graphics folder). No trailing slash. */
extern char _gDosGraphicsDir[256];

#endif /* SWOS_DOS_MODE_H */
