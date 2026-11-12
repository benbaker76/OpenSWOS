/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef MATCH_SETUP_H
#define MATCH_SETUP_H

#include "game_types.h"

/* Ball */
void SetBallPosition(int16_t ballX, int16_t ballY);

/* Player state resets */
void ResetBothTeamsPlayerPassingKicking(void);
void StopAllPlayers(void);

/* Referee */
void RemoveReferee(void);

/* Animated patterns */
void InitAnimatedPatterns(void);

/* Match state transitions */
void InitPlayersBeforeEnteringPitch(void);
void StartingMatch(void);
void SetCameraMovingToShowerState(void);
void EndFirstHalf(void);
void FirstHalfJustEnded(void);
void EndOfGame(void);
void PlayersLeavingPitch(void);
void GoToHalftime(void);
void GameOver(void);
void StartFirstExtraTime(void);
void EndFirstExtraTime(void);
void PrepareForInitialKick(void);
void StartPenalties(void);

/* Game restore */
void InitGameRestoreTeams(void);
/* Its pitch step alone: palette, Jaguar shadow, ball physics (the highlights viewer). */
void ApplyPitchPaletteAndFactors(void);

/* ---- launcher options (agent B) ---- */
/* SWOS 2020's improved pitch palette ([video] improved_pitch_palette): the wet and soft pitches'
 * second grass stripe lightened, so their stripes show. Palette only. */
extern uint8_t _gImprovedPitchPalette;

#endif
