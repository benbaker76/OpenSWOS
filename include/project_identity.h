/*
 * project_identity.h -- what this project is called, and how it is told apart online.
 *
 * OpenSWOS is forked downstream (SWOS United, github.com/SWOS-United): a fork changes THIS
 * file, and only this one, for its name, its online identifiers and its servers, so that
 * merging OpenSWOS into it never conflicts over them (docs/FORKING.md). Every string the
 * player sees that names the project, and every identifier another build or a server
 * compares, comes from here. Shared by the game (both targets), OpenSWOS Setup
 * (tools/swosdata) and the matchmaking server (third_party/matchmaker).
 *
 * Two builds meet online only when PROJECT_NETPLAY_TAG, PROJECT_GGPO_GAME and NETPLAY_COMPAT
 * (netplay_compat.h) agree: a fork that keeps OpenSWOS's tags plays OpenSWOS players, one
 * with its own does not.
 */
#ifndef PROJECT_IDENTITY_H
#define PROJECT_IDENTITY_H

/* The name, as the player reads it (window title, build line, messages). */
#define PROJECT_NAME            "OpenSWOS"
#define PROJECT_NAME_UPPER      "OPENSWOS"          /* (the game's menus: capitals) */
/* Where it lives (the setup's About window). */
#define PROJECT_URL             "github.com/benbaker76/OpenSWOS"
/* Its data setup (tools/swosdata): the window, and what messages tell the player to run. */
#define PROJECT_SETUP_NAME      PROJECT_NAME " Setup"
#define PROJECT_SETUP_PROGRAM   "openswos-setup"    /* (its executable, its temp folder) */

/* Online. The matchmaking server lists a game only to players with the same tag
 * (VERSION:<tag>/<NETPLAY_COMPAT>, mm_client.c; the server's Connections.cpp), the web
 * build's with -WEB after it; GGPO-X's session name; a custom team file's format. */
#define PROJECT_NETPLAY_TAG     "OPENSWOS"
#define PROJECT_GGPO_GAME       "openswos"
#define PROJECT_TEAM_FORMAT     "openswos-team"
/* The public matchmaking server (and recorder, reports, team check): its host. */
#define PROJECT_MM_HOST         "online.openswos.net"

/* The player's folder: saves, SWOS.ini, SWOS.log, the assets the setup makes (swos_io.c,
 * tools/swosdata SwosData_DefaultAssetsDir) -- Documents/<it> on Windows, the app-data
 * folder <it> on macOS and the Switch, ~/<dot dir> elsewhere. */
#define PROJECT_USER_DIR        "SWOS"
#define PROJECT_USER_DOT_DIR    ".swos"

#endif /* PROJECT_IDENTITY_H */
