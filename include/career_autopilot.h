/*
 * career_autopilot.h -- a career that plays itself, for the golden career test
 * (docs/WORLD_2026_27.md, phase 0; scripts/career_golden_test.sh).
 *
 *   --career-golden=<save.CAR>|new:<continent>/<country>/<division>/<club>
 *                   [--golden-seasons=N] [--golden-out=DIR]
 *
 * Loads the career from DIR (the save folder is pointed there, so a player's own saves
 * are never touched), then presses the button a player would on every career screen,
 * with every match of the career club simulated. At each season-end hub it writes the
 * game's own JSON career save (SEASON00.CAR at the first hub, then one per season
 * played), then continues; after N seasons it exits.
 * The clock that seeds the game's random numbers advances once per menu pass, and the
 * game tick once per frame from the main thread (the timer thread is quiet), so the same
 * career plays out the same way every run. A sacked manager takes the first job offer.
 * SWOS_GOLDEN_TRACE=1 prints each new screen's onInit and selection (addresses: resolve
 * them with nm against the SWOSMainMenuInit address the first line prints).
 */
#ifndef CAREER_AUTOPILOT_H
#define CAREER_AUTOPILOT_H

#include <stdint.h>

/* main.c: 1 if `a` was one of the flags above (consumed), 0 if not, -1 on a bad value. */
int32_t CareerAutopilot_ParseArg(const char *a);
int32_t CareerAutopilot_Active(void);

/* menu_system.c, once per menu pass, after the controls are read. */
void CareerAutopilot_Tick(void);

/* interrupts.c: the autopilot advances _currentTick itself. */
int32_t CareerAutopilot_OwnsClock(void);

#endif
