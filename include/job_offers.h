/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* job_offers.h — Job offer queue management. */
#pragma once
#include "game_types.h"
#include "menu_system.h"

/* --- Job offer search --- */
int16_t IsOldJobOffer(uint16_t teamNumber);
int16_t IsNewJobOffer(uint16_t teamNumber);

/* --- Job offer pointer lookup --- */
JobOffer *GetJobOfferPtr(uint16_t index);
JobOffer *GetNewJobOfferPtr(uint16_t index);

/* --- Job offer queue manipulation --- */
void AddJobOffer(uint16_t teamNumber, uint32_t initialFunds);
void RemoveJobOffer(uint16_t teamNumber);
void JobOffers_RemoveOffer(uint16_t teamNumber);
void JobOffers_MaybeExpireSomeNewOffers(void);
void JobOffers_FillMissingNewOfferTeamNames(void);
void JobOffers_RemoveExpiredOffers(void);
void JobOffers_ConvertNewOfferToOffer(uint16_t teamNumber, uint32_t initialFunds);

/* --- Job offer menu functions (Chunk 99) --- */
void GotJobOfferInit(void);
void GotJobOfferFinish(void);
void AcceptJobOffer(void);
void RejectJobOffer(void);
void ViewFutureTeam(void);
void ViewFutureTeamLeague(void);
void ConsiderJobOffer(void);

/* --- JobOffersInit, DrawOldOfferLine, AfterDrawCommon (Chunk 169) --- */
void JobOffersInit(void);
void JobOffers_DrawOldOfferLine(void *entryPtr);
void JobOffersAfterDrawCommon(void *entryPtr);

/* --- Job offers menu layout/exit (Chunk 116) --- */
void JobOffers_UpdateMenuEntryLayout(void);
void JobOffersFinish(void);
void JobOffersCommon(MenuEntry *entry);
void OldJobOffersCommon(MenuEntry *entry);
void JobOffersExit(void);

/* --- Career_MaybeGenerateJobOffer (Chunk 216) --- */
void Career_MaybeGenerateJobOffer(void);
