#ifndef CLASSIC_COMPS_H
#define CLASSIC_COMPS_H

#include "swos_types.h"
#include "menu_system.h"

/* CLASSICS (src/classic_comps.c), the row above WORLD CUP in PRESET COMPETITION's world
 * list (teams.c SelectTeamsFinalMenu): NATIONAL (then UEFA or WORLD CUP) or CLUB, then a
 * competition from that group's classic/
 * folder, then who controls each team. 1 when a competition is loaded, and
 * PresetCompetitionMenu is to start it (StartContest); 0 when the player backed out, to
 * the world list. */
int32_t ClassicComps_Choose(void);

/* 1 once, after ClassicComps_Choose loaded one: the preset chooser has closed and that
 * competition is to be started. */
int32_t ClassicComps_TakeReady(void);

/* The menus' handlers, named in menu_data.c. */
void ClassicComps_PickNational(void);
void ClassicComps_PickClub(void);
void ClassicComps_PickUefa(void);
void ClassicComps_PickWorldCup(void);
void ClassicComps_ListInit(void);
void ClassicComps_PickCompetition(MenuEntry *entry);
void ClassicComps_TeamsInit(void);
void ClassicComps_ToggleTeam(MenuEntry *entry);
void ClassicComps_Play(void);

#endif
