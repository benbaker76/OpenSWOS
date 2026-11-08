# Rendering -- the GPU presenter

The game draws its picture as it always has: 8bpp palette indices in the frontbuffer, the HUD in
its own 8bpp layer (platform_sdl.c). Nothing about the simulation, rollback state, replays or
recordings depends on how that picture reaches the screen. This page is about that last step.

## Two presenters

| `[video] renderer` | what presents | notes |
|---|---|---|
| `accel` (default; `gpu` is the same) | the **GPU presenter** (`src/gpu_present.c`, sokol_gfx) | the 8bpp frame, the HUD layer and the palette go up as R8 / 256x1 textures; a shader colours, scales and shifts them |
| `sdl` | SDL's renderer, hardware | the path before the GPU presenter: the frame is converted to RGBA on the CPU (`chunky_to_rgba`) and copied |
| `software` | SDL's software renderer | CPU only |

`accel` falls back to SDL's renderer, said in the log as a `[gpu]` line, when:
- the build has no GPU presenter (iOS, Switch: `SWOS_GPU 0` in `src/sokol_config.h`);
- SDL gives no OpenGL window (SDL's dummy driver, used by every headless rig);
- the context (OpenGL 4.1 core, or GLES 3 / WebGL 2) cannot be made;
- the driver lacks a function sokol needs.

Old SWOS.ini files say `renderer=accel`, which is how existing players get the GPU presenter
without changing anything.

## Backends (`src/sokol_config.h`)

| target | backend |
|---|---|
| Windows (Cygwin) | Direct3D 11 (`src/sokol_impl_d3d11.c`; `-ld3d11 -ldxgi -ldxguid`, Windows 10's own DLLs, HLSL compiled by its `d3dcompiler_47.dll`); `make WIN_GL=1` keeps OpenGL 4.1 core |
| Linux | OpenGL 4.1 core; entry points loaded through `SDL_GL_GetProcAddress` (`src/sokol_gl_loader.h`), so no GL library is linked |
| macOS | Metal (`src/sokol_impl_metal.m`, Objective-C with ARC; `-framework Metal QuartzCore Foundation`); `make MACOS_GL=1` keeps OpenGL 4.1 core |
| web | WebGL 2 (`-sMAX_WEBGL_VERSION=2`) |
| Android | GLES 3 (`GLESv3` in `packaging/mobile/CMakeLists.txt`) |
| iOS | Metal, as macOS (`src/sokol_impl_metal.m` in `packaging/mobile/CMakeLists.txt`; until 2026-11-08 SDL's renderer) |
| Switch | none yet: SDL's renderer |

The web build compiles it as well (`make WASM=1`): with a 4 MB stack (glslang's parser overran
Emscripten's 64 KB) and the compiler left out of Asyncify (`ASYNCIFY_REMOVE`: instrumented, the
browser's page process died mid-compile with no message; left out, the wasm is 24 -> 15 MB).
Proved in headless Chrome (the Retro TV preset) and on a Pixel 8 (it and crt-lottes-multipass on
Mali GLES), 2026-11-08.

The phone builds compile the shader compiler too: `packaging/mobile/CMakeLists.txt` reads the
Makefile's `SHADERCC_SRCS` list out of the Makefile itself, so the two cannot drift.

`src/sokol_impl.c` compiles sokol's implementation for GL. Metal and Direct3D 11 each have a file
that compiles it for their backend and gives the presenter one native interface
(`src/gpu_native.h`: the device, the window's swapchain, the frame's target, a capture, mipmaps).
`src/sokol_impl_d3d11.c`: a feature level 11.1 (else 11.0) device, a flip-model swapchain on
SDL's window (RGBA8, resized when the window is), `Present` at the frame's end, a capture through
a staging texture, and mipmaps through a helper texture (sokol's images cannot take
`GENERATE_MIPS`). Cygwin's SDL headers are its Unix ones, so the window's `HWND` is read from
`SDL_SysWMinfo`'s union by SDL's ABI. `src/sokol_impl_metal.m`, for Metal: the device, SDL's Metal view and its
`CAMetalLayer` (BGRA8, so the presenter's default colour format is BGRA8 there; vsync is the
layer's `displaySyncEnabled`), an autorelease pool a frame, the drawable for the screen pass
(sokol presents it at the pass's end and commits at `sg_commit`), and a capture: the drawable
blitted into a shared buffer on the frame's own command buffer before the pass ends, read after the
commit completes; mipmaps with a blit encoder. sokol itself is vendored in `third_party/sokol` (see its `OPENSWOS.md`: one
change, a double release when a Metal shader fails to compile).

## Shaders

`src/shaders/present.glsl` holds two programs:
- `present`: the picture and the HUD layer;
- `batch`: textured or coloured triangles for the overlays.

`scripts/gen_shaders.sh` runs sokol-shdc over it to give `src/shaders/present.glsl.h`, which is
committed so that no build needs the tool. The languages are `glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5`.
sokol-shdc has no glsl330, which is why the desktop context is 4.1.

What the presenter reproduces from SDL's path, to the pixel:
- **the letterbox:** SDL2's logical-size rule (floor, centred), or stretch;
- **smooth scaling:** nearest-neighbour to a whole factor n, then linear resampling, done in one
  pass of four reads of the n-times picture;
- **the sub-pixel scroll:** quantised to 1/n of a source pixel, as `present_frame_copy` does;
- **the HUD layer:** unshifted, index 0 transparent;
- **the arcade's screens:** given as RGBA (ARGB8888 swizzled in the shader).

Checked against `--renderer=sdl` with `SWOS_PRESENT_CAPTURE`:
- at 1x and 3x, identical;
- fullscreen 3440x1440 with smooth scaling, within 1 level of rounding;
- with sharp scaling at a non-integer scale, one row at a texel boundary rounds the other way.

## Overlays: `include/gfx.h`

What is drawn over the picture works on either presenter through `gfx.h`:
- the weather and the flags (mods);
- the CRT mask;
- the phones' touch controls;
- the loading bar.

It offers textures, triangles, rectangles and lines, in two spaces:
- `GFX_SPACE_PICTURE`: the picture's own pixels, letterboxed or stretched as the picture is;
- `GFX_SPACE_OUTPUT`: the window's drawable pixels.

On SDL it maps onto SDL_Renderer; on the GPU presenter onto one streamed vertex buffer
(`GpuGfx_*`). New overlay drawing goes through `gfx.h`, never straight to SDL_Renderer, or it will
not show on the GPU presenter.

## Shaders from files: RetroArch's slang (the Retro TV mod)

The picture can go through a RetroArch **slang** preset (`.slangp`) -- OpenSWOS's own, or any of
libretro's -- on the GPU presenter (SDL's renderer has no shader stage: none there). RetroArch's
way, one file for every backend: `src/slang.c` reads the preset and its passes (`.slang`, Vulkan
GLSL with `#pragma stage` sections), **glslang** compiles each to SPIR-V on the player's machine,
and **SPIRV-Cross** reflects its uniforms and textures and translates it for the backend: GLSL
410, GLSL ES 300, Metal or HLSL (both libraries are submodules built by the Makefile: `SHADERCC`).
`src/gpu_slang.c` runs the passes: the game's picture, its HUD and the mods' picture-space
overlays drawn at the game's own size into `Original`; each pass into its own image (its scale
type, its format; mipmaps when the next asks), the last onto the screen in the picture's place,
then the window's overlays. Each pass gets what it reads -- Original, Source, earlier passes'
output, feedback, history, the preset's textures -- and its uniforms by meaning (MVP, the sizes,
FrameCount, the parameters). A last pass that reads its own last frame (its `PassFeedback`) is
drawn twice: into its image, the picture's size, for the next frame, and onto the screen -- the
same shader and inputs, so the same pixels.

Traps, each met: GL keeps render targets bottom up and slang takes the first row as the top, so
on GL every pass into an image (and Original) is drawn upside down; GL's blocks are real uniform
blocks bound to our own buffers (`SokolGl_*`), since sokol's GL uniforms take one type a block and
no `uint`; sRGB passes need `GL_FRAMEBUFFER_SRGB`; a preset value may carry a `#` comment. Metal
and HLSL bind by slot, not name, so every texture a pass declares needs one: a texture we do not
feed gets a slot past ours (`SLANG_MAX_TEXTURES` on), or SPIRV-Cross puts it at 0 over the one
that is there -- crt-guest-advanced's `OriginalHistory0` did (which is Original, slang's spec says;
now fed). On Metal, Original is drawn in the presenter's format (BGRA8), not RGBA8. Metal and GL
draw crt-royale's 12 passes the same, to 1 in 255 (2026-11-08, Apple M4 Pro); Direct3D 11 and GL
on one Windows machine (AMD 890M) crt-guest-advanced and the Retro TV preset to 1 in 255, royale
but for 8 pixels in 1.6 million (6 in 255: float rounding). A Metal or D3D compiler's warnings are
not logged, its errors are.

Choosing one: the **retrotv** mod (`src/mod_retrotv.c`, off by default) -- `[mod.retrotv] preset`
from its `shaders/` or the player's `Documents\SWOS\shaders` (a preset with its `.slang` files and
textures beside it, as RetroArch keeps them), `curvature` / `ntsc` setting the parameters of those
names. Its default, `openswos-tv.slangp`, is the tube's glass and our own composite-signal model
(no filter's code). For a run: `SWOS_SHADER=<preset>`; `SWOS_SHADER_PASSES=k` stops the chain at
pass k; `--slang-selftest=<preset>` compiles one for every backend. Until 2026-11-08 the curvature
and signal were a built-in pass (`[video] crt_curvature`, `crt_ntsc`): gone, the mod is it. The
mask / vignette / corners overlay (`[video] crt_mask` ...) is unchanged and works on both renderers.

## Captures

With the GPU presenter, F12, `SWOS_SCREENSHOT_INTERVAL` and `SWOS_DUMP_FRAMES` still convert the
frame on the CPU, only on the frames they capture. `SWOS_PRESENT_CAPTURE` reads the drawable back
(`glReadPixels`; on Metal and Direct3D 11, the copies described above).
