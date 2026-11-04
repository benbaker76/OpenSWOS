#ifndef SWOS_AGE_IDENTITY_LOG_H
#define SWOS_AGE_IDENTITY_LOG_H
/* Career diagnostics enabled by default in SDL desktop builds.
 * Set SWOS_AGE_LOG=0 to disable. Logs contain no addresses or player names. */
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
static void swos_age_log(const char *event, const char *fmt, ...)
{
    const char *enabled = getenv("SWOS_AGE_LOG");
    if (enabled && (enabled[0] != '1' || enabled[1])) return;
    FILE *f = fopen("swos_age_identity_debug.log", "a");
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "[%lld] %s ", (long long)now, event);
    va_list args; va_start(args, fmt); vfprintf(f, fmt, args); va_end(args);
    fputc('\n', f);
    fclose(f);
}
#else
#define swos_age_log(...) ((void)0)
#endif
#endif
