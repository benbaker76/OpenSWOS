/*
 * shirt_edit.c -- EDIT CUSTOM TEAMS: a player's shirt number, edited on the screen (include/shirt_edit.h;
 * the SWOS 96/97 Mod 4.1's edteam.py / shirtedit.py, Marco's idea; docs/WORLD_2026_27.md §6j).
 *
 * RIGHT on a player's face button lights his number box (drawn right of the face). Then:
 *   - UP / DOWN: the next / previous number of 1..99 that nobody else in the team wears;
 *   - the number keys: one or two digits (the second within ~1 s), 1..99; a number another player
 *     wears is SWAPPED with him -- never two players with one number;
 *   - FIRE, ENTER, LEFT or RIGHT: done; ESC: the numbers as they were before this edit.
 * The next RIGHT moves on to the name, as before. From the first change the team's numbers stay
 * with its players (SHIRT_NUMBERS_KEPT in the team record's byte 23, as the Mod's benchnum PIN_BIT):
 * a line-up change no longer swaps them (SquadNumbers_Kept, teams.c), and the bench is not
 * renumbered 12..16. The numbers are the records' own, so they go wherever the team does --
 * CUSTOMS.EDT, MY CUSTOM TEAM, a match, online (a team travels as its record).
 *
 * Runs in MenuProc after the controls are read: while the box is lit, the controls are this
 * editor's, and the menu sees none of them. Both targets.
 */
#include "shirt_edit.h"
#include "game_types.h"
#include "menu_system.h"

extern uint8_t   _currentMenu[];
extern MenuEntry _menuEntries[];
extern volatile uint16_t _fire;
extern volatile uint16_t _shortFire;
extern uint8_t   _pl1Fire;
extern uint8_t   _left, _right, _up, _down;
extern int16_t   _menuControlsDirection;
extern int16_t   _menuControlsDirection2;
extern volatile uint16_t _currentKey;
extern TeamFileEx _selectedTeams[];
extern void EditTeamsInit(void);         /* (the editor's onInit: editTeamsMenu) */
extern void CallDrawMenuItem(MenuEntry *entry);

enum {
    kFirstFace = 23,                /* the face buttons: entries 23..38, a player each */
    kPlayers = 16,
    kNumberEntry = 55,              /* (0xC08 / 56: drawn for every row by EditTeamsPlayersBeforeDraw) */
    kDigitWait = 50,                /* menu frames for a second digit (~1 s) */
    kLitColour = 0x0E,              /* the lit box's background (the Mod's HL_COLOR) */
    kDirUp = 0, kDirRight = 2, kDirDown = 4, kDirLeft = 6,
    kKeyEnter = 13, kKeyEsc = 0x1B,
};

static int8_t   s_lit = -1;         /* the player whose number is lit, -1 none */
static int8_t   s_old[kPlayers];    /* the numbers when it was lit (ESC) */
static uint8_t  s_oldKept;
static int16_t  s_wait;             /* frames left for a second digit */
static int8_t   s_first;            /* the first digit typed, -1 none */
static uint8_t  s_hold;             /* after an edit: wait for the controls to be let go */

static TeamFileEx *team(void) { return &_selectedTeams[0]; }

static int32_t in_editor(const Menu *menu)
{
    return menu && menu->onInit == (MenuVoidFn)EditTeamsInit;
}

int32_t ShirtEdit_LitRow(void)
{
    return in_editor((const Menu *)_currentMenu) ? s_lit : -1;
}

uint16_t ShirtEdit_LitColour(void) { return kLitColour; }

/* The whole menu: a number drawn over a wider one would leave its digits showing (the rows have
 * no box of their own to clear). */
static void redraw(int32_t row)
{
    extern void DrawMenu(void);
    (void)row;
    DrawMenu();
}

static void consume(void)
{
    _fire = _shortFire = _pl1Fire = 0;
    _left = _right = _up = _down = 0;
    _menuControlsDirection = _menuControlsDirection2 = -1;
    _currentKey = 0;
}

/* Gives player `row` number n; one who wears it already takes `row`'s. */
static void set_number(int32_t row, int32_t n)
{
    TeamFileEx *t = team();
    const int8_t was = t->players[row].shirtNumber;
    if (n < 1 || n > 99 || n == was)
        return;
    for (int32_t i = 0; i < kPlayers; i++)
        if (i != row && t->players[i].shirtNumber == n) {
            t->players[i].shirtNumber = was;
            redraw(i);
        }
    t->players[row].shirtNumber = (int8_t)n;
    t->team.andWith0xFE |= SHIRT_NUMBERS_KEPT;      /* (from the first change: theirs) */
}

/* The next (step 1) / previous (-1) number nobody else wears. */
static int32_t next_free(int32_t row, int32_t step)
{
    const TeamFileEx *t = team();
    int32_t n = t->players[row].shirtNumber;
    for (int32_t k = 0; k < 99; k++) {
        n += step;
        if (n > 99) n = 1;
        if (n < 1) n = 99;
        int32_t taken = 0;
        for (int32_t i = 0; i < kPlayers; i++)
            if (i != row && t->players[i].shirtNumber == n)
                taken = 1;
        if (!taken)
            return n;
    }
    return t->players[row].shirtNumber;
}

static void finish(int32_t undo)
{
    const int32_t row = s_lit;
    if (undo) {
        TeamFileEx *t = team();
        for (int32_t i = 0; i < kPlayers; i++)
            if (t->players[i].shirtNumber != s_old[i]) {
                t->players[i].shirtNumber = s_old[i];
                if (i != row)
                    redraw(i);
            }
        t->team.andWith0xFE = (uint8_t)((t->team.andWith0xFE & ~SHIRT_NUMBERS_KEPT) | s_oldKept);
    }
    s_lit = -1;
    s_hold = 1;
    redraw(row);
}

int32_t ShirtEdit_Frame(void)
{
    const Menu *menu = (const Menu *)_currentMenu;
    if (!in_editor(menu)) {
        s_lit = -1;
        s_hold = 0;
        return 0;
    }
    const MenuEntry *sel = menu->selectedEntry;
    const int32_t row = sel ? (int32_t)sel->ordinal - kFirstFace : -1;
    const int16_t dir = _menuControlsDirection;
    const uint16_t key = _currentKey;

    if (s_lit < 0) {
        if (s_hold) {                   /* (the confirming press is not the menu's too) */
            if (_fire || _shortFire || _left || _right || dir >= 0) {
                consume();
                return 1;
            }
            s_hold = 0;
        }
        if (row < 0 || row >= kPlayers || dir != kDirRight)
            return 0;
        TeamFileEx *t = team();
        for (int32_t i = 0; i < kPlayers; i++)
            s_old[i] = t->players[i].shirtNumber;
        s_oldKept = (uint8_t)(t->team.andWith0xFE & SHIRT_NUMBERS_KEPT);
        s_lit = (int8_t)row;
        s_first = -1;
        s_wait = 0;
        consume();
        redraw(row);
        return 1;
    }

    if (row != s_lit) {                 /* (the cursor moved some other way: done) */
        finish(0);
        return 0;
    }
    if (s_wait > 0 && --s_wait == 0)
        s_first = -1;
    if (key == kKeyEsc) {
        finish(1);
        consume();
        return 1;
    }
    if (_fire || _shortFire || key == kKeyEnter || dir == kDirLeft || dir == kDirRight) {
        finish(0);
        consume();
        return 1;
    }
    if (dir == kDirUp || dir == kDirDown) {
        set_number(s_lit, next_free(s_lit, dir == kDirUp ? 1 : -1));
        s_first = -1;
    } else if (key >= '0' && key <= '9') {
        const int32_t d = key - '0';
        if (s_first >= 0 && s_wait > 0) {
            set_number(s_lit, s_first * 10 + d);
            s_first = -1;
            s_wait = 0;
        } else {
            set_number(s_lit, d);
            s_first = (int8_t)d;
            s_wait = kDigitWait;
        }
    }
    consume();
    redraw(s_lit);
    return 1;
}
