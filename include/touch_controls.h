/*
 * touch_controls.h -- the phones' on-screen controls (src/touch_controls.c, docs/MOBILE.md):
 * whether they are shown. [input] touch_controls in SWOS.ini, OPTIONS -> CONTROLS on a phone.
 */
#ifndef TOUCH_CONTROLS_H
#define TOUCH_CONTROLS_H

enum {
    TOUCH_CONTROLS_AUTO   = 0,  /* shown; gone while a game controller is played, back on a touch */
    TOUCH_CONTROLS_ALWAYS = 1,  /* shown, whatever controller is pressed */
    TOUCH_CONTROLS_NEVER  = 2,  /* never drawn: a handheld with controls of its own (a touch
                                 * where they would be still works: nobody is locked out) */
};

void TouchControls_SetMode(int mode);
int  TouchControls_Mode(void);

#endif
