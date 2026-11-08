/*
 * menu_dialog.h -- a dialog drawn by the game: a title, text and up to four buttons, on the menus'
 * backdrop with the menus' own font and buttons (src/menu_dialog.c,
 * bin/data_json/menus/openswos/menuDialog.json). SDL builds. In place of the OS's message boxes
 * wherever the menus are up: the game data's and the world's notices at start-up
 * (src/assets_check.c), OPTIONS' notes; a mod's through ModApi_ShowDialog.
 */
#ifndef MENU_DIALOG_H
#define MENU_DIALOG_H

#include <stdint.h>

enum { MENU_DIALOG_MAX_BUTTONS = 4 };

/* Shows the dialog until a button is chosen: its index (0..count-1), or -1 for ESC.
 * `text`: any case (shown in capitals: the menu font has no small letters), wrapped to the
 * screen; '|' or '\n' starts a new paragraph. Buttons stack at the bottom, in order; the
 * cursor starts on `defaultButton`. */
int32_t MenuDialog_Show(const char *title, const char *text, const char *const *buttons, int32_t count,
                        int32_t defaultButton);

#endif /* MENU_DIALOG_H */
