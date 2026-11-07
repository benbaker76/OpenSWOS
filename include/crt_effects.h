/*
 * crt_effects.h — CRT-style presentation effects (SDL only).
 *
 * A modern panel draws SWOS's 336x272 as huge, razor-sharp squares. These
 * effects put back what an arcade/TV tube did to the picture: a shadow-mask
 * grille in the mid-tones, a darkened surround, and a rounded glass edge.
 *
 * Approach: ONE pre-built texture, multiplied over the finished frame with
 * SDL_BLENDMODE_MOD. The mask, the vignette and the corner shape all fold into
 * the same texture's RGB, so the per-frame cost is a single full-screen draw no
 * matter how many effects are on — the same trick CannonBall-SE uses to hold
 * 60 fps on a Pi2 without a shader (docs/CannonBall-SE-Screen-Filters.txt).
 *
 * The texture is built at OUTPUT resolution so the grille lands on whole
 * screen pixels; it is rebuilt only when the output size or a setting changes.
 */
#ifndef SWOS_CRT_EFFECTS_H
#define SWOS_CRT_EFFECTS_H

#ifdef SWOS_SDL_BUILD

/* <SDL.h>, NOT <SDL2/SDL.h>: the Switch SDL fork has no SDL2/ prefix on its
 * include path, and this is the convention platform_sdl.h already follows. */
#include <SDL.h>
#include <stdint.h>

/* [video] crt_mask */
typedef enum {
    CRT_MASK_OFF       = 0,
    CRT_MASK_APERTURE  = 1,  /* vertical RGB grille — Trinitron-ish */
    CRT_MASK_SLOT      = 2,  /* aperture + row-staggered slots — shadow mask */
    CRT_MASK_SCANLINES = 3,  /* horizontal scanlines only */
} CrtMaskKind;

typedef struct {
    CrtMaskKind mask;
    int mask_strength;   /* 0..100 — how dark the grille's off-phase gets */
    int vignette;        /* 0..100 — corner darkening, 0 = off */
    int corners;         /* 0..100 — rounded-glass corner size, 0 = square */
} CrtEffectsConfig;

/* Set the active configuration. Cheap; the texture rebuild is deferred to the
 * next CrtEffects_Apply so several setters cost nothing extra. */
void CrtEffects_Configure(const CrtEffectsConfig *cfg);

/* True when anything is enabled — lets the caller skip the draw entirely. */
int  CrtEffects_Active(void);

/* Multiply the effects over whatever has just been drawn. Call AFTER the frame
 * (and its HUD) and BEFORE SDL_RenderPresent. Draws in OUTPUT pixels, so it is
 * unaffected by the renderer's logical size. No-op when nothing is enabled. */
void CrtEffects_Apply(void);   /* over the finished frame, in output pixels (gfx.h) */

/* Drop the cached texture (renderer teardown). */
void CrtEffects_Shutdown(void);

#endif /* SWOS_SDL_BUILD */
#endif /* SWOS_CRT_EFFECTS_H */
