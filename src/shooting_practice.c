/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* shooting_practice.c — SHOOTING PRACTICE training mode. See shooting_practice.h. */

#include "shooting_practice.h"
#include "menu_system.h"     /* MenuEntry, CallDrawMenuItem -- the DEFENDERS toggle */
#include "ball_physics.h"    /* GetPlayerPointerFromShirtNumber -- who is a defender */

int32_t g_shootingPracticeMode;

/* The setup screen's DEFENDERS and MIDFIELD rows (menu_data.c
 * _shootingPracticeSetupMenu, ordinals 1 and 2). uint16_t because
 * MENU_CMD_STRING_TABLE reads 2 bytes at the table's indexPtr
 * (_practiceDefendersStrTable / _practiceMidfieldStrTable,
 * swos_amiga_ptr_imports.c). 0 = OFF, the drill as it has always been: that
 * line is stripped. DEFENDERS on keeps the computer's back line (RB, LB, D);
 * MIDFIELD on keeps its middle (M, LW, RW). Attackers are always stripped. */
uint16_t g_shootingPracticeDefenders;
uint16_t g_shootingPracticeMidfield;
/* 0 = attack up (default), 1 = attack down. */
uint16_t g_shootingPracticeAttackDown;

extern uint16_t _team1Computer;
extern uint16_t _team2Computer;
extern uint16_t _numSelectedTeams;
extern TeamFileEx _selectedTeams[];
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

/* The friendly flow this mode borrows whole (src/menus.c) — FriendlyFlow, not
 * FriendlyMenu: this handler runs under the 4-entry PRACTICE submenu since
 * 2026-10-26, where FriendlyMenu's closing CommonMenuExit would write through
 * main-menu entries that are not there (docs/MENUS.md §12). The submenu's
 * opener, PracticeMenuSelected, makes that call once, after it closes. */
extern void FriendlyFlow(void);

/* teams.c's names for TeamFile.teamControls, which has no shared header. */
#define TEAM_NOT_SELECTED 0
#define TEAM_COMPUTER     1

/* ---- The setup screen (menu_data.c _shootingPracticeSetupMenu) -----------
 * SHOOTING PRACTICE on the PRACTICE submenu no longer starts the drill
 * directly: since 2026-10-26 it opens a small setup screen first -- the
 * DEFENDERS OFF/ON toggle, CONTINUE, EXIT -- so the drill's options have a
 * home that is not a stateful button buried in the match flow (the exact trap
 * the old PLAY MATCH toggle was, docs/SHOOTING_PRACTICE.md bug 3). */

extern uint8_t _shootingPracticeSetupMenu[];
extern MenuEntry _menuEntries[];

/* Runtime ordinals of the toggle rows on the setup screen: entry 0 is the
 * title, 1 and 2 the toggles (menu_data.c). */
#define SETUP_DEFENDERS_ORDINAL 1
#define SETUP_MIDFIELD_ORDINAL  2

/* Green (COLOR_GREEN, the ordinary button colour) when OFF, yellow
 * (COLOR_YELLOW) when ON -- the "this one is chosen" colouring. Re-applied by
 * the menu's onInit/onReturn because every ShowMenu re-unpacks the packed
 * bytes, whose baked colour is the OFF green. */
static void setToggleRowColor(MenuEntry *e, uint16_t on)
{
    uint16_t color = on ? 0xF /* COLOR_YELLOW */
                        : 0xE /* COLOR_GREEN */;
    e->bgColor = (uint16_t)((e->bgColor & 0xFFF0) | color);
}

/* The PRACTICE submenu's SHOOTING PRACTICE button: open the setup screen. */
__attribute__((used, externally_visible))
void ShootingPracticeMenu(void)
{
    ShowMenu(_shootingPracticeSetupMenu);
}

/* The setup menu's onInit AND onReturn (menu header, like _offersMenu): the
 * toggles' colours must survive both a fresh open and the return from the
 * friendly flow, each of which re-unpacks the menu. */
__attribute__((used, externally_visible))
void ShootingPracticeSetupInit(void)
{
    setToggleRowColor(&_menuEntries[SETUP_DEFENDERS_ORDINAL], g_shootingPracticeDefenders);
    setToggleRowColor(&_menuEntries[SETUP_MIDFIELD_ORDINAL],  g_shootingPracticeMidfield);
}

/* The toggle rows: flip, recolour, redraw. The text follows on its own --
 * each row draws through its string table, indexed by its flag. */
__attribute__((used, externally_visible))
void ChangePracticeDefenders(MenuEntry *entry)
{
    g_shootingPracticeDefenders ^= 1;
    setToggleRowColor(entry, g_shootingPracticeDefenders);
    CallDrawMenuItem(entry);
}

__attribute__((used, externally_visible))
void ChangePracticeMidfield(MenuEntry *entry)
{
    g_shootingPracticeMidfield ^= 1;
    setToggleRowColor(entry, g_shootingPracticeMidfield);
    CallDrawMenuItem(entry);
}

__attribute__((used, externally_visible))
void ChangePracticeDirection(MenuEntry *entry)
{
    g_shootingPracticeAttackDown ^= 1;
    CallDrawMenuItem(entry);
}

/* CONTINUE -- what the SHOOTING PRACTICE button itself used to be.
 *
 * The whole mode is "a friendly, with the flag set". FriendlyFlow seeds the
 * RNG, saves and restores gameType around the flow and invalidates the team
 * cache so the country lists reload pristine (ShowFriendlyMenu, menus.c, where
 * that last one is a deliberate fix over an original SWOS bug) -- all of which
 * this mode needs exactly as a friendly does, so it reuses them rather than
 * repeating them. The screens it puts up read g_shootingPracticeMode to say
 * PRACTICE where they would say FRIENDLY.
 *
 * Clearing on the way out is what makes the flag safe: every path back to the
 * setup screen, including an abort, returns through here. */
__attribute__((used, externally_visible))
void ShootingPracticeContinue(void)
{
    g_shootingPracticeMode = 1;
    FriendlyFlow();
    g_shootingPracticeMode = 0;
    ShootingPractice_BeginMatch(); /* Clear the latch before menus/attract replays. */
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

/* Latched beside it: whether THIS match keeps the computer's back line and/or
 * its midfield (the setup screen's toggles at the moment of kick-off). */
static int16_t s_keepDefenders;
static int16_t s_keepMidfield;
static int16_t s_attackDown;

static int16_t cpuTeamNum(void)
{
    return s_cpuTeamNum;
}

void ShootingPractice_BeginMatch(void)
{
    s_cpuTeamNum = 0;
    s_keepDefenders = 0;
    s_keepMidfield = 0;
    s_attackDown = 0;
    if (!g_shootingPracticeMode)
        return;
    /* Exactly one side must be the computer. The team selection will not start a
     * drill otherwise (ShootingPractice_SelectionOk), but this must never apply
     * to CPU-v-CPU or human-v-human whatever got us here. */
    if (_team1Computer && !_team2Computer)
        s_cpuTeamNum = 1;
    else if (_team2Computer && !_team1Computer)
        s_cpuTeamNum = 2;
    s_keepDefenders = (g_shootingPracticeDefenders != 0);
    s_keepMidfield  = (g_shootingPracticeMidfield != 0);
    s_attackDown = (g_shootingPracticeAttackDown != 0);
}

int16_t ShootingPractice_IsActive(void)
{
    return s_cpuTeamNum != 0;
}

int16_t ShootingPractice_ForcedTopTeamNum(void)
{
    /* Despite its name, _teamPlayingUp identifies the team occupying the
     * TOP half and attacking DOWN (InitTeamsData / PrepareForInitialKick). */
    return s_cpuTeamNum ? (s_attackDown ? 3 - s_cpuTeamNum : s_cpuTeamNum) : 0;
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
     * leave it alone. 1..10 are the ten outfield players. The setup screen's
     * toggles keep whole lines: with DEFENDERS on a player whose position is
     * RB, LB or D (PlayerInfo.position, via the sprite's playerOrdinal) is not
     * stripped, with MIDFIELD on neither is M, LW or RW — so what stays
     * follows the CPU team's actual line-up. Attackers always go. */
    int16_t i;
    for (i = 1; i <= 10; i++) {
        Sprite *pl = sprites[i];
        if (s_keepDefenders || s_keepMidfield) {
            PlayerInfo *pi = GetPlayerPointerFromShirtNumber(pl, cpuTeam);
            int8_t pos = pi->position;
            if (s_keepDefenders &&
                (pos == POS_RIGHT_BACK || pos == POS_LEFT_BACK || pos == POS_DEFENDER))
                continue;
            if (s_keepMidfield &&
                (pos == POS_MIDFIELDER || pos == POS_LEFT_WING || pos == POS_RIGHT_WING))
                continue;
        }
        pl->sentAway = 1;
        pl->destX    = -20;
        pl->destY    = 449;
        pl->x.s.i    = -20;
        pl->y.s.i    = 449;
    }
}
