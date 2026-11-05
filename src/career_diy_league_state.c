/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_diy_league_state.c — UNTANGLE step 3 (overlay retirement, region B5).
 *
 * The DIY-league per-season state table (diyLeagueSeasonStateTable, a
 * 2000-byte per-global-team league-offset table) plus the adjacent
 * lastFinancialWeekIndex byte, lifted out of the careerFileBuffer overlay into
 * standalone storage. Real labels (not .equ) so every reader (career_init /
 * diy_competition / teams) + the save FX/FA descriptors keep working unchanged.
 * The 2002-byte overlay run becomes a .skip hole (no cascade). See
 * project_career_overlay_extraction_recipe.
 *
 * Layout from SWOS2.s (BSS, ds.b):
 *     lastFinancialWeekIndex  ds.b $1  (uint8, FLD_U8) + pad_165CE1 ds.b $1
 *     diyLeagueSeasonStateTable ds.b $7D0 (2000; indexed by global team number)
 *   = 2002 bytes.
 *
 * NOTE: this diyLeagueSeasonStateTable is NOT the in-competition league
 * standings that clearLeagueTableStats overlays onto cupTeamIds /
 * teamGridPairings / audioInstrumentBank — that overlay lives inside the
 * separate diyCompetitionStart (DIYCompetitionEx) buffer (see
 * project_diycomp_league_table_overlay). This is a standalone career-level
 * table; SWOS2.s addresses it only via `lea (diyLeagueSeasonStateTable).l`
 * (&symbol), never base+offset of another global.
 *
 * Audited (no consumer edits): diyLeagueSeasonStateTable is accessed only as
 * &symbol — bounded index diyLeagueSeasonStateTable[globalTeamNo], a 2000-byte
 * clear, and a 2000-byte save/restore copy; lastFinancialWeekIndex is a plain
 * scalar assign/read (no &-address, no range-bound use). Both are
 * &symbol-addressed in the save (FA diy_league_season_state_table 2000
 * FLD_BYTES; FX last_financial_week_index FLD_U8), so the format is unchanged.
 * lastFinancialWeekIndex is grouped here only as the contiguous neighbour, for
 * a single overlay hole.
 */
#include "swos_types.h"
#include "swos_platform.h"

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b). Independent globals; the old 1-byte overlay
 * pad between them is not relied on (both are &symbol-addressed in the save). */
uint8_t _lastFinancialWeekIndex;
uint8_t _diyLeagueSeasonStateTable[GLOBAL_TEAM_NUMBERS];   /* (the original's 2000: game_types.h) */
_Static_assert(sizeof(_diyLeagueSeasonStateTable) == GLOBAL_TEAM_NUMBERS,
               "diyLeagueSeasonStateTable: one byte per global team number");
