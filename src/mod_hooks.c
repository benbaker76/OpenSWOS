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

#include <stddef.h>

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

#endif /* SWOS_SDL_BUILD */
