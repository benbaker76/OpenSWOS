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

extern uint8_t _challengeMenu[];
extern uint16_t _quitToDos, _fadeInitFlag;
extern volatile uint16_t _fadeState;
extern void ResetMenuViewState(void);
extern void PrepareMenuForFadeIn(void);
extern int16_t SelectFileOfExtension(uint32_t ext, uint8_t *title);
extern uint8_t _selectedFilename[];

enum { HUB, LIST, DETAILS, RESULT };
enum { BACK = 1, COLLECTION, SAVED, PLAY, RETRY, NEXT, RETRY_SAVE, PICK = 100 };
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
        actions[i] = 0;
    }
    if (screen == HUB) {
        row(0, 0, "CHALLENGES", 0, 0x17);
        row(2, 60, "CHALLENGE COLLECTION", COLLECTION, 14);
        row(3, 90, "SAVED POSITIONS", SAVED, 14);
        row(11, 185, "BACK", BACK, 12);
    } else if (screen == LIST) {
        static const int ord[] = {3, 4, 6, 7, 9, 10};
        static const int ys[] = {50, 65, 96, 111, 142, 157};
        row(0, 0, "CHALLENGE COLLECTION", 0, 0x17);
        swos_io_snprintf(buf, sizeof buf, "%d OF 6 COMPLETED", ChallengeProgress_Count());
        row(1, 20, ChallengeProgress_Count() == 6 ? "ALL CHALLENGES COMPLETED" : buf, 0, 0);
        row(2, 36, "EASY", 0, 0);
        row(5, 82, "MEDIUM", 0, 0);
        row(8, 128, "HARD", 0, 0);
        for (i = 0; i < COLLECTION_COUNT; ++i) {
            swos_io_snprintf(buf, sizeof buf, "%s   %s", g_challengeCollection[i].title,
                ChallengeProgress_Done(i) ? "DONE" : "NOT DONE");
            row(ord[i], ys[i], buf, PICK + i, ChallengeProgress_Done(i) ? 14 : 13);
        }
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
        row(6, 124, "A DRAW AT FULL TIME FAILS", 0, 0);
        row(7, 143, ChallengeProgress_Done(selected) ? "DONE" : "NOT DONE", 0, 0);
        row(8, 163, "PLAY", PLAY, 14);
        row(11, 185, "BACK", BACK, 12);
        row(12, 207, notice, 0, 0);
    } else {
        int won = ChallengeProgress_Result() == COLLECTION_WON;
        row(0, 0, won ? "CHALLENGE COMPLETED" : "CHALLENGE NOT COMPLETED", 0, 0x17);
        row(1, 25, c->title, 0, 0);
        row(2, 46, c->teams, 0, 0);
        swos_io_snprintf(buf, sizeof buf, "FINAL SCORE %u-%u",
            ChallengeProgress_Goals(1), ChallengeProgress_Goals(2));
        row(3, 65, ChallengeProgress_Result() == COLLECTION_ABORTED ? "MATCH LEFT EARLY" : buf, 0, 0);
        row(4, 84, "OBJECTIVE: WIN THE MATCH", 0, 0);
        row(5, 103, ChallengeProgress_Count() == 6 ? "ALL CHALLENGES COMPLETED" : save_status(), 0, 0);
        row(6, 127, "RETRY", RETRY, 14);
        if (won && selected + 1 < COLLECTION_COUNT) row(7, 147, "NEXT CHALLENGE", NEXT, 14);
        row(11, 185, "CHALLENGE LIST", BACK, 12);
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
    restore_menu();
}
