/*
 * mod_hooks.h -- where a built-in (source) mod's code runs (docs/MODS.md §9, §16). SDL builds.
 *
 * A built-in mod is code in the game, with a canonical manifest (bin/mods/<slug>/mod.json): its
 * hooks run only while that mod is on (Mods_IsOn). The hooks are fixed points of the engine, never
 * patches of its code, so an engine change does not break a mod. They draw or read; none may write
 * match state (a frame here is never re-simulated: it is already the one shown).
 */
#ifndef MOD_HOOKS_H
#define MOD_HOOKS_H

#ifdef SWOS_SDL_BUILD
#include <SDL.h>

/* A frame of the game is on the renderer (world, sprites and HUD), in its logical size; before
 * the CRT effects, the touch controls and the swap. platform_sdl.c's present. */
void ModHooks_FrameDrawn(SDL_Renderer *renderer);

/* A match frame's pitch is drawn into the frame being made (src/game_loop.c, after
 * ScrollToCurrent) and its sprites are not yet: what is drawn here is under the players. The
 * frame may be one GGPO re-simulates (and never shows), so a hook here only draws. */
void ModHooks_PitchDrawn(void);

/* A match's player sprites are loaded and not yet recoloured: each team's first set (team 1:
 * sprites 227..327) is the raw art (src/color_sprites.c, ConvertColorsAndTextSprites). Read only. */
void ModHooks_PlayerSetsRaw(void);

/* An outfield or goalkeeper sprite (teamNumber 1 or 2) has just been drawn into the frame being
 * made with its top left at screen x, y (src/sprites.c DrawQueuedSprites); nothing has been drawn over it yet. May redraw
 * the sprite's own pixels and draw beside it. Like PitchDrawn, possibly a re-simulated frame. */
struct Sprite;
struct SpriteGraphics;
void ModHooks_PlayerDrawn(const struct Sprite *sprite, const struct SpriteGraphics *gfx, int16_t x, int16_t y);

/* A replay frame (the in-match one, src/highlight.c DrawReplayFrame, or a .SRP's,
 * src/replay_v2.c ReplayV2_DrawFrame): its pitch is drawn into the frame and its sprites are not
 * yet -- what is drawn here is under them. `first`: the replay's first frame, or one it jumped
 * to; `fresh`: a frame not shown before (0 when paused, the frame drawn again). Then each of its
 * sprites is told, as recorded: its image and its top left in the world (ModApi_ReplayPlayerFeet
 * says whether it is a player and where his feet are). Draw only. */
void ModHooks_ReplayFrameBegin(int32_t first, int32_t fresh);
void ModHooks_ReplaySprite(uint16_t image, int16_t left, int16_t top);

/* The rest, each named for its ModDef hook (include/mod_api.h says when each runs):
 * Init (src/main.c, after Mods_Init), MenuDrawn (src/menu_system.c), MatchStart
 * (src/match_setup.c, InitGameRestoreTeams' end), MatchFrame (src/game_loop.c, after the
 * clock), MatchEvent (src/mod_match.c, from the event sites), MatchEnded (src/game_loop.c). */
struct ModMatchEvent;
struct ModMatchResult;
void ModHooks_Init(void);
void ModHooks_MenuDrawn(void);
void ModHooks_MatchStart(void);
void ModHooks_MatchFrame(void);
void ModHooks_MatchEvent(const struct ModMatchEvent *event);
void ModHooks_MatchEnded(const struct ModMatchResult *result);
#endif

#endif /* MOD_HOOKS_H */
