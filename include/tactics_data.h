/*
 * tactics_data.h — Extern declarations for tactics data
 */
#ifndef TACTICS_DATA_H
#define TACTICS_DATA_H

#include "game_types.h"

/* ======================================================================
 * TacticsPosition arrays — one per formation
 * ====================================================================== */

extern TacticsPosition _tacticsPosition442;
extern TacticsPosition _tacticsPosition541;
extern TacticsPosition _tacticsPosition451;
extern TacticsPosition _tacticsPosition532;
extern TacticsPosition _tacticsPosition352;
extern TacticsPosition _tacticsPosition433;
extern TacticsPosition _tacticsPosition424;
extern TacticsPosition _tacticsPosition343;
extern TacticsPosition _tacticsPositionSweep;
extern TacticsPosition _tacticsPosition523;
extern TacticsPosition _tacticsPositionAttack;
extern TacticsPosition _tacticsPositionDefend;
extern TacticsPosition _tacticsPositionUSERA;
extern TacticsPosition _tacticsPositionUSERB;
extern TacticsPosition _tacticsPositionUSERC;
extern TacticsPosition _tacticsPositionUSERD;
extern TacticsPosition _tacticsPositionUSERE;
extern TacticsPosition _tacticsPositionUSERF;

/* Pointer table for all 18 formation position mappings */
extern TacticsPosition *_playersPositionsByTactics[18];

/* ======================================================================
 * TeamTactics — built-in formation definitions (12 preset + 6 user)
 * ====================================================================== */

extern TeamTactics _teamTactics442;
extern TeamTactics _teamTactics541;
extern TeamTactics _teamTactics451;
extern TeamTactics _teamTactics532;
extern TeamTactics _teamTactics352;
extern TeamTactics _teamTactics433;
extern TeamTactics _teamTactics424;
extern TeamTactics _teamTactics343;
extern TeamTactics _teamTacticsSweep;
extern TeamTactics _teamTactics523;
extern TeamTactics _teamTacticsAttack;
extern TeamTactics _teamTacticsDefend;

/* User-defined tactics (BSS — zeroed at init) */
extern TeamTactics _teamTacticsUSERA;
extern TeamTactics _teamTacticsUSERB;
extern TeamTactics _teamTacticsUSERC;
extern TeamTactics _teamTacticsUSERD;
extern TeamTactics _teamTacticsUSERE;
extern TeamTactics _teamTacticsUSERF;

/* Working copies for tactics editor */
extern TeamTactics _editTacticsCurrentTactics;
extern TeamTactics _savedCurrentTactics;

/* Pointer table for all 19 tactics (18 formations + editor working copy) */
extern TeamTactics *_teamTacticsTable[19];

/* Point the four re-tuned formation slots (4-4-2, 4-3-3, 3-4-3, Sweep) at the
 * DOS release's coverage or the Amiga's. Called from ApplyGameStyle so
 * --dos-gameplay switches the formations along with the physics; re-applies
 * InitUserTactics' 5-3-2 forward-row copy, so call order does not matter.
 * A -DSWOS_DOS_TACTICS build stays pinned to the DOS set. */
void TacticsData_SelectRelease(int dos);

/* ======================================================================
 * Skill and quadrant tables
 * ====================================================================== */

/* 7x35 skill values per quadrant for player evaluation */
extern uint8_t _skillsPerQuadrant[245];

/* Skill adjustment deltas for quadrant-based processing */
extern int8_t _skillQuadrantAdjustDeltas[37];

/* Distance factor lookup for quadrant calculations (35x35 matrix, 1226 bytes) */
extern uint8_t _quadrantDistanceFactor[1226];

/* Skill ponder weights for price calculation */
extern uint32_t _priceSkillPonders[35];

/* Quadrant coordinate-to-ball-quadrant mapping */
extern int8_t _quadrantCoordinatesToBallQuadrant[240];

/* ======================================================================
 * Misc tactics-related variables
 * ====================================================================== */

/* Tactics filename buffer for import */
extern uint8_t _importTacticsFilename[];

/* Player position arrays (working buffers) */
extern uint8_t _playerPositionsArray[32];
extern uint8_t _oldPlayerPositionsArray[32];

/* Coach animation frames */
extern uint16_t _ourCoachFrames[16];
extern uint16_t _opponentCoachFrames[16];

/* Formation menu state */
extern uint16_t _formationMenuX;
extern uint16_t _selectedFormationEntry;

#endif /* TACTICS_DATA_H */
