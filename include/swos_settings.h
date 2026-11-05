/*
 * swos_settings.h — read/write SWOS.ini for persistent display + audio
 * settings.
 *
 * Order of operations at startup:
 *   1. main() seeds PlatformInitOptions with hard-coded defaults.
 *   2. Settings_Load() reads SWOS.ini (if present) and overrides.
 *   3. CLI parsing overrides anything the user passed on the command line.
 *   4. Platform_Init() consumes the final options.
 *
 * On clean shutdown, Settings_Save() snapshots the *current* runtime
 * state (Platform_GetWindowScale / Platform_GetFullscreenMode handle
 * the runtime-changeable bits) plus the launch-time renderer / vsync /
 * audio choice, and writes SWOS.ini back next to the binary. Missing
 * file is fine — we'll create it.
 */

#ifndef SWOS_SETTINGS_H
#define SWOS_SETTINGS_H

#include "platform_sdl.h"

/* Parse `path` (typically "SWOS.ini") and apply each known key onto
 * `*opts`. Unknown keys / sections are silently ignored. Missing or
 * unreadable file is a no-op. */
void Settings_Load(PlatformInitOptions *opts, const char *path);

/* Write ALL current settings to `path`, replacing it. Reads runtime-mutable
 * values (window scale, fullscreen mode) via the Platform_Get* getters. This is
 * a deliberate full rewrite — used when the arcade F2 service menu saves the
 * rule values the operator just changed. NOT used on normal startup (it would
 * clobber a hand-edited file); use Settings_AppendMissing for that. */
void Settings_Save(const PlatformInitOptions *opts, const char *path);

/* Non-destructive: if `path` is absent, write it in full (Settings_Save);
 * otherwise APPEND only the keys it's missing, preserving the user's values,
 * comments and ordering — and if it already has every key, leave it untouched.
 * This is the startup/shutdown path so hand-edited SWOS.ini files survive. */
void Settings_AppendMissing(const PlatformInitOptions *opts, const char *path);

/* --game-options-selftest: OpenSWOS Setup's settings (include/game_options.h) through the game's
 * reader and writer; 0 when every value comes back. */
int Settings_GameOptionsSelfTest(void);

#endif /* SWOS_SETTINGS_H */
