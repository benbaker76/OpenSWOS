/* Classic packed menu shared by SDL and Amiga. Each screen is unpacked afresh;
 * match play runs after ShowMenu restores the main menu, just like saved positions. */
#include "challenge_collection.h"
#include "challenge.h"
#include "menu_system.h"
#include "menus.h"
#include "swos_io.h"
#include "dos_mode.h"
#include "dos_gameplay.h"
#include "test_rules.h"
#include "sprites.h"

extern uint8_t _challengeMenu[], _currentMenu[];
extern uint16_t _quitToDos, _fadeInitFlag;
extern volatile uint16_t _fadeState;
extern void ResetMenuViewState(void);
extern void PrepareMenuForFadeIn(void);
extern int16_t SelectFileOfExtension(uint32_t ext, uint8_t *title);
extern uint8_t _selectedFilename[];
extern uint8_t *_spriteGraphicsPtr;

enum { HUB, LIST, DETAILS, RESULT };
enum { BACK = 1, COLLECTION, SAVED, PLAY, RETRY, NEXT, RETRY_SAVE, LEAVE, PICK = 100 };
static int screen, selected, action, actions[13];
static int listFocus = 3;
static char text[13][80];
static const char *notice = "";

static void row(int n, int y, const char *label, int command, int color)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)n);
    swos_io_snprintf(text[n], sizeof text[n], "%s", label);
    e->fg = (uintptr_t)text[n];
    e->x = 12; e->y = (uint16_t)y;
    e->dwWidth = 280; e->dwHeight = 14;
    e->isInvisible = 0;
    e->isDisabled = command == 0;
    e->bgColor = (uint16_t)color;
    actions[n] = command;
}

/* The list rows wear the squad screen's own art: its small green tick (the tactics
 * "better" mark) when done, and the value stars a national team's players show
 * (InitPlayerValueDisplayStruct: ten frames from 113, half a star apart). */
enum { SPR_STARS_HALF = 113, SPR_TICK_SMALL = 208 };
static const int listOrd[COLLECTION_COUNT] = {3, 4, 6, 7, 9, 10};

/* Under the list: two story lines and the starting position for the highlighted
 * row, in the spare entries the old EASY/MEDIUM/HARD headings used. */
enum { DESC_LINE1 = 2, DESC_LINE2 = 5, DESC_SUMMARY = 8, DESC_YELLOW = 15 };
static int shownDesc = -1;

static void set_description(int i, int redraw)
{
    static const int lines[3] = {DESC_LINE1, DESC_LINE2, DESC_SUMMARY};
    const CollectionChallenge *c = &g_challengeCollection[i];
    const char *score = c->start, *p;
    int minute = 0, n;
    for (p = c->start; *p >= '0' && *p <= '9'; ++p) minute = minute * 10 + (*p - '0');
    for (p = c->start; *p; ++p) if (*p == ' ') score = p + 1;
    shownDesc = i;
    swos_io_snprintf(text[DESC_LINE1], sizeof text[0], "%s", c->story[0]);
    swos_io_snprintf(text[DESC_LINE2], sizeof text[0], "%s", c->story[1]);
    swos_io_snprintf(text[DESC_SUMMARY], sizeof text[0], "* %s, %d MIN, %s *", score, minute, c->restart);
    if (redraw)
        for (n = 0; n < 3; ++n) CallDrawMenuItem(CalcMenuEntryAddress((uint16_t)lines[n]));
}

static void desc_row(int n, int y, int color)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)n);
    row(n, y, "", 0, 0);
    e->fg = (uintptr_t)text[n];
    e->dwHeight = 9;
    e->stringFlags = (uint16_t)((e->stringFlags & ~0xF) | color);
}

static void draw_row_marks(MenuEntry *e)
{
    SpriteGraphics **gfx = (SpriteGraphics **)_spriteGraphicsPtr;
    SpriteGraphics *spr;
    int i, frame;
    for (i = 0; i < COLLECTION_COUNT && listOrd[i] != e->ordinal; ++i)
        ;
    if (i == COLLECTION_COUNT) return;
    if (ChallengeProgress_Done(i)) {
        spr = gfx[SPR_TICK_SMALL];
        DrawSprite16Pixels(SPR_TICK_SMALL, e->x + 4, e->y + (e->dwHeight - spr->nlines) / 2);
    }
    frame = SPR_STARS_HALF + 2 * g_challengeCollection[i].stars - 1;
    spr = gfx[frame];
    DrawSprite16Pixels((uint16_t)frame, e->x + e->dwWidth - 4 - spr->pixWidth,
                       e->y + (e->dwHeight - spr->nlines) / 2);
    /* Moving the cursor redraws the row it left, after the selection has changed,
     * so this is where the description follows it (MenuCheckControls). */
    for (i = 0; i < COLLECTION_COUNT; ++i)
        if (CalcMenuEntryAddress((uint16_t)listOrd[i]) == ((Menu *)_currentMenu)->selectedEntry
            && i != shownDesc)
            set_description(i, 1);
}

static const char *save_status(void)
{
    if (ChallengeProgress_Dirty()) return "PROGRESS NOT SAVED";
    if (ChallengeProgress_Recovered()) return "PROGRESS FILE DAMAGED - CHECK SAVE DISK";
    return notice;
}

void ChallengeCollection_MenuInit(void)
{
    int i, first = -1, prev = -1;
    char buf[80];
    const CollectionChallenge *c = &g_challengeCollection[selected];
    for (i = 0; i < 13; ++i) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)i);
        e->isInvisible = e->isDisabled = 1;
        e->leftEntry = e->rightEntry = e->upEntry = e->downEntry = -1;
        e->afterDrawCb = 0;
        actions[i] = 0;
    }
    if (screen == HUB) {
        row(0, 0, "CHALLENGES", 0, 0x17);
        row(2, 60, "CHALLENGE COLLECTION", COLLECTION, 14);
        row(3, 90, "SAVED POSITIONS", SAVED, 14);
        row(11, 185, "BACK", BACK, 12);
    } else if (screen == LIST) {
        static const int ys[] = {38, 54, 70, 86, 102, 118};
        row(0, 0, "CHALLENGE COLLECTION", 0, 0x17);
        swos_io_snprintf(buf, sizeof buf, "%d OF 6 COMPLETED", ChallengeProgress_Count());
        row(1, 20, ChallengeProgress_Count() == 6 ? "ALL CHALLENGES COMPLETED" : buf, 0, 0);
        for (i = 0; i < COLLECTION_COUNT; ++i) {
            row(listOrd[i], ys[i], g_challengeCollection[i].title, PICK + i,
                ChallengeProgress_Done(i) ? 14 : 13);
            CalcMenuEntryAddress((uint16_t)listOrd[i])->afterDrawCb = draw_row_marks;
        }
        desc_row(DESC_LINE1, 140, 0);
        desc_row(DESC_LINE2, 150, 0);
        desc_row(DESC_SUMMARY, 163, DESC_YELLOW);
        for (i = 0; i < COLLECTION_COUNT && listOrd[i] != listFocus; ++i)
            ;
        set_description(i < COLLECTION_COUNT ? i : 0, 0);
        row(11, 185, "BACK", BACK, 12);
        if (ChallengeProgress_Dirty()) row(12, 207, "PROGRESS NOT SAVED - RETRY SAVE", RETRY_SAVE, 12);
        else if (save_status()[0]) row(12, 207, save_status(), 0, 0);
    } else if (screen == DETAILS) {
        row(0, 0, c->title, 0, 0x17);
        row(1, 24, c->teams, 0, 0);
        row(2, 43, c->start, 0, 0);
        swos_io_snprintf(buf, sizeof buf, "YOU CONTROL %s", c->human);
        row(3, 62, buf, 0, 0);
        row(4, 81, c->restart, 0, 0);
        row(5, 105, "WIN THE MATCH", 0, 14);
        row(6, 124, "DRAW: EXTRA TIME, THEN PENALTIES", 0, 0);
        row(7, 143, ChallengeProgress_Done(selected) ? "DONE" : "NOT DONE", 0, 0);
        row(8, 163, "PLAY", PLAY, 14);
        row(11, 185, "BACK", BACK, 12);
        row(12, 207, notice, 0, 0);
    } else {
        int won = ChallengeProgress_Result() == COLLECTION_WON;
        MenuEntry *e;
        row(0, 0, c->title, 0, 0x17);
        row(1, 25, c->teams, 0, 0);
        swos_io_snprintf(buf, sizeof buf, "FINAL SCORE %u-%u%s",
            ChallengeProgress_Goals(1), ChallengeProgress_Goals(2),
            ChallengeProgress_Goals(1) != ChallengeProgress_Goals(2) ? ""
                : won ? ", WON ON PENALTIES" : ", LOST ON PENALTIES");
        row(2, 46, ChallengeProgress_Result() == COLLECTION_ABORTED ? "MATCH LEFT EARLY" : buf, 0, 0);
        row(3, 65, "OBJECTIVE: WIN THE MATCH", 0, 0);
        row(4, 84, ChallengeProgress_Count() == 6 ? "ALL CHALLENGES COMPLETED" : save_status(), 0, 0);
        /* At the bottom: the verdict on its own (red failed, green completed), then
         * RETRY beside CHALLENGE LIST, then EXIT back to the main menu. */
        i = won && selected + 1 < COLLECTION_COUNT;     /* NEXT CHALLENGE under it */
        row(5, i ? 118 : 138, won ? "CHALLENGE COMPLETED" : "CHALLENGE FAILED", 0, won ? 14 : 10);
        CalcMenuEntryAddress(5)->dwHeight = 20;
        if (i) row(7, 145, "NEXT CHALLENGE", NEXT, 14);
        row(6, 165, "RETRY", RETRY, 14);
        CalcMenuEntryAddress(6)->dwWidth = 138;
        row(10, 165, "CHALLENGE LIST", BACK, 13);   /* the list rows' blue */
        e = CalcMenuEntryAddress(10); e->x = 154; e->dwWidth = 138;
        row(11, 185, "EXIT", LEAVE, 12);
        if (ChallengeProgress_Dirty()) row(12, 207, "PROGRESS NOT SAVED - RETRY SAVE", RETRY_SAVE, 12);
    }
    for (i = 0; i < 13; ++i) if (actions[i]) {
        if (first < 0) first = i;
        if (prev >= 0) {
            CalcMenuEntryAddress((uint16_t)prev)->downEntry = (int8_t)i;
            CalcMenuEntryAddress((uint16_t)i)->upEntry = (int8_t)prev;
        }
        prev = i;
    }
    if (screen == RESULT) {
        /* RETRY and CHALLENGE LIST share a row; the chain above links top to bottom. */
        MenuEntry *retry = CalcMenuEntryAddress(6), *list = CalcMenuEntryAddress(10),
                  *leave = CalcMenuEntryAddress(11);
        int above = actions[7] ? 7 : -1;
        retry->rightEntry = 10; list->leftEntry = 6;
        retry->upEntry = list->upEntry = (int8_t)above;
        retry->downEntry = list->downEntry = 11;
        leave->upEntry = 6;
        if (above >= 0) CalcMenuEntryAddress(7)->upEntry = -1;
        if (above >= 0) CalcMenuEntryAddress(7)->downEntry = 6;
    }
    SetCurrentEntry((uint16_t)(screen == LIST && actions[listFocus] ? listFocus : first));
}

void ChallengeCollection_MenuSelect(void *entry)
{
    MenuEntry *e = (MenuEntry *)entry;
    if (e->ordinal >= 13) return;
    action = actions[e->ordinal];
    if (screen == LIST) listFocus = e->ordinal;
    if (action) SetExitMenuFlag();
}

static void restore_menu(void)
{
    CommonMenuExit();
    _fadeState = 0;
    _fadeInitFlag = 0x03E7u;
    ResetMenuViewState();
    PrepareMenuForFadeIn();
}

static void play_selected(void)
{
    int gameplay = _gDosGameplay, view = _gDosView;
    /* Collection assets use standard Amiga rules. Keep preferences intact and
     * apply these only for this attempt, including the loader's rules check. */
    _gDosGameplay = _gDosView = 0;
    TestRules_Force(0);
    if (Challenge_ArmBundled(g_challengeCollection[selected].asset) != 0) {
        notice = "CHALLENGE MISSING OR INCOMPATIBLE";
        screen = DETAILS;
    } else {
        ChallengeProgress_Begin(selected, Challenge_ArmedInfo()->humanSide);
        Challenge_Play();
        ChallengeProgress_End();
        notice = ChallengeProgress_Result() == COLLECTION_WON ? "PROGRESS SAVED" : "";
        screen = RESULT;
        restore_menu();
    }
    TestRules_Force(-1);
    _gDosGameplay = gameplay;
    _gDosView = view;
    ApplyGameStyle();
}

void ChallengeCollection_Show(void)
{
    ChallengeProgress_Load();
    screen = HUB;
    notice = "";
    while (!_quitToDos) {
        action = 0;
        ShowMenu(_challengeMenu);
        if (_quitToDos) break;
        if (action == LEAVE) break;
        if (!action || action == BACK) {
            if (screen == HUB) break;
            screen = screen == LIST ? HUB : LIST;
            notice = "";
        } else if (action == COLLECTION) screen = LIST;
        else if (action == SAVED) {
            Challenge_EnsureSample();
            if (SelectFileOfExtension(0x2E43484Cu, (uint8_t *)"SAVED POSITIONS") == 0) {
                if (Challenge_Arm(_selectedFilename) == 0) {
                    Challenge_Play();
                    restore_menu();
                } else {
                    extern void ShowErrorMenu(uint8_t *text);
                    ShowErrorMenu((uint8_t *)"POSITION MISSING OR INCOMPATIBLE");
                }
            }
        } else if (action >= PICK && action < PICK + COLLECTION_COUNT) {
            selected = action - PICK;
            notice = "";
            screen = DETAILS;
        } else if (action == PLAY || action == RETRY) play_selected();
        else if (action == NEXT && selected + 1 < COLLECTION_COUNT) {
            ++selected;
            notice = "";
            screen = DETAILS;
        } else if (action == RETRY_SAVE) {
            notice = ChallengeProgress_Save() ? "PROGRESS NOT SAVED" : "PROGRESS SAVED";
        }
    }
    /* Every match above already repainted the menus after it (restore_menu). Leaving
     * is an ordinary submenu exit, as EDIT and PRACTICE make: the full restore's
     * buffer swap showed a frame of stale back buffer on the Amiga. */
    CommonMenuExit();
}
