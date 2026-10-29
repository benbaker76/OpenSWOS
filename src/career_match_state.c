/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_match_state.c — UNTANGLE step 3 (overlay retirement, region B3).
 *
 * The "match squad state" block — the current team's player-order translation
 * table, plus the saved training-match and last-match squad snapshots — lifted
 * out of the careerFileBuffer overlay into a standalone typed `CareerMatchState`
 * struct. Real labels (not .equ) so every reader (career_status / play_match_menu /
 * training_match / edit_tactics / menus) + the save FX(...)/FA(...) descriptors
 * keep working unchanged. The 176-byte overlay run becomes a .skip 176 hole (no
 * cascade). See project_career_overlay_extraction_recipe.
 *
 * Authoritative byte layout from SWOS2.s (BSS, ds.b):
 *     _playerPositions                   ds.b $20 (32; play-match index -> team file)
 *     numPlayers                        ds.b $2
 *     currentTeamNumber                 ds.b $2
 *     currentTeamSelectedPlayerPosition ds.b $2
 *     trainingMatchSavedPlayerPos       ds.b $2
 *     trainingMatchSavedPlayers         ds.b $20 (32)
 *     trainingMatchSavedShirtNums       ds.b $20 (32)
 *     trainingMatchSavedTactics         ds.b $1  + pad_166A27 ds.b $1
 *     lastMatchSavedPlayerPos           ds.b $2
 *     lastMatchSavedCoachOrPlayer       ds.b $2
 *     lastMatchPlayers                  ds.b $20 (32)
 *     lastMatchSavedShirtNums           ds.b $20 (32)
 *     currentTactics                    ds.b $1
 *     lastMatchSavedTeamControls        ds.b $1
 *   = 176 bytes total.
 *
 * Audited (no consumer edits): the block sits between currentTeam (deferred) and
 * careerLastMatchIndexByCompetitionSlot (already a hole). currentTeam is only
 * touched via `== &currentTeam` equality checks or `(uint8_t *)&currentTeam`
 * within its own 1292 B — never `&currentTeam + 1292` into _playerPositions. No
 * &symbol walk spans the block; every field is &symbol-addressed in the save
 * (FX scalars + FA arrays; trainingMatchSavedTactics is FA 2 FLD_BYTES covering
 * the tactics byte + pad_166A27), so the format is unchanged. The uint16 fields
 * all fall on even offsets, so the typed view needs no packing (natural 2-byte
 * alignment; offsets locked by _Static_assert).
 */
#include "swos_types.h"
#include "swos_platform.h"

typedef struct {
    uint8_t  _playerPositions[32];                /*   0 */
    uint16_t numPlayers;                         /*  32 */
    uint16_t currentTeamNumber;                  /*  34 */
    uint16_t currentTeamSelectedPlayerPosition;  /*  36 */
    uint16_t trainingMatchSavedPlayerPos;        /*  38 */
    uint8_t  trainingMatchSavedPlayers[32];      /*  40 */
    uint8_t  trainingMatchSavedShirtNums[32];    /*  72 */
    uint8_t  trainingMatchSavedTactics;          /* 104 */
    uint8_t  _pad_166A27;                        /* 105  unnamed pad */
    uint16_t lastMatchSavedPlayerPos;            /* 106 */
    uint16_t lastMatchSavedCoachOrPlayer;        /* 108 */
    uint8_t  lastMatchPlayers[32];               /* 110 */
    uint8_t  lastMatchSavedShirtNums[32];        /* 142 */
    uint8_t  currentTactics;                     /* 174 */
    uint8_t  lastMatchSavedTeamControls;         /* 175 */
} CareerMatchState;

_Static_assert(sizeof(CareerMatchState) == 176,
               "CareerMatchState must stay 176 bytes (overlay-exact)");
_Static_assert(offsetof(CareerMatchState, _playerPositions)            ==   0, "ms layout");
_Static_assert(offsetof(CareerMatchState, numPlayers)                 ==  32, "ms layout");
_Static_assert(offsetof(CareerMatchState, trainingMatchSavedPlayers)  ==  40, "ms layout");
_Static_assert(offsetof(CareerMatchState, trainingMatchSavedTactics)  == 104, "ms layout");
_Static_assert(offsetof(CareerMatchState, lastMatchSavedPlayerPos)    == 106, "ms layout");
_Static_assert(offsetof(CareerMatchState, lastMatchPlayers)           == 110, "ms layout");
_Static_assert(offsetof(CareerMatchState, lastMatchSavedTeamControls) == 175, "ms layout");

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b). Independent globals matching the old
 * member labels' names/types/sizes; the typed base + overlay pads were unused
 * scaffolding and are dropped. Each member is &symbol-addressed in the save,
 * so the format is unchanged. */
uint8_t  _playerPositions[32];
uint16_t _numPlayers, _currentTeamNumber, _currentTeamSelectedPlayerPosition,
         _trainingMatchSavedPlayerPos;
uint8_t  _trainingMatchSavedPlayers[32];
uint8_t  _trainingMatchSavedShirtNums[32];
uint8_t  _trainingMatchSavedTactics[2];        /* the tactics byte, then the original's pad
                                                 * (SWOS2.s pad_166A27): the save writes both;
                                                 * one byte read its neighbour's (ASan, 2026-10-29) */
uint16_t _lastMatchSavedPlayerPos, _lastMatchSavedCoachOrPlayer;
uint8_t  _lastMatchPlayers[32];
uint8_t  _lastMatchSavedShirtNums[32];
uint8_t  _currentTactics;
uint8_t  _lastMatchSavedTeamControls;
