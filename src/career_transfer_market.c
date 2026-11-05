/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_transfer_market.c — UNTANGLE step 3 (overlay retirement, region B7).
 *
 * The transfer-market working state — the foreign/home market slot tables, the
 * transfer-offer table, the job-offer queues, the players-on-market bitset and
 * the per-offer negotiation scalars — lifted out of the careerFileBuffer overlay
 * into standalone storage. Real labels (not .equ) so every reader
 * (transfer_market / job_offers / menus) + the save descriptors keep working
 * unchanged. See project_career_overlay_extraction_recipe.
 *
 * Two contiguous overlay runs (split by the already-extracted A5b transfer
 * counters careerSeasonsCompleted..offerSwapSlotsUsed, now a 16-byte hole):
 *   run 1 (230 B): foreignMarketPlayers(60), homeMarketPlayers(100),
 *                  transferOfferTable(70)
 *   run 2 (296 B): numNewJobOffers, newJobOffers(JobOffer[5]=130), numJobOffers,
 *                  jobOffers(JobOffer[5]=130), timeToNegotiate,
 *                  numPlayersOnTransferMarket, playersOnTransferMarket(10),
 *                  lastOffer{Primary,Swap,Extra}*Ordinal / ExtraPlayersWanted,
 *                  offerNegotiationAttemptCount, transferMarketRandOffset0..2,
 *                  transferMarketPhase
 * Each run becomes its own .skip hole (no cascade; the A5b hole between them is
 * untouched).
 *
 * Format-neutral: market slots are 4-byte {word teamNum, byte ordinal, byte pad}
 * entries and JobOffer is 26 bytes — all pointer-free, no 64-bit growth. The
 * save addresses every field via &symbol: FA foreign/home_market_players +
 * players_on_transfer_market (FLD_BYTES), FX scalars, and the typed
 * s_transfer_offer_fields / s_job_offer_fields tables over &transferOfferTable /
 * &newJobOffers / &jobOffers. So relocating the storage changes nothing the save
 * sees.
 *
 * Audited: every access is bounded within-array indexing — foreignMarketPlayers /
 * homeMarketPlayers via + idx*MARKET_ENTRY_SIZE (idx < num{Foreign,Home}MarketPlayers),
 * &jobOffers[index] / &newJobOffers[i] within JobOffer[5] — no cross-array walk
 * into a neighbour. Emitted via __asm__ real labels (raw-byte storage) so the
 * conflicting element-type externs (uint8_t[] vs JobOffer[]) resolve as before.
 */
#include "swos_types.h"
#include "swos_platform.h"
#include "career_capacity.h"   /* CAREER_MAX_*: the lists' compiled sizes (the original's on the Amiga) */

#define JOB_OFFER_BYTES 26u    /* sizeof(JobOffer), game_types.h */
extern uint8_t _foreignMarketPlayers[CAREER_MAX_FOREIGN_LISTING * CAREER_MARKET_ENTRY_SIZE];
extern uint8_t _homeMarketPlayers[100];
extern uint8_t _transferOfferTable[70];
extern uint8_t _newJobOffers[CAREER_MAX_JOB_OFFERS * JOB_OFFER_BYTES];
extern uint8_t _jobOffers[CAREER_MAX_JOB_OFFERS * JOB_OFFER_BYTES];
extern uint16_t _playersOnTransferMarket[CAREER_MAX_TRANSFER_LIST];
_Static_assert(sizeof(_homeMarketPlayers)==100 && sizeof(_transferOfferTable)==70,
               "transfer-market array sizes must match the overlay reservation");
_Static_assert(sizeof(_foreignMarketPlayers) >= 60 && sizeof(_newJobOffers) >= 130
            && sizeof(_jobOffers) >= 130 && sizeof(_playersOnTransferMarket) >= 10,
               "the lists hold the original's sizes at least (include/career_capacity.h)");
#ifndef SWOS_SDL_BUILD
_Static_assert(sizeof(_foreignMarketPlayers)==60 && sizeof(_newJobOffers)==130
            && sizeof(_jobOffers)==130 && sizeof(_playersOnTransferMarket)==10,
               "the Amiga keeps the original's sizes");
#endif

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM target — docs/WASM.md §4b). The arrays match their
 * in-file externs above (kept) with aligned() preserving the old .p2align 2;
 * newJobOffers/jobOffers are JobOffer[5] raw blobs (fixed-size &symbol save). */
uint8_t  _foreignMarketPlayers[CAREER_MAX_FOREIGN_LISTING * CAREER_MARKET_ENTRY_SIZE] __attribute__((aligned(4)));
uint8_t  _homeMarketPlayers[100]      __attribute__((aligned(4)));
uint8_t  _transferOfferTable[70]      __attribute__((aligned(4)));
uint16_t _numNewJobOffers;
uint8_t  _newJobOffers[CAREER_MAX_JOB_OFFERS * JOB_OFFER_BYTES] __attribute__((aligned(4)));   /* JobOffer[] */
uint16_t _numJobOffers;
uint8_t  _jobOffers[CAREER_MAX_JOB_OFFERS * JOB_OFFER_BYTES]    __attribute__((aligned(4)));   /* JobOffer[] */
uint16_t _timeToNegotiate;
uint16_t _numPlayersOnTransferMarket;
uint16_t _playersOnTransferMarket[CAREER_MAX_TRANSFER_LIST];   /* the players listed: ids, host-order words */
uint16_t _lastOfferPrimaryPlayerOrdinal, _lastOfferExtraPlayersWanted,
         _lastOfferSwapPlayerOrdinal, _lastOfferExtraPlayerOrdinal,
         _offerNegotiationAttemptCount, _transferMarketRandOffset0,
         _transferMarketRandOffset1, _transferMarketRandOffset2, _transferMarketPhase;
