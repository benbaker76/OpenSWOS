/*
 * phone_fetch.h -- the phone's end of the Wi-Fi copy from OpenSWOS Setup (src/phone_fetch.c,
 * include/phone_copy.h).
 */
#ifndef PHONE_FETCH_H
#define PHONE_FETCH_H

#include <stdint.h>

typedef struct {
    char host[64];      /* its IPv4 address */
    int  port;
    char key[32];
    char name[96];      /* the computer's name (its address, from a link) */
} PhoneFetchSetup;

/* <PROJECT_URL_SCHEME>://get?h=<ip>:<port>&k=<key>: 1 and the setup, or 0. */
int PhoneFetch_ParseLink(const char *link, PhoneFetchSetup *out);

/* Asks the local network for a setup on its phone screen, up to waitMs: 1 and the first. */
int PhoneFetch_Find(PhoneFetchSetup *out, int waitMs);

/* Copies its assets folder into assetsDir (in place only once every file came) and its phone
 * GAME OPTIONS into iniPath (those keys only; NULL: not). progress(0..1000) as it goes.
 * 1, or 0 and what went wrong in err. */
int PhoneFetch_Copy(const PhoneFetchSetup *from, const char *assetsDir, const char *iniPath,
                    void (*progress)(int permille), char *err, int errCap);

/* What a copy cut off left beside the assets folder: <assets>.part removed, <assets>.old put
 * back when the assets folder is gone (the copy stopped between its renames), else removed. */
void PhoneFetch_Tidy(const char *assetsDir);

/* --phone-fetch=<link|find>: a copy, from a desktop, into swos_assets_dir() (a test). */
int PhoneFetch_Cli(const char *arg, const char *iniPath);

#endif
