/*
 * dos_music.h -- the PC game's menu music: menu.xmi played on an emulated AdLib chip
 * with the game's own instruments (SDL builds; docs/DOS_SOUND.md, "The music").
 */
#ifndef DOS_MUSIC_H
#define DOS_MUSIC_H

#ifdef SWOS_SDL_BUILD

#include <stdint.h>

/* [audio] music / --music: 0 the Amiga's menu music, 1 the PC's. */
void    DosMusic_SetStyle(int32_t pc);
int32_t DosMusic_Style(void);
/* [audio] menu_music / --menu-music, with the PC's music: 0 menu.xmi on the emulated
 * chip, 1 a recording of it, sound_dos/menu.mp3. */
void    DosMusic_SetMenuRecording(int32_t on);
int32_t DosMusic_MenuRecording(void);

/* [audio] title_music / --title-music, with the PC's music: 0 the CD's title song,
 * sound_dos/title.mp3; 1 sound_dos/title.xmi on the emulated chip (SWOS 2020's floppy title
 * song, which a mod brings). Either falls back to the Amiga's HERO without its file. */
void    DosMusic_SetTitleMidi(int32_t on);
int32_t DosMusic_TitleMidi(void);

/* Read sound_dos/menu.xmi and sound_dos/sample.ad (once). Nonzero when the song can be played. */
int32_t DosMusic_Load(void);
/* Read sound_dos/<stem>.xmi (once; "menu" or "title"). Nonzero when it can be played. Not on
 * the audio thread. */
int32_t DosMusic_LoadSong(const char *stem);

/* The three below are called by the audio layer (src/audio_sdl.c) under its lock:
 * the song is rendered on the audio thread. */
void    DosMusic_Start(void);           /* menu.xmi from the beginning; it repeats */
void    DosMusic_StartSong(const char *stem, int32_t loop);  /* loaded first: DosMusic_LoadSong */
void    DosMusic_Stop(void);
int32_t DosMusic_Playing(void);
/* Add `frames` stereo frames at `rate` Hz to `out`, at gain/256. */
void    DosMusic_Render(int16_t *out, int32_t frames, int32_t rate, int32_t gain);

/* --dos-music-selftest: plays the song through once without sound and reports what it
 * found. Returns the number of failures. */
int32_t DosMusic_SelfTest(void);

#endif /* SWOS_SDL_BUILD */
#endif /* DOS_MUSIC_H */
