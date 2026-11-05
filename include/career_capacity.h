#ifndef CAREER_CAPACITY_H
#define CAREER_CAPACITY_H

/* The career's list capacities (docs/mods/CAREER_PLUS.md §3.2 on benefactorr's Career+ branch).
 *
 * Each list has the ORIGINAL's size (CAREER_ORIG_*, SWOS2.s) and a compiled maximum
 * (CAREER_MAX_*): the array is sized to the maximum, and a career param (career_params.h,
 * default = the original) says how much of it a career uses. Every bound reads the PARAM.
 *
 * The bigger maxima are the SDL build's only: the Amiga's RAM is tight, so its arrays stay the
 * original's and its params cannot rise above them (career_params.json is SDL-only anyway).
 *
 * The save writes what a list holds, not its capacity: a list's tail past the original's size
 * is written only when something is in it, so a default career's save is byte for byte as before
 * (save_format_json.c, emitted_len / the pool and job-offer emitters). */

#define CAREER_ORIG_FOREIGN_POOL     432u   /* _careerForeignMarketPlayers: MarketPlayer[432] */
#define CAREER_ORIG_FOREIGN_LISTING  15u    /* _foreignMarketPlayers: 15 x 4 B = 60 B */
#define CAREER_ORIG_TRANSFER_LIST    5u     /* _playersOnTransferMarket: your players listed */
#define CAREER_ORIG_JOB_OFFERS       5u     /* _newJobOffers + _jobOffers together (JobOffer[5] each) */

#ifdef SWOS_SDL_BUILD
#define CAREER_MAX_FOREIGN_POOL      50000u /* < 65536: the count is a uint16_t */
#define CAREER_MAX_FOREIGN_LISTING   45u
#define CAREER_MAX_TRANSFER_LIST     10u
#define CAREER_MAX_JOB_OFFERS        20u
#else
#define CAREER_MAX_FOREIGN_POOL      CAREER_ORIG_FOREIGN_POOL
#define CAREER_MAX_FOREIGN_LISTING   CAREER_ORIG_FOREIGN_LISTING
#define CAREER_MAX_TRANSFER_LIST     CAREER_ORIG_TRANSFER_LIST
#define CAREER_MAX_JOB_OFFERS        CAREER_ORIG_JOB_OFFERS
#endif

#define CAREER_MARKET_ENTRY_SIZE     4u     /* a market listing slot: word team, byte ordinal, byte pad */

#endif /* CAREER_CAPACITY_H */
