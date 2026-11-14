/*
 * shirt_edit.h -- a player's shirt number edited on the screen (EDIT CUSTOM TEAMS, a career's squad screen) (src/shirt_edit.c;
 * the SWOS 96/97 Mod 4.1's edteam.py, Marco's idea).
 */
#ifndef SHIRT_EDIT_H
#define SHIRT_EDIT_H

#include "swos_types.h"

/* The team record's byte 23 (TeamFile.andWith0xFE; bit 0 is the game's training-match flag): its
 * shirt numbers are its players' own -- a line-up change swaps none (the Mod's benchnum PIN_BIT). */
#define SHIRT_NUMBERS_KEPT 0x80

/* MenuProc, after the controls are read: 1 when this frame's controls were the editor's. */
int32_t ShirtEdit_Frame(void);
/* EditTeamsPlayersBeforeDraw: the row whose number box is lit (-1 none), and its colour. */
int32_t ShirtEdit_LitRow(void);
int32_t ShirtEdit_LitSquadRow(void);   /* (SquadAfterDrawCommon: a career squad screen's lit row) */
uint16_t ShirtEdit_LitColour(void);

#endif
