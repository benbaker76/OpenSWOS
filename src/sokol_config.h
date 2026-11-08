/*
 * sokol_config.h -- which sokol_gfx backend each SDL build uses, in one place so every file that
 * includes sokol agrees (src/sokol_impl.c, src/gpu_present.c; docs/RENDERING.md).
 *
 *   Windows (Cygwin)          Direct3D 11 (src/sokol_impl_d3d11.c); `make WIN_GL=1` keeps OpenGL 4.1 core
 *   Linux                     OpenGL 4.1 core, entry points loaded through SDL (sokol_gl_loader.h)
 *   macOS                     Metal (src/sokol_impl_metal.m); `make MACOS_GL=1` keeps OpenGL 4.1 core
 *   iOS                       Metal (the same file; packaging/mobile/CMakeLists.txt)
 *   web, Android              OpenGL ES 3.0 / WebGL2, the toolchain's headers
 *   Switch                    none yet (SWOS_GPU 0): SDL's renderer, as before
 *
 * A machine whose driver will not give the context, or lacks a function sokol needs, falls back
 * to SDL's renderer at start-up (platform_sdl.c): the GPU presenter is a choice made per run.
 */
#ifndef SWOS_SOKOL_CONFIG_H
#define SWOS_SOKOL_CONFIG_H

#if defined(SWOS_SWITCH_BUILD)
#define SWOS_GPU 0
#elif defined(SWOS_IOS_BUILD)
#define SWOS_GPU 1
#define SOKOL_METAL
#elif defined(SWOS_WASM_BUILD) || defined(__EMSCRIPTEN__) || defined(SWOS_ANDROID_BUILD) || defined(__ANDROID__)
#define SWOS_GPU 1
#define SOKOL_GLES3
#elif defined(__APPLE__) && !defined(SWOS_MACOS_GL)
#define SWOS_GPU 1
#define SOKOL_METAL
#elif defined(__APPLE__)
#define SWOS_GPU 1
#define SOKOL_GLCORE
#elif defined(__CYGWIN__) && !defined(SWOS_WIN_GL)
#define SWOS_GPU 1
#define SOKOL_D3D11
#else
#define SWOS_GPU 1
#define SOKOL_GLCORE
#define SOKOL_EXTERNAL_GL_LOADER
#define SWOS_GPU_GL_LOADER 1
#endif

#if SWOS_GPU
#include "../third_party/sokol/sokol_gfx.h"
#endif

#endif
