/*
 * live_cast.h -- LIVE CAST: watching a live online match from the recorder (src/live_cast.c,
 * include/record_proto.h RECORD_WATCH). SDL builds with netplay.
 */
#ifndef LIVE_CAST_H
#define LIVE_CAST_H

#include "swos_types.h"

/* --watch=<host[:port]>[,<id>] (list 0) or --watch-list=<host[:port]> (list 1): 0 = taken. */
int32_t LiveCast_Request(const char *spec, int32_t list);
int32_t LiveCast_Armed(void);
/* After the game's data is loaded: connects, and for a list prints it and exits; to watch,
 * waits for the match's first frames and writes them as the .SRP the viewer opens (its name
 * into viewPath). 0 = ready to view. */
int32_t LiveCast_Run(char *viewPath, uint32_t cap);
/* The replay viewer, each frame: the frames that arrived since (ReplayV2_LiveAppend). */
void    LiveCast_Pump(void);

#endif
