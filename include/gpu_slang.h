/*
 * gpu_slang.h -- a slang preset's passes on the GPU presenter (src/gpu_slang.c; the compiler is
 * src/slang.c). SDL builds with SWOS_SHADERCC and the GPU presenter.
 */
#ifndef SWOS_GPU_SLANG_H
#define SWOS_GPU_SLANG_H

#include <stdint.h>

/* Load a preset (its passes compiled for this backend, its textures read); 0 and a log line when
 * it cannot be (the game's own picture then). NULL / "" unloads. */
int32_t GpuSlang_Load(const char *preset);
void    GpuSlang_Unload(void);
int32_t GpuSlang_Active(void);
/* A preset parameter's value (its #pragma parameter name), over the preset's own: kept for presets
 * loaded later, clamped to the parameter's range; a name the preset lacks is left alone. */
void    GpuSlang_SetParameter(const char *name, float value);

#if defined(SWOS_SDL_BUILD) && defined(SWOS_SHADERCC)
#include "sokol_config.h"
#if SWOS_GPU
/* This frame's Original, w x h (the game's picture): the attachment to draw it into -- the first
 * row its top (slang's way: on GL the presenter draws it upside down). */
sg_view GpuSlang_BeginOriginal(int w, int h);
/* Every pass, the last onto the screen in (vx, vy, vw, vh) after beginScreen() began its pass. */
void    GpuSlang_Run(float vx, float vy, float vw, float vh, void (*beginScreen)(void));
#endif
#endif

#endif
