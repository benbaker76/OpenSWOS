/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_foreign_market.c — UNTANGLE step 3 / teamSelectionKeyCache cluster
 * Tier 2 (overlay retirement).
 *
 * `careerForeignMarketPlayers` lifted OUT of the teamSelectionKeyCache cluster
 * in the careerFileBuffer overlay into its own standalone array, so the cache
 * writer-overflow can no longer corrupt foreign-market player data. Previously
 * the cache and this array shared one BSS run (cache overflow bound =
 * &teamSelectionKeyCacheEnd, 18 KB past the cache, straight through here — see
 * project_cache_overlay_writer_overflow). With the cache now in its own
 * dedicated buffer (career_key_cache.c), the two no longer overlap.
 *
 * MarketPlayer[432] = 18144 bytes. SWOS2.s names only the first 352 entries
 * (`careerForeignMarketPlayers: ds.b $39C0` = 14784) followed by the unnamed
 * `BSS_01_16E082: ds.b $D20` (3360 = 80 more MarketPlayer); both ports treat
 * it as ONE 432-element pool (the refill quota at career_init.c is computed from
 * 0x1B0 = 432, and the JSON save emits all 432 slots), so it is defined here as
 * the full 432-element array.
 *
 * Real label (not .equ) so every reader (transfer_market.c) and the save's
 * typed MarketPlayer objects (addressed by &careerForeignMarketPlayers) keep
 * working unchanged — the array is `&symbol`-addressed everywhere, so this
 * relocation is byte-neutral for the save.
 *
 * See project_keycache_cluster_design / docs/KEYCACHE_CLUSTER_REWORK.md.
 */
#include "swos_types.h"
#include "swos_platform.h"
#include "game_types.h"     /* MarketPlayer */
#include "career_capacity.h"   /* CAREER_MAX_FOREIGN_POOL */

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM target, which rejects asm-defined data symbols —
 * docs/WASM.md §4b). &symbol-addressed everywhere, so byte-neutral; the typed
 * MarketPlayer[] array gives the same 2-byte alignment the old .p2align 1 did. */
MarketPlayer _careerForeignMarketPlayers[CAREER_MAX_FOREIGN_POOL];   /* (432 on the Amiga; the SDL
                                       * build's maximum, used up to foreign_pool_capacity) */

_Static_assert(sizeof(MarketPlayer) == 42, "MarketPlayer must stay 42 bytes");
_Static_assert(CAREER_MAX_FOREIGN_POOL >= CAREER_ORIG_FOREIGN_POOL && CAREER_MAX_FOREIGN_POOL < 65536u,
               "the pool holds the original's 432 at least (named 352 + unnamed 80-entry tail, "
               "one pool), and its count is a uint16_t");
#ifndef SWOS_SDL_BUILD
_Static_assert(sizeof(_careerForeignMarketPlayers) == 18144,
               "careerForeignMarketPlayers stays 432*42 = 18144 bytes on the Amiga");
#endif
