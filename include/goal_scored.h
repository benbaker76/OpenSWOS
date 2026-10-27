/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef GOAL_SCORED_H
#define GOAL_SCORED_H

#include "game_types.h"

/* ---- GoalScored ----
 * Updates goal counters and builds scorer list sprites.
 * scoringTeam: 1 or 2
 * scorerSprite: pointer to in-game Sprite struct of the scorer
 */
void GoalScored(int16_t scoringTeam, Sprite *scorerSprite);

/* ---- RebuildScorerSprites ----
 * Redraws the scorer-banner sprites from the scorer tables, for a loaded save state:
 * the tables travel in the state registry, the pixels do not. Layout is taken as
 * restored (numSpritesTaken, imageIndexes), never re-allocated.
 */
void RebuildScorerSprites(void);

#endif
