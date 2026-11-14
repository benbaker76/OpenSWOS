/*
 * squad_extras.h -- a world's squads beyond 16 in the career (src/squad_extras.c; docs/WORLD_2026_27.md,
 * "Squads of 30 and the B-team call-ups"): the SWOS 96/97 Mod's tools/xtra.py (4.0.2), xbrowse.py and
 * xleft.py (4.0.6) -- Davide Lorigliola's, MIT.
 *
 * A world's team files carry each club's players 17..30 (the Mod's XTW5.nnn; our import's
 * "extra_players": DataJson_LoadTeamExtras). With them:
 *   - the club you take over (a new career, a new job) starts with them in its places 17.. --
 *     the club's real players -- instead of the game's 10 invented reserves and trial players
 *     (InitializeNewSeason -> TrainingMatch_AddTrialPlayersForNewSeason); paid like the game's
 *     reserves (66 %) while they are the club's, but normal players (their form, they never leave);
 *   - the national market offers other clubs' players 17.. as well (AddCareerTeamPlayer: 16 +
 *     the club's extras equally likely), a club's second page (BUY OTHER PLAYERS: NEXT PAGE) lists
 *     them, and one bought joins you with no replacement for his club;
 *   - a player 17.. who leaves his club -- bought from it, or leaving the managed club, or left in
 *     its record at a job change -- is listed, so he is never offered or brought back again
 *     (the Mod's lists XS and XL: "squad_extras" in the career save).
 *
 * Computer clubs still play with their 16. World careers only (never 96/97), SDL only: the
 * Amiga's team files hold 16 and it reads no JSON, so there none of this happens.
 *
 * Place numbering: a club's player `index` 0..15 is its record's, 16.. its extras' (the 17th
 * player = index 16), as the game's GetPlayer takes them.
 */
#ifndef SQUAD_EXTRAS_H
#define SQUAD_EXTRAS_H

#include "swos_types.h"
#include "game_types.h"

#define SQX_MAX       14                /* extras a club: 16 + 14 = the game's squad of 30 */
#define SQX_LEFT_MAX  256               /* the departed list (the Mod's XS 128 + XL; oldest dropped) */

typedef struct {
    uint16_t team;                      /* the club, as the game packs it (file << 8 | ordinal) */
    uint8_t  index;                     /* 16.. its place - 1 */
    int8_t   name[23];                  /* (his name: the place is checked against it) */
} SqxLeft;

typedef struct {
    uint16_t n;                         /* entries in left[] */
    uint16_t ring;                      /* the next to overwrite once full */
    SqxLeft  left[SQX_LEFT_MAX];
} SqxState;
extern SqxState g_sqx;

int32_t SquadExtras_On(void);           /* a world career, not a national team's */
void    SquadExtras_Reset(void);        /* a new career, or a save without the item: none left */
void    SquadExtras_Changed(void);      /* (the list loaded: what the market sees is read again) */

/* The managed club just loaded from its record (Career_InitializeSelectedTeamAndStartingBalance):
 * its extras into places 17.. */
void    SquadExtras_ClubLoaded(void);
/* InitializeNewSeason's new-club reserves: 1 when the club's extras took their place (once). */
int32_t SquadExtras_TakeAdded(void);
/* A player of the managed club's own extras (by name): paid like a reserve. */
int32_t SquadExtras_IsOwnExtra(const PlayerFile *p);

/* Another club's extras: how many it has (gone ones counted), the one at `index` (16..) or NULL
 * when gone, GetPlayer's view of him (the player - 76, as GetPlayer returns a record's), or NULL
 * when the squads are not on. */
int32_t     SquadExtras_Count(uint16_t team);
PlayerFile *SquadExtras_Player(uint16_t team, uint16_t index);
uint8_t    *SquadExtras_GetPlayer(uint16_t team, uint16_t index);
/* The n-th extra still there (page 2 of a club): its index (16..), or -1. */
int32_t     SquadExtras_Nth(uint16_t team, int32_t n);

/* A long bench (long_bench.c): the n-th of a club's players 17.. it brings to a match, in any
 * match of a world (a friendly's, a DIY's, a career's): in a career the ones gone are left out;
 * `index` his place - 1 (16..). And his record by that index (the after-match write-backs land in
 * the club's cached block only, as the Mod's). NULL: none. */
PlayerFile *SquadExtras_BenchPlayer(uint16_t team, int32_t n, uint16_t *index);
PlayerFile *SquadExtras_BenchRecord(uint16_t team, uint16_t index);
/* The checksum of a club's players 17.. as its team file has them, in order (the team check's
 * TEAMCHECK_PROTOCOL_EXTRAS); 0: the club has none. */
int32_t     SquadExtras_Checksum(uint16_t team, uint8_t out[8]);

/* AddCareerTeamPlayer's pick for a club with extras: 16 + extras equally likely (one more
 * Rand2, as the Mod); the game's own pick stays for 0..15 or a gone one. */
uint16_t SquadExtras_MarketPick(uint16_t team, uint16_t pick);

/* Departures. */
void SquadExtras_Left(uint16_t team, uint16_t index, const int8_t *name);
void SquadExtras_OwnLeaving(uint16_t slot);     /* the managed club's player at file slot `slot` */
void SquadExtras_JobChange(void);               /* the first 16 written back into the club's record */
/* A purchase from another club (Career_RemovePlayerAndAddAsTrialist): 1 when the player was
 * one of its extras -- listed as gone; the game makes no replacement for him. */
int32_t SquadExtras_Bought(uint16_t team, uint16_t index, const int8_t *name);

/* --squad-extras-selftest=<file>,<ordinal>; SWOS_TEST_BUY_EXTRA (the golden careers' purchase) */
int32_t SquadExtras_SelfTest(int32_t file, int32_t ordinal);
void    SquadExtras_TestRequests(void);

#endif
