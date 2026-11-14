/*
 * conti_view.h -- the continental cups' views of a world career (src/conti_view.c; SDL builds,
 * the Amiga's calls do nothing): CONTINENTAL CUPS (VIEW COMPETITIONS), VIEW WORLD's cup rows.
 * docs/WORLD_2026_27.md 6m.
 */
#ifndef CONTI_VIEW_H
#define CONTI_VIEW_H

#include "swos_types.h"

#define CONTI_CONFS_MENU_NAME  "openswos/contiConfsMenu"
#define CONTI_ROUNDS_MENU_NAME "openswos/contiRoundsMenu"
#define CONTI_ROUND_MENU_NAME  "openswos/contiRoundMenu"
#define CONTI_CONFS_MENU_ENTRIES  9
#define CONTI_ROUNDS_MENU_ENTRIES 20
#define CONTI_ROUND_MENU_ENTRIES  150

void ContiView_Init(void);              /* (the JSON menus' handlers) */
int32_t ContiView_Available(void);      /* the world plays other confederations' cups (SDL) */
void ContiView_ShowConfs(void);         /* CONTINENTAL CUPS */
/* VIEW WORLD: a continent's (CN_*, 254 the world) cups, their indices into World_ContiCups; their
 * names; one shown on its round being played. */
int32_t ContiView_CupsOfContinent(uint8_t continent, uint8_t *out, int32_t max);
const char *ContiView_CupName(int32_t cup);
void ContiView_ShowCup(int32_t cup);
int32_t ContiView_SelfTest(void);
/* STAT > SUMMARY: the career club's European cup's record, a conti cup's rounds (0: none for the contest viewed) */
int32_t ContiView_StatSummaryAvailable(void);
void ContiView_ShowStatSummary(void);       /* --conti-view-selftest */

#endif
