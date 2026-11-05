/*
 * mod_hooks.c -- the engine's side of the built-in mods' hooks (include/mod_hooks.h): each hook
 * point calls every mod that is on and has that hook (include/mod_api.h ModDef, the list in
 * src/mods_builtin.c). docs/MODS.md §16.
 */
#include "mod_hooks.h"

#ifdef SWOS_SDL_BUILD

#include "game_types.h"
#include "mod_api.h"
#include "mods.h"

#include "mjson.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const ModDef *const g_builtinMods[];
extern void ModApi_NotePresent(void);       /* src/mod_api.c: what ModApi_InReplay counts by */
extern void ModApi_NoteReplayFrame(void);
extern int32_t ModMatch_Quiet(void);        /* src/mod_match.c: a re-simulated frame */

/* every mod that is on, with that hook */
#define EACH(hook)                                                                        \
    for (const ModDef *const *m_ = g_builtinMods; *m_; m_++)                             \
        if ((*m_)->hook && Mods_IsOn((*m_)->slug))

void ModHooks_Init(void)
{
    EACH(init) (*m_)->init();
}

void ModHooks_FrameDrawn(SDL_Renderer *renderer)
{
    ModApi_NotePresent();
    EACH(frameDrawn) (*m_)->frameDrawn(renderer);
}

void ModHooks_PitchDrawn(void)
{
    EACH(pitchDrawn) (*m_)->pitchDrawn();
}

void ModHooks_PlayerSetsRaw(void)
{
    EACH(playerSetsRaw) (*m_)->playerSetsRaw();
}

void ModHooks_PlayerDrawn(const Sprite *sprite, const SpriteGraphics *gfx, int16_t x, int16_t y)
{
    EACH(playerDrawn) (*m_)->playerDrawn(sprite, gfx, x, y);
}

void ModHooks_ReplayFrameBegin(int32_t first, int32_t fresh)
{
    ModApi_NoteReplayFrame();
    EACH(replayFrameBegin) (*m_)->replayFrameBegin(first, fresh);
}

void ModHooks_ReplaySprite(uint16_t image, int16_t left, int16_t top)
{
    EACH(replaySprite) (*m_)->replaySprite(image, left, top);
}

void ModHooks_MenuDrawn(void)
{
    EACH(menuDrawn) (*m_)->menuDrawn();
}

void ModHooks_MatchStart(void)
{
    EACH(matchStart) (*m_)->matchStart();
}

void ModHooks_MatchFrame(void)
{
    if (ModMatch_Quiet())
        return;
    EACH(matchFrame) (*m_)->matchFrame();
}

void ModHooks_MatchEvent(const ModMatchEvent *event)
{
    if (ModMatch_Quiet())
        return;
    EACH(matchEvent) (*m_)->matchEvent(event);
}

void ModHooks_MatchEnded(const ModMatchResult *result)
{
    EACH(matchEnded) (*m_)->matchEnded(result);
}

/* ---- the career (docs/MODS.md §27) ---- */

void ModHooks_CareerNew(void)
{
    EACH(careerNew) (*m_)->careerNew();
}

void ModHooks_CareerSeasonEnd(uint16_t season)
{
    EACH(careerSeasonEnd) (*m_)->careerSeasonEnd(season);
}

void ModHooks_CareerBeforeContinue(void)
{
    EACH(careerBeforeContinue) (*m_)->careerBeforeContinue();
}

void ModHooks_CompetitionFinished(uint16_t season, uint8_t competitionId, uint8_t kind, uint8_t division,
                                  uint16_t winner)
{
    const ModCompetitionResult r = { season, competitionId, kind, division, winner };
    EACH(competitionFinished) (*m_)->competitionFinished(&r);
}

void ModHooks_PlayerMoved(uint16_t fromTeam, uint8_t fromSlot, uint16_t toTeam, uint8_t toSlot)
{
    const ModPlayerMove m = { fromTeam, toTeam, fromSlot, toSlot };
    EACH(playerMoved) (*m_)->playerMoved(&m);
}

void ModHooks_PlayerCreated(uint16_t team, uint8_t slot, uint8_t kind, PlayerFile *player)
{
    const ModPlayerCreated c = { team, slot, kind, player };
    EACH(playerCreated) (*m_)->playerCreated(&c);
}

/* careerSave's writer: a growing buffer */
struct ModJsonWriter {
    char   *buf;
    size_t  len, cap;
    int32_t oom;
};

void ModJson_Printf(ModJsonWriter *out, const char *fmt, ...)
{
    if (!out || out->oom)
        return;
    va_list ap;
    va_start(ap, fmt);
    char small[256];
    va_list ap2;
    va_copy(ap2, ap);
    const int n = vsnprintf(small, sizeof small, fmt, ap2);
    va_end(ap2);
    if (n < 0) {
        va_end(ap);
        out->oom = 1;
        return;
    }
    if (out->len + (size_t)n + 1 > out->cap) {
        size_t cap = out->cap ? out->cap : 1024;
        while (out->len + (size_t)n + 1 > cap)
            cap *= 2;
        char *nb = (char *)realloc(out->buf, cap);
        if (!nb) {
            va_end(ap);
            out->oom = 1;
            return;
        }
        out->buf = nb;
        out->cap = cap;
    }
    if ((size_t)n < sizeof small)
        memcpy(out->buf + out->len, small, (size_t)n + 1);
    else
        vsnprintf(out->buf + out->len, (size_t)n + 1, fmt, ap);
    out->len += (size_t)n;
    va_end(ap);
}

char *ModHooks_CareerSaveSection(void)
{
    ModJsonWriter all = { 0 };
    int32_t any = 0;
    EACH(careerSave) {
        ModJsonWriter one = { 0 };
        (*m_)->careerSave(&one);
        if (one.oom) {
            fprintf(stderr, "[mods] %s: its career section ran out of memory; not saved\n", (*m_)->slug);
        } else if (one.len) {
            ModJson_Printf(&all, "%s\"%s\":{%s}", any ? "," : ",\n  \"mods\":{", (*m_)->slug, one.buf);
            any = 1;
        }
        free(one.buf);
    }
    if (any)
        ModJson_Printf(&all, "}");
    if (!any || all.oom) {
        free(all.buf);
        return NULL;
    }
    return all.buf;
}

void ModHooks_CareerLoadSection(const char *json, int32_t len)
{
    EACH(careerLoad) {
        char path[96];
        const char *obj = NULL;
        int objLen = 0;
        snprintf(path, sizeof path, "$.mods.%s", (*m_)->slug);
        if (json && mjson_find(json, (int)len, path, &obj, &objLen) == MJSON_TOK_OBJECT)
            (*m_)->careerLoad(obj, (int32_t)objLen);
        else
            (*m_)->careerLoad(NULL, 0);
    }
}

#endif /* SWOS_SDL_BUILD */
