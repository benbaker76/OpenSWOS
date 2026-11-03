/*
 * project_identity.h -- what this project is called, and how it is told apart online.
 *
 * A fork of OpenSWOS (SWOS United, github.com/SWOS-United, is one) changes THIS file, and only
 * this one, for its name, its online identifiers and its servers, so that a fork which merges
 * OpenSWOS's later changes never conflicts over them (docs/FORKING.md). Every string the
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

/* The phone apps (packaging/android, packaging/mobile/CMakeLists.txt; docs/MOBILE.md): the Android
 * application id and the iOS bundle id -- an app's identity in its store and on the phone, so a
 * fork's own apps install beside OpenSWOS's. Read from here by the mobile build. */
#define PROJECT_APP_ID          "net.openswos.game"

/* The phone apps' link scheme: OpenSWOS Setup's Wi-Fi copy opens the app with
 * <scheme>://get?h=<address>&k=<key> (include/phone_copy.h). A fork's own, so a link opens its app.
 * Lower case, letters only. */
#define PROJECT_URL_SCHEME      "openswos"

/* The project's public site (packaging/web/site): the Wi-Fi copy's QR code is
 * https://<site>/get/#h=<address>&k=<key> -- a page any phone's camera opens without reaching the
 * local network (Android 17 lets a browser reach it only with a permission), whose button opens
 * the app; on Android it opens the app directly (an App Link: <site>/.well-known/assetlinks.json). */
#define PROJECT_SITE            "openswos.net"

/* SWOS 2020's online-menu entries for features this project will not have -- the log-in's NEWS,
 * MULTIPLAYER SETTINGS' commentary and chat rows, JOIN AMIGA SUPER LEAGUE, JOIN DISCORD, HELP
 * DESK (src/online_menus.c): 1 hides them, 0 keeps them for a fork that builds them. */
#define PROJECT_HIDE_SWOS2020_EXTRAS 1

#endif /* PROJECT_IDENTITY_H */
