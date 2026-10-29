/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_team_buffers.c — UNTANGLE step 3 / step-4 phase 4 (overlay retirement).
 *
 * The player's club team (careerTeam), the in-memory working team (currentTeam),
 * and the TMD team pool (tmdFileBuffer, TeamFileEx[128] — was [80], enlarged
 * 2026-06-28 for the Copa Libertadores qualifier; see the .skip note below) are
 * lifted out of the careerFileBuffer overlay into standalone storage. After
 * tmdFileBuffer the overlay is down to just its carrier + the careerFileBuffer
 * head + numSelectedTeams. careerTeam/currentTeam are each a TeamFileEx (16 active
 * players) followed by 16 reserve PlayerFile entries = 1292 bytes
 * (CAREER_TEAM_BUF_SIZE). Real labels (not .equ) so every reader + the save
 * (emit_player_file_body at &careerTeam / &careerTeam+684 and the currentTeam
 * equivalents) keep working unchanged. Each overlay run becomes a .skip 1292
 * hole (no cascade).
 *
 * This was only safe AFTER the step-4 rework removed both load-bearing
 * adjacencies (see docs/CAREERTEAM_STEP4_REWORK.md):
 *   - phase 1: the "is this player inside the team buffer?" range checks now
 *     bound on CAREER_TEAM_BUF_SIZE, not the next BSS symbol
 *     (careerNumPlayers / _playerPositions).
 *   - phases 2-3: DIY standings / cup brackets store a team IDENTITY (-1 ==
 *     careerTeam) resolved by a branch in diy_resolve_team(), never
 *     `&selTeamsPtr + (careerTeam offset)` — so careerTeam no longer has to
 *     live adjacent below selTeamsPtr. Functionally verified on SDL + Amiga.
 *
 * Both are &symbol-addressed everywhere (the only location references left are
 * within-buffer field accesses and == &careerTeam identity checks), so this is a
 * routine format-neutral extraction. careerTeam is declared `TeamFileEx` by some
 * consumers but the storage is the full 1292 B (684 TeamFileEx + 16 reserve
 * PlayerFile) — emitted as raw-byte labels so the typed-but-undersized externs
 * resolve correctly, exactly as when it lived in the overlay.
 */
#include "swos_types.h"
#include "swos_platform.h"
#include "game_types.h"   /* CAREER_TEAM_BUF_SIZE size lock */

_Static_assert(CAREER_TEAM_BUF_SIZE == 1292,
               "careerTeam/currentTeam buffers must stay 1292 bytes (TeamFileEx + 16 reserves)");

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM target, which rejects asm-defined data symbols —
 * docs/WASM.md §4b). Raw-byte arrays so the typed-but-undersized externs
 * ((TeamFileEx *)&_careerTeam etc.) resolve, with aligned(4) to match the old
 * .p2align 2 for the struct casts consumers do.
 *
 * _tmdFileBuffer — the TMD team pool: TeamFileEx[128] (128*684 = 87552 B),
 * lifted out of the _careerFileBuffer overlay (UNTANGLE step 3, 2026-06-10).
 * Addressed as (TeamFileEx *)&_tmdFileBuffer + i; saved as the typed `tmd_teams`
 * JSON array (same emit_team_file_ex_body path as _selectedTeams), so this is
 * save-format-neutral.
 *
 * ENLARGED 80 -> 128 (2026-06-28, Copa Libertadores Inc3b-part-2): a career with
 * the Copa qualifier active stages 20 CONMEBOL teams in addition to the 80
 * European-cup teams (16+32+32) that every season transition appends, so the pool
 * peaks at 100 (+ margin for the play-path auto-advance). The save is COUNT-DRIVEN
 * (emits/loads exactly `tmd_team_count` = _careerFileBuffer entries —
 * save_format_json.c), so this is backward-compatible: old <=80 saves load
 * unchanged; new saves carry up to 100 entries. Back-port the same size to the
 * Amiga tree. */
uint8_t _careerTeam[CAREER_TEAM_BUF_SIZE]  __attribute__((aligned(4)));
uint8_t _currentTeam[CAREER_TEAM_BUF_SIZE] __attribute__((aligned(4)));
/* A world's league phases stage 36 + 36 + 36 European clubs (+ the Copa's 20): 160 records with
 * the worlds compiled in (include/swos_types.h). Count-driven save: unchanged on disk. */
uint8_t _tmdFileBuffer[TMD_POOL_RECORDS * 684] __attribute__((aligned(4)));
