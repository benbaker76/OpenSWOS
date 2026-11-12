/*
 * world_conti.h -- the other confederations' club cups of a world career (src/world_conti.c;
 * the SWOS 96/97 Mod's tools/conti.c, MIT, Davide Lorigliola): its state and calls.
 *
 * The season end: ct_begin (its first country: this season's edition finished, its winners
 * the next holders), ct_table + ct_country per country (its top division's final table, its
 * top 9 and cup winner records), ct_go (the career club, a seed, the UEFA holder): the next
 * edition of every cup, its entrants kept (CT_ED, saved) and played through -- up to the
 * career club's next game (CT_SEG), which ct_result reports. CT_SPEC: the cups (the world's
 * data, scripts/world_conti.py).
 */
#ifndef WORLD_CONTI_H
#define WORLD_CONTI_H

#include "swos_types.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#define CT_NA 128                          /* associations (countries) of the confederations in the world */
#define CT_NL 32                           /* league clubs kept per association (the final table; J1 20, ARG 30) */
#define CT_NC 1024                         /* clubs seen + the guest clubs */
#define CT_NCUP 16
#define CT_NRD 8                           /* rounds per cup (the Libertadores: 8) */
#define CT_NE 64                           /* entrants of a round (CAF q1: 52) */
#define CT_RB 24                           /* bytes per round in the spec */
#define CT_NONE 0xFF
#define CT_CHAMP 0xFE

/* round kinds, draws, association rules (spec byte values) */
enum { K_KO, K_GROUPS };
enum { D_ORDER, D_POTS2, D_SAMEPAIRS, D_SEED12, D_BRACKET, D_GROUPS, D_CROSS, D_SLOT, D_PLACE };
enum { A_ANY, A_NO, A_NOVIA };
/* access codes: 0 champion, 1 cup winner, 2..10 the k-th place, NEXT the best club not yet in, AP / CL a split
   season's stage champions (else as NEXT) */
enum { C_NEXT = 0x40, C_AP = 0x41, C_CL = 0x42 };

struct ct_in {                          /* filled by the caller */
    u32 seed;
    u16 nclub;
    u16 manager;                        /* the career club: index + 1 (0 = none) */
    u16 league[CT_NA][CT_NL];                 /* club indices + 1, final table order (0 = none) */
    u16 cup[CT_NA];                        /* the national cup winner */
    u16 ap[CT_NA], cl[CT_NA];                 /* stage champions (Argentina) */
    struct { u16 id; u8 assoc; u8 pad; u32 str; u32 pts; } club[CT_NC];   /* team word, association, strength, ranking */
};
struct ct_state {                       /* kept in the save */
    u16 mark;                           /* 'CT' */
    u16 season;
    struct { u16 id; u8 assoc; u8 pad; u32 str; u32 pts; } hold[CT_NCUP];  /* each cup's holder (id 0 = none) */
};
struct ct_out {
    u8 ncup;
    u8 pad[3];
    u16 winner[CT_NCUP], second[CT_NCUP];     /* club indices + 1 */
    u8 last[CT_NC];                        /* the last round reached by each club: cup << 4 | round, CT_NONE none */
    u8 out_of[CT_NC];                      /* how it left that round: 0 lost / eliminated, 1 won the cup */
    u8 nent[CT_NCUP][CT_NRD];                 /* entrants per round (after its draw) */
    u16 ent[CT_NCUP][CT_NRD][CT_NE];             /* in draw order: ties (a, b) / groups of gsize */
    u8 res[CT_NCUP][CT_NRD][CT_NE];              /* ko: 1 = the first club of the tie went through; groups: final place */
};

#define CT_NED 440                         /* (the save's room: the item must leave 80 team records; 384 used with
                                           all six confederations, reserves included) */
struct ct_ed {
    u16 mark;                           /* 'CE' */
    u16 ne;                             /* entries */
    u32 seed;
    u16 mgr;                            /* the career club's team word (0 = none) */
    u8 nf, pad;
    struct { u8 c, r, v; } f[16];       /* the club's results: ko 1 won / 0 lost, groups the place */
    struct { u8 id0, id1, cr, s0, s1; } e[CT_NED];   /* an entrant: team word, cup << 4 | round (0xFF: a reserve
                                           club for group fills), strength / 16 (12 bits) | its slot << 12; the
                                           association from the team file (the guests' table), the ranking from
                                           the spec */
};
struct ct_seg {                         /* the career club's next game (CT_SEG.kind 0xFF: none) */
    u8 cup, round, kind, n;             /* kind: K_KO a tie (club[0] v club[1]), K_GROUPS the drawn groups */
    u8 legs, level, month, pos;         /* pos: the club's place in the list */
    u16 club[32];                       /* team words */
};
/* 4.0.6 option C (the Mod's ctgame.c): the goals of a replay, kept only while CT_GREC points at one (a
   view's own replay; NULL in the season end's, a load's and the daily's runs -- the same RNG calls either
   way). A played round's bytes from off[cup][round] - 1 (0: not kept): a knockout 3 per tie (the first leg,
   the second, extra time), a group stage its groups' matches in group()'s order. A byte: home goals + 1 |
   (away goals + 1) << 4; 0 no match, CT_GR_OWN a game the career club played itself. */
#define CT_GR_OWN 0xFF
struct ct_grec {
    u16 used, room;                     /* bytes of b[] used / available */
    u16 off[CT_NCUP][CT_NRD];
    u8 b[];
};
/* CT_ED.pad: bit 0 the season-1 edition as the build drew it (CT_S1), bit 1 the 4.0.6 goals model (every
   edition made since; an older save's plays on to its end as it began), bit 2 the Mod 4.1.1's draws (every
   edition made since: the confederation's club ranking first in the pots, a group stage drawn again while two
   clubs of one association meet; an older save's edition replays as it began) */
#define CT_ED_S1 1
#define CT_ED_G2 2
#define CT_ED_R 4

extern const u8 *CT_SPEC;
extern const u8 *CT_S1;                 /* 4.0.6 B2: the season-1 block ('S1'), or NULL: none (ours) */
extern struct ct_grec *CT_GREC;         /* (NULL: no replay kept) */
extern u16 *CT_LP[CT_NCUP];
extern struct ct_in CT_IN;
extern struct ct_state CT_ST;
extern struct ct_out CT_OUT;
extern struct ct_ed CT_ED;
extern struct ct_seg CT_SEG;

void ct_table(const u8 *diy);
void ct_table_words(const u16 *w, int n);    /* (ours: the final table as team words) */
const u8 *ct_round_spec(int c, int r);       /* (ours: cup c's round r in the spec, 0 past its last) */
/* A group stage's end in the game's contest (the Mod's ct_gpre / ct_gend over our contest as arrays: its
   groups in the game's order -- row index, team word, matches played, points -- and the next stage's list). */
u32 ct_gplace(int c, int ng, int gs, const u16 *word, const u16 *played);
u32 ct_gend(int c, int ng, int gs, u8 *row, const u16 *word, const u16 *pts, u8 *list);
void ct_begin(void);
void ct_country(const u8 *stand, const u8 *cuprec);
void ct_go(u32 manager, u32 seed, u32 uefa);
void ct_finish(void);
void ct_season(void);
void ct_result(u32 v);
void ct_resume(void);
/* 4.1.1: the career club in a knockout round of its cup's game contest (CT_SEG kind 0x40, or a knockout main
   round 0x80): id the slot-3 contest's id, nx the round byte of the club's next slot-3 calendar entry (0xFFFF
   none), won the contest's winner is the club. 1: its results reported, CT_SEG moved. */
u32 ct_gamein(u32 id, u32 nx, u32 won);
u32 ct_contest_id(int c);               /* (ours: the id ct_gamein takes for cup c's game contest) */
u32 ct_new1(u32 mgr);                   /* 4.0.6 B2: the season-1 edition from CT_S1 (0: none) */
u32 ct_load1(u32 mgr);                  /* (after a load: a season-1 save without one) */

#include "conti_season.h"           /* (the season's glue: src/conti_season.c) */

#endif
