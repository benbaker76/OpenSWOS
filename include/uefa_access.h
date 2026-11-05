/*
 * uefa_access.h -- the UEFA qualifying rounds of a world with league phases (src/uefa_access.c;
 * docs/WORLD_2026_27.md, phase 5): the 2024-27 access list, its places' clubs, the qualifying
 * rounds simulated, the three 36-club league phases.
 *
 * The SWOS 96/97 Mod's tools/uefaq.c (MIT, Davide Lorigliola), its interface as it is: the
 * caller fills UQ_IN (the associations of the world by rank, each one's top 9 and cup winner,
 * the clubs' coefficients and strengths, the title holders), calls uq_season(), reads UQ_OUT;
 * with UQ_S set, the whole simulated bracket is kept there, the career club's qualifying ties
 * played one by one (uq_resolve), their coefficient points counted at the next season end
 * (uq_coef).
 */
#ifndef UEFA_ACCESS_H
#define UEFA_ACCESS_H

#include "swos_types.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

enum { CL_LS, CL_PO, CL_POnc, CL_Q3, CL_Q3nc, CL_Q2, CL_Q2nc, CL_Q1, EL_LS, EL_PO, EL_Q3, EL_Q3ch, EL_Q2, EL_Q1,
       CO_LS, CO_PO, CO_POch, CO_Q3, CO_Q3ch, CO_Q2, CO_Q2ch, CO_Q1, NR, OUT = 255, SPLIT = 254 };
#define COMP(r) ((r) < EL_LS ? 0 : (r) < CO_LS ? 1 : 2)
#define NA 56                           /* ranks 1..55 */
#define NL 10                           /* clubs per association: the top 9 of the league + the cup winner */
#define NC 600
#define NE 400

/* input (filled by the caller) */
struct uq_in {
    u32 seed;
    u8 present[NA];                     /* rank -> 1 if the world has that association */
    u8 eps[2];                          /* ranks of the 2 European Performance Spots (0 = none) */
    u16 hold[3];                        /* title holders CL, EL, UECL: club indices + 1 (0 = none) */
    u16 nclub;
    u16 league[NA][NL - 1];             /* club indices + 1, league order (0 = none) */
    u16 cup[NA];                        /* the cup winner: club index + 1 (0 = none) */
    struct { u16 id; u8 rank; u8 pad; u32 coef; u32 str; } club[NC];
    u16 manager;                        /* the manager's club: index + 1 (0 = none / not seen) */
    u8 force;                           /* test builds: the manager's club enters this round (0 = its place) */
    u8 pad3;
    u16 lcup[NA];                       /* the league cup winner of an association whose league cup gives its last
                                           UECL place (England's EFL Cup): club index + 1 (0 = none / not seen) */
    const u8 *eplay[NA];                /* its knockout play-offs (gplay.py's bytes, a 'europe' one gives the last
                                           UECL place: Denmark, Austria, Netherlands, Wales ...), 0 = none (the Mod
                                           keeps the address in a u32: a 32-bit build's only) */
};
/* output */
struct uq_out {
    u8 n[3];                            /* clubs in the three league phases (36 each) */
    u16 ls[3][40];                      /* club indices + 1 */
    u8 status[3];                       /* holders: 0 none, 1 'league', 2 'own' */
    u8 nent;                            /* places of the season's list (after the rebalancing) */
    u8 round_of[NC];                    /* the club's first round (NR = none) */
    u8 last[NC];                        /* the last round played (NR = none) */
    u8 mt_n;                            /* the manager's ties (in the simulation): round, opponent, won */
    u8 mt_round[6];
    u16 mt_opp[6];
    u8 mt_win[6];
    u16 standin;                        /* the club in the manager's league-phase place until its ties are played */
    u8 gn[3];                           /* the game's lists: the league phases without the holders that w_holders */
    u16 gl[3][40];                      /* appends (CL: CL / EL holders, EL: UECL holder), exactly 36 - those */
    u8 hkeep[3];                        /* 4.0.2: a holder already in a HIGHER league phase than the one w_holders
                                           would append it to (the UECL winner who won the league: Champions
                                           League): kept in that list of gl, and w_holders must not append it */
};

/* the season's qualifying state, kept in the save (the 80-record area, after uefacoef's block) while the career
   club has ties to play: the whole simulated bracket (team words), the game's lists as written, the stand-in */
#define NBR 320
struct uq_state {
    u16 mark;                           /* 'UQ' while valid */
    u16 m;                              /* the career club (team word) */
    u16 s;                              /* the stand-in holding its league-phase place (0 = none) */
    u8 sc;                              /* the stand-in's list (0 CL, 1 EL, 2 UECL) */
    u8 k;                               /* the career club's ties played */
    u8 res;                             /* their results: bit j = won tie j */
    u8 pad;
    u16 nbr;
    u8 gn[3];
    u8 pad2;
    u16 gl[3][36];                      /* the lists written at the season end */
    struct { u8 r, aw; u16 a, b; } br[NBR];     /* every tie in playing order: round, 1 = a went through */
};
/* uq_resolve(): from the state, the career club's next tie or (done) the final lists */
struct uq_res {
    u8 done;                            /* 1: no tie left (the lists below are final) */
    u8 r;                               /* the next tie: round, opponent */
    u16 opp;
    u8 dest;                            /* done: the club's league phase (0 CL, 1 EL, 2 UECL, 3 none) */
    u8 pad;
    u16 gl[3][36];
    u8 npot;                            /* uq_season: every club the career club may meet (records needed) */
    u8 pad2;
    u16 pot[16];
};

extern struct uq_in UQ_IN;
extern struct uq_out UQ_OUT;
extern struct uq_state *UQ_S;
extern struct uq_res UQ_R;
struct uq_cp { u16 team; u8 units, count; };
extern struct uq_cp UQ_CP[2 * NBR];
extern u32 UQ_NCP;
/* The world's association with a cup winner and no league (Liechtenstein): its league file
 * (country), 0 = none. */
extern u8 UQ_CUPONLY_FILE[4];

void uq_season(void);
void uq_resolve(void);
void uq_coef(void);

#endif
