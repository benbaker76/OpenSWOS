/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* shooting_practice.c — SHOOTING PRACTICE training mode. See shooting_practice.h. */

#include "shooting_practice.h"

int32_t g_playMatchShootingPractice;

extern uint16_t _team1Computer;
extern uint16_t _team2Computer;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

/* 0 = inactive, else the computer side's team number (1 or 2), LATCHED for the
 * whole match by ShootingPractice_BeginMatch.
 *
 * It is latched rather than recomputed because the match must not depend on a
 * menu global that outlives the menu: UpdateShootingPracticeButton clears
 * g_playMatchShootingPractice whenever the button is not on screen, so reading
 * it live meant the restart after a goal could find the mode "off" and hand the
 * kickoff back to a side that has no outfield players to take it. */
static int16_t s_cpuTeamNum;

static int16_t cpuTeamNum(void)
{
    return s_cpuTeamNum;
}

void ShootingPractice_BeginMatch(void)
{
    s_cpuTeamNum = 0;
    if (!g_playMatchShootingPractice)
        return;
    /* Exactly one side must be the computer — the menu only offers the toggle
     * in that case, but a stale ON must never apply to CPU-v-CPU or human-v-human. */
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
