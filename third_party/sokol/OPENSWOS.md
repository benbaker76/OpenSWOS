# sokol (vendored)

`sokol_gfx.h` and `sokol_log.h` from [floooh/sokol](https://github.com/floooh/sokol), zlib/libpng
license (at the end of each header). Taken as they are from the Jungool project
(`D:\Projects\GitHub\Jungool\third_party\sokol`, 2026-08-26), whose sokol-shdc matches them: the
shader header sokol-shdc writes follows sokol_gfx's own structures, so the two are updated
together -- never one without the other.

Used by the GPU presenter (`src/gpu_present.c`, `src/sokol_impl.c`, docs/RENDERING.md): the
game's 8bpp frame and its palette go to the GPU, which colours, scales and composites them.
How they are built (one change, below):

- Desktop OpenGL (Linux; Windows with `WIN_GL=1`): `SOKOL_GLCORE` with `SOKOL_EXTERNAL_GL_LOADER` -- the GL
  entry points are loaded through SDL (`SDL_GL_GetProcAddress`, src/sokol_gl_loader.h), so no GL
  header of the system and no GL library is linked (Cygwin has neither the Windows one sokol's
  own loader wants nor a native one).
- Windows (Cygwin): `SOKOL_D3D11` (`src/sokol_impl_d3d11.c`); `make WIN_GL=1` keeps OpenGL.
- macOS: `SOKOL_METAL` (`src/sokol_impl_metal.m`, Objective-C with ARC); `make MACOS_GL=1` keeps
  `SOKOL_GLCORE`, sokol's own `<OpenGL/gl3.h>` path.
- Web, Android: `SOKOL_GLES3` (WebGL2 / OpenGL ES 3.0), the toolchain's headers.

Shaders: `src/shaders/*.glsl`, compiled by `scripts/gen_shaders.sh` (sokol-shdc, the same
version as these headers) into the committed `src/shaders/*.glsl.h`.

## Our one change

`_sg_mtl_create_shader`: when a function fails to compile (a slang pass the Metal compiler refuses,
src/gpu_slang.c), the functions it did make are released -- and their slot indices were left set,
so `sg_destroy_shader` released them a second time and sokol asserted in
`_sg_mtl_garbage_collect`. The indices are now zeroed (`_SG_MTL_INVALID_SLOT_INDEX`) after the
release; marked `OPENSWOS:`. Carry it forward when sokol is updated, unless upstream has fixed it.
