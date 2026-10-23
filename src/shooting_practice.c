/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* shooting_practice.c — SHOOTING PRACTICE training mode. See shooting_practice.h. */

#include "shooting_practice.h"

int32_t g_shootingPracticeMode;

extern uint16_t _team1Computer;
extern uint16_t _team2Computer;
extern uint16_t _numSelectedTeams;
extern TeamFileEx _selectedTeams[];
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

/* The friendly flow this mode borrows whole (src/menus.c). */
extern void FriendlyMenu(void);

/* teams.c's names for TeamFile.teamControls, which has no shared header. */
#define TEAM_NOT_SELECTED 0
#define TEAM_COMPUTER     1

/* ShootingPracticeMenu -- the main menu's SHOOTING PRACTICE button (entry 16).
 *
 * The whole mode is "a friendly, with the flag set". FriendlyMenu seeds the
 * RNG, saves and restores gameType around the flow and invalidates the team
 * cache so the country lists reload pristine (ShowFriendlyMenu, menus.c, where
 * that last one is a deliberate fix over an original SWOS bug) -- all of which
 * this mode needs exactly as a friendly does, so it reuses them rather than
 * repeating them. The screens it puts up read g_shootingPracticeMode to say
 * PRACTICE where they would say FRIENDLY.
 *
 * Clearing on the way out is what makes the flag safe: every path back to the
 * main menu, including an abort, returns through here. */
__attribute__((used, externally_visible))
void ShootingPracticeMenu(void)
{
    g_shootingPracticeMode = 1;
    FriendlyMenu();
    g_shootingPracticeMode = 0;
}

int16_t ShootingPractice_SelectionOk(void)
{
    int16_t cpu = 0;
    int16_t human = 0;
    int16_t i;

    for (i = 0; i < (int16_t)_numSelectedTeams; i++) {
        uint8_t ctrl = _selectedTeams[i].team.teamControls;
        if (ctrl == TEAM_COMPUTER)
            cpu++;
        else if (ctrl != TEAM_NOT_SELECTED)
            human++;      /* PLAYER-COACH or COACH: a side somebody is running */
    }
    return (cpu == 1 && human == 1);
}

/* 0 = inactive, else the computer side's team number (1 or 2), LATCHED for the
 * whole match by ShootingPractice_BeginMatch.
 *
 * It is latched rather than recomputed because the match must not depend on a
 * menu global at all: reading the mode live meant the restart after a goal
 * could find it "off" and hand the kickoff back to a side that has no outfield
 * players to take it. That was a real bug when the mode was a menu toggle that
 * cleared itself; the latch is kept because it is the right shape regardless --
 * what the match plays by is decided once, at kick-off. */
static int16_t s_cpuTeamNum;

static int16_t cpuTeamNum(void)
{
    return s_cpuTeamNum;
}

void ShootingPractice_BeginMatch(void)
{
    s_cpuTeamNum = 0;
    if (!g_shootingPracticeMode)
        return;
    /* Exactly one side must be the computer. The team selection will not start a
     * drill otherwise (ShootingPractice_SelectionOk), but this must never apply
     * to CPU-v-CPU or human-v-human whatever got us here. */
    if (_team1Computer && !_team2Computer)
        s_cpuTeamNum = 1;
    else if (_team2Computer && !_team1Computer)
        s_cpuTeamNum = 2;
}

int16_t ShootingPractice_ForcedKickoffTeamNum(void)
{
    int16_t cpu = cpuTeamNum();
    return cpu ? (int16_t)(3 - cpu) : 0;
}

void ShootingPractice_StripCpuOutfieldPlayers(void)
{
    int16_t cpu = cpuTeamNum();
    if (!cpu)
        return;

    TeamGeneralInfo *cpuTeam = (_topTeamData.teamNumber == cpu) ? &_topTeamData : &_bottomTeamData;
    Sprite * const *sprites = cpuTeam->spritesTable;

    /* index 0 is the goalkeeper (goalkeeper.c: "first sprite = goalkeeper") —
     * leave it alone. 1..10 are the ten outfield players. */
    int16_t i;
    for (i = 1; i <= 10; i++) {
        Sprite *pl = sprites[i];
        pl->sentAway = 1;
        pl->destX    = -20;
        pl->destY    = 449;
        pl->x.s.i    = -20;
        pl->y.s.i    = 449;
    }
}
