/*
 * sd_json.c -- the game's own JSON code in the setup: src/mjson.c and src/challenge_pack.c
 * (with its JSON reader), built here as one file so every build of the setup (the Makefile,
 * CMake, the MSVC solution) takes them without a list of OpenSWOS sources to keep in step.
 * OpenSWOS's include/ is on the setup's include path AFTER its own folders, for their headers.
 */
#include <stdlib.h>

#define MJSON_REALLOC(p, n) realloc((p), (size_t)(n))
#ifndef CHALLENGE_PACK_JSON
#define CHALLENGE_PACK_JSON 1
#endif

#include "../../../src/mjson.c"
#include "../../../src/challenge_pack.c"
