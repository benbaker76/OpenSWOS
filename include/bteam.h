/*
 * bteam.h -- the B-team call-ups of a world's career (src/bteam.c; docs/WORLD_2026_27.md,
 * "Squads of 30 and the B-team call-ups"): the SWOS 96/97 Mod 4.1's tools/bteam.py (4.0.6, Davide
 * Lorigliola's, MIT).
 *
 * A career club with a B team (its second team or under-23 side: the world's
 * competitions/bteams.json, from the Mod's DATA\BAGE.nnn -- 20 clubs in World 2026-27) calls up
 * its players from the squad screen's B TEAM button:
 *   - NEXT MATCH ONLY (kind 1): back to the B team at the calendar's next step (after the match);
 *     no fee, no wages. Under 23 by year of birth and goalkeepers always (where the country lets a
 *     keeper of any age play), an older player only up to the country's over-age cap (4 Italy,
 *     Spain, Portugal; 3 Germany; none in the Netherlands; Australia and New Zealand: none, and no
 *     keeper exception); at most 4 a match. A player without a year counts as under 23.
 *   - FOR THE SEASON (kind 2): in the country's transfer windows only, at most 3 a window; the
 *     game's wages; the B team gets the game's generated player in his place. Back at the season's
 *     end (or a job elsewhere), his season's form and value going with him.
 * A called-up player is on loan (PF_LOAN: his value reads LOAN, he cannot be sold or listed);
 * the squad holds 30 at most. Kept with the career ("b_team" in a world's save, the Mod's item BU).
 *
 * SDL builds with SWOS_WORLDS (the B teams' squads and years are the world's JSON).
 */
#ifndef BTEAM_H
#define BTEAM_H

#include "swos_types.h"
#include "game_types.h"

#define BTEAM_MAX       10              /* call-ups held: 4 for a match + 2 windows x 3 (the Mod's BU_MAX) */
#define BTEAM_MAX_MATCH 4
#define BTEAM_MAX_WINDOW 3

enum {                                  /* BTeam_Can / BTeam_Add (the Mod's bteam.CODES) */
    BTEAM_OK, BTEAM_MAX4_MATCH, BTEAM_OVER_AGE, BTEAM_WINDOW_CLOSED, BTEAM_SQUAD_FULL,
    BTEAM_MAX3_WINDOW, BTEAM_ALREADY, BTEAM_NO_BTEAM, BTEAM_NO_PLAYER, BTEAM_LIST_FULL
};

typedef struct {
    uint8_t  kind;                      /* bits 0-1: 1 next match / 2 season; bit 3: over-age;
                                           bits 4-5: a season call-up's window (1..) */
    uint8_t  index;                     /* his place in the B team: 0..15 its record, 16.. its extras */
    uint16_t team;                      /* the B team (file << 8 | ordinal) */
    int8_t   name[23];                  /* (his career squad slot is found by it) */
} BTeamCall;

typedef struct {
    uint8_t   n;
    BTeamCall call[BTEAM_MAX];
} BTeamState;
extern BTeamState g_bteam;

void    BTeam_Reset(void);              /* a new career, or a save without the item */
uint16_t BTeam_Of(uint16_t parent);     /* a club's B team, 0xFFFF none */
int32_t BTeam_Shown(void);              /* the squad screen's B TEAM button: the career club has one */
int32_t BTeam_Can(int32_t kind, uint16_t index);
int32_t BTeam_Add(int32_t kind, uint16_t index);
void    BTeam_ReturnMatch(void);        /* the calendar's step: the one-match call-ups go back */
void    BTeam_ReturnAll(void);          /* the season's end / a new job: all go back */
int32_t BTeam_Holds(uint16_t team, uint16_t index);     /* a call-up of that B team's player */
int32_t BTeam_CalledKind(const PlayerFile *p);          /* 1 / 2 for a called-up player, else 0 */
uint16_t BTeam_Born(uint16_t team, uint16_t index);     /* his year of birth, 0 unknown */
const char *BTeam_CodeText(int32_t code);

/* The squad screen (squadMenu): its B TEAM button (entry 64, BTeamSquadButton), which shows the
 * B squad in BUY OTHER PLAYERS' screen (transfer_market.c: page 2 its extras); a row picked there
 * opens the call-up page (a dialog). */
void     BTeam_SquadMenuInit(void);
int32_t  BTeam_Browsing(void);          /* that screen shows the B team */
uint16_t BTeam_BrowseTeam(void);
void     BTeam_PickPlayer(uint16_t index);

/* --bteam-selftest[=<parent team hex>]: the B teams, their players' years and what each call-up
 * would answer. */
int32_t BTeam_SelfTest(const char *arg);
/* SWOS_TEST_BTEAM="kind:index,..." -- the golden careers' call-ups, made at a hub of the career
 * club's first season (src/career_autopilot.c). */
void    BTeam_TestRequests(void);
void    BTeam_TestShowCalled(void);     /* SWOS_TEST_BTEAM_SHOWN: a call-up onto a drawn row */

#endif
