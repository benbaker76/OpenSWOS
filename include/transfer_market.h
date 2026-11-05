/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef TRANSFER_MARKET_H
#define TRANSFER_MARKET_H

#include "game_types.h"
#include "menu_system.h"

/* ---- Transfer pool slot utilities ---- */
uint16_t GetRemainingTransferPoolSlots(void);
uint16_t GetRemainingOfferSwapSlots(void);

/* ---- Transfer offer management ---- */
void RemoveTransferOfferAtIndex(uint16_t offerIndex);
void PrunePlayerOffersAgainstRosterLimits(void);

/* ---- Home market (career contest) player management ---- */
int16_t IsPlayerInCareerContest(uint16_t teamNumber, uint16_t playerOrdinal);
uint16_t AddPlayerToCareerContest(uint16_t teamNumber, uint16_t playerOrdinal);
void RemovePlayerFromCareerContest(uint16_t teamNumber, uint16_t playerOrdinal);

/* ---- Foreign market player management ---- */
int16_t FindForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal);
uint16_t AddForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal);
void RemoveForeignMarketPlayer(uint16_t teamId, uint16_t playerOrdinal);

/* ---- Combined market search ---- */
uint16_t FindPlayerInCareerOrForeignMarket(uint16_t teamId, uint16_t playerOrdinal);

/* ---- Transfer offer cleanup ---- */
void RemovePlayerFromTransferOffers(uint16_t teamNumber, uint16_t playerOrdinal);

/* ---- Pool player database ---- */
void FillForeignMarketPlayersData(void);
void LoadPoolplyrDat(void);
PoolPlayer *FindPoolplyrDatPlayer(uint16_t teamId, uint16_t playerOrdinal);

/* ---- Market population ---- */
void AddCareerTeamPlayer(void);
void FillCareerTeamPlayers(void);
void AddRandomForeignMarketPlayer(void);
void FillForeignMarketPlayers(void);
void UpdateHomeMarketPlayers(void);
void UpdateForeignMarketPlayers(void);

/* ---- Transfer offer helpers (Chunk 74) ---- */
int16_t FindTeamIdInDiyTeamsList(uint16_t *teamIdPtr);
int16_t IsOfferTeamPlayerPairUnique(uint16_t teamId, uint16_t playerIdx);
int16_t IsTeamEligibleForOfferByRefSkill(TeamFileEx *team, uint16_t refSkill);
void RemoveOfferPlayerFromMarketsAndCount(uint16_t teamId, uint16_t playerIndex);
void RemoveNegotiatedOfferPlayersFromMarkets(void);
int16_t PickRandomEligibleExtraPlayerOrdinal(uint16_t refSkill);
int16_t PickRandomEligibleSwapPlayerOrdinalFromTeam(TeamFileEx *teamBlock, int16_t excludeSlot);

/* ---- Transfer market list (Chunk 75) ---- */
int16_t FindPlayerOnTransferMarket(uint16_t playerId);
void AddPlayerToTransferMarket(uint16_t playerId);
void RemovePlayerFromTransferMarket(uint16_t playerId);
int16_t CheckForTransferOffers(void);

/* ---- Career finance (Chunk 75) ---- */
void Career_CommitNewBalanceAndResetFinanceTotals(void);

/* ---- Career pool & foreign market patching (Chunk 75) ---- */
void AddCareerPoolPlayer(uint16_t teamNumber, uint16_t playerOrdinal, PlayerFile *playerFile);
void Career_PatchTeamWithForeignMarketPlayers(TeamFileEx *teamPtr);
void Career_ApplyForeignMarketPlayersToSelectedTeams(void);
void Career_ApplyPendingForeignMarketPlayersToSelectedTeams(void);

/* ---- DIY fixture & team removal (Chunk 76) ---- */
void DIY_AdvanceFixtureIndex(uint16_t lo, uint16_t hi);
void Career_RemovePlayerFromTeamInternal(uint16_t teamNumber, uint16_t playerOrd);

/* ---- Transfer/Offer menu callbacks (Chunk 113) ---- */
void TransferPlayerOnSelect10(void);
void TransferPlayerOnSelect11(void);
void TransferPlayerOnSelect12(void);
void AcceptOffer(void);
void RejectOffer(void);
void AskForMore(void);
void ConsiderOffer(void);
void ThirdPartyOfferMenuExit(void);

/* --- Transfer menu helpers (Chunk 117) --- */
void TransferMenuSetBankState(void);
void BuyPlayerInit(void);
void TransferMarketFinish(void);
void ShowDomesticPlayerMarket(void);
void ViewDomesticPlayerMarket(void);
void ShowForeignPlayerMarket(void);
void ViewForeignPlayerMarket(void);

/* --- Player offer menu helpers (Chunk 145) --- */
void PlayerOffersUpdateOfferAmountText(void);
void UpdatePlayerOfferMenuIndicators(void);
void PlayerOffersInit(void);

/* --- Offer expiry & random offers (Chunk 146) --- */
void GenerateRandomTransferOffers(void);
void UpdateAndExpireTransferOffers(void);
void ProcessOfferExpiryCountdown(void);

/* --- Buy Other Players menu (Chunk 147) --- */
void BuyOtherPlayersInit(void);
void BuyOtherPlayersFinish(void);

/* --- View Offers menu (Chunk 161) --- */
void ViewOffersMenu(void);

/* --- Transfer Market menu helpers (Chunk 162) --- */
void CheckNumPlayersOnTransferMarket(void);
void CheckIfPlayerSelected(void);
void EnableDisableOffers(void);
void TransferMarketInit(void);
void BuyPlayerMenu(void);
void GiveFreeTransfer(void);
void TakeOffMarketMenu(void);
void TransferMarketCommon(void *entry);

/* --- BuildTransferPlayerMenuEntry (Chunk 163) --- */
/* The player the next BuildTransferPlayerMenuEntry row shows (team id, his file slot, the row),
 * for the mods' playerRowDrawn; taken by that call. */
void TransferRow_Identity(uint16_t team, uint16_t slot, int16_t row);
void BuildTransferPlayerMenuEntry(void *item, uint8_t *player,
                                    uint16_t y, int16_t idx,
                                    uint16_t style, uint8_t *extra);
void TransferMarketAfterDrawCommon(void *entry);

/* --- ShowPlayerOfferMenuForPlayer + market helpers (Chunk 163 cont.) --- */
void ShowPlayerOfferMenuForPlayer(uint16_t teamNumber, uint16_t playerOrdinal);
void ShowPlayerMarketInit(void);
void ShowPlayerMarket(void);
void PlayerMarketUpdateFilterMenuEntryState(void);
void PlayerMarketUpdateMoreListLabel(void);
void PlayerMarketResetListState(void);
void BuyOtherPlayersCommon(void *entry);
void BuyOtherPlayersAfterDrawCommon(void *entry);

/* --- ShowSquad callers + OffersInit (Chunk 164) --- */
void ThirdPartyOfferViewSquad(void);
void BuyPlayerViewSquad(void);
void OffersInit(void);

/* --- AfterDrawCommon + OffersCommon + PlayerOffers callbacks (Chunk 165) --- */
void TransferPlayerAfterDrawCommon(void *entry);
void ThirdPartyOfferMenuAfterDrawCommon(void *entry);
void PlayerOffersAfterDrawCommon(void *entry);
void PlayerOffersOnSelect13(void);
void OffersCommon(void *entry);
void PlayerOffersOnSelect8(void);
void PlayerOffersOnSelect10(void);
void PlayerOffersOnSelect11(void);
void PlayerOffersOnSelect12(void);
void PlayerOffersAdjustOfferAmount(void);

/* --- TransferPlayerInit + ThirdPartyOfferMenuInit (Chunk 166) --- */
void TransferPlayerInit(void *entry);
void ThirdPartyOfferMenuInit(void *entry);

/* --- OffersAfterDrawCommon (Chunk 168) --- */
void OffersAfterDrawCommon(void *entryPtr);

/* --- Career Transfer Accept + Trialist System (Chunk 167) --- */
void Career_BuildForeignMarketPoolFromSelectedTeam(void);
void Career_TransferPlayerAndAddReplacedToMarketPool(uint16_t sourceTeam, uint16_t playerOrd);
void Career_AcceptOffer_SellPlayer(void);
void Career_AcceptOffer_BuyPlayer(void);
void Career_RemovePlayerAndAddAsTrialist(uint16_t sourceTeam, uint16_t playerOrd);
void Career_GenerateTrialistForTeamPlayer(uint16_t playerIndex);
void Career_ConvertPlayerToTrialistFromName(uint16_t sourceTeam, uint16_t playerOrd);
void TrainingMatch_AddTrialPlayerToSelectedTeam(uint16_t role);
void TrainingMatch_AdjustSelectedTeamRosterForTrialists(uint16_t excludeFlags, uint16_t baseCount, TeamFileEx *teamPtr);

/* --- PutPlayerOnMarketMenu (Chunk 177) --- */
void PutPlayerOnMarketMenu(void);

/* --- Chunk 202: BuyOtherDomesticPlayer + BuyOtherForeignPlayer --- */
void BuyOtherDomesticPlayer(void);
void BuyOtherForeignPlayer(void);

/* --- Chunk 207: PlayerMarketBuildVisiblePlayerList --- */
void PlayerMarketBuildVisiblePlayerList(void);

/* --- Player market menu callbacks --- */
void PlayerMarketFilterPositionOnSelect(MenuEntry *entry);
void PlayerMarketFilterAgeGroupOnSelect(MenuEntry *entry);
void PlayerMarketScrollOnSelect(MenuEntry *entry);
void PlayerMarketViewSquadOnSelect(MenuEntry *entry);
void PlayerMarketItemOnSelect(MenuEntry *entry);
void PlayerMarketItemBeforeDraw(MenuEntry *entry);

/* --- Chunk 215: CreateRandomTransferOfferForPlayer + EvaluateTransferOfferNegotiation --- */
void CreateRandomTransferOfferForPlayer(uint16_t playerOrdinal);
int16_t EvaluateTransferOfferNegotiation(void);
uint16_t ApplyOfferDemandMultipliersForSwapAndExtraPlayers(uint16_t demandValue);

#endif
