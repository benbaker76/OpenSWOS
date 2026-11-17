/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_season_info.c — UNTANGLE step 3 (overlay retirement, region B4).
 *
 * The per-season club history table, seasonInformationsArray[20]
 * (SeasonInformations), lifted out of the careerFileBuffer overlay into
 * standalone storage. Real label (not .equ) so every reader (statistics /
 * job_offers / menus) + the save's semantic season-info encoder keep working
 * unchanged; the overlay run becomes a conditional .skip hole (no cascade).
 * See project_career_overlay_extraction_recipe.
 *
 * Each SeasonInformations row is 106 bytes on 32-bit and 122 bytes on 64-bit
 * (it has 4 comp-name pointer fields — leagueStrPtr / strPtr1..3 — that widen
 * from 4 to 8 bytes), so the array is 20*106 = 2120 B (32-bit) / 20*122 = 2440 B
 * (64-bit). The reservation keeps the same conditional +320 (= 20*16) pad the
 * overlay used; the _Static_assert below locks the reservation to
 * 20*sizeof(SeasonInformations) on both targets.
 *
 * Format / VERIFY: the save serialises this array via emit_season_informations,
 * addressed by &seasonInformationsArray (the 4 pointer slots are emitted as
 * re-interned string pool content), so relocating the storage is format-neutral.
 * Those 4 pointer-to-disk slots used to be the ONLY bytes career_verify_ignore
 * exempted from the byte-diff; now that the array lives outside the
 * [careerFileBuffer, numSelectedTeams) VERIFY region, there is nothing left to
 * exempt — the verify call drops the ignore predicate (passes NULL) and
 * career_verify_ignore is retired (see save_format_json.c). The overlay hole
 * stays zero in both the live image and the reload, so VERIFY remains 0.
 *
 * Audited: all access is bounded row-addressing — seasonInformationsArray + off
 * (job_offers / statistics) and ((SeasonInformations *)…)[currentSeasonNumber]
 * (currentSeasonNumber < CAREER_MAX_SEASONS = 40, bounded by currentSeasonShowing) —
 * nothing walks past the array into
 * lastFinancialWeekIndex. Pure C (was an __asm__ .skip block; converted for the
 * WASM target, which rejects asm-defined data symbols — docs/WASM.md §4b).
 * Defining it with the real SeasonInformations[] element type makes the
 * 32-bit (106 B/row) vs 64-bit (122 B/row) reservation automatic — no
 * conditional .skip — and gives correct natural alignment. The conflicting
 * element-type externs in OTHER TUs (uint8_t[] vs SeasonInformations*) stay
 * link-compatible (C does no cross-TU type check on globals).
 */
#include "swos_types.h"
#include "swos_platform.h"
#include "game_types.h"   /* SeasonInformations — for the size lock below */
#include "career_params.h"   /* CAREER_MAX_SEASONS */

/* CAREER_MAX_SEASONS (40) seasons of history; num_seasons (career_params: CAREER YEARS, 20 / 30
 * / 40) is the career's. The array is sized to the max. */
_Static_assert(sizeof(SeasonInformations) * CAREER_MAX_SEASONS ==
               (UINTPTR_MAX != 0xFFFFFFFFu ? 122u * 40u : 106u * 40u),
               "seasonInformationsArray must equal 40 * sizeof(SeasonInformations) (CAREER_MAX_SEASONS)");

SeasonInformations _seasonInformationsArray[CAREER_MAX_SEASONS];
