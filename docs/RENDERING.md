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
| Windows (Cygwin), Linux | OpenGL 4.1 core; entry points loaded through `SDL_GL_GetProcAddress` (`src/sokol_gl_loader.h`), so no GL library is linked |
| macOS | OpenGL 4.1 core (`-framework OpenGL`); Metal is the planned next step |
| web | WebGL 2 (`-sMAX_WEBGL_VERSION=2`) |
| Android | GLES 3 (`GLESv3` in `packaging/mobile/CMakeLists.txt`) |

`src/sokol_impl.c` is the one file that compiles sokol's implementation. sokol itself is
vendored in `third_party/sokol` (see its `OPENSWOS.md`).

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

## Captures

With the GPU presenter, F12, `SWOS_SCREENSHOT_INTERVAL` and `SWOS_DUMP_FRAMES` still convert the
frame on the CPU, only on the frames they capture. `SWOS_PRESENT_CAPTURE` reads the drawable back
(`glReadPixels`).
