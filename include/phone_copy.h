/*
 * phone_copy.h -- copying the game's data from OpenSWOS Setup to a phone over Wi-Fi
 * (docs/MOBILE.md, "Copying over Wi-Fi"). Shared by the two ends: the setup serves
 * (tools/swosdata/app/phone_server.cpp), the phone app fetches (src/phone_fetch.c).
 *
 * The setup, while its phone screen is open, serves its assets folder over HTTP on the local
 * network, under a key it makes up each time:
 *
 *   GET /<key>/          a page for the phone's browser: "Open in OpenSWOS" -- the QR code the
 *                        setup shows is this page's address, so any camera app opens it
 *   GET /<key>/list      PHONE_COPY_LIST, then "<size> <path>" per file of the assets folder
 *   GET /<key>/f/<path>  one of them (the path %-encoded)
 *   GET /<key>/ini       the GAME OPTIONS chosen for the phone (a SWOS.ini), or 404: none
 *
 * The page's button opens the app with  <PROJECT_URL_SCHEME>://get?h=<ip>:<port>&k=<key>.
 * Without the QR code, the app asks the network: a UDP broadcast of PHONE_COPY_ASK to
 * PHONE_COPY_PORT, answered with  PHONE_COPY_HERE <port> <key> <computer's name>.
 *
 * The app copies into <assets>.part and puts it in the assets folder's place only when every
 * file came; of the phone's SWOS.ini it changes only the GAME OPTIONS keys
 * (include/game_options.h) the setup sent.
 *
 * Plain C, no includes but project_identity.h: the setup (C++) includes it too.
 */
#ifndef PHONE_COPY_H
#define PHONE_COPY_H

#include "project_identity.h"

#define PHONE_COPY_PORT      38630      /* TCP (the HTTP: the next free of 10, else any) and UDP.
                                         * Not 44620-48715: WSL2 in mirrored mode holds those on
                                         * Windows, unlisted by netsh. */
#define PHONE_COPY_ASK       PROJECT_NAME_UPPER "-ASSETS?"
#define PHONE_COPY_HERE      PROJECT_NAME_UPPER "-ASSETS!"
#define PHONE_COPY_LIST      PROJECT_NAME_UPPER "-ASSETS 1"
#define PHONE_COPY_KEY_LEN   12         /* letters and digits */

#endif
