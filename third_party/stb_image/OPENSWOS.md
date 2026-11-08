# stb_image in OpenSWOS

`stb_image.h` v2.30 (https://github.com/nothings/stb, public domain / MIT, the author's choice),
unchanged -- the same file Jungool and PaletteAgent vendor. Instantiated once by
`src/stb_image_impl.c` with PNG, BMP and JPEG only, SDL builds only (Makefile.amiga's
`SDL_ONLY_SRCS`). Used by the backgrounds mod (`src/mod_backgrounds.c`) for the menus'
true-colour backdrops.
