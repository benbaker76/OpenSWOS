/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_index_tables.c — UNTANGLE step 3 (overlay retirement, region B2).
 *
 * Three independent, self-contained career lookup/index tables lifted out of the
 * careerFileBuffer overlay into standalone storage. They live in different parts
 * of the overlay, so each is carved as its own .skip hole; they share this file
 * only for economy. Real labels (not .equ) so every reader + the save FA(...)
 * descriptors keep working unchanged. See project_career_overlay_extraction_recipe.
 *
 *   teamBudgetRankTable                  128 B  (uint8; diy_competition rank-by-budget)
 *   careerLastMatchIndexByCompetitionSlot 256 B (uint8; 0xFF-filled, one per compId)
 *   seasonSquadTeamNumbers                48 B  (uint16[24]; squad team numbers)
 *
 * Audited (no consumer edits, all &symbol-addressed FA blobs → format unchanged):
 *  - teamBudgetRankTable[idx] (idx < team count) — within 128 B.
 *  - careerLastMatchIndexByCompetitionSlot — base ptr, 0xFF fill, indexed
 *    by compId (the original had 162: its ids).
 *  - seasonSquadTeamNumbers — uint16 walked up to seasonNumberOfSquads (<= 24) —
 *    within 48 B. In SWOS2.s its ds.b $30 (48) is followed by
 *    careerCompetitionSlotPtrTable (ds.b $14 = 20), which the overlay folded into
 *    a single .skip 68. careerCompetitionSlotPtrTable is now a SEPARATE standalone
 *    symbol (uint8_t*[5], re-derived on load), so those 20 bytes are dead overlay
 *    space; we reserve seasonSquadTeamNumbers as 48 here and leave the overlay's
 *    full 68-byte run as a hole (no cascade).
 *
 * Single standalone arrays (no sub-field aliasing). Pure C (was an __asm__ .skip
 * block; converted for the WASM target, which rejects asm-defined data symbols —
 * docs/WASM.md §4b). Each is defined with its true element type here, which gives
 * correct natural alignment (uint16 -> 2-byte) for free; the conflicting
 * element-type externs in OTHER TUs (uint8_t[] vs uint16_t[]) stay link-compatible
 * (C does no cross-TU type check on globals).
 */
#include "swos_types.h"
#include "swos_platform.h"

uint8_t  _teamBudgetRankTable[128];
uint8_t  _careerLastMatchIndexByCompetitionSlot[256];   /* one per compId (was 162: see below) */
uint16_t _seasonSquadTeamNumbers[24];

_Static_assert(sizeof(_teamBudgetRankTable) == 128, "teamBudgetRankTable 128 B");
/* The original's 162 bytes covered its compIds 0..161; a data-only competition
 * (the Copa, 162) or a world's play-off (200) indexes it by its own id, which ran
 * past the end into _teamBudgetRankTable until 2026-10-27. */
_Static_assert(sizeof(_careerLastMatchIndexByCompetitionSlot) == 256,
               "careerLastMatchIndexByCompetitionSlot: one byte per compId");
_Static_assert(sizeof(_seasonSquadTeamNumbers) == 48, "seasonSquadTeamNumbers 48 B");
