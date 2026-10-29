/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_board_confidence.c — UNTANGLE step 3 (overlay retirement, pilot 5).
 *
 * The board-confidence state — split across TWO non-adjacent runs in the
 * careerFileBuffer overlay (run 1: history[0..3] + score + previous; run 2:
 * mod[0..3], value[0..1], mod[4..9]) — lifted into ONE standalone typed
 * `BoardConfidenceState` struct. The two overlay runs each become a .skip hole
 * (12 + 24 bytes) so the globals between them keep their offsets (no cascade);
 * the struct packs all 18 fields contiguously. Real labels (not .equ) so every
 * reader + the save FX(...) descriptors keep working unchanged. Audited: no
 * raw careerFileBuffer+offset, no address-of/array walks (the interleaved
 * mod/value layout is fine — they are separate named globals, not an array).
 * See project_career_overlay_extraction_recipe.
 */
#include "swos_types.h"
#include "swos_platform.h"

typedef struct {
    uint16_t boardConfidenceHistory0;   /* --- overlay run 1 --- */
    uint16_t boardConfidenceHistory1;
    uint16_t boardConfidenceHistory2;
    uint16_t boardConfidenceHistory3;
    uint16_t boardConfidenceScore;
    uint16_t previousBoardConfidence;
    uint16_t boardConfidenceMod0;       /* --- overlay run 2 --- */
    uint16_t boardConfidenceMod1;
    uint16_t boardConfidenceMod2;
    uint16_t boardConfidenceMod3;
    uint16_t boardConfidenceValue0;
    uint16_t boardConfidenceValue1;
    uint16_t boardConfidenceMod4;
    uint16_t boardConfidenceMod5;
    uint16_t boardConfidenceMod6;
    uint16_t boardConfidenceMod7;
    uint16_t boardConfidenceMod8;
    uint16_t boardConfidenceMod9;
} BoardConfidenceState;

_Static_assert(sizeof(BoardConfidenceState) == 36,
               "BoardConfidenceState must stay 36 bytes (18 x uint16)");
_Static_assert(offsetof(BoardConfidenceState, boardConfidenceHistory0) ==  0, "bc layout");
_Static_assert(offsetof(BoardConfidenceState, previousBoardConfidence) == 10, "bc layout");
_Static_assert(offsetof(BoardConfidenceState, boardConfidenceMod0)      == 12, "bc layout");
_Static_assert(offsetof(BoardConfidenceState, boardConfidenceMod9)      == 34, "bc layout");

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b). Independent globals matching the old
 * member labels' names/types/sizes; the typed base + overlay pads were unused
 * scaffolding and are dropped. Each member is &symbol-addressed in the save,
 * so the format is unchanged. */
/* The last four seasons' confidence deltas, newest first: ONE array -- the save writes them as
 * one (board_confidence_history), and four separate globals were not adjacent (the save read
 * and its load wrote whatever the linker put after the first: a career's play then depended on
 * the build's layout -- found by AddressSanitizer, 2026-10-29). Read signed (int16_t). */
uint16_t _boardConfidenceHistory[4];
uint16_t _boardConfidenceScore, _previousBoardConfidence,
         _boardConfidenceMod0, _boardConfidenceMod1, _boardConfidenceMod2,
         _boardConfidenceMod3, _boardConfidenceValue0, _boardConfidenceValue1,
         _boardConfidenceMod4, _boardConfidenceMod5, _boardConfidenceMod6,
         _boardConfidenceMod7, _boardConfidenceMod8, _boardConfidenceMod9;
