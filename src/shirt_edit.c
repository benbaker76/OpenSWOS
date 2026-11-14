/*
 * shirt_edit.c -- a player's shirt number, edited on the screen (include/shirt_edit.h; the SWOS 96/97
 * Mod 4.1's edteam.py / squadnum.py / shirtedit.py, Marco's idea; docs/WORLD_2026_27.md §6l).
 *
 * Two screens:
 *   - EDIT CUSTOM TEAMS: RIGHT on a player's face button lights his number box (drawn right of
 *     the face; the next RIGHT moves on to the name, as before);
 *   - a career's SQUAD screen (the managed club, not a squad only viewed): LEFT on a player's row
 *     lights his number (LEFT goes nowhere there otherwise).
 * Then:
 *   - UP / DOWN: the next / previous number of 1..99 that nobody else in the team wears;
 *   - the number keys: one or two digits (the second within ~1 s), 1..99; a number another player
 *     wears is SWAPPED with him -- never two players with one number;
 *   - FIRE, ENTER, LEFT or RIGHT: done; ESC: the numbers as they were before this edit.
 * From the first change the team's numbers stay with its players (SHIRT_NUMBERS_KEPT in the team
 * record's byte 23, as the Mod's benchnum PIN_BIT): a line-up change no longer swaps them
 * (SquadNumbers_Kept, teams.c), the bench is not renumbered 12..16, a career club's new player gets
 * a free number. The numbers are the records' own, so they go wherever the team does -- CUSTOMS.EDT,
 * MY CUSTOM TEAM, a career's save, a match, online (a team travels as its record).
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
extern TeamFileEx _careerTeam;
extern uint8_t   _careerNumPlayers[];
extern uint8_t   _currentMatchPlayers[];
extern uintptr_t _squadTeamFilePtr;
extern uint16_t  _isSquadViewMode;
extern uint16_t  _gameType;
extern void EditTeamsInit(void);         /* (the editor's onInit: editTeamsMenu) */
extern void SquadInit(void);             /* (the squad screen's: squadMenu) */
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t *GetPlayerInfoBreakPosition(TeamFileEx *teamPtr, uint16_t requestedSlot,
                                           uint16_t *outSlot, uint16_t *outPlayerIndex);

enum {
    kFirstFace = 23,                /* EDIT TEAMS' face buttons: entries 23..38, a player each */
    kEditPlayers = 16,
    kFirstSquadRow = 28,            /* the squad screen's rows: 28..43 (44, the 17th, has no number) */
    kSquadRows = 16,
    kMaxPlayers = 32,
    kDigitWait = 50,                /* menu frames for a second digit (~1 s) */
    kLitColour = 0x0E,              /* the lit box's background (the Mod's HL_COLOR) */
    kDirUp = 0, kDirRight = 2, kDirDown = 4, kDirLeft = 6,
    kKeyEnter = 13, kKeyEsc = 0x1B,
};

enum { SCREEN_NONE, SCREEN_EDITOR, SCREEN_SQUAD };

static int8_t      s_lit = -1;      /* the screen row whose number is lit, -1 none */
static int8_t      s_litScreen;
static PlayerFile *s_team[kMaxPlayers];   /* the team's players while lit (the number's owner first) */
static int32_t     s_count;
static int8_t      s_old[kMaxPlayers];    /* their numbers when it was lit (ESC) */
static TeamFileEx *s_owner;               /* whose byte 23 keeps the numbers */
static uint8_t     s_oldKept;
static int16_t     s_wait;                /* frames left for a second digit */
static int8_t      s_first;               /* the first digit typed, -1 none */
static uint8_t     s_hold;                /* after an edit: wait for the controls to be let go */

static int32_t screen(const Menu *menu)
{
    if (!menu)
        return SCREEN_NONE;
    if (menu->onInit == (MenuVoidFn)EditTeamsInit)
        return SCREEN_EDITOR;
    if (menu->onInit == (MenuVoidFn)SquadInit && _gameType == 4 && _isSquadViewMode == 0 &&
        (TeamFileEx *)_squadTeamFilePtr == &_careerTeam)
        return SCREEN_SQUAD;
    return SCREEN_NONE;
}

int32_t ShirtEdit_LitRow(void)
{
    return screen((const Menu *)_currentMenu) == SCREEN_EDITOR && s_litScreen == SCREEN_EDITOR ? s_lit : -1;
}

int32_t ShirtEdit_LitSquadRow(void)
{
    return screen((const Menu *)_currentMenu) == SCREEN_SQUAD && s_litScreen == SCREEN_SQUAD ? s_lit : -1;
}

uint16_t ShirtEdit_LitColour(void) { return kLitColour; }

/* The whole menu: a number drawn over a wider one would leave its digits showing (the rows have
 * no box of their own to clear). */
static void redraw(void)
{
    extern void DrawMenu(void);
    DrawMenu();
}

static void consume(void)
{
    _fire = _shortFire = _pl1Fire = 0;
    _left = _right = _up = _down = 0;
    _menuControlsDirection = _menuControlsDirection2 = -1;
    _currentKey = 0;
}

/* The players whose numbers must differ, the lit one first: 0 when the row has none. */
static int32_t collect(int32_t scr, int32_t row)
{
    s_count = 0;
    if (scr == SCREEN_EDITOR) {
        TeamFileEx *t = &_selectedTeams[0];
        s_owner = t;
        s_team[s_count++] = &t->players[row];
        for (int32_t i = 0; i < kEditPlayers; i++)
            if (i != row)
                s_team[s_count++] = &t->players[i];
        return 1;
    }
    /* a career's managed club: every player of its squad (the reserves too) */
    TeamFileEx *t = &_careerTeam;
    const uint16_t team = (uint16_t)((t->team.countryNumber << 8) | t->team.teamOrdinal);
    uint16_t slot, file;
    uint8_t *raw = GetPlayerInfoBreakPosition(t, (uint16_t)row, &slot, &file);
    if (!raw)
        return 0;
    PlayerFile *lit = (PlayerFile *)(raw + sizeof(TeamFile));
    uint16_t n = *(uint16_t *)_careerNumPlayers;     /* (a word: declared bytes) */
    if (n > kMaxPlayers)
        n = kMaxPlayers;
    s_owner = t;
    s_team[s_count++] = lit;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t s = _currentMatchPlayers[i];
        if (s >= kMaxPlayers)
            continue;
        PlayerFile *p = (PlayerFile *)(GetPlayer(team, s) + sizeof(TeamFile));
        if (p != lit && s_count < kMaxPlayers)
            s_team[s_count++] = p;
    }
    return 1;
}

/* The lit player gets number n; one who wears it already takes his. */
static void set_number(int32_t n)
{
    PlayerFile *me = s_team[0];
    const int8_t was = me->shirtNumber;
    if (n < 1 || n > 99 || n == was)
        return;
    for (int32_t i = 1; i < s_count; i++)
        if (s_team[i]->shirtNumber == n)
            s_team[i]->shirtNumber = was;
    me->shirtNumber = (int8_t)n;
    s_owner->team.andWith0xFE |= SHIRT_NUMBERS_KEPT;   /* (from the first change: theirs) */
}

/* The next (step 1) / previous (-1) number nobody else wears. */
static int32_t next_free(int32_t step)
{
    int32_t n = s_team[0]->shirtNumber;
    for (int32_t k = 0; k < 99; k++) {
        n += step;
        if (n > 99) n = 1;
        if (n < 1) n = 99;
        int32_t taken = 0;
        for (int32_t i = 1; i < s_count; i++)
            if (s_team[i]->shirtNumber == n)
                taken = 1;
        if (!taken)
            return n;
    }
    return s_team[0]->shirtNumber;
}

static void finish(int32_t undo)
{
    if (undo) {
        for (int32_t i = 0; i < s_count; i++)
            s_team[i]->shirtNumber = s_old[i];
        s_owner->team.andWith0xFE = (uint8_t)((s_owner->team.andWith0xFE & ~SHIRT_NUMBERS_KEPT) | s_oldKept);
    }
    s_lit = -1;
    s_hold = 1;
    redraw();
}

int32_t ShirtEdit_Frame(void)
{
    const Menu *menu = (const Menu *)_currentMenu;
    const int32_t scr = screen(menu);
    if (scr == SCREEN_NONE) {
        s_lit = -1;
        s_hold = 0;
        return 0;
    }
    const MenuEntry *sel = menu->selectedEntry;
    const int32_t first = scr == SCREEN_EDITOR ? kFirstFace : kFirstSquadRow;
    const int32_t rows = scr == SCREEN_EDITOR ? kEditPlayers : kSquadRows;
    const int32_t row = sel ? (int32_t)sel->ordinal - first : -1;
    const int16_t dir = _menuControlsDirection;
    const uint16_t key = _currentKey;
    const int16_t open = scr == SCREEN_EDITOR ? kDirRight : kDirLeft;

    if (s_lit < 0) {
        if (s_hold) {                   /* (the confirming press is not the menu's too) */
            if (_fire || _shortFire || _left || _right || dir >= 0) {
                consume();
                return 1;
            }
            s_hold = 0;
        }
        if (row < 0 || row >= rows || dir != open || !collect(scr, row))
            return 0;
        for (int32_t i = 0; i < s_count; i++)
            s_old[i] = s_team[i]->shirtNumber;
        s_oldKept = (uint8_t)(s_owner->team.andWith0xFE & SHIRT_NUMBERS_KEPT);
        s_lit = (int8_t)row;
        s_litScreen = (int8_t)scr;
        s_first = -1;
        s_wait = 0;
        consume();
        redraw();
        return 1;
    }

    if (row != s_lit || scr != s_litScreen) {   /* (the cursor moved some other way: done) */
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
        set_number(next_free(dir == kDirUp ? 1 : -1));
        s_first = -1;
    } else if (key >= '0' && key <= '9') {
        const int32_t d = key - '0';
        if (s_first >= 0 && s_wait > 0) {
            set_number(s_first * 10 + d);
            s_first = -1;
            s_wait = 0;
        } else {
            set_number(d);
            s_first = (int8_t)d;
            s_wait = kDigitWait;
        }
    }
    consume();
    redraw();
    return 1;
}
