/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "transfer_market.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "menu_system.h"
#include "menus.h"
#include "string_utils.h"
#include "career_teams.h"
#include "input_text.h"
#include "teams.h"
#include "auto_skip_debug.h"
#include "world.h"

/* Offset from GetPlayer() return value to actual PlayerFile data */
#define PF_OFF              76  /* sizeof(TeamFile) */

/* Transfer offer entry size: 14 bytes ($E) */
#define OFFER_ENTRY_SIZE    14
/* Home market player entry: 4 bytes (word teamNum + byte playerOrd + pad) */
#define MARKET_ENTRY_SIZE   4
#define HOME_MARKET_MAX     25
#define FOREIGN_MARKET_MAX  15

#define POOLPLAYER_COUNT        900

#define GAME_TYPE_CAREER        4

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _transferPoolSlotLimit;
extern uint16_t _transferPoolSlotsUsed;
extern uint16_t _offerSwapSlotLimit;
extern uint16_t _offerSwapSlotsUsed;
extern uint16_t _numOffers;
extern uint8_t  _transferOfferTable[];
extern uint16_t _careerNumPlayers;
extern uint16_t _numHomeMarketPlayers;
extern uint8_t  _homeMarketPlayers[];
extern uint16_t _numForeignMarketPlayers;
extern uint8_t  _foreignMarketPlayers[];
extern uint16_t _selTeamNumber;
extern uint16_t _gameType;
extern uint16_t _numForeignMarketPlayers2;
extern MarketPlayer _careerForeignMarketPlayers[];
extern PoolPlayer   _poolplyrDatBuffer[];
extern uint16_t _poolplyrLoaded;
extern uint8_t  _aDataPoolplyrDa[];
extern PoolPlayer   _poolPlayerStrideRef[];
extern uint16_t _isNationalSelector;
extern uint16_t _numSelectedTeams;
extern uint16_t _homeMarketUpdateBacklog;
extern uint16_t _foreignMarketUpdateBacklog;
extern uint16_t _homeMarketRandomPickRetries;
extern uint16_t _foreignMarketRandomPickRetries;
extern uint16_t _selTeamSelectedPlayerPosition;

/* Already-ported functions called from this module */
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern uint16_t Rand2(void);
extern uint16_t GetRandomWord(void);
extern uint32_t GetLowestAndHighestPlayerPrice(void);

/* ---- Additional externs for Chunk 74/75 ---- */
extern CompetitionSlotBuffer _primaryCompetitionBuf;          /* slot-0 untangle (S6): typed buffer (was competitionFileBuffer + diyTeamIdListType0/1/2 + numDiyTeams) */

/* Pin the two trialist-insert-position fields (see _AddTrialist... below), which
 * replaced raw 0x4E/0x66 reads. Both sit past DIYCompetition.namePtr@0x28, so the
 * raw byte offsets are correct on 32-bit but WRONG on 64-bit — and by DIFFERENT
 * amounts, which is exactly why the typed fix is needed:
 *   - sizeof(DIYCompetition) is 0x60 (32-bit) but 0x68 (64-bit): namePtr 4->8
 *     (+4) PLUS +4 end-padding to 8-byte alignment.
 *   - numTeamsInStage is INSIDE the header (before the end-pad): +4 -> 0x52.
 *   - leagueMaxSubstitutes is in matchData AFTER the header: +8 -> 0x6E.
 * Asserting both layouts proves byte-identical behaviour on the Amiga (32-bit)
 * and the correct (different-per-field) offsets on the 64-bit SDL build. */
#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(offsetof(DIYCompetitionEx, base.numTeamsInStage) == 0x4E,
               "numTeamsInStage must be at 0x4E on 32-bit (was raw compBuf[0x4E/2])");
_Static_assert(offsetof(DIYCompetitionEx, matchData.league.leagueMaxSubstitutes) == 0x66,
               "matchData+2 must be at 0x66 on 32-bit (was raw compBuf[0x66/2])");
#else
_Static_assert(offsetof(DIYCompetitionEx, base.numTeamsInStage) == 0x52,
               "numTeamsInStage must be at 0x4E+4 on 64-bit (namePtr +4, in-header)");
_Static_assert(offsetof(DIYCompetitionEx, matchData.league.leagueMaxSubstitutes) == 0x6E,
               "matchData+2 must be at 0x66+8 on 64-bit (namePtr +4 + header end-pad +4)");
#endif
/* In-progress transfer offer parameters — 7 host-native fields untangled
 * from the offerOrdinalsBackingBuf .equ overlay into a typed struct
 * (OfferOrdinals, game_types.h). All access below is offerOrdinals.<field>;
 * offerOrdinals.offerNegotiationStep1Amount is the 7th field (was a separate extern). */
extern OfferOrdinals _offerOrdinals;
extern uint16_t _removedOfferPlayersFromSelectedTeamCount;
extern uint16_t _removedOfferPlayersFromOtherTeamsCount;

/* ---- Additional externs for Chunk 75 ---- */
extern uint16_t _numPlayersOnTransferMarket;
extern uint16_t _playersOnTransferMarket[];
extern uint16_t _financeTotalsNeedResetFlag;
extern int32_t  _newBalance;
extern int32_t  _old_balance;
extern int32_t  _playerPurchases;
extern int32_t  _playerSales;
extern int32_t  _playerWagesBill;
extern int32_t  _generalRunningCosts;
extern int32_t  _gateReceipts;
extern int32_t  _startingBalanceCopy;
extern int32_t  _competitionBonuses;
extern uint16_t _pendingForeignPlayerCount;
extern uint8_t  _lastLoadedExtNumber;
extern uint8_t  _viewRandomPhase;
extern uint16_t _offerExpiryIterationsRemaining;

/* C-callable bridge for asm GetPlayerAtIndex (in swos2_unit.s) */
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);

/* File index output from _CGetPlayerAtIndex — written by asm bridge, read by C */
uint16_t _cGetPlayerAtIndexFileIdx __attribute__((used, externally_visible));

/* ================================================================
 * GetRemainingTransferPoolSlots  (SWOS2.s line 72720)
 *
 * Returns the number of remaining transfer pool slots.
 *
 * Asm register convention:
 *   out: d2.w = transferPoolSlotLimit - transferPoolSlotsUsed
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetRemainingTransferPoolSlots(void)
{
    return _transferPoolSlotLimit - _transferPoolSlotsUsed;
}

/* ================================================================
 * GetRemainingOfferSwapSlots  (SWOS2.s line 72727)
 *
 * Returns the number of remaining offer swap slots.
 *
 * Asm register convention:
 *   out: d2.w = offerSwapSlotLimit - offerSwapSlotsUsed
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetRemainingOfferSwapSlots(void)
{
    return _offerSwapSlotLimit - _offerSwapSlotsUsed;
}

/* ================================================================
 * RemoveTransferOfferAtIndex  (SWOS2.s line 72848)
 *
 * Removes the transfer offer at the given index by shifting all
 * subsequent entries left by one. Decrements numOffers.
 *
 * Asm register convention:
 *   in:  d0.w = offerIndex
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void RemoveTransferOfferAtIndex(uint16_t offerIndex)
{
    int32_t remaining;
    int32_t i;

    remaining = (int16_t)((int16_t)_numOffers - (int16_t)offerIndex - 2);  /* sub.w d0,d1; subq.w #2,d1 */

    if (remaining >= 0) {
        TransferOfferEntry *table = (TransferOfferEntry *)_transferOfferTable;
        /* Shift subsequent entries down one slot (overwrites entry offerIndex).
         * asm walked this byte-by-byte; one struct copy per slot is equivalent. */
        for (i = 0; i <= remaining; i++)
            table[offerIndex + i] = table[offerIndex + i + 1];
    }

    _numOffers--;                                                /* subq.w #1,(numOffers) */
}

/* ================================================================
 * PrunePlayerOffersAgainstRosterLimits  (SWOS2.s line 72734)
 *
 * Iterates through transfer offers and removes any that would
 * violate roster/slot limits:
 * Phase 1: Remove offers that exceed swap or pool slot limits.
 * Phase 2: If roster is full (>= 32), remove offers that buy
 *          players without selling (byte 3 negative + byte 5 positive).
 *
 * Transfer offer entry layout (14 bytes):
 *   +3: offered player ordinal (bit 7 set = no player)
 *   +4: secondary player (bit 7 set = none)
 *   +5: extra player (bit 7 set = none)
 * ================================================================ */
__attribute__((used, externally_visible))
void PrunePlayerOffersAgainstRosterLimits(void)
{
    uint16_t swapSlots, poolSlots;
    uint16_t idx;
    TransferOfferEntry *offer;

    /* Phase 1: Check swap slot and pool slot limits */
restart_phase1:
    if (_numOffers == 0)
        goto phase2;

    swapSlots = GetRemainingOfferSwapSlots();
    if (swapSlots == 0) {
        _numOffers = 0;                              /* no swap slots → clear all offers */
        goto phase2;
    }

    poolSlots = GetRemainingTransferPoolSlots();
    idx = 0;
    offer = (TransferOfferEntry *)_transferOfferTable;

    while (idx != _numOffers) {
        /* second wanted player non-negative (a player being offered in swap) */
        if (offer->secondPlayerOrdinal >= 0) {
            if (swapSlots == 1)                     /* only 1 swap slot left → remove */
                goto remove_phase1;
        }

        /* swap player non-negative (secondary player) */
        if (offer->swapPlayerOrdinal >= 0) {
            if (poolSlots == 0)                     /* no pool slots → remove */
                goto remove_phase1;
            if (poolSlots < 2) {                    /* only 1 pool slot */
                if (offer->extraPlayerOrdinal >= 0) /* and extra player also present → remove */
                    goto remove_phase1;
            }
        }

        /* Keep this offer, advance */
        offer++;
        idx++;
        continue;

    remove_phase1:
        RemoveTransferOfferAtIndex(idx);
        goto restart_phase1;                        /* restart scan after removal */
    }

    /* Phase 2: If career roster full (>= 32 players), remove buy-only offers */
phase2:
    if (_careerNumPlayers < 0x20)
        return;

restart_phase2:
    if (_numOffers == 0)
        return;

    idx = 0;
    offer = (TransferOfferEntry *)_transferOfferTable;

    while (idx != _numOffers) {
        /* Remove offers that buy (extra player present) but don't sell (no second wanted) */
        if (offer->extraPlayerOrdinal >= 0 && offer->secondPlayerOrdinal < 0) {
            RemoveTransferOfferAtIndex(idx);
            goto restart_phase2;
        }

        offer++;
        idx++;
    }
}

/* ================================================================
 * IsPlayerInCareerContest  (SWOS2.s line 73096)
 *
 * Searches homeMarketPlayers for a (teamNumber, playerOrdinal) pair.
 * Returns the index if found, or -1 if not found.
 *
 * Asm register convention:
 *   in:  d0 = teamNumber, d1 = playerOrdinal
 *   out: Z flag (set = found, clear = not found), d2 = index, a1 = entry pointer
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsPlayerInCareerContest(uint16_t teamNumber, uint16_t playerOrdinal)
{
    uint8_t *entry;
    uint16_t i;

    if (_numHomeMarketPlayers == 0)
        return -1;

    entry = _homeMarketPlayers;
    for (i = 0; i < _numHomeMarketPlayers; i++) {
        /* Each entry stores [countryByte, ordinalByte, playerOrdByte, _].
         * Read as PACK_BE_U16 so the resulting uint16 has country in
         * the high byte (matching findTeamInCache's PACK_BE_U16 lookup
         * convention) — a raw `*(uint16_t *)entry` would byte-swap on
         * LE and the comparison against PACK_BE_U16-formatted
         * teamNumber would never match. */
        if (PACK_BE_U16(entry[0], entry[1]) == teamNumber &&
            entry[2] == (uint8_t)playerOrdinal)
            return (int16_t)i;                      /* found at index i */
        entry += MARKET_ENTRY_SIZE;
    }

    return -1;                                      /* not found */
}

/* ================================================================
 * AddPlayerToCareerContest  (SWOS2.s line 73053)
 *
 * Appends a player to the homeMarketPlayers array if not full.
 * Returns non-zero if added, 0 if full (matches Z flag convention).
 *
 * Asm register convention:
 *   in:  d0 = teamNumber, d1 = playerOrdinal
 *   out: Z flag (set = full/can't add, clear = added OK)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t AddPlayerToCareerContest(uint16_t teamNumber, uint16_t playerOrdinal)
{
    uint8_t *entry;
    uint32_t offset;

    if (_numHomeMarketPlayers >= HOME_MARKET_MAX)
        return 0;                                   /* full */

    offset = (uint32_t)_numHomeMarketPlayers * MARKET_ENTRY_SIZE;
    entry = _homeMarketPlayers + offset;
    /* teamNumber is in PACK_BE_U16 format (country << 8 | ordinal).
     * Write byte-wise so byte[0]=country, byte[1]=ordinal, matching
     * the file format — a `*(uint16_t *)entry = teamNumber` on LE
     * would byte-swap and put ordinal at byte[0]. */
    entry[0] = (uint8_t)(teamNumber >> 8);          /* country */
    entry[1] = (uint8_t)teamNumber;                  /* ordinal */
    entry[2] = (uint8_t)playerOrdinal;              /* move.b d1,2(a1) */
    _numHomeMarketPlayers++;                          /* addq.w #1 → clears Z */

    return 1;                                       /* added OK (non-zero → Z clear) */
}

/* ================================================================
 * RemovePlayerFromCareerContest  (SWOS2.s line 73068)
 *
 * Removes a player from homeMarketPlayers by searching for it
 * then shifting subsequent entries left.
 *
 * Asm register convention:
 *   in:  d0 = teamNumber, d1 = playerOrdinal
 * ================================================================ */
__attribute__((used, externally_visible))
void RemovePlayerFromCareerContest(uint16_t teamNumber, uint16_t playerOrdinal)
{
    int16_t idx = IsPlayerInCareerContest(teamNumber, playerOrdinal);

    if (idx < 0)
        return;                                     /* not found */

    /* If not the last entry, shift remaining entries left */
    if ((uint16_t)(idx + 1) != _numHomeMarketPlayers) {
        uint16_t entriesToMove = _numHomeMarketPlayers - (uint16_t)(idx + 1);
        volatile uint32_t *ldst = (volatile uint32_t *)(_homeMarketPlayers + (uint16_t)idx * MARKET_ENTRY_SIZE);
        volatile uint32_t *lsrc = ldst + 1;
        uint16_t i;
        /* Each entry is 4 bytes = 1 longword; use volatile to prevent memmove */
        for (i = 0; i < entriesToMove; i++) {
            ldst[i] = lsrc[i];
        }
    }

    _numHomeMarketPlayers--;
}

/* ================================================================
 * FindForeignMarketPlayer  (SWOS2.s line 73264)
 *
 * Searches foreignMarketPlayers for a (teamId, playerOrdinal) pair.
 * Returns the index if found, or -1 if not found.
 *
 * Asm register convention:
 *   in:  d0 = teamId, d1 = playerOrdinal
 *   out: Z flag (set = found, clear = not found), d2 = index, a1 = entry pointer
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t FindForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal)
{
    uint8_t *entry;
    uint16_t i;

    if (_numForeignMarketPlayers == 0)
        return -1;

    entry = _foreignMarketPlayers;
    for (i = 0; i < _numForeignMarketPlayers; i++) {
        /* PACK_BE_U16 read — see IsPlayerInCareerContest comment. */
        if (PACK_BE_U16(entry[0], entry[1]) == teamId &&
            entry[2] == (uint8_t)playerOrdinal)
            return (int16_t)i;                      /* found at index i */
        entry += MARKET_ENTRY_SIZE;
    }

    return -1;                                      /* not found */
}

/* ================================================================
 * AddForeignMarketPlayer  (SWOS2.s line 73227)
 *
 * Appends a player to the foreignMarketPlayers array if not full.
 * Returns non-zero if added, 0 if full.
 *
 * Asm register convention:
 *   in:  d0 = teamId, d1 = playerOrdinal
 *   out: Z flag (set = full, clear = added OK)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t AddForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal)
{
    uint8_t *entry;
    uint32_t offset;

    if (_numForeignMarketPlayers >= FOREIGN_MARKET_MAX)
        return 0;                                   /* full */

    offset = (uint32_t)_numForeignMarketPlayers * MARKET_ENTRY_SIZE;
    entry = _foreignMarketPlayers + offset;
    /* Byte-wise PACK_BE_U16 write — see AddPlayerToCareerContest. */
    entry[0] = (uint8_t)(teamId >> 8);              /* country */
    entry[1] = (uint8_t)teamId;                      /* ordinal */
    entry[2] = (uint8_t)playerOrdinal;              /* move.b d1,2(a1) */
    _numForeignMarketPlayers++;

    return 1;                                       /* added OK */
}

/* ================================================================
 * RemoveForeignMarketPlayer  (SWOS2.s line 73242)
 *
 * Removes a player from foreignMarketPlayers by searching for it
 * then shifting subsequent entries left.
 *
 * Asm register convention:
 *   in:  d0 = teamId, d1 = playerOrdinal
 * ================================================================ */
__attribute__((used, externally_visible))
void RemoveForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal)
{
    int16_t idx = FindForeignMarketPlayer(teamId, playerOrdinal);

    if (idx < 0)
        return;                                     /* not found */

    /* If not the last entry, shift remaining entries left */
    if ((uint16_t)(idx + 1) != _numForeignMarketPlayers) {
        uint16_t entriesToMove = _numForeignMarketPlayers - (uint16_t)(idx + 1);
        volatile uint32_t *ldst = (volatile uint32_t *)(_foreignMarketPlayers + (uint16_t)idx * MARKET_ENTRY_SIZE);
        volatile uint32_t *lsrc = ldst + 1;
        uint16_t i;
        /* Each entry is 4 bytes = 1 longword; use volatile to prevent memmove */
        for (i = 0; i < entriesToMove; i++) {
            ldst[i] = lsrc[i];
        }
    }

    _numForeignMarketPlayers--;
}

/* ================================================================
 * FindPlayerInCareerOrForeignMarket  (SWOS2.s line 73286)
 *
 * Searches for a player in both the home career contest and the
 * foreign market. Returns 0 if found in either, 1 if not found.
 *
 * Asm register convention:
 *   in:  d0 = teamId, d1 = playerOrdinal
 *   out: Z flag (set = found, clear = not found)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t FindPlayerInCareerOrForeignMarket(uint16_t teamId, uint16_t playerOrdinal)
{
    if (IsPlayerInCareerContest(teamId, playerOrdinal) >= 0)
        return 0;                                   /* found in home market → Z set */

    if (FindForeignMarketPlayer(teamId, playerOrdinal) >= 0)
        return 0;                                   /* found in foreign market → Z set */

    return 1;                                       /* not found → Z clear */
}

/* ================================================================
 * RemovePlayerFromTransferOffers  (SWOS2.s line 72807)
 *
 * Removes all transfer offers involving a specific player
 * (as primary, secondary, or counter-offer player).
 *
 * Asm register convention:
 *   in:  d0 = teamNumber, d1.b = playerOrdinal
 * ================================================================ */
__attribute__((used, externally_visible))
void RemovePlayerFromTransferOffers(uint16_t teamNumber, uint16_t playerOrdinal)
{
    uint8_t ord = (uint8_t)playerOrdinal;
    uint16_t idx;
    TransferOfferEntry *offer;

restart:
    if (_numOffers == 0)
        return;

    idx = 0;
    offer = (TransferOfferEntry *)_transferOfferTable;

    while (idx != _numOffers) {
        /* Check if this is an offer from selTeamNumber */
        if (teamNumber == _selTeamNumber) {
            /* Check the two wanted players (raw byte compare) */
            if ((uint8_t)offer->primaryPlayerOrdinal == ord ||
                (uint8_t)offer->secondPlayerOrdinal == ord) {
                RemoveTransferOfferAtIndex(idx);
                goto restart;
            }
        }

        /* Check if offer is TO this team */
        if (offer->teamOffering == teamNumber) {
            /* Check the two offered (counter/swap) players */
            if ((uint8_t)offer->swapPlayerOrdinal == ord ||
                (uint8_t)offer->extraPlayerOrdinal == ord) {
                RemoveTransferOfferAtIndex(idx);
                goto restart;
            }
        }

        offer++;
        idx++;
    }
}

/* ================================================================
 * FillForeignMarketPlayersData  (SWOS2.s line 71546)
 *
 * For each foreign market player in careerForeignMarketPlayers,
 * finds the matching entry in poolplyrDatBuffer and copies
 * 38 bytes (19 words) of player data from market entry to pool entry.
 *
 * Only runs in career mode.
 * ================================================================ */
__attribute__((used, externally_visible))
void FillForeignMarketPlayersData(void)
{
    uint16_t count, i, j;
    uint8_t countrySel;
    MarketPlayer *marketEntry;
    PoolPlayer *poolEntry;

    if (_gameType != GAME_TYPE_CAREER)
        return;

    count = _numForeignMarketPlayers2;
    if (count == 0)
        return;

    countrySel = (uint8_t)(_selTeamNumber >> 8);  /* move.b (selTeamNumber).l,d5 — high byte on big-endian */
    marketEntry = _careerForeignMarketPlayers;

    for (i = count - 1; ; i--) {
        /* Skip if same country as selected team */
        if (*(uint8_t *)&marketEntry->teamId != countrySel) {
            uint16_t teamId = marketEntry->teamId;
            int8_t playerOrd = marketEntry->playerOrdinal;

            /* Search poolplyrDatBuffer for matching player */
            poolEntry = _poolplyrDatBuffer;
            for (j = POOLPLAYER_COUNT - 1; ; j--) {
                if (poolEntry->teamId == teamId &&
                    poolEntry->playerOrdinal == playerOrd) {
                    /* Found match: copy PlayerFile (19 words) from market to pool entry */
                    volatile uint16_t *src = (volatile uint16_t *)&marketEntry->player;
                    volatile uint16_t *dst = (volatile uint16_t *)&poolEntry->player;
                    int16_t k;
                    for (k = 18; k >= 0; k--) {  /* move.w #18,d0; dbf loop */
                        *dst++ = *src++;
                    }
                    break;
                }
                poolEntry++;
                if (j == 0)
                    break;
            }
        }

        marketEntry++;
        if (i == 0)
            break;
    }
}

/* ================================================================
 * LoadPoolplyrDat  (SWOS2.s line 72843)
 *
 * Loads the pool player database file if not already loaded,
 * then calls FillForeignMarketPlayersData.
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadPoolplyrDat(void)
{
    if (_poolplyrLoaded == 0) {
        uint32_t fileSize;
        _poolplyrLoaded = 1;
        LoadFileEx(_aDataPoolplyrDa, (uint8_t *)_poolplyrDatBuffer, &fileSize);
    }
    FillForeignMarketPlayersData();
}

/* ================================================================
 * FindPoolplyrDatPlayer  (SWOS2.s line 72856)
 *
 * Searches poolplyrDatBuffer for a player matching (teamId, playerOrdinal).
 * Scans PoolPlayer entries starting from poolPlayerStrideRef + 1.
 *
 * WARNING: No bounds check — loops until match found (assumes match exists).
 *
 * Asm register convention:
 *   in:  d0 = teamId, d1.b = playerOrdinal
 *   out: a0 = pointer to matching pool entry
 * ================================================================ */
__attribute__((used, externally_visible))
PoolPlayer *FindPoolplyrDatPlayer(uint16_t teamId, uint16_t playerOrdinal)
{
    /* The original Amiga starts iteration at `poolPlayerStrideRef`
     * (a 1-byte BSS marker) and does `entry++` (= +58) before the
     * first comparison, so the first entry visited is at
     * `poolPlayerStrideRef + 58`. The Amiga BSS layout pins
     * poolPlayerStrideRef EXACTLY 58 bytes before poolplyrDatBuffer
     * — making the +58 land precisely at poolplyrDatBuffer[0]. On
     * SDL each symbol lives in its own .o; the two are NOT 58 bytes
     * apart, so the Amiga trick reads from arbitrary unrelated BSS
     * (or, in the user's session, walks off into unmapped memory
     * and SIGSEGVs).
     *
     * Iterate from poolplyrDatBuffer directly. Bounded at
     * POOLPLAYER_COUNT (the .dat decompresses to exactly 900
     * 58-byte records) so a not-found lookup returns NULL instead
     * of walking off the end. Callers handle NULL by skipping /
     * disabling the row. */
    PoolPlayer *entry = _poolplyrDatBuffer;
    for (int i = 0; i < POOLPLAYER_COUNT; i++) {
        /* poolplyr.dat stores teamId as the BE byte pair
         * [country, ordinal]. On BE Amiga `entry->teamId` reads as
         * country<<8|ordinal = PACK_BE_U16; on LE x86 the same field
         * read byte-swaps to ordinal<<8|country and the comparison
         * against the PACK_BE_U16-formatted `teamId` parameter never
         * matches. Convert through BE16_TO_HOST so the compare sees
         * the same format on both endians. */
        if (BE16_TO_HOST(entry->teamId) == teamId &&
            entry->playerOrdinal == (int8_t)playerOrdinal)
            return entry;
        entry++;
    }
    return (PoolPlayer *)0;
}

/* ================================================================
 * AddCareerTeamPlayer  (SWOS2.s line 72936)
 *
 * Selects a random player from a random non-home team among the
 * selected teams and adds them to the home market (career contest).
 * Uses price range filtering and duplicate checking.
 * ================================================================ */
__attribute__((used, externally_visible))
void AddCareerTeamPlayer(void)
{
    uint32_t priceRange = GetLowestAndHighestPlayerPrice();
    uint8_t lowestPrice = (uint8_t)(priceRange >> 16);     /* d4 = lowest */
    uint8_t highestPrice = (uint8_t)(priceRange & 0xFFFF); /* d5 = highest */
    uint8_t homeCountry;
    uint16_t attemptsLeft;
    uint16_t rand;
    uint16_t teamIdx;
    uint8_t *teamEntry;
    uint16_t teamNum;
    uint16_t playerOrd;

    highestPrice += 3;                                      /* addq.w #3,d5 */
    homeCountry = (uint8_t)(_selTeamNumber >> 8);             /* move.b (selTeamNumber).l,d3 — high byte on big-endian */

    attemptsLeft = 1;                                       /* move.w #1,d6 */
    rand = Rand2();
    if ((rand & 1) == 0) {                                  /* btst #0,d0; beq */
        attemptsLeft = _numSelectedTeams >> 1;               /* lsr.w #1,d6 */
    }

retry:
    attemptsLeft--;
    if (attemptsLeft == 0) {                                /* tst.w d6; bne skip */
        _homeMarketRandomPickRetries++;
    }

    /* Pick a random team from selected teams */
    rand = Rand2();
    teamIdx = (uint16_t)(((uint32_t)rand * _numSelectedTeams) >> 8);  /* mulu+lsr */
    /* mulu.w #684 gives a 32-bit offset (adda.l): kept as 32 bits. In 16 it wrapped for
     * team 96 on (96 * 684 = 65664) and picked the wrong team. */
    teamEntry = &_selTeamsPtr + (uint32_t)teamIdx * sizeof(TeamFileEx);

    /* Must be same country but different team. teamEntry points at
     * a TeamFile so byte[0]=country, byte[1]=ordinal. Read teamNum
     * as PACK_BE_U16 (country << 8 | ordinal) so it's comparable to
     * selTeamNumber and survives findTeamInCache lookup later. The
     * raw `*(uint16_t *)` read on LE would byte-swap and produce a
     * format mismatch — both with the selTeamNumber check here AND
     * with downstream consumers via AddPlayerToCareerContest →
     * homeMarketPlayers → PlayerMarketBuildVisiblePlayerList. */
    if (World_FileCountry(*teamEntry) != World_FileCountry(homeCountry))  /* cmp.b (a0),d3 (a second
                                                             * team file's clubs: their country's, include/world.h) */
        goto retry;
    teamNum = PACK_BE_U16(teamEntry[0], teamEntry[1]);
    if (teamNum == _selTeamNumber)                            /* cmp.w (selTeamNumber).l,d1 */
        goto retry;

    /* Pick a random player (0-15) */
    rand = Rand2();
    playerOrd = rand & 0xF;                                 /* andi.w #$F,d0 */
    /* Now d0 = teamNum, d1 = playerOrd (after exg) */

    /* If not first attempt (attemptsLeft was > 0), check price range */
    if (attemptsLeft != 0) {
        TeamFileEx *teamEx = (TeamFileEx *)teamEntry;
        uint8_t price = (uint8_t)teamEx->players[playerOrd].price;
        if (lowestPrice > price)                            /* cmp.b ... bhi retry */
            goto retry;
        if (highestPrice < price)                           /* cmp.b ... bcs retry */
            goto retry;
        /* Check position bit 2 (goalkeeper flag?) */
        if (teamEx->players[playerOrd].positionAndFace & PF_TRIALIST)
            goto retry;                                     /* btst #2,... bne retry */
    }

    /* Check if player is already in contest */
    if (IsPlayerInCareerContest(teamNum, playerOrd) >= 0)
        goto retry;                                         /* beq retry (Z set = found) */

    AddPlayerToCareerContest(teamNum, playerOrd);
}

/* ================================================================
 * FillCareerTeamPlayers  (SWOS2.s line 72868)
 *
 * Populates the home market with random players from selected teams.
 * Number of players is based on numSelectedTeams, with random reduction.
 * ================================================================ */
__attribute__((used, externally_visible))
void FillCareerTeamPlayers(void)
{
    uint16_t numToAdd;
    uint16_t rand;

    _numHomeMarketPlayers = 0;

    if (_isNationalSelector != 0)
        return;

    numToAdd = (uint16_t)(((uint32_t)_numSelectedTeams * 16) / 40);  /* mulu #$10; divu #$28 */
    if (numToAdd == 0)
        return;

    rand = Rand2();
    rand &= 0x1F;
    numToAdd -= (uint16_t)(((uint32_t)rand * numToAdd) >> 8);       /* mulu d7,d0; lsr.l #8 */

    if (numToAdd > 25) {                                    /* cmpi.w #$19,d7 */
        numToAdd = 25;
        rand = Rand2();
        numToAdd -= (rand & 7);                             /* andi.w #7,d0; sub.w */
    }

    {
        int16_t i;
        for (i = (int16_t)numToAdd - 1; i >= 0; i--) {     /* subq.w #1; dbf */
            AddCareerTeamPlayer();
        }
    }
}

/* ================================================================
 * AddRandomForeignMarketPlayer  (SWOS2.s line 73108)
 *
 * Adds a random foreign player to the foreign market from the
 * pool player database. 25% chance to enforce price range.
 * ================================================================ */
__attribute__((used, externally_visible))
void AddRandomForeignMarketPlayer(void)
{
    uint32_t priceRange = GetLowestAndHighestPlayerPrice();
    uint8_t lowestPrice = (uint8_t)(priceRange >> 16);     /* d4 */
    uint8_t highestPrice = (uint8_t)(priceRange & 0xFFFF); /* d5 */
    uint8_t homeCountry;
    uint16_t attemptsLeft;
    uint16_t rand;
    uint16_t poolIdx;
    PoolPlayer *poolEntry;
    uint16_t teamId;
    uint8_t playerOrd;

    highestPrice += 3;                                      /* addq.w #3,d5 */
    homeCountry = (uint8_t)(_selTeamNumber >> 8);             /* move.b (selTeamNumber).l — high byte on big-endian */

    attemptsLeft = 1;                                       /* move.w #1,d6 */
    rand = Rand2();
    if ((rand & 3) != 0)                                    /* andi.w #3,d0; bne */
        attemptsLeft = 0xB4;                                /* move.w #$B4,d6 */

retry:
    attemptsLeft--;
    if (attemptsLeft == 0)                                  /* tst.w d6; bne skip */
        _foreignMarketRandomPickRetries++;

    /* Pick random pool entry using GetRandomWord for full 16-bit random */
    rand = GetRandomWord();
    poolIdx = (uint16_t)((uint32_t)rand * POOLPLAYER_COUNT >> 16);  /* mulu #900; lsr.l #8; lsr.l #8 */
    poolEntry = &_poolplyrDatBuffer[poolIdx];

    /* Must be different country. Byte access at offset 0 is endian-
     * safe (the file format stores [country, ordinal] bytes). */
    if (*(uint8_t *)&poolEntry->teamId == homeCountry)      /* cmp.b (a0),d3 */
        goto retry;

    /* Convert poolEntry->teamId (file-format native = byte-swapped on
     * LE) into PACK_BE_U16 format (country << 8 | ordinal) to match
     * the format the foreign-market write/find helpers now expect.
     * Without this conversion, passing the LE-native value
     * downstream would put the wrong bytes into foreignMarketPlayers
     * and the next PlayerMarketBuildVisiblePlayerList →
     * FindPoolplyrDatPlayer would walk the pool buffer forever and
     * SIGSEGV (which is exactly what the user hit). */
    teamId = BE16_TO_HOST(poolEntry->teamId);
    playerOrd = (uint8_t)poolEntry->playerOrdinal;

    /* If attempts remain (price filtering), check price range */
    if (attemptsLeft != 0) {
        uint8_t price = (uint8_t)poolEntry->player.price;   /* player.price at PoolPlayer offset 52 */
        if (lowestPrice > price)                             /* bhi retry */
            goto retry;
        if (highestPrice < price)                            /* bcs retry */
            goto retry;
    }

    /* Check if already in foreign market */
    if (FindForeignMarketPlayer(teamId, playerOrd) >= 0)
        goto retry;                                         /* beq retry */

    AddForeignMarketPlayer(teamId, playerOrd);
}

/* ================================================================
 * FillForeignMarketPlayers  (SWOS2.s line 73048)
 *
 * Populates the foreign market with random players from the pool
 * player database. Adds 8-15 random foreign players.
 * ================================================================ */
__attribute__((used, externally_visible))
void FillForeignMarketPlayers(void)
{
    uint16_t numToAdd;
    int16_t i;

    LoadPoolplyrDat();

    _numForeignMarketPlayers = 0;

    if (_isNationalSelector != 0)
        return;

    numToAdd = 15;                                          /* move.w #$F,d7 */
    numToAdd -= (Rand2() & 7);                             /* andi.w #7,d0; sub.w */
    if ((int16_t)numToAdd <= 0)
        return;

    for (i = (int16_t)numToAdd - 1; i >= 0; i--) {         /* subq.w #1; dbf */
        AddRandomForeignMarketPlayer();
    }
}

/* ================================================================
 * UpdateHomeMarketPlayers  (SWOS2.s line 72897)
 *
 * Randomly removes and adds players in the home market.
 * Number of changes is based on current market size.
 * Re-enters itself until homeMarketUpdateBacklog reaches 0.
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateHomeMarketPlayers(void)
{
restart:
    if (_isNationalSelector != 0)
        return;
    if (_homeMarketUpdateBacklog == 0)
        return;

    _homeMarketUpdateBacklog--;

    {
        uint16_t numChanges = _numHomeMarketPlayers;
        uint16_t numRemoves;
        int16_t i;
        uint16_t rand;

        if (numChanges == 0)
            return;

        numChanges += 15;
        numChanges >>= 4;                                  /* lsr.w #4 */
        if (numChanges == 0)
            return;

        /* Remove phase: randomly pick and remove players */
        numRemoves = numChanges;
        for (i = (int16_t)numRemoves - 1; i >= 0; i--) {
            if (_numHomeMarketPlayers == 0)
                goto add_phase;
            rand = Rand2();
            {
                uint16_t pickIdx = (uint16_t)(((uint32_t)rand * _numHomeMarketPlayers) >> 8);
                uint8_t *entry = _homeMarketPlayers + (uint32_t)pickIdx * MARKET_ENTRY_SIZE;
                uint16_t teamNum = PACK_BE_U16(entry[0], entry[1]);
                uint8_t playerOrd = entry[2];
                RemovePlayerFromCareerContest(teamNum, playerOrd);
            }
        }

    add_phase:
        /* Add phase: add random players until full or done */
        for (i = (int16_t)numChanges - 1; i >= 0; i--) {
            if (_numHomeMarketPlayers >= HOME_MARKET_MAX)
                goto restart;                               /* beq.s → re-enter */
            AddCareerTeamPlayer();
        }
        goto restart;                                       /* bra.s UpdateHomeMarketPlayers */
    }
}

/* ================================================================
 * UpdateForeignMarketPlayers  (SWOS2.s line 73067)
 *
 * Randomly removes and adds players in the foreign market.
 * Number of changes is based on current market size.
 * Re-enters itself until foreignMarketUpdateBacklog reaches 0.
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateForeignMarketPlayers(void)
{
restart:
    if (_isNationalSelector != 0)
        return;
    if (_foreignMarketUpdateBacklog == 0)
        return;

    _foreignMarketUpdateBacklog--;

    {
        uint16_t numChanges = _numForeignMarketPlayers;
        uint16_t numRemoves;
        int16_t i;
        uint16_t rand;

        if (numChanges == 0)
            return;

        numChanges += 15;
        numChanges >>= 4;
        if (numChanges == 0)
            return;

        /* Remove phase */
        numRemoves = numChanges;
        for (i = (int16_t)numRemoves - 1; i >= 0; i--) {
            if (_numForeignMarketPlayers == 0)
                goto add_phase;
            rand = Rand2();
            {
                uint16_t pickIdx = (uint16_t)(((uint32_t)rand * _numForeignMarketPlayers) >> 8);
                uint8_t *entry = _foreignMarketPlayers + (uint32_t)pickIdx * MARKET_ENTRY_SIZE;
                uint16_t teamId = PACK_BE_U16(entry[0], entry[1]);
                uint8_t playerOrd = entry[2];
                RemoveForeignMarketPlayer(teamId, playerOrd);
            }
        }

    add_phase:
        /* Add phase */
        for (i = (int16_t)numChanges - 1; i >= 0; i--) {
            if (_numForeignMarketPlayers >= FOREIGN_MARKET_MAX)
                goto restart;
            AddRandomForeignMarketPlayer();
        }
        goto restart;
    }
}

/* ================================================================
 * FindTeamIdInDiyTeamsList  (SWOS2.s line 78693)
 *
 * Searches the DIY team ID list for a team number.
 * List selected by competition type (0/1/2 → diyTeamIdListType0/1/2).
 * Returns 0 if found, non-zero if not found (maps to Z flag in asm).
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t FindTeamIdInDiyTeamsList(uint16_t *teamIdPtr)
{
    uint8_t *list;
    int16_t count;
    uint16_t target;

    /* slot-0 untangle (S6, D-A1): the three legacy diyTeamIdList* views are the
     * cup/league/tournament team-id regions of primaryCompetitionBuf. Access stays
     * BE_READ_U16 below — only the base symbol changes. */
    if (_primaryCompetitionBuf.tourn.header.competitionType == 1)
        list = (uint8_t *)((DIYCompetitionEx *)&_primaryCompetitionBuf)->cupTeamIds;            /* diyTeamIdListType1 @0x24E */
    else if (_primaryCompetitionBuf.tourn.header.competitionType == 2)
        list = (uint8_t *)((DIYTournamentState *)&_primaryCompetitionBuf)->teamIds;             /* diyTeamIdListType2 @0x9A */
    else
        list = (uint8_t *)((DIYCompetitionEx *)&_primaryCompetitionBuf)->matchData.league.leagueTeamIds; /* diyTeamIdListType0 @0x12E */

    count = (int16_t)(_primaryCompetitionBuf.tourn.header.numTeams - 1);
    /* Both sides read via BE_READ_U16 so they normalise to the same
     * (country<<8)|ord encoding on every host.
     *
     * The diy team-id lists store bytes in Amiga BE byte-pair order
     * — `[country, ordinal]` per entry — same as eurocup team lists
     * (see appendTeamToList in career_eurocups.c). teamIdPtr points
     * at a TeamFile's leading {country, ordinal} byte pair (also BE).
     *
     * c8a509f fixed the target side (BE_READ_U16) but kept the list
     * read as host-native `*list`, which on LE decodes [country, ord]
     * as (ord<<8)|country — opposite endian, no match. That left
     * TOP GOAL SCORERS empty in career mode because every non-career
     * team's FindTeamIdInDiyTeamsList returned "not found" and
     * AddTeamScorersToHighestScorersTable skipped it. */
    target = BE_READ_U16((uint8_t *)teamIdPtr);

    for (; count >= 0; count--) {
        if (BE_READ_U16(list) == target)
            return 0;  /* found */
        list += 2;
    }
    return 1;  /* not found */
}

/* ================================================================
 * IsOfferTeamPlayerPairUnique  (SWOS2.s line 79294)
 *
 * Checks whether (teamId, playerIdx) pair already exists in the
 * transfer offer table. Compares teamId as word, playerIdx as byte
 * against offer bytes 2 (primary) and 3 (extra).
 * Returns non-zero if unique (no match), 0 if duplicate.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsOfferTeamPlayerPairUnique(uint16_t teamId, uint16_t playerIdx)
{
    int16_t count = (int16_t)_numOffers;
    TransferOfferEntry *entry;
    int16_t i;
    uint8_t pidx = (uint8_t)playerIdx;

    if (count == 0)
        return 1;  /* no offers → unique */

    entry = (TransferOfferEntry *)_transferOfferTable;
    for (i = count - 1; i >= 0; i--) {
        /* asm: cmp.w (a1),d0 — host-native WORD compare of the team id.
         * The offer builder writes the entry's teamOffering host-native (whole word), so
         * this must NOT be a PACK_BE byte-pair read (unlike the market tables,
         * which the builder deliberately writes in BE byte order). A PACK_BE
         * read here byte-swaps on LE, so the dedup never matched and the game
         * could generate duplicate (team, player) offers. */
        if (entry->teamOffering == teamId) {
            if ((uint8_t)entry->primaryPlayerOrdinal == pidx)
                return 0;  /* duplicate */
            if ((uint8_t)entry->secondPlayerOrdinal == pidx)
                return 0;  /* duplicate */
        }
        entry++;
    }
    return 1;  /* unique */
}

/* ================================================================
 * IsTeamEligibleForOfferByRefSkill  (SWOS2.s line 79189)
 *
 * Checks if a team has players spanning the reference skill level.
 * Eligible if: any player has exact match, OR team has players both
 * stronger AND weaker than ref skill.
 * Not eligible if: team == selected team, OR league type == 4.
 * Returns 0 if eligible, non-zero if not (maps to Z flag in asm).
 * team points to a team file entry (team number at offset 0,
 * league type at 0x19, player skills at 0x6C with 0x26 stride).
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsTeamEligibleForOfferByRefSkill(TeamFileEx *team, uint16_t refSkill)
{
    TeamFileEx *tfx = team;
    uint16_t teamNum = PACK_BE_U16(tfx->team.countryNumber, tfx->team.teamOrdinal);
    uint16_t flags;
    int16_t i;
    uint8_t ref = (uint8_t)refSkill;

    /* Same as selected team → not eligible */
    if (teamNum == _selTeamNumber)
        return 1;

    /* Outside its country's league → not eligible (was league == 4) */
    if (Team_IsOutsideLeague(&tfx->team))
        return 1;

    flags = 0;
    for (i = 15; i >= 0; i--) {
        uint8_t skill = (uint8_t)tfx->players[i].price;
        if (skill == ref)
            return 0;  /* exact match → eligible */
        if (ref > skill)
            flags |= 2;  /* player weaker than ref */
        if (ref < skill)
            flags |= 1;  /* player stronger than ref */
        if (flags == 3)
            return 0;  /* skill range spans ref → eligible */
    }
    return 1;  /* not eligible */
}

/* ================================================================
 * RemoveOfferPlayerFromMarketsAndCount  (SWOS2.s line 79607)
 *
 * Removes a player from both career contest and foreign market,
 * then increments the appropriate removal counter (selected team
 * or other teams) based on country match.
 * ================================================================ */
__attribute__((used, externally_visible))
void RemoveOfferPlayerFromMarketsAndCount(uint16_t teamId, uint16_t playerIndex)
{
    RemovePlayerFromCareerContest(teamId, playerIndex);
    RemoveForeignMarketPlayer(teamId, playerIndex);

    if ((teamId >> 8) == (_selTeamNumber >> 8))
        _removedOfferPlayersFromSelectedTeamCount++;
    else
        _removedOfferPlayersFromOtherTeamsCount++;
}

/* ================================================================
 * RemoveNegotiatedOfferPlayersFromMarkets  (SWOS2.s line 79592)
 *
 * Removes the primary (and optionally extra) offer player from
 * both career contest and foreign market, counting removals.
 * Uses globals: offerOrdinals.teamOffering, offerOrdinals.offerPrimaryPlayerOrdinal,
 * offerOrdinals.numExtraPlayersWanted.
 * ================================================================ */
__attribute__((used, externally_visible))
void RemoveNegotiatedOfferPlayersFromMarkets(void)
{
    RemoveOfferPlayerFromMarketsAndCount(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    if ((int16_t)_offerOrdinals.numExtraPlayersWanted >= 0)
        RemoveOfferPlayerFromMarketsAndCount(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
}

/* ================================================================
 * PickRandomEligibleExtraPlayerOrdinal  (SWOS2.s line 79219)
 *
 * Picks a random eligible extra player from the selected team.
 * Player must: not be the primary offer player, not on transfer
 * list, not sold/transferred, and have skill <= refSkill.
 * Returns file index (from GetPlayerAtIndex) or -1 if not found.
 * Up to 16 random attempts.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t PickRandomEligibleExtraPlayerOrdinal(uint16_t refSkill)
{
    int16_t retries;
    uint8_t ref = (uint8_t)refSkill;

    for (retries = 15; retries >= 0; retries--) {
        uint16_t rand = Rand2();
        uint16_t ordinal = (uint16_t)(((uint32_t)rand * _careerNumPlayers) >> 8);
        uint8_t *player;
        uint16_t fileIdx;

        player = GetPlayerAtIndex(_selTeamNumber, ordinal);
        fileIdx = _cGetPlayerAtIndexFileIdx;
        {
        PlayerFile *pf = (PlayerFile *)(player + PF_OFF);

        /* Skip if same as primary player */
        if (fileIdx == _offerOrdinals.offerPrimaryPlayerOrdinal)
            continue;

        /* Skip if on transfer list (bit 1 of positionAndFace) */
        if (pf->positionAndFace & PF_LOAN)
            continue;

        /* Skip trialists (form 0x9B / "TRIAL") and reserves (form 0x9A / "RES") */
        if ((uint8_t)pf->form == PLAYER_FORM_TRIALIST)
            continue;
        if ((uint8_t)pf->form == PLAYER_FORM_RESERVE)
            continue;

        /* Skill must be <= refSkill (unsigned) */
        if ((uint8_t)pf->price > ref)
            continue;

        return (int16_t)fileIdx;
        }
    }
    return -1;  /* not found */
}

/* ================================================================
 * PickRandomEligibleSwapPlayerOrdinalFromTeam  (SWOS2.s line 79246)
 *
 * Phase 1: Scans selected team to find min/max skill range.
 * Phase 2: Picks a random player (0-15) from the offering team
 * whose skill falls within [min, max] of the selected team.
 * Player must not: be excluded slot, be on transfer list,
 * be sold/transferred, or have skill outside the range.
 * Returns player index in offering team or -1 if not found.
 * Up to 16 random attempts in Phase 2.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t PickRandomEligibleSwapPlayerOrdinalFromTeam(TeamFileEx *teamBlock, int16_t excludeSlot)
{
    uint8_t minSkill = 0xFF;
    uint8_t maxSkill = 0;
    int16_t i;
    int16_t retries;

    /* Phase 1: Find min/max skill in selected team */
    for (i = (int16_t)(_careerNumPlayers - 1); i >= 0; i--) {
        uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
        PlayerFile *pf1 = (PlayerFile *)(player + PF_OFF);
        uint8_t skill = (uint8_t)pf1->price;
        if (skill < minSkill)
            minSkill = skill;
        if (skill > maxSkill)
            maxSkill = skill;
    }

    /* Phase 2: Pick random player from offering team within skill range */
    for (retries = 15; retries >= 0; retries--) {
        uint16_t rand = Rand2();
        uint16_t idx = (uint16_t)((rand * 16) >> 8);
        uint8_t skill;

        if ((int16_t)idx == excludeSlot)
            continue;

        PlayerFile *pf2 = &teamBlock->players[idx];

        /* Skip if on transfer list */
        if (pf2->positionAndFace & PF_LOAN)
            continue;

        /* Skip if sold or transferred */
        if ((uint8_t)pf2->form == PLAYER_FORM_TRIALIST)
            continue;
        if ((uint8_t)pf2->form == PLAYER_FORM_RESERVE)
            continue;

        /* Must be within selected team's skill range */
        skill = (uint8_t)pf2->price;
        if (skill < minSkill)
            continue;
        if (skill > maxSkill)
            continue;

        return (int16_t)idx;
    }
    return -1;  /* not found */
}

/* ================================================================
 * FindPlayerOnTransferMarket  (SWOS2.s line 73022)
 *
 * Linear search in playersOnTransferMarket[] for playerId.
 *
 * Asm register convention:
 *   in:  d1.w = playerId
 *   out: d0.w = 0 (found, Z=1), 1 (not found, Z=0)
 *        d2.w = index of found entry  (only valid when found)
 *        a1   = pointer to found entry (only valid when found)
 *
 * C returns: 0 = found, 1 = not found
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t FindPlayerOnTransferMarket(uint16_t playerId)
{
    uint16_t count = _numPlayersOnTransferMarket;
    uint16_t *p;
    uint16_t i;

    if (count == 0)
        return 1;

    p = _playersOnTransferMarket;
    for (i = 0; i < count; i++) {
        if (p[i] == playerId)
            return 0;   /* found */
    }
    return 1;   /* not found */
}

/* ================================================================
 * AddPlayerToTransferMarket  (SWOS2.s line 72992)
 *
 * Appends a playerId to the transfer market list (max 5 entries).
 *
 * Asm register convention:
 *   in:  d0.w = playerId
 * ================================================================ */
__attribute__((used, externally_visible))
void AddPlayerToTransferMarket(uint16_t playerId)
{
    uint16_t count;

    if (_numPlayersOnTransferMarket >= 5)
        return;

    count = _numPlayersOnTransferMarket;
    _playersOnTransferMarket[count] = playerId;
    _numPlayersOnTransferMarket = count + 1;
}

/* ================================================================
 * RemovePlayerFromTransferMarket  (SWOS2.s line 73005)
 *
 * Finds and removes a playerId from the transfer market list.
 * Uses shift-left to compact the array.
 *
 * Asm register convention:
 *   in:  d1.w = playerId
 * ================================================================ */
__attribute__((used, externally_visible))
void RemovePlayerFromTransferMarket(uint16_t playerId)
{
    uint16_t count = _numPlayersOnTransferMarket;
    uint16_t *p = _playersOnTransferMarket;
    uint16_t i;
    int16_t foundIdx = -1;

    /* Find the player */
    if (count == 0)
        return;
    for (i = 0; i < count; i++) {
        if (p[i] == playerId) {
            foundIdx = (int16_t)i;
            break;
        }
    }
    if (foundIdx < 0)
        return;

    /* Shift entries left to fill gap */
    {
        uint16_t remaining = count - (uint16_t)foundIdx - 1;
        volatile uint16_t *dst = (volatile uint16_t *)&p[foundIdx];
        uint16_t j;
        for (j = 0; j < remaining; j++)
            dst[j] = dst[j + 1];
    }
    _numPlayersOnTransferMarket = count - 1;
}

/* ================================================================
 * CheckForTransferOffers  (SWOS2.s line 72748)
 *
 * Searches transferOfferTable for any offer with byte 0x0D == 0.
 *
 * Asm register convention:
 *   out: d0.w = 0 (got offers, Z=1), 1 (no offers, Z=0)
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t CheckForTransferOffers(void)
{
    uint16_t count = _numOffers;
    TransferOfferEntry *p;
    uint16_t i;

    if (count == 0)
        return 1;

    p = (TransferOfferEntry *)_transferOfferTable;
    for (i = 0; i < count; i++) {
        if (p->seenFlag == 0)
            return 0;   /* found an active offer (seen flag at byte 0x0D) */
        p++;
    }
    return 1;   /* no active offers */
}

/* ================================================================
 * Career_CommitNewBalanceAndResetFinanceTotals  (SWOS2.s line 86770)
 *
 * If financeTotalsNeedResetFlag is set, clears it, copies newBalance
 * to old_balance, and zeroes all financial tracking variables.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_CommitNewBalanceAndResetFinanceTotals(void)
{
    if (_financeTotalsNeedResetFlag == 0)
        return;

    _financeTotalsNeedResetFlag = 0;
    _old_balance = _newBalance;
    _playerPurchases = 0;
    _playerSales = 0;
    _playerWagesBill = 0;
    _generalRunningCosts = 0;
    _gateReceipts = 0;
    _startingBalanceCopy = 0;
    _competitionBonuses = 0;
}

/* ================================================================
 * AddCareerPoolPlayer  (SWOS2.s line 71391)
 *
 * Adds a player to careerForeignMarketPlayers pool.
 * Entry: 42 bytes (word teamNum + byte playerOrd + byte randomPhase
 *        + 38 bytes player data from players[]).
 *
 * Asm register convention:
 *   in:  d0.w = teamNumber, d1.b = playerOrdinal, a0 = teamFilePtr
 * ================================================================ */
__attribute__((used, externally_visible))
void AddCareerPoolPlayer(uint16_t teamNumber, uint16_t playerOrdinal, PlayerFile *playerFile)
{
    uint16_t idx = _numForeignMarketPlayers2;
    MarketPlayer *entry = &_careerForeignMarketPlayers[idx];
    volatile uint16_t *dst;
    uint16_t *src;
    uint16_t i;

    /* Header: team number (word), player ordinal (byte), randomPhase (byte).
     *
     * teamId MUST be stored in Amiga byte order. The asm writes it with
     * `move.w d0,(a6)` (SWOS2.s:96367) and the only reader,
     * Career_PatchTeamWithForeignMarketPlayers, compares it BYTE BY BYTE
     * against the team record's separate countryNumber/teamOrdinal fields
     * (`cmp.b (a5),d5 / cmp.b 1(a5),d4`, SWOS2.s:96497) — so byte 0 has to be
     * the country and byte 1 the ordinal.
     *
     * A plain `entry->teamId = teamNumber` is that on m68k and the REVERSE on
     * a little-endian host, so the patch never matched a single entry on SDL.
     * That is not cosmetic: the patch is how a change to a team that is NOT in
     * the career pool survives. GetTeamPointer resolves such a team by loading
     * the country's team file into the scratch teamFileBuffer, and the patch
     * re-applies the career's edits on top of that fresh copy every time. With
     * the compare dead, the edit was written into scratch memory and lost on
     * the next lookup — so a player bought from a club outside your pool came
     * BACK, and the game then had him in both squads, scoring for both. Exactly
     * the tester's "he reappears on his team, he gets duplicated".
     *
     * Measured before the fix: buying slot 1 of team 0x0100 wrote the entry
     * bytes 0x00,0x01 where the reader wants 0x01,0x00. */
    BE_WRITE_U16((uint8_t *)&entry->teamId, teamNumber);
    entry->playerOrdinal = (int8_t)playerOrdinal;
    entry->padding = (int8_t)_viewRandomPhase;

    /* Copy 19 words (38 bytes) of player data from players[] offset */
    dst = (volatile uint16_t *)&entry->player;
    src = (uint16_t *)&playerFile[2];
    for (i = 0; i < 19; i++)
        dst[i] = src[i];

    _numForeignMarketPlayers2 = idx + 1;
    _pendingForeignPlayerCount = _pendingForeignPlayerCount + 1;
    _lastLoadedExtNumber = 255;
}

/* ================================================================
 * Career_PatchTeamWithForeignMarketPlayers  (SWOS2.s line 71515)
 *
 * Iterates careerForeignMarketPlayers pool; for each entry whose
 * team number matches the target team, copies the 19-word player
 * data into the team's player slot.
 *
 * Asm register convention:
 *   in:  a0 = teamPtr (pointer to TeamFile header)
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_PatchTeamWithForeignMarketPlayers(TeamFileEx *teamPtr)
{
    uint16_t count;
    MarketPlayer *entry;
    uint16_t i;
    uint8_t teamHi, teamLo;

    if (_gameType != GAME_TYPE_CAREER)
        return;

    count = _numForeignMarketPlayers2;
    if (count == 0)
        return;

    /* Team number is first word — compare bytes (big-endian) */
    teamHi = teamPtr->team.countryNumber;
    teamLo = teamPtr->team.teamOrdinal;

    entry = _careerForeignMarketPlayers;
    for (i = 0; i < count; i++) {
        if (*(uint8_t *)&entry->teamId == teamHi && *((uint8_t *)&entry->teamId + 1) == teamLo) {
            uint8_t playerOrd = (uint8_t)entry->playerOrdinal;
            volatile uint16_t *dst = (volatile uint16_t *)&teamPtr->players[playerOrd];
            uint16_t *src = (uint16_t *)&entry->player;
            uint16_t j;
            for (j = 0; j < 19; j++)
                dst[j] = src[j];
        }
        entry++;
    }
}

/* ================================================================
 * Static helper: applyForeignMarketPlayersToTeams
 *
 * Shared loop for Career_ApplyForeignMarketPlayersToSelectedTeams
 * and Career_ApplyPendingForeignMarketPlayersToSelectedTeams.
 * ================================================================ */
static void applyForeignMarketPlayersToTeams(uint16_t count)
{
    MarketPlayer *entry;
    uint16_t i;

    if (_gameType != GAME_TYPE_CAREER)
        return;

    if (count == 0)
        return;

    entry = _careerForeignMarketPlayers;
    for (i = 0; i < count; i++) {
        uint8_t entryHi = *(uint8_t *)&entry->teamId;
        uint8_t entryLo = *((uint8_t *)&entry->teamId + 1);
        TeamFileEx *team = _selectedTeams;
        uint16_t t;

        for (t = 0; t < _numSelectedTeams; t++) {
            if (team->team.countryNumber == entryHi &&
                team->team.teamOrdinal  == entryLo) {
                uint8_t playerOrd = (uint8_t)entry->playerOrdinal;
                volatile uint16_t *dst = (volatile uint16_t *)&team->players[playerOrd];
                uint16_t *src = (uint16_t *)&entry->player;
                uint16_t j;
                for (j = 0; j < 19; j++)
                    dst[j] = src[j];
                break;
            }
            team++;
        }
        entry++;
    }
}

/* ================================================================
 * Career_ApplyForeignMarketPlayersToSelectedTeams  (SWOS2.s line 71459)
 *
 * Applies ALL foreign market player entries to selected teams.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_ApplyForeignMarketPlayersToSelectedTeams(void)
{
    applyForeignMarketPlayersToTeams(_numForeignMarketPlayers2);
}

/* ================================================================
 * Career_ApplyPendingForeignMarketPlayersToSelectedTeams  (SWOS2.s line 71469)
 *
 * Applies only the non-pending foreign market player entries
 * to selected teams (total minus pending count).
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_ApplyPendingForeignMarketPlayersToSelectedTeams(void)
{
    uint16_t total = _numForeignMarketPlayers2;
    uint16_t pending = _pendingForeignPlayerCount;
    uint16_t applied = total - pending;
    applyForeignMarketPlayersToTeams(applied);
}

/* ================================================================
 * DIY_AdvanceFixtureIndex  (SWOS2.s line 71405)
 *
 * Applies pending foreign market players to selected teams for
 * players whose randomPhase falls within [lo..hi].
 * Only runs in career mode (gameType == 4).
 * ================================================================ */
__attribute__((used, externally_visible))
void DIY_AdvanceFixtureIndex(uint16_t lo, uint16_t hi)
{
    if (_gameType != GAME_TYPE_CAREER)
        return;

    uint16_t pending = _pendingForeignPlayerCount;
    if (pending == 0)
        return;

    /* Point to the start of pending entries (after non-pending ones) */
    uint16_t nonPending = _numForeignMarketPlayers2 - _pendingForeignPlayerCount;
    MarketPlayer *entry = &_careerForeignMarketPlayers[nonPending];

    uint16_t i;
    for (i = 0; i < pending; i++) {
        uint8_t phase = (uint8_t)entry->padding;  /* randomPhase byte */

        /* Check if phase is within [lo..hi] range */
        if (phase >= (uint8_t)lo && phase <= (uint8_t)hi) {
            uint8_t entryHi = *(uint8_t *)&entry->teamId;       /* team number high byte */
            uint8_t entryLo = *((uint8_t *)&entry->teamId + 1); /* team number low byte */
            TeamFileEx *team = _selectedTeams;
            uint16_t t;

            for (t = 0; t < _numSelectedTeams; t++) {
                if (team->team.countryNumber == entryHi &&
                    team->team.teamOrdinal  == entryLo) {
                    /* Found matching team — copy player data */
                    int8_t playerOrd = entry->playerOrdinal;
                    int16_t ord = (int16_t)playerOrd;
                    volatile uint16_t *dst = (volatile uint16_t *)&team->players[(uint16_t)ord];
                    uint16_t *src = (uint16_t *)&entry->player;
                    uint16_t j;
                    for (j = 0; j < 19; j++)
                        dst[j] = src[j];
                    break;
                }
                team++;
            }
        }
        entry++;
    }
}

/* ================================================================
 * Career_RemovePlayerFromTeamInternal  (SWOS2.s line 71521)
 *
 * Removes a player from transfer offers, and then either from
 * the transfer market list (if it's the selected team) or from
 * the career contest and foreign market (if another team).
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_RemovePlayerFromTeamInternal(uint16_t teamNumber, uint16_t playerOrd)
{
    RemovePlayerFromTransferOffers(teamNumber, playerOrd);

    if (teamNumber == _selTeamNumber) {
        RemovePlayerFromTransferMarket(playerOrd);
        if (playerOrd == _selTeamSelectedPlayerPosition) {
            _selTeamSelectedPlayerPosition = 0xFFFF;
        }
    } else {
        RemovePlayerFromCareerContest(teamNumber, playerOrd);
        RemoveForeignMarketPlayer(teamNumber, playerOrd);
    }
}

/* ================================================================
 * Transfer/Offer menu callbacks (Chunk 113, SWOS2.s lines 71942-72592)
 * ================================================================ */

extern uint16_t _transferPlayerMenuResult;
extern uint16_t _thirdPartyOfferResult;

/* TransferPlayerOnSelect10: reject transfer (0xFFFF) */
__attribute__((used, externally_visible))
void TransferPlayerOnSelect10(void)
{
    _transferPlayerMenuResult = 0xFFFF;
    SetExitMenuFlag();
}

/* TransferPlayerOnSelect11: accept transfer (0) */
__attribute__((used, externally_visible))
void TransferPlayerOnSelect11(void)
{
    _transferPlayerMenuResult = 0;
    SetExitMenuFlag();
}

/* TransferPlayerOnSelect12: counter-offer transfer (1) */
__attribute__((used, externally_visible))
void TransferPlayerOnSelect12(void)
{
    _transferPlayerMenuResult = 1;
    SetExitMenuFlag();
}

/* AcceptOffer: accept third-party offer (1) */
__attribute__((used, externally_visible))
void AcceptOffer(void)
{
    _thirdPartyOfferResult = 1;
    SetExitMenuFlag();
}

/* RejectOffer: reject third-party offer (2) */
__attribute__((used, externally_visible))
void RejectOffer(void)
{
    _thirdPartyOfferResult = 2;
    SetExitMenuFlag();
}

/* AskForMore: negotiate third-party offer (3) */
__attribute__((used, externally_visible))
void AskForMore(void)
{
    _thirdPartyOfferResult = 3;
    SetExitMenuFlag();
}

/* ConsiderOffer: consider third-party offer (0) */
__attribute__((used, externally_visible))
void ConsiderOffer(void)
{
    _thirdPartyOfferResult = 0;
    SetExitMenuFlag();
}

/* ThirdPartyOfferMenuExit: exit offer menu (0) */
__attribute__((used, externally_visible))
void ThirdPartyOfferMenuExit(void)
{
    _thirdPartyOfferResult = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * Chunk 117: Transfer Menu Helpers
 * ================================================================ */

extern MenuEntry _menuEntries[];
extern void GetBankBalance(uint8_t *dst);
extern void CheckNumPlayersOnTransferMarket(void);  /* still in asm */
extern void CheckIfPlayerSelected(void);             /* still in asm */
extern void EnableDisableOffers(void);               /* still in asm */

/* TransferMenuSetBankState — write bank balance to entry 1's string (SWOS2.s line 76190) */
__attribute__((used, externally_visible))
void TransferMenuSetBankState(void)
{
    uint8_t *str = (uint8_t *)_menuEntries[1].fg;
    GetBankBalance(str);
}

/* BuyPlayerInit — write bank balance to entry 1's string (SWOS2.s line 75647) */
__attribute__((used, externally_visible))
void BuyPlayerInit(void)
{
    uint8_t *str = (uint8_t *)_menuEntries[1].fg;
    GetBankBalance(str);
}

/* TransferMarketFinish — refresh all transfer menu state (SWOS2.s line 76181) */
__attribute__((used, externally_visible))
void TransferMarketFinish(void)
{
    TransferMenuSetBankState();
    CheckNumPlayersOnTransferMarket();
    CheckIfPlayerSelected();
    EnableDisableOffers();
}

/* ShowDomesticPlayerMarket — update home market, show market menu (SWOS2.s line 70638) */
extern uint16_t _showDomesticPlayerMarket;
extern uint8_t _showPlayerMarketMenu[];

__attribute__((used, externally_visible))
void ShowDomesticPlayerMarket(void)
{
    UpdateHomeMarketPlayers();
    _showDomesticPlayerMarket = 1;
    ShowMenu(_showPlayerMarketMenu);
}

/* ---- Chunk 142: ViewDomesticPlayerMarket ---- */
/* SWOS2.s line 71089 — show domestic market, check if transfer completed */
extern uint16_t _transferCompleted;

__attribute__((used, externally_visible))
void ViewDomesticPlayerMarket(void)
{
    ShowDomesticPlayerMarket();
    /* Original asm has duplicate check — preserve as-is */
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
}

/* ---- Chunk 144: ShowForeignPlayerMarket ---- */
/* SWOS2.s line 66050 — load pool data, maybe add random player, show foreign market menu */
__attribute__((used, externally_visible))
void ShowForeignPlayerMarket(void)
{
    LoadPoolplyrDat();
    if (_removedOfferPlayersFromOtherTeamsCount != 0) {
        _removedOfferPlayersFromOtherTeamsCount--;
        AddRandomForeignMarketPlayer();
    }
    UpdateForeignMarketPlayers();
    _showDomesticPlayerMarket = 0;
    ShowMenu(_showPlayerMarketMenu);
}

/* ---- Chunk 144: ViewForeignPlayerMarket ---- */
/* SWOS2.s line 70889 — show foreign market, check if transfer completed */
__attribute__((used, externally_visible))
void ViewForeignPlayerMarket(void)
{
    ShowForeignPlayerMarket();
    /* Original asm has duplicate check — preserve as-is */
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
}

/* ---- Chunk 145: PlayerOffersUpdateOfferAmountText ---- */
/* SWOS2.s line 68515 — format offer amount * 1000 into menu entry 9's text string */
__attribute__((used, externally_visible))
void PlayerOffersUpdateOfferAmountText(void)
{
    MenuEntry *entry = &_menuEntries[9]; /* 0x1F8 / 56 = 9 */
    uint8_t *dst = (uint8_t *)entry->fg;
    uint32_t amount = (uint32_t)_offerOrdinals.offerAmount * 1000;
    Int2Pounds(amount, dst);
}

/* ---- Chunk 145: UpdatePlayerOfferMenuIndicators ---- */
/* SWOS2.s line 68243 — update +/- indicators and visibility for offer menu entries */
__attribute__((used, externally_visible))
void UpdatePlayerOfferMenuIndicators(void)
{
    extern uint8_t _aPlus[];   /* "+" */
    extern uint8_t _aMinus[];  /* "-" */

    /* Entry 8 (0x1C0/56): toggle +/- based on offerOrdinals.numExtraPlayersWanted sign */
    MenuEntry *entry8 = &_menuEntries[8];
    entry8->fg = (uintptr_t)_aPlus;
    if (_offerOrdinals.numExtraPlayersWanted >= 0) {
        entry8->fg = (uintptr_t)_aMinus;
    }

    /* Entries 10,11 (0x230/56, 0x268/56): toggle +/- based on swap/extra player state */
    MenuEntry *entry10 = &_menuEntries[10];
    MenuEntry *entry11 = &_menuEntries[11];
    entry10->fg = (uintptr_t)_aPlus;
    entry11->fg = (uintptr_t)_aPlus;
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        entry10->fg = (uintptr_t)_aMinus;
        if (_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            entry11->fg = (uintptr_t)_aMinus;
        }
    }

    /* Entry 5 (0x118/56): hide if offerOrdinals.numExtraPlayersWanted < 0 */
    _menuEntries[5].isInvisible = 1;
    if (_offerOrdinals.numExtraPlayersWanted >= 0) {
        _menuEntries[5].isInvisible = 0;
    }

    /* Entries 6,11 (0x150/56, 0x268/56): hide if offerOrdinals.offerSwapPlayerOrdinal < 0 */
    _menuEntries[6].isInvisible = 1;
    _menuEntries[11].isInvisible = 1;
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        _menuEntries[6].isInvisible = 0;
        _menuEntries[11].isInvisible = 0;
    }

    /* Entry 7 (0x188/56): hide if offerOrdinals.offerExtraPlayerOrdinal < 0 */
    _menuEntries[7].isInvisible = 1;
    if (_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
        _menuEntries[7].isInvisible = 0;
    }
}

/* ---- Chunk 145: PlayerOffersInit ---- */
/* SWOS2.s line 68219 — initialize player offers menu: amount text, bank balance,
 * team names in formatted strings, update indicators */
__attribute__((used, externally_visible))
void PlayerOffersInit(void)
{
    PlayerOffersUpdateOfferAmountText();

    /* Entry 1 (0x38/56): bank balance text */
    MenuEntry *entry1 = &_menuEntries[1];
    uint8_t *bankDst = (uint8_t *)entry1->fg;
    GetBankBalance(bankDst);

    /* Entry 3 (0xA8/56): "WE HERE AT %a WISH TO BUY" with our team name */
    TeamFileEx *ourTeam = GetTeamPointer(_selTeamNumber);
    uint8_t *ourName = (uint8_t *)ourTeam->team.teamName;
    MenuEntry *entry3 = &_menuEntries[3];
    uint8_t *dst3 = (uint8_t *)entry3->fg;
    PrintFormatted((const uint8_t *)"WE HERE AT %a WISH TO BUY", dst3,
                    0, 0, 0, 0, 0, 0, 0, ourName, 0, 0);

    /* Entry 4 (0xE0/56): "FROM %a IN EXCHANGE FOR" with offering team name */
    TeamFileEx *offeringTeam = GetTeamPointer(_offerOrdinals.teamOffering);
    uint8_t *offeringName = (uint8_t *)offeringTeam->team.teamName;
    MenuEntry *entry4 = &_menuEntries[4];
    uint8_t *dst4 = (uint8_t *)entry4->fg;
    PrintFormatted((const uint8_t *)"FROM %a IN EXCHANGE FOR", dst4,
                    0, 0, 0, 0, 0, 0, 0, offeringName, 0, 0);

    UpdatePlayerOfferMenuIndicators();
}

/* ---- Chunk 146: GenerateRandomTransferOffers ---- */
/* SWOS2.s line 71753 — loop through career players starting at random offset,
 * try to create transfer offers for eligible players. */

/* CreateRandomTransferOfferForPlayer declared in transfer_market.h */

__attribute__((used, externally_visible))
void GenerateRandomTransferOffers(void)
{
    uint16_t startIdx = Rand2();
    startIdx = (uint16_t)((uint32_t)startIdx * _careerNumPlayers >> 8);
    uint16_t idx = startIdx;
    int16_t count = _careerNumPlayers - 1;

    SWOS_AUTOSKIP_LOG("[bug2] GenerateRandomTransferOffers ENTER: careerNumPlayers=%u "
                      "numOffers=%u numPlayersOnTransferMarket=%u startIdx=%u",
                      (unsigned)_careerNumPlayers, (unsigned)_numOffers,
                      (unsigned)_numPlayersOnTransferMarket, (unsigned)startIdx);
    /* Show what player ordinals the user has listed */
    for (uint16_t k = 0; k < _numPlayersOnTransferMarket; k++) {
        SWOS_AUTOSKIP_LOG("[bug2]   playersOnTransferMarket[%u] = %u",
                          (unsigned)k, (unsigned)_playersOnTransferMarket[k]);
    }

    int boostedFound = 0;
    int offersCreated = 0;
    while (count >= 0) {
        if (_numOffers == 5) {
            SWOS_AUTOSKIP_LOG("[bug2]   numOffers reached 5, stopping");
            return;
        }

        uint8_t *player = GetPlayerAtIndex(_selTeamNumber, idx);
        PlayerFile *pf = (PlayerFile *)(player + PF_OFF);
        uint16_t fileIdx = _cGetPlayerAtIndexFileIdx;
        /* Check bit 1 of positionAndFace — skip if on transfer list */
        if (!(pf->positionAndFace & PF_LOAN)) {
            /* Skip trialists (form 0x9B) and reserves (form 0x9A) */
            if ((uint8_t)pf->form != PLAYER_FORM_TRIALIST && (uint8_t)pf->form != PLAYER_FORM_RESERVE) {
                uint16_t threshold = 1;
                int onMarket = 0;
                /* _cGetPlayerAtIndexFileIdx holds the file index from last GetPlayerAtIndex call */
                if (FindPlayerOnTransferMarket(fileIdx) == 0) {
                    threshold = 11;
                    onMarket = 1;
                    boostedFound++;
                }
                uint16_t roll = Rand2();
                if (roll < threshold) {
                    SWOS_AUTOSKIP_LOG("[bug2]   idx=%u fileIdx=%u onMarket=%d roll=%u th=%u -> CREATE OFFER",
                                      (unsigned)idx, (unsigned)fileIdx, onMarket,
                                      (unsigned)roll, (unsigned)threshold);
                    CreateRandomTransferOfferForPlayer(fileIdx);
                    offersCreated++;
                } else if (onMarket) {
                    /* Only log misses for players who were boosted, otherwise log
                     * floods every match with all 16 players' default 1/256 rolls. */
                    SWOS_AUTOSKIP_LOG("[bug2]   idx=%u fileIdx=%u onMarket=1 roll=%u th=11 -> miss",
                                      (unsigned)idx, (unsigned)fileIdx, (unsigned)roll);
                }
            } else {
                if (FindPlayerOnTransferMarket(fileIdx) == 0) {
                    SWOS_AUTOSKIP_LOG("[bug2]   idx=%u fileIdx=%u onMarket=1 SKIPPED someFlag=0x%02X",
                                      (unsigned)idx, (unsigned)fileIdx, (unsigned)(uint8_t)pf->form);
                }
            }
        } else {
            if (FindPlayerOnTransferMarket(fileIdx) == 0) {
                SWOS_AUTOSKIP_LOG("[bug2]   idx=%u fileIdx=%u onMarket=1 SKIPPED positionAndFace bit1 set (0x%02X)",
                                  (unsigned)idx, (unsigned)fileIdx,
                                  (unsigned)pf->positionAndFace);
            }
        }

        idx++;
        if (idx == _careerNumPlayers)
            idx = 0;
        count--;
    }
    SWOS_AUTOSKIP_LOG("[bug2] GenerateRandomTransferOffers EXIT: boostedFound=%d offersCreated=%d numOffers=%u",
                      boostedFound, offersCreated, (unsigned)_numOffers);
}

/* ---- Chunk 146: UpdateAndExpireTransferOffers ---- */
/* SWOS2.s line 71727 — decrement offer expiry counters, remove expired offers,
 * then generate new random offers. */
__attribute__((used, externally_visible))
void UpdateAndExpireTransferOffers(void)
{
    if (GetRemainingOfferSwapSlots() == 0)
        return;

    if (_numOffers != 0) {
        TransferOfferEntry *entry = (TransferOfferEntry *)_transferOfferTable;
        uint16_t i = 0;

        while (i < _numOffers) {
            entry->lifespan--;  /* decrement expiry counter (0x0C) */
            if (entry->lifespan == 0) {
                RemoveTransferOfferAtIndex(i);
                /* Don't advance — entry shifted down */
            } else {
                entry++;
                i++;
            }
        }
    }

    GenerateRandomTransferOffers();
}

/* ---- Chunk 146: ProcessOfferExpiryCountdown ---- */
/* SWOS2.s line 71716 — iterative loop: while offerExpiryIterationsRemaining > 0,
 * decrement and call UpdateAndExpireTransferOffers. Skip if national selector. */
__attribute__((used, externally_visible))
void ProcessOfferExpiryCountdown(void)
{
    if (_isNationalSelector != 0)
        return;

    while (_offerExpiryIterationsRemaining != 0) {
        _offerExpiryIterationsRemaining--;
        UpdateAndExpireTransferOffers();
    }
}

/* ---- Chunk 147: BuyOtherPlayersInit + BuyOtherPlayersFinish ---- */
/* SWOS2.s line 69577/69584 — init/return callbacks for Buy Other Players menu */
extern TeamFileEx *_buyOtherPlayersTeamPtr;
extern uint8_t *_buyOtherPlayersTitlePtr;

/* BuyOtherPlayersFinish: set menu entry 0 title and entry 1 team name */
__attribute__((used, externally_visible))
void BuyOtherPlayersFinish(void)
{
    _menuEntries[0].fg = (uintptr_t)_buyOtherPlayersTitlePtr;
    uint8_t *teamName = _buyOtherPlayersTeamPtr->team.teamName;
    _menuEntries[1].fg = (uintptr_t)teamName;
}

/* BuyOtherPlayersInit: get team pointer from offerOrdinals.teamOffering, store it, then finish */
__attribute__((used, externally_visible))
void BuyOtherPlayersInit(void)
{
    _buyOtherPlayersTeamPtr = GetTeamPointer(_offerOrdinals.teamOffering);
    BuyOtherPlayersFinish();
}

/* ================================================================
 * Chunk 161: ViewOffersMenu  (SWOS2.s line 70107)
 *
 * Shows the offers menu. If a transfer was completed, exits menu.
 * Otherwise if no offers exist, jumps to entry 6 and redraws.
 * ================================================================ */
extern uint16_t _transferCompleted;
extern uint8_t _offersMenu[];

__attribute__((used, externally_visible))
void ViewOffersMenu(void)
{
    ShowMenu(_offersMenu);
    if (_transferCompleted != 0) {
        SetExitMenuFlag();
        return;
    }
    if (_numOffers == 0) {
        SetCurrentEntry(6);
        DrawMenu();
    }
}

/* ================================================================
 * Chunk 162: Transfer Market Menu Helpers
 *
 * CheckNumPlayersOnTransferMarket, CheckIfPlayerSelected,
 * EnableDisableOffers, TransferMarketInit, BuyPlayerMenu,
 * GiveFreeTransfer, TakeOffMarketMenu, TransferMarketCommon
 * ================================================================ */

extern uint16_t _playerSelected;
extern volatile uint16_t _currentMessage;
extern uint8_t  _buyPlayerMenu[];
extern uint8_t  _buyPlayerRedrawList[];
extern uint8_t  _aNoOffers[];
extern uint16_t _selTeamNumber;

/* Menu entry offset constants for the transfer market menu entries.
 * These correspond to menuEntries[N] where N = byteOffset / sizeof(MenuEntry).
 * sizeof(MenuEntry) = 0x38 = 56 bytes.
 * MENUENTRY_ISINVISIBLE is at offset 4 within each entry.
 * MENUENTRY_ISDISABLED is at offset 6 within each entry.
 * MENUENTRY_BG + ENTRYTYPE1UNION_BACKANDFRAMECOLOR is at offset 0x1E within each entry.
 * MENUENTRY_STRINGFLAGS is at offset 0x24 within each entry.
 */

/* Menu entry indices derived from byte offsets:
 * 0xE0  / 0x38 = 4  (menuEntries[4])
 * 0x118 / 0x38 = 5  (menuEntries[5])  — offset 0x118 is entry 5 base + 0
 * 0x188 / 0x38 = 7  (menuEntries[7])  — offset 0x188 is entry 7 base - 0x08...
 * Actually: MENUENTRY_ISINVISIBLE + 0xE0 means base + 0xE0 + 4.
 * The MENUENTRY_ALREADYDRAWN offsets add 8 to make entry-relative offsets.
 * menuEntries + MENUENTRY_ALREADYDRAWN + X = &menuEntries[0].alreadyDrawn + X
 * MENUENTRY_ALREADYDRAWN = 8, so the real byte offset is 8 + X.
 * Entry N starts at N * 0x38.
 *
 * 0xE0:  (8 + 0xE0) = 0xE8. 0xE8 / 0x38 = 4 remainder 0x18. entry 4.
 * 0x1C0: (8 + 0x1C0) = 0x1C8. 0x1C8 / 0x38 = 8 remainder 0. entry 8.
 * 0x118: (8 + 0x118) = 0x120. 0x120 / 0x38 = 5 remainder 0x18. entry 5 + MENUENTRY_ISINVISIBLE(4) at 0x11C?
 *
 * Actually the asm uses:
 *   menuEntries+MENUENTRY_ISINVISIBLE+$E0  — this means address of menuEntries + 4 + 0xE0 = menuEntries + 0xE4
 *   0xE4 / 0x38 = 4.06... That's entry 4 at offset 0xE4 - 4*0x38 = 0xE4 - 0xE0 = 4 = MENUENTRY_ISINVISIBLE. Correct!
 *
 *   menuEntries+MENUENTRY_ISINVISIBLE+$1C0 = menuEntries + 4 + 0x1C0 = menuEntries + 0x1C4
 *   0x1C4 / 0x38 = 8.06... Entry 8 at offset 0x1C4 - 8*0x38 = 0x1C4 - 0x1C0 = 4 = MENUENTRY_ISINVISIBLE. Correct!
 *
 *   menuEntries+MENUENTRY_ISINVISIBLE+$118 = menuEntries + 4 + 0x118 = menuEntries + 0x11C
 *   0x11C / 0x38 = 5.07... Entry 5 at offset 0x11C - 5*0x38 = 0x11C - 0x118 = 4 = MENUENTRY_ISINVISIBLE. Correct!
 *
 *   menuEntries+MENUENTRY_ISINVISIBLE+$188 = menuEntries + 4 + 0x188 = menuEntries + 0x18C
 *   0x18C / 0x38 = 7.07... Entry 7 at offset 0x18C - 7*0x38 = 0x18C - 0x188 = 4 = MENUENTRY_ISINVISIBLE. Correct!
 */

/* Chairman message constants */
#define CLUB_UNWILLING_TO_PURCHASE_PLAYERS  1

/* Color constants */
#define PINK_TO_BROWN_9  9

extern void RemovePlayerFromTeam(uint16_t playerOrdinal);
extern void ShowChairmanMessage(void);

/* ================================================================
 * CheckNumPlayersOnTransferMarket  (SWOS2.s line 70068)
 *
 * If 5 players on market, hide "put on market" entry (4).
 * If 0 players, hide "take off market" entry (8).
 * ================================================================ */
static void checkNumPlayersOnTransferMarket(void)
{
    if (_numPlayersOnTransferMarket >= 5) {
        _menuEntries[4].isInvisible = 1;
    } else {
        _menuEntries[4].isInvisible = 0;
    }
    if (_numPlayersOnTransferMarket == 0) {
        _menuEntries[8].isInvisible = 1;
    }
}

__attribute__((used, externally_visible))
void CheckNumPlayersOnTransferMarket(void)
{
    checkNumPlayersOnTransferMarket();
}

/* ================================================================
 * CheckIfPlayerSelected  (SWOS2.s line 70084)
 *
 * If no player selected (playerSelected < 0), hide entries 5 and 7.
 * Otherwise show them.
 * ================================================================ */
static void checkIfPlayerSelected(void)
{
    if ((int16_t)_playerSelected < 0) {
        _menuEntries[5].isInvisible = 1;
        _menuEntries[7].isInvisible = 1;
    } else {
        _menuEntries[5].isInvisible = 0;
        _menuEntries[7].isInvisible = 0;
    }
}

__attribute__((used, externally_visible))
void CheckIfPlayerSelected(void)
{
    checkIfPlayerSelected();
}

/* ================================================================
 * EnableDisableOffers  (SWOS2.s line 70097)
 *
 * Checks for transfer offers and enables/disables the offers menu
 * entry accordingly. If no offers, shows "NO OFFERS" text, disables
 * entry, and grays it out.
 * ================================================================ */
__attribute__((used, externally_visible))
void EnableDisableOffers(void)
{
    /* Entry at offset 0xA8: (8 + 0xA8) = 0xB0 / 0x38 = 3.14... entry 3 */
    MenuEntry *entry = &_menuEntries[3];

    if (CheckForTransferOffers() == 0) {
        /* No offers eligible — set bit 13 of stringFlags */
        entry->stringFlags |= (1 << 13);
    }

    if (_numOffers == 0) {
        entry->fg = (uintptr_t)_aNoOffers;
        entry->isDisabled = 1;
        uint16_t color = entry->bgColor;
        color = (color & 0xFFF0) + PINK_TO_BROWN_9;
        entry->bgColor = color;
    }
}

/* ================================================================
 * TransferMarketInit  (SWOS2.s line 69899)
 *
 * Initializes the transfer market menu: clears transferCompleted,
 * resets playerSelected, checks players on market and offers.
 * If no offers, sets current entry to 3.
 * ================================================================ */
__attribute__((used, externally_visible))
void TransferMarketInit(void)
{
    _transferCompleted = 0;
    _playerSelected = 0xFFFF;
    checkNumPlayersOnTransferMarket();
    if (CheckForTransferOffers() == 0) {
        SetCurrentEntry(3);
    }
    TransferMarketFinish();
}

/* ================================================================
 * BuyPlayerMenu  (SWOS2.s line 69923)
 *
 * Checks if team has room (>= 2 remaining slots). If not, shows
 * chairman message. Otherwise shows buy player menu. If transfer
 * completed, exits menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void BuyPlayerMenu(void)
{
    uint16_t slots = GetRemainingTransferPoolSlots();
    if (slots < 2) {
        _currentMessage = CLUB_UNWILLING_TO_PURCHASE_PLAYERS;
        ShowChairmanMessage();
        return;
    }
    _transferCompleted = 0;
    ShowMenu(_buyPlayerMenu);
    if (_transferCompleted == 1) {
        SetExitMenuFlag();
    }
}

/* ================================================================
 * GiveFreeTransfer  (SWOS2.s line 70000)
 *
 * Removes the selected player from the team, resets selection,
 * updates menu state.
 * ================================================================ */
__attribute__((used, externally_visible))
void GiveFreeTransfer(void)
{
    uint16_t idx = _playerSelected;
    uint16_t playerOrd = _playersOnTransferMarket[idx];
    RemovePlayerFromTeam(playerOrd);
    _playerSelected = 0xFFFF;
    checkNumPlayersOnTransferMarket();
    checkIfPlayerSelected();
    EnableDisableOffers();
    SetCurrentEntry(6);
    DrawMenu();
}

/* ================================================================
 * TakeOffMarketMenu  (SWOS2.s line 70022)
 *
 * Removes the selected player from the transfer market list,
 * resets selection, updates menu state.
 * ================================================================ */
__attribute__((used, externally_visible))
void TakeOffMarketMenu(void)
{
    uint16_t idx = _playerSelected;
    uint16_t playerOrd = _playersOnTransferMarket[idx];
    RemovePlayerFromTransferMarket(playerOrd);
    _playerSelected = 0xFFFF;
    checkNumPlayersOnTransferMarket();
    checkIfPlayerSelected();
    SetCurrentEntry(6);
    DrawMenu();
}

/* ================================================================
 * TransferMarketCommon  (SWOS2.s line 70037)
 *
 * Menu common handler (called with a5=current MenuEntry).
 * Computes player index from entry ordinal, toggles selection,
 * redraws old and new entries, updates button visibility.
 * ================================================================ */
__attribute__((used, externally_visible))
void TransferMarketCommon(void *entryPtr)
{
    MenuEntry *entry = (MenuEntry *)entryPtr;
    /* Compute relative index: entry ordinal minus base entry ordinal */
    /* Base entry is at menuEntries + MENUENTRY_ALREADYDRAWN + 0x1F8
     * = entry (8+0x1F8)/0x38 = 0x200/0x38 = 9.14... = entry 9 */
    uint16_t d7 = entry->ordinal - _menuEntries[9].ordinal;
    if (d7 == _playerSelected)
        d7 = 0xFFFF;

    uint16_t oldSel = _playerSelected;
    _playerSelected = d7;

    /* If old selection was valid, redraw that entry */
    if ((int16_t)oldSel >= 0) {
        uint16_t entryIdx = oldSel + 9;
        MenuEntry *oldEntry = CalcMenuEntryAddress(entryIdx);
        CallDrawMenuItem(oldEntry);
    }

    /* Redraw current entry */
    CallDrawMenuItem(entry);
    checkIfPlayerSelected();

    /* Redraw specific entries from buyPlayerRedrawList */
    DrawMenuItemsWaitForVSync((int16_t *)_buyPlayerRedrawList);
}

/* ================================================================
 * Chunk 163: BuildTransferPlayerMenuEntry + callers
 *
 * BuildTransferPlayerMenuEntry, TransferMarketAfterDrawCommon,
 * ShowPlayerMarketInit, PlayerMarketResetListState,
 * BuyOtherPlayersCommon, BuyOtherPlayersAfterDrawCommon,
 * ViewSquadInit, ViewSquadCommon, ViewSquadAfterDrawCommon,
 * BuyOtherDomesticPlayer, BuyOtherForeignPlayer, BuyPlayerViewSquad
 * ================================================================ */

extern uint8_t  _fundsBuffer[];
extern void *_playerPositionsStringTable[];
extern uint8_t  _shortCountryNames[];
extern uint16_t _isNationalSelector;
extern uint16_t _viewFutureTeamModeFlag;

extern uint8_t *ExtractSurname(uint8_t *name, uint16_t maxLenAndFlags);
extern void FormTopSkillsString(uint8_t *dst, PlayerFile *playerFile);
extern void SetSquadMenuEntryStatusSprite_AfterSim(MenuEntry *entry, PlayerFile *playerFile);
extern void InitPlayerValueDisplayStruct(uint8_t *playerEntry, uint8_t *dst);
extern void FormatPlayerValueOrStatusText(uint8_t *base, uint8_t *dst);

/* ================================================================
 * BuildTransferPlayerMenuEntry  (SWOS2.s line 70078)
 *
 * Populates 12 consecutive menu entries for a transfer player row.
 * If player is NULL, hides all 12 entries.
 * If player name starts with 0xFF, hides entries 2-11.
 *
 * Entries layout:
 *  0: Color/style row header
 *  1: Player number
 *  2: Status sprite
 *  3: Surname
 *  4: Extra string (club name in parentheses)
 *  5: Position
 *  6: Top skills
 *  7-9: Padding (invisible)
 * 10: Country code
 * 11: Value/status text
 * ================================================================ */
__attribute__((used, externally_visible))
void BuildTransferPlayerMenuEntry(void *itemPtr, uint8_t *player,
                                    uint16_t y, int16_t idx,
                                    uint16_t style, uint8_t *extra)
{
    MenuEntry *a0 = (MenuEntry *)itemPtr;

    if (player == 0) {
        /* NULL player: hide all 12 entries */
        int16_t count = 11;
        do {
            a0->isInvisible = 1;
            a0->y = y;
            a0++;
        } while (--count >= 0);
        return;
    }

    /* Entry 0: Style/color row header */
    {
        uint16_t color = a0->bgColor;
        color = (color & 0xFFF0) | style;
        a0->bgColor = color;
    }
    a0->y = y;
    a0->isInvisible = 0;
    a0++;

    /* Entry 1: Player number */
    a0->y = y;
    a0->isInvisible = 1;
    a0->fgWord = (uint16_t)idx;  /* ENTRY_NUMBER: write high word */
    if (idx >= 0)
        a0->isInvisible = 0;
    a0++;

    /* Get PlayerFile pointer from team-base-offset pointer */
    PlayerFile *pf = (PlayerFile *)(player + PF_OFF);

    /* Check if player name byte is 0xFF (empty/skip) */
    if ((uint8_t)pf->playerName[0] == 0xFF) {
        int16_t count = 9;
        do {
            a0->isInvisible = 1;
            a0->y = y;
            a0++;
        } while (--count >= 0);
        return;
    }

    /* Entry 2: Status sprite */
    SetSquadMenuEntryStatusSprite_AfterSim(a0, pf);
    a0->y = y;
    a0->isInvisible = 0;
    a0++;

    /* Entry 3: Surname */
    {
        uint16_t maxNameLen = 0x10;
        if (extra != 0) {
            uint16_t extraLen = GetStringLength(extra);
            maxNameLen = 0x10 - extraLen;
        }
        uint16_t extractParam = 0x0D + maxNameLen;
        uint8_t *nameResult = ExtractSurname((uint8_t *)pf->playerName, extractParam);
        a0->fg = (uintptr_t)nameResult;
        a0->y = y;
        a0->isInvisible = 0;
        a0++;
    }

    /* Entry 4: Extra string (club name in parentheses) */
    a0->isInvisible = 1;
    if (extra != 0) {
        uint8_t *dst = (uint8_t *)a0->fg;
        *dst++ = '(';
        StringCopy(extra, dst);
        /* StringCopy returns end pointer — need to find end of string */
        while (*dst != 0) dst++;
        *dst++ = ')';
        *dst = 0;
        a0->isInvisible = 0;
    }
    a0->y = y;
    a0++;

    /* Entry 5: Position */
    {
        uint16_t posIdx = (pf->positionAndFace & PF_POSITION_MASK) >> 3;
        a0->fg = (uintptr_t)_playerPositionsStringTable[posIdx / 4];
    }
    a0->y = y;
    a0->isInvisible = 0;
    a0++;

    /* Entry 6: Top skills */
    FormTopSkillsString((uint8_t *)a0->fg, (PlayerFile *)player);
    a0->y = y;
    a0->isInvisible = 0;
    a0++;

    /* Entry 7: invisible padding */
    a0->isInvisible = 1;
    a0->y = y;
    a0++;

    /* Entry 8: invisible padding */
    a0->isInvisible = 1;
    a0->y = y;
    a0++;

    /* Entry 9: invisible padding */
    a0->isInvisible = 1;
    a0->y = y;
    a0++;

    /* Entry 10: Country code "(ABC)" */
    {
        uint8_t *dst = (uint8_t *)a0->fg;
        uint16_t countryIdx = (uint8_t)pf->nationality;
        uint16_t offset = countryIdx * 3;
        uint8_t *src = _shortCountryNames + offset;
        *dst++ = '(';
        *dst++ = src[0];
        *dst++ = src[1];
        *dst++ = src[2];
        *dst++ = ')';
        *dst = 0;
    }
    a0->y = y;
    a0->isInvisible = 0;
    a0++;

    /* Entry 11: Value/status text */
    if (_isNationalSelector != 0 && _viewFutureTeamModeFlag == 0) {
        InitPlayerValueDisplayStruct(player, (uint8_t *)a0);
    } else {
        FormatPlayerValueOrStatusText(player, _fundsBuffer);
        a0->fg = (uintptr_t)_fundsBuffer;
    }
    a0->y = y;
    a0->isInvisible = 0;

    /* Set status color in stringFlags */
    {
        uint16_t colorBits = 0xD;
        if ((uint8_t)pf->form == PLAYER_FORM_TRIALIST) {
            /* keep 0xD */
        } else if ((uint8_t)pf->form == PLAYER_FORM_RESERVE) {
            colorBits = 1;
        } else if (pf->positionAndFace & PF_LOAN) {
            colorBits = 0xF;
        } else {
            colorBits = 2;
        }
        uint16_t flags = a0->stringFlags;
        flags = (flags & 0xFFF0) | colorBits;
        a0->stringFlags = flags;
    }
}

/* ================================================================
 * TransferMarketAfterDrawCommon  (SWOS2.s line 70024)
 *
 * AfterDraw callback for transfer market menu rows (a5=entry).
 * Computes player index from entry ordinal offset, gets player
 * data, calls BuildTransferPlayerMenuEntry, then DrawMultipleItems.
 * ================================================================ */
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerOrdinal);

__attribute__((used, externally_visible))
void TransferMarketAfterDrawCommon(void *entryPtr)
{
    MenuEntry *entry = (MenuEntry *)entryPtr;
    /* Base entry is menuEntries[9] */
    uint16_t d6 = entry->ordinal - _menuEntries[9].ordinal;
    uint8_t *player;

    if (d6 >= _numPlayersOnTransferMarket) {
        /* Out of bounds — disable this entry, use NULL player */
        entry->isDisabled = 1;
        player = 0;
    } else {
        /* Valid player index */
        entry->isDisabled = 0;
        uint16_t style = 0;
        if (d6 == _playerSelected)
            style = 0xD;

        uint16_t playerOrd = _playersOnTransferMarket[d6];
        player = GetPlayer(_selTeamNumber, playerOrd);
        d6++;   /* 1-based index for display */

        uint16_t yCoord = entry->y;
        BuildTransferPlayerMenuEntry(&_menuEntries[14], player, yCoord,
                                       (int16_t)d6, style, 0);
        /* Draw 12 entries starting from menuEntries[14] */
        DrawMultipleItems(12, &_menuEntries[14]);
        return;
    }

    /* NULL player path */
    uint16_t yCoord = entry->y;
    BuildTransferPlayerMenuEntry(&_menuEntries[14], player, yCoord,
                                   (int16_t)d6, 0, 0);
    DrawMultipleItems(12, &_menuEntries[14]);
}

/* =============================================================== */
/* Chunk 163 continued: ShowPlayerOfferMenuForPlayer, player market */
/*                      helpers, BuyOtherPlayers common             */
/* =============================================================== */

extern uint8_t _playerOffersMenu[];
extern uint8_t _aPlayersOnForei[];
extern uint8_t _aPlayersOnDomes[];
extern uint8_t _aMore[];
extern uint8_t _aList[];
extern uint16_t _playerMarketFilterPosition;
extern uint16_t _playerMarketFilterRatingIndex;
extern uint16_t _playerMarketFilterAgeGroup;
extern uint16_t _playerMarketFilterCountry;
extern uint16_t _playerMarketTotalMatchCount;
extern uint16_t _playerMarketVisibleItemCount;
extern uint16_t _playerMarketListScrollOffset;

extern TeamFileEx *GetTeamPointer(uint16_t teamId);
extern uint16_t GetAvgTeamValueBaseForCurrentPlayer(uint16_t teamNumber, uint16_t playerIndex);
extern void ShowMenu(uint8_t *packedMenu);
extern void DrawMenu(void);
extern void SetCurrentEntry(uint16_t entryIndex);
extern void SetExitMenuFlag(void);
extern uint8_t *GetPlayerInfoBreakPosition(uint8_t *teamPtr, uint16_t requestedSlot,
                                              uint16_t *outSlot, uint16_t *outPlayerIndex);

/* ShowPlayerOfferMenuForPlayer (Chunk 163)
 * d0 = teamNumber, d1 = playerOrdinal */
__attribute__((used, externally_visible))
void ShowPlayerOfferMenuForPlayer(uint16_t teamNumber, uint16_t playerOrdinal)
{
    TeamFileEx *teamPtr = GetTeamPointer(teamNumber);
    _offerOrdinals.teamOffering = PACK_BE_U16(teamPtr->team.countryNumber, teamPtr->team.teamOrdinal);
    _offerOrdinals.offerPrimaryPlayerOrdinal = playerOrdinal;
    _offerOrdinals.numExtraPlayersWanted = (int16_t)-1;
    _offerOrdinals.offerSwapPlayerOrdinal = (int16_t)-1;
    _offerOrdinals.offerExtraPlayerOrdinal = (int16_t)-1;

    uint16_t avgValue = GetAvgTeamValueBaseForCurrentPlayer(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    uint32_t total = (uint32_t)avgValue * 1000;
    if ((int32_t)total <= _newBalance) {
        _offerOrdinals.offerAmount = avgValue;
    } else {
        int32_t bal = _newBalance;
        if (bal < 0)
            bal = 0;
        _offerOrdinals.offerAmount = (uint16_t)((uint32_t)bal / 1000);
    }
    ShowMenu(_playerOffersMenu);
}

/* ShowPlayerMarketInit (Chunk 163)
 *
 * In SWOS2.s line 102649 this function ends with `bra.w *+4` — a
 * branch to PC+4 that falls through to the very next instruction,
 * which is the start of ShowPlayerMarket. So the init resets the
 * filter state AND THEN runs the title/balance-setup code in
 * ShowPlayerMarket. Without the explicit fall-through call, our
 * SDL port returned after the init and showPlayerMarketMenu's
 * title (entry 0) and balance buffer (entry 1) never got
 * populated — entry 1's STDMENUTEXT placeholder buffer leaked
 * through to the screen. */
__attribute__((used, externally_visible))
void ShowPlayerMarketInit(void)
{
    _playerMarketFilterPosition = 0xFFFF;
    _playerMarketFilterRatingIndex = 0xFFFF;
    _playerMarketFilterAgeGroup = 0xFFFF;
    _playerMarketFilterCountry = 6;
    _playerMarketTotalMatchCount = 0;
    _playerMarketVisibleItemCount = 0;
    _playerMarketListScrollOffset = 0;
    ShowPlayerMarket();   /* SWOS2.s `bra.w *+4` fall-through */
}

/* PlayerMarketUpdateFilterMenuEntryState (Chunk 163)
 * Updates menuEntries[6] color and disabled state based on position filter. */
__attribute__((used, externally_visible))
void PlayerMarketUpdateFilterMenuEntryState(void)
{
    MenuEntry *entry = &_menuEntries[6]; /* 0x150/56 = 6 */
    if (_playerMarketFilterPosition != 0) {
        /* Active: bright blue, enabled */
        entry->bgColor = (entry->bgColor & 0xFFF0) | 13; /* BRIGHT_BLUE_TO_BLUE_13 */
        entry->isDisabled = 0;
    } else {
        /* Inactive: pink/brown, disabled */
        entry->bgColor = (entry->bgColor & 0xFFF0) | 9; /* PINK_TO_BROWN_9 */
        entry->isDisabled = 1;
    }
}

/* ShowPlayerMarket (Chunk 163) */
__attribute__((used, externally_visible))
void ShowPlayerMarket(void)
{
    uint8_t *title;
    if (_showDomesticPlayerMarket)
        title = _aPlayersOnDomes;
    else
        title = _aPlayersOnForei;

    _menuEntries[0].fg = (uintptr_t)title;
    uint8_t *balanceBuf = (uint8_t *)_menuEntries[1].fg;
    GetBankBalance(balanceBuf);
    PlayerMarketUpdateFilterMenuEntryState();
}

/* =============================================================== */
/* Player Market menu callbacks — ported from loc_* asm blocks     */
/* =============================================================== */

extern int16_t _filterMenuRedrawList[];
extern uintptr_t _playerMarketVisibleItems[];
static void showSquadWithTransferMarketMask(void);

/* Helper: update filter entry states and redraw filter entries (was loc_14506E) */
static void playerMarketUpdateFiltersAndRedraw(void)
{
    PlayerMarketUpdateFilterMenuEntryState();
    DrawMenuItemsWaitForVSync(_filterMenuRedrawList);
}

/* PlayerMarketFilterPositionOnSelect (was loc_144EFC)
 * Cycles the position filter through 0→1→2→3→-1(all)→0→... */
__attribute__((used, externally_visible))
void PlayerMarketFilterPositionOnSelect(MenuEntry *entry)
{
    (void)entry;
    _playerMarketFilterPosition++;
    if (_playerMarketFilterPosition == 4)
        _playerMarketFilterPosition = 0xFFFF;
    PlayerMarketResetListState();
    playerMarketUpdateFiltersAndRedraw();
}

/* PlayerMarketFilterAgeGroupOnSelect (was loc_144F90)
 * Cycles the age group filter through 0→1→...→6→-1(all)→0→... */
__attribute__((used, externally_visible))
void PlayerMarketFilterAgeGroupOnSelect(MenuEntry *entry)
{
    (void)entry;
    _playerMarketFilterAgeGroup++;
    if (_playerMarketFilterAgeGroup == 7)
        _playerMarketFilterAgeGroup = 0xFFFF;
    PlayerMarketResetListState();
    playerMarketUpdateFiltersAndRedraw();
}

/* PlayerMarketScrollOnSelect (was loc_144FB0)
 * Scrolls the player market list by 10 entries, wrapping to 0. */
__attribute__((used, externally_visible))
void PlayerMarketScrollOnSelect(MenuEntry *entry)
{
    (void)entry;
    uint16_t d0 = 0;
    uint16_t total = _playerMarketTotalMatchCount;
    if (total != 0) {
        d0 = _playerMarketListScrollOffset + 10;
        if (d0 >= total)
            d0 = 0;
    }
    _playerMarketListScrollOffset = d0;
    PlayerMarketBuildVisiblePlayerList();
    PlayerMarketUpdateMoreListLabel();
    DrawMenu();
}

/* PlayerMarketViewSquadOnSelect (was loc_14500A)
 * Shows the career team squad with transfer market players highlighted. */
__attribute__((used, externally_visible))
void PlayerMarketViewSquadOnSelect(MenuEntry *entry)
{
    (void)entry;
    showSquadWithTransferMarketMask();
}

/* PlayerMarketItemOnSelect (was loc_1450CA)
 * Shows player offer menu for the selected market item. */
__attribute__((used, externally_visible))
void PlayerMarketItemOnSelect(MenuEntry *entry)
{
    /* Compute relative index from entry ordinal vs base entry 10 */
    uint16_t relIdx = entry->ordinal - _menuEntries[10].ordinal;

    /* Get the visible item pointer */
    uint8_t *item = (uint8_t *)_playerMarketVisibleItems[relIdx];
    uint16_t teamNum = PACK_BE_U16(item[0], item[1]);
    int16_t playerOrd = (int8_t)item[2];

    ShowPlayerOfferMenuForPlayer(teamNum, (uint16_t)playerOrd);

    if (_transferCompleted) {
        SetExitMenuFlag();
        return;
    }

    SetCurrentEntry(7);
    PlayerMarketResetListState();
}

/* PlayerMarketItemBeforeDraw (was loc_14510C)
 * beforeDraw callback: sets up visibility and renders each player entry. */
__attribute__((used, externally_visible))
void PlayerMarketItemBeforeDraw(MenuEntry *entry)
{
    entry->isDisabled = 0;

    /* Compute relative index from entry ordinal vs base entry 10 */
    uint16_t relIdx = entry->ordinal - _menuEntries[10].ordinal;
    uint8_t *player = 0;
    uint8_t *extra = 0;

    if (relIdx >= _playerMarketVisibleItemCount) {
        /* Beyond visible count — disable entry */
        entry->isDisabled = 1;
        player = 0;
    } else {
        uint8_t *item = (uint8_t *)_playerMarketVisibleItems[relIdx];

        if (_showDomesticPlayerMarket) {
            /* Domestic player */
            uint16_t teamNum = PACK_BE_U16(item[0], item[1]);
            TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
            extra = (uint8_t *)teamData->team.teamName; /* team name area */
            uint8_t ordinal = item[2];
            player = (uint8_t *)teamData + (uint16_t)ordinal * 0x26;
        } else {
            /* Foreign player */
            uint16_t teamNum = PACK_BE_U16(item[0], item[1]);
            uint8_t playerOrd = item[2];
            PoolPlayer *pp = FindPoolplyrDatPlayer(teamNum, playerOrd);
            if (pp) {
                extra = (uint8_t *)pp + 3; /* name area */
                player = (uint8_t *)pp - 0x38;
            } else {
                /* Stale market entry — pool entry no longer exists
                 * (legacy bad-saved data). Disable this row rather
                 * than dereference NULL. */
                entry->isDisabled = 1;
                player = 0;
            }
        }
    }

    /* Build the menu entry for this player */
    uint16_t idx = relIdx + 1 + _playerMarketListScrollOffset;
    uint16_t yCoord = entry->y;

    BuildTransferPlayerMenuEntry(&_menuEntries[20], player, yCoord,
                                   (int16_t)idx, 0, extra);
    DrawMultipleItems(12, &_menuEntries[20]);
}

/* PlayerMarketUpdateMoreListLabel (Chunk 163)
 * Sets menuEntries[7].fg to "MORE" or "LIST" based on scroll state. */
__attribute__((used, externally_visible))
void PlayerMarketUpdateMoreListLabel(void)
{
    uint16_t total = _playerMarketTotalMatchCount;
    MenuEntry *entry = &_menuEntries[7]; /* 0x188/56 = 7 */
    uint16_t offset = _playerMarketListScrollOffset + 10;
    if (offset < total)
        entry->fg = (uintptr_t)_aMore;
    else
        entry->fg = (uintptr_t)_aList;
}

/* PlayerMarketResetListState (Chunk 163) */
__attribute__((used, externally_visible))
void PlayerMarketResetListState(void)
{
    if (_playerMarketTotalMatchCount == 0)
        return;
    _playerMarketTotalMatchCount = 0;
    _playerMarketVisibleItemCount = 0;
    _playerMarketListScrollOffset = 0;
    PlayerMarketUpdateMoreListLabel();
    DrawMenu();
}

/* BuyOtherPlayersCommon (Chunk 163)
 * a5 = current menu entry (register bridge) */
__attribute__((used, externally_visible))
void BuyOtherPlayersCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    MenuEntry *entry3 = &_menuEntries[3]; /* 0xA8/56 = 3 */
    uint16_t relIdx = a5->ordinal - entry3->ordinal;

    uint16_t outSlot, outPlayerIndex;
    GetPlayerInfoBreakPosition((uint8_t *)_buyOtherPlayersTeamPtr, relIdx, &outSlot, &outPlayerIndex);

    ShowPlayerOfferMenuForPlayer(_offerOrdinals.teamOffering, outPlayerIndex);

    if (_transferCompleted) {
        SetExitMenuFlag();
        return;
    }
    SetCurrentEntry(2);
    DrawMenu();
}

/* BuyOtherPlayersAfterDrawCommon (Chunk 163)
 * a5 = current menu entry (register bridge) */
__attribute__((used, externally_visible))
void BuyOtherPlayersAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    MenuEntry *entry3 = &_menuEntries[3]; /* 0xA8/56 = 3 */
    uint16_t d6 = a5->ordinal - entry3->ordinal;
    uint16_t d1 = d6;
    d6++;

    uint16_t outSlot, outPlayerIndex;
    uint8_t *player = GetPlayerInfoBreakPosition((uint8_t *)_buyOtherPlayersTeamPtr,
                                                    d1, &outSlot, &outPlayerIndex);

    uint16_t yCoord = a5->y;
    BuildTransferPlayerMenuEntry(&_menuEntries[19], player, yCoord,
                                   (int16_t)d6, 0, 0);
    DrawMultipleItems(12, &_menuEntries[19]);
}

/* =============================================================== */
/* Chunk 164: ShowSquad callers + OffersInit                       */
/* =============================================================== */

extern TeamFileEx _careerTeam;
extern uint8_t _aViewSquad[];
extern int16_t ShowSquad(TeamFile *teamPtr, uint8_t *title, uint32_t maskLo,
                           uint32_t maskHi, uint16_t arg2, uint16_t arg3);

/* Helper: build bitmask of players on transfer market, then show squad */
static void showSquadWithTransferMarketMask(void)
{
    uint32_t mask = 0;
    uint16_t n = _numPlayersOnTransferMarket;
    if (n) {
        uint16_t i;
        for (i = 0; i < n; i++)
            mask |= (1u << _playersOnTransferMarket[i]);
    }
    ShowSquad((TeamFile *)&_careerTeam, _aViewSquad, mask, 0, 1, 0xFFFF);
}

/* ThirdPartyOfferViewSquad (Chunk 164) */
__attribute__((used, externally_visible))
void ThirdPartyOfferViewSquad(void)
{
    showSquadWithTransferMarketMask();
}

/* BuyPlayerViewSquad (Chunk 164) */
__attribute__((used, externally_visible))
void BuyPlayerViewSquad(void)
{
    showSquadWithTransferMarketMask();
}

/* OffersInit (Chunk 164) — empty init callback */
__attribute__((used, externally_visible))
void OffersInit(void)
{
    /* Original asm is just rts */
}

/* =============================================================== */
/* Chunk 165: AfterDrawCommon variants + OffersCommon + PlayerOffers*/
/*            callbacks + TransferPlayerInit + ThirdPartyOfferInit  */
/* =============================================================== */

extern uint16_t _offerStatus;
extern uint16_t _timeToNegotiate;
extern uint16_t _lastOfferPrimaryPlayerOrdinal;
extern uint16_t _lastOfferExtraPlayersWanted;
extern uint16_t _lastOfferSwapPlayerOrdinal;
extern uint16_t _lastOfferExtraPlayerOrdinal;
extern uint16_t _offerNegotiationAttemptCount;
extern uint16_t _controlMask;
extern uint8_t _thirdPartyOfferMenu[];
extern uint8_t _aSelectPlayerTo[];
extern uint8_t _aSelectPlayerTo_0[];

extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
extern void CallDrawMenuItem(MenuEntry *entry);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);

/*
 * Shared helper: three AfterDrawCommon variants use the same pattern.
 * baseEntryIdx: the menu entry index for the base (ordinal origin)
 * targetEntryIdx: the menu entry index for the target draw
 * reverseTeam: if 0, d7<2 → offerOrdinals.teamOffering, else selTeamNumber;
 *              if 1, d7<2 → selTeamNumber, else offerOrdinals.teamOffering (ThirdPartyOffer)
 */
static void offerAfterDrawCommonShared(MenuEntry *a5,
                                        uint16_t baseEntryIdx,
                                        uint16_t targetEntryIdx,
                                        uint16_t reverseTeam)
{
    MenuEntry *baseEntry = &_menuEntries[baseEntryIdx];
    int16_t d7 = a5->ordinal - baseEntry->ordinal;

    uint16_t teamId;
    if (d7 < 2) {
        teamId = reverseTeam ? _selTeamNumber : _offerOrdinals.teamOffering;
    } else {
        teamId = reverseTeam ? _offerOrdinals.teamOffering : _selTeamNumber;
    }

    /* Access contiguous offer ordinals as array:
       offerOrdinals.offerPrimaryPlayerOrdinal, offerOrdinals.numExtraPlayersWanted,
       offerOrdinals.offerSwapPlayerOrdinal, offerOrdinals.offerExtraPlayerOrdinal */
    int16_t *ordinals = (int16_t *)&_offerOrdinals.offerPrimaryPlayerOrdinal;
    int16_t playerOrd = ordinals[d7];

    uint8_t *playerData = (uint8_t *)0;  /* NULL = no player */
    if (playerOrd >= 0) {
        playerData = GetPlayer(teamId, playerOrd);
    }

    uint16_t yCoord = a5->y;
    MenuEntry *targetEntry = &_menuEntries[targetEntryIdx];
    BuildTransferPlayerMenuEntry(targetEntry, playerData, yCoord,
                                   (int16_t)0xFFFF, 0xD, (uint8_t *)0);
    DrawMultipleItems(12, targetEntry);
}

/* TransferPlayerAfterDrawCommon: base=13, target=17, offerOrdinals.teamOffering for d7<2 */
__attribute__((used, externally_visible))
void TransferPlayerAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    offerAfterDrawCommonShared(a5, 13, 17, 0);
}

/* ThirdPartyOfferMenuAfterDrawCommon: base=15, target=19, selTeamNumber for d7<2 (reversed) */
__attribute__((used, externally_visible))
void ThirdPartyOfferMenuAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    offerAfterDrawCommonShared(a5, 15, 19, 1);
}

/* PlayerOffersAfterDrawCommon: base=14, target=18, offerOrdinals.teamOffering for d7<2 */
__attribute__((used, externally_visible))
void PlayerOffersAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    offerAfterDrawCommonShared(a5, 14, 18, 0);
}


/* PlayerOffersOnSelect13: just forwards to SetExitMenuFlag */
__attribute__((used, externally_visible))
void PlayerOffersOnSelect13(void)
{
    SetExitMenuFlag();
}

/* OffersCommon (Chunk 165) - main offer negotiation loop */
__attribute__((used, externally_visible))
void OffersCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;
    MenuEntry *baseEntry = &_menuEntries[2];  /* menuEntries + 0x70/0x38 = entry 2 */
    int16_t d7 = a5->ordinal - baseEntry->ordinal;

restart:;
    /* Index into transferOfferTable: each entry is 14 bytes */
    TransferOfferEntry *offerEntry = &((TransferOfferEntry *)_transferOfferTable)[(uint16_t)d7];

    /* Determine offer status from the negotiation-step sentinels */
    uint16_t status = 3;
    if (offerEntry->negotiationStep1Amount == (int16_t)0xFFFE) {
        /* status stays 3 */
    } else {
        status = 2;
        if (offerEntry->negotiationStep2Amount == (int16_t)0xFFFF) {
            /* status stays 2 */
        } else {
            status = 1;
            if (offerEntry->negotiationStep1Amount == (int16_t)0xFFFF) {
                /* status stays 1 */
            } else {
                status = 0;
            }
        }
    }

    _offerStatus = status;
    _offerOrdinals.teamOffering = offerEntry->teamOffering;
    _offerOrdinals.offerAmount = offerEntry->offerAmount;

    /* Sign-extend the ordinal bytes to word */
    _offerOrdinals.offerPrimaryPlayerOrdinal = (uint16_t)(int16_t)offerEntry->primaryPlayerOrdinal;
    _offerOrdinals.numExtraPlayersWanted = (int16_t)offerEntry->secondPlayerOrdinal;
    _offerOrdinals.offerSwapPlayerOrdinal = (int16_t)offerEntry->swapPlayerOrdinal;
    _offerOrdinals.offerExtraPlayerOrdinal = (int16_t)offerEntry->extraPlayerOrdinal;

    ShowMenu(_thirdPartyOfferMenu);

    uint16_t result = _thirdPartyOfferResult;
    if (result == 0) {
        /* Consider: mark this offer as "seen" */
        offerEntry->seenFlag = 1;
        return;
    }
    if (result == 1) {
        /* Accept */
        Career_AcceptOffer_SellPlayer();
        _timeToNegotiate--;
        _transferCompleted = 1;
        SetExitMenuFlag();
        return;
    }
    if (result == 2) {
        /* Reject: remove the offer */
        goto removeOffer;
    }

    /* result == 3: Ask for more */
    /* Check if offer can be improved */
    if (offerEntry->negotiationStep1Amount != 0) {
        if (offerEntry->negotiationStep1Amount == (int16_t)0xFFFF) {
            /* First counter exhausted, try second */
            if (offerEntry->negotiationStep2Amount != 0 &&
                offerEntry->negotiationStep2Amount != (int16_t)0xFFFF) {
                offerEntry->offerAmount = (uint16_t)offerEntry->negotiationStep2Amount;
                offerEntry->negotiationStep2Amount = (int16_t)0xFFFF;
                goto restart;
            }
            /* Fall through to random reject/withdraw */
        } else {
            /* Use first counter amount */
            offerEntry->offerAmount = (uint16_t)offerEntry->negotiationStep1Amount;
            offerEntry->negotiationStep1Amount = (int16_t)0xFFFF;
            goto restart;
        }
    }

    /* Random: 50% chance to refuse further, 50% to withdraw */
    {
        uint16_t r = Rand2();
        if (r & 1) {
            /* Refuse to further improve */
            offerEntry->negotiationStep1Amount = (int16_t)0xFFFE;
            goto restart;
        }
    }

    /* Withdraw offer */
    _offerStatus = 0xFFFF;
    ShowMenu(_thirdPartyOfferMenu);
    /* Fall through to removeOffer */

removeOffer:
    RemoveTransferOfferAtIndex(d7);
    if (_numOffers == 0) {
        SetExitMenuFlag();
        return;
    }
    SetCurrentEntry(1);
    DrawMenu();
}

/* PlayerOffersOnSelect8 (Chunk 165) - select main player to buy */
__attribute__((used, externally_visible))
void PlayerOffersOnSelect8(void)
{
    TeamFileEx *teamPtr = GetTeamPointer(_offerOrdinals.teamOffering);
    uint8_t *title = _aSelectPlayerTo;  /* "SELECT PLAYER TO MAKE OFFER ON" */
    uint32_t maskLo = 0;
    uint32_t maskHi = 0;
    uint16_t d2 = (uint16_t)_offerOrdinals.offerPrimaryPlayerOrdinal;

    /* Set bit for the primary player ordinal */
    maskLo |= (1u << d2);

    if (_offerOrdinals.numExtraPlayersWanted >= 0) {
        /* Extra player slot occupied: clear it */
        _offerOrdinals.numExtraPlayersWanted = -1;
    } else {
        /* Show squad to pick a player */
        int16_t result = ShowSquad((TeamFile *)teamPtr, title, maskLo, maskHi,
                                     0, 0xFFFF);
        if (result >= 0) {
            _offerOrdinals.numExtraPlayersWanted = result;
        }
    }

    UpdatePlayerOfferMenuIndicators();
    DrawMenu();
}

/* PlayerOffersOnSelect10 (Chunk 165) - select swap player */
__attribute__((used, externally_visible))
void PlayerOffersOnSelect10(void)
{
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        /* Already have a swap player: shift extra into swap, clear extra */
        _offerOrdinals.offerSwapPlayerOrdinal = _offerOrdinals.offerExtraPlayerOrdinal;
        _offerOrdinals.offerExtraPlayerOrdinal = -1;
    } else {
        /* Build mask of ineligible players (goalies, injured, on transfer list) */
        uint32_t d7mask = 0;
        /* The mask is indexed by PLAYER FILE INDEX, not by the loop's squad
         * ordinal. `GetPlayerAtIndex` takes an ordinal and REMAPS it through
         * currentMatchPlayers; the asm bit-sets the remapped value it leaves in
         * d1 (`jsr GetPlayerAtIndex / ... / bset d1,d7`, SWOS2.s:100671-100679),
         * and ShowSquad greys by the same file index. Using the ordinal greys
         * whoever happens to OCCUPY that file slot instead — so a real player
         * went grey (the tester's RONALDO, £9M) while a RES/TRIAL stayed
         * selectable. Identical only while currentMatchPlayers is the identity
         * permutation, which it stops being after any squad reorder.
         * Same file-index-vs-display-slot domain error as the national-squad
         * one; see project_national_squad_index_domains. */
        int16_t i = _careerNumPlayers - 1;
        while (i >= 0) {
            uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
            uint16_t fileIdx = _cGetPlayerAtIndexFileIdx;
            PlayerFile *plf = (PlayerFile *)(player + PF_OFF);
            /* Check if player is a trialist (0x9B) or reserve (0x9A) */
            if ((uint8_t)plf->form == PLAYER_FORM_TRIALIST || (uint8_t)plf->form == PLAYER_FORM_RESERVE) {
                d7mask |= (1u << fileIdx);
            } else if (plf->positionAndFace & PF_LOAN) {
                /* on transfer list */
                d7mask |= (1u << fileIdx);
            }
            i--;
        }

        TeamFileEx *teamPtr = GetTeamPointer(_selTeamNumber);
        int16_t result = ShowSquad((TeamFile *)teamPtr, _aSelectPlayerTo_0,
                                     d7mask, 0, 0, 0xFFFF);
        if (result >= 0) {
            _offerOrdinals.offerSwapPlayerOrdinal = result;
        }
    }

    UpdatePlayerOfferMenuIndicators();
    DrawMenu();
}

/* PlayerOffersOnSelect11 (Chunk 165) - select extra swap player */
__attribute__((used, externally_visible))
void PlayerOffersOnSelect11(void)
{
    uint32_t d7mask = 0;
    /* Mark the current swap player as ineligible */
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        d7mask |= (1u << (uint16_t)_offerOrdinals.offerSwapPlayerOrdinal);
    }

    if (_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
        /* Already have an extra player: clear it */
        _offerOrdinals.offerExtraPlayerOrdinal = -1;
    } else {
        /* Build mask of ineligible players */
        /* The mask is indexed by PLAYER FILE INDEX, not by the loop's squad
         * ordinal. `GetPlayerAtIndex` takes an ordinal and REMAPS it through
         * currentMatchPlayers; the asm bit-sets the remapped value it leaves in
         * d1 (`jsr GetPlayerAtIndex / ... / bset d1,d7`, SWOS2.s:100671-100679),
         * and ShowSquad greys by the same file index. Using the ordinal greys
         * whoever happens to OCCUPY that file slot instead — so a real player
         * went grey (the tester's RONALDO, £9M) while a RES/TRIAL stayed
         * selectable. Identical only while currentMatchPlayers is the identity
         * permutation, which it stops being after any squad reorder.
         * Same file-index-vs-display-slot domain error as the national-squad
         * one; see project_national_squad_index_domains. */
        int16_t i = _careerNumPlayers - 1;
        while (i >= 0) {
            uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
            uint16_t fileIdx = _cGetPlayerAtIndexFileIdx;
            PlayerFile *plf = (PlayerFile *)(player + PF_OFF);
            if ((uint8_t)plf->form == PLAYER_FORM_TRIALIST || (uint8_t)plf->form == PLAYER_FORM_RESERVE) {
                d7mask |= (1u << fileIdx);
            } else if (plf->positionAndFace & PF_LOAN) {
                d7mask |= (1u << fileIdx);
            }
            i--;
        }

        TeamFileEx *teamPtr = GetTeamPointer(_selTeamNumber);
        int16_t result = ShowSquad((TeamFile *)teamPtr, _aSelectPlayerTo_0,
                                     d7mask, 0, 0, 0xFFFF);
        if (result >= 0) {
            _offerOrdinals.offerExtraPlayerOrdinal = result;
        }
    }

    UpdatePlayerOfferMenuIndicators();
    DrawMenu();
}

/* PlayerOffersOnSelect12 (Chunk 165) - confirm offer */
__attribute__((used, externally_visible))
void PlayerOffersOnSelect12(void)
{
    /* Check if offer parameters changed from last time */
    if (_offerOrdinals.offerPrimaryPlayerOrdinal != _lastOfferPrimaryPlayerOrdinal ||
        (uint16_t)_offerOrdinals.numExtraPlayersWanted != _lastOfferExtraPlayersWanted ||
        (uint16_t)_offerOrdinals.offerSwapPlayerOrdinal != _lastOfferSwapPlayerOrdinal ||
        (uint16_t)_offerOrdinals.offerExtraPlayerOrdinal != _lastOfferExtraPlayerOrdinal) {

        _offerNegotiationAttemptCount = 0;
        _timeToNegotiate--;
        _lastOfferPrimaryPlayerOrdinal = _offerOrdinals.offerPrimaryPlayerOrdinal;
        _lastOfferExtraPlayersWanted = (uint16_t)_offerOrdinals.numExtraPlayersWanted;
        _lastOfferSwapPlayerOrdinal = (uint16_t)_offerOrdinals.offerSwapPlayerOrdinal;
        _lastOfferExtraPlayerOrdinal = (uint16_t)_offerOrdinals.offerExtraPlayerOrdinal;
    }

    _offerNegotiationAttemptCount++;
    int16_t result = EvaluateTransferOfferNegotiation();
    if (result < 0) {
        /* Rejected: set current entry to 9 */
        SetCurrentEntry(9);
        return;
    }
    if (result > 0) {
        /* Accepted: finalize the purchase */
        Career_AcceptOffer_BuyPlayer();
        _transferCompleted = 1;
    }
    /* result == 0 or > 0: exit menu */
    SetExitMenuFlag();
}

/* PlayerOffersAdjustOfferAmount (Chunk 165) - input handler for offer amount */
__attribute__((used, externally_visible))
void PlayerOffersAdjustOfferAmount(void)
{
    uint16_t amount = _offerOrdinals.offerAmount;
    int32_t bal = _newBalance;
    if (bal < 0)
        bal = 0;
    uint16_t maxAmount = (uint16_t)((uint32_t)bal / 1000u);

    /* Determine direction from controlMask */
    if (_controlMask == 0x81 || _controlMask == 0x101) {
        /* Right/increase */
        uint16_t step = 5;
        if (amount >= 100) step = 10;
        if (amount >= 200) step = 25;
        if (amount >= 300) step = 50;
        if (amount >= 1000) step = 100;
        if (amount >= 2000) step = 250;
        if (amount >= 3000) step = 500;

        amount += step;
        if (amount > 50000)
            amount = 50000;
        if (amount > maxAmount)
            amount = maxAmount;
    } else if (_controlMask == 0x41 || _controlMask == 0x201) {
        /* Left/decrease */
        uint16_t step = 5;
        if (amount > 100) step = 10;
        if (amount > 200) step = 25;
        if (amount > 300) step = 50;
        if (amount > 1000) step = 100;
        if (amount > 2000) step = 250;
        if (amount > 3000) step = 500;

        /* Special case: if amount == maxAmount, snap to grid */
        if (amount == maxAmount) {
            uint32_t snapped = ((uint32_t)maxAmount / step) * step;
            if ((uint16_t)snapped != amount) {
                amount = (uint16_t)snapped;
                goto done;
            }
        }

        if (amount >= step)
            amount -= step;
        else
            amount = 0;
    } else {
        return;
    }

done:
    _offerOrdinals.offerAmount = amount;
    PlayerOffersUpdateOfferAmountText();
    CallDrawMenuItem(&_menuEntries[9]);
}

/* =============================================================== */
/* Chunk 166: TransferPlayerInit + ThirdPartyOfferMenuInit          */
/* =============================================================== */

extern int16_t  _teamWillingToSellOrExchange;
extern uint8_t  _managerSexBuffer[];
extern uint8_t  _managerSurnameBuffer[];
extern void *_sellExchangeStrings[];
extern char    *_sellExchangeResponseStrings[];  /* native-width pointer table; storage is char *[12] in swos_data_typed_stubs.c */
extern uint8_t  _aDearAB[];
extern uint8_t  _aInADealWithAFo[];
extern uint8_t  _aUnlessTheOffer[];
extern uint8_t  _aWeHereAtAWould[];
extern uint8_t  _fundsBuffer[];
extern uint8_t  _aWeHereAtAWishT[];
extern uint8_t  _aWeHereAtAWishT_0[];
extern uint8_t  _aOurOfferToBuy[];
extern uint8_t  _aWeHereAtAWishT_1[];
extern uint8_t  _aImproveOurOffe[];
extern uint8_t  _aWeHereAtARefus[];
extern uint8_t  _aImproveOnOurOf[];
extern uint8_t  _aWeHereAtAWithd[];
extern uint8_t  _aOfferOn[];
extern uint8_t  _aFromAInExchang[];

extern void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                            uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                            uint16_t n4, uint16_t n5, uint16_t n6,
                            const uint8_t *strA, const uint8_t *strB, const uint8_t *strC);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern void Int2Pounds(uint32_t pounds, uint8_t *dst);
extern uint16_t GetStringLength(const uint8_t *str);

/* TransferPlayerInit (Chunk 166) — init for playerTransferMenu */
__attribute__((used, externally_visible))
void TransferPlayerInit(void *entryPtr)
{
    (void)entryPtr;

    /* Handle menu entry visibility based on teamWillingToSellOrExchange */
    if (_teamWillingToSellOrExchange >= 1 && _teamWillingToSellOrExchange <= 4) {
        /* Willing to sell/exchange: hide entry 10, shift entry 11 left */
        _menuEntries[10].isInvisible = 1;
        _menuEntries[11].x -= 0x3C;
    } else if (_teamWillingToSellOrExchange == 0) {
        /* Not willing at all: hide 10+11, show 12, select entry 12 */
        _menuEntries[10].isInvisible = 1;
        _menuEntries[11].isInvisible = 1;
        _menuEntries[12].isInvisible = 0;
        SetCurrentEntry(12);
    }

    /* Format "DEAR %a %b" into entry 1 */
    PrintFormatted(_aDearAB, (uint8_t *)_menuEntries[1].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerSexBuffer, _managerSurnameBuffer, 0);

    /* Select appropriate sell/exchange string from table. The table is
     * `void *[4]` (native pointer width), so walk as `void **` — was
     * `uint32_t *` which read half-pointers on 64-bit and warned at the
     * (uint8_t *)<u32> cast below. */
    void **strPair = _sellExchangeStrings;
    if (_teamWillingToSellOrExchange != 0)
        strPair += 2;
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0)
        strPair += 1;

    /* Get offering team name and format into entry 2 */
    TeamFileEx *teamPtr = GetTeamPointer(_offerOrdinals.teamOffering);
    uint8_t *teamName = (uint8_t *)teamPtr->team.teamName;
    PrintFormatted((const uint8_t *)*strPair,
                    (uint8_t *)_menuEntries[2].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    teamName, 0, 0);

    /* Get our team name and format "IN A DEAL WITH %a FOR" into entry 3 */
    teamPtr = GetTeamPointer(_selTeamNumber);
    teamName = (uint8_t *)teamPtr->team.teamName;
    PrintFormatted(_aInADealWithAFo, (uint8_t *)_menuEntries[3].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    teamName, 0, 0);

    int16_t twse = _teamWillingToSellOrExchange;
    if (twse < 0) {
        /* Negative: team wants more money. Amount = -twse * 1000 */
        uint16_t amountNeeded = (uint16_t)(-twse);
        Int2Pounds((uint32_t)amountNeeded * 1000, _fundsBuffer);

        /* Show entry 4: "UNLESS THE OFFER IS RAISED BY %a" */
        _menuEntries[4].isInvisible = 0;
        PrintFormatted(_aUnlessTheOffer, (uint8_t *)_menuEntries[4].fg,
                        0, 0, 0, 0, 0, 0, 0,
                        _fundsBuffer, 0, 0);

        /* Show entry 5: "WE HERE AT %a WOULD LIKE TO OFFER..." */
        _menuEntries[5].isInvisible = 0;
        PrintFormatted(_aWeHereAtAWould, (uint8_t *)_menuEntries[5].fg,
                        0, 0, 0, 0, 0, 0, 0,
                        _fundsBuffer, 0, 0);
    } else {
        /* Non-negative: use response string lookup table.
         * Walk as `char **` (native width); was `uint32_t *` which
         * read half-pointers on 64-bit. */
        char **respPair = _sellExchangeResponseStrings + (twse * 2);
        if (respPair[0] != 0) {
            _menuEntries[4].isInvisible = 0;
            StringCopy((const uint8_t *)respPair[0],
                        (uint8_t *)_menuEntries[4].fg);
        }
        if (respPair[1] != 0) {
            _menuEntries[5].isInvisible = 0;
            StringCopy((const uint8_t *)respPair[1],
                        (uint8_t *)_menuEntries[5].fg);
        }
    }

    /* Format offer amount into entry 9's fg string buffer */
    Int2Pounds((uint32_t)_offerOrdinals.offerAmount * 1000,
                (uint8_t *)_menuEntries[9].fg);

    /* Entry 6 (plus marker): visible only if offerOrdinals.numExtraPlayersWanted >= 0 */
    _menuEntries[6].isInvisible = 1;
    if (_offerOrdinals.numExtraPlayersWanted >= 0)
        _menuEntries[6].isInvisible = 0;

    /* Entry 7 (swap player plus): visible only if offerOrdinals.offerSwapPlayerOrdinal >= 0 */
    _menuEntries[7].isInvisible = 1;
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0)
        _menuEntries[7].isInvisible = 0;

    /* Entry 8 (extra player plus): visible only if offerOrdinals.offerExtraPlayerOrdinal >= 0 */
    _menuEntries[8].isInvisible = 1;
    if (_offerOrdinals.offerExtraPlayerOrdinal >= 0)
        _menuEntries[8].isInvisible = 0;
}

/* ThirdPartyOfferMenuInit (Chunk 166) — init for thirdPartyOfferMenu */
__attribute__((used, externally_visible))
void ThirdPartyOfferMenuInit(void *entryPtr)
{
    (void)entryPtr;

    /* Format offer amount into entry 8's fg string buffer */
    Int2Pounds((uint32_t)_offerOrdinals.offerAmount * 1000,
                (uint8_t *)_menuEntries[8].fg);

    /* "DEAR %a %b" into entry 1 */
    PrintFormatted(_aDearAB, (uint8_t *)_menuEntries[1].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    _managerSexBuffer, _managerSurnameBuffer, 0);

    /* Get offering team name */
    TeamFileEx *teamPtr = GetTeamPointer(_offerOrdinals.teamOffering);
    uint8_t *teamName = (uint8_t *)teamPtr->team.teamName;

    if (_offerStatus == 0) {
        /* "WE HERE AT %a WISH TO BUY" into entry 2 */
        PrintFormatted(_aWeHereAtAWishT, (uint8_t *)_menuEntries[2].fg,
                        0, 0, 0, 0, 0, 0, 0,
                        teamName, 0, 0);
    } else {
        /* Improved/further/refused/withdrawn offer — entry 2 adjusted */
        _menuEntries[2].y -= 8;

        const uint8_t *fmtLine1;
        const uint8_t *fmtLine2;
        if (_offerStatus == 1) {
            fmtLine1 = _aWeHereAtAWishT_0;  /* "WE HERE AT %a WISH TO IMPROVE" */
            fmtLine2 = _aOurOfferToBuy;      /* "OUR OFFER TO BUY" */
        } else if (_offerStatus == 2) {
            fmtLine1 = _aWeHereAtAWishT_1;  /* "WE HERE AT %a WISH TO FURTHER" */
            fmtLine2 = _aImproveOurOffe;    /* "IMPROVE OUR OFFER TO BUY" */
        } else if (_offerStatus == 3) {
            fmtLine1 = _aWeHereAtARefus;    /* "WE HERE AT %a REFUSE TO FURTHER" */
            fmtLine2 = _aImproveOnOurOf;    /* "IMPROVE ON OUR OFFER TO BUY" */
        } else {
            fmtLine1 = _aWeHereAtAWithd;    /* "WE HERE AT %a WITHDRAW OUR" */
            fmtLine2 = _aOfferOn;           /* "OFFER ON" */
        }

        PrintFormatted(fmtLine1, (uint8_t *)_menuEntries[2].fg,
                        0, 0, 0, 0, 0, 0, 0,
                        teamName, 0, 0);

        /* Show entry 3 with line 2 text */
        _menuEntries[3].isInvisible = 0;
        PrintFormatted(fmtLine2, (uint8_t *)_menuEntries[3].fg,
                        0, 0, 0, 0, 0, 0, 0,
                        teamName, 0, 0);

        /* Shift entry 1 up by 8 */
        _menuEntries[1].y -= 8;
    }

    /* "FROM %a IN EXCHANGE FOR" into entry 4 */
    teamPtr = GetTeamPointer(_selTeamNumber);
    teamName = (uint8_t *)teamPtr->team.teamName;
    PrintFormatted(_aFromAInExchang, (uint8_t *)_menuEntries[4].fg,
                    0, 0, 0, 0, 0, 0, 0,
                    teamName, 0, 0);

    /* Entry 5: visible only if offerOrdinals.numExtraPlayersWanted >= 0 */
    _menuEntries[5].isInvisible = 1;
    if (_offerOrdinals.numExtraPlayersWanted >= 0)
        _menuEntries[5].isInvisible = 0;

    /* Entry 6: visible only if offerOrdinals.offerSwapPlayerOrdinal >= 0 */
    _menuEntries[6].isInvisible = 1;
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0)
        _menuEntries[6].isInvisible = 0;

    /* Entry 7: visible only if offerOrdinals.offerExtraPlayerOrdinal >= 0 */
    _menuEntries[7].isInvisible = 1;
    if (_offerOrdinals.offerExtraPlayerOrdinal >= 0)
        _menuEntries[7].isInvisible = 0;

    /* Handle offerStatus < 0 (negative): withdrawn offer — hide most entries.
     *
     * The Amiga asm (SWOS2.s:99942+, the offerStatus-negative branch)
     * also hides entries 10, 11, 12 — ACCEPT OFFER, ASK FOR MORE,
     * REJECT OFFER. Without that, the user can still see and click
     * those three action buttons even though the offer has been
     * withdrawn, instead of being left with only EXIT as the Amiga
     * intended. */
    if ((int16_t)_offerStatus < 0) {
        _menuEntries[4].isInvisible = 1;
        _menuEntries[6].isInvisible = 1;
        _menuEntries[7].isInvisible = 1;
        _menuEntries[8].isInvisible = 1;
        _menuEntries[9].isInvisible = 1;
        _menuEntries[10].isInvisible = 1; /* ACCEPT OFFER */
        _menuEntries[11].isInvisible = 1; /* ASK FOR MORE */
        _menuEntries[12].isInvisible = 1; /* REJECT OFFER */
        _menuEntries[13].isInvisible = 1;
        _menuEntries[17].isInvisible = 1;
        _menuEntries[18].isInvisible = 1;
        _menuEntries[14].isInvisible = 0; /* EXIT */
        SetCurrentEntry(14);
    }

    /* Handle offerStatus == 3: refuse to improve further — hide askForMore, widen accept/reject */
    if (_offerStatus == 3) {
        _menuEntries[11].isInvisible = 1;
        _menuEntries[10].dwWidth += 0x32;
        _menuEntries[12].x -= 0x33;
        _menuEntries[12].dwWidth += 0x33;
    }
}

/* ================================================================
 * Chunk 168 — OffersAfterDrawCommon
 * ================================================================ */

/* Format string pointer globals (BSS in asm — 12 pointers) */
extern uintptr_t _offerFmtSinglePlayer;
extern uintptr_t _offerFmtTwoPlayers;
extern uintptr_t _exchangeFmtSinglePlayer;
extern uintptr_t _exchangeFmtTwoPlayers;
extern uintptr_t _offerFmtLine1a;
extern uintptr_t _offerFmtLine2a;
extern uintptr_t _offerFmtLine1b;
extern uintptr_t _offerFmtLine2b;
extern uintptr_t _exchangeFmtLine1a;
extern uintptr_t _exchangeFmtLine2a;
extern uintptr_t _exchangeFmtLine1b;
extern uintptr_t _exchangeFmtLine2b;

/* Format string labels */
extern uint8_t _aAOfferForB[];
extern uint8_t _aAOfferForBAndC[];
extern uint8_t _aAExchangeOffer[];
extern uint8_t _aAExchangeOffer_0[];
extern uint8_t _aAOfferFor[];
extern uint8_t _aB[];
extern uint8_t _aAOfferFor_0[];
extern uint8_t _aBAndC[];
extern uint8_t _aAExchangeOffer_1[];
extern uint8_t _aB_0[];
extern uint8_t _aAExchangeOffer_2[];
extern uint8_t _aBAndC_0[];
extern uint8_t _aAFurtherImprov[];
extern uint8_t _aAFurtherImprov_0[];
extern uint8_t _aAFurtherImprov_1[];
extern uint8_t _aAFurtherImprov_2[];
extern uint8_t _aAFurtherImprov_3[];
extern uint8_t _aB_3[];
extern uint8_t _aAFurtherImprov_4[];
extern uint8_t _aBAndC_3[];
extern uint8_t _aAFurtherImprov_5[];
extern uint8_t _aOfferForB[];
extern uint8_t _aAFurtherImprov_6[];
extern uint8_t _aBAndC_4[];
extern uint8_t _aAImprovedOffer[];
extern uint8_t _aAImprovedOffer_0[];
extern uint8_t _aAImprovedExcha[];
extern uint8_t _aAImprovedExcha_0[];
extern uint8_t _aAImprovedOffer_1[];
extern uint8_t _aB_1[];
extern uint8_t _aAImprovedOffer_2[];
extern uint8_t _aBAndC_1[];
extern uint8_t _aAImprovedExcha_1[];
extern uint8_t _aB_2[];
extern uint8_t _aAImprovedExcha_2[];
extern uint8_t _aBAndC_2[];

extern uint8_t _fundsBuffer[];
extern uint16_t GetStringLength(const uint8_t *str);

/* ================================================================
 * OffersAfterDrawCommon  (SWOS2.s line 64153)
 *
 * After-draw callback for the offers view menu. Lays out offer
 * text entries dynamically, selects format strings based on
 * offer type (normal/improved/further improved, single/two players,
 * offer/exchange), formats player names, and handles text wrapping.
 *
 * in: a5 = current menu entry
 * ================================================================ */
__attribute__((used, externally_visible))
void OffersAfterDrawCommon(void *entryPtr)
{
    MenuEntry *a5 = (MenuEntry *)entryPtr;

    /* Calculate offer index: d7 = a5->ordinal - entries[2].ordinal */
    int16_t d7 = (int16_t)a5->ordinal - (int16_t)_menuEntries[2].ordinal;

    /* Set y-positions and make entries 7-10 invisible (initially) */
    _menuEntries[7].y = a5->y;
    _menuEntries[7].isInvisible = 1;

    _menuEntries[8].y = a5->y + 2;
    _menuEntries[8].isInvisible = 1;

    _menuEntries[9].y = a5->y + 11;
    _menuEntries[9].isInvisible = 1;

    _menuEntries[10].y = a5->y + 7;
    _menuEntries[10].isInvisible = 1;

    /* ENTRY_NUMBER: store offer index+1 in entry 10's fg-word. */
    _menuEntries[10].fgWord = (uint16_t)(d7 + 1);

    /* Mark current entry as disabled */
    a5->isDisabled = 1;

    /* If d7 >= numOffers, skip offer rendering */
    if ((uint16_t)d7 >= _numOffers)
        goto drawAndReturn;

    /* Point to the offer in transferOfferTable */
    TransferOfferEntry *offer = &((TransferOfferEntry *)_transferOfferTable)[(uint16_t)d7];

    a5->isDisabled = 0;
    _menuEntries[7].isInvisible = 0;
    _menuEntries[8].isInvisible = 0;
    _menuEntries[9].isInvisible = 0;
    _menuEntries[10].isInvisible = 0;

    /* Set color based on offer seen flag (byte 0x0D) */
    uint16_t colorBits = 0;
    if (offer->seenFlag != 0)
        colorBits = 6;

    /* Apply color to stringFlags of entries 7-10 (low 4 bits) */
    for (int i = 0; i < 4; i++) {
        MenuEntry *e = &_menuEntries[7 + i];
        uint16_t sf = e->stringFlags;
        sf = (sf & 0xFFF0) | colorBits;
        e->stringFlags = sf;
    }

    /* Reset entry 7's y to match a5's y */
    _menuEntries[7].y = a5->y;

    /* Set up format string pointers: default (normal offer) */
    _offerFmtSinglePlayer   = (uintptr_t)_aAOfferForB;
    _offerFmtTwoPlayers     = (uintptr_t)_aAOfferForBAndC;
    _exchangeFmtSinglePlayer = (uintptr_t)_aAExchangeOffer;
    _exchangeFmtTwoPlayers   = (uintptr_t)_aAExchangeOffer_0;
    _offerFmtLine1a          = (uintptr_t)_aAOfferFor;
    _offerFmtLine2a          = (uintptr_t)_aB;
    _offerFmtLine1b          = (uintptr_t)_aAOfferFor_0;
    _offerFmtLine2b          = (uintptr_t)_aBAndC;
    _exchangeFmtLine1a       = (uintptr_t)_aAExchangeOffer_1;
    _exchangeFmtLine2a       = (uintptr_t)_aB_0;
    _exchangeFmtLine1b       = (uintptr_t)_aAExchangeOffer_2;
    _exchangeFmtLine2b       = (uintptr_t)_aBAndC_0;

    /* Check for "further improved" (negotiationStep2Amount == 0xFFFF) */
    if (offer->negotiationStep2Amount == (int16_t)0xFFFF) {
        _offerFmtSinglePlayer   = (uintptr_t)_aAFurtherImprov;
        _offerFmtTwoPlayers     = (uintptr_t)_aAFurtherImprov_0;
        _exchangeFmtSinglePlayer = (uintptr_t)_aAFurtherImprov_1;
        _exchangeFmtTwoPlayers   = (uintptr_t)_aAFurtherImprov_2;
        _offerFmtLine1a          = (uintptr_t)_aAFurtherImprov_3;
        _offerFmtLine2a          = (uintptr_t)_aB_3;
        _offerFmtLine1b          = (uintptr_t)_aAFurtherImprov_4;
        _offerFmtLine2b          = (uintptr_t)_aBAndC_3;
        _exchangeFmtLine1a       = (uintptr_t)_aAFurtherImprov_5;
        _exchangeFmtLine2a       = (uintptr_t)_aOfferForB;
        _exchangeFmtLine1b       = (uintptr_t)_aAFurtherImprov_6;
        _exchangeFmtLine2b       = (uintptr_t)_aBAndC_4;
    }
    /* Check for "improved" (negotiationStep1Amount == 0xFFFF) */
    else if (offer->negotiationStep1Amount == (int16_t)0xFFFF) {
        _offerFmtSinglePlayer   = (uintptr_t)_aAImprovedOffer;
        _offerFmtTwoPlayers     = (uintptr_t)_aAImprovedOffer_0;
        _exchangeFmtSinglePlayer = (uintptr_t)_aAImprovedExcha;
        _exchangeFmtTwoPlayers   = (uintptr_t)_aAImprovedExcha_0;
        _offerFmtLine1a          = (uintptr_t)_aAImprovedOffer_1;
        _offerFmtLine2a          = (uintptr_t)_aB_1;
        _offerFmtLine1b          = (uintptr_t)_aAImprovedOffer_2;
        _offerFmtLine2b          = (uintptr_t)_aBAndC_1;
        _exchangeFmtLine1a       = (uintptr_t)_aAImprovedExcha_1;
        _exchangeFmtLine2a       = (uintptr_t)_aB_2;
        _exchangeFmtLine1b       = (uintptr_t)_aAImprovedExcha_2;
        _exchangeFmtLine2b       = (uintptr_t)_aBAndC_2;
    }

    /* Get team name pointer (offset 5 from team data = teamName field) */
    TeamFileEx *teamPtr = GetTeamPointer(offer->teamOffering);
    uint8_t *teamName = (uint8_t *)teamPtr->team.teamName;

    /* Determine format string and player names based on offer type */
    uint8_t *fmtStr;
    uint8_t *playerNameA;
    uint8_t *playerNameB = 0;

    if (offer->secondPlayerOrdinal < 0) {
        /* Single player */
        fmtStr = (uint8_t *)_offerFmtSinglePlayer;
        if (offer->swapPlayerOrdinal >= 0)
            fmtStr = (uint8_t *)_exchangeFmtSinglePlayer;

        uint8_t *p = GetPlayer(_selTeamNumber, (uint16_t)offer->primaryPlayerOrdinal);
        PlayerFile *namePf = (PlayerFile *)(p + PF_OFF);
        playerNameA = (uint8_t *)namePf->playerName;
    } else {
        /* Two players */
        fmtStr = (uint8_t *)_offerFmtTwoPlayers;
        if (offer->swapPlayerOrdinal >= 0)
            fmtStr = (uint8_t *)_exchangeFmtTwoPlayers;

        uint8_t *p1 = GetPlayer(_selTeamNumber, (uint16_t)offer->primaryPlayerOrdinal);
        PlayerFile *namePf1 = (PlayerFile *)(p1 + PF_OFF);
        playerNameA = (uint8_t *)namePf1->playerName;

        uint8_t *p2 = GetPlayer(_selTeamNumber, (uint16_t)offer->secondPlayerOrdinal);
        PlayerFile *namePf2 = (PlayerFile *)(p2 + PF_OFF);
        playerNameB = (uint8_t *)namePf2->playerName;
    }

    /* Format the offer text into fundsBuffer using PrintFormatted
     * a0=fmt, a1=dest(fundsBuffer), a2=teamName, a3=playerNameA, a4=playerNameB */
    PrintFormatted(fmtStr, _fundsBuffer, 0, 0, 0, 0, 0, 0, 0,
                    teamName, playerNameA, playerNameB);

    /* Check if formatted text fits on one line (<=47 chars) */
    uint16_t len = GetStringLength(_fundsBuffer);
    if (len <= 47) {
        /* Single line: copy to entry 8's fg string, adjust y */
        _menuEntries[8].y += 5;
        uint8_t *dst8 = (uint8_t *)_menuEntries[8].fg;
        StringCopy(_fundsBuffer, dst8);

        /* Clear entry 9's string */
        uint8_t *dst9 = (uint8_t *)_menuEntries[9].fg;
        *dst9 = 0;
    } else {
        /* Two lines: determine which format variant was used */
        uint8_t *line1Fmt;
        uint8_t *line2Fmt;
        uint8_t *dst8str = (uint8_t *)_menuEntries[8].fg;
        uint8_t *dst9str = (uint8_t *)_menuEntries[9].fg;

        if (fmtStr == (uint8_t *)_offerFmtTwoPlayers) {
            line1Fmt = (uint8_t *)_offerFmtLine1b;
            line2Fmt = (uint8_t *)_offerFmtLine2b;
        } else if (fmtStr == (uint8_t *)_exchangeFmtSinglePlayer) {
            line1Fmt = (uint8_t *)_exchangeFmtLine1a;
            line2Fmt = (uint8_t *)_exchangeFmtLine2a;
        } else if (fmtStr == (uint8_t *)_exchangeFmtTwoPlayers) {
            line1Fmt = (uint8_t *)_exchangeFmtLine1b;
            line2Fmt = (uint8_t *)_exchangeFmtLine2b;
        } else {
            /* Default: single-player offer */
            line1Fmt = (uint8_t *)_offerFmtLine1a;
            line2Fmt = (uint8_t *)_offerFmtLine2a;
        }

        /* Format line 1 into entry 8's string */
        PrintFormatted(line1Fmt, dst8str, 0, 0, 0, 0, 0, 0, 0,
                        teamName, playerNameA, playerNameB);

        /* Format line 2 into entry 9's string */
        PrintFormatted(line2Fmt, dst9str, 0, 0, 0, 0, 0, 0, 0,
                        teamName, playerNameA, playerNameB);
    }

drawAndReturn:
    /* Draw entries 7-10 (4 entries starting from entry 7) */
    DrawMultipleItems(4, &_menuEntries[7]);
}

/* ================================================================
 * Chunk 167 — Career Transfer Accept + Trialist System
 * 12 functions from SWOS2.s lines 63218-64296.
 * ================================================================ */

/* ---- Additional externs for Chunk 167 ---- */
extern uint8_t  _tempTeamBuffer[];          /* 0x2AC-byte team buffer */
extern uint8_t *_transferReplacementPriorityTable[]; /* 8 pointers to priority lists */
extern volatile uint16_t _currentMessage;
extern uint16_t _trainingGame;
extern uint16_t _careerFileBuffer;          /* count of teams in TMD pool */
extern uint8_t  _tmdFileBuffer;             /* start of TMD team pool */
extern uint8_t  _trialistNameBuffer[];      /* 23-byte trialist name buffer */
/* trialistNameBufTerminator was a SWOS2.s 1-byte BSS label sitting at
 * `trialistNameBuffer + 22`. Reference the buffer end directly instead
 * of relying on adjacency — see swos_data_typed_stubs.c comment. */
#define TRIALIST_NAME_BUF_END (&_trialistNameBuffer[22])
extern uint8_t  _trialistSkillDistribution[];   /* 20 bytes cumulative prob weights */
extern uint8_t  _trialistSkillTemplates[];  /* 512 bytes packed skill nibbles */
extern uint8_t  _trialistSkillBitMasks[];   /* 32 bytes skill bit masks per position */
extern uint8_t  _currentMatchPlayers[];     /* player ordinal mapping */
extern uint16_t _panicReasonCode;

extern void ClampNewBalanceAndUpdateClubStatus(void);
extern void ClearOverdraftStateIfRecovered(void);
extern TeamFileEx *GetTeamPointer(uint16_t teamNumber);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern void Career_RemovePlayerFromTeamInternal(uint16_t teamNumber, uint16_t playerOrd);
extern void AddCareerPoolPlayer(uint16_t teamNumber, uint16_t playerOrdinal, PlayerFile *playerFile);
extern void Career_RemovePlayerFromSelectedTeamRoster(uint16_t playerIndex);
extern void SetBenchPlayersNumbers(TeamFileEx *team);
extern int32_t SquadNumbers_On(void);           /* (real squad numbers: teams.c) */
extern void SquadNumbers_Free(uint16_t slot);
extern void TrainingMatch_SaveSelectedPlayerId(uint16_t value);
extern void Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
extern int16_t SearchTeamInTmdFile(uint16_t teamNumber);
extern void Career_RemoveTeamFromTmdTeamPool(uint16_t teamNumber);
extern void Career_AppendTeamToTmdTeamPool(TeamFileEx *teamData);
extern uint16_t GetAveragePlayerPrice(TeamFileEx *team);
extern void Career_CommitNewBalanceAndResetFinanceTotals(void);
extern uint32_t GetNumberOfAvailableSubstitutes(TeamFileEx *team, int16_t flag);
extern uint32_t GetNumberOfAvailableStartingPlayers(TeamFileEx *team, int16_t flag);
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern void PanicHaltLoop(void);

/* ================================================================
 * Career_BuildForeignMarketPoolFromSelectedTeam  (SWOS2.s line 63218)
 *
 * Copies careerTeam to tempTeamBuffer, then rearranges players
 * according to selTeamPlayerIndices, copies back, updates
 * currentMatchPlayers and careerNumPlayers, clears player flags,
 * adds all 16 players to career pool, and optionally updates TMD.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void Career_BuildForeignMarketPoolFromSelectedTeam(void)
{
    uint8_t *src, *dst;
    int16_t i, j;

    /* Copy careerTeam (0x2AC bytes) to tempTeamBuffer */
    src = (uint8_t *)&_careerTeam;
    dst = _tempTeamBuffer;
    for (i = 0x2AB; i >= 0; i--)
        *dst++ = *src++;

    /* Rearrange players: for each ordinal 0-15, get the player data from
       selTeamPlayerIndices[ordinal] and copy 0x26 bytes of player data */
    uint8_t *indices = _selTeamPlayerIndices;
    for (i = 0; i < 16; i++) {
        uint8_t *playerSrc = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
        playerSrc += PF_OFF;  /* point to actual PlayerFile data */

        uint8_t idx = *indices++;
        uint16_t off = (uint16_t)((int8_t)idx) * 0x26;  /* ext.w d0; mulu.w #$26,d0 */
        uint8_t *playerDst = _tempTeamBuffer + PF_OFF + off;

        for (j = 0x25; j >= 0; j--)
            *playerDst++ = *playerSrc++;
    }

    /* Copy tempTeamBuffer back to careerTeam */
    src = _tempTeamBuffer;
    dst = (uint8_t *)&_careerTeam;
    for (i = 0x2AB; i >= 0; i--)
        *dst++ = *src++;

    /* Copy selTeamPlayerIndices to currentMatchPlayers */
    src = _selTeamPlayerIndices;
    dst = _currentMatchPlayers;
    for (i = 0xF; i >= 0; i--)
        *dst++ = *src++;

    _careerNumPlayers = 16;

    /* Clear flags on all 16 players and add to career pool */
    for (i = 15; i >= 0; i--) {
        uint8_t *p = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
        PlayerFile *pf = (PlayerFile *)(p + PF_OFF);
        pf->positionAndFace &= ~PF_LOAN;  /* bclr #1 */
        pf->cardsInjuries = 0;
        pf->positionAndFace &= ~PF_INFORM;  /* bclr #0 */
        pf->form = 0;
        AddCareerPoolPlayer(_selTeamNumber, (uint16_t)i, (PlayerFile *)p);
    }

    /* If team found in TMD file, remove and re-add with updated data */
    if (SearchTeamInTmdFile(_selTeamNumber) == 0) {
        Career_RemoveTeamFromTmdTeamPool(_selTeamNumber);
        Career_AppendTeamToTmdTeamPool(&_careerTeam);
    }
}

/* ================================================================
 * Career_TransferPlayerAndAddReplacedToMarketPool  (SWOS2.s line 63534)
 *
 * Removes player d1 from selTeam. Finds lowest-price player of same
 * position group from team d0 to replace, copies player data over,
 * clears stats, and adds replaced player to career pool.
 *
 * in:  d0 = source team number, d1 = player ordinal to transfer
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void Career_TransferPlayerAndAddReplacedToMarketPool(uint16_t sourceTeam, uint16_t playerOrd)
{
    uint16_t savedOrd = playerOrd;
    uint16_t d6 = sourceTeam;

    /* Remove player from selected team's internal roster */
    Career_RemovePlayerFromTeamInternal(_selTeamNumber, playerOrd);

    /* Get the player being transferred (from selected team) */
    uint8_t *transferredBase = GetPlayer(_selTeamNumber, playerOrd);
    PlayerFile *transferredPf = (PlayerFile *)(transferredBase + PF_OFF);

    /* Get position group (bits 7-5 of positionAndFace) */
    uint16_t posGroup = ((uint16_t)transferredPf->positionAndFace & PF_POSITION_MASK) >> 3;
    /* posGroup is now index*4 into the priority table (lsr.w #3) */

    uint8_t *priorityList = (uint8_t *)_transferReplacementPriorityTable[posGroup >> 2];

    /* Get source team pointer */
    TeamFileEx *teamPtr = GetTeamPointer(d6);

    while (1) {
        uint8_t targetRole = *priorityList++;
        if ((int8_t)targetRole < 0) {
            /* No replacement found — panic */
            _panicReasonCode = 0x7272;
            PanicHaltLoop();
            return;
        }

        /* Search team for lowest-priced player of this role */
        uint8_t *bestPlayer = 0;
        uint16_t bestOrdinal = 0;
        uint8_t bestPrice = 0xFF;

        uint8_t *scanPtr = (uint8_t *)teamPtr;
        for (uint16_t slot = 0; slot < 16; slot++) {
            PlayerFile *pf = (PlayerFile *)(scanPtr + PF_OFF);
            uint16_t role = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> 5;
            if (role == targetRole) {
                /* asm: `cmp.b $6C(a0),d3 / bcs.s skip` — that computes
                 * bestPrice - price and skips only when bestPrice < price, so
                 * the candidate is TAKEN on a tie and the LAST equal-priced
                 * player in the team wins. `<` here kept the FIRST instead,
                 * which picks a different player to give away whenever two
                 * squad members of the same role are valued the same — common,
                 * since price is a byte. (SWOS2.s:96838.) */
                if (pf->price <= bestPrice) {
                    bestOrdinal = slot;
                    bestPrice = pf->price;
                    bestPlayer = scanPtr;
                }
            }
            scanPtr += 0x26;  /* advance by PlayerFile size */
        }

        if (bestPrice == 0xFF)
            continue;  /* no match for this role, try next priority */

        /* Found a replacement — copy player data (0x26 bytes from offset 0x4C) */
        uint8_t *srcData = transferredBase + PF_OFF;
        uint8_t *dstData = bestPlayer + PF_OFF;

        /* Save shirt numbers before copy */
        uint8_t srcShirt = (uint8_t)transferredPf->shirtNumber;
        PlayerFile *bestReplacePf = (PlayerFile *)(bestPlayer + PF_OFF);
        uint8_t dstShirt = (uint8_t)bestReplacePf->shirtNumber;

        /* Copy 0x26 bytes (19 words) of player data */
        uint8_t *s = srcData;
        uint8_t *d = dstData;
        for (int16_t k = 0x12; k >= 0; k--) {
            *(uint16_t *)d = *(uint16_t *)s;
            s += 2;
            d += 2;
        }

        /* Restore shirt numbers */
        transferredPf->shirtNumber = (int8_t)srcShirt;
        bestReplacePf->shirtNumber = (int8_t)dstShirt;

        /* Mark replacement as trialist and clear flags */
        PlayerFile *bestPf = (PlayerFile *)(bestPlayer + PF_OFF);
        bestPf->positionAndFace |= PF_TRIALIST;   /* bset #2 */
        bestPf->positionAndFace &= ~PF_INFORM;  /* bclr #0 */
        bestPf->form = 0;
        bestPf->goalStats[0] = 0;
        bestPf->goalStats[1] = 0;
        bestPf->goalStats[2] = 0;
        bestPf->goalStats[3] = 0;

        /* Remove from source team and add to career pool */
        Career_RemovePlayerFromTeamInternal(d6, bestOrdinal);
        AddCareerPoolPlayer(d6, bestOrdinal, (PlayerFile *)bestPlayer);
        _offerSwapSlotsUsed++;

        /* Remove from selected team roster */
        Career_RemovePlayerFromSelectedTeamRoster(savedOrd);
        return;
    }
}

/* ================================================================
 * Career_AcceptOffer_SellPlayer  (SWOS2.s line 63408)
 *
 * Handles accepting a sell offer: adds sale amount to balance,
 * processes swap/extra players, flags teams as changed.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_AcceptOffer_SellPlayer(void)
{
    Career_CommitNewBalanceAndResetFinanceTotals();

    uint32_t saleAmount = (uint32_t)_offerOrdinals.offerAmount * 1000;
    _newBalance += (int32_t)saleAmount;
    _playerSales += (int32_t)saleAmount;

    ClampNewBalanceAndUpdateClubStatus();
    ClearOverdraftStateIfRecovered();

    if ((int16_t)_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        /* Swap player: remove from offering team as trialist, transfer primary */
        Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.offerSwapPlayerOrdinal);
        Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    } else {
        /* No swap: just transfer the primary player */
        Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    }

    /* Handle extra player */
    if ((int16_t)_offerOrdinals.numExtraPlayersWanted < 0) {
        /* Extra player wanted by us (offerOrdinals.numExtraPlayersWanted < 0) */
        if ((int16_t)_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.offerExtraPlayerOrdinal);
        }
    } else {
        /* Extra player offered by them (offerOrdinals.numExtraPlayersWanted >= 0) */
        if ((int16_t)_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.offerExtraPlayerOrdinal);
            Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.numExtraPlayersWanted);
        } else {
            Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.numExtraPlayersWanted);
        }
    }

    /* Flag both teams as changed */
    TeamFileEx *tp = GetTeamPointer(_offerOrdinals.teamOffering);
    tp->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;

    PrunePlayerOffersAgainstRosterLimits();

    if (GetRemainingOfferSwapSlots() == 0) {
        _numOffers = 0;
        _currentMessage = 4;
        ShowChairmanMessage();
    }
}

/* ================================================================
 * Career_AcceptOffer_BuyPlayer  (SWOS2.s line 63474)
 *
 * Handles accepting a buy offer: subtracts purchase amount from
 * balance, processes swap/extra players, flags teams as changed.
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_AcceptOffer_BuyPlayer(void)
{
    Career_CommitNewBalanceAndResetFinanceTotals();

    uint32_t purchaseAmount = (uint32_t)_offerOrdinals.offerAmount * 1000;
    _newBalance -= (int32_t)purchaseAmount;
    _playerPurchases += (int32_t)purchaseAmount;

    ClampNewBalanceAndUpdateClubStatus();
    ClearOverdraftStateIfRecovered();

    if ((int16_t)_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        /* Swap: remove primary as trialist, transfer swap player */
        Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
        Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.offerSwapPlayerOrdinal);
    } else {
        /* No swap: remove primary as trialist */
        Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    }

    /* Handle extra player */
    if ((int16_t)_offerOrdinals.numExtraPlayersWanted < 0) {
        /* Extra player wanted by us */
        if ((int16_t)_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.offerExtraPlayerOrdinal);
        }
    } else {
        /* Extra player offered by them */
        if ((int16_t)_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.numExtraPlayersWanted);
            Career_TransferPlayerAndAddReplacedToMarketPool(_offerOrdinals.teamOffering, _offerOrdinals.offerExtraPlayerOrdinal);
        } else {
            Career_RemovePlayerAndAddAsTrialist(_offerOrdinals.teamOffering, _offerOrdinals.numExtraPlayersWanted);
        }
    }

    /* Flag both teams as changed */
    TeamFileEx *tp = GetTeamPointer(_offerOrdinals.teamOffering);
    tp->team.playerValuesDirty = TEAM_VALUES_DIRTY;
    _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;

    PrunePlayerOffersAgainstRosterLimits();
}

/* ================================================================
 * Career_RemovePlayerAndAddAsTrialist  (SWOS2.s line 63815)
 *
 * Removes player from source team, finds free slot in
 * currentMatchPlayers, saves player ID, copies player data,
 * inserts into roster at correct position, and converts to trialist.
 *
 * in: d0 = source team, d1 = player ordinal
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void Career_RemovePlayerAndAddAsTrialist(uint16_t sourceTeam, uint16_t playerOrd)
{
    uint16_t savedTeam = sourceTeam;
    uint16_t savedOrd = playerOrd;

    /* Remove from source team */
    Career_RemovePlayerFromTeamInternal(sourceTeam, playerOrd);

    /* Get the source player data */
    uint8_t *srcBase = GetPlayer(sourceTeam, playerOrd);
    PlayerFile *srcPf = (PlayerFile *)(srcBase + PF_OFF);

    /* Find a free slot index not used in currentMatchPlayers */
    uint16_t freeSlot = 0;
    while (1) {
        int16_t found = 0;
        int16_t numPl = (int16_t)_careerNumPlayers - 1;
        while (numPl >= 0) {
            if (_currentMatchPlayers[numPl] == (uint8_t)freeSlot) {
                freeSlot++;
                found = 1;
                break;
            }
            numPl--;
        }
        if (!found)
            break;
    }

    /* Get the destination slot in selected team */
    uint8_t *dstBase = GetPlayer(_selTeamNumber, freeSlot);
    TrainingMatch_SaveSelectedPlayerId(freeSlot);

    /* Mark source player as trialist */
    srcPf->positionAndFace |= PF_TRIALIST;  /* bset #2 */
    srcPf->goalStats[0] = 0;
    srcPf->goalStats[1] = 0;
    srcPf->goalStats[2] = 0;
    srcPf->goalStats[3] = 0;

    /* Copy player data (0x26 bytes = 19 words from offset 0x4C) */
    uint8_t *s = srcBase + PF_OFF;
    uint8_t *d = dstBase + PF_OFF;
    for (int16_t k = 0x12; k >= 0; k--) {
        *(uint16_t *)d = *(uint16_t *)s;
        s += 2;
        d += 2;
    }

    /* Determine insertion position in currentMatchPlayers. The original asm read
     * raw 16-bit offsets into the slot-0 competition buffer: type-2 (tournament)
     * -> 0x4E, else -> 0x66. Those are now TYPED field reads (S6 FLAG-3 fix):
     * both offsets sit past DIYCompetition.namePtr@0x28, so the raw byte offsets
     * slid +4 on 64-bit (CFB_NP namePtr growth) and read the wrong field there.
     *   0x4E = base.numTeamsInStage  (DIYCompetition header)
     *   0x66 = matchData+2 = matchData.league.leagueMaxSubstitutes (the league
     *          view names it; for a cup the union covers the same bytes — the
     *          original read matchData+2 regardless of type).
     * Typed access auto-adjusts per target: byte-identical on 32-bit, correct on
     * 64-bit. */
    DIYCompetitionEx *comp = (DIYCompetitionEx *)&_primaryCompetitionBuf;
    uint16_t insertPos;
    if (comp->base.competitionType == 2) {
        insertPos = (uint16_t)comp->base.numTeamsInStage;
    } else {
        insertPos = comp->matchData.league.leagueMaxSubstitutes;
    }
    insertPos += 11;  /* addi.w #$B,d2 */

    /* Shift currentMatchPlayers entries right to make room */
    uint8_t *cmp = _currentMatchPlayers;
    uint16_t numPl = _careerNumPlayers;
    uint8_t *insertPtr = cmp + numPl;  /* end of array */
    int16_t shiftCount = (int16_t)(numPl - insertPos);
    if (shiftCount > 0) {
        shiftCount--;
        while (shiftCount >= 0) {
            *insertPtr = *(insertPtr - 1);  /* move.b -1(a0),(a0) */
            insertPtr--;
            shiftCount--;
        }
    }
    /* Insert the new player at the insertion point */
    *insertPtr = (uint8_t)freeSlot;

    /* Set bench player numbers */
    SetBenchPlayersNumbers(&_careerTeam);

    _careerNumPlayers++;
    SquadNumbers_Free(freeSlot);        /* (real squad numbers: his own, or a free one; teams.c) */
    Career_UpdatePlayerWeeklyWageCache(freeSlot);

    /* Convert player to trialist using source name */
    Career_ConvertPlayerToTrialistFromName(savedTeam, savedOrd);
}

/* ================================================================
 * PickRandomTeamRecordAvoidingTeam  (SWOS2.s line 64261)
 *
 * Picks a random team from TMD file or selected teams: any team OTHER
 * than d6 whose countryNumber EQUALS d7. After 100 misses it returns
 * whatever it drew last.
 *
 * The IDA prototype calls d7 "avoidTeamByte", but only the team word is
 * avoided — the country byte is WANTED: `cmp.b (a0),d7 / beq.s return`
 * (SWOS2.s:97714, :97728). So a generated replacement borrows its name,
 * nationality and face from another club in the same country, which is
 * what the Amiga shows (a Spanish club's replacement gets a Spanish-league
 * name). Testing `!=` here drew them from foreign clubs instead.
 *
 * in:  d6 = team number to avoid (word), d7 = country byte to match
 * out: a0 = pointer to team record
 * ================================================================ */
static uint8_t *pickRandomTeamRecordAvoidingTeam(uint16_t avoidTeamWord, uint8_t countryByte)
{
    TeamFileEx *result;
    uint16_t retries = 100;

    while (1) {
        /* Try TMD file first if non-empty.
         *
         * PACK_BE_U16 of the (countryNumber, teamOrdinal) byte pair
         * produces the Amiga BE-packed id on either host so the
         * word-level avoid check matches. Reading host-native would
         * give (ordinal<<8)|country on LE and never match. */
        if (_careerFileBuffer != 0) {
            uint16_t r = Rand2();
            uint32_t idx = (uint32_t)r * (uint32_t)_careerFileBuffer;
            idx >>= 8;
            result = (TeamFileEx *)&_tmdFileBuffer + idx;

            uint16_t candTeamId = PACK_BE_U16(result->team.countryNumber,
                                              result->team.teamOrdinal);
            if (candTeamId != avoidTeamWord) {
                if (World_FileCountry(result->team.countryNumber) == World_FileCountry(countryByte))
                    return (uint8_t *)result;
            }
            if (retries == 0)
                return (uint8_t *)result;
        }

        /* Try selected teams — same byte-pair fix as above. */
        uint16_t r = Rand2();
        uint32_t idx = (uint32_t)r * (uint32_t)_numSelectedTeams;
        idx >>= 8;
        result = _selectedTeams + idx;

        uint16_t candTeamId2 = PACK_BE_U16(result->team.countryNumber,
                                           result->team.teamOrdinal);
        if (candTeamId2 != avoidTeamWord) {
            if (World_FileCountry(result->team.countryNumber) == World_FileCountry(countryByte))
                return (uint8_t *)result;
        }
        if (retries == 0)
            return (uint8_t *)result;

        retries--;
    }
}

/* ================================================================
 * Career_PickRandomTrialistOriginValue  (SWOS2.s line 64205)
 *
 * Picks a random player name from a random team to use as
 * trialist name. Builds name by taking last word(s) from player
 * surname. Also returns nationality and position flags.
 *
 * in:  d0 = country byte to match, d3 = avoidTeamWord (team#)
 * out: d0 = nationality byte, d3 = position flags (bits 3-4),
 *      a2 = pointer to trialistNameBuffer
 * ================================================================ */
static uint32_t pickRandomTrialistOriginValue(uint8_t countryByte, uint16_t avoidTeamWord)
{
    uint8_t nationality;
    uint8_t posFlags;

restart:;
    uint8_t *nameDst = _trialistNameBuffer;

    /* Pick random team, then random player from it */
    uint8_t *team = pickRandomTeamRecordAvoidingTeam(avoidTeamWord, countryByte);
    uint16_t r = Rand2();
    uint16_t playerIdx = (r & 0xF) * 0x26;
    uint8_t *playerBase = team + playerIdx;
    PlayerFile *tpf = (PlayerFile *)(playerBase + PF_OFF);
    nationality = (uint8_t)tpf->nationality;
    posFlags = tpf->positionAndFace & PF_FACE_MASK; /* position flags bits 3-4 */
    uint8_t *name = (uint8_t *)tpf->playerName;

#ifdef SWOS_DEBUG_AUTOSKIP
    /* [bug11] Capture the SOURCE player chosen for the trialist name's
     * initial. The "*. SURNAME" symptom requires this player's name to
     * literally start with "*. ". Log the first 8 bytes plus the team
     * id so we can correlate against tmdFileBuffer / selTeamsPtr to
     * find where the asterisk-prefixed names are coming from. */
    SWOS_AUTOSKIP_LOG("[bug11] trialistSrc1: team=%p teamId=0x%04X playerIdx=%u "
                      "playerName=%02X %02X %02X %02X %02X %02X %02X %02X (\"%c%c%c%c%c%c\")",
                      (void *)team,
                      (unsigned)((team[0] << 8) | team[1]),
                      (unsigned)(playerIdx / 0x26),
                      (unsigned)(uint8_t)name[0], (unsigned)(uint8_t)name[1],
                      (unsigned)(uint8_t)name[2], (unsigned)(uint8_t)name[3],
                      (unsigned)(uint8_t)name[4], (unsigned)(uint8_t)name[5],
                      (unsigned)(uint8_t)name[6], (unsigned)(uint8_t)name[7],
                      name[0] >= 0x20 && name[0] < 0x7F ? (char)name[0] : '?',
                      name[1] >= 0x20 && name[1] < 0x7F ? (char)name[1] : '?',
                      name[2] >= 0x20 && name[2] < 0x7F ? (char)name[2] : '?',
                      name[3] >= 0x20 && name[3] < 0x7F ? (char)name[3] : '?',
                      name[4] >= 0x20 && name[4] < 0x7F ? (char)name[4] : '?',
                      name[5] >= 0x20 && name[5] < 0x7F ? (char)name[5] : '?');
#endif

    /* Skip source players whose name starts with '*' — that's the
     * "*ERR* " marker left by Career_RemovePlayerFromSelectedTeamRoster
     * on a freshly-removed slot (teams.c:1348, matches Amiga
     * SWOS2.s:97253). Without this guard the trialist generator copies
     * "*ERR*" into trialistNameBuffer and the new trialist's playerName
     * becomes "*ERR* SURNAME", which then propagates further on the next
     * cycle when this trialist's slot is sampled as a source. */
    if (*name == '*')
        goto restart;

    /* Copy characters from name to trialistNameBuffer */
    while (*name != 0) {
        if (*name == ' ')
            goto handleSpace;
        *nameDst++ = *name++;
        if (nameDst >= TRIALIST_NAME_BUF_END)
            goto restart;
        continue;

    handleSpace:
        /* On space: pick a new random team/player, take last word of their surname */
        team = pickRandomTeamRecordAvoidingTeam(avoidTeamWord, countryByte);
        r = Rand2();
        playerIdx = (r & 0xF) * 0x26;
        playerBase = team + playerIdx;
        tpf = (PlayerFile *)(playerBase + PF_OFF);
        nationality = (uint8_t)tpf->nationality;
        posFlags = tpf->positionAndFace & PF_FACE_MASK;
        name = (uint8_t *)tpf->playerName;

        /* Same '*ERR* ' marker guard as the first-source pick above —
         * skip a "*X" source so the trialist doesn't pick up the last
         * word "ERR*" as its surname. */
        if (*name == '*')
            goto restart;

        /* Find end of name string */
        uint8_t *nameEnd = name;
        while (*nameEnd != 0)
            nameEnd++;

        /* Walk back to find last space */
        uint8_t *scan = nameEnd;
        while (scan > name) {
            scan--;
            if (*scan == ' ') {
                /* Found space — copy from here (including space) to end */
                break;
            }
        }
        if (scan == name)
            goto restart;  /* no space found, retry */

        /* Copy from last word to end */
        while (*scan != 0) {
            *nameDst++ = *scan++;
            if (nameDst >= TRIALIST_NAME_BUF_END)
                goto restart;
        }
        break;
    }

    *nameDst = 0;  /* null-terminate */

    /* Return packed result: nationality in low byte, posFlags in bits 8-12 */
    return ((uint32_t)posFlags << 8) | (uint32_t)nationality;
}

/* ================================================================
 * Career_GenerateTrialistForTeamPlayer  (SWOS2.s line 63941)
 *
 * Generates trialist data for a player slot. Picks random origin,
 * copies name, clears stats, marks as trialist.
 *
 * in: d1 = player file index
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_GenerateTrialistForTeamPlayer(uint16_t playerIndex)
{
    uint16_t d4 = _selTeamNumber;
    uint16_t d5 = _selTeamNumber;
    uint16_t d6 = playerIndex;

    uint8_t *base = GetPlayer(_selTeamNumber, playerIndex);
    PlayerFile *pf = (PlayerFile *)(base + PF_OFF);

    uint8_t country = (uint8_t)(_selTeamNumber >> 8);  /* move.b — high byte */
    uint16_t avoidWord = _selTeamNumber;

    uint32_t result = pickRandomTrialistOriginValue(country, avoidWord);
    uint8_t nationality = (uint8_t)(result & 0xFF);
    uint8_t posFlags = (uint8_t)((result >> 8) & 0xFF);

    pf->nationality = (int8_t)nationality;

    uint8_t posAndFace = pf->positionAndFace;
    posAndFace &= (uint8_t)~PF_FACE_MASK;  /* andi.b #$E7 — clear bits 3-4 */
    posAndFace |= posFlags;
    pf->positionAndFace = posAndFace;

    /* Copy name from trialistNameBuffer */
    StringCopy(_trialistNameBuffer, (uint8_t *)pf->playerName);

    /* Clear stats */
    pf->goalStats[0] = 0;
    pf->goalStats[1] = 0;
    pf->goalStats[2] = 0;
    pf->goalStats[3] = 0;
    pf->cardsInjuries = 0;
    *(uint32_t *)&pf->passing = 0;  /* clr.l $68(a3) — clear 4 bytes at once */
    pf->price = 0;

    /* Mark as trialist (bit 1) */
    pf->positionAndFace |= PF_LOAN;  /* bset #1 */
    pf->form = 0;
}

/* ================================================================
 * Career_InitTrialistAttributes  (SWOS2.s line 64019)
 *
 * Computes skill attributes for a trialist based on team average
 * and random distribution. Uses trialistSkillTemplates for the
 * final skill assignment.
 *
 * in: a3 = player pointer (to TeamFileEx base, player at +0x4C)
 *     d5 = source team number
 * ================================================================ */
static void careerInitTrialistAttributes(PlayerFile *pf, uint16_t sourceTeam)
{
    /* Save current player data to tempTeamBuffer */
    uint8_t *src = (uint8_t *)pf;
    uint8_t *dst = _tempTeamBuffer;
    for (int16_t i = 0x25; i >= 0; i--)
        *dst++ = *src++;

    /* Calculate average price of players with same position (or all if same team) */
    uint16_t numPl = _careerNumPlayers;
    if (sourceTeam != _selTeamNumber)
        numPl = 16;

    int16_t iter = (int16_t)numPl - 1;
    uint16_t totalPrice = 0;
    uint16_t count = 0;
    uint16_t posGroup = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> 5;

    while (iter >= 0) {
        uint8_t *p = GetPlayerAtIndex(sourceTeam, (uint16_t)iter);
        PlayerFile *ppf = (PlayerFile *)(p + PF_OFF);

        if (sourceTeam == _selTeamNumber) {
            /* Same team: count all players */
            totalPrice += (uint8_t)ppf->price;
            count++;
        } else {
            /* Different team: only count same position group */
            uint16_t pg = ((uint16_t)ppf->positionAndFace & PF_POSITION_MASK) >> 5;
            if (pg == posGroup) {
                totalPrice += (uint8_t)ppf->price;
                count++;
            }
        }
        iter--;
    }

    /* Calculate skill level: avg/3, clamped to 0-15 */
    int32_t avgPrice = (int32_t)(int16_t)totalPrice;  /* ext.l d2 */
    uint16_t skillLevel = (uint16_t)(avgPrice / count);
    skillLevel = skillLevel / 3;
    if (skillLevel > 15)
        skillLevel = 15;

    if (sourceTeam != _selTeamNumber) {
        /* Foreign team: add random -1 to +2 adjustment */
        uint16_t r = Rand2();
        int16_t adj = (int16_t)(r & 3) - 1;
        skillLevel = (uint16_t)((int16_t)skillLevel + adj);
    } else {
        /* Same team: pick random team, random player, use their price */
        uint8_t *randomTeam;
        do {
            uint16_t r = Rand2();
            uint32_t tidx = (uint32_t)r * (uint32_t)_numSelectedTeams;
            tidx >>= 8;
            tidx *= 0x2AC;
            randomTeam = &_selTeamsPtr + tidx;
        } while ((uint8_t)(_selTeamNumber >> 8) != *randomTeam);  /* bne: loop until SAME country */

        uint16_t r = Rand2();
        uint16_t pidx = (r & 0xF) * 0x26;
        uint8_t *rp = randomTeam + pidx;
        PlayerFile *rpPf = (PlayerFile *)(rp + PF_OFF);
        int16_t rpPrice = (int8_t)rpPf->price;

        /* Random skill distribution lookup */
        r = Rand2();
        uint16_t roll = (uint16_t)((uint32_t)r * 100) >> 8;
        int16_t adjust = -90;  /* 0xFFA6 as int16_t */
        uint8_t *distrib = _trialistSkillDistribution;
        while (roll >= *distrib) {
            roll -= *distrib;
            distrib++;
            adjust += 10;
        }

        /* Get average player price for career team */
        uint16_t avgTeamPrice = GetAveragePlayerPrice(&_careerTeam);
        int16_t factor = (int16_t)(avgTeamPrice * 2) + adjust;
        if (factor < 0 || factor > 100)
            factor -= adjust;

        /* Scale skill by price ratio */
        uint32_t scaled = (uint32_t)((uint16_t)factor) * (uint16_t)rpPrice;
        skillLevel = (uint16_t)(scaled / 100);
        skillLevel = skillLevel / 3;
    }

    /* Clamp to 0-15 */
    if ((int16_t)skillLevel < 0)
        skillLevel = 0;
    else if (skillLevel > 15)
        skillLevel = 15;

    /* Look up skill template */
    uint16_t templateOffset = skillLevel << 5;  /* 32 bytes per level */
    uint8_t *tmpl = _trialistSkillTemplates + templateOffset;

    uint16_t posIdx = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> 5;
    uint16_t posOff = posIdx << 2;
    /* Both tables are Amiga byte tables read as ONE longword (`move.l (a0,d1.w),d0`,
     * SWOS2.s:97573), so the four bytes {passing, velocityHeading, tacklingControl,
     * speedFinishing} land BIG-ENDIAN in the register. A native `*(uint32_t *)` read put
     * them in the opposite order on x86, which is why a generated goalkeeper was worth
     * 25K: every GK template entry is {0,0,0,price}, the price is the LOW byte of the
     * big-endian long, and `skills & 0xFF` was picking up the leading zero instead
     * (price 0 -> _avgTeamValues[0] = 5 -> _avgTeamValueBases[5] = 25K). Outfielders lost
     * their speed nibble from the price sum below for the same reason. */
    uint32_t skills = BE_READ_U32(tmpl + posOff);
    uint32_t masks = BE_READ_U32(_trialistSkillBitMasks + posOff);

    /* Restore saved player data from tempTeamBuffer */
    src = _tempTeamBuffer;
    dst = (uint8_t *)pf;
    for (int16_t i = 0x25; i >= 0; i--)
        *dst++ = *src++;

    if (posIdx == 0) {
        /* Goalkeeper: just set price, clear skills (asm: `move.b d0,$6C(a3) / clr.l
         * $68(a3)`, SWOS2.s:97584) — the price is the last byte of the template entry. */
        pf->price = (int8_t)(skills & 0xFF);
        BE_WRITE_U32(&pf->passing, 0);
        return;
    }

    /* Set skills with bit masks. Written back big-endian, so each template byte lands in
     * the field it was authored for (passing, velocityHeading, tacklingControl,
     * speedFinishing) on either target. */
    BE_WRITE_U32(&pf->passing, skills | masks);

    /* Calculate total skill (sum of all nibbles) for price. Seven nibbles, not eight:
     * the one dropped is the high nibble of `passing`, which is always 0 (asm loops
     * `dbf` from 6, SWOS2.s:97590). Reading the long natively dropped the high nibble of
     * `speedFinishing` instead — the player's SPEED — and under-priced every outfielder. */
    uint8_t totalSkill = 0;
    for (int16_t i = 6; i >= 0; i--) {
        totalSkill += (uint8_t)(skills & 0xF);
        skills >>= 4;
    }
    pf->price = (int8_t)totalSkill;
}

/* ================================================================
 * Career_ConvertPlayerToTrialistFromName  (SWOS2.s line 63975)
 *
 * Converts a player from another team to a trialist using their name.
 * Unlike Career_GenerateTrialistForTeamPlayer, this uses the source
 * team's country byte for origin instead of selTeamNumber.
 *
 * in: d0 = source team, d1 = player ordinal
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_ConvertPlayerToTrialistFromName(uint16_t sourceTeam, uint16_t playerOrd)
{
    uint16_t d5 = sourceTeam;
    uint16_t d6 = playerOrd;

    uint8_t *base = GetPlayer(sourceTeam, playerOrd);
    PlayerFile *pf = (PlayerFile *)(base + PF_OFF);

    /* High byte of the team number = the country to draw from; the full word = the team to avoid */
    uint8_t country = (uint8_t)(sourceTeam >> 8);
    uint16_t avoidWord = sourceTeam;

    uint32_t result = pickRandomTrialistOriginValue(country, avoidWord);
    uint8_t nationality = (uint8_t)(result & 0xFF);
    uint8_t posFlags = (uint8_t)((result >> 8) & 0xFF);

    pf->nationality = (int8_t)nationality;

    uint8_t posAndFace = pf->positionAndFace;
    posAndFace &= (uint8_t)~PF_FACE_MASK;
    posAndFace |= posFlags;
    pf->positionAndFace = posAndFace;

    /* Copy name from trialistNameBuffer */
    StringCopy(_trialistNameBuffer, (uint8_t *)pf->playerName);

    /* Clear stats */
    pf->goalStats[0] = 0;
    pf->goalStats[1] = 0;
    pf->goalStats[2] = 0;
    pf->goalStats[3] = 0;
    pf->cardsInjuries = 0;

    /* Init skill attributes */
    careerInitTrialistAttributes(pf, d5);

    if (d5 == _selTeamNumber) {
        /* Same team: set as goalkeeper sub and clear trialist/other flags */
        pf->form = (int8_t)PLAYER_FORM_TRIALIST;
        pf->positionAndFace &= ~PF_LOAN;  /* bclr #1 */
        pf->positionAndFace &= ~PF_TRIALIST;  /* bclr #2 */
        pf->positionAndFace &= ~PF_INFORM;  /* bclr #0 */
    } else {
        /* Different team: add to career pool */
        AddCareerPoolPlayer(d5, d6, (PlayerFile *)base);
        _transferPoolSlotsUsed++;
    }
}

/* ================================================================
 * TrainingMatch_AddTrialPlayerToSelectedTeam  (SWOS2.s line 63782)
 *
 * Adds a trial player to the selected team roster. Finds a free
 * slot index, marks player as trialist with specified role.
 *
 * in: d0 = role (0=GK, 1-7=field positions)
 * ================================================================ */
__attribute__((used, externally_visible))
void TrainingMatch_AddTrialPlayerToSelectedTeam(uint16_t role)
{
    /* Find a free slot index not used in currentMatchPlayers */
    uint16_t freeSlot = 0;
    while (1) {
        int16_t found = 0;
        int16_t numPl = (int16_t)_careerNumPlayers - 1;
        while (numPl >= 0) {
            if (_currentMatchPlayers[numPl] == (uint8_t)freeSlot) {
                freeSlot++;
                found = 1;
                break;
            }
            numPl--;
        }
        if (!found)
            break;
    }

    /* Get player slot and set it up */
    uint8_t *base = GetPlayer(_selTeamNumber, freeSlot);
    TrainingMatch_SaveSelectedPlayerId(freeSlot);

    PlayerFile *pf = (PlayerFile *)(base + PF_OFF);
    pf->positionAndFace |= PF_TRIALIST;  /* bset #2 — mark as trialist */
    pf->shirtNumber = 0x10;       /* shirt number 16 */
    if (_selTeamNumber == BE_READ_U16(&_careerTeam))
        SquadNumbers_Free(freeSlot);    /* (real squad numbers: a free one, teams.c) */

    /* Set position/role: lsl.w #5,d7; move.b d7,$66(a3) */
    pf->positionAndFace = (uint8_t)(role << 5);  /* this overwrites — matches asm: move.b d7,$66 */

    /* Generate trialist attributes */
    Career_GenerateTrialistForTeamPlayer(freeSlot);

    /* Add to currentMatchPlayers */
    _currentMatchPlayers[_careerNumPlayers] = (uint8_t)freeSlot;
    _careerNumPlayers++;

    Career_UpdatePlayerWeeklyWageCache(freeSlot);
}

/* ================================================================
 * TrainingMatch_AdjustSelectedTeamRosterForTrialists  (SWOS2.s line 63660)
 *
 * Adjusts the selected team roster for training match by removing
 * excess goalkeepers/subs and adding trial players to fill gaps.
 *
 * in: d0 = team number, d1 = base count, a0 = team pointer
 * ================================================================ */
__attribute__((used, externally_visible))
void TrainingMatch_AdjustSelectedTeamRosterForTrialists(uint16_t teamNum, uint16_t baseCount, TeamFileEx *teamPtr)
{
    if (_isNationalSelector != 0)
        return;

    if (_trainingGame != 0)
        baseCount <<= 1;  /* lsl.w #1,d1 */

    uint16_t d4 = teamNum;
    uint16_t d5 = baseCount;

    if (_trainingGame == 0) {
        /* Non-training: remove excess goalkeepers if more than 2 subs */
        uint32_t subsResult = GetNumberOfAvailableSubstitutes(teamPtr, 0);
        uint16_t subs = (uint16_t)(subsResult & 0xFFFF);
        if (subs > 2) {
            int16_t d2 = (int16_t)_careerNumPlayers - 1;
            while (d2 >= 0) {
                uint8_t *p = GetPlayerAtIndex(_selTeamNumber, (uint16_t)d2);
                PlayerFile *pf = (PlayerFile *)(p + PF_OFF);
                uint16_t pos = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> 5;
                if (pos == 0) {
                    /* GK — check if trialist (bit 1 of positionAndFace) */
                    if (pf->positionAndFace & PF_LOAN)
                        goto removeAndRecheck1;
                }
                d2--;
                continue;
            removeAndRecheck1:
                /* Asm passes d1 (player number from GetPlayerAtIndex), not d2 (index) */
                Career_RemovePlayerFromSelectedTeamRoster(_cGetPlayerAtIndexFileIdx);
                break;
            }
        }

        /* Remove excess starting players if more than 16 */
        uint32_t startResult = GetNumberOfAvailableStartingPlayers(teamPtr, 0);
        uint16_t starters = (uint16_t)(startResult & 0xFFFF);
        if (starters > 16) {
            int16_t d2 = (int16_t)_careerNumPlayers - 1;
            while (d2 >= 0) {
                uint8_t *p = GetPlayerAtIndex(_selTeamNumber, (uint16_t)d2);
                PlayerFile *pf = (PlayerFile *)(p + PF_OFF);
                uint16_t pos = ((uint16_t)pf->positionAndFace & PF_POSITION_MASK) >> 5;
                if (pos == 0) {
                    /* GK — skip */
                    d2--;
                    continue;
                }
                if (pf->positionAndFace & PF_LOAN)
                    goto removeAndRecheck2;
                d2--;
                continue;
            removeAndRecheck2:
                /* Asm passes d1 (player number from GetPlayerAtIndex), not d2 (index) */
                Career_RemovePlayerFromSelectedTeamRoster(_cGetPlayerAtIndexFileIdx);
                break;
            }
        }
    }

    /* Add trial GK if needed: ensure at least 2 goalkeeper subs */
    while (_careerNumPlayers < 30) {
        uint32_t subsResult = GetNumberOfAvailableSubstitutes(teamPtr, 0);
        uint16_t subs = (uint16_t)(subsResult & 0xFFFF);
        if (subs >= 2)
            break;
        if (subs == 0) {
            TrainingMatch_AddTrialPlayerToSelectedTeam(0);  /* add GK */
            continue;
        }
        /* subs == 1: check if baseCount-11 is within [0..2] (skip adding if bench has room) */
        int16_t benchCheck = (int16_t)d5 - 11;
        if ((uint16_t)benchCheck <= 2)
            break;
        TrainingMatch_AddTrialPlayerToSelectedTeam(0);
    }

    /* Add trial field players if needed: fill to required starter count */
    while (_careerNumPlayers < 30) {
        uint32_t startResult = GetNumberOfAvailableStartingPlayers(teamPtr, 0);
        uint16_t starters = (uint16_t)(startResult & 0xFFFF);

        uint16_t required = 11;
        if (_trainingGame != 0)
            required = 22;
        required += d5 - 2;

        if ((int16_t)starters >= (int16_t)required)
            break;

        /* Add random field player (role 1-7) */
        uint16_t r = Rand2();
        uint16_t role = (uint16_t)(((uint32_t)r * 7) >> 8) + 1;
        TrainingMatch_AddTrialPlayerToSelectedTeam(role);
    }
}

/* ================================================================
 * PutPlayerOnMarketMenu  (SWOS2.s line 67670)
 *
 * Transfer market sub-menu: shows squad and lets user toggle players
 * on/off the transfer market (max 5). Loops until user cancels or
 * 5 players are on market. Then updates transfer market UI state.
 * ================================================================ */
extern uint32_t _viewSquadFlags;
extern volatile int16_t _viewSquadSelectedTeam;
extern uint8_t _aPutPlayerOnTra[];  /* "PUT PLAYER ON TRANSFER MARKET" */

__attribute__((used, externally_visible))
void PutPlayerOnMarketMenu(void)
{
    uint16_t preselect = 0xFFFF;

    for (;;) {
        /* Build eligible player bitfield (bit 1 of positionAndFace set).
         * ASM uses the FILE INDEX (d1 from GetPlayerAtIndex) for the bitfield,
         * not the loop counter. _cGetPlayerAtIndexFileIdx holds this value. */
        uint32_t eligible = 0;
        for (int16_t i = (int16_t)(_careerNumPlayers - 1); i >= 0; i--) {
            uint8_t *player = GetPlayerAtIndex(_selTeamNumber, (uint16_t)i);
            PlayerFile *pf = (PlayerFile *)(player + 76);
            if (pf->positionAndFace & PF_LOAN)
                eligible |= (1u << (uint16_t)_cGetPlayerAtIndexFileIdx);
        }

        /* Get team pointer */
        TeamFileEx *teamPtr = GetTeamPointer(_selTeamNumber);

        /* Build transfer market bitfield */
        uint32_t marketMask = 0;
        uint16_t numOnMarket = _numPlayersOnTransferMarket;
        for (uint16_t i = 0; i < numOnMarket; i++) {
            uint16_t pid = _playersOnTransferMarket[i];
            marketMask |= (1u << pid);
        }

        /* Show squad selection (clears viewSquadFlags as per ShowSquad entry point) */
        _viewSquadFlags = 0;
        int16_t selected = ShowSquad((TeamFile *)teamPtr, _aPutPlayerOnTra, eligible, marketMask, 0, preselect);
        preselect = (uint16_t)_viewSquadSelectedTeam;

        /* If cancelled (ShowSquad returns d0 < 0), exit loop.
         * ASM checks d0 (viewSquadSelectedPlayer) via bmi, not d3 (viewSquadSelectedTeam). */
        if (selected < 0)
            break;

        /* Toggle player on/off transfer market */
        uint16_t playerIdx = (uint16_t)selected;
        if (FindPlayerOnTransferMarket(playerIdx) == 0) {
            /* Already on market — remove */
            RemovePlayerFromTransferMarket(playerIdx);
        } else {
            /* Not on market — add */
            AddPlayerToTransferMarket(playerIdx);
            if (_numPlayersOnTransferMarket == 5)
                break;
        }
    }

    /* Update transfer market UI state */
    CheckNumPlayersOnTransferMarket();
    if (_numPlayersOnTransferMarket == 5)
        SetCurrentEntry(6);
}


/* ==========================================================================
 * Chunk 202 — BuyOtherDomesticPlayer + BuyOtherForeignPlayer
 * (SWOS2.s lines 63572-63613)
 * ========================================================================== */

extern uint16_t _selectedTeamCountryNumber;
extern uint8_t  _aBuyOtherDomest[];
extern uint8_t  _aBuyOtherForeig[];
extern uint8_t  _buyOtherPlayersMenu[];

/* BuyOtherDomesticPlayer — open dialog to buy player from another domestic team.
 * Calls ChooseTeamsDialogWithNationality with selTeamNumber nationality,
 * then shows buyOtherPlayersMenu. */
__attribute__((used, externally_visible))
void BuyOtherDomesticPlayer(void)
{
    /* move.b (selTeamNumber).l,d0 — reads HIGH byte on big-endian = country code */
    uint16_t nationality = (uint8_t)(_selTeamNumber >> 8);
    ChooseTeamsDialogWithNationality(nationality, 1, 0, 0, _aBuyOtherDomest, (void *)0);
    if ((int16_t)_selectedTeamCountryNumber == -1)
        return;
    _offerOrdinals.teamOffering = _selectedTeamCountryNumber;
    _buyOtherPlayersTitlePtr = (uint8_t *)_aBuyOtherDomest;
    ShowMenu(_buyOtherPlayersMenu);
    /* Original asm has duplicate check — preserve as-is */
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
}

/* BuyOtherForeignPlayer — open dialog to buy player from foreign team.
 * Calls ChooseTeamsDialog with showClub=1, then shows buyOtherPlayersMenu. */
__attribute__((used, externally_visible))
void BuyOtherForeignPlayer(void)
{
    ChooseTeamsDialog(1, 0, 0, _aBuyOtherForeig, (void *)0);
    if ((int16_t)_selectedTeamCountryNumber == -1)
        return;
    _offerOrdinals.teamOffering = _selectedTeamCountryNumber;
    _buyOtherPlayersTitlePtr = (uint8_t *)_aBuyOtherForeig;
    ShowMenu(_buyOtherPlayersMenu);
    /* Original asm has duplicate check — preserve as-is */
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
    if (_transferCompleted != 0) {
        BuyPlayerExit();
        return;
    }
}

/* =============================================================== */
/* Chunk 207: PlayerMarketBuildVisiblePlayerList +                  */
/*            PlayerMarketPlayerMatchesFilters                     */
/* =============================================================== */

extern uint8_t *_positionGroupTable[];   /* 4 pointers to position group byte arrays (0xFF terminated) */
extern uint8_t  _playerRatingFilterThresholds[];  /* 17-byte lookup: filter index → min rating */
extern uintptr_t _playerMarketVisibleItems[];      /* up to 10 pointers to market entries */

/* PlayerMarketPlayerMatchesFilters — filter predicate for player market.
 * blockPtr uses the "block pointer" convention: blockPtr + 0x4C = PlayerFile start.
 * Returns 0 if player matches all active filters, 1 otherwise. */
static int16_t playerMarketPlayerMatchesFilters(uint8_t *blockPtr, int16_t ratingThreshold,
                                                 uint8_t *countryRange, uint8_t *positionList,
                                                 int16_t skillBitIndex)
{
    PlayerFile *pf = (PlayerFile *)(blockPtr + 76);  /* blockPtr + $4C */

    /* --- Rating/price filter --- */
    if (ratingThreshold >= 0) {
        int8_t diff = (int8_t)((uint8_t)pf->price - (uint8_t)ratingThreshold);
        if (diff > 6 || diff < (int8_t)0xFA)   /* 0xFA = -6 signed */
            return 1;
    }

    /* --- Country/nationality range filter --- */
    if (countryRange != (uint8_t *)0) {
        uint8_t nat = (uint8_t)pf->nationality;
        if (nat < countryRange[0])              /* bcs = unsigned below */
            return 1;
        if (nat >= countryRange[1])             /* bcc = unsigned above or equal */
            return 1;
    }

    /* --- Position group filter (from positionGroupTable) --- */
    if (positionList != (uint8_t *)0) {
        uint8_t posGroup = (uint8_t)((pf->positionAndFace & PF_POSITION_MASK) >> 5);
        uint8_t *p = positionList;
        for (;;) {
            if ((int8_t)*p < 0)                 /* 0xFF terminator (negative as signed) */
                return 1;                       /* position not found → no match */
            if (*p == posGroup)
                break;                          /* found → pass this filter */
            p++;
        }
    }

    /* --- Specific position skill bit filter --- */
    if (_playerMarketFilterPosition != 0 && skillBitIndex >= 0) {
        uint8_t posGroup = (uint8_t)((pf->positionAndFace & PF_POSITION_MASK) >> 5);
        if (posGroup == 0)
            return 1;                           /* goalkeepers excluded */
        /* Reconstruct big-endian skills longword from 4 consecutive bytes */
        uint32_t skills = ((uint32_t)(uint8_t)pf->passing << 24) |
                          ((uint32_t)(uint8_t)pf->velocityHeading << 16) |
                          ((uint32_t)(uint8_t)pf->tacklingControl << 8) |
                          (uint32_t)(uint8_t)pf->speedFinishing;
        if (!(skills & ((uint32_t)1 << skillBitIndex)))
            return 1;                           /* required skill bit not set */
    }

    return 0;   /* all filters passed */
}

/* PlayerMarketBuildVisiblePlayerList — builds the visible player list for the
 * transfer market browser.  Sets up filter state from globals, iterates
 * domestic or foreign market entries, applies filters, and populates the
 * playerMarketVisibleItems array with pointers to matching entries within
 * the current scroll window (up to 10 items). */
__attribute__((used, externally_visible))
void PlayerMarketBuildVisiblePlayerList(void)
{
    /* --- Set up position group list (a6) --- */
    uint8_t *positionList = (uint8_t *)0;
    int16_t filterPos = (int16_t)_playerMarketFilterPosition;
    if (filterPos >= 0) {
        positionList = _positionGroupTable[filterPos];
    }

    /* --- Set up skill bit index (d6) from age group filter --- */
    int16_t skillBitIndex = (int16_t)_playerMarketFilterAgeGroup;
    if (skillBitIndex >= 0) {
        skillBitIndex = 6 - (int16_t)_playerMarketFilterAgeGroup;
        skillBitIndex = skillBitIndex * 4 + 3;
    }

    /* --- Set up rating threshold (d4) --- */
    int16_t ratingThreshold = (int16_t)_playerMarketFilterRatingIndex;
    if (ratingThreshold >= 0) {
        ratingThreshold = (int16_t)(uint8_t)_playerRatingFilterThresholds[ratingThreshold];
    }

    /* --- Set up country range pointer (a5) --- */
    uint8_t *countryRange = (uint8_t *)0;
    if (_showDomesticPlayerMarket == 0) {
        uint16_t countryFilter = _playerMarketFilterCountry;
        if (countryFilter != 6) {
            countryRange = &_shortCountryNames[0x1CB] + countryFilter;
        }
    }

    /* --- Reset counters --- */
    _playerMarketTotalMatchCount = 0;
    _playerMarketVisibleItemCount = 0;

    uintptr_t *visPtr = _playerMarketVisibleItems;

    if (_showDomesticPlayerMarket != 0) {
        /* ---- Domestic market path ---- */
        uint8_t *a1 = _homeMarketPlayers;
        uint16_t count = _numHomeMarketPlayers;
        if (count == 0)
            return;
        uint16_t i = count;
        do {
            /* a1 points at a homeMarketPlayers entry: byte[0]=country,
             * byte[1]=ordinal, byte[2]=playerOrd. Read teamNum as
             * PACK_BE_U16 so it matches the format VerifyTeamLoaded /
             * findTeamInCache use to look up team entries — a raw
             * `*(uint16_t *)a1` on LE would byte-swap the country and
             * ordinal, producing a teamNum that's never in the cache
             * and panicking. */
            uint16_t teamNum = PACK_BE_U16(a1[0], a1[1]);
            TeamFileEx *teamData = VerifyTeamLoaded(teamNum);
            uint16_t ordinal = (uint16_t)(uint8_t)a1[2];
            uint8_t *blockPtr = (uint8_t *)teamData + (uint32_t)ordinal * 38;  /* mulu #$26 */
            if (playerMarketPlayerMatchesFilters(blockPtr, ratingThreshold,
                                                  countryRange, positionList,
                                                  skillBitIndex) == 0) {
                _playerMarketTotalMatchCount++;
                uint16_t scrollOff = _playerMarketListScrollOffset;
                if (scrollOff < _playerMarketTotalMatchCount &&
                    scrollOff + 10 >= _playerMarketTotalMatchCount) {
                    *visPtr++ = (uintptr_t)a1;
                    _playerMarketVisibleItemCount++;
                }
            }
            a1 += 4;
            i--;
        } while (i != 0);
    } else {
        /* ---- Foreign market path ---- */
        uint8_t *a1 = _foreignMarketPlayers;
        uint16_t count = _numForeignMarketPlayers;
        if (count == 0)
            return;
        uint16_t i = count;
        do {
            /* PACK_BE_U16 read — see earlier comment in domestic loop. */
            uint16_t teamId = PACK_BE_U16(a1[0], a1[1]);
            uint8_t ordinal = a1[2];
            PoolPlayer *pool = FindPoolplyrDatPlayer(teamId, (uint16_t)ordinal);
            if (!pool) {
                /* Stale foreignMarketPlayers entry whose team isn't
                 * in the loaded pool (legacy bad-saved data — see
                 * FindPoolplyrDatPlayer). Skip it instead of
                 * dereferencing NULL in the filter check. */
                a1 += 4;
                i--;
                continue;
            }
            uint8_t *blockPtr = (uint8_t *)pool - 0x38;   /* lea -$38(a0),a0 */
            if (playerMarketPlayerMatchesFilters(blockPtr, ratingThreshold,
                                                  countryRange, positionList,
                                                  skillBitIndex) == 0) {
                _playerMarketTotalMatchCount++;
                uint16_t scrollOff = _playerMarketListScrollOffset;
                if (scrollOff < _playerMarketTotalMatchCount &&
                    scrollOff + 10 >= _playerMarketTotalMatchCount) {
                    *visPtr++ = (uintptr_t)a1;
                    _playerMarketVisibleItemCount++;
                }
            }
            a1 += 4;
            i--;
        } while (i != 0);
    }
}

/* ================================================================
 * CreateRandomTransferOfferForPlayer  (SWOS2.s line 63443)
 *
 * Creates a random transfer offer for a specified player. Picks a
 * random eligible team (from home or foreign market), determines
 * swap/extra players, calculates offer amount with negotiation steps,
 * and writes the offer to the transfer offer table.
 *
 * Asm register convention:
 *   in:  d1.w = player ordinal (offerOrdinals.offerPrimaryPlayerOrdinal)
 *        d7.b = ref skill (for team eligibility)
 *   out: none (offer written to transferOfferTable)
 * ================================================================ */
extern uint16_t _offerSearchRetryCount;
extern uint16_t _offerPrimaryPositionMask;
extern uint16_t _offerExtraPositionMask;
extern uint16_t _offerTargetValueBase;
extern uint16_t _offerValueBaseSum;
extern uint16_t _offerSwapValueBaseSum;
extern uint8_t  _offerCandidateTeamIndexBuffer[];
extern uint8_t  _offerLifespanTable[];
/* offerNegotiationStep1Amount is now offerOrdinals.offerNegotiationStep1Amount */
extern uint16_t _offerNegotiationStep2Amount;
extern uint16_t _swapPlayerValueRandom;
extern uint16_t _extraPlayerValueRandom;

extern uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);
extern uint16_t SnapToNearestAvgTeamValueBase(uint16_t value);
extern uint16_t SnapUpToAvgTeamValueBase(uint16_t value);
extern void SaveRand2State(void);
extern void RestoreRand2State(void);
extern void Randomize2(uint16_t _seed);
extern uint16_t GetPlayerRandomNum(PlayerFile *playerFile);

__attribute__((used, externally_visible))
void CreateRandomTransferOfferForPlayer(uint16_t playerOrdinal)
{
    _offerOrdinals.offerPrimaryPlayerOrdinal = playerOrdinal;

    /* Get value base for the primary player */
    uint16_t avgVal = GetAvgTeamValueBaseForCurrentPlayer(_selTeamNumber, playerOrdinal);
    _offerValueBaseSum = avgVal;

    /* Get player data and check injury */
    uint8_t *playerEntry = GetPlayer(_selTeamNumber, playerOrdinal);
    PlayerFile *pfEntry = (PlayerFile *)(playerEntry + PF_OFF);
    uint16_t refSkill = (uint8_t)pfEntry->price;
    if (IsPlayerInjured((PlayerFile *)playerEntry, -1) != 0) {
        SWOS_AUTOSKIP_LOG("[bug2]   CreateOffer playerOrd=%u: BAILED — injured",
                          (unsigned)playerOrdinal);
        return;
    }

    /* Get position mask */
    uint16_t posMask = (uint16_t)pfEntry->positionAndFace & PF_POSITION_MASK;
    _offerPrimaryPositionMask = posMask;

    /* Decide home vs foreign market */
    uint16_t rnd = Rand2();
    uint8_t *teamsBase;
    uint16_t teamCount;

    if (rnd >= 8) {
        /* Home market path */
        _homeMarketRandomPickRetries++;
        uint8_t *a0 = &_selTeamsPtr;
        teamsBase = a0;
        uint16_t d5 = 0;
        uint16_t d4 = 0;
        /* selTeamNumber MSB is the country (BE-packed: country<<8 | ordinal).
         * Amiga m68k `move.b selTeamNumber.l,d0` reads the MSB, so the byte
         * being compared against a0tf->countryNumber is the country. On LE
         * x86 *(uint8_t *)&selTeamNumber would read the *ordinal* instead
         * and the home-market scan would almost never match, leaving
         * teamCount=0 and the function bailing at "no eligible buyer". */
        uint8_t selTeamByte = (uint8_t)(_selTeamNumber >> 8);
        while (d4 < _numSelectedTeams) {
            if (selTeamByte == *a0) {
                if (IsTeamEligibleForOfferByRefSkill((TeamFileEx *)a0, refSkill) == 0) {
                    _offerCandidateTeamIndexBuffer[d5] = (uint8_t)d4;
                    d5++;
                }
            }
            a0 += 0x2AC;
            d4++;
        }
        teamCount = d5;
    } else {
        /* Foreign market path */
        _offerSearchRetryCount++;
        uint8_t *a0 = &_tmdFileBuffer;
        teamsBase = a0;
        uint16_t d5 = 0;
        uint16_t d4 = 0;
        uint16_t numCareerTeams = _careerFileBuffer;
        /* MSB of selTeamNumber = country byte (see same fix in home-market
         * branch above). The foreign path wants country != candidate, so the
         * pre-fix LE byte read mostly accepted anything (ordinal byte rarely
         * coincides with another country code) and the foreign offer rate
         * was only "broken into mostly working", but it's still wrong. */
        uint8_t selTeamByte = (uint8_t)(_selTeamNumber >> 8);
        while (d4 < numCareerTeams) {
            if (selTeamByte != *a0) {
                if (IsTeamEligibleForOfferByRefSkill((TeamFileEx *)a0, refSkill) == 0) {
                    _offerCandidateTeamIndexBuffer[d5] = (uint8_t)d4;
                    d5++;
                }
            }
            a0 += 0x2AC;
            d4++;
        }
        teamCount = d5;
    }

    if (teamCount == 0) {
        SWOS_AUTOSKIP_LOG("[bug2]   CreateOffer playerOrd=%u: BAILED — no eligible buyer team "
                          "(numSelectedTeams=%u refSkill=%u home/foreign rnd=%u)",
                          (unsigned)playerOrdinal,
                          (unsigned)_numSelectedTeams,
                          (unsigned)refSkill,
                          (unsigned)rnd);
        return;
    }

    /* Pick a random team from candidates */
    rnd = Rand2();
    uint16_t idx = (uint16_t)((uint16_t)(rnd * teamCount) >> 8);
    uint8_t teamIdx = _offerCandidateTeamIndexBuffer[idx];
    uint8_t *teamBlock = teamsBase + (uint32_t)teamIdx * 0x2AC;

    /* Check uniqueness. teamBlock[0..1] = {countryNumber, teamOrdinal}
     * byte pair. offerOrdinals.teamOffering / IsOfferTeamPlayerPairUnique expect a
     * BE-packed team id, so read via PACK_BE_U16 — *(uint16_t *)teamBlock
     * gives the swapped (ordinal<<8|country) value on LE and the
     * downstream offer record is then stored with the wrong country
     * byte at offset 0. */
    uint16_t offTeamId = PACK_BE_U16(teamBlock[0], teamBlock[1]);
    if (IsOfferTeamPlayerPairUnique(offTeamId, _offerOrdinals.offerPrimaryPlayerOrdinal) == 0) {
        SWOS_AUTOSKIP_LOG("[bug2]   CreateOffer playerOrd=%u: BAILED — duplicate offer "
                          "(team=0x%04X already has offer)",
                          (unsigned)playerOrdinal, (unsigned)offTeamId);
        return;
    }

    _offerOrdinals.teamOffering = offTeamId;
    _offerExtraPositionMask = 0xFFFF;
    _offerOrdinals.numExtraPlayersWanted = -1;

    /* Try to add an extra player wanted */
    uint16_t swapSlots = GetRemainingOfferSwapSlots();
    /* swapSlots is in return value; the asm checks d2 for slot count */
    /* Actually GetRemainingOfferSwapSlots returns the slot count as a uint16_t
       but the asm bridge also sets d2. Let me re-check. */

    /* The asm checks d2 >= 2. The C GetRemainingOfferSwapSlots returns the count.
       But looking at the bridge, it returns with d0 = result (Z flag from beq).
       Actually the asm code at 63521 does:
         bsr.w GetRemainingOfferSwapSlots
         cmpi.w #2,d2
       So d2 is set by GetRemainingOfferSwapSlots. Let me check the bridge... */

    /* GetRemainingOfferSwapSlots returns the remaining count.
       But the asm bridge sets d2 = remaining count AND sets Z flag for ==0.
       Since I'm calling the C function directly, I get the count back. */
    if (swapSlots >= 2) {
        rnd = Rand2();
        if (rnd < 0x19) {
            int16_t extraOrd = PickRandomEligibleExtraPlayerOrdinal(refSkill);
            _offerOrdinals.numExtraPlayersWanted = extraOrd;
            if (extraOrd >= 0) {
                /* Check uniqueness for extra player */
                if (IsOfferTeamPlayerPairUnique(offTeamId, (uint16_t)extraOrd) == 0) {
                    _offerOrdinals.numExtraPlayersWanted = -1;
                } else {
                    /* Add extra player's value */
                    uint16_t extraVal = GetAvgTeamValueBaseForCurrentPlayer(_selTeamNumber, (uint16_t)extraOrd);
                    _offerValueBaseSum += extraVal;

                    /* Get extra player entry and check injury + position */
                    uint8_t *extraEntry = GetPlayer(_selTeamNumber, (uint16_t)extraOrd);
                    PlayerFile *extraPf = (PlayerFile *)(extraEntry + PF_OFF);
                    uint16_t extraPos = (uint16_t)extraPf->positionAndFace & PF_POSITION_MASK;
                    _offerExtraPositionMask = extraPos;
                    if (IsPlayerInjured((PlayerFile *)extraEntry, -1) != 0)
                        return;
                }
            }
        }
    }

    /* Calculate target value (random 85-119% of value sum).
     * ASM uses mulu.w (16×16→32) then divu.w (32÷16). Must use 32-bit
     * intermediate to avoid overflow for players valued > £650K. */
    rnd = Rand2();
    uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x23) >> 8) + 0x55;
    uint16_t targetVal = (uint16_t)((uint32_t)pct * (uint32_t)_offerValueBaseSum / 100);
    _offerTargetValueBase = targetVal;

    /* Initialize swap player fields */
    _offerSwapValueBaseSum = 0;
    _offerOrdinals.offerSwapPlayerOrdinal = -1;
    _offerOrdinals.offerExtraPlayerOrdinal = -1;

    /* Try to add a swap player */
    uint16_t poolSlots = GetRemainingTransferPoolSlots();
    if (poolSlots != 0) {
        rnd = Rand2();
        if (rnd < 0x19) {
            int16_t swapOrd = PickRandomEligibleSwapPlayerOrdinalFromTeam(
                (TeamFileEx *)teamBlock, -1);
            if (swapOrd >= 0) {
                uint16_t swapVal = GetAvgTeamValueBaseForCurrentPlayer(_offerOrdinals.teamOffering, (uint16_t)swapOrd);
                if (swapVal <= _offerTargetValueBase) {
                    _offerSwapValueBaseSum = swapVal;
                    _offerOrdinals.offerSwapPlayerOrdinal = swapOrd;

                    /* Try to add an extra swap player */
                    poolSlots = GetRemainingTransferPoolSlots();
                    if (poolSlots >= 2) {
                        rnd = Rand2();
                        if (rnd < 0x19) {
                            int16_t extraSwapOrd = PickRandomEligibleSwapPlayerOrdinalFromTeam(
                                (TeamFileEx *)teamBlock, swapOrd);
                            if (extraSwapOrd >= 0) {
                                uint16_t extraSwapVal = GetAvgTeamValueBaseForCurrentPlayer(
                                    _offerOrdinals.teamOffering, (uint16_t)extraSwapOrd);
                                uint16_t combined = extraSwapVal + _offerSwapValueBaseSum;
                                if (combined <= _offerTargetValueBase) {
                                    _offerSwapValueBaseSum += extraSwapVal;
                                    _offerOrdinals.offerExtraPlayerOrdinal = extraSwapOrd;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* Calculate offer amount = targetValue - swapValue, with 15% floor */
    uint16_t cashOffer = _offerTargetValueBase - _offerSwapValueBaseSum;
    uint16_t floor15pct = (uint16_t)((uint16_t)(_offerTargetValueBase * 15) / 100);
    if (cashOffer < floor15pct)
        cashOffer = 0;
    cashOffer = SnapToNearestAvgTeamValueBase(cashOffer);
    _offerOrdinals.offerAmount = cashOffer;

    /* Generate negotiation steps */
    rnd = Rand2();
    _offerOrdinals.offerNegotiationStep1Amount = 0;
    _offerNegotiationStep2Amount = 0;

    if (rnd & 1) {
        /* Step 1: random 105-112% of offer amount */
        uint16_t step1pct = (uint16_t)((uint16_t)(rnd * 0x14) >> 8) + 0x69;
        uint16_t step1 = (uint16_t)((uint32_t)step1pct * (uint32_t)_offerOrdinals.offerAmount / 100);
        step1 = SnapToNearestAvgTeamValueBase(step1);
        if (step1 != _offerOrdinals.offerAmount) {
            _offerOrdinals.offerNegotiationStep1Amount = step1;

            rnd = Rand2();
            if (rnd & 1) {
                /* Step 2: random 105-112% of step1 */
                uint16_t step2pct = (uint16_t)((uint16_t)(rnd * 0x14) >> 8) + 0x69;
                uint16_t step2 = (uint16_t)((uint32_t)step2pct * (uint32_t)_offerOrdinals.offerNegotiationStep1Amount / 100);
                step2 = SnapToNearestAvgTeamValueBase(step2);
                if (step2 != _offerOrdinals.offerNegotiationStep1Amount) {
                    _offerNegotiationStep2Amount = step2;
                }
            }
        }
    }

    /* Check roster size constraint */
    int16_t rosterDelta = -1; /* start at 0xFFFF = -1 (will add primary player) */
    if (_offerOrdinals.numExtraPlayersWanted >= 0)
        rosterDelta--;  /* losing an extra player */
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        rosterDelta++;  /* gaining a swap player */
        if (_offerOrdinals.offerExtraPlayerOrdinal >= 0)
            rosterDelta++;  /* gaining extra swap player */
    }
    int16_t newTotal = rosterDelta + (int16_t)_careerNumPlayers;
    if (newTotal > 30) {
        SWOS_AUTOSKIP_LOG("[bug2]   CreateOffer playerOrd=%u: BAILED — roster overflow "
                          "(newTotal=%d careerNumPlayers=%u)",
                          (unsigned)playerOrdinal,
                          (int)newTotal, (unsigned)_careerNumPlayers);
        return;
    }

    /* Write offer to table */
    uint16_t offerIdx = _numOffers;
    TransferOfferEntry *entry = &((TransferOfferEntry *)_transferOfferTable)[offerIdx];
    entry->offerAmount            = _offerOrdinals.offerAmount;
    entry->negotiationStep1Amount = (int16_t)_offerOrdinals.offerNegotiationStep1Amount;
    entry->negotiationStep2Amount = (int16_t)_offerNegotiationStep2Amount;
    entry->teamOffering           = _offerOrdinals.teamOffering;
    entry->primaryPlayerOrdinal   = (int8_t)_offerOrdinals.offerPrimaryPlayerOrdinal;
    entry->secondPlayerOrdinal    = (int8_t)_offerOrdinals.numExtraPlayersWanted;
    entry->swapPlayerOrdinal      = (int8_t)_offerOrdinals.offerSwapPlayerOrdinal;
    entry->extraPlayerOrdinal     = (int8_t)_offerOrdinals.offerExtraPlayerOrdinal;

    /* Random lifespan from table */
    rnd = Rand2();
    entry->lifespan = _offerLifespanTable[rnd & 0xF];
    entry->seenFlag = 0;

    _numOffers++;
    SWOS_AUTOSKIP_LOG("[bug2]   CreateOffer playerOrd=%u: STORED — team=0x%04X amount=%u numOffers=%u",
                      (unsigned)_offerOrdinals.offerPrimaryPlayerOrdinal,
                      (unsigned)_offerOrdinals.teamOffering, (unsigned)_offerOrdinals.offerAmount,
                      (unsigned)_numOffers);
}

/* ================================================================
 * EvaluateTransferOfferNegotiation  (SWOS2.s line 63754)
 *
 * Evaluates a transfer offer negotiation. Checks roster constraints,
 * randomizes player valuation, evaluates if the offering team will
 * accept, and shows the player transfer menu.
 *
 * Asm register convention:
 *   in:  globals (offerOrdinals.teamOffering, offerOrdinals.offerPrimaryPlayerOrdinal, etc.)
 *   out: d0.w = transferPlayerMenuResult
 * ================================================================ */
extern uint8_t  _playerTransferMenu[];

__attribute__((used, externally_visible))
int16_t EvaluateTransferOfferNegotiation(void)
{
    /* Check roster size constraint */
    int16_t rosterDelta = 1;  /* gaining the primary player */
    if (_offerOrdinals.numExtraPlayersWanted >= 0)
        rosterDelta++;  /* losing an extra player → gaining one more */
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0)
        rosterDelta--;  /* giving away swap player */
    if (_offerOrdinals.offerExtraPlayerOrdinal >= 0)
        rosterDelta--;  /* giving away extra swap player */

    int16_t newTotal = rosterDelta + (int16_t)_careerNumPlayers;
    if ((uint16_t)newTotal > 30) {
        _currentMessage = 2;
        ShowChairmanMessage();
        return 0;
    }

    /* Save random state, seed with player-specific random */
    SaveRand2State();

    uint8_t *playerEntry = GetPlayer(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    uint16_t playerRand = GetPlayerRandomNum((PlayerFile *)playerEntry);
    Randomize2(playerRand);

    uint16_t d2 = Rand2();  /* save for later XOR */

    /* Generate random divisor: Rand2 * 16 / 256 + 1, then 96 / that */
    uint16_t rnd = Rand2();
    uint16_t divisor = (uint16_t)((uint16_t)(rnd * 0x10) >> 8) + 1;
    uint16_t d1val = 0x60 / divisor;

    /* XOR with viewRandomPhase mod d1val, then re-randomize */
    uint16_t phase = (uint16_t)(uint8_t)_viewRandomPhase;
    uint16_t phasemod = (d1val != 0) ? (phase % d1val) : 0;
    uint16_t _seed2 = phasemod ^ d2;
    Randomize2(_seed2);

    /* Check if swap players can be accepted */
    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        uint16_t slots = GetRemainingOfferSwapSlots();
        if (slots == 0)
            goto reject;
        if (_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            slots = GetRemainingOfferSwapSlots();
            if (slots < 2)
                goto reject;
        }
    }

    /* Random values for swap/extra player valuation */
    Rand2();  /* discard one */
    Rand2();
    _swapPlayerValueRandom = Rand2();  /* actually: rnd→discard, rnd→swap, rnd→extra */
    /* Wait, let me re-read the asm more carefully:
       jsr Rand2          ; discard
       jsr Rand2
       move.w d0,(swapPlayerValueRandom)
       jsr Rand2
       move.w d0,(extraPlayerValueRandom)
       Actually no — the first Rand2 result is discarded, second goes to swapPlayerValueRandom,
       third goes to extraPlayerValueRandom. But looking at lines 63804-63808:
       jsr Rand2               ; d0 = rnd1 (discarded)
       jsr Rand2               ; d0 = rnd2
       move.w d0,(swapPlayerValueRandom)  ; swapPlayerValueRandom = rnd2
       jsr Rand2               ; d0 = rnd3
       move.w d0,(extraPlayerValueRandom) ; extraPlayerValueRandom = rnd3
    */
    /* Let me re-do this properly */
    {
        Rand2();  /* line 63804 - result discarded */
        _swapPlayerValueRandom = Rand2();  /* line 63805-63806 */
        _extraPlayerValueRandom = Rand2(); /* line 63807-63808 */
    }

    /* Get primary player demand value */
    uint16_t demandValue = GetAvgTeamValueBaseForCurrentPlayer(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
    uint16_t d7demand = demandValue;

    /* Add extra player wanted demand */
    if (_offerOrdinals.numExtraPlayersWanted >= 0) {
        uint16_t extraDemand = GetAvgTeamValueBaseForCurrentPlayer(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
        d7demand += extraDemand;
    }

    /* Calculate total offer value (cash + swap players) */
    uint16_t totalOffer = _offerOrdinals.offerAmount;

    if (_offerOrdinals.offerSwapPlayerOrdinal >= 0) {
        /* Swap player valuation with random multiplier */
        uint16_t swapVal = GetAvgTeamValueBaseForCurrentPlayer(_selTeamNumber, (uint16_t)_offerOrdinals.offerSwapPlayerOrdinal);
        uint16_t swapRnd = _swapPlayerValueRandom;
        uint16_t swapPct = (uint16_t)((uint16_t)(swapRnd * 0x4B) >> 8) + 0x32;
        uint16_t adjustedSwap = (uint16_t)(((uint32_t)swapPct * swapVal) / 100);
        adjustedSwap = SnapToNearestAvgTeamValueBase(adjustedSwap);
        totalOffer += adjustedSwap;

        if (_offerOrdinals.offerExtraPlayerOrdinal >= 0) {
            /* Extra swap player valuation */
            uint16_t extraSwapVal = GetAvgTeamValueBaseForCurrentPlayer(_selTeamNumber, (uint16_t)_offerOrdinals.offerExtraPlayerOrdinal);
            uint16_t extraRnd = _extraPlayerValueRandom;
            uint16_t extraPct = (uint16_t)((uint16_t)(extraRnd * 0x32) >> 8) + 0x32;
            uint16_t adjustedExtra = (uint16_t)(((uint32_t)extraPct * extraSwapVal) / 100);
            adjustedExtra = SnapToNearestAvgTeamValueBase(adjustedExtra);
            totalOffer += adjustedExtra;
        }
    }

    /* Determine selling team's asking price (d5).
     *
     * NOTE on the percentage maths throughout this routine: the product is
     * computed in 32 BITS, then divided. That is what the 68k does --
     * `mulu.w` produces a 32-bit result and `divu.w` divides a 32-bit
     * dividend by a 16-bit divisor -- and it matters here because the values
     * are large. Truncating the product to 16 bits first (as this code used
     * to) wraps for any player worth more than ~218 value units once the
     * random percentage reaches 300%, which COLLAPSES the asking price of an
     * expensive player to a small number and lets a derisory swap deal
     * through. Reported as "buying expensive top players by offering shitty
     * players in an exchange deal" -- Newcastle accepting $1M + Edgar Davids
     * ($1.8M) for Alan Shearer ($16M). Keep these in 32-bit. */
    uint16_t askingPrice;

    /* Check if offering team is from a different division than career team.
     * Both sides need the country byte (MSB). Pre-fix the right-hand side
     * read LE byte[0] = ordinal, so the cross-division check fired for
     * any team whose country byte differed from the user's ordinal — i.e.
     * almost every offer routed through the "different division" branch. */
    uint16_t offTeamHigh = _offerOrdinals.teamOffering >> 8;
    uint8_t selTeamByte = (uint8_t)(_selTeamNumber >> 8);
    if (offTeamHigh != selTeamByte) {
        /* Different division — check average player price */
        uint16_t avgPrice = GetAveragePlayerPrice(&_careerTeam);
        uint16_t threshold = (uint16_t)((uint16_t)(avgPrice * 0x8C) / 100);

        /* Get primary player's skill and check against threshold */
        uint8_t *primEntry = GetPlayer(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
        PlayerFile *primPf = (PlayerFile *)(primEntry + PF_OFF);
        if (threshold >= (uint8_t)primPf->price) {
            /* Primary player below threshold, check extra */
            if (_offerOrdinals.numExtraPlayersWanted >= 0) {
                uint8_t *extraEntry = GetPlayer(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
                PlayerFile *extraPfEval = (PlayerFile *)(extraEntry + PF_OFF);
                if (threshold < (uint8_t)extraPfEval->price) {
                    goto highDemand;
                }
            }
            /* Player(s) below threshold — go to market check */
            goto checkMarket;
        }
    highDemand:
        /* High-value player: 100-149% of demand */
        rnd = Rand2();
        uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x32) >> 8) + 0x64;
        askingPrice = (uint16_t)(((uint32_t)pct * d7demand) / 100);
        /* nullsub_14 — no-op */
        goto compareOffer;
    }

checkMarket:
    /* Check if player(s) are on the transfer market */
    {
        int16_t primOnMarket = (FindPlayerInCareerOrForeignMarket(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal) == 0);
        int16_t allOnMarket = 0;

        if (primOnMarket) {
            if (_offerOrdinals.numExtraPlayersWanted < 0) {
                allOnMarket = 1;  /* only primary, and it's on market */
            } else {
                int16_t extraOnMarket = (FindPlayerInCareerOrForeignMarket(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted) == 0);
                if (extraOnMarket)
                    allOnMarket = 1;  /* both on market */
                /* else: primary on market, extra not → check transfer list */
            }
        }
        /* else: primary not on market → check transfer list */

        if (allOnMarket) {
            /* All requested players are on market → low demand (87-101%) */
            rnd = Rand2();
            uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x26) >> 8) + 0x57;
            askingPrice = (uint16_t)(((uint32_t)pct * d7demand) / 100);
            goto compareOffer;
        }

        /* At least one player NOT on market — check transfer list flag */
        {
            uint8_t *prim2 = GetPlayer(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
            PlayerFile *primPf2 = (PlayerFile *)(prim2 + PF_OFF);
            if (primPf2->cardsInjuries & 0x04)
                goto onTransferList;

            if (_offerOrdinals.numExtraPlayersWanted >= 0) {
                uint8_t *extra2 = GetPlayer(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
                PlayerFile *extraPf3 = (PlayerFile *)(extra2 + PF_OFF);
                if (extraPf3->cardsInjuries & 0x04)
                    goto onTransferList;
            }

            /* Not on transfer list — chance of low/high demand */
            rnd = Rand2();
            if (rnd >= 0x99) {
                /* Rare low demand: 95-133% */
                rnd = Rand2();
                uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x28) >> 8) + 0x5F;
                askingPrice = (uint16_t)(((uint32_t)pct * d7demand) / 100);
                goto compareOffer;
            }

            /* Normal demand: 150-249% */
            rnd = Rand2();
            uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x64) >> 8) + 0x96;
            askingPrice = (uint16_t)(((uint32_t)pct * d7demand) / 100);
            goto compareOffer;

        onTransferList:
            /* On transfer list: 150-299% */
            rnd = Rand2();
            {
                uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x96) >> 8) + 0x96;
                askingPrice = (uint16_t)(((uint32_t)pct * d7demand) / 100);
            }
            goto compareOffer;
        }
    }

compareOffer:
    if (totalOffer >= askingPrice) {
        /* Offer meets asking price — willing to sell */
        _teamWillingToSellOrExchange = 0;
    } else {
        /* Check if close enough (within 20%) and negotiation attempts <= 3 */
        if (_offerNegotiationAttemptCount > 3)
            goto reject;

        uint16_t diff = askingPrice - totalOffer;
        uint16_t diffPct = (uint16_t)(((uint32_t)diff * 100) / askingPrice);
        if (diffPct >= 20)
            goto reject;

        /* Close enough — counter-offer */
        rnd = Rand2();
        uint16_t counterPct = (uint16_t)((uint16_t)(rnd * 0xA) >> 8) + 0x64;
        uint16_t counterVal = (uint16_t)(((uint32_t)counterPct * askingPrice) / 100);
        counterVal = SnapUpToAvgTeamValueBase(counterVal);
        int16_t counterDiff = (int16_t)(counterVal - totalOffer);
        counterDiff = -counterDiff;
        _teamWillingToSellOrExchange = counterDiff;
    }
    goto showMenu;

reject:
    /* Rejection — determine reason */
    {
        int16_t primFound = FindPlayerInCareerOrForeignMarket(_offerOrdinals.teamOffering, _offerOrdinals.offerPrimaryPlayerOrdinal);
        if (primFound == 0) {
            /* Primary not on market */
            if (_offerOrdinals.numExtraPlayersWanted >= 0) {
                int16_t extraFound = FindPlayerInCareerOrForeignMarket(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
                if (extraFound == 0) {
                    /* Neither on market → willing=4 */
                    _teamWillingToSellOrExchange = 4;
                    RemoveNegotiatedOfferPlayersFromMarkets();
                    goto showMenu;
                }
                /* Primary not on market, extra IS on market → willing=4 */
                _teamWillingToSellOrExchange = 4;
                RemoveNegotiatedOfferPlayersFromMarkets();
                goto showMenu;
            }
            /* Primary not on market, no extra → willing=3 */
            _teamWillingToSellOrExchange = 3;
            RemoveNegotiatedOfferPlayersFromMarkets();
            goto showMenu;
        }
        /* Primary IS on market */
        if (_offerOrdinals.numExtraPlayersWanted >= 0) {
            int16_t extraFound = FindPlayerInCareerOrForeignMarket(_offerOrdinals.teamOffering, (uint16_t)_offerOrdinals.numExtraPlayersWanted);
            if (extraFound == 0) {
                /* Extra not on market → willing=4 */
                _teamWillingToSellOrExchange = 4;
                RemoveNegotiatedOfferPlayersFromMarkets();
                goto showMenu;
            }
            /* Both on market but rejected → willing=2 */
            _teamWillingToSellOrExchange = 2;
            goto showMenu;
        }
        /* Primary on market, no extra → willing=1 */
        _teamWillingToSellOrExchange = 1;
    }

showMenu:
    RestoreRand2State();
    ShowMenu(_playerTransferMenu);
    return (int16_t)_transferPlayerMenuResult;
}

/* ================================================================
 * ApplyOfferDemandMultipliersForSwapAndExtraPlayers  (SWOS2.s line 64031)
 *
 * Adjusts the demand value (d5) by random multipliers based on
 * whether swap and extra players are part of the offer.
 * Dead code — never called from anywhere.
 *
 * Asm register convention:
 *   in:  d5 = demand value
 *   out: d5 = adjusted demand value
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t ApplyOfferDemandMultipliersForSwapAndExtraPlayers(uint16_t demandValue)
{
    if (_offerOrdinals.offerSwapPlayerOrdinal < 0)
        return demandValue;

    /* Swap player: random 100-139% */
    uint16_t rnd = Rand2();
    uint16_t pct = (uint16_t)((uint16_t)(rnd * 0x64) >> 8) + 0x64;
    demandValue = (uint16_t)((uint16_t)(pct * demandValue) / 100);

    if (_offerOrdinals.offerExtraPlayerOrdinal < 0)
        return demandValue;

    /* Extra player: random 100-149% */
    rnd = Rand2();
    pct = (uint16_t)((uint16_t)(rnd * 0x32) >> 8) + 0x64;
    demandValue = (uint16_t)((uint16_t)(pct * demandValue) / 100);

    return demandValue;
}

