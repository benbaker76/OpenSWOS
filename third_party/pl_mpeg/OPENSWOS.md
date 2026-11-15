# pl_mpeg in OpenSWOS

The MPEG-1 video decoder of the PC cutscenes' upscaled clips (`src/mod_dosvideos.c`,
docs/DOS_VIDEO.md "The upscaled clips"): a clip's `video_dos/hd/<NAME>.mpg` played in place of
its `.FLC`.

| | |
|---|---|
| upstream | https://github.com/phoboslab/pl_mpeg |
| version | commit `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023` (2025-12-30) |
| licence | MIT (`SPDX-License-Identifier: MIT` in the file, Dominic Szablewski); the text in `LICENSE` |
| files taken | `pl_mpeg.h` — **unmodified** |
| sha256 | `3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9` |

Compiled once, by `src/pl_mpeg_impl.c` (`PL_MPEG_IMPLEMENTATION`, `PLM_NO_STDIO`: the file is
read by the game and decoded from memory). SDL builds only (Makefile.amiga's `SDL_ONLY_SRCS`).
