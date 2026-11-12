/*
 * cpu_repl.h -- CPU replacements from the squad (src/cpu_repl.c; docs/WORLD_2026_27.md, "The Mod
 * 4.1's market"): the SWOS 96/97 Mod 4.1's tools/cpurefill.py (Davide, after Nicola's purchases).
 *
 * A player bought from one of a computer club's 16 leaves a hole the game fills at once with an
 * INVENTED player. With CPU REPL.: FROM SQUAD (BUY PLAYER's button right of EXIT, saved with the
 * career, INVENTED for a new career and an older save) one of the club's own squad players from
 * place 17 on takes the hole instead: the most valuable of the same role (goalkeeper; defender
 * RB LB D; midfielder RW LW M; forward), else the most valuable outfield player -- a goalkeeper
 * only for a goalkeeper and never for an outfield place; the lowest place on a tie; nobody whose
 * name is already in the 16; nobody already used. Nobody fits: the invented player stays.
 *
 * World 2026-27 only, and only where the world's team files carry the clubs' squad players 17-30
 * ("extra_players", scripts/world_import.py); SDL only (the Amiga's team files hold 16).
 */
#ifndef CPU_REPL_H
#define CPU_REPL_H

#include "swos_types.h"

#define CPU_REPL_CLUBS 64               /* clubs whose squad players were used (oldest dropped) */

typedef struct {
    uint8_t  fromSquad;                 /* 1: FROM SQUAD */
    uint8_t  n;                         /* used[] entries */
    struct {
        uint16_t team;                  /* the club, as the game packs it (file << 8 | ordinal) */
        uint16_t mask;                  /* its squad players 17.. already used (bit 0: the 17th) */
    } used[CPU_REPL_CLUBS];
} CpuReplState;
extern CpuReplState g_cpuRepl;

void    CpuRepl_NewCareer(void);
void    CpuRepl_LoadedWithout(void);    /* a save without the item: INVENTED, nothing used */
int32_t CpuRepl_Shown(void);            /* BUY PLAYER shows the button */
void    CpuRepl_MenuInit(void);         /* BuyPlayerInit: the button (entry 9) */
/* After the game made the invented player in the seller's slot (Career_RemovePlayerAndAddAsTrialist):
 * FROM SQUAD puts the chosen squad player there instead. 1: replaced. */
int32_t CpuRepl_AfterSale(uint16_t team, uint16_t slot);

#endif
