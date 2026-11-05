/* This fork's mods (src/mods_builtin.c): benefactorr's Career+, the SDL build's. */
extern const ModDef ModDef_careerplus;      /* src/mod_careerplus.c */
#ifdef SWOS_SDL_BUILD
#define FORK_MOD_LIST &ModDef_careerplus,
#else
#define FORK_MOD_LIST
#endif
