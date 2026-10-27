/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_key_cache.c — UNTANGLE step 3 / teamSelectionKeyCache cluster Tier 2
 * (overlay retirement).
 *
 * The teamSelectionKeyCache cluster lifted OUT of the careerFileBuffer overlay
 * into a dedicated, bounded buffer. In SWOS2.s (129242-129261) this was one BSS
 * run where the cache OVERLAPPED careerForeignMarketPlayers: the cache builder's
 * overflow bound is `&teamSelectionKeyCacheEnd`, which sat 20014 bytes past the
 * cache start — straight through the foreign-market region. A career-mode
 * 22-byte-slot overflow could therefore stomp live player data (the documented
 * project_cache_overlay_writer_overflow bug). Here the cache gets its OWN
 * buffer and careerForeignMarketPlayers lives in career_foreign_market.c, so the
 * two can never overlap; an overflow now stays in the cache's own dead tail
 * (clamped by the stride bounds-check in the three cache walkers).
 *
 * Buffer size = 8192 bytes: a tight, HONEST bound. The original gave the cache
 * the full 20014-byte span to &teamSelectionKeyCacheEnd (straight through the
 * foreign-market region) as accidental slack; once the two no longer share
 * memory that 18 KB of dead tail only served to SWALLOW a genuine overflow
 * instead of tripping TeamSelectionKeyCache_InsertSpaceSorted's `ptr >
 * &teamSelectionKeyCacheEnd` panic (0x8889) — counter to "panics are signals".
 * The high-water-mark probe measured real usage at ~1188 bytes over full
 * careers, and the analytical heavy case (24-team league + domestic cup +
 * European cup, KEYCACHE_CLUSTER_REWORK.md §8.1) tops out ~2.5 KB. 8192 is ~7x
 * the measured HWM / ~3x the analytical worst case — ample headroom — while
 * making the 0x8889 bound fire loudly on any real overflow rather than eating
 * 18 KB of player-data-free slack first. The full-buffer save blob (TSKC_SIZE)
 * and the stride-clamped walkers are unaffected (both << 8192). This is the
 * Tier 2 §8.3 finish; the deferred per-record writer fix is still a follow-up.
 *
 * careerJobOfferPhaseBias was byte 1 of the cache buffer in SWOS2.s
 * (`teamSelectionKeyCache: ds.b $1` then `careerJobOfferPhaseBias: ds.b $1`).
 * This is NOT idle memory-reuse: the finance/job-offer code DEPENDS on the
 * overlap value. The cache builder writes entry-0's sortKey to
 * teamSelectionKeyCache[1] == careerJobOfferPhaseBias, so the bias equals the
 * earliest fixture's week — which is the season-start base for
 * lastFinancialWeekIndex (first weekly deduction diff) and the `lastKey - bias`
 * season span in wageScalingDivisor. Promoting it to an independent scalar here
 * decoupled it from the cache, so InitCareer MUST re-derive it explicitly
 * (`_careerJobOfferPhaseBias = ((TeamSelectionKeyCacheEntry*)cache)->sortKey`,
 * career_init.c) — otherwise it stays 0 and the first-match finances are
 * inflated (long-standing bug fixed 2026-07-03). It has its own save descriptor;
 * old JSON saves that carried it as cache-blob byte 1 load it as 0 and it is
 * re-derived on the next season init.
 *
 * Real labels (not .equ) so every reader and the save FX/FA descriptors
 * (addressed by &symbol) keep working unchanged.
 *
 * See project_keycache_cluster_design / docs/KEYCACHE_CLUSTER_REWORK.md.
 */
#include "swos_types.h"
#include "swos_platform.h"

/* Tightened from the SWOS2.s 20014-byte teamSelectionKeyCache->End span to an
 * honest bound now that foreign-market no longer shares the slack — see the
 * file header. The original 8192 was sized from CAREER usage (22-byte
 * entries, ~1188 B measured HWM) — but SEASON-mode entries carry every match
 * of their round (numTeams/2 × 12 + 10, capped 298 B), so a 20-team English
 * season (38 matchdays ≈ 4.9 KB) + two domestic cups (~2.5 KB) + the 36-team
 * european cup's 17 entries (~2.4 KB, docs/CHAMPIONS36.md "Season mode")
 * tripped the 0x8889 overflow panic at PLAY SEASON (live report, 2026-10-27;
 * an Italian season measured 60 entries / ~7.8 KB — just under). 16384 is
 * ~1.6x that analytical season worst case; the save blob (TSKC_SIZE,
 * save_format_json.c) must stay == this size so a season's cache survives a
 * save/load whole. */
#define KEY_CACHE_BUF_SIZE 16384
/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b).
 *
 * The cache and the season-flag `_teamSelectionKeyCacheEnd` are now INDEPENDENT
 * globals. In SWOS2.s `End` sat immediately past the cache, so its single
 * storage slot did double duty: its VALUE is a season flag, and its ADDRESS was
 * the InsertSpaceSorted overflow bound (== cache + KEY_CACHE_BUF_SIZE). The save
 * already models the two roles as SEPARATE fields (a full-buffer cache blob +
 * a uint16 `team_selection_key_cache_end`), so decoupling the storage is
 * format-neutral; End's address-bound role is re-expressed at the two
 * career_teams.c call sites as `_teamSelectionKeyCache + sizeof(...)`, so the
 * Amiga memory-reuse adjacency is no longer required. The four cursors and
 * `_careerJobOfferPhaseBias` were already independent scalars. */
uint16_t _teamSelectionKeyCacheIndex;
uint16_t _teamSelectionKeyCacheOffset;
uint16_t _teamSelectionKeyCacheCount;
uint16_t _careerSlotProcessingInProgress;
uint8_t  _teamSelectionKeyCache[KEY_CACHE_BUF_SIZE];
uint16_t _teamSelectionKeyCacheEnd;     /* season-flag VALUE; the old &End == cache+SIZE
                                           overflow bound is now derived in career_teams.c */
uint8_t  _careerJobOfferPhaseBias;      /* promoted standalone scalar (was cache byte 1) */

_Static_assert(sizeof(_teamSelectionKeyCache) == 16384,
               "teamSelectionKeyCache buffer must stay 16384 bytes "
               "(InsertSpaceSorted bound AND the save blob size — "
               "KEYCACHE_CLUSTER_REWORK.md §8.3, TSKC_SIZE in save_format_json.c)");
