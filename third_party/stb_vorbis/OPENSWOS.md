# stb_vorbis in OpenSWOS

`stb_vorbis.c` v1.22 (https://github.com/nothings/stb at f1c79c0, public domain / MIT, the
author's choice), unchanged. Instantiated once by `src/stb_vorbis_impl.c`, SDL builds only
(Makefile.amiga's `SDL_ONLY_SRCS`); `src/dos_sound.c` includes it with `STB_VORBIS_HEADER_ONLY`
and decodes whole files from memory (`stb_vorbis_decode_memory`): a mod's `.ogg` commentary and
PC sound effects (docs/MODS.md §10.3), mixed to mono as the PC sound plays everything.
