/* The game's FindTeamInCache for the Mod's gplay.c as the drivers build it (included after
 * GPLAY_C): its 4.1 gp_champ (dev, f2a748f) looks a title play-off's winner from outside the top
 * 9 up there through gp_ftic, 32-bit assembly -- why the Mod's side builds with -m32. Here it is
 * never found (ZF clear), the Mod's own "not in the cache: as before". A 4.0.5 gplay.c names none
 * of these: defining them changes nothing there. */
u32 GP_D0, GP_D1, GP_A0;
__asm__(".globl GP_FTIC\nGP_FTIC:\n\ttestl %esp, %esp\n\tret\n");
