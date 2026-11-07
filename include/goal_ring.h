/*
 * goal_ring.h -- the match's replay ring (goalBase): every frame's camera and drawn sprites, the
 * instant replay (R) and the goal a highlights scene is copied from.
 *
 * The original's ring is 19000 bytes, a highlights scene's size (HIL_SCENE_BYTES), and filled by
 * every sprite drawn: a wider screen draws more of them a frame, so the same bytes hold fewer
 * frames -- at 484 or 672 wide 14% fewer than at 4:3 (measured on the corpus). The SDL build's
 * ring is bigger, and an instant replay goes back as far as a 4:3 screen's 19000 bytes would
 * have (highlight.c HandleInstantReplayStateSwitch): the same length at every width. A scene
 * saved to a .HIL is still the newest 19000 bytes. The Amiga's is the original's.
 */
#ifndef SWOS_GOAL_RING_H
#define SWOS_GOAL_RING_H

#define GOAL_SCENE_BYTES 19000u            /* a highlights scene; a 4:3 replay's bytes */
#ifdef SWOS_SDL_BUILD
#define GOAL_RING_BYTES  (2u * GOAL_SCENE_BYTES)   /* past 4:3's at any width (1.17x measured) */
#else
#define GOAL_RING_BYTES  GOAL_SCENE_BYTES
#endif

#endif
