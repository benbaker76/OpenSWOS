/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef GOALKEEPER_H
#define GOALKEEPER_H

#include "game_types.h"

/* ---- Exported functions ---- */
int16_t __attribute__((used, externally_visible)) ShouldGoalkeeperDive(Sprite *keeper, Sprite *ball, TeamGeneralInfo *team);
void    __attribute__((used, externally_visible)) GoalkeeperDeflectedBall(Sprite *ball, TeamGeneralInfo *team);
void    __attribute__((used, externally_visible)) NextPenalty(void);
void    __attribute__((used, externally_visible)) GoalkeeperClaimedTheBall(Sprite *keeper, Sprite *ball, TeamGeneralInfo *team);
void    __attribute__((used, externally_visible)) CheckIfGoalkeeperClaimedTheBall(void);
void    __attribute__((used, externally_visible)) GoalkeeperJumping(int16_t diveDir, int16_t speedVariant, int16_t destDir, Sprite *keeper, Sprite *ball, TeamGeneralInfo *team);
/* A mod's manual penalty keeper: the dive's height as the player chose it (goalkeeper.c). */
void    GoalkeeperForceDiveAnim(Sprite *keeper, TeamGeneralInfo *team, int16_t low);

#endif
