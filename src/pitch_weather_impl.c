/* The weather effect (third_party/pitch_weather, MIT, C4ppy77), compiled into the SDL builds whose
 * SDL has SDL_RenderGeometry (2.0.18). Its own file in third_party so it stays as it came. */
#ifdef SWOS_SDL_BUILD
#include <SDL.h>
#if SDL_VERSION_ATLEAST(2, 0, 18)
#include "../third_party/pitch_weather/pitch_weather.c"
#endif
#endif
