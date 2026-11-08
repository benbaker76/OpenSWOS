/* stb_image_impl.c -- the one translation unit that instantiates stb_image (third_party/stb_image,
 * public domain): the backgrounds mod's pictures. PNG, BMP and JPEG only. SDL only. */
#ifdef SWOS_SDL_BUILD
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#include "../third_party/stb_image/stb_image.h"
#endif
